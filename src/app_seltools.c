/* app_seltools.c -- the ways to select besides the box (app.h): a click on an
   element or node, the faces up to the feature edges, a chain of feature edges,
   the connected part, inverting, elements to nodes and back, a layer more or
   less, the boundary, deck sets, surfaces, element types and materials by name,
   an id list, and --select. Each goes in
   through app_sel_apply (app_select.c), which does the rest. */
#include "app_int.h"
#include "idlist.h"
#include "seltopo.h"
#include <math.h>
#include <stdio.h>
#include <strings.h>
#include <ctype.h>

static bool shown(uint32_t e) { return !G.vis || G.vis[e]; }

static void say(const char* fmt, const char* what) {
    snprintf(G.sel_note, sizeof G.sel_note, fmt, what ? what : "");
}

/* a candidate given both ways (either may be NULL): only what is shown, then the
   kinds in which by the mode. el / nd are not taken over. */
static bool take_which(const uint32_t* el, uint32_t ne, const uint32_t* nd, uint32_t nn, int mode, int which) {
    uint8_t* sn = cv_sel_shown_nodes(&G.frd, G.vis);
    uint32_t* e2 = malloc((size_t)CV_MAX(ne, 1) * sizeof *e2);
    uint32_t* n2 = malloc((size_t)CV_MAX(nn, 1) * sizeof *n2);
    if (!sn || !e2 || !n2) { free(sn); free(e2); free(n2); return false; }
    uint32_t a = 0, b = 0;
    for (uint32_t k = 0; k < ne; k++) if (el[k] < G.frd.n_elems && shown(el[k])) e2[a++] = el[k];
    for (uint32_t k = 0; k < nn; k++) if (nd[k] < G.frd.n_nodes && sn[nd[k]]) n2[b++] = nd[k];
    G.sel_note[0] = 0;
    if (!a && !b && (ne || nn)) say("none of them is shown%s", "");
    bool ok = app_sel_apply(e2, a, n2, b, mode, which);
    free(sn); free(e2); free(n2);
    return ok;
}

static bool take_both(const uint32_t* el, uint32_t ne, const uint32_t* nd, uint32_t nn, int mode) {
    return take_which(el, ne, nd, nn, mode, app_sel_which());
}

bool app_sel_take_elems(const uint32_t* el, uint32_t ne, int mode) {
    uint32_t nn = 0;
    uint32_t* nd = G.sel_nodes ? cv_sel_elem_nodes(&G.frd, el, ne, &nn) : NULL;
    bool ok = take_both(el, ne, nd, nn, mode);
    free(nd);
    return ok;
}

bool app_sel_take_nodes(const uint32_t* nd, uint32_t nn, int mode) {
    return take_which(NULL, 0, nd, nn, mode, 2);
}

static bool in_list(const uint32_t* a, uint32_t n, uint32_t v) {
    uint32_t lo = 0, hi = n;                        /* the lists are sorted */
    while (lo < hi) { uint32_t m = (lo + hi) / 2; if (a[m] < v) lo = m + 1; else hi = m; }
    return lo < n && a[lo] == v;
}

/* the click tool: the element and node under the pixel. NEW toggles it: a click on
   something selected takes it out, on anything else adds it */
static bool click_one(const cv_pick* p) {
    int mode = G.sel_mode, which = app_sel_which();
    if (mode == CV_SEL_NEW) {
        bool in = (which & 1) ? in_list(G.sel, G.sel_n, p->elem) : in_list(G.seln, G.seln_n, p->node);
        mode = in ? CV_SEL_REMOVE : CV_SEL_ADD;
    }
    G.sel_note[0] = 0;
    return app_sel_apply(&p->elem, 1, &p->node, 1, mode, which) || mode == CV_SEL_REMOVE;
}

