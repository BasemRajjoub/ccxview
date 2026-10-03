/* cap.c -- the filled cut of a plane through the solid elements (cap.h). */
#include "cap.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- the cells ---------------------------------------------------------------- */

/* Linear cells: a brick as six tetrahedra about its diagonal, a wedge as three. */
static const int kHexTet[6][4] = { { 0, 1, 2, 6 }, { 0, 2, 3, 6 }, { 0, 3, 7, 6 }, { 0, 7, 4, 6 }, { 0, 4, 5, 6 }, { 0, 5, 1, 6 } };
static const int kWedgeTet[3][4] = { { 0, 1, 2, 3 }, { 1, 2, 3, 4 }, { 2, 3, 4, 5 } };
static const int kTet[4] = { 0, 1, 2, 3 };

/* Quadratic cells, in the .frd order of the connectivity (the mid nodes of the
   bottom face, of the upright edges, of the top face).
   kHex27: eight bricks over the 20 nodes, the middles of the six faces (20..25, as
   kHexFace lists them) and the brick's own middle (26).
   kWedge18: eight wedges over the 15 nodes and the middles of the three
   quadrilateral faces (15..17, kWedgeFace).
   kTet10: the four corner tetrahedra, and the octahedron between them as four more.
   A face table: four corners, then the four mid nodes between them. */
