/* app_overlay.c -- geometry built from the field for the overlay layers: the
   vector arrows at the nodes and the filled caps where the clip plane cuts the
   solid elements. */
#include "app_int.h"
#include "gauss.h"
#include "path.h"
#include <math.h>

/* ---- vector arrows: a 3-component field drawn at the nodes ------------------------
   Arrow from the (deformed) node along the vector; the longest one is vec_pct %
   of the model diagonal. Coloured by the current scalar, so the legend applies. */

bool app_field_is_vector(void) {
    if (!G.loaded || G.field_src != 0) return false;
    int fi = find_field(G.step, G.field_name);
    if (fi < 0) return false;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
    return d->ncomp == 3 || (G.comp <= CV_COMP_P1 && G.comp >= CV_COMP_P3_XZ && cv_tensor_order(d));
}

/* a principal value as a pair of arrows through the node along its direction:
   pointing out for tension, in for compression (the usual stress-cross picture) */
static void principal_arrows(const float* v, const uint32_t* ids, size_t n, size_t stride, cv_fvec* pos, cv_fvec* disp, cv_fvec* scal) {
    bool xz = G.comp <= CV_COMP_P1_XZ;
    int k = (xz ? CV_COMP_P1_XZ : CV_COMP_P1) - G.comp;
    float peak = 0;
    for (size_t j = 0; j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        float m = fabsf(G.scalar[i]);
        if (m == m && m > peak) peak = m;
    }
    float L = CV_MAX(G.vec_pct, 0.1f) * 0.01f * G.diag;
    for (size_t j = 0; peak > 0 && j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        float val[3], vec[3][3];
        if (!cv_principal_dirs(v + 6 * (size_t)i, xz, val, vec)) continue;
        float h = 0.5f * L * fabsf(val[k]) / peak;
        if (!(h > 0)) continue;
        const float* p = G.frd.xyz + 3 * (size_t)i;
        float d[6];
        app_node_disp6(i, d);
        size_t before = pos->n;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            float dir[3] = { vec[k][0] * sgn, vec[k][1] * sgn, vec[k][2] * sgn };
            if (val[k] >= 0) {
                float tip[3] = { p[0] + dir[0] * h, p[1] + dir[1] * h, p[2] + dir[2] * h };
                deck_arrow(pos, disp, tip, dir, h, d, false);
            } else {                                    /* head at the node, shaft outside */
                float in[3] = { -dir[0], -dir[1], -dir[2] };
                deck_arrow(pos, disp, p, in, h, d, false);
            }
        }
        for (size_t q = before; q < pos->n; q += 3) cv_push(*scal, G.scalar[i]);
    }
}

void refresh_vectors(void) {
    if (!G.show_vec || !app_field_is_vector() || !G.has_field) { cv_render_aux(CV_AUX_VECLN, NULL, NULL, NULL, 0); return; }
    int fi = find_field(G.step, G.field_name);
    const float* v = cache_get(G.step, fi);
    if (!v) { cv_render_aux(CV_AUX_VECLN, NULL, NULL, NULL, 0); return; }
    const uint32_t* ids = G.skin.n_pt ? G.skin.pt : NULL;
    size_t n = ids ? G.skin.n_pt : G.frd.n_nodes;
    size_t stride = n / 200000 + 1;                     /* huge models: a sample */
    if (G.frd.steps[G.step].fields[fi].ncomp == 6) {
        cv_fvec pos = {0}, disp = {0}, scal = {0};
        if (G.scalar) principal_arrows(v, ids, n, stride, &pos, &disp, &scal);
        app_aux_upload(CV_AUX_VECLN, &pos, &disp, scal.a);
        cv_free_vec(pos); cv_free_vec(disp); cv_free_vec(scal);
        return;
    }
    float peak = 0;
    for (size_t j = 0; j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        float m = sqrtf(v[3 * i] * v[3 * i] + v[3 * i + 1] * v[3 * i + 1] + v[3 * i + 2] * v[3 * i + 2]);
        if (m == m && m > peak) peak = m;
    }
    cv_fvec pos = {0}, disp = {0}, scal = {0};
    float L = CV_MAX(G.vec_pct, 0.1f) * 0.01f * G.diag;
    for (size_t j = 0; peak > 0 && j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        const float* r = v + 3 * i;
        float m = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        if (!(m > 0) || m != m) continue;
        float dir[3] = { r[0] / m, r[1] / m, r[2] / m }, len = L * m / peak;
        float p[3] = { G.frd.xyz[3 * i], G.frd.xyz[3 * i + 1], G.frd.xyz[3 * i + 2] };
        float tip[3] = { p[0] + dir[0] * len, p[1] + dir[1] * len, p[2] + dir[2] * len };
        float d[6];
        app_node_disp6(i, d);
        size_t before = pos.n;
        deck_arrow(&pos, &disp, tip, dir, len, d, false);
        float sv = G.scalar ? G.scalar[i] : NAN;
        for (size_t k = before; k < pos.n; k += 3) cv_push(scal, sv);
    }
    app_aux_upload(CV_AUX_VECLN, &pos, &disp, scal.a);
    cv_free_vec(pos); cv_free_vec(disp); cv_free_vec(scal);
}

void app_vectors_changed(void) { refresh_vectors(); }

