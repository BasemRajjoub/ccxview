/* app_inspect.c -- what a click probes besides the node (G.pick_kinds, several at
   once): the element, the face of it under the cursor, the element's edge nearest
   the cursor; each kind ticked outlined in the view as the selection is (its faces
   toned, its lines), the nodes of the face or edge and the field over them for the
   Inspector. */
#include "app_int.h"
#include "pickinfo.h"
#include "seltopo.h"
#include <math.h>

void app_pick_clear(void) { G.probe_face = G.probe_edge[0] = G.probe_edge[1] = G.probe_edge[2] = UINT32_MAX; }

/* a node where it is drawn: displaced as the shape is on screen */
static void shown_at(uint32_t n, float p[3]) {
    float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f, sc2 = G.deform ? G.deform_scale * G.anim_factor2 : 0.f;
    for (int q = 0; q < 3; q++)
        p[q] = G.frd.xyz[3 * n + q] + (G.disp ? sc * G.disp[3 * n + q] : 0) + (G.disp2 ? sc2 * G.disp2[3 * n + q] : 0);
}

/* the squared distance from h to the segment a b (as drawn) */
static float seg_d2(const float h[3], uint32_t a, uint32_t b) {
    float A[3], B[3], d[3], t = 0, l = 0, e = 0;
    shown_at(a, A); shown_at(b, B);
    for (int q = 0; q < 3; q++) { d[q] = B[q] - A[q]; t += (h[q] - A[q]) * d[q]; l += d[q] * d[q]; }
    t = l > 0 ? CV_MIN(CV_MAX(t / l, 0.f), 1.f) : 0.f;
    for (int q = 0; q < 3; q++) { float x = A[q] + t * d[q] - h[q]; e += x * x; }
    return e;
}

void app_pick_face_edge(const cv_pick* p, const float o[3], const float d[3], uint32_t* face, uint32_t edge[3]) {
    *face = edge[0] = edge[1] = edge[2] = UINT32_MAX;
    if (!p->hit || p->elem >= G.frd.n_elems) return;
    if (p->tri != UINT32_MAX) {
        uint32_t k = cv_skin_face_of_tri(&G.frd, &G.skin, p->tri);
        if (k != UINT32_MAX) *face = G.skin.face[k];
    }
    /* the element's edge nearest the point clicked */
    float h[3] = { o[0] + d[0] * p->t, o[1] + d[1] * p->t, o[2] + d[2] * p->t };
    uint32_t ed[32][3], ring[8];
    int n = cv_elem_edges(&G.frd, p->elem, ed, 32), best = -1;
    int nr = *face != UINT32_MAX ? cv_face_ring(&G.frd, *face >> 3, (int)(*face & 7), ring) : 0;
    float bd = INFINITY;
    for (int i = 0; i < n; i++) {
        int on = 0;                                  /* one of the face clicked: not one running away behind it */
        for (int j = 0; j < nr; j++) on += ring[j] == ed[i][0] || ring[j] == ed[i][1];
        if (nr && on < 2) continue;
        float e = ed[i][2] == UINT32_MAX ? seg_d2(h, ed[i][0], ed[i][1])
                : CV_MIN(seg_d2(h, ed[i][0], ed[i][2]), seg_d2(h, ed[i][2], ed[i][1]));
        if (e < bd) { bd = e; best = i; }
    }
    if (best >= 0) memcpy(edge, ed[best], sizeof ed[best]);
}

void app_pick_kind_apply(const float o[3], const float d[3]) {
    app_pick_clear();
    /* both, whatever the kinds: another kind ticked in the Inspector shows at once */
    if (G.probe_on) app_pick_face_edge(&G.probe, o, d, &G.probe_face, G.probe_edge);
}