/* the skin faces from face k up to the creases: their elements, the nodes of the faces */
static bool take_faces(uint32_t k, int mode) {
    uint32_t nf = 0;
    uint32_t* fc = cv_sel_face_flood(&G.frd, &G.skin, k, G.outline_angle, &nf);
    uint32_t* el = malloc((size_t)CV_MAX(nf, 1) * sizeof *el);
    uint32_t* nd = malloc((size_t)CV_MAX(nf, 1) * 8 * sizeof *nd);
    bool ok = false;
    if (fc && el && nd) {
        uint32_t nn = 0;
        for (uint32_t i = 0; i < nf; i++) {
            uint32_t e = G.skin.face[fc[i]] >> 3;
            el[i] = e;
            nn += (uint32_t)cv_elem_face_nodes(&G.frd, e, (int)(G.skin.face[fc[i]] & 7), nd + nn);
        }
        ok = take_both(el, nf, nd, nn, mode);
        snprintf(G.sel_note, sizeof G.sel_note, "%u faces up to the feature edges (crease %g deg)", nf, G.outline_angle);
    }
    free(fc); free(el); free(nd);
    return ok;
}

/* the feature edge nearest p, as drawn: its index in G.skin.fedge, or n_fedge */
static size_t nearest_fedge(const float p[3]) {
    float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f, sc2 = G.deform ? G.deform_scale * G.anim_factor2 : 0.f;
    size_t best = G.skin.n_fedge;
    float bd = INFINITY;
    for (size_t k = 0; k < G.skin.n_fedge; k++) {
        float a[3], b[3], d[3], t = 0, l = 0;
        for (int q = 0; q < 3; q++) {
            uint32_t i = G.skin.fedge[2 * k], j = G.skin.fedge[2 * k + 1];
            a[q] = G.frd.xyz[3 * i + q] + (G.disp ? sc * G.disp[3 * i + q] : 0) + (G.disp2 ? sc2 * G.disp2[3 * i + q] : 0);
            b[q] = G.frd.xyz[3 * j + q] + (G.disp ? sc * G.disp[3 * j + q] : 0) + (G.disp2 ? sc2 * G.disp2[3 * j + q] : 0);
        }
        for (int q = 0; q < 3; q++) { d[q] = b[q] - a[q]; t += (p[q] - a[q]) * d[q]; l += d[q] * d[q]; }
        t = l > 0 ? CV_MIN(CV_MAX(t / l, 0.f), 1.f) : 0.f;
        float e = 0;
        for (int q = 0; q < 3; q++) { float x = a[q] + t * d[q] - p[q]; e += x * x; }
        if (e < bd) { bd = e; best = k; }
    }
    return best;
}

static bool take_chain(size_t k, int mode) {
    uint32_t nn = 0;
    uint32_t* nd = cv_sel_edge_chain(&G.frd, &G.skin, k, G.outline_angle, &nn);
    bool ok = nd && app_sel_take_nodes(nd, nn, mode);
    if (nd) snprintf(G.sel_note, sizeof G.sel_note, "%u nodes along the feature edges", nn);
    free(nd);
    return ok;
}

static bool take_part(uint32_t e, int mode) {
    uint32_t ne = 0;
    uint32_t* el = cv_sel_part(&G.frd, G.vis, e, &ne);
    bool ok = el && app_sel_take_elems(el, ne, mode);
    if (el) snprintf(G.sel_note, sizeof G.sel_note, "a part of %u elements", ne);
    free(el);
    return ok;
}

bool app_sel_click(float px, float py) {
    if (!G.loaded || G.sel_tool == CV_ST_NONE) return false;
    cv_pick p;
    float o[3], d[3];
    if (!app_pick(px, py, &p, o, d) || !p.hit) { say("nothing under the cursor%s", ""); return false; }
    switch (G.sel_tool) {
    case CV_ST_CLICK: return click_one(&p);
    case CV_ST_FACE: {
        uint32_t k = cv_skin_face_of_tri(&G.frd, &G.skin, p.tri);
        if (k == UINT32_MAX) { say("no outer face there%s", ""); return false; }
        return take_faces(k, G.sel_mode);
    }
    case CV_ST_CHAIN: {
        float h[3] = { o[0] + d[0] * p.t, o[1] + d[1] * p.t, o[2] + d[2] * p.t };
        size_t k = nearest_fedge(h);
        if (k >= G.skin.n_fedge) { say("no feature edges%s", ""); return false; }
        return take_chain(k, G.sel_mode);
    }
    case CV_ST_PART: return take_part(p.elem, G.sel_mode);
    default: return false;
    }
}

