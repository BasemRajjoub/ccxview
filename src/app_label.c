/* app_label.c -- labels on the model (label.h): what to label for the chosen kind, the
   font atlas, and the per-frame thinning, layout and upload. Only what can be seen gets
   a label: skin nodes, exterior faces, shown elements. Each label has a key (kind, id)
   that a label moved by hand is found by (app_labelpos.c); the boxes drawn are kept
   for the hit test that lets a press drag one. */
#include "app_int.h"
#include "label.h"
#include "ui.h"
#include "nk.h"
#include <math.h>
#include <strings.h>

extern const unsigned char cv_font_text[];
extern const unsigned cv_font_text_size;

static const struct { const char *key, *name; } kinds[CV_LABEL_N] = {
    { "none", "none" }, { "node", "node id" }, { "elem", "element id" }, { "value", "node value" },
    { "evalue", "element value" }, { "sets", "set and surface names" }, { "links", "couplings, rigid bodies" },
    { "loads", "loads" }, { "supports", "supports" }, { "materials", "materials" },
    { "gpvalue", "Gauss point value" }, { "gpid", "Gauss point id" }, { "minmax", "min / max" },
};
const char* app_label_name(int k) { return k >= 0 && k < CV_LABEL_N ? kinds[k].name : "?"; }
const char* app_label_key(int k) { return k >= 0 && k < CV_LABEL_N ? kinds[k].key : "none"; }
int app_label_parse(const char* keys) {
    int mask = 0;
    for (const char* a = keys; *a;) {
        const char* e = strchr(a, ',');
        size_t len = e ? (size_t)(e - a) : strlen(a);
        int found = -1;
        for (int k = 1; k < CV_LABEL_N; k++) if (strlen(kinds[k].key) == len && !strncasecmp(kinds[k].key, a, len)) found = k;
        if (found < 0) return 0;
        mask |= 1 << found;
        a += len + (e ? 1 : 0);
    }
    return mask;
}

/* ---- the font: Inter baked at the label size, kept as metrics ------------------------ */
static struct {
    float px;
    struct nk_font_atlas atlas;
    bool live;
    cv_label_glyph g[95];               /* codepoints 32 .. 126 */
    cv_label_metrics m;
} F;

static void font_bake(float px) {
    if (F.live) { nk_font_atlas_clear(&F.atlas); F.live = false; }
    nk_font_atlas_init_default(&F.atlas);
    nk_font_atlas_begin(&F.atlas);
    static const nk_rune range[] = { 32, 126, 0 };
    struct nk_font_config cfg = nk_font_config(px);
    cfg.range = range; cfg.oversample_h = 1; cfg.oversample_v = 1; cfg.pixel_snap = nk_true;
    struct nk_font* font = nk_font_atlas_add_from_memory(&F.atlas, (void*)cv_font_text, cv_font_text_size, px, &cfg);
    int w = 0, h = 0;
    const void* pixels = font ? nk_font_atlas_bake(&F.atlas, &w, &h, NK_FONT_ATLAS_ALPHA8) : NULL;
    if (!pixels) { nk_font_atlas_clear(&F.atlas); cv_render_label_atlas(NULL, 0, 0); F.px = px; return; }   /* out of memory: no labels */
    cv_render_label_atlas(pixels, w, h);
    struct nk_draw_null_texture null = {0};
    nk_font_atlas_end(&F.atlas, nk_handle_id(0), &null);
    for (int c = 32; c <= 126; c++) {
        const struct nk_font_glyph* g = nk_font_find_glyph(font, (nk_rune)c);
        F.g[c - 32] = g ? (cv_label_glyph){ g->xadvance, g->x0, g->y0, g->x1, g->y1, g->u0, g->v0, g->u1, g->v1 } : (cv_label_glyph){0};
    }
    F.m = (cv_label_metrics){ F.g, 32, 95, px, null.uv.x, null.uv.y };
    F.px = px; F.live = true;
}

/* ---- the anchors: one per labelled thing, pos[3] disp[3] disp2[3], and its text ------- */
/* Ids and values are formatted only for the labels shown (a million anchors would take
   100 ms to format up front): such an anchor keeps the node or element index in ref and
   no text. Named things keep their text. */
typedef struct {
    cv_fvec anc;                        /* 9 per anchor */
    cv_fvec nrm;                        /* 3 per anchor: the surface normal there, 0 0 0 for none (never culled) */
    CV_VEC(uint32_t) ref;               /* per anchor: node / element / Gauss point index, UINT32_MAX none */
    CV_VEC(uint8_t) src;                /* per anchor: what ref is (SRC_*), named things SRC_TEXT */
    cv_fvec pull;                       /* per anchor: model units it is moved toward the eye for its visibility
                                           test (an element's radius for a point inside it; the model for a name) */
    CV_VEC(char) txt;                   /* texts, 0-terminated, back to back */
    CV_VEC(uint32_t) toff;              /* text offset per anchor, UINT32_MAX: formatted from ref */
    CV_VEC(uint8_t) kk;                 /* per anchor: its key's kind (K_*), what a moved label is found by */
    CV_VEC(uint32_t) kid;               /* per anchor: the key's id, a number; the named kinds: an offset into txt */
    uint32_t n;
} anchors;

/* The keys of the labels, kind and id, stable while the model's numbering is:
     node 940, elem 12, gp 12:3 (element id : point), min 1, max 2 (the rank),
     measure 1 (its place in the list), set EHOLE (sets and surfaces by name),
     link NAME (the reference node's id when it has no name), material STEEL (its
     label's text), load / support "940 dof 2", "12.3 p" ... (the node, or the
     element and face, it sits at and what it is; see label_sink_add's callers) */
enum { K_NONE, K_NODE, K_ELEM, K_GP, K_MIN, K_MAX, K_MEAS, K_SET, K_LINK, K_LOAD, K_SUPPORT, K_MAT, K_N };
static const char* const KNAME[K_N] = { "", "node", "elem", "gp", "min", "max", "measure", "set", "link", "load", "support", "material" };
static bool k_named(int k) { return k >= K_SET; }
bool app_label_key_kind(const char* kind) {
    for (int k = 1; k < K_N; k++) if (!strcmp(kind, KNAME[k])) return true;
    return false;
}

enum { SRC_TEXT, SRC_NODE, SRC_ELEM, SRC_GAUSS };
static struct { anchors a; unsigned gen, serial; int kinds, asked; bool sel_only, probe_only, probe_on; uint32_t probe_elem;
                uint32_t pin0, pin1, meas0; float meas_w; } A;   /* anchors pin0 .. pin1: the extremes, then from meas0 the
                                                                     measurements (meas_w: their widest text, px); always drawn */
