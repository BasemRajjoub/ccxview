/* shell.c -- section forces of shells from their expanded solids (shell.h). */
#include "shell.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const char* cv_shell_comp(int c) {
    static const char* const n[CV_SF_N] = { "Nxx", "Nyy", "Nxy", "Mxx", "Myy", "Mxy", "Qx", "Qy" };
    return c >= 0 && c < CV_SF_N ? n[c] : "?";
}

/* The lines of nodes through the thickness of an expanded shell, in the .frd order
   (mesh.c): one face of the solid, the other face, and the node between them when
   there is one (-1 none); one line per node of the shell, in its order. */
typedef struct { int8_t a, b, m; } line_t;
enum { LINES_MAX = 8 };

static int lines_of(int type, const line_t** out) {
    static const line_t he8[] = { {0,4,-1}, {1,5,-1}, {2,6,-1}, {3,7,-1} };
    static const line_t pe6[] = { {0,3,-1}, {1,4,-1}, {2,5,-1} };
    static const line_t he20[] = { {0,4,12}, {1,5,13}, {2,6,14}, {3,7,15}, {8,16,-1}, {9,17,-1}, {10,18,-1}, {11,19,-1} };
    static const line_t pe15[] = { {0,3,9}, {1,4,10}, {2,5,11}, {6,12,-1}, {7,13,-1}, {8,14,-1} };
    switch (type) {
        case 1: *out = he8;  return 4;
        case 2: *out = pe6;  return 3;
        case 4: *out = he20; return 8;
        case 5: *out = pe15; return 6;
    }
    return 0;
}

static double dot3(const double* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* the unit direction of a line (bottom to top: along +e3) and its nodes' heights on it */
static bool line_z(const cv_frd* f, const uint32_t* cn, const line_t* l, const float* q,
                   double d[3], double z[3]) {
    const float *xa = f->xyz + 3 * (size_t)cn[l->a], *xb = f->xyz + 3 * (size_t)cn[l->b];
    double len = 0;
    for (int k = 0; k < 3; k++) { d[k] = (double)xb[k] - xa[k]; len += d[k] * d[k]; }
    len = sqrt(len);
    if (!(len > 0)) return false;
    double up = d[0] * q[6] + d[1] * q[7] + d[2] * q[8] < 0 ? -1 : 1;
    for (int k = 0; k < 3; k++) d[k] *= up / len;
    z[0] = dot3(d, xa); z[2] = dot3(d, xb);
    z[1] = l->m >= 0 ? dot3(d, f->xyz + 3 * (size_t)cn[l->m]) : NAN;
    return true;
}

/* the stress at node n in the axes q, as the eight integrands: xx yy xy (N and M), xz yz */
static void local(const float* s, const float* q, double v[CV_SF_N]) {
    const double T[3][3] = { { s[0], s[3], s[5] }, { s[3], s[1], s[4] }, { s[5], s[4], s[2] } };
    double L[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = i; j < 3; j++) {
            double t = 0;
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++) t += q[3 * i + a] * T[a][b] * q[3 * j + b];
            L[i][j] = t;
        }
    v[0] = v[3] = L[0][0]; v[1] = v[4] = L[1][1]; v[2] = v[5] = L[0][1];
    v[6] = L[0][2]; v[7] = L[1][2];
}

bool cv_shell_forces(const cv_frd* f, const uint32_t* shell, const float* q, uint32_t nshell,
                     const float* s, int nc, float* out) {
    return cv_shell_forces_ref(f, shell, q, nshell, NULL, s, nc, out, NULL);
}