bool app_sel_invert(void) {
    if (!G.loaded) return false;
    int which = app_sel_which();
    if (G.sel_n) which |= 1;                         /* what is selected turns over too */
    if (G.seln_n) which |= 2;
    uint32_t ne = 0, nn = 0;
    uint32_t* el = (which & 1) ? cv_sel_invert_elems(&G.frd, G.vis, G.sel, G.sel_n, &ne) : NULL;
    uint32_t* nd = (which & 2) ? cv_sel_invert_nodes(&G.frd, G.vis, G.seln, G.seln_n, &nn) : NULL;
    G.sel_note[0] = 0;
    bool ok = app_sel_apply(el, ne, nd, nn, CV_SEL_NEW, which);
    free(el); free(nd);
    return ok;
}

bool app_sel_to_nodes(void) {
    if (!G.sel_n) { say("no elements selected%s", ""); return false; }
    uint32_t nn = 0;
    uint32_t* nd = cv_sel_elem_nodes(&G.frd, G.sel, G.sel_n, &nn);
    G.sel_elems = false; G.sel_nodes = true;          /* the tools take nodes now */
    G.sel_note[0] = 0;
    bool ok = app_sel_apply(NULL, 0, nd, nn, CV_SEL_NEW, 2);
    free(nd);
    return ok;
}

bool app_sel_to_elems(bool any) {
    if (!G.seln_n) { say("no nodes selected%s", ""); return false; }
    uint32_t ne = 0;
    uint32_t* el = cv_sel_node_elems(&G.frd, G.vis, G.seln, G.seln_n, any, &ne);
    G.sel_elems = true; G.sel_nodes = false;
    G.sel_note[0] = 0;
    bool ok = app_sel_apply(el, ne, NULL, 0, CV_SEL_NEW, 1);
    if (!ok) say(any ? "no shown element has a selected node%s" : "no shown element has every node selected%s", "");
    free(el);
    return ok;
}

bool app_sel_grow(bool shrink) {
    if (!G.sel_n && !G.seln_n) { say("nothing selected%s", ""); return false; }
    uint32_t ne = 0, nn = 0;
    uint32_t* el = !G.sel_n ? NULL : shrink ? cv_sel_shrink_elems(&G.frd, G.vis, G.sel, G.sel_n, &ne)
                                            : cv_sel_grow_elems(&G.frd, G.vis, G.sel, G.sel_n, &ne);
    uint32_t* nd = !G.seln_n ? NULL : shrink ? cv_sel_shrink_nodes(&G.frd, G.vis, G.seln, G.seln_n, &nn)
                                             : cv_sel_grow_nodes(&G.frd, G.vis, G.seln, G.seln_n, &nn);
    int which = (G.sel_n ? 1 : 0) | (G.seln_n ? 2 : 0);
    G.sel_note[0] = 0;
    bool ok = app_sel_apply(el, ne, nd, nn, CV_SEL_NEW, which);
    free(el); free(nd);
    return ok;
}

bool app_sel_boundary(void) {
    if (!G.sel_n) { say("no elements selected%s", ""); return false; }
    uint32_t *nd = NULL, *el = NULL, nn = 0, ne = 0;
    if (!cv_sel_boundary(&G.frd, G.sel, G.sel_n, &nd, &nn, &el, &ne)) return false;
    G.sel_note[0] = 0;
    bool ok = app_sel_apply(el, ne, nd, nn, CV_SEL_NEW, 3);
    free(el); free(nd);
    return ok;
}

/* ---- by name ------------------------------------------------------------------------ */

/* deck ids to indices, those not in the model dropped */
static uint32_t* ids_to_index(const uint32_t* ids, uint32_t n, bool nodes, uint32_t* m) {
    uint32_t* r = malloc((size_t)CV_MAX(n, 1) * sizeof *r);
    *m = 0;
    if (!r) return NULL;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = nodes ? cv_frd_node_index(&G.frd, ids[k]) : cv_frd_elem_index(&G.frd, ids[k]);
        if (i != UINT32_MAX) r[(*m)++] = i;
    }
    return r;
}

