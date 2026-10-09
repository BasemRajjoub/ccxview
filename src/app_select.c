/* app_select.c -- the selection (app.h): the one place it changes (the field's
   extremes over it, the probe on its max, its outline in the view, the labels),
   the box that takes it CAD style, and what is done with it -- hidden, isolated,
   a clip through it, its ids and CSV. The other ways to select are in app_seltools.c. */
#include "app_int.h"
#include "selset.h"
#include "seltopo.h"
#include "web.h"
#include <math.h>
#include <stdio.h>

static void shown_pos(uint32_t n, float sc, float sc2, float p[3]) {
    for (int k = 0; k < 3; k++)
        p[k] = G.frd.xyz[3 * n + k] + (G.disp ? sc * G.disp[3 * n + k] : 0) + (G.disp2 ? sc2 * G.disp2[3 * n + k] : 0);
}

static void sel_upload_none(void) {
    const int w[] = { CV_AUX_SELLN, CV_AUX_SELTRI, CV_AUX_SELPT, CV_AUX_SELMAX, CV_AUX_SELMIN };
    for (size_t k = 0; k < CV_COUNT(w); k++) cv_render_aux(w[k], NULL, NULL, NULL, 0);
}

/* The box the selection came from: its nodes and facing masks (G.sel_inside, ...)
   and what was selected before it with the mode the box went in by, so a changed
   tick (elements, nodes, facing side) takes the same box again. Any other change
   forgets it. */
static struct { uint32_t* e; uint32_t ne; uint32_t* n; uint32_t nn; int mode; } base;

static void box_forget(void) {
    free(G.sel_inside); G.sel_inside = NULL;
    free(G.sel_front); G.sel_front = NULL; free(G.sel_front_e); G.sel_front_e = NULL;
    free(base.e); free(base.n);
    memset(&base, 0, sizeof base);
}

static void lists_free(void) {
    free(G.sel); G.sel = NULL; G.sel_n = 0;
    free(G.seln); G.seln = NULL; G.seln_n = 0;
}

void app_sel_clear(void) {
    bool had = G.sel_n || G.seln_n;
    lists_free();
    box_forget();
    if (had && G.label_sel_only) app_label_changed();
    G.boxq.on = false;
    sel_upload_none();
}

static void push_pt(cv_fvec* pos, cv_fvec* disp, uint32_t i) {
    float d[6];
    app_node_disp6(i, d);
    if (cv_reserve(*pos, pos->n + 3)) for (int q = 0; q < 3; q++) pos->a[pos->n++] = G.frd.xyz[3 * i + q];
    if (cv_reserve(*disp, disp->n + 6)) for (int q = 0; q < 6; q++) disp->a[disp->n++] = d[q];
}

/* one marker ball: at a node, or for an element at its first node (where the probe goes) */
static void marker(int which, uint32_t i, bool elem) {
    cv_fvec pos = {0}, disp = {0};
    push_pt(&pos, &disp, elem ? G.frd.conn[G.frd.eoff[i]] : i);
    app_aux_upload(which, &pos, &disp, NULL);
    cv_free_vec(pos); cv_free_vec(disp);
}

/* what shows the selection: the elements' outer faces toned yellow and outlined, the
   nodes as magenta dots, the max and min as red and blue balls when asked; moves with the shape */
