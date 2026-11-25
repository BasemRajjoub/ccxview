/* nodalrec.c -- see nodalrec.h. */
#include "nodalrec.h"
#include "gauss.h"
#include <math.h>
#include <string.h>

enum { B_MAX = 8 };

/* the basis the fit uses at a point; its size, 0 = constant */
static int basis(int t, int nip, const double xi[3], double* f) {
    int nb = 0;
    f[0] = 1;
    switch (t) {
        case 1: case 4:
            if (nip >= 8) { f[1] = xi[0]; f[2] = xi[1]; f[3] = xi[2]; f[4] = xi[0] * xi[1];
                            f[5] = xi[1] * xi[2]; f[6] = xi[2] * xi[0]; f[7] = xi[0] * xi[1] * xi[2]; nb = 8; }
            break;
        case 3: case 6:
            if (nip >= 4) { f[1] = xi[0]; f[2] = xi[1]; f[3] = xi[2]; nb = 4; }
            break;
        case 2: case 5:
            if (nip == 2) { f[1] = xi[2]; nb = 2; }
            else if (nip >= 6) { f[1] = xi[0]; f[2] = xi[1]; f[3] = xi[2]; f[4] = xi[0] * xi[2]; f[5] = xi[1] * xi[2]; nb = 6; }
            break;
        case 7: case 8:
            if (nip >= 3) { f[1] = xi[0]; f[2] = xi[1]; nb = 3; }
            break;
        case 9: case 10:
            if (nip >= 4) { f[1] = xi[0]; f[2] = xi[1]; f[3] = xi[0] * xi[1]; nb = 4; }
            break;
    }
    return nb;
}