/* ---- clip caps: the plane cut through the solid elements, filled ---------------------
   Each solid is split into tetrahedra over its corners; a tet crossing the plane
   gives one or two triangles. Vertices carry the undeformed position (with the
   DISPI part baked in), DISP and the value, interpolated along the cut edges, so
   the shader's deformation puts them exactly on the plane; a hair inside it, so the
   clip does not discard them. */

typedef struct { cv_fvec pos, disp, val; float n[3], eps; bool has_disp; } cap_out;

static void cap_vertex(cap_out* o, const float X[][3], const float U[][3], const float* S, int i, int j, float t) {
    for (int k = 0; k < 3; k++) cv_push(o->pos, X[i][k] + t * (X[j][k] - X[i][k]) - o->eps * o->n[k]);
    if (o->has_disp) for (int k = 0; k < 3; k++) cv_push(o->disp, U[i][k] + t * (U[j][k] - U[i][k]));
    cv_push(o->val, S[i] + t * (S[j] - S[i]));
}

static void cap_tet(cap_out* o, const int v[4], const float* sd, const float X[][3], const float U[][3], const float* S) {
    int in[4], out[4], ni = 0, no = 0;
    for (int k = 0; k < 4; k++) { if (sd[v[k]] > 0) out[no++] = v[k]; else in[ni++] = v[k]; }
    if (!ni || !no) return;
    #define CUT(a, b) cap_vertex(o, X, U, S, a, b, sd[a] / (sd[a] - sd[b]))
    if (no == 1) { CUT(out[0], in[0]); CUT(out[0], in[1]); CUT(out[0], in[2]); }
    else if (ni == 1) { CUT(in[0], out[0]); CUT(in[0], out[1]); CUT(in[0], out[2]); }
    else {                                              /* a quad: (a,c) (a,d) (b,d) (b,c) */
        CUT(out[0], in[0]); CUT(out[0], in[1]); CUT(out[1], in[1]);
        CUT(out[0], in[0]); CUT(out[1], in[1]); CUT(out[1], in[0]);
    }
    #undef CUT
}

void app_clip_caps(bool on, const float n[3], float dd, float f1, float f2) {
    static char key[256];
    char k[256];
    on = on && G.clip_cap && G.loaded && G.frd.n_elems <= 4000000;
    snprintf(k, sizeof k, "%d|%g|%g|%g|%g|%g|%g|%u|%p|%zu|%d|%d|%d", on, n[0], n[1], n[2], dd, f1, f2, G.field_gen,
             (void*)G.skin.tri, G.skin.n_tri, G.elem_mode, G.has_field, G.field_src);
    if (!strcmp(k, key)) return;
    snprintf(key, sizeof key, "%s", k);
    cap_out o = { .n = { n[0], n[1], n[2] }, .eps = 1e-5f * G.diag, .has_disp = G.disp != NULL };
    static const int hex[6][4] = { { 0, 1, 2, 6 }, { 0, 2, 3, 6 }, { 0, 3, 7, 6 }, { 0, 7, 4, 6 }, { 0, 4, 5, 6 }, { 0, 5, 1, 6 } };
    static const int wedge[3][4] = { { 0, 1, 2, 3 }, { 1, 2, 3, 4 }, { 2, 3, 4, 5 } };
    static const int tet[1][4] = { { 0, 1, 2, 3 } };
    bool nodal = G.has_field && G.field_src == 0 && !G.elem_mode && G.scalar;
    bool elem = G.has_field && G.field_src == 0 && G.elem_mode && G.elem_val;
    for (uint32_t e = 0; on && e < G.frd.n_elems; e++) {
        if (G.vis && !G.vis[e]) continue;
        int t = G.frd.etype[e], nc = t == 1 || t == 4 ? 8 : t == 2 || t == 5 ? 6 : t == 3 || t == 6 ? 4 : 0;
        uint32_t b = G.frd.eoff[e];
        if (!nc || G.frd.eoff[e + 1] - b < (uint32_t)nc) continue;
        float X[8][3], U[8][3], S[8], sd[8];
        int pos = 0, neg = 0;
        for (int i = 0; i < nc; i++) {
            uint32_t nd = G.frd.conn[b + i];
            const float* x = G.frd.xyz + 3 * (size_t)nd;
            float p = 0;
            for (int c = 0; c < 3; c++) {
                X[i][c] = x[c] + (G.disp2 ? f2 * G.disp2[3 * (size_t)nd + c] : 0.f);
                U[i][c] = G.disp ? G.disp[3 * (size_t)nd + c] : 0.f;
                p += n[c] * (X[i][c] + f1 * U[i][c]);
            }
            sd[i] = p - dd;
            S[i] = nodal ? G.scalar[nd] : elem ? G.elem_val[e] : 0.f;
            if (sd[i] > 0) pos++; else neg++;
        }
        if (!pos || !neg) continue;
        const int (*tt)[4] = nc == 8 ? hex : nc == 6 ? wedge : tet;
        int nt = nc == 8 ? 6 : nc == 6 ? 3 : 1;
        for (int q = 0; q < nt; q++) cap_tet(&o, tt[q], sd, X, U, S);
    }
    cv_render_aux(CV_AUX_CAPTRI, o.pos.a, o.has_disp ? o.disp.a : NULL, o.val.a, (uint32_t)(o.pos.n / 3));
    cv_free_vec(o.pos); cv_free_vec(o.disp); cv_free_vec(o.val);
}