void app_sel_refresh(void) {
    sel_upload_none();
    if (!G.loaded || (!G.sel_n && !G.seln_n)) return;
    cv_fvec pos = {0}, disp = {0}, val = {0};
    const float* nv = G.has_field && !G.elem_mode ? G.scalar : NULL;
    const float* ev = G.has_field && G.elem_mode ? G.elem_val : NULL;
    if (G.sel_n) {
        uint8_t* on = calloc(CV_MAX(G.frd.n_elems, 1), 1);
        if (!on) return;
        for (uint32_t i = 0; i < G.sel_n; i++) on[G.sel[i]] = 1;
        for (size_t k = 0; k < G.skin.n_face; k++) {     /* the corner edges of their exterior faces */
            uint32_t e = G.skin.face[k] >> 3, c[4];
            if (e >= G.frd.n_elems || !on[e]) continue;
            int m = cv_elem_face_corners(&G.frd, e, (int)(G.skin.face[k] & 7), c);
            for (int j = 0; j < m; j++) { push_pt(&pos, &disp, c[j]); push_pt(&pos, &disp, c[(j + 1) % m]); }
        }
        app_aux_upload(CV_AUX_SELLN, &pos, &disp, NULL);
        pos.n = disp.n = 0;
        for (size_t t = 0; t < G.skin.n_tri; t++) {     /* their skin triangles, coloured as the faces are */
            uint32_t e = G.skin.tri_elem[t];
            if (e >= G.frd.n_elems || !on[e]) continue;
            for (int j = 0; j < 3; j++) {
                uint32_t i = G.skin.tri[3 * t + j];
                push_pt(&pos, &disp, i);
                if (cv_reserve(val, val.n + 1)) val.a[val.n++] = nv ? nv[i] : ev ? ev[e] : 0.f;
            }
        }
        free(on);
        app_aux_upload(CV_AUX_SELTRI, &pos, &disp, nv || ev ? val.a : NULL);
    }
    if (G.seln_n) {
        pos.n = disp.n = 0;
        for (uint32_t k = 0; k < G.seln_n; k++) push_pt(&pos, &disp, G.seln[k]);
        app_aux_upload(CV_AUX_SELPT, &pos, &disp, NULL);
    }
    cv_free_vec(pos); cv_free_vec(disp); cv_free_vec(val);
    if (G.boxq.on && G.boxq.gen == G.field_gen) {
        if (G.sel_mark_max) marker(CV_AUX_SELMAX, G.boxq.max_at, G.boxq.elem);
        if (G.sel_mark_min) marker(CV_AUX_SELMIN, G.boxq.min_at, G.boxq.elem);   /* off by default: on a selection it sits on the rim */
    }
}

/* one value into the running extremes */
static void take(const float* val, uint32_t* imax, uint32_t* imin, uint32_t* cnt, uint32_t i) {
    float v = val[i];
    if (v != v) return;
    (*cnt)++;
    if (*imax == UINT32_MAX || v > val[*imax]) *imax = i;
    if (*imin == UINT32_MAX || v < val[*imin]) *imin = i;
}

/* the field's extremes over the selection into G.boxq, the probe on the max: over
   the selected nodes (or the selected elements' nodes); per element over the
   selected elements (or the shown ones holding a selected node) */
static void sel_extremes(void) {
    G.boxq.on = false;
    bool elem = G.elem_mode;
    const float* val = !G.has_field || G.field_src == 1 ? NULL : elem ? G.elem_val : G.scalar;
    uint32_t N = G.frd.n_nodes, E = G.frd.n_elems;
    uint8_t* seen = val ? calloc(CV_MAX(N, 1), 1) : NULL;
    if (!seen) return;
    uint32_t imax = UINT32_MAX, imin = UINT32_MAX, cnt = 0;
    if (!elem && G.seln_n) for (uint32_t k = 0; k < G.seln_n; k++) take(val, &imax, &imin, &cnt, G.seln[k]);
    else if (!elem) {
        for (uint32_t k = 0; k < G.sel_n; k++)
            for (uint32_t j = G.frd.eoff[G.sel[k]]; j < G.frd.eoff[G.sel[k] + 1]; j++) {
                uint32_t i = G.frd.conn[j];
                if (!seen[i]) { seen[i] = 1; take(val, &imax, &imin, &cnt, i); }
            }
    } else if (G.sel_n) for (uint32_t k = 0; k < G.sel_n; k++) take(val, &imax, &imin, &cnt, G.sel[k]);
    else {
        for (uint32_t k = 0; k < G.seln_n; k++) seen[G.seln[k]] = 1;
        for (uint32_t e = 0; e < E; e++) {
            if (G.vis && !G.vis[e]) continue;
            for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) if (seen[G.frd.conn[j]]) { take(val, &imax, &imin, &cnt, e); break; }
        }
    }
    free(seen);
    if (!cnt) return;
    app_probe_at(imax, elem);
    G.boxq.on = true; G.boxq.elem = elem; G.boxq.n = cnt; G.boxq.gen = G.field_gen;
    G.boxq.max_at = imax; G.boxq.min_at = imin; G.boxq.vmax = val[imax]; G.boxq.vmin = val[imin];
}