/* kinds: the ones built (a value field per element: the element value stands for the node
   value); asked: G.label_kinds then; probe_*: the probe these were built for */

static void pack(anchors* s, const float p[3], const float d[6], const float n[3], int src, uint32_t ref, float pull, const char* text) {
    if (!cv_reserve(s->anc, s->anc.n + 9) || !cv_reserve(s->nrm, s->nrm.n + 3) || !cv_reserve(s->toff, s->toff.n + 1) ||
        !cv_reserve(s->ref, s->ref.n + 1) || !cv_reserve(s->src, s->src.n + 1) || !cv_reserve(s->pull, s->pull.n + 1) ||
        !cv_reserve(s->kk, s->kk.n + 1) || !cv_reserve(s->kid, s->kid.n + 1)) return;
    size_t len = text ? strlen(text) + 1 : 0;
    if (len && !cv_reserve(s->txt, s->txt.n + len)) return;
    for (int k = 0; k < 3; k++) s->anc.a[s->anc.n++] = p[k];
    for (int k = 0; k < 6; k++) s->anc.a[s->anc.n++] = d[k];
    for (int k = 0; k < 3; k++) s->nrm.a[s->nrm.n++] = n ? n[k] : 0.f;
    s->ref.a[s->ref.n++] = ref;
    s->src.a[s->src.n++] = (uint8_t)src;
    s->pull.a[s->pull.n++] = pull;
    s->toff.a[s->toff.n++] = text ? (uint32_t)s->txt.n : UINT32_MAX;
    if (len) { memcpy(s->txt.a + s->txt.n, text, len); s->txt.n += len; }
    s->kk.a[s->kk.n++] = src == SRC_NODE ? K_NODE : src == SRC_ELEM ? K_ELEM : src == SRC_GAUSS ? K_GP : K_NONE;
    s->kid.a[s->kid.n++] = src == SRC_NODE ? G.frd.node_id[ref] : src == SRC_ELEM ? G.frd.elem_id[ref] : ref;
    s->n++;
}

/* the last anchor's key: a number, or a name (kept in txt) */
static void key_set(anchors* s, int kk, uint32_t num, const char* name) {
    if (!s->n || s->kk.n != s->n) return;
    if (name) {
        size_t len = strlen(name) + 1;
        if (!cv_reserve(s->txt, s->txt.n + len)) { s->kk.a[s->n - 1] = K_NONE; return; }
        num = (uint32_t)s->txt.n;
        memcpy(s->txt.a + s->txt.n, name, len); s->txt.n += len;
    }
    s->kk.a[s->n - 1] = (uint8_t)kk;
    s->kid.a[s->n - 1] = num;
}
static void anchors_reset(anchors* s) {
    s->anc.n = s->nrm.n = s->txt.n = s->toff.n = s->ref.n = s->src.n = s->pull.n = s->kk.n = s->kid.n = 0; s->n = 0;
}
static void anchor_add(const float p[3], const float d[6], const char* text) { pack(&A.a, p, d, NULL, SRC_TEXT, UINT32_MAX, G.diag, text); }
static void anchor_ref(const float p[3], const float d[6], const float n[3], int src, uint32_t ref, float pull) { pack(&A.a, p, d, n, src, ref, pull, NULL); }

/* the centre of an element (undeformed) */
static void elem_centre(uint32_t e, float c[3]) {
    const cv_frd* f = &G.frd;
    uint32_t b = f->eoff[e], m = f->eoff[e + 1] - b;
    c[0] = c[1] = c[2] = 0;
    for (uint32_t j = 0; m && j < m; j++) for (int k = 0; k < 3; k++) c[k] += f->xyz[3 * f->conn[b + j] + k] / m;
}

/* the normal of a triangle a b c, turned to point away from the element it belongs to:
   the skin's winding is not kept outward (the faces are shaded from screen derivatives) */
static void tri_normal_out(const float* a, const float* b, const float* c, uint32_t e, float nn[3]) {
    float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    nn[0] = e1[1] * e2[2] - e1[2] * e2[1]; nn[1] = e1[2] * e2[0] - e1[0] * e2[2]; nn[2] = e1[0] * e2[1] - e1[1] * e2[0];
    float ec[3], out[3];
    elem_centre(e, ec);
    for (int k = 0; k < 3; k++) out[k] = (a[k] + b[k] + c[k]) / 3 - ec[k];
    if (nn[0] * out[0] + nn[1] * out[1] + nn[2] * out[2] < 0) for (int k = 0; k < 3; k++) nn[k] = -nn[k];
}

/* the surface normal at each skin node: the mean of its skin triangles' (undeformed), once
   per skin; a label whose node faces away from the eye is not drawn */
static struct { float* n; const uint32_t* tri; size_t n_tri; } N;
static const float* node_normals(void) {
    if (N.n && N.tri == G.skin.tri && N.n_tri == G.skin.n_tri) return N.n;
    free(N.n); N.n = calloc((size_t)CV_MAX(G.frd.n_nodes, 1) * 3, sizeof *N.n);
    if (!N.n) return NULL;
    const float* x = G.frd.xyz;
    for (size_t t = 0; t < G.skin.n_tri; t++) {
        const uint32_t* v = G.skin.tri + 3 * t;
        float nn[3];
        tri_normal_out(x + 3 * v[0], x + 3 * v[1], x + 3 * v[2], G.skin.tri_elem[t], nn);
        for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++) N.n[3 * v[j] + k] += nn[k];   /* area weighted */
    }
    N.tri = G.skin.tri; N.n_tri = G.skin.n_tri;
    return N.n;
}

static void anchor_node(uint32_t i) {
    float d[6];
    app_node_disp6(i, d);
    const float* nn = node_normals();
    anchor_ref(G.frd.xyz + 3 * i, d, nn ? nn + 3 * i : NULL, SRC_NODE, i, 0.f);
}


/* the text of an anchor: its own, or its id and / or value formatted now ("940: 256.7"
   when both are asked for at the same point) */
