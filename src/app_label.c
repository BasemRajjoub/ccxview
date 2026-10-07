/* app_label.c -- labels on the model (label.h): what to label for the chosen kind, the
   font atlas, and the per-frame thinning, layout and upload. Only what can be seen gets
   a label: skin nodes, exterior faces, shown elements. */
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
};
const char* app_label_name(int k) { return k >= 0 && k < CV_LABEL_N ? kinds[k].name : "?"; }
const char* app_label_key(int k) { return k >= 0 && k < CV_LABEL_N ? kinds[k].key : "none"; }
int app_label_find(const char* key) {
    for (int k = 0; k < CV_LABEL_N; k++) if (!strcasecmp(kinds[k].key, key)) return k;
    return -1;
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
    CV_VEC(uint32_t) ref;               /* per anchor: node / element index, UINT32_MAX none */
    CV_VEC(char) txt;                   /* texts, 0-terminated, back to back */
    CV_VEC(uint32_t) toff;              /* text offset per anchor, UINT32_MAX: formatted from ref */
    uint32_t n;
} anchors;

static struct { anchors a; unsigned gen, serial; int kind, asked; bool sel_only, probe_only, probe_on; uint32_t probe_elem; } A;
/* kind: the one built (a value field per element becomes evalue); asked: G.label_kind then;
   probe_*: the probe these were built for (probe-only labels follow it) */

static void pack(anchors* s, const float p[3], const float d[6], uint32_t ref, const char* text) {
    if (!cv_reserve(s->anc, s->anc.n + 9) || !cv_reserve(s->toff, s->toff.n + 1) || !cv_reserve(s->ref, s->ref.n + 1)) return;
    size_t len = text ? strlen(text) + 1 : 0;
    if (len && !cv_reserve(s->txt, s->txt.n + len)) return;
    for (int k = 0; k < 3; k++) s->anc.a[s->anc.n++] = p[k];
    for (int k = 0; k < 6; k++) s->anc.a[s->anc.n++] = d[k];
    s->ref.a[s->ref.n++] = ref;
    s->toff.a[s->toff.n++] = text ? (uint32_t)s->txt.n : UINT32_MAX;
    if (len) { memcpy(s->txt.a + s->txt.n, text, len); s->txt.n += len; }
    s->n++;
}
static void anchor_add(const float p[3], const float d[6], const char* text) { pack(&A.a, p, d, UINT32_MAX, text); }
static void anchor_ref(const float p[3], const float d[6], uint32_t ref) { pack(&A.a, p, d, ref, NULL); }

static void anchor_node(uint32_t i) {
    float d[6];
    app_node_disp6(i, d);
    anchor_ref(G.frd.xyz + 3 * i, d, i);
}

/* the text of an anchor: its own, or its id / value formatted now */
static const char* anchor_text(uint32_t i, char* buf, size_t n) {
    uint32_t off = A.a.toff.a[i], ref = A.a.ref.a[i];
    if (off != UINT32_MAX) return A.a.txt.a + off;
    switch (A.kind) {
    case CV_LABEL_NODE:   snprintf(buf, n, "%u", G.frd.node_id[ref]); break;
    case CV_LABEL_ELEM:   snprintf(buf, n, "%u", G.frd.elem_id[ref]); break;
    case CV_LABEL_VALUE:  app_legend_fmt(buf, n, G.scalar[ref]); break;
    case CV_LABEL_EVALUE: app_legend_fmt(buf, n, G.elem_val[ref]); break;
    default: buf[0] = 0;
    }
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
void label_sink_begin(int kind) { S.on = kind == CV_LABEL_LOADS || kind == CV_LABEL_SUPPORTS; S.a.anc.n = S.a.txt.n = S.a.toff.n = S.a.ref.n = 0; S.a.n = 0; }
void label_sink_add(const float p[3], const float d[6], const char* text) { if (S.on) pack(&S.a, p, d, UINT32_MAX, text); }

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
            mean_anchor(&m, st->name);
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
            mean_anchor(&m, sf->name);
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
            mean_anchor(&m[mi], t);
        }
        free(m);
        break;
    }
    case CV_LABEL_LOADS: case CV_LABEL_SUPPORTS:  /* at the symbols: what loads_refresh recorded */
        loads_refresh();
        for (uint32_t i = 0; i < S.a.n; i++) anchor_add(S.a.anc.a + 9 * i, S.a.anc.a + 9 * i + 3, S.a.txt.a + S.a.toff.a[i]);
        break;
    default: break;
    }
}