/* the field changed: the extremes over the same selection again (the probe stays) */
void app_sel_field_changed(void) {
    if (!G.sel_n && !G.seln_n) return;
    bool probe = G.probe_on; cv_pick p = G.probe; float pv = G.probe_value; int ip = G.probe_ip;
    sel_extremes();
    G.probe_on = probe; G.probe = p; G.probe_value = pv; G.probe_ip = ip;
    app_sel_refresh();
}

static uint32_t* dup_list(const uint32_t* a, uint32_t n) {
    uint32_t* r = n ? malloc((size_t)n * sizeof *r) : NULL;
    if (r) memcpy(r, a, (size_t)n * sizeof *r);
    return r;
}

/* the lists in which (1 elements, 2 nodes) combined with the current ones by mode;
   NEW empties the others too. Then the extremes, the probe, the outline, the labels. */
static bool apply(const uint32_t* el, uint32_t ne, const uint32_t* nd, uint32_t nn, int mode, int which) {
    if (!G.loaded) return false;
    uint32_t *e2 = NULL, *n2 = NULL, ce = 0, cn = 0;
    bool ok = true;
    if (which & 1) ok = cv_sel_combine(mode, G.sel, G.sel_n, el, ne, G.frd.n_elems, &e2, &ce);
    else if (mode != CV_SEL_NEW) { e2 = dup_list(G.sel, G.sel_n); ce = e2 ? G.sel_n : 0; }
    if (ok && (which & 2)) ok = cv_sel_combine(mode, G.seln, G.seln_n, nd, nn, G.frd.n_nodes, &n2, &cn);
    else if (ok && mode != CV_SEL_NEW) { n2 = dup_list(G.seln, G.seln_n); cn = n2 ? G.seln_n : 0; }
    if (!ok) { free(e2); free(n2); return false; }
    bool had = G.sel_n || G.seln_n;
    lists_free();
    G.sel = e2; G.sel_n = ce; G.seln = n2; G.seln_n = cn;
    if (G.sel_n || G.seln_n) sel_extremes();
    else G.boxq.on = false;
    app_sel_refresh();
    if (G.label_sel_only && (had || G.sel_n || G.seln_n)) app_label_changed();
    return G.sel_n || G.seln_n;
}

bool app_sel_apply(const uint32_t* el, uint32_t ne, const uint32_t* nd, uint32_t nn, int mode, int which) {
    box_forget();
    return apply(el, ne, nd, nn, mode, which);
}

void app_sel_drop_hidden(void) {
    if (!G.sel_n && !G.seln_n) return;
    uint8_t* sn = cv_sel_shown_nodes(&G.frd, G.vis);
    if (!sn) { app_sel_clear(); return; }
    uint32_t* el = dup_list(G.sel, G.sel_n);
    uint32_t* nd = dup_list(G.seln, G.seln_n);
    uint32_t ne = 0, nn = 0;
    for (uint32_t k = 0; el && k < G.sel_n; k++) if (!G.vis || G.vis[el[k]]) el[ne++] = el[k];
    for (uint32_t k = 0; nd && k < G.seln_n; k++) if (sn[nd[k]]) nd[nn++] = nd[k];
    if (ne == G.sel_n && nn == G.seln_n) app_sel_refresh();     /* all still there: the outline on the new skin */
    else app_sel_apply(el, ne, nd, nn, CV_SEL_NEW, 3);
    free(el); free(nd); free(sn);
}

/* what the ticks say a pick takes: 1 elements, 2 nodes, never neither */
int app_sel_which(void) { return (G.sel_elems || !G.sel_nodes ? 1 : 0) | (G.sel_nodes ? 2 : 0); }