int app_pick_head(void) {
    if (app_pk(CV_PK_FACE) && G.probe_face != UINT32_MAX) return CV_PK_FACE;
    if (app_pk(CV_PK_EDGE) && G.probe_edge[0] != UINT32_MAX) return CV_PK_EDGE;
    if (app_pk(CV_PK_NODE)) return CV_PK_NODE;
    if (app_pk(CV_PK_ELEM)) return CV_PK_ELEM;
    return app_pk(CV_PK_FACE) ? CV_PK_FACE : CV_PK_EDGE;   /* the face or edge ticked, none picked */
}

int app_pick_nodes_of(int kind, uint32_t out[8]) {
    if (!G.probe_on) return 0;
    if (kind == CV_PK_FACE && G.probe_face != UINT32_MAX && (G.probe_face >> 3) < G.frd.n_elems)
        return cv_face_ring(&G.frd, G.probe_face >> 3, (int)(G.probe_face & 7), out);
    if (kind == CV_PK_EDGE && G.probe_edge[0] < G.frd.n_nodes) {
        int n = 0;
        out[n++] = G.probe_edge[0];
        if (G.probe_edge[2] != UINT32_MAX) out[n++] = G.probe_edge[2];
        out[n++] = G.probe_edge[1];
        return n;
    }
    return 0;
}

int app_pick_nodes(uint32_t out[8]) { return app_pick_nodes_of(app_pick_head(), out); }

float app_pick_average_of(int kind) {
    uint32_t nd[8];
    int n = app_pick_nodes_of(kind, nd);
    if (!n || !G.has_field) return NAN;
    if (G.elem_mode || G.field_src == 1) return G.elem_val ? G.elem_val[G.probe.elem] : NAN;
    if (!G.scalar) return NAN;
    double s = 0;
    for (int i = 0; i < n; i++) s += G.scalar[nd[i]];
    return (float)(s / n);
}

float app_pick_average(void) { return app_pick_average_of(app_pick_head()); }

bool app_pick_anchor(float p[3]) {
    if (!G.loaded || !G.probe_on || G.probe.elem >= G.frd.n_elems || G.probe.node >= G.frd.n_nodes) return false;
    uint32_t r[32];
    int h = app_pick_head(), n = app_pick_nodes(r);
    if (!n && (h == CV_PK_FACE || h == CV_PK_EDGE)) return false;
    if (!n && h == CV_PK_ELEM) {                     /* the element: the centre of its nodes */
        uint32_t a = G.frd.eoff[G.probe.elem];
        n = (int)CV_MIN(G.frd.eoff[G.probe.elem + 1] - a, 32u);
        for (int i = 0; i < n; i++) r[i] = G.frd.conn[a + i];
    }
    if (!n) { r[0] = G.probe.node; n = 1; }
    p[0] = p[1] = p[2] = 0;
    for (int i = 0; i < n; i++) { float q[3]; shown_at(r[i], q); for (int k = 0; k < 3; k++) p[k] += q[k] / n; }
    return true;
}

static void push_node(cv_fvec* pos, cv_fvec* disp, uint32_t i) {
    float d6[6];
    app_node_disp6(i, d6);
    if (cv_reserve(*pos, pos->n + 3)) for (int q = 0; q < 3; q++) pos->a[pos->n++] = G.frd.xyz[3 * i + q];
    if (cv_reserve(*disp, disp->n + 6)) for (int q = 0; q < 6; q++) disp->a[disp->n++] = d6[q];
}

static void push_val(cv_fvec* val, uint32_t node) {
    float v = !G.has_field ? 0.f : G.elem_mode && G.elem_val ? G.elem_val[G.probe.elem] : G.scalar ? G.scalar[node] : 0.f;
    if (cv_reserve(*val, val->n + 1)) val->a[val->n++] = v;
}

/* a line along a ring of nodes, closed or not, as vertex pairs */
static void push_ring(cv_fvec* pos, cv_fvec* disp, const uint32_t* r, int n, bool closed) {
    for (int i = 0; i + 1 < n + closed; i++) { push_node(pos, disp, r[i]); push_node(pos, disp, r[(i + 1) % n]); }
}