static bool on(int k) { return (A.kinds >> k) & 1; }
static const char* anchor_text(uint32_t i, char* buf, size_t n) {
    uint32_t off = A.a.toff.a[i], ref = A.a.ref.a[i];
    if (off != UINT32_MAX) return A.a.txt.a + off;
    char id[32] = "", val[32] = "";
    switch (A.a.src.a[i]) {
    case SRC_NODE:
        if (on(CV_LABEL_NODE)) snprintf(id, sizeof id, "%u", G.frd.node_id[ref]);
        if (on(CV_LABEL_VALUE) && G.scalar) app_legend_fmt(val, sizeof val, G.scalar[ref]);
        break;
    case SRC_ELEM:
        if (on(CV_LABEL_ELEM)) snprintf(id, sizeof id, "%u", G.frd.elem_id[ref]);
        if (on(CV_LABEL_EVALUE) && G.elem_val) app_legend_fmt(val, sizeof val, G.elem_val[ref]);
        break;
    case SRC_GAUSS: {
        gp_points g = gp_last();
        if (ref >= g.n) break;
        if (on(CV_LABEL_GPID)) snprintf(id, sizeof id, "%u:%u", G.frd.elem_id[g.elem[ref]], (unsigned)g.ip[ref] + 1);
        if (on(CV_LABEL_GPVALUE)) app_legend_fmt(val, sizeof val, g.val[ref]);
        break;
    }
    }
    snprintf(buf, n, "%s%s%s", id, id[0] && val[0] ? ": " : "", val);
    return buf;
}

/* the centre of an exterior face of e, with its mean displacement */
static bool face_centre(uint32_t e, int face, float p[3], float d[6]) {
    uint32_t c[4];
    int m = cv_elem_face_corners(&G.frd, e, face, c);
    if (m < 3) return false;
    memset(p, 0, 3 * sizeof *p); memset(d, 0, 6 * sizeof *d);
    for (int j = 0; j < m; j++) {
        float dj[6];
        app_node_disp6(c[j], dj);
        for (int q = 0; q < 3; q++) p[q] += G.frd.xyz[3 * c[j] + q] / m;
        for (int q = 0; q < 6; q++) d[q] += dj[q] / m;
    }
    return true;
}

/* ---- the sink: loads and supports, recorded by app_loads.c as it builds the symbols ---- */
static struct { bool on; anchors a; } S;
void label_sink_begin(int kinds) {
    S.on = (kinds >> CV_LABEL_LOADS & 1) || (kinds >> CV_LABEL_SUPPORTS & 1);
    anchors_reset(&S.a);
}
void label_sink_add(const float p[3], const float d[6], const char* text, bool support, const char* at) {
    if (!S.on) return;
    pack(&S.a, p, d, NULL, SRC_TEXT, UINT32_MAX, G.diag, text);
    key_set(&S.a, support ? K_SUPPORT : K_LOAD, 0, at);
}

/* per element its first exterior face (0xFF none), from the skin */
static uint8_t* first_faces(void) {
    uint8_t* ff = malloc(CV_MAX(G.frd.n_elems, 1));
    if (!ff) return NULL;
    memset(ff, 0xFF, CV_MAX(G.frd.n_elems, 1));
    for (size_t k = 0; k < G.skin.n_face; k++) {
        uint32_t e = G.skin.face[k] >> 3;
        if (e < G.frd.n_elems && ff[e] == 0xFF) ff[e] = (uint8_t)(G.skin.face[k] & 7);
    }
    return ff;
}

/* the mean of some points with their displacements, as one anchor */
typedef struct { double c[3], cd[6]; uint32_t n; } mean;   /* double: a set may hold millions of nodes */
static void mean_add(mean* m, const float p[3], const float d[6]) {
    for (int q = 0; q < 3; q++) m->c[q] += p[q];
    for (int q = 0; q < 6; q++) m->cd[q] += d[q];
    m->n++;
}
static void mean_anchor(mean* m, const char* text) {
    if (!m->n) return;
    float c[3], cd[6];
    for (int q = 0; q < 3; q++) c[q] = (float)(m->c[q] / m->n);
    for (int q = 0; q < 6; q++) cd[q] = (float)(m->cd[q] / m->n);
    anchor_add(c, cd, text);
}

static void build_named(int kind) {
    const cv_frd* f = &G.frd;
    const cv_inp* dk = deck_get();
    char t[64];
    float p[3], d[6];
    switch (kind) {
    case CV_LABEL_SETS: {                         /* the ticked sets and surfaces: the name at their centre */
        if (!dk) break;
        uint8_t* ff = first_faces();
        bool* son = deck_set_flags(); bool* fon = deck_surf_flags();
        for (int s = 0; ff && s < dk->nsets; s++) {
            if (!son[s]) continue;
            const cv_set* st = &dk->sets[s];
            mean m = {{0}, {0}, 0};
            for (uint32_t i = 0; i < st->n; i++) {
                if (st->is_elem) {
                    uint32_t e = deck_elem(f, st->ids[i]);
                    if (e == UINT32_MAX || ff[e] == 0xFF || !face_centre(e, ff[e], p, d)) continue;
                } else if (!deck_node_pd(st->ids[i], p, d)) continue;
                mean_add(&m, p, d);
            }
            if (m.n) { mean_anchor(&m, st->name); key_set(&A.a, K_SET, 0, st->name); }
        }
        for (int s = 0; s < dk->nsurfs; s++) {
            if (!fon[s]) continue;
            const cv_surface* sf = &dk->surfs[s];
            mean m = {{0}, {0}, 0};
            for (uint32_t j = 0; j < sf->n; j++) {
                uint32_t e = deck_elem(f, sf->elem[j]);
                if (e == UINT32_MAX || !face_centre(e, sf->face[j], p, d)) continue;
                mean_add(&m, p, d);
            }
            for (uint32_t j = 0; j < sf->nn; j++) if (deck_node_pd(sf->nodes[j], p, d)) mean_add(&m, p, d);
            if (m.n) { mean_anchor(&m, sf->name); key_set(&A.a, K_SET, 0, sf->name); }
        }
        free(ff);
        break;
    }
    case CV_LABEL_LINKS: {                        /* couplings and rigid bodies: the name at the reference node */
        if (!dk) break;
        static const char* kn[] = { "rigid body", "coupling", "distributing", "equation", "tie", "contact" };
        for (int k = 0; k < dk->nlinks; k++) {
            const cv_link* l = &dk->links[k];
            if (!l->ref || !deck_node_pd(l->ref, p, d)) continue;
            anchor_add(p, d, l->name[0] ? l->name : kn[l->kind < 6 ? l->kind : 0]);
            snprintf(t, sizeof t, "%u", l->ref);
            key_set(&A.a, K_LINK, 0, l->name[0] ? l->name : t);
        }
        break;
    }
    case CV_LABEL_MATERIALS: {                    /* the name at the centre of each material's surface */
        const cv_axis* ax = &G.groups.axis[CV_AXIS_MAT];
        mean* m = calloc(CV_MAX(ax->n, 1), sizeof *m);
        if (!m) break;
        for (size_t k = 0; k < G.skin.n_face; k++) {
            uint32_t e = G.skin.face[k] >> 3;
            if (e >= f->n_elems || !ax->of_elem || ax->of_elem[e] >= ax->n) continue;
            if (!face_centre(e, (int)(G.skin.face[k] & 7), p, d)) continue;
            mean_add(&m[ax->of_elem[e]], p, d);
        }
        for (int mi = 0; mi < ax->n; mi++) {
            const char* nm = deck_material_name(ax->value[mi]);
            if (nm) snprintf(t, sizeof t, "%s", nm); else snprintf(t, sizeof t, "material %u", ax->value[mi]);
            if (m[mi].n) { mean_anchor(&m[mi], t); key_set(&A.a, K_MAT, 0, t); }
        }
        free(m);
        break;
    }
    case CV_LABEL_LOADS: case CV_LABEL_SUPPORTS: {  /* at the symbols: what loads_refresh recorded */
        if (!S.a.n) loads_refresh();              /* once for both kinds: build_named is called per kind */
        int want = kind == CV_LABEL_LOADS ? K_LOAD : K_SUPPORT;
        for (uint32_t i = 0; i < S.a.n; i++) {
            if (S.a.kk.a[i] != want) continue;
            anchor_add(S.a.anc.a + 9 * i, S.a.anc.a + 9 * i + 3, S.a.txt.a + S.a.toff.a[i]);
            key_set(&A.a, want, 0, S.a.txt.a + S.a.kid.a[i]);
        }
        break;
    }
    default: break;
    }
}