/* screen positions of the nodes, as drawn with the camera as it is now; ok[i] 0 behind the eye */
static float* project_nodes(uint8_t** ok) {
    uint32_t N = G.frd.n_nodes;
    float* xy = malloc((size_t)CV_MAX(N, 1) * 2 * sizeof *xy);
    *ok = calloc(CV_MAX(N, 1), 1);
    if (!xy || !*ok) { free(xy); free(*ok); *ok = NULL; return NULL; }
    float mvp[16], mv[16];
    cam_matrices(mvp, mv, NULL);
    float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f, sc2 = G.deform ? G.deform_scale * G.anim_factor2 : 0.f;
    for (uint32_t i = 0; i < N; i++) {
        float p[3], c[4];
        shown_pos(i, sc, sc2, p);
        for (int r = 0; r < 4; r++) c[r] = mvp[r] * p[0] + mvp[4 + r] * p[1] + mvp[8 + r] * p[2] + mvp[12 + r];
        if (c[3] <= 1e-9f) continue;                 /* behind the eye */
        xy[2 * i] = G.vp_x + (c[0] / c[3] * 0.5f + 0.5f) * G.vp_w; xy[2 * i + 1] = G.vp_y + (0.5f - c[1] / c[3] * 0.5f) * G.vp_h;
        (*ok)[i] = 1;
    }
    return xy;
}

/* which nodes a box holds, by projection with the camera as it is now */
static uint8_t* box_nodes(float x0, float y0, float x1, float y1) {
    uint8_t* inside = NULL;
    float* xy = project_nodes(&inside);
    if (!xy) return NULL;
    float lx = CV_MIN(x0, x1), hx = CV_MAX(x0, x1), ly = CV_MIN(y0, y1), hy = CV_MAX(y0, y1);
    for (uint32_t i = 0; i < G.frd.n_nodes; i++)
        if (inside[i]) inside[i] = xy[2 * i] >= lx && xy[2 * i] <= hx && xy[2 * i + 1] >= ly && xy[2 * i + 1] <= hy;
    free(xy);
    return inside;
}

/* the side facing the camera: per node 1 when a skin triangle of its turns toward the
   eye, per element 1 when one of its does; a shell counts from both sides, and an element
   beam (no face to judge by) is 2; an interior solid element has no face toward the eye: 0.
   Triangles are oriented outward from the element's centre, as drawn (deformed). */
void app_sel_facing(uint8_t* node, uint8_t* elem) {
    const cv_frd* f = &G.frd;
    const cv_skin* s = &G.skin;
    uint32_t N = f->n_nodes, E = f->n_elems;
    memset(node, 0, N); memset(elem, 0, E);
    for (uint32_t e = 0; e < E; e++) if (f->etype[e] == 11 || f->etype[e] == 12) elem[e] = 2;   /* beams: no face to judge by */
    float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f, sc2 = G.deform ? G.deform_scale * G.anim_factor2 : 0.f;
    v3 eye, fwd, right, up;
    cam_basis(&G.cam, &eye, &fwd, &right, &up);
    bool persp = !G.cam.ortho;
    for (size_t t = 0; t < s->n_tri; t++) {
        uint32_t e = s->tri_elem[t];
        const uint32_t* c = s->tri + 3 * t;
        if (e >= E || c[0] >= N || c[1] >= N || c[2] >= N) continue;
        if (elem[e] == 2) elem[e] = 0;
        float a[3], b[3], d[3], n[3], ec[3] = { 0, 0, 0 }, mid[3], to[3];
        shown_pos(c[0], sc, sc2, a); shown_pos(c[1], sc, sc2, b); shown_pos(c[2], sc, sc2, d);
        float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { d[0] - a[0], d[1] - a[1], d[2] - a[2] };
        n[0] = e1[1] * e2[2] - e1[2] * e2[1]; n[1] = e1[2] * e2[0] - e1[0] * e2[2]; n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        for (int k = 0; k < 3; k++) mid[k] = (a[k] + b[k] + d[k]) / 3;
        to[0] = persp ? eye.x - mid[0] : -fwd.x; to[1] = persp ? eye.y - mid[1] : -fwd.y; to[2] = persp ? eye.z - mid[2] : -fwd.z;
        float dot = n[0] * to[0] + n[1] * to[1] + n[2] * to[2];
        int ty = f->etype[e];
        if (!(ty >= 7 && ty <= 10)) {                /* a solid: outward is away from the element's centre */
            uint32_t m = f->eoff[e + 1] - f->eoff[e]; float q[3];
            for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) { shown_pos(f->conn[j], sc, sc2, q); for (int k = 0; k < 3; k++) ec[k] += q[k] / m; }
            float out = n[0] * (mid[0] - ec[0]) + n[1] * (mid[1] - ec[1]) + n[2] * (mid[2] - ec[2]);
            if (out < 0) dot = -dot;
            if (dot <= 0) continue;
        }
        node[c[0]] = node[c[1]] = node[c[2]] = 1; elem[e] = 1;
    }
}