bool cv_shell_forces_ref(const cv_frd* f, const uint32_t* shell, const float* q, uint32_t nshell,
                         const float* off, const float* s, int nc, float* out, float* thick) {
    size_t N = f->n_nodes, NL = (size_t)nshell * LINES_MAX;
    for (size_t i = 0; i < N * CV_SF_N; i++) out[i] = NAN;
    if (thick) for (size_t i = 0; i < N; i++) thick[i] = NAN;
    if (!nshell || nc < 6) return true;
    double* zlo = malloc(NL * sizeof(double));
    double* zhi = malloc(NL * sizeof(double));
    double* acc = calloc(NL * CV_SF_N, sizeof(double));
    double* sum = calloc(N * (CV_SF_N + 1) + 1, sizeof(double));   /* the forces, then the thickness */
    uint32_t* cnt = calloc(N + 1, sizeof(uint32_t));
    if (!zlo || !zhi || !acc || !sum || !cnt) { free(zlo); free(zhi); free(acc); free(sum); free(cnt); return false; }
    for (size_t i = 0; i < NL; i++) { zlo[i] = INFINITY; zhi[i] = -INFINITY; }

    /* the extent of each line of each shell over all its layers: z from its middle */
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (shell[e] >= nshell) continue;
        const line_t* ln;
        int nl = lines_of(f->etype[e], &ln);
        const uint32_t* cn = f->conn + f->eoff[e];
        if ((uint32_t)cv_frd_type_nodes(f->etype[e]) != f->eoff[e + 1] - f->eoff[e]) continue;
        for (int j = 0; j < nl; j++) {
            double d[3], z[3];
            if (!line_z(f, cn, &ln[j], q + 9 * (size_t)e, d, z)) continue;
            size_t k = (size_t)shell[e] * LINES_MAX + j;
            zlo[k] = fmin(zlo[k], fmin(z[0], z[2]));
            zhi[k] = fmax(zhi[k], fmax(z[0], z[2]));
        }
    }
    /* each layer's line integrated and added to its shell's */
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (shell[e] >= nshell) continue;
        const line_t* ln;
        int nl = lines_of(f->etype[e], &ln);
        const uint32_t* cn = f->conn + f->eoff[e];
        const float* qe = q + 9 * (size_t)e;
        if ((uint32_t)cv_frd_type_nodes(f->etype[e]) != f->eoff[e + 1] - f->eoff[e]) continue;
        for (int j = 0; j < nl; j++) {
            double d[3], z[3];
            if (!line_z(f, cn, &ln[j], qe, d, z)) continue;
            size_t k = (size_t)shell[e] * LINES_MAX + j;
            int lo = z[0] <= z[2] ? ln[j].a : ln[j].b, hi = lo == ln[j].a ? ln[j].b : ln[j].a;
            double va[CV_SF_N], vb[CV_SF_N], vm[CV_SF_N];
            local(s + (size_t)cn[lo] * nc, qe, va);
            local(s + (size_t)cn[hi] * nc, qe, vb);
            if (ln[j].m >= 0) local(s + (size_t)cn[ln[j].m] * nc, qe, vm);
            double h = fabs(z[2] - z[0]), zc = (z[0] + z[2]) / 2 - (zlo[k] + zhi[k]) / 2;
            double* a = acc + k * CV_SF_N;
            for (int c = 0; c < CV_SF_N; c++) {
                double n = ln[j].m >= 0 ? h * (va[c] + 4 * vm[c] + vb[c]) / 6 : h * (va[c] + vb[c]) / 2;
                bool mom = c >= CV_SF_MXX && c <= CV_SF_MXY;
                a[c] += mom ? zc * n + (vb[c] - va[c]) * h * h / 12 : n;   /* NaN stays NaN */
            }
        }
    }
    /* about the reference surface: off t above the middle */
    if (off)
        for (size_t k = 0; k < NL; k++) {
            double r = off[k / LINES_MAX] * (zhi[k] - zlo[k]);
            if (!(zhi[k] >= zlo[k]) || r == 0) continue;
            double* a = acc + k * CV_SF_N;
            for (int c = 0; c < 3; c++) a[CV_SF_MXX + c] -= r * a[CV_SF_NXX + c];
        }
    /* every node of a line takes its shell's value; a node the mean over its shells */
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (shell[e] >= nshell) continue;
        const line_t* ln;
        int nl = lines_of(f->etype[e], &ln);
        const uint32_t* cn = f->conn + f->eoff[e];
        if ((uint32_t)cv_frd_type_nodes(f->etype[e]) != f->eoff[e + 1] - f->eoff[e]) continue;
        for (int j = 0; j < nl; j++) {
            const double* a = acc + ((size_t)shell[e] * LINES_MAX + j) * CV_SF_N;
            bool ok = true;
            for (int c = 0; c < CV_SF_N; c++) ok = ok && a[c] == a[c];
            if (!ok || !(zhi[(size_t)shell[e] * LINES_MAX + j] >= zlo[(size_t)shell[e] * LINES_MAX + j])) continue;
            const int8_t at[3] = { ln[j].a, ln[j].b, ln[j].m };
            for (int p = 0; p < 3; p++) {
                if (at[p] < 0) continue;
                uint32_t n = cn[at[p]];
                for (int c = 0; c < CV_SF_N; c++) sum[(size_t)n * CV_SF_N + c] += a[c];
                sum[N * CV_SF_N + n] += zhi[(size_t)shell[e] * LINES_MAX + j] - zlo[(size_t)shell[e] * LINES_MAX + j];
                cnt[n]++;
            }
        }
    }
    for (size_t i = 0; i < N; i++)
        if (cnt[i]) {
            for (int c = 0; c < CV_SF_N; c++) out[i * CV_SF_N + c] = (float)(sum[i * CV_SF_N + c] / cnt[i]);
            if (thick) thick[i] = (float)(sum[N * CV_SF_N + i] / cnt[i]);
        }
    free(zlo); free(zhi); free(acc); free(sum); free(cnt);
    return true;
}