/* an element's centre (undeformed), its mean displacement, and its radius: half the
   diagonal of its nodes' box, how far a point inside it may be behind its surface */
static float elem_anchor(uint32_t e, float p[3], float d[6]) {
    const cv_frd* f = &G.frd;
    uint32_t b = f->eoff[e], m = f->eoff[e + 1] - b;
    float lo[3] = { INFINITY, INFINITY, INFINITY }, hi[3] = { -INFINITY, -INFINITY, -INFINITY }, dj[6];
    memset(p, 0, 3 * sizeof *p); memset(d, 0, 6 * sizeof *d);
    if (!m) return 0.f;
    for (uint32_t j = 0; j < m; j++) {
        uint32_t i = f->conn[b + j];
        app_node_disp6(i, dj);
        for (int q = 0; q < 3; q++) {
            float x = f->xyz[3 * i + q];
            p[q] += x / m; lo[q] = fminf(lo[q], x); hi[q] = fmaxf(hi[q], x);
        }
        for (int q = 0; q < 6; q++) d[q] += dj[q] / m;
    }
    float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    return 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
}

/* "el N" at an element's centre */
static void elem_label(uint32_t e) {
    float p[3], d[6];
    float r = elem_anchor(e, p, d);
    char t[32];
    snprintf(t, sizeof t, "el %u", G.frd.elem_id[e]);
    pack(&A.a, p, d, NULL, SRC_TEXT, UINT32_MAX, 1.2f * r, t);
    key_set(&A.a, K_ELEM, G.frd.elem_id[e], NULL);
}