/* The selection from the nodes a box (or a lasso) held, taken over: kept with the
   selection so changed ticks take it again. The elements and / or nodes asked for,
   combined with what was selected before by the mode. false: the box took nothing. */
static bool select_from(uint8_t* inside, bool crossing) {
    if (G.sel_inside != inside) {                    /* a new box: what was there before it */
        box_forget();
        base.mode = G.sel_mode;
        if (base.mode != CV_SEL_NEW) {
            base.e = dup_list(G.sel, G.sel_n); base.ne = base.e ? G.sel_n : 0;
            base.n = dup_list(G.seln, G.seln_n); base.nn = base.n ? G.seln_n : 0;
        }
        G.sel_inside = inside;
        G.sel_crossing = crossing;
    }
    int which = app_sel_which();
    bool want_e = which & 1, want_n = which & 2;
    uint32_t N = G.frd.n_nodes, E = G.frd.n_elems;
    uint32_t* sel = malloc((size_t)CV_MAX(E, 1) * sizeof *sel);
    uint32_t* seln = want_n ? malloc((size_t)CV_MAX(N, 1) * sizeof *seln) : NULL;
    uint8_t* seen = calloc(CV_MAX(N, 1), 1);
    if (!sel || (want_n && !seln) || !seen) { free(sel); free(seln); free(seen); return false; }
    uint32_t n = want_e ? cv_box_elems(&G.frd, G.vis, inside, crossing, sel) : 0, nn = 0;
    /* the side facing the camera, when only that is wanted: judged as the box was dragged
       and kept with it, so a reselect after a turn takes the same nodes */
    if (G.sel_visible && !G.sel_front) {
        G.sel_front = malloc(CV_MAX(N, 1)); G.sel_front_e = malloc(CV_MAX(E, 1));
        if (G.sel_front && G.sel_front_e) app_sel_facing(G.sel_front, G.sel_front_e);
        else { free(G.sel_front); free(G.sel_front_e); G.sel_front = G.sel_front_e = NULL; }
    }
    uint8_t* nface = G.sel_visible ? G.sel_front : NULL; uint8_t* eface = G.sel_visible ? G.sel_front_e : NULL;
    if (eface) {                                     /* elements with a face turned toward the eye; faceless ones stay */
        uint32_t m = 0;
        for (uint32_t k = 0; k < n; k++) if (eface[sel[k]]) sel[m++] = sel[k];
        n = m;
    }
    if (want_n) {                                    /* nodes of shown elements; on the facing side when asked:
                                                        a node of an element with faces needs one of them turned this way */
        for (uint32_t e = 0; e < E; e++) {
            if (G.vis && !G.vis[e]) continue;
            uint8_t v = eface ? (eface[e] == 2 ? 2 : 1) : 1;   /* 2: an element without a face on the skin */
            for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) seen[G.frd.conn[j]] |= v;
        }
        for (uint32_t i = 0; i < N; i++) {
            if (!inside[i] || !seen[i]) continue;
            if (nface && !nface[i] && (seen[i] & 1)) continue;   /* on a faced element, but no face of it turned this way */
            seln[nn++] = i;
        }
    }
    free(seen);
    /* what was selected before the box, then the box by its mode */
    lists_free();
    G.sel = dup_list(base.e, base.ne); G.sel_n = G.sel ? base.ne : 0;
    G.seln = dup_list(base.n, base.nn); G.seln_n = G.seln ? base.nn : 0;
    apply(sel, n, seln, nn, base.mode, which);
    free(sel); free(seln);
    return n || nn;
}

bool app_box_select(float x0, float y0, float x1, float y1) {
    if (!G.loaded) return false;
    uint8_t* inside = box_nodes(x0, y0, x1, y1);
    if (!inside) return false;
    return select_from(inside, x1 < x0);             /* right to left: a crossing */
}

bool app_lasso_select(const float* poly, int np) {
    if (!G.loaded || np < 3) return false;
    uint8_t* inside = NULL;
    float* xy = project_nodes(&inside);
    if (!xy) return false;
    for (uint32_t i = 0; i < G.frd.n_nodes; i++)
        if (inside[i]) inside[i] = cv_point_in_poly(xy[2 * i], xy[2 * i + 1], poly, np);
    free(xy);
    return select_from(inside, false);
}

