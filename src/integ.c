/* integ.c -- integrals over solid elements and their faces, sums over nodes (integ.h). */
#include "integ.h"
#include "gauss.h"
#include <math.h>

static bool solid(int t) { return t >= 1 && t <= 6; }

/* the element's nodes in CalculiX order (what cv_shape numbers), their coordinates
   and values; false when it is not a solid or a value is missing (*nan set) */
typedef struct { int t, nn; uint32_t node[20]; double x[20][3]; } elem_t;

static bool elem_get(const cv_frd* f, uint32_t e, elem_t* E) {
    E->t = f->etype[e];
    uint32_t b = f->eoff[e];
    E->nn = (int)(f->eoff[e + 1] - b);
    if (!solid(E->t) || E->nn > 20) return false;
    double N[20], xi[3] = { 0.25, 0.25, 0.25 };
    if (!cv_shape(E->t, E->nn, xi, N)) return false;           /* an odd node count */
    for (int i = 0; i < E->nn; i++) {
        E->node[i] = f->conn[b + (uint32_t)cv_frd_node_pos(E->t, E->nn, i)];
        const float* p = f->xyz + 3 * (size_t)E->node[i];
        for (int k = 0; k < 3; k++) E->x[i][k] = p[k];
    }
    return true;
}

static bool has_values(const elem_t* E, const float* vals, int nc) {
    if (!vals) return true;
    for (int i = 0; i < E->nn; i++)
        for (int c = 0; c < nc; c++) { float v = vals[(size_t)E->node[i] * nc + c]; if (v != v) return false; }
    return true;
}

static double det3(const double J[3][3]) {
    return J[0][0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1]) - J[0][1] * (J[1][0] * J[2][2] - J[1][2] * J[2][0])
         + J[0][2] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]);
}

/* int_e dV and int_e v_c dV of one element into vol, acc[nc] (added) */
static void elem_integrate(const elem_t* E, const float* vals, int nc, double* vol, double* acc) {
    double xi[CV_RULE_MAX][3], w[CV_RULE_MAX], N[20], dN[20][3], V = 0, a[CV_INTEG_MAXC] = { 0 };
    int np = cv_solid_rule(E->t, xi, w);
    for (int p = 0; p < np; p++) {
        if (!cv_shape(E->t, E->nn, xi[p], N) || !cv_shape_d(E->t, E->nn, xi[p], dN)) continue;
        double J[3][3] = { { 0 } };
        for (int i = 0; i < E->nn; i++)
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) J[r][c] += dN[i][r] * E->x[i][c];
        double dv = w[p] * det3(J);
        V += dv;
        for (int c = 0; vals && c < nc; c++) {
            double v = 0;
            for (int i = 0; i < E->nn; i++) v += N[i] * vals[(size_t)E->node[i] * nc + c];
            a[c] += v * dv;
        }
    }
    double s = V < 0 ? -1 : 1;                  /* numbered the mirror way round: still a volume */
    *vol += s * V;
    for (int c = 0; vals && c < nc; c++) acc[c] += s * a[c];
}

double cv_elem_volume(int t, int nn, const double (*x)[3]) {
    if (!solid(t) || nn > 20) return NAN;
    elem_t E = { .t = t, .nn = nn };
    for (int i = 0; i < nn; i++) memcpy(E.x[i], x[cv_frd_node_pos(t, nn, i)], sizeof E.x[i]);
    double V = 0;
    elem_integrate(&E, NULL, 0, &V, NULL);
    return V;
}

void cv_integ_volume(const cv_frd* f, const uint32_t* elems, uint32_t n, const float* vals, int nc, cv_integ* out) {
    memset(out, 0, sizeof *out);
    nc = CV_MIN(CV_MAX(nc, 0), CV_INTEG_MAXC);
    if (!elems) n = f->n_elems;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t e = elems ? elems[k] : k;
        elem_t E;
        if (e >= f->n_elems || !elem_get(f, e, &E)) { out->skipped++; continue; }
        if (!has_values(&E, vals, nc)) { out->missing++; continue; }
        elem_integrate(&E, vals, nc, &out->size, out->integ);
        out->n++;
    }
}

/* ---- faces: each one a patch of the natural domain, xi = o + a u + b v over the
   square [-1,1]^2 or the triangle a, b >= 0, a + b <= 1 (CalculiX face numbering) */

typedef struct { bool tri; double o[3], u[3], v[3]; } face_param;