static void build_anchors(void) {
    anchors_reset(&A.a);
    A.kinds = A.asked = G.label_kinds; A.gen = G.label_gen; A.sel_only = G.label_sel_only; A.probe_only = G.label_probe_only;
    A.probe_on = G.probe_on; A.probe_elem = G.probe.elem;
    A.serial++;
    if (!G.loaded || (!G.label_kinds && !app_measure_count())) return;
    const cv_frd* f = &G.frd;
    bool nodal = G.has_field && !G.elem_mode && G.field_src != 1 && G.scalar;
    bool elemv = G.has_field && (G.elem_mode || G.field_src == 1) && G.elem_val;
    if (on(CV_LABEL_VALUE) && !nodal) {           /* a per-element field: its value on the faces instead */
        A.kinds &= ~(1 << CV_LABEL_VALUE);
        if (elemv) A.kinds |= 1 << CV_LABEL_EVALUE;
    }
    if (on(CV_LABEL_EVALUE) && !elemv) A.kinds &= ~(1 << CV_LABEL_EVALUE);
    /* the selection as marks, when asked for */
    uint8_t* nmark = NULL; uint8_t* emark = NULL;
    if (G.label_sel_only) {
        nmark = calloc(CV_MAX(f->n_nodes, 1), 1); emark = calloc(CV_MAX(f->n_elems, 1), 1);
        if (nmark && emark) {
            for (uint32_t k = 0; k < G.seln_n; k++) nmark[G.seln[k]] = 1;
            for (uint32_t k = 0; k < G.sel_n; k++) {
                emark[G.sel[k]] = 1;
                for (uint32_t j = f->eoff[G.sel[k]]; j < f->eoff[G.sel[k] + 1]; j++) nmark[f->conn[j]] = 1;
            }
        }
    }
    /* nodes: id and / or value */
    if (on(CV_LABEL_NODE) || on(CV_LABEL_VALUE)) {
        bool need_val = !on(CV_LABEL_NODE);           /* a value alone: no label where there is none */
        if (G.label_probe_only) {                     /* the probed element's nodes, and its own id */
            if (G.probe_on) {
                uint32_t e = G.probe.elem;
                for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) {
                    uint32_t k = f->conn[j];
                    if (need_val && G.scalar[k] != G.scalar[k]) continue;
                    anchor_node(k);
                }
                if (on(CV_LABEL_NODE)) elem_label(e);
            }
        } else {
            for (size_t k = 0; k < G.skin.n_pt; k++) {
                uint32_t n = G.skin.pt[k];
                if (G.label_sel_only && !(nmark && nmark[n])) continue;
                if (need_val && G.scalar[n] != G.scalar[n]) continue;
                anchor_node(n);
            }
        }
    }
    /* elements: id and / or value, on the first exterior face */
    if (on(CV_LABEL_ELEM) || on(CV_LABEL_EVALUE)) {
        bool need_val = !on(CV_LABEL_ELEM);
        if (G.label_probe_only) {                     /* the probed element, wherever it is (a cap, a found element) */
            if (G.probe_on && on(CV_LABEL_ELEM)) elem_label(G.probe.elem);
        } else {
            uint8_t* done = calloc(CV_MAX(f->n_elems, 1), 1);
            for (size_t k = 0; done && k < G.skin.n_face; k++) {
                uint32_t e = G.skin.face[k] >> 3;
                if (e >= f->n_elems || done[e]) continue;
                done[e] = 1;
                if (G.label_sel_only && !(emark && emark[e])) continue;
                if (need_val && G.elem_val[e] != G.elem_val[e]) continue;
                float p[3], d[6];
                float r = elem_anchor(e, p, d);       /* at the centre; its radius lets the test see past its own faces */
                anchor_ref(p, d, NULL, SRC_ELEM, e, 1.2f * r);
            }
            free(done);
        }
    }
    /* Gauss points: id and / or value */
    if (on(CV_LABEL_GPID) || on(CV_LABEL_GPVALUE)) {
        gp_points g = gp_last();
        bool need_val = !on(CV_LABEL_GPID);
        for (uint32_t k = 0; k < g.n; k++) {
            if (need_val && g.val[k] != g.val[k]) continue;
            if (G.label_probe_only && !(G.probe_on && g.elem[k] == G.probe.elem)) continue;
            if (G.label_sel_only && !(emark && emark[g.elem[k]])) continue;
            float d6[6] = { g.disp[3 * k], g.disp[3 * k + 1], g.disp[3 * k + 2], 0, 0, 0 }, pc[3], pd[6];
            float r = elem_anchor(g.elem[k], pc, pd);
            anchor_ref(g.pos + 3 * k, d6, NULL, SRC_GAUSS, k, 1.2f * r);
        }
    }
    /* the field's extremes: "max: 256.7", "max 2: 250.1" ... at their node or element */
    A.pin0 = A.pin1 = A.a.n;
    if (on(CV_LABEL_MINMAX) && G.has_field && (nodal || (elemv && G.field_src != 1)))
        for (uint32_t i = 0; i < 2 * G.ext_n; i++) {
            bool hi = i >= G.ext_n;
            uint32_t rank = hi ? i - G.ext_n : i, at = hi ? G.ext_hi[rank] : G.ext_lo[rank];
            char t[48], val[32];
            float p[3], d[6], r = G.diag;
            if (nodal) { memcpy(p, f->xyz + 3 * at, sizeof p); app_node_disp6(at, d); }
            else r = CV_MAX(1.2f * elem_anchor(at, p, d), G.diag);
            app_legend_fmt(val, sizeof val, nodal ? G.scalar[at] : G.elem_val[at]);
            if (rank) snprintf(t, sizeof t, "%s %u: %s", hi ? "max" : "min", rank + 1, val);
            else snprintf(t, sizeof t, "%s: %s", hi ? "max" : "min", val);
            pack(&A.a, p, d, NULL, SRC_TEXT, UINT32_MAX, r, t);
            key_set(&A.a, hi ? K_MAX : K_MIN, rank + 1, NULL);
            A.pin1 = A.a.n;
        }
    /* the measurements, pinned after the extremes: seen through the model */
    A.meas0 = A.pin1 = A.a.n; A.meas_w = 0;
    if (app_measure_count()) {
        uint32_t nm = (uint32_t)app_measure_count();
        float* anc = malloc(nm * 9 * sizeof *anc);
        char (*txt)[96] = malloc(nm * sizeof *txt);
        uint32_t* which = malloc(nm * sizeof *which);
        nm = anc && txt && which ? app_measure_anchors(anc, txt, which, nm) : 0;
        for (uint32_t i = 0; i < nm; i++) {
            pack(&A.a, anc + 9 * i, anc + 9 * i + 3, NULL, SRC_TEXT, UINT32_MAX, G.diag, txt[i]);
            key_set(&A.a, K_MEAS, which[i] + 1, NULL);
            A.meas_w = CV_MAX(A.meas_w, cv_label_width(&F.m, txt[i]));
        }
        A.pin1 = A.a.n;
        free(anc); free(txt); free(which);
    }
    /* the named kinds */
    for (int k = CV_LABEL_SETS; k <= CV_LABEL_MATERIALS; k++) if (on(k)) build_named(k);
    free(nmark); free(emark);
}

void app_label_changed(void) { G.label_gen++; }

/* the widest text the labels of this build can have, px: ids by their largest id, values
   by the legend's ends, names by the names themselves */
static float widest_label(void) {
    char t[64], id[32], val[32] = "";
    float w = 0;
    uint32_t big_n = 0, big_e = 0;
    for (uint32_t i = 0; i < G.frd.n_nodes; i++) if (G.frd.node_id[i] > big_n) big_n = G.frd.node_id[i];
    for (uint32_t i = 0; i < G.frd.n_elems; i++) if (G.frd.elem_id[i] > big_e) big_e = G.frd.elem_id[i];
    /* the widest value of the legend's range */
    {
        char a[32], b[32], c[32];
        app_legend_fmt(a, sizeof a, G.rmin); app_legend_fmt(b, sizeof b, G.rmax); app_legend_fmt(c, sizeof c, -G.rmax);
        const char* best = strlen(a) >= strlen(b) ? a : b;
        if (strlen(c) > strlen(best)) best = c;
        snprintf(val, sizeof val, "%s", best);
    }
    if (on(CV_LABEL_NODE) || on(CV_LABEL_VALUE)) {
        if (on(CV_LABEL_NODE)) snprintf(id, sizeof id, "%u", big_n); else id[0] = 0;
        snprintf(t, sizeof t, "%s%s%s", id, on(CV_LABEL_NODE) && on(CV_LABEL_VALUE) ? ": " : "", on(CV_LABEL_VALUE) ? val : "");
        w = CV_MAX(w, cv_label_width(&F.m, t));
        if (A.probe_only) { snprintf(t, sizeof t, "el %u", big_e); w = CV_MAX(w, cv_label_width(&F.m, t)); }
    }
    if (on(CV_LABEL_ELEM) || on(CV_LABEL_EVALUE)) {
        if (on(CV_LABEL_ELEM)) snprintf(id, sizeof id, "%u", big_e); else id[0] = 0;
        snprintf(t, sizeof t, "%s%s%s", id, on(CV_LABEL_ELEM) && on(CV_LABEL_EVALUE) ? ": " : "", on(CV_LABEL_EVALUE) ? val : "");
        w = CV_MAX(w, cv_label_width(&F.m, t));
    }
    if (on(CV_LABEL_GPID) || on(CV_LABEL_GPVALUE)) {
        if (on(CV_LABEL_GPID)) snprintf(id, sizeof id, "%u:27", big_e); else id[0] = 0;
        snprintf(t, sizeof t, "%s%s%s", id, on(CV_LABEL_GPID) && on(CV_LABEL_GPVALUE) ? ": " : "", on(CV_LABEL_GPVALUE) ? val : "");
        w = CV_MAX(w, cv_label_width(&F.m, t));
    }
    if (on(CV_LABEL_MINMAX)) { snprintf(t, sizeof t, "max %d: %s", G.minmax_n, val); w = CV_MAX(w, cv_label_width(&F.m, t)); }
    for (uint32_t i = 0; i < A.a.n; i++)             /* the named ones: the texts themselves (not the measurements: pinned) */
        if (A.a.toff.a[i] != UINT32_MAX && !(i >= A.meas0 && i < A.pin1)) w = CV_MAX(w, cv_label_width(&F.m, A.a.txt.a + A.a.toff.a[i]));
    return w;
}