static bool by_set(const cv_set* s, int mode) {
    uint32_t m = 0;
    uint32_t* ix = ids_to_index(s->ids, s->n, !s->is_elem, &m);
    if (!ix) return false;
    bool ok = s->is_elem ? app_sel_take_elems(ix, m, mode) : app_sel_take_nodes(ix, m, mode);
    free(ix);
    return ok;
}

/* an element face surface: its elements, and the nodes of its faces; a node surface: its nodes */
static bool by_surface(const cv_surface* s, int mode) {
    if (!s->n) {
        uint32_t m = 0;
        uint32_t* ix = ids_to_index(s->nodes, s->nn, true, &m);
        bool ok = ix && app_sel_take_nodes(ix, m, mode);
        free(ix);
        return ok;
    }
    uint32_t* el = malloc((size_t)s->n * sizeof *el);
    uint32_t* nd = malloc((size_t)s->n * 8 * sizeof *nd);
    if (!el || !nd) { free(el); free(nd); return false; }
    uint32_t ne = 0, nn = 0;
    for (uint32_t k = 0; k < s->n; k++) {
        uint32_t e = cv_frd_elem_index(&G.frd, s->elem[k]);
        if (e == UINT32_MAX) continue;
        el[ne++] = e;
        nn += (uint32_t)cv_elem_face_nodes(&G.frd, e, s->face[k], nd + nn);
    }
    bool ok = take_both(el, ne, nd, nn, mode);
    free(el); free(nd);
    return ok;
}

/* the elements of one value of a group axis (type, material) */
static bool by_axis(int axis, int v, int mode) {
    uint32_t* el = malloc((size_t)CV_MAX(G.frd.n_elems, 1) * sizeof *el), ne = 0;
    if (!el) return false;
    for (uint32_t e = 0; e < G.frd.n_elems; e++) if (G.groups.axis[axis].of_elem[e] == v) el[ne++] = e;
    bool ok = app_sel_take_elems(el, ne, mode);
    free(el);
    return ok;
}

static int axis_find(int axis, const char* name) {
    const cv_axis* ax = &G.groups.axis[axis];
    char* end;
    unsigned long num = strtoul(name, &end, 10);
    for (int i = 0; i < ax->n; i++) {
        uint32_t v = ax->value[i];
        if (axis == CV_AXIS_TYPE && !strcasecmp(cv_frd_type_name((int)v), name)) return i;
        if (axis == CV_AXIS_MAT) {
            const char* mn = deck_material_name(v);
            if (mn && !strcasecmp(mn, name)) return i;
        }
        if (*name && !*end && num == v) return i;
    }
    return -1;
}

bool app_sel_by_name(const char* name, int mode) {
    if (!G.loaded) return false;
    const cv_inp* d = deck_get();
    bool set = !strncasecmp(name, "set:", 4), surf = !strncasecmp(name, "surf:", 5);
    if (!strncasecmp(name, "type:", 5) || !strncasecmp(name, "mat:", 4)) {
        int axis = name[0] == 't' || name[0] == 'T' ? CV_AXIS_TYPE : CV_AXIS_MAT;
        const char* v = strchr(name, ':') + 1;
        int i = axis_find(axis, v);
        if (i < 0) { say(axis == CV_AXIS_TYPE ? "no element of type %s" : "no material %s", v); return false; }
        return by_axis(axis, i, mode);
    }
    const char* nm = set ? name + 4 : surf ? name + 5 : name;
    for (int i = 0; d && !surf && i < d->nsets; i++)
        if (!strcasecmp(d->sets[i].name, nm)) return by_set(&d->sets[i], mode);
    for (int i = 0; d && !set && i < d->nsurfs; i++)
        if (!strcasecmp(d->surfs[i].name, nm)) return by_surface(&d->surfs[i], mode);
    say(d ? "no set or surface %s in the deck" : "no deck: no set %s", nm);
    return false;
}

