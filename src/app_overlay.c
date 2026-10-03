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

/* an arrow len long arriving at tip along dir: a tube and a cone, coloured by sv */
static void vec_arrow(cv_fvec* in, const float tip[3], const float dir[3], float len, float sv, const float d[6]) {
    float t[3], b[3];
    for (int k = 0; k < 3; k++) { t[k] = tip[k] - dir[k] * len; b[k] = tip[k] - dir[k] * 0.3f * len; }
    deck_inst(in, t, b, 0.03f * len, 0.03f * len, sv, d);
    deck_inst(in, b, tip, 0.11f * len, 0, sv, d);
}

/* a principal value as a pair of arrows through the node along its direction:
   pointing out for tension, in for compression (the usual stress-cross picture) */
static void principal_arrows(const float* v, const uint32_t* ids, size_t n, size_t stride, cv_fvec* in) {
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
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            float dir[3] = { vec[k][0] * sgn, vec[k][1] * sgn, vec[k][2] * sgn };
            if (val[k] >= 0) {
                float tip[3] = { p[0] + dir[0] * h, p[1] + dir[1] * h, p[2] + dir[2] * h };
                vec_arrow(in, tip, dir, h, G.scalar[i], d);
            } else {                                    /* head at the node, shaft outside */
                float back[3] = { -dir[0], -dir[1], -dir[2] };
                vec_arrow(in, p, back, h, G.scalar[i], d);
            }
        }
    }
}

void refresh_vectors(void) {
    if (!G.show_vec || !app_field_is_vector() || !G.has_field) { cv_render_inst(CV_INST_VEC, NULL, 0); return; }
    int fi = find_field(G.step, G.field_name);
    const float* v = cache_get(G.step, fi);
    if (!v) { cv_render_inst(CV_INST_VEC, NULL, 0); return; }
    const uint32_t* ids = G.skin.n_pt ? G.skin.pt : NULL;
    size_t n = ids ? G.skin.n_pt : G.frd.n_nodes;
    size_t stride = n / 200000 + 1;                     /* huge models: a sample */
    if (G.frd.steps[G.step].fields[fi].ncomp == 6) {
        cv_fvec in = {0};
        if (G.scalar) principal_arrows(v, ids, n, stride, &in);
        cv_render_inst(CV_INST_VEC, in.a, (uint32_t)(in.n / CV_INST_FLOATS));
        cv_free_vec(in);
        return;
    }
    float peak = 0;
    for (size_t j = 0; j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        float m = sqrtf(v[3 * i] * v[3 * i] + v[3 * i + 1] * v[3 * i + 1] + v[3 * i + 2] * v[3 * i + 2]);
        if (m == m && m > peak) peak = m;
    }
    cv_fvec in = {0};
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
        vec_arrow(&in, tip, dir, len, G.scalar ? G.scalar[i] : NAN, d);
    }
    cv_render_inst(CV_INST_VEC, in.a, (uint32_t)(in.n / CV_INST_FLOATS));
    cv_free_vec(in);
}

void app_vectors_changed(void) { refresh_vectors(); }

/* ---- clip caps: the plane cut through the solid elements, filled ---------------------
   Each solid is split into tetrahedra over its nodes; a tet crossing the plane
   gives one or two triangles. Vertices carry the undeformed position (with the
   DISPI part baked in), DISP and the value, interpolated along the cut edges, so
   the shader's deformation puts them exactly on the plane; a hair inside it, so the
   clip does not discard them. */

typedef struct { cv_fvec pos, disp, val; float n[3], eps; bool has_disp; } cap_out;

static void cap_vertex(cap_out* o, float X[][3], float U[][3], const float* S, int i, int j, float t) {
    for (int k = 0; k < 3; k++) cv_push(o->pos, X[i][k] + t * (X[j][k] - X[i][k]) - o->eps * o->n[k]);
    if (o->has_disp) for (int k = 0; k < 3; k++) cv_push(o->disp, U[i][k] + t * (U[j][k] - U[i][k]));
    cv_push(o->val, S[i] + t * (S[j] - S[i]));
}