/* ---- moved labels: which anchors they are, the boxes drawn ---------------------------- */

/* an anchor's key as text, its offset 0 */
static void anchor_key(uint32_t i, cv_label_off* o) {
    memset(o, 0, sizeof *o);
    int k = A.a.kk.a[i];
    uint32_t id = A.a.kid.a[i];
    snprintf(o->kind, sizeof o->kind, "%s", KNAME[k]);
    if (k_named(k)) snprintf(o->id, sizeof o->id, "%s", A.a.txt.a + id);
    else if (k == K_GP) {
        gp_points g = gp_last();
        if (id < g.n) snprintf(o->id, sizeof o->id, "%u:%u", G.frd.elem_id[g.elem[id]], (unsigned)g.ip[id] + 1);
    } else snprintf(o->id, sizeof o->id, "%u", id);
}

/* per anchor the moved entry + 1 (0: where it would be), and the moved anchors; again
   when the anchors or the entries change */
static struct { uint16_t* of; CV_VEC(uint32_t) list; unsigned serial, gen; bool done; } R;

static void resolve(void) {
    if (R.done && R.serial == A.serial && R.gen == label_moved_gen()) return;
    R.done = true; R.serial = A.serial; R.gen = label_moved_gen();
    R.list.n = 0;
    free(R.of); R.of = NULL;
    int nm = label_moved_n();
    if (!nm || !A.a.n || A.a.kk.n != A.a.n) return;
    R.of = calloc(A.a.n, sizeof *R.of);
    int* ek = malloc((size_t)nm * sizeof *ek);
    uint32_t* en = malloc((size_t)nm * sizeof *en);
    if (!R.of || !ek || !en) { free(R.of); R.of = NULL; free(ek); free(en); return; }
    for (int j = 0; j < nm; j++) {               /* each entry's kind, and its number */
        const cv_label_off* o = label_moved(j);
        ek[j] = K_NONE;
        for (int k = 1; k < K_N; k++) if (!strcmp(o->kind, KNAME[k])) ek[j] = k;
        if (ek[j] != K_NONE && !k_named(ek[j]) && ek[j] != K_GP) {
            char* e;
            unsigned long v = strtoul(o->id, &e, 10);
            if (e == o->id || *e || v > UINT32_MAX) ek[j] = K_NONE;
            en[j] = (uint32_t)v;
        }
    }
    for (uint32_t i = 0; i < A.a.n; i++) {
        int k = A.a.kk.a[i];
        if (k == K_NONE) continue;
        for (int j = 0; j < nm; j++) {
            if (ek[j] != k) continue;
            bool hit;
            if (k_named(k)) hit = !strcmp(A.a.txt.a + A.a.kid.a[i], label_moved(j)->id);
            else if (k == K_GP) { cv_label_off o; anchor_key(i, &o); hit = !strcmp(o.id, label_moved(j)->id); }
            else hit = A.a.kid.a[i] == en[j];
            if (!hit) continue;
            R.of[i] = (uint16_t)(j + 1);
            cv_push(R.list, i);
            break;
        }
    }
    free(ek); free(en);
}

/* the labels drawn: their boxes in window px (x, y, w, h), anchors, shown points (for
   the hit test's look past the model: x y z and the pixel, 5 each) */
static struct { cv_fvec box, pos; CV_VEC(uint32_t) anc; bool on_top; float reach; } H;

/* is the label of anchor i (drawn as H's k-th) seen: in front, or its point not behind
   the surface at its pixel (as the shader tests it) */
static bool label_seen(uint32_t k) {
    if (H.on_top) return true;
    const float* p = H.pos.a + 5 * k;                     /* x y z, then its pixel */
    cv_pick pk;
    float o[3], dir[3];
    if (!app_pick(p[3], p[4], &pk, o, dir)) return true;  /* nothing in front of it */
    float dd = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
    float t = ((p[0] - o[0]) * dir[0] + (p[1] - o[1]) * dir[1] + (p[2] - o[2]) * dir[2]) / CV_MAX(dd, 1e-30f);
    float tol = CV_MAX(A.a.pull.a[H.anc.a[k]], CV_MAX(H.reach, 2.f) * app_pixel_size(v3_make(p[0], p[1], p[2]))) / sqrtf(CV_MAX(dd, 1e-30f));
    return pk.t >= t - tol;
}

/* the drawn label at a window pixel: H's index, -1 none */
static int hit_at(float x, float y) {
    uint32_t n = (uint32_t)H.anc.n;
    for (;;) {
        int k = cv_label_hit(H.box.a, n, x, y);
        if (k < 0) return -1;
        if (H.anc.a[k] < A.a.n && label_seen((uint32_t)k)) return k;
        n = (uint32_t)k;                                   /* hidden: the one under it */
    }
}

bool label_hit(float x, float y, cv_label_off* key) {
    int k = hit_at(x, y);
    if (k < 0) return false;
    uint32_t i = H.anc.a[k];
    anchor_key(i, key);
    if (R.of && R.of[i] && R.gen == label_moved_gen()) { key->dx = label_moved(R.of[i] - 1)->dx; key->dy = label_moved(R.of[i] - 1)->dy; }
    else                                                   /* the entries changed since the last frame: by the key */
        for (int j = 0; j < label_moved_n(); j++)
            if (!strcmp(label_moved(j)->kind, key->kind) && !strcmp(label_moved(j)->id, key->id)) { key->dx = label_moved(j)->dx; key->dy = label_moved(j)->dy; break; }
    return key->kind[0] != 0;
}