bool app_sel_by_ids(const char* text, bool nodes, int mode) {
    if (!G.loaded) return false;
    cv_idlist l;
    if (!cv_idlist_parse(text, &l)) { say("too many ids%s", ""); return false; }
    uint32_t m = 0;
    uint32_t* ix = ids_to_index(l.ids, (uint32_t)l.n, nodes, &m);
    bool ok = false;
    if (ix) ok = nodes ? app_sel_take_nodes(ix, m, mode) : app_sel_take_elems(ix, m, mode);
    char t[sizeof G.sel_note];
    t[0] = 0;
    size_t miss = l.n - m;
    if (l.bad) snprintf(t, sizeof t, "not ids: %s", l.bad_text);
    if (miss) snprintf(t + strlen(t), sizeof t - strlen(t), "%s%zu %s not in the model", t[0] ? "; " : "", miss, nodes ? "nodes" : "elements");
    if (t[0]) snprintf(G.sel_note, sizeof G.sel_note, "%s", t);
    free(ix);
    cv_idlist_free(&l);
    return ok;
}

/* ---- --select ------------------------------------------------------------------------ */

static int cyl_axis = 2;                                /* axis:Z@x,y,z for r, theta, axial */
static float cyl_at[3];

/* filter:x>10, filter:10<r<20, filter:type:C3D20R, filter:mat:2, field>100, field<5, top:5, facing */
static bool filter_spec(const char* spec, int mode, bool* done) {
    cv_selfilter q;
    memset(&q, 0, sizeof q);
    q.axis = cyl_axis; memcpy(q.at, cyl_at, sizeof q.at);
    *done = true;
    if (!strncasecmp(spec, "axis:", 5)) {
        char a = (char)tolower((unsigned char)spec[5]);
        if (a < 'x' || a > 'z') { say("axis: x, y or z, then @x,y,z if not through 0,0,0 (%s)", spec); return false; }
        cyl_axis = a - 'x';
        memset(cyl_at, 0, sizeof cyl_at);
        const char* p = strchr(spec, '@');
        if (p && sscanf(p + 1, "%f,%f,%f", &cyl_at[0], &cyl_at[1], &cyl_at[2]) != 3) { say("axis: x,y,z after @ (%s)", spec); return false; }
        return true;
    }
    if (!strcasecmp(spec, "facing")) return app_sel_filter_facing(mode);
    if (!strncasecmp(spec, "top:", 4)) { q.kind = CV_SQ_TOP; q.value = strtof(spec + 4, NULL); return app_sel_filter(&q, mode); }
    if (!strncasecmp(spec, "field>", 6) || !strncasecmp(spec, "field<", 6)) {
        q.kind = spec[5] == '>' ? CV_SQ_ABOVE : CV_SQ_BELOW;
        q.value = strtof(spec + 6, NULL);
        return app_sel_filter(&q, mode);
    }
    if (strncasecmp(spec, "filter:", 7)) { *done = false; return false; }
    const char* w = spec + 7;
    if (!strncasecmp(w, "type:", 5) || !strncasecmp(w, "mat:", 4)) {
        int axis = tolower((unsigned char)w[0]) == 't' ? CV_AXIS_TYPE : CV_AXIS_MAT;
        int i = axis_find(axis, strchr(w, ':') + 1);
        if (i < 0) { say("no such type or material: %s", w); return false; }
        q.kind = axis == CV_AXIS_TYPE ? CV_SQ_TYPE : CV_SQ_MAT;
        q.code = G.groups.axis[axis].value[i];
        return app_sel_filter(&q, mode);
    }
    if (!cv_selfilter_parse(w, &q)) { say("filter: x>10, 10<y<20, r<5, theta>30, axial<2, type:NAME or mat:NAME, not %s", w); return false; }
    return app_sel_filter(&q, mode);
}