/* "el N" at an element's centre (probe-only labels are drawn on top, so a centre inside it shows) */
static void elem_label(uint32_t e) {
    const cv_frd* f = &G.frd;
    uint32_t b = f->eoff[e], m = f->eoff[e + 1] - b;
    if (!m) return;
    float p[3] = { 0, 0, 0 }, d[6] = { 0 }, dj[6];
    for (uint32_t j = 0; j < m; j++) {
        uint32_t i = f->conn[b + j];
        app_node_disp6(i, dj);
        for (int q = 0; q < 3; q++) p[q] += f->xyz[3 * i + q] / m;
        for (int q = 0; q < 6; q++) d[q] += dj[q] / m;
    }
    char t[32];
    snprintf(t, sizeof t, "el %u", f->elem_id[e]);
    anchor_add(p, d, t);
}

static void build_anchors(void) {
    A.a.anc.n = A.a.txt.n = A.a.toff.n = A.a.ref.n = 0; A.a.n = 0;
    A.kind = A.asked = G.label_kind; A.gen = G.label_gen; A.sel_only = G.label_sel_only; A.probe_only = G.label_probe_only;
    A.probe_on = G.probe_on; A.probe_elem = G.probe.elem;
    A.serial++;
    if (!G.loaded || G.label_kind == CV_LABEL_NONE) return;
    const cv_frd* f = &G.frd;
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
    bool nodal = G.has_field && !G.elem_mode && G.field_src != 1 && G.scalar;
    bool elemv = G.has_field && (G.elem_mode || G.field_src == 1) && G.elem_val;
    int kind = G.label_kind;
    if (kind == CV_LABEL_VALUE && !nodal && elemv) kind = CV_LABEL_EVALUE;   /* a per-element field: on the faces */
    A.kind = kind;
    switch (kind) {
    case CV_LABEL_NODE: case CV_LABEL_VALUE:
        if (kind == CV_LABEL_VALUE && !nodal) break;
        if (G.label_probe_only) {                 /* the probed element's nodes, and its own id */
            if (!G.probe_on) break;
            uint32_t e = G.probe.elem;
            for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) {
                uint32_t i = f->conn[j];
                if (kind == CV_LABEL_VALUE && G.scalar[i] != G.scalar[i]) continue;
                anchor_node(i);
            }
            if (kind == CV_LABEL_NODE) elem_label(e);
            break;
        }
        for (size_t k = 0; k < G.skin.n_pt; k++) {
            uint32_t i = G.skin.pt[k];
            if (G.label_sel_only && !(nmark && nmark[i])) continue;
            if (kind == CV_LABEL_VALUE && G.scalar[i] != G.scalar[i]) continue;
            anchor_node(i);
        }
        break;
    case CV_LABEL_ELEM: case CV_LABEL_EVALUE: {
        if (kind == CV_LABEL_EVALUE && !elemv) break;
        uint8_t* done = calloc(CV_MAX(f->n_elems, 1), 1);
        if (!done) break;
        if (G.label_probe_only) {                 /* the probed element, wherever it is (a cap, a found element) */
            if (G.probe_on && kind == CV_LABEL_ELEM) elem_label(G.probe.elem);
            free(done);
            break;
        }
        for (size_t k = 0; k < G.skin.n_face; k++) {   /* each element once, on its first exterior face */
            uint32_t e = G.skin.face[k] >> 3;
            if (e >= f->n_elems || done[e]) continue;
            done[e] = 1;
            if (G.label_sel_only && !(emark && emark[e])) continue;
            float p[3], d[6];
            if (!face_centre(e, (int)(G.skin.face[k] & 7), p, d)) continue;
            if (kind == CV_LABEL_EVALUE && G.elem_val[e] != G.elem_val[e]) continue;
            anchor_ref(p, d, e);
        }
        free(done);
        break;
    }
    default: build_named(kind); break;
    }
    free(nmark); free(emark);
}

void app_label_changed(void) { G.label_gen++; }

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