static void cap_tet(cap_out* o, const int v[4], const float* sd, float X[][3], float U[][3], const float* S) {
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

/* A point of a quadratic element that is no node of it, the middle of an 8-node face
   or of a 20-node brick, from the element's own shape functions there: `wc` on each
   of the nc corners c[], `wm` on each of the nm mid nodes m[]. */
static void cap_extra(int at, const int* c, int nc, float wc, const int* m, int nm, float wm,
                      float X[][3], float U[][3], float* S, float* sd) {
    for (int k = 0; k < 3; k++) X[at][k] = U[at][k] = 0;
    S[at] = sd[at] = 0;
    for (int pass = 0; pass < 2; pass++) {
        const int* v = pass ? m : c;
        int n = pass ? nm : nc;
        float w = pass ? wm : wc;
        for (int i = 0; i < n; i++) {
            for (int k = 0; k < 3; k++) { X[at][k] += w * X[v[i]][k]; U[at][k] += w * U[v[i]][k]; }
            S[at] += w * S[v[i]]; sd[at] += w * sd[v[i]];
        }
    }
}

/* Linear cells: a brick as six tetrahedra about its diagonal, a wedge as three. */
static const int kHexTet[6][4] = { { 0, 1, 2, 6 }, { 0, 2, 3, 6 }, { 0, 3, 7, 6 }, { 0, 7, 4, 6 }, { 0, 4, 5, 6 }, { 0, 5, 1, 6 } };
static const int kWedgeTet[3][4] = { { 0, 1, 2, 3 }, { 1, 2, 3, 4 }, { 2, 3, 4, 5 } };

/* Quadratic cells cut through their mid nodes (.frd order: the mid nodes of the bottom
   face, of the upright edges, of the top face), so the cut shows what the nodes say
   inside the element too -- the neutral plane of a shell one element thick.
   A C3D20: its 20 nodes, the middles of its six faces (20..25) and its own (26), as
   eight bricks. A C3D15: its 15 nodes and the middles of its three quadrilateral faces
   (15..17), as eight wedges. A C3D10: four corner tetrahedra and the octahedron
   between them as four more. */
static const int kHex27[8][8] = {
    { 0, 8, 20, 11, 12, 22, 26, 25 }, { 8, 1, 9, 20, 22, 13, 23, 26 }, { 20, 9, 2, 10, 26, 23, 14, 24 },
    { 11, 20, 10, 3, 25, 26, 24, 15 }, { 12, 22, 26, 25, 4, 16, 21, 19 }, { 22, 13, 23, 26, 16, 5, 17, 21 },
    { 26, 23, 14, 24, 21, 17, 6, 18 }, { 25, 26, 24, 15, 19, 21, 18, 7 },
};
static const int kHexFace[6][8] = {       /* four corners, then the four mid nodes between them */
    { 0, 1, 2, 3, 8, 9, 10, 11 }, { 4, 5, 6, 7, 16, 17, 18, 19 }, { 0, 1, 5, 4, 8, 13, 16, 12 },
    { 1, 2, 6, 5, 9, 14, 17, 13 }, { 2, 3, 7, 6, 10, 15, 18, 14 }, { 3, 0, 4, 7, 11, 12, 19, 15 },
};
static const int kWedge18[8][6] = {
    { 0, 6, 8, 9, 15, 17 }, { 6, 1, 7, 15, 10, 16 }, { 8, 7, 2, 17, 16, 11 }, { 6, 7, 8, 15, 16, 17 },
    { 9, 15, 17, 3, 12, 14 }, { 15, 10, 16, 12, 4, 13 }, { 17, 16, 11, 14, 13, 5 }, { 15, 16, 17, 12, 13, 14 },
};
static const int kWedgeFace[3][8] = { { 0, 1, 4, 3, 6, 10, 12, 9 }, { 1, 2, 5, 4, 7, 11, 13, 10 }, { 2, 0, 3, 5, 8, 9, 14, 11 } };
static const int kTet10[8][4] = { { 0, 4, 6, 7 }, { 4, 1, 5, 8 }, { 6, 5, 2, 9 }, { 7, 8, 9, 3 },
                                  { 6, 8, 4, 5 }, { 6, 8, 5, 9 }, { 6, 8, 9, 7 }, { 6, 8, 7, 4 } };

void app_clip_caps(bool on, const float n[3], float dd, float f1, float f2) {
    static char key[256];
    char k[256];
    on = on && G.clip_cap && G.loaded && G.frd.n_elems <= 4000000;
    snprintf(k, sizeof k, "%d|%g|%g|%g|%g|%g|%g|%u|%p|%zu|%d|%d|%d|%d", on, n[0], n[1], n[2], dd, f1, f2, G.field_gen,
             (void*)G.skin.tri, G.skin.n_tri, G.elem_mode, G.has_field, G.field_src, G.mid_faces);
    if (!strcmp(k, key)) return;
    snprintf(key, sizeof key, "%s", k);
    cap_out o = { .n = { n[0], n[1], n[2] }, .eps = 1e-5f * G.diag, .has_disp = G.disp != NULL };
    static const int tet[4] = { 0, 1, 2, 3 };
    bool nodal = G.has_field && G.field_src != 1 && !G.elem_mode && G.scalar;
    bool elem = G.has_field && G.field_src != 1 && G.elem_mode && G.elem_val;
    for (uint32_t e = 0; on && e < G.frd.n_elems; e++) {
        if (G.vis && !G.vis[e]) continue;
        int t = G.frd.etype[e], nc = t == 1 || t == 4 ? 8 : t == 2 || t == 5 ? 6 : t == 3 || t == 6 ? 4 : 0;
        uint32_t b = G.frd.eoff[e], have = G.frd.eoff[e + 1] - b;
        if (!nc || have < (uint32_t)nc) continue;
        /* through the mid nodes when the element has them and the faces are drawn so */
        int nq = t == 4 ? 20 : t == 5 ? 15 : t == 6 ? 10 : 0;
        bool quad = G.mid_faces && nq && have >= (uint32_t)nq;
        int nn = quad ? nq : nc;
        float X[27][3], U[27][3], S[27], sd[27];
        int pos = 0, neg = 0;
        for (int i = 0; i < nn; i++) {
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
        if (!quad) {
            const int (*tt)[4] = nc == 8 ? kHexTet : nc == 6 ? kWedgeTet : NULL;
            int nt = nc == 8 ? 6 : nc == 6 ? 3 : 1;
            for (int q = 0; q < nt; q++) cap_tet(&o, tt ? tt[q] : tet, sd, X, U, S);
            continue;
        }
        int v[4];
        if (nq == 10) {
            for (int q = 0; q < 8; q++) cap_tet(&o, kTet10[q], sd, X, U, S);
        } else if (nq == 20) {
            static const int corners[8] = { 0, 1, 2, 3, 4, 5, 6, 7 }, mids[12] = { 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };
            for (int fc = 0; fc < 6; fc++) cap_extra(20 + fc, kHexFace[fc], 4, -0.25f, kHexFace[fc] + 4, 4, 0.5f, X, U, S, sd);
            cap_extra(26, corners, 8, -0.25f, mids, 12, 0.25f, X, U, S, sd);
            for (int c = 0; c < 8; c++)
                for (int q = 0; q < 6; q++) {
                    for (int j = 0; j < 4; j++) v[j] = kHex27[c][kHexTet[q][j]];
                    cap_tet(&o, v, sd, X, U, S);
                }
        } else {
            for (int fc = 0; fc < 3; fc++) cap_extra(15 + fc, kWedgeFace[fc], 4, -0.25f, kWedgeFace[fc] + 4, 4, 0.5f, X, U, S, sd);
            for (int c = 0; c < 8; c++)
                for (int q = 0; q < 3; q++) {
                    for (int j = 0; j < 4; j++) v[j] = kWedge18[c][kWedgeTet[q][j]];
                    cap_tet(&o, v, sd, X, U, S);
                }
        }
    }
    cv_render_aux(CV_AUX_CAPTRI, o.pos.a, o.has_disp ? o.disp.a : NULL, o.val.a, (uint32_t)(o.pos.n / 3));
    cv_free_vec(o.pos); cv_free_vec(o.disp); cv_free_vec(o.val);
}