bool app_sel_spec(const char* spec) {
    static const struct { const char* key; int mode; } modes[] = {
        { "new:", CV_SEL_NEW }, { "add:", CV_SEL_ADD }, { "remove:", CV_SEL_REMOVE }, { "sub:", CV_SEL_REMOVE },
        { "and:", CV_SEL_AND }, { "intersect:", CV_SEL_AND },
    };
    int mode = CV_SEL_NEW;
    for (size_t k = 0; k < CV_COUNT(modes); k++)
        if (!strncasecmp(spec, modes[k].key, strlen(modes[k].key))) { mode = modes[k].mode; spec += strlen(modes[k].key); break; }
    G.sel_note[0] = 0;
    if (!strncasecmp(spec, "ids:", 4)) return app_sel_by_ids(spec + 4, false, mode);
    if (!strncasecmp(spec, "nids:", 5)) return app_sel_by_ids(spec + 5, true, mode);
    if (!strcasecmp(spec, "invert")) return app_sel_invert();
    if (!strcasecmp(spec, "nodes")) return app_sel_to_nodes();
    if (!strcasecmp(spec, "elements") || !strcasecmp(spec, "elems")) return app_sel_to_elems(false);
    if (!strcasecmp(spec, "elements-any") || !strcasecmp(spec, "elems-any")) return app_sel_to_elems(true);
    if (!strcasecmp(spec, "clear")) { app_sel_clear(); return true; }
    if (!strncasecmp(spec, "lasso:", 6)) {               /* lasso:x,y,x,y,...: fractions of the view */
        float xy[64];
        int n = 0;
        for (const char* p = spec + 6; *p && n < 64;) {
            char* end;
            float v = strtof(p, &end);
            if (end == p) break;
            xy[n] = n % 2 ? G.vp_y + v * G.vp_h : G.vp_x + v * G.vp_w; n++;
            p = *end == ',' ? end + 1 : end;
        }
        if (n < 6) { say("lasso: three points or more, x,y fractions of the view%s", ""); return false; }
        int m = G.sel_mode;
        G.sel_mode = mode;
        bool ok = app_lasso_select(xy, n / 2);
        G.sel_mode = m;
        return ok;
    }
    bool done;
    bool fok = filter_spec(spec, mode, &done);
    if (done) return fok;
    if (!strcasecmp(spec, "grow")) return app_sel_grow(false);
    if (!strcasecmp(spec, "shrink")) return app_sel_grow(true);
    if (!strcasecmp(spec, "boundary")) return app_sel_boundary();
    if (!strncasecmp(spec, "part:", 5) || !strncasecmp(spec, "face:", 5)) {   /* part:EID, face:EID:N (the deck's face SN) */
        unsigned id = 0, fn = 0;
        int k = sscanf(spec + 5, "%u:S%u", &id, &fn);
        if (k < 2) k = sscanf(spec + 5, "%u:%u", &id, &fn);
        uint32_t e = k >= 1 ? cv_frd_elem_index(&G.frd, id) : UINT32_MAX;
        if (e == UINT32_MAX) { say("no element %s", spec + 5); return false; }
        if (spec[0] == 'p' || spec[0] == 'P') return take_part(e, mode);
        for (size_t q = 0; q < G.skin.n_face; q++)
            if (G.skin.face[q] >> 3 == e && (k < 2 || (G.skin.face[q] & 7) + 1 == fn)) return take_faces((uint32_t)q, mode);
        say("element %s has no outer face like that", spec + 5);
        return false;
    }
    if (!strncasecmp(spec, "chain:", 6)) {               /* chain:NID: the feature edges through that node */
        uint32_t v = cv_frd_node_index(&G.frd, (uint32_t)strtoul(spec + 6, NULL, 10));
        for (size_t q = 0; v != UINT32_MAX && q < G.skin.n_fedge; q++)
            if (G.skin.fedge[2 * q] == v || G.skin.fedge[2 * q + 1] == v) return take_chain(q, mode);
        say("node %s is on no feature edge", spec + 6);
        return false;
    }
    if (!strncasecmp(spec, "takes:", 6)) {               /* what the next ones take */
        const char* w = spec + 6;
        G.sel_elems = !strcasecmp(w, "elements") || !strcasecmp(w, "both");
        G.sel_nodes = !strcasecmp(w, "nodes") || !strcasecmp(w, "both");
        if (!G.sel_elems && !G.sel_nodes) { G.sel_elems = true; say("takes: elements, nodes or both, not %s", w); return false; }
        return true;
    }
    if (!strncasecmp(spec, "facing:", 7)) { G.sel_visible = !strcasecmp(spec + 7, "on"); return true; }
    return app_sel_by_name(spec, mode);
}