static const int kHex27[8][8] = {
    { 0, 8, 20, 11, 12, 22, 26, 25 }, { 8, 1, 9, 20, 22, 13, 23, 26 }, { 20, 9, 2, 10, 26, 23, 14, 24 },
    { 11, 20, 10, 3, 25, 26, 24, 15 }, { 12, 22, 26, 25, 4, 16, 21, 19 }, { 22, 13, 23, 26, 16, 5, 17, 21 },
    { 26, 23, 14, 24, 21, 17, 6, 18 }, { 25, 26, 24, 15, 19, 21, 18, 7 },
};
static const int kHexFace[6][8] = {
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

/* corners of a solid's type (0: not a solid), and its node count when quadratic (0: linear) */
static int corners_of(int t) { return t == 1 || t == 4 ? 8 : t == 2 || t == 5 ? 6 : t == 3 || t == 6 ? 4 : 0; }
static int quad_nodes(int t) { return t == 4 ? 20 : t == 5 ? 15 : t == 6 ? 10 : 0; }

int cv_cap_cells(int type, bool mid, int tets[][4], int max) {
    int n = 0, nq = mid ? quad_nodes(type) : 0, nc = corners_of(type);
#define ADD(a, b, c, d) do { if (n < max) { tets[n][0] = (a); tets[n][1] = (b); tets[n][2] = (c); tets[n][3] = (d); } n++; } while (0)
    if (nq == 10) for (int q = 0; q < 8; q++) ADD(kTet10[q][0], kTet10[q][1], kTet10[q][2], kTet10[q][3]);
    else if (nq == 20) { for (int c = 0; c < 8; c++) for (int q = 0; q < 6; q++)
        ADD(kHex27[c][kHexTet[q][0]], kHex27[c][kHexTet[q][1]], kHex27[c][kHexTet[q][2]], kHex27[c][kHexTet[q][3]]); }
    else if (nq == 15) { for (int c = 0; c < 8; c++) for (int q = 0; q < 3; q++)
        ADD(kWedge18[c][kWedgeTet[q][0]], kWedge18[c][kWedgeTet[q][1]], kWedge18[c][kWedgeTet[q][2]], kWedge18[c][kWedgeTet[q][3]]); }
    else if (nc == 8) for (int q = 0; q < 6; q++) ADD(kHexTet[q][0], kHexTet[q][1], kHexTet[q][2], kHexTet[q][3]);
    else if (nc == 6) for (int q = 0; q < 3; q++) ADD(kWedgeTet[q][0], kWedgeTet[q][1], kWedgeTet[q][2], kWedgeTet[q][3]);
    else if (nc == 4) ADD(kTet[0], kTet[1], kTet[2], kTet[3]);
#undef ADD
    return n;
}

/* A point of a quadratic element that is no node of it, the middle of an 8-node
   face or of a 20-node brick, from the shape functions there: wc on each of the nc
   corners c[], wm on each of the nm mid nodes m[]; q: values per point, w wide. */
static void extra(float* q, int w, int at, const int* c, int nc, float wc, const int* m, int nm, float wm) {
    for (int k = 0; k < w; k++) {
        float v = 0;
        for (int i = 0; i < nc; i++) v += wc * q[c[i] * w + k];
        for (int i = 0; i < nm; i++) v += wm * q[m[i] * w + k];
        q[at * w + k] = v;
    }
}

int cv_cap_points(int type, bool mid, float* q, int w) {
    int nq = mid ? quad_nodes(type) : 0;
    if (nq == 20) {
        static const int corners[8] = { 0, 1, 2, 3, 4, 5, 6, 7 }, mids[12] = { 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };
        for (int fc = 0; fc < 6; fc++) extra(q, w, 20 + fc, kHexFace[fc], 4, -0.25f, kHexFace[fc] + 4, 4, 0.5f);
        extra(q, w, 26, corners, 8, -0.25f, mids, 12, 0.25f);
        return 27;
    }
    if (nq == 15) {
        for (int fc = 0; fc < 3; fc++) extra(q, w, 15 + fc, kWedgeFace[fc], 4, -0.25f, kWedgeFace[fc] + 4, 4, 0.5f);
        return 18;
    }
    return nq ? nq : corners_of(type);
}

/* ---- the plane ------------------------------------------------------------------ */

bool cv_cap_prepare(cv_cap_prep* p, const cv_cap_model* m) {
    const cv_frd* f = m->f;
    memset(p, 0, sizeof *p);
    p->s = malloc(CV_MAX(f->n_nodes, 1) * sizeof(float));
    p->lo = malloc(CV_MAX(f->n_elems, 1) * sizeof(float));
    p->hi = malloc(CV_MAX(f->n_elems, 1) * sizeof(float));
    if (!p->s || !p->lo || !p->hi) { cv_cap_prep_free(p); return false; }
    p->n_nodes = f->n_nodes; p->n_elems = f->n_elems;
    for (uint32_t i = 0; i < f->n_nodes; i++) {
        float s = 0;
        for (int c = 0; c < 3; c++)
            s += m->n[c] * (f->xyz[3 * (size_t)i + c] + (m->disp ? m->f1 * m->disp[3 * (size_t)i + c] : 0.f)
                                                      + (m->disp2 ? m->f2 * m->disp2[3 * (size_t)i + c] : 0.f));
        p->s[i] = s;
    }
    for (uint32_t e = 0; e < f->n_elems; e++) {
        int nc = corners_of(f->etype[e]), nq = m->mid ? quad_nodes(f->etype[e]) : 0;
        uint32_t b = f->eoff[e], have = f->eoff[e + 1] - b;
        int nn = nq && have >= (uint32_t)nq ? nq : nc;
        p->lo[e] = 1; p->hi[e] = 0;                    /* empty: never crossed */
        if (!nc || have < (uint32_t)nc || (m->vis && !m->vis[e])) continue;
        float lo = INFINITY, hi = -INFINITY;
        for (int i = 0; i < nn; i++) { float s = p->s[f->conn[b + i]]; lo = fminf(lo, s); hi = fmaxf(hi, s); }
        if (lo <= hi) { p->lo[e] = lo; p->hi[e] = hi; }    /* a NaN position leaves it empty */
    }
    return true;
}

void cv_cap_prep_free(cv_cap_prep* p) {
    free(p->s); free(p->lo); free(p->hi);
    memset(p, 0, sizeof *p);
}
void cv_cap_out_free(cv_cap_out* o) { cv_free_vec(o->pos); cv_free_vec(o->disp); cv_free_vec(o->val); }

/* the points of one element: 3 position, 3 displacement, value, distance to the plane */
enum { PW = 8, PX = 0, PU = 3, PS = 6, PD = 7 };

typedef struct { cv_cap_out* o; const float* n; float eps; bool has_disp; const float* q; } cut_t;

static void vertex(const cut_t* c, int i, int j) {
    const float *a = c->q + i * PW, *b = c->q + j * PW;
    float t = a[PD] / (a[PD] - b[PD]);
    for (int k = 0; k < 3; k++) cv_push(c->o->pos, a[PX + k] + t * (b[PX + k] - a[PX + k]) - c->eps * c->n[k]);
    if (c->has_disp) for (int k = 0; k < 3; k++) cv_push(c->o->disp, a[PU + k] + t * (b[PU + k] - a[PU + k]));
    cv_push(c->o->val, a[PS] + t * (b[PS] - a[PS]));
}

static void cut_tet(const cut_t* c, const int v[4]) {
    int in[4], out[4], ni = 0, no = 0;
    for (int k = 0; k < 4; k++) { if (c->q[v[k] * PW + PD] > 0) out[no++] = v[k]; else in[ni++] = v[k]; }
    if (!ni || !no) return;
    if (no == 1) { vertex(c, out[0], in[0]); vertex(c, out[0], in[1]); vertex(c, out[0], in[2]); }
    else if (ni == 1) { vertex(c, in[0], out[0]); vertex(c, in[0], out[1]); vertex(c, in[0], out[2]); }
    else {                                              /* a quad: (a,c) (a,d) (b,d) (b,c) */
        vertex(c, out[0], in[0]); vertex(c, out[0], in[1]); vertex(c, out[1], in[1]);
        vertex(c, out[0], in[0]); vertex(c, out[1], in[1]); vertex(c, out[1], in[0]);
    }
}

void cv_cap_cut(const cv_cap_model* m, const cv_cap_prep* p, float d, float eps,
                const float* node_val, const float* elem_val, cv_cap_out* out) {
    const cv_frd* f = m->f;
    if (!p->s || p->n_elems != f->n_elems || p->n_nodes != f->n_nodes) return;
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (!(p->lo[e] <= d && d < p->hi[e])) continue;         /* the plane misses it: nearly every element */
        int t = f->etype[e], nc = corners_of(t), nq = m->mid ? quad_nodes(t) : 0;
        uint32_t b = f->eoff[e];
        bool quad = nq && f->eoff[e + 1] - b >= (uint32_t)nq;
        int nn = quad ? nq : nc;
        float q[27 * PW];
        for (int i = 0; i < nn; i++) {
            uint32_t nd = f->conn[b + i];
            float* r = q + i * PW;
            for (int c = 0; c < 3; c++) {
                r[PX + c] = f->xyz[3 * (size_t)nd + c] + (m->disp2 ? m->f2 * m->disp2[3 * (size_t)nd + c] : 0.f);
                r[PU + c] = m->disp ? m->disp[3 * (size_t)nd + c] : 0.f;
            }
            r[PS] = node_val ? node_val[nd] : elem_val ? elem_val[e] : 0.f;
            r[PD] = p->s[nd] - d;
        }
        cv_cap_points(t, quad, q, PW);
        int tets[48][4], nt = cv_cap_cells(t, quad, tets, 48);
        cut_t c = { out, m->n, eps, m->disp != NULL, q };
        for (int k = 0; k < nt; k++) cut_tet(&c, tets[k]);
    }
}