void app_label_frame(const cv_draw* d) {
    static float last_mvp[16], last_px, last_sp, last_f1, last_f2, last_clip[4]; static unsigned last_gen; static int last_kind, last_w, last_h;
    if (!G.loaded || G.label_kind == CV_LABEL_NONE) {
        if (A.a.n || last_kind) {
            A.a.n = 0; A.asked = -1;                  /* the same kind again rebuilds */
            free(C.keep); C.keep = NULL; C.n = 0;
            cv_render_labels(NULL, 0, NULL, 0); G.label_note[0] = 0; last_kind = 0;
        }
        return;
    }
    float px = roundf(CV_MAX(G.label_px, 6.f) * ui_scale());
    if (F.px != px) font_bake(px);           /* a failed bake is tried again only at another size */
    if (!F.live) return;
    bool rebuild = A.gen != G.label_gen || A.asked != G.label_kind || A.sel_only != G.label_sel_only || A.probe_only != G.label_probe_only ||
                   (G.label_probe_only && (A.probe_on != G.probe_on || A.probe_elem != G.probe.elem));
    if (rebuild) build_anchors();
    float clip[4] = { d->clip ? d->clip_n[0] : 0, d->clip ? d->clip_n[1] : 0, d->clip ? d->clip_n[2] : 0, d->clip ? d->clip_d : 0 };
    bool moved = memcmp(last_mvp, d->mvp, sizeof last_mvp) != 0 || last_px != px || last_sp != G.label_spacing ||
                 last_w != d->vp_w || last_h != d->vp_h || last_f1 != d->def_scale || last_f2 != d->def_scale2 ||
                 memcmp(last_clip, clip, sizeof clip) != 0;
    if (!rebuild && !moved && last_gen == G.label_gen && last_kind == G.label_kind) return;

    float spacing = G.label_spacing * ui_scale();
    /* which anchors to project: all, or the coarse subset of a huge set */
    const uint32_t* ids = NULL; uint32_t n_ids = A.a.n;
    if (A.a.n > 200000 && spacing > 0) {
        coarse_check(spacing * app_pixel_size(G.cam.target));
        if (C.keep) { ids = C.keep; n_ids = C.n; }
    }
    cv_label_pt* pts = malloc((size_t)CV_MAX(n_ids, 1) * sizeof *pts);
    uint32_t* chosen = malloc((size_t)CV_MAX(n_ids, 1) * sizeof *chosen);
    if (!pts || !chosen) { free(pts); free(chosen); return; }   /* tried again next frame */
    memcpy(last_mvp, d->mvp, sizeof last_mvp); memcpy(last_clip, clip, sizeof last_clip); last_px = px; last_sp = G.label_spacing; last_gen = G.label_gen;
    last_kind = G.label_kind; last_w = d->vp_w; last_h = d->vp_h; last_f1 = d->def_scale; last_f2 = d->def_scale2;
    const float* M = d->mvp;
    uint32_t np = 0;
    for (uint32_t k = 0; k < n_ids; k++) {
        uint32_t i = ids ? ids[k] : k;
        const float* a = A.a.anc.a + 9 * i;
        float p[3], c[4];
        for (int q = 0; q < 3; q++) p[q] = a[q] + d->def_scale * a[3 + q] + d->def_scale2 * a[6 + q];
        if (p[0] != p[0] || p[1] != p[1] || p[2] != p[2]) continue;      /* no position (NaN displacement) */
        if (d->clip && p[0] * d->clip_n[0] + p[1] * d->clip_n[1] + p[2] * d->clip_n[2] > d->clip_d) continue;   /* cut away */
        for (int r = 0; r < 4; r++) c[r] = M[r] * p[0] + M[4 + r] * p[1] + M[8 + r] * p[2] + M[12 + r];
        if (c[3] <= 1e-9f) continue;                                      /* behind the eye */
        pts[np].sx = d->vp_x + (c[0] / c[3] * 0.5f + 0.5f) * d->vp_w;
        pts[np].sy = d->vp_y + (0.5f - c[1] / c[3] * 0.5f) * d->vp_h;
        pts[np].depth = c[2] / c[3] * 0.5f + 0.5f;
        pts[np].id = i;
        np++;
    }
    uint32_t max_out = 20000;                     /* more than fits any screen: a cap for spacing 0 */
    uint32_t n = cv_label_thin(pts, np, spacing, (float)d->vp_x, (float)d->vp_y, (float)d->vp_w, (float)d->vp_h, chosen, max_out);
    /* lay the chosen out: text a little right of and above the point */
    cv_fvec gly = {0}, box = {0};
    float dx = 4 * ui_scale(), dy = -(F.m.height + 3 * ui_scale());
    char t[64];
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = chosen[k];
        cv_label_layout(&F.m, A.a.anc.a + 9 * i, anchor_text(i, t, sizeof t), dx, dy, true, 2 * ui_scale(), &gly, &box);
    }
    cv_render_labels(box.a, (uint32_t)(box.n / CV_LABEL_FLOATS), gly.a, (uint32_t)(gly.n / CV_LABEL_FLOATS));
    cv_free_vec(gly); cv_free_vec(box); free(pts); free(chosen);
    snprintf(G.label_note, sizeof G.label_note, "labels: %s, shown %u of %u", app_label_name(A.kind), n, A.a.n);
}