bool app_label_box(int k, float r[4]) {
    if (k < 0 || (size_t)k >= H.anc.n) return false;
    memcpy(r, H.box.a + 4 * (size_t)k, 4 * sizeof *r);
    return true;
}

/* ---- per frame: thin, lay out, upload when the camera or the anchors changed --------- */

/* huge sets: one anchor per 3D cell about the spacing at the model's distance, so the
   per-move cost stays bounded; kept until the cell changes by a fifth or the anchors do */
static struct { uint32_t* keep; uint32_t n; float cell; unsigned serial; } C;

/* on the undeformed positions: the cell is coarse, and an animation must not rebuild it every frame */
static void coarse_check(float cell) {
    if (C.keep && C.serial == A.serial && fabsf(cell - C.cell) <= 0.2f * C.cell) return;
    free(C.keep); C.keep = malloc((size_t)CV_MAX(A.a.n, 1) * sizeof *C.keep);
    float* xyz = malloc((size_t)CV_MAX(A.a.n, 1) * 3 * sizeof *xyz);
    if (!C.keep || !xyz) { free(xyz); free(C.keep); C.keep = NULL; return; }
    for (uint32_t i = 0; i < A.a.n; i++) memcpy(xyz + 3 * i, A.a.anc.a + 9 * i, 3 * sizeof *xyz);
    C.n = cv_label_coarse(xyz, A.a.n, cell, C.keep);
    free(xyz);
    C.cell = cell; C.serial = A.serial;
}

/* an anchor as shown (deformed) and on the screen; false when it gets no label there
   (no position, cut away, facing away, behind the eye) */
typedef struct { const cv_draw* d; v3 eye, fwd; bool persp; } view_ctx;
static bool project(const view_ctx* v, uint32_t i, float p[3], cv_label_pt* o) {
    const cv_draw* d = v->d;
    const float* a = A.a.anc.a + 9 * i, *M = d->mvp;
    for (int q = 0; q < 3; q++) p[q] = a[q] + d->def_scale * a[3 + q] + d->def_scale2 * a[6 + q];
    if (p[0] != p[0] || p[1] != p[1] || p[2] != p[2]) return false;   /* no position (NaN displacement) */
    if (d->clip && p[0] * d->clip_n[0] + p[1] * d->clip_n[1] + p[2] * d->clip_n[2] > d->clip_d) return false;   /* cut away */
    const float* nn = A.a.nrm.a + 3 * i;
    if (nn[0] != 0 || nn[1] != 0 || nn[2] != 0) {                     /* faces away from the eye */
        float to[3] = { v->persp ? v->eye.x - p[0] : -v->fwd.x, v->persp ? v->eye.y - p[1] : -v->fwd.y, v->persp ? v->eye.z - p[2] : -v->fwd.z };
        if (nn[0] * to[0] + nn[1] * to[1] + nn[2] * to[2] <= 0) return false;
    }
    float c[4];
    for (int r = 0; r < 4; r++) c[r] = M[r] * p[0] + M[4 + r] * p[1] + M[8 + r] * p[2] + M[12 + r];
    if (c[3] <= 1e-9f) return false;                                  /* behind the eye */
    o->sx = d->vp_x + (c[0] / c[3] * 0.5f + 0.5f) * d->vp_w;
    o->sy = d->vp_y + (0.5f - c[1] / c[3] * 0.5f) * d->vp_h;
    o->depth = c[2] / c[3] * 0.5f + 0.5f;
    o->id = i;
    return true;
}