/* the same nodes again, whatever the camera does now: only what is taken of them changes */
bool app_box_reselect(void) {
    if (!G.sel_inside) return false;
    return select_from(G.sel_inside, G.sel_crossing);
}

/* ---- hiding by hand -------------------------------------------------------------- */

static bool hide_alloc(void) {
    if (!G.hide) G.hide = calloc(CV_MAX(G.frd.n_elems, 1), 1);
    return G.hide != NULL;
}

void app_hide_elems(const uint32_t* el, uint32_t n) {
    if (!G.loaded || !n || !hide_alloc()) return;
    for (uint32_t i = 0; i < n; i++) if (el[i] < G.frd.n_elems) G.hide[el[i]] = 1;
    G.probe_on = false;
    app_sel_clear();
    app_groups_changed();
}

void app_isolate_elems(const uint32_t* el, uint32_t n) {
    if (!G.loaded || !n || !hide_alloc()) return;
    memset(G.hide, 1, G.frd.n_elems);
    for (uint32_t i = 0; i < n; i++) if (el[i] < G.frd.n_elems) G.hide[el[i]] = 0;
    G.probe_on = false;
    app_sel_clear();
    app_groups_changed();
}

/* hide: the set's flag (Groups > Element sets shows it, and it can be undone there);
   isolate: only its elements left, by hand */
void app_hide_set(const char* name, bool isolate) {
    const cv_inp* d = deck_get();
    const cv_set* s = d ? cv_inp_set(d, name, true) : NULL;
    if (!s) return;
    if (!isolate) {
        deck_set_hidden_flags()[s - d->sets] = true;
        G.probe_on = false;
        app_sel_clear();
        app_groups_changed();
        return;
    }
    uint32_t* el = malloc((size_t)CV_MAX(s->n, 1) * sizeof *el), n = 0;
    if (!el) return;
    for (uint32_t i = 0; i < s->n; i++) {
        uint32_t e = cv_frd_elem_index(&G.frd, s->ids[i]);
        if (e != UINT32_MAX) el[n++] = e;
    }
    app_isolate_elems(el, n);
    free(el);
}

void app_show_all(void) {
    if (!G.loaded) return;
    free(G.hide); G.hide = NULL;
    for (int a = 0; a < CV_AXIS_N; a++)
        for (int i = 0; i < G.groups.axis[a].n; i++) G.groups.axis[a].on[i] = true;
    const cv_inp* d = deck_get();
    for (int i = 0; d && i < d->nsets; i++) deck_set_hidden_flags()[i] = false;
    app_groups_changed();
}

/* ---- clip here ------------------------------------------------------------------- */

void app_clip_at(const float p[3], const float n[3]) {
    if (!G.loaded) return;
    int k = 0;
    for (int a = 1; a < 3; a++) if (fabsf(n[a]) > fabsf(n[k])) k = a;
    const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
    v3 eye, f, r, u;
    cam_basis(&G.cam, &eye, &f, &r, &u);
    const float e[3] = { eye.x, eye.y, eye.z };
    G.clip_axis = k;
    G.clip_pos = hi[k] > lo[k] ? CV_MIN(CV_MAX((p[k] - lo[k]) / (hi[k] - lo[k]), 0.f), 1.f) : 0.5f;
    G.clip_flip = e[k] < p[k];                       /* the clip drops n . x > d: the eye's side goes */
    G.clip_on = true;
}

/* ---- the selection out ------------------------------------------------------------- */

size_t app_sel_ids(char* out, size_t n) {
    bool nodes = !G.sel_n;                           /* the elements, else the nodes */
    uint32_t cnt = nodes ? G.seln_n : G.sel_n;
    size_t o = 0;
    if (n) out[0] = 0;
    for (uint32_t i = 0; i < cnt && o + 16 < n; i++)
        o += (size_t)snprintf(out + o, n - o, "%u%s", nodes ? G.frd.node_id[G.seln[i]] : G.frd.elem_id[G.sel[i]],
                              i + 1 == cnt ? "\n" : i % 16 == 15 ? ",\n" : ", ");
    return o;
}