/* the outline and toned faces of what is probed, each kind ticked: the element (its
   edges, its skin triangles), the face (its ring, its skin triangles), the edge (its
   line); a node has its ball already (app_path.c) */
static void refresh(void) {
    cv_render_aux(CV_AUX_PICKLN, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_PICKTRI, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_PICKDOT, NULL, NULL, NULL, 0);
    if (!G.loaded || !G.probe_on || G.probe.elem >= G.frd.n_elems) return;
    cv_fvec pos = {0}, disp = {0}, tp = {0}, td = {0}, val = {0};
    bool vals = G.has_field, el = app_pk(CV_PK_ELEM);
    uint32_t e = G.probe.elem, r[8];
    if (el) {
        uint32_t ed[32][3];
        int n = cv_elem_edges(&G.frd, e, ed, 32);
        for (int i = 0; i < n; i++) {
            bool m = ed[i][2] != UINT32_MAX && G.mid_faces;
            uint32_t q[3] = { ed[i][0], m ? ed[i][2] : ed[i][1], ed[i][1] };
            push_ring(&pos, &disp, q, m ? 3 : 2, false);
        }
        for (size_t t = 0; t < G.skin.n_tri; t++)
            if (G.skin.tri_elem[t] == e)
                for (int j = 0; j < 3; j++) { push_node(&tp, &td, G.skin.tri[3 * t + j]); push_val(&val, G.skin.tri[3 * t + j]); }
    }
    if (app_pk(CV_PK_FACE) && G.probe_face != UINT32_MAX) {
        int n = G.mid_faces ? app_pick_nodes_of(CV_PK_FACE, r) : cv_elem_face_corners(&G.frd, G.probe_face >> 3, (int)(G.probe_face & 7), r);
        push_ring(&pos, &disp, r, n, true);
        for (size_t t = 0; t < G.skin.n_tri && !el; t++) {   /* the face's own skin triangles: toned, not redrawn (the element's hold them) */
            if (G.skin.tri_elem[t] != e) continue;
            uint32_t k = cv_skin_face_of_tri(&G.frd, &G.skin, (uint32_t)t);
            if (k == UINT32_MAX || G.skin.face[k] != G.probe_face) continue;
            for (int j = 0; j < 3; j++) { push_node(&tp, &td, G.skin.tri[3 * t + j]); push_val(&val, G.skin.tri[3 * t + j]); }
        }
    }
    if (app_pk(CV_PK_EDGE)) {
        int n = app_pick_nodes_of(CV_PK_EDGE, r);
        if (n == 3 && !G.mid_faces) { r[1] = r[2]; n = 2; }   /* drawn straight between the corners, as the mesh is */
        push_ring(&pos, &disp, r, n, false);
    }
    app_aux_upload(CV_AUX_PICKLN, &pos, &disp, NULL);
    app_aux_dots(CV_AUX_PICKDOT, &pos, &disp);
    app_aux_upload(CV_AUX_PICKTRI, &tp, &td, vals ? val.a : NULL);
    cv_free_vec(pos); cv_free_vec(disp); cv_free_vec(tp); cv_free_vec(td); cv_free_vec(val);
}

/* redrawn when the probe, the kind, or the model, step or field under them change */
void app_pick_sync(void) {
    uint64_t k[] = { G.loaded, (uintptr_t)G.frd.xyz, (uintptr_t)G.disp, (uintptr_t)G.skin.tri, (uint64_t)G.step,
                     (uint64_t)G.field_gen, G.mid_faces, G.probe_on, G.probe.elem, (uint64_t)G.pick_kinds, G.probe_face,
                     G.probe_edge[0], G.probe_edge[1], G.elem_mode };
    static uint64_t last[sizeof k / sizeof k[0]];
    if (!memcmp(k, last, sizeof k)) return;
    memcpy(last, k, sizeof k);
    refresh();
}