void app_label_frame(cv_draw* d) {
    static float last_mvp[16], last_px, last_sp, last_f1, last_f2, last_clip[4]; static unsigned last_gen, last_mv; static int last_kind, last_w, last_h;
    H.on_top = d->labels_on_top;
    if (!G.loaded || (!G.label_kinds && !app_measure_count())) {
        if (A.a.n || last_kind) {
            A.a.n = 0; A.pin0 = A.pin1 = 0; A.asked = -1;                  /* the same kind again rebuilds */
            free(C.keep); C.keep = NULL; C.n = 0;
            cv_render_labels(NULL, 0, NULL, 0); G.label_note[0] = 0; last_kind = 0;
        }
        H.box.n = H.pos.n = H.anc.n = 0;
        return;
    }
    float px = roundf(CV_MAX(G.label_px, 6.f) * ui_scale());
    if (F.px != px) font_bake(px);           /* a failed bake is tried again only at another size */
    if (!F.live) return;
    bool rebuild = A.gen != G.label_gen || A.asked != G.label_kinds || A.sel_only != G.label_sel_only || A.probe_only != G.label_probe_only ||
                   (G.label_probe_only && (A.probe_on != G.probe_on || A.probe_elem != G.probe.elem));
    if (rebuild) build_anchors();
    float clip[4] = { d->clip ? d->clip_n[0] : 0, d->clip ? d->clip_n[1] : 0, d->clip ? d->clip_n[2] : 0, d->clip ? d->clip_d : 0 };
    bool moved = memcmp(last_mvp, d->mvp, sizeof last_mvp) != 0 || last_px != px || last_sp != G.label_spacing ||
                 last_w != d->vp_w || last_h != d->vp_h || last_f1 != d->def_scale || last_f2 != d->def_scale2 ||
                 memcmp(last_clip, clip, sizeof clip) != 0 || last_mv != label_moved_gen();
    if (!rebuild && !moved && last_gen == G.label_gen && last_kind == G.label_kinds) return;
    resolve();

    /* the thinning box: the widest label plus the gap across, the text height plus it down */
    float s = ui_scale(), gap = G.label_spacing * s, pad = 2 * s;
    float bx = gap > 0 ? widest_label() + 2 * pad + gap : 0, by = gap > 0 ? F.m.height + 2 * pad + gap : 0;
    float spacing = CV_MAX(bx, by);
    /* which anchors to project: all, or the coarse subset of a huge set */
    const uint32_t* ids = NULL; uint32_t n_ids = A.a.n;
    if (A.a.n > 200000 && spacing > 0) {
        coarse_check(spacing * app_pixel_size(G.cam.target));
        if (C.keep) { ids = C.keep; n_ids = C.n; }
    }
    /* the pinned first, in this order: the moved labels, the extremes, the measurements;
       then the rest. A moved one is thinned as if its point were where its label went,
       so the others keep clear of the label. */
    uint32_t n_mv = (uint32_t)R.list.n, n_pin = A.pin1 - A.pin0, n_all = n_mv + n_pin + n_ids;
    cv_label_pt* pts = malloc((size_t)CV_MAX(n_all, 1) * sizeof *pts);
    uint32_t* chosen = malloc((size_t)CV_MAX(n_all, 1) * sizeof *chosen);
    if (!pts || !chosen) { free(pts); free(chosen); return; }   /* tried again next frame */
    memcpy(last_mvp, d->mvp, sizeof last_mvp); memcpy(last_clip, clip, sizeof last_clip); last_px = px; last_sp = G.label_spacing; last_gen = G.label_gen;
    last_kind = G.label_kinds; last_w = d->vp_w; last_h = d->vp_h; last_f1 = d->def_scale; last_f2 = d->def_scale2; last_mv = label_moved_gen();
    view_ctx v = { .d = d, .persp = !G.cam.ortho };
    v3 right, up;                                /* to cull the nodes facing away: toward the eye, or against the view */
    cam_basis(&G.cam, &v.eye, &v.fwd, &right, &up);
    uint32_t np = 0, npin = 0;
    for (uint32_t k = 0; k < n_all; k++) {
        uint32_t i;
        if (k < n_mv) i = R.list.a[k];
        else if (k < n_mv + n_pin) { i = A.pin0 + k - n_mv; if (R.of && R.of[i]) continue; }
        else {
            uint32_t q = k - n_mv - n_pin;
            i = ids ? ids[q] : q;
            if ((i >= A.pin0 && i < A.pin1) || (R.of && R.of[i])) continue;   /* taken with the pinned */
        }
        float p[3];
        if (!project(&v, i, p, &pts[np])) continue;
        if (k < n_mv) {
            const cv_label_off* m = label_moved(R.of[i] - 1);
            pts[np].sx += m->dx * s; pts[np].sy += m->dy * s;
        }
        np++;
        if (k < n_mv + n_pin) npin = np;
    }
    uint32_t max_out = 500000;                    /* gap 0 shows every one; the cap only keeps a huge model from a GB of quads */
    uint32_t n = cv_label_thin(pts, np, npin, bx, by, (float)d->vp_x, (float)d->vp_y, (float)d->vp_w, (float)d->vp_h, chosen, max_out);
    /* lay the chosen out: text a little right of and above the point */
    cv_fvec gly = {0}, box = {0};
    float dx = 4 * s, dy = -(F.m.height + 3 * s);
    char t[64];
    float reach = F.m.height;                      /* the farthest a label's box reaches from its point, px */
    /* pinned labels at one spot (the extremes of a symmetric part, seen along the axis)
       stack: each one that would cover an earlier one goes a row lower */
    float pin_w = CV_MAX(gap > 0 ? bx - gap : widest_label() + 2 * pad, A.meas_w + 2 * pad), pin_h = F.m.height + 2 * pad;
    struct { float x, y; int row; } st[200];
    uint32_t ns = 0;
    H.box.n = H.pos.n = H.anc.n = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = chosen[k];
        float p[3];
        cv_label_pt o;
        if (!project(&v, i, p, &o)) continue;
        const char* text = anchor_text(i, t, sizeof t);
        float lx = dx, ly = dy, w = cv_label_width(&F.m, text);
        int mv = R.of ? R.of[i] : 0;
        float pull = A.a.pull.a[i];
        if (mv) pull = -(pull > 0 ? pull : 1e-30f);  /* in front: a face must not cut it or its leader */
        if (mv) {                                  /* moved: where it was put, a leader from the point to its box */
            const cv_label_off* m = label_moved(mv - 1);
            lx += m->dx * s; ly += m->dy * s;
            float qx, qy;
            cv_label_nearest(lx - pad, ly - pad, w + 2 * pad, pin_h, 0, 0, &qx, &qy);
            if (qx * qx + qy * qy > 4 * pad * pad) cv_label_leader(&F.m, A.a.anc.a + 9 * i, 0, 0, qx, qy, pull, &gly);   /* in the text colour: it reads over any face */
        } else if (i >= A.pin0 && i < A.pin1 && ns < 200) {
            int row = 0;
            for (uint32_t j = 0; j < ns; j++)
                if (fabsf(o.sx - st[j].x) < pin_w && fabsf(o.sy - st[j].y) < pin_h * (st[j].row + 1)) row = CV_MAX(row, st[j].row + 1);
            st[ns].x = o.sx; st[ns].y = o.sy; st[ns].row = row; ns++;
            ly += row * (pin_h + 1 * s);
            if (row) cv_label_leader(&F.m, A.a.anc.a + 9 * i, dx + pad, 0, dx + pad, ly - pad, A.a.pull.a[i], &box);   /* down to the dropped label */
        }
        cv_label_layout(&F.m, A.a.anc.a + 9 * i, text, lx, ly, true, pad, pull, &gly, &box);
        if (!mv) {                                 /* not a moved one, which may be far off: the pull would show hidden labels */
            float rx = lx + w + pad, ry = CV_MAX(-dy, ly + pin_h) + pad;
            reach = CV_MAX(reach, sqrtf(rx * rx + ry * ry));
        }
        float r[4] = { o.sx + lx - pad, o.sy + ly - pad, w + 2 * pad, pin_h }, q[5] = { p[0], p[1], p[2], o.sx, o.sy };
        if (cv_reserve(H.box, H.box.n + 4) && cv_reserve(H.pos, H.pos.n + 5) && cv_push(H.anc, i)) {
            memcpy(H.box.a + H.box.n, r, sizeof r); H.box.n += 4;
            memcpy(H.pos.a + H.pos.n, q, sizeof q); H.pos.n += 5;
        }
    }
    d->label_px = H.reach = reach;                 /* the depth pull: a label clears the face its point lies on */
    cv_render_labels(box.a, (uint32_t)(box.n / CV_LABEL_FLOATS), gly.a, (uint32_t)(gly.n / CV_LABEL_FLOATS));
    cv_free_vec(gly); cv_free_vec(box); free(pts); free(chosen);
    if (!G.label_kinds) G.label_note[0] = 0;          /* measurements alone */
    else {   /* "labels: node id, loads; shown 420 of 18 000" */
        char what[40] = "";
        size_t o = 0;
        for (int k = 1; k < CV_LABEL_N && o < sizeof what - 1; k++)
            if (on(k)) o += (size_t)snprintf(what + o, sizeof what - o, "%s%s", o ? ", " : "", app_label_name(k));
        if (o >= sizeof what - 1) snprintf(what + sizeof what - 4, 4, "...");
        snprintf(G.label_note, sizeof G.label_note, "labels: %s; shown %u of %u", what, n, A.a.n);
    }
}