bool app_sel_csv(void) {
    if (!G.loaded || (!G.sel_n && !G.seln_n)) return false;
    char base[1024], path[1100];
    snprintf(base, sizeof base, "%s", G.path);
    char* dot = strrchr(base, '.');
    char* sep = strrchr(base, cv_path_sep());
    if (dot && (!sep || dot > sep)) *dot = 0;
    snprintf(path, sizeof path, "%s_selection.csv", base);
    FILE* fp = fopen(path, "w");
    bool ok = fp != NULL;
    if (fp && !G.sel_n) {                            /* nodes only: a row per node */
        bool val = G.has_field && G.field_src != 1 && !G.elem_mode;
        fprintf(fp, "node,x,y,z%s\n", val ? ",value" : "");
        for (uint32_t k = 0; k < G.seln_n; k++) {
            uint32_t i = G.seln[k];
            const float* x = G.frd.xyz + 3 * i;
            fprintf(fp, "%u,%.9g,%.9g,%.9g", G.frd.node_id[i], x[0], x[1], x[2]);
            if (val) fprintf(fp, ",%.9g", G.scalar[i]);
            fputc('\n', fp);
        }
        ok = fclose(fp) == 0;
        fp = NULL;
    }
    if (fp) {
        bool val = G.has_field && G.field_src != 1;
        fprintf(fp, "element,type,material,x,y,z%s\n", !val ? "" : G.elem_mode ? ",value" : ",min,max");
        for (uint32_t k = 0; k < G.sel_n; k++) {
            uint32_t e = G.sel[k], b = G.frd.eoff[e], m = G.frd.eoff[e + 1] - b;
            double c[3] = { 0, 0, 0 };
            float mn = INFINITY, mx = -INFINITY;
            for (uint32_t j = 0; j < m; j++) {
                uint32_t i = G.frd.conn[b + j];
                for (int a = 0; a < 3; a++) c[a] += G.frd.xyz[3 * i + a] / (double)m;
                if (val && !G.elem_mode && G.scalar[i] == G.scalar[i]) { mn = CV_MIN(mn, G.scalar[i]); mx = CV_MAX(mx, G.scalar[i]); }
            }
            fprintf(fp, "%u,%s,%u,%.9g,%.9g,%.9g", G.frd.elem_id[e], cv_frd_type_name(G.frd.etype[e]), G.frd.emat[e], c[0], c[1], c[2]);
            if (val && G.elem_mode) fprintf(fp, ",%.9g", G.elem_val[e]);
            else if (val) fprintf(fp, ",%.9g,%.9g", mn, mx);
            fputc('\n', fp);
        }
        ok = fclose(fp) == 0;
    }
    snprintf(G.note, sizeof G.note, ok ? "saved %s" : "could not write %s", path);
    G.note_t = cv_now();
    cv_msg_add(&G.msgs, 0, false, G.note);
    if (ok) CV_EXPORTED(path);
    return ok;
}

/* ---- the context menu ---------------------------------------------------------------- */

void app_menu_open(float x, float y) {
    if (!G.loaded) return;
    float o[3], d[3];
    G.menu_on = true; G.menu_x = x; G.menu_y = y;
    memset(G.menu_n, 0, sizeof G.menu_n);
    if (!app_pick(x, y, &G.menu_pick, o, d)) return;
    for (int k = 0; k < 3; k++) G.menu_p[k] = o[k] + d[k] * G.menu_pick.t;
    if (G.menu_pick.tri < G.skin.n_tri) {             /* the face's normal, as shown */
        float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f, sc2 = G.deform ? G.deform_scale * G.anim_factor2 : 0.f, q[3][3];
        for (int j = 0; j < 3; j++) shown_pos(G.skin.tri[3 * G.menu_pick.tri + j], sc, sc2, q[j]);
        float a[3], b[3];
        for (int k = 0; k < 3; k++) { a[k] = q[1][k] - q[0][k]; b[k] = q[2][k] - q[0][k]; }
        G.menu_n[0] = a[1] * b[2] - a[2] * b[1]; G.menu_n[1] = a[2] * b[0] - a[0] * b[2]; G.menu_n[2] = a[0] * b[1] - a[1] * b[0];
    }
}