static const face_param kHex[6] = {
    { false, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } }, { false, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
    { false, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } }, { false, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
    { false, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },  { false, { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
};
static const face_param kTet[4] = {
    { true, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } }, { true, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
    { true, { 0, 0, 1 }, { 1, 0, -1 }, { 0, 1, -1 } }, { true, { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
};
static const face_param kWedge[5] = {
    { true, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } }, { true, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
    { false, { 0.5, 0, 0 }, { 0.5, 0, 0 }, { 0, 0, 1 } }, { false, { 0.5, 0.5, 0 }, { -0.5, 0.5, 0 }, { 0, 0, 1 } },
    { false, { 0, 0.5, 0 }, { 0, 0.5, 0 }, { 0, 0, 1 } },
};

static const face_param* face_of(int t, int face) {
    if (face < 0) return NULL;
    if ((t == 1 || t == 4) && face < 6) return &kHex[face];
    if ((t == 3 || t == 6) && face < 4) return &kTet[face];
    if ((t == 2 || t == 5) && face < 5) return &kWedge[face];
    return NULL;
}

static void cross(const double a[3], const double b[3], double o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}

/* one face: area, int v_c dA, -int v_0 n dA and (a tensor) int S n dA, added to out */
static void face_integrate(const elem_t* E, const face_param* F, const float* vals, int nc, cv_integ* out) {
    double ab[9][2], w[9], N[20], dN[20][3], ctr[3] = { 0, 0, 0 };
    int np = cv_face_rule(F->tri, ab, w);
    for (int i = 0; i < E->nn; i++) for (int k = 0; k < 3; k++) ctr[k] += E->x[i][k] / E->nn;
    for (int p = 0; p < np; p++) {
        double xi[3], x[3] = { 0, 0, 0 }, tu[3] = { 0, 0, 0 }, tv[3] = { 0, 0, 0 }, n[3];
        for (int k = 0; k < 3; k++) xi[k] = F->o[k] + ab[p][0] * F->u[k] + ab[p][1] * F->v[k];
        if (!cv_shape(E->t, E->nn, xi, N) || !cv_shape_d(E->t, E->nn, xi, dN)) continue;
        for (int i = 0; i < E->nn; i++) {
            double du = dN[i][0] * F->u[0] + dN[i][1] * F->u[1] + dN[i][2] * F->u[2];
            double dv = dN[i][0] * F->v[0] + dN[i][1] * F->v[1] + dN[i][2] * F->v[2];
            for (int k = 0; k < 3; k++) { x[k] += N[i] * E->x[i][k]; tu[k] += du * E->x[i][k]; tv[k] += dv * E->x[i][k]; }
        }
        cross(tu, tv, n);
        double len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (!(len > 0)) continue;
        double out_dir = (x[0] - ctr[0]) * n[0] + (x[1] - ctr[1]) * n[1] + (x[2] - ctr[2]) * n[2];
        for (int k = 0; k < 3; k++) n[k] /= out_dir < 0 ? -len : len;        /* unit, outward */
        double dA = w[p] * len, v[CV_INTEG_MAXC];
        out->size += dA;
        for (int c = 0; vals && c < nc; c++) {
            v[c] = 0;
            for (int i = 0; i < E->nn; i++) v[c] += N[i] * vals[(size_t)E->node[i] * nc + c];
            out->integ[c] += v[c] * dA;
        }
        if (!vals || nc < 1) continue;
        for (int k = 0; k < 3; k++) out->push[k] -= v[0] * n[k] * dA;
        if (nc >= 6) {                                  /* the first six: XX YY ZZ XY YZ ZX */
            out->traction[0] += (v[0] * n[0] + v[3] * n[1] + v[5] * n[2]) * dA;
            out->traction[1] += (v[3] * n[0] + v[1] * n[1] + v[4] * n[2]) * dA;
            out->traction[2] += (v[5] * n[0] + v[4] * n[1] + v[2] * n[2]) * dA;
        }
    }
}

int cv_face_nodes(const cv_frd* f, uint32_t e, int face, uint32_t out[8]) {
    elem_t E;
    const face_param* F = e < f->n_elems ? face_of(f->etype[e], face) : NULL;
    if (!F || !elem_get(f, e, &E)) return 0;
    double nrm[3];
    cross(F->u, F->v, nrm);
    int k = 0;
    for (int i = 0; i < E.nn && k < 8; i++) {           /* on the face's plane in natural coordinates */
        double xi[3];
        if (!cv_node_param(E.t, E.nn, i, xi)) continue;
        double d = (xi[0] - F->o[0]) * nrm[0] + (xi[1] - F->o[1]) * nrm[1] + (xi[2] - F->o[2]) * nrm[2];
        if (fabs(d) < 1e-9) out[k++] = E.node[i];
    }
    return k;
}

void cv_integ_faces(const cv_frd* f, const uint32_t* elems, const uint8_t* faces, uint32_t n,
                    const float* vals, int nc, cv_integ* out) {
    memset(out, 0, sizeof *out);
    nc = CV_MIN(CV_MAX(nc, 0), CV_INTEG_MAXC);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t e = elems[k];
        elem_t E;
        const face_param* F = e < f->n_elems ? face_of(f->etype[e], faces[k]) : NULL;
        if (!F || !elem_get(f, e, &E)) { out->skipped++; continue; }
        if (!has_values(&E, vals, nc)) { out->missing++; continue; }
        face_integrate(&E, F, vals, nc, out);
        out->n++;
    }
}

void cv_integ_nodes(const cv_frd* f, const uint32_t* nodes, uint32_t n, const float* vals, int nc,
                    const double about[3], cv_nsum* out) {
    memset(out, 0, sizeof *out);
    nc = CV_MIN(CV_MAX(nc, 0), CV_INTEG_MAXC);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = nodes[k];
        if (i >= f->n_nodes) continue;
        const float* v = vals + (size_t)i * nc;
        bool nan = false;
        for (int c = 0; c < nc; c++) nan |= v[c] != v[c];
        if (nan) { out->missing++; continue; }
        for (int c = 0; c < nc; c++) out->sum[c] += v[c];
        if (nc >= 3) {
            double r[3], F[3] = { v[0], v[1], v[2] }, m[3];
            for (int c = 0; c < 3; c++) r[c] = f->xyz[3 * (size_t)i + c] - (about ? about[c] : 0);
            cross(r, F, m);
            for (int c = 0; c < 3; c++) out->moment[c] += m[c];
        }
        out->n++;
    }
}