/* the corners of an element with nn nodes, and the two corners each mid-side node sits between (CalculiX numbering) */
static int corners_of(int t, int nn, const signed char (**edge)[2]) {
    static const signed char hex20[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
    static const signed char tet10[6][2] = { {0,1},{1,2},{2,0},{0,3},{1,3},{2,3} };
    static const signed char wed15[9][2] = { {0,1},{1,2},{2,0},{3,4},{4,5},{5,3},{0,3},{1,4},{2,5} };
    static const signed char tri6[3][2] = { {0,1},{1,2},{2,0} };
    static const signed char quad8[4][2] = { {0,1},{1,2},{2,3},{3,0} };
    *edge = NULL;
    switch (t) {
        case 1: case 4: if (nn == 8) return 8; if (nn == 20) { *edge = hex20; return 8; } break;
        case 3: case 6: if (nn == 4) return 4; if (nn == 10) { *edge = tet10; return 4; } break;
        case 2: case 5: if (nn == 6) return 6; if (nn == 15) { *edge = wed15; return 6; } break;
        case 7: case 8: if (nn == 3) return 3; if (nn == 6) { *edge = tri6; return 3; } break;
        case 9: case 10: if (nn == 4) return 4; if (nn == 8) { *edge = quad8; return 4; } break;
    }
    return 0;
}

bool cv_nr_matrix(int t, int nn, int nip, float* M) {
    for (int i = 0; i < nn; i++) for (int k = 0; k < nip; k++) M[i * nip + k] = nip ? 1.f / nip : 0.f;   /* the constant */
    const signed char (*edge)[2];
    int nc = corners_of(t, nn, &edge);
    if (!nc || nn > CV_NR_NN || nip < 1 || nip > CV_NR_NIP) return false;
    double Phi[CV_NR_NIP][B_MAX], f[B_MAX];
    int nb = 0;
    for (int k = 0; k < nip; k++) {
        double xi[3];
        if (!cv_ip_param(t, nip, k, xi)) return false;
        int b = basis(t, nip, xi, f);
        if (k && b != nb) return false;
        nb = b;
        for (int j = 0; j < nb; j++) Phi[k][j] = f[j];
    }
    if (nb < 2) return false;
    /* P = (Phi^T Phi)^-1 Phi^T by Gauss-Jordan on [A | Phi^T] */
    double A[B_MAX][B_MAX + CV_NR_NIP];
    for (int a = 0; a < nb; a++) {
        for (int b = 0; b < nb; b++) { double s = 0; for (int k = 0; k < nip; k++) s += Phi[k][a] * Phi[k][b]; A[a][b] = s; }
        for (int k = 0; k < nip; k++) A[a][nb + k] = Phi[k][a];
    }
    for (int c = 0; c < nb; c++) {
        int p = c;
        for (int r = c + 1; r < nb; r++) if (fabs(A[r][c]) > fabs(A[p][c])) p = r;
        if (fabs(A[p][c]) < 1e-12) return false;                 /* the points do not determine the fit */
        if (p != c) for (int j = 0; j < nb + nip; j++) { double x = A[p][j]; A[p][j] = A[c][j]; A[c][j] = x; }
        double d = A[c][c];
        for (int j = 0; j < nb + nip; j++) A[c][j] /= d;
        for (int r = 0; r < nb; r++) {
            if (r == c) continue;
            double m = A[r][c];
            for (int j = 0; j < nb + nip; j++) A[r][j] -= m * A[c][j];
        }
    }
    float row[CV_NR_NN][CV_NR_NIP];                              /* per CalculiX node */
    for (int i = 0; i < nc; i++) {
        double xi[3];
        if (!cv_node_param(t, nn, i, xi)) return false;
        basis(t, nip, xi, f);
        for (int k = 0; k < nip; k++) { double s = 0; for (int j = 0; j < nb; j++) s += f[j] * A[j][nb + k]; row[i][k] = (float)s; }
    }
    for (int i = nc; i < nn; i++)
        for (int k = 0; k < nip; k++) row[i][k] = 0.5f * (row[edge[i - nc][0]][k] + row[edge[i - nc][1]][k]);
    for (int i = 0; i < nn; i++) {
        int p = cv_frd_node_pos(t, nn, i);
        if (p < 0 || p >= nn) return false;
        memcpy(M + p * nip, row[i], (size_t)nip * sizeof(float));
    }
    return true;
}

/* ---- averaging --------------------------------------------------------------------- */

bool cv_nr_topo_build(cv_nr_topo* t, uint32_t n_nodes, uint32_t n_elems, const uint32_t* eoff, const uint32_t* conn) {
    memset(t, 0, sizeof *t);
    uint32_t ns = n_elems ? eoff[n_elems] : 0;
    t->start = calloc((size_t)n_nodes + 2, sizeof(uint32_t));
    t->slot = malloc((size_t)CV_MAX(ns, 1) * sizeof(uint32_t));
    t->elem = malloc((size_t)CV_MAX(ns, 1) * sizeof(uint32_t));
    if (!t->start || !t->slot || !t->elem) { cv_nr_topo_free(t); return false; }
    t->n_nodes = n_nodes; t->n_slots = ns;
    for (uint32_t e = 0; e < n_elems; e++)
        for (uint32_t j = eoff[e]; j < eoff[e + 1]; j++) { t->elem[j] = e; t->start[conn[j] + 2]++; }
    for (uint32_t n = 0; n < n_nodes; n++) t->start[n + 2] += t->start[n + 1];
    for (uint32_t j = 0; j < ns; j++) t->slot[t->start[conn[j] + 1]++] = j;   /* start[n] ends as the first of node n */
    return true;
}

void cv_nr_topo_free(cv_nr_topo* t) {
    free(t->start); free(t->slot); free(t->elem);
    memset(t, 0, sizeof *t);
}

double cv_nr_average(const cv_nr_topo* t, const float* v, const uint8_t* vis, const uint32_t* mat, float pct, float* out) {
    double lo = INFINITY, hi = -INFINITY;
    for (uint32_t j = 0; j < t->n_slots; j++) {
        bool on = (!vis || vis[t->elem[j]]) && v[j] == v[j] && !isinf(v[j]);
        out[j] = on ? v[j] : NAN;
        if (!on) continue;
        if (v[j] < lo) lo = v[j];
        if (v[j] > hi) hi = v[j];
    }
    double range = hi > lo ? hi - lo : 0;
    if (pct <= 0 || range <= 0) return range;
    enum { CAP = 512 };
    for (uint32_t n = 0; n < t->n_nodes; n++) {
        uint32_t a = t->start[n], b = t->start[n + 1];
        if (b - a < 2) continue;
        uint32_t m = b - a > CAP ? CAP : b - a;                  /* more elements than that round one node: the rest keep theirs */
        uint8_t done[CAP] = {0};
        for (uint32_t i = 0; i < m; i++) {
            if (done[i]) continue;
            uint32_t si = t->slot[a + i];
            if (out[si] != out[si]) { done[i] = 1; continue; }
            double mn = out[si], mx = out[si], sum = 0;
            int cnt = 0;
            for (uint32_t k = i; k < m; k++) {                   /* the group: same material, or all */
                uint32_t sk = t->slot[a + k];
                if (done[k] || out[sk] != out[sk] || (mat && mat[t->elem[sk]] != mat[t->elem[si]])) continue;
                if (out[sk] < mn) mn = out[sk];
                if (out[sk] > mx) mx = out[sk];
                sum += out[sk]; cnt++;
            }
            bool avg = cnt > 1 && (pct >= 100 || (mx - mn) / range * 100.0 <= pct);
            float mean = (float)(sum / cnt);
            for (uint32_t k = i; k < m; k++) {
                uint32_t sk = t->slot[a + k];
                if (done[k] || out[sk] != out[sk] || (mat && mat[t->elem[sk]] != mat[t->elem[si]])) continue;
                done[k] = 1;
                if (avg) out[sk] = mean;
            }
        }
    }
    return range;
}
