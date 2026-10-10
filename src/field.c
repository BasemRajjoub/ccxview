/* field.c -- derived scalars and ranges. */
#include "field.h"
#include <math.h>
#include <ctype.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool is_stress(const char* name) {
    return strncmp(name, "STRESS", 6) == 0 || strncmp(name, "ZZS", 3) == 0 || strncmp(name, "stresses", 8) == 0 ||   /* .dat */
           !strcmp(name, "STRPOS") || !strcmp(name, "STRNEG") || !strcmp(name, "STRMID");   /* shell faces */
}

/* 0: not a tensor; 1: shears XY YZ ZX (.frd); 2: shears xy xz yz (.dat).
   Decided from the last two letters of the component names. */
static int tensor_order(const cv_field_desc* d) {
    if (d->ncomp != 6) return 0;
    char t[6][3];
    for (int c = 0; c < 6; c++) {
        size_t k = strlen(d->comp[c]);
        if (k < 2) return 0;
        t[c][0] = (char)tolower((unsigned char)d->comp[c][k - 2]);
        t[c][1] = (char)tolower((unsigned char)d->comp[c][k - 1]);
        t[c][2] = 0;
    }
    if (strcmp(t[0], "xx") || strcmp(t[1], "yy") || strcmp(t[2], "zz") || strcmp(t[3], "xy")) return 0;
    if (!strcmp(t[4], "yz") && !strcmp(t[5], "zx")) return 1;
    if (!strcmp(t[4], "xz") && !strcmp(t[5], "yz")) return 2;
    return 0;
}

int cv_tensor_order(const cv_field_desc* d) { return tensor_order(d); }

void cv_cyl_basis(const float p[3], const float o[3], int axis, float Q[3][3]) {
    double A[3] = { 0, 0, 0 }, d[3], r[3];
    A[axis] = 1;
    for (int k = 0; k < 3; k++) d[k] = (double)p[k] - o[k];
    double da = d[axis];
    for (int k = 0; k < 3; k++) r[k] = d[k] - da * A[k];
    double l = sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    if (!(l > 1e-12 * (fabs(da) + 1e-30))) { r[0] = r[1] = r[2] = 0; r[(axis + 1) % 3] = 1; l = 1; }
    for (int k = 0; k < 3; k++) r[k] /= l;
    double t[3] = { A[1] * r[2] - A[2] * r[1], A[2] * r[0] - A[0] * r[2], A[0] * r[1] - A[1] * r[0] };
    for (int k = 0; k < 3; k++) { Q[0][k] = (float)r[k]; Q[1][k] = (float)t[k]; Q[2][k] = (float)A[k]; }
}

void cv_csys_axes(const cv_csys* c, const float p[3], double Q[3][3]) {
    const double* a = c->a;
    double e1[3], e2[3], e3[3], d;
    if (!c->cyl) {
        d = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        for (int k = 0; k < 3; k++) e1[k] = a[k] / d;
        d = e1[0] * a[3] + e1[1] * a[4] + e1[2] * a[5];
        for (int k = 0; k < 3; k++) e2[k] = a[3 + k] - d * e1[k];
        d = sqrt(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
        for (int k = 0; k < 3; k++) e2[k] /= d;
        e3[0] = e1[1] * e2[2] - e2[1] * e1[2];
        e3[1] = e1[2] * e2[0] - e1[0] * e2[2];
        e3[2] = e1[0] * e2[1] - e2[0] * e1[1];
    } else {
        for (int k = 0; k < 3; k++) { e1[k] = (double)p[k] - a[k]; e3[k] = a[3 + k] - a[k]; }
        d = sqrt(e3[0] * e3[0] + e3[1] * e3[1] + e3[2] * e3[2]);
        for (int k = 0; k < 3; k++) e3[k] /= d;
        d = e1[0] * e3[0] + e1[1] * e3[1] + e1[2] * e3[2];
        for (int k = 0; k < 3; k++) e1[k] -= d * e3[k];
        d = sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
        if (d < 1e-10) {                          /* on the axis: transformatrix's pick */
            if (fabs(e3[0]) > 1e-10)      { e1[0] = -e3[1] / e3[0]; e1[1] = 1; e1[2] = 0; }
            else if (fabs(e3[1]) > 1e-10) { e1[0] = 0; e1[1] = -e3[2] / e3[1]; e1[2] = 1; }
            else                          { e1[0] = 1; e1[1] = 0; e1[2] = -e3[0] / e3[2]; }
            d = sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
        }
        for (int k = 0; k < 3; k++) e1[k] /= d;
        e2[0] = e3[1] * e1[2] - e1[1] * e3[2];
        e2[1] = e3[2] * e1[0] - e1[2] * e3[0];
        e2[2] = e3[0] * e1[1] - e1[0] * e3[1];
    }
    for (int k = 0; k < 3; k++) { Q[0][k] = e1[k]; Q[1][k] = e2[k]; Q[2][k] = e3[k]; }
}

void cv_vec_to_global(const double Q[3][3], float v[3]) {
    double g[3];
    for (int k = 0; k < 3; k++) g[k] = Q[0][k] * v[0] + Q[1][k] * v[1] + Q[2][k] * v[2];
    for (int k = 0; k < 3; k++) v[k] = (float)g[k];
}

void cv_ten_to_global(const double Q[3][3], float s[6]) {
    static const int si[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };
    double S[3][3], T[3][3], G[3][3];
    for (int a = 0; a < 3; a++) S[a][a] = s[a];
    for (int k = 0; k < 3; k++) S[si[k][0]][si[k][1]] = S[si[k][1]][si[k][0]] = s[3 + k];
    for (int a = 0; a < 3; a++)                   /* G = Q^T S Q */
        for (int b = 0; b < 3; b++) T[a][b] = S[a][0] * Q[0][b] + S[a][1] * Q[1][b] + S[a][2] * Q[2][b];
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) G[a][b] = Q[0][a] * T[0][b] + Q[1][a] * T[1][b] + Q[2][a] * T[2][b];
    for (int a = 0; a < 3; a++) s[a] = (float)G[a][a];
    for (int k = 0; k < 3; k++) s[3 + k] = (float)G[si[k][0]][si[k][1]];
}

bool cv_cyl_applies(const cv_field_desc* d) { return d->ncomp == 3 || tensor_order(d) != 0; }

void cv_cyl_values(const cv_field_desc* d, const float* xyz, uint32_t n, int axis, const float o[3], float* v) {
    int ord = tensor_order(d);
    if (d->ncomp != 3 && !ord) return;
    /* where the shears sit: (i,j) of components 3, 4, 5 */
    int si[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };
    if (ord == 2) { si[1][0] = 0; si[1][1] = 2; si[2][0] = 1; si[2][1] = 2; }
    for (uint32_t i = 0; i < n; i++) {
        float Q[3][3];
        cv_cyl_basis(xyz + 3 * (size_t)i, o, axis, Q);
        if (d->ncomp == 3) {
            float* r = v + 3 * (size_t)i;
            double w[3];
            for (int a = 0; a < 3; a++) w[a] = (double)Q[a][0] * r[0] + (double)Q[a][1] * r[1] + (double)Q[a][2] * r[2];
            for (int a = 0; a < 3; a++) r[a] = (float)w[a];
        } else {
            float* r = v + 6 * (size_t)i;
            double S[3][3], QS[3][3], T[3][3];
            for (int a = 0; a < 3; a++) S[a][a] = r[a];
            for (int k = 0; k < 3; k++) S[si[k][0]][si[k][1]] = S[si[k][1]][si[k][0]] = r[3 + k];
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++) QS[a][b] = Q[a][0] * S[0][b] + Q[a][1] * S[1][b] + Q[a][2] * S[2][b];
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++) T[a][b] = QS[a][0] * Q[b][0] + QS[a][1] * Q[b][1] + QS[a][2] * Q[b][2];
            for (int a = 0; a < 3; a++) r[a] = (float)T[a][a];
            for (int k = 0; k < 3; k++) r[3 + k] = (float)T[si[k][0]][si[k][1]];
        }
    }
}

void cv_cyl_comp_name(const cv_field_desc* d, int c, char out[12]) {
    const char* s = d->comp[c];
    int k = (int)strlen(s);
    if (d->ncomp == 3 && k >= 1) {
        snprintf(out, 12, "%.*s%c", k - 1, s, "rta"[c]);
    } else if (tensor_order(d) && k >= 2) {
        char a = (char)tolower((unsigned char)s[k - 2]), b = (char)tolower((unsigned char)s[k - 1]);
        a = a == 'x' ? 'r' : a == 'y' ? 't' : 'a';
        b = b == 'x' ? 'r' : b == 'y' ? 't' : 'a';
        snprintf(out, 12, "%.*s%c%c", k - 2, s, a, b);
    } else {
        snprintf(out, 12, "%s", s);
    }
}

int cv_field_options(const cv_field_desc* d, cv_scalar_opt* out, int max) {
    int n = 0;
    if (d->ncomp == 3 && n < max) {
        snprintf(out[n].label, sizeof out[n].label, "|%s|", d->name);
        out[n++].comp = CV_COMP_MAG;
    }
    if (d->ncomp == 6 && is_stress(d->name) && n < max) {
        snprintf(out[n].label, sizeof out[n].label, "von Mises");
        out[n++].comp = CV_COMP_MISES;
    }
    int order = tensor_order(d);
    if (order) {
        /* S1..S3 for stresses, E1..E3 for strains, from the component names */
        char p = (char)toupper((unsigned char)d->comp[0][0]);
        if (p != 'S' && p != 'E') p = 'P';
        static const char* what[3] = { "max", "mid", "min" };
        for (int k = 0; k < 3 && n < max; k++) {
            snprintf(out[n].label, sizeof out[n].label, "%c%d %s", p, k + 1, what[k]);
            out[n++].comp = (order == 1 ? CV_COMP_P1 : CV_COMP_P1_XZ) - k;
        }
    }
    for (int c = 0; c < d->ncomp && n < max; c++) {
        snprintf(out[n].label, sizeof out[n].label, "%s", d->comp[c][0] ? d->comp[c] : "C?");
        out[n++].comp = c;
    }
    if (!strcmp(d->name, "CONTACT") && n < max) {     /* far / near open, sliding, sticking ... per slave node */
        snprintf(out[n].label, sizeof out[n].label, "STATUS");
        out[n++].comp = CV_COMP_STATUS;
    }
    return n;
}

float cv_von_mises(const float s[6]) {
    double a = s[0] - s[1], b = s[1] - s[2], c = s[2] - s[0];
    double sh = (double)s[3] * s[3] + (double)s[4] * s[4] + (double)s[5] * s[5];
    return (float)sqrt(0.5 * (a * a + b * b + c * c) + 3.0 * sh);
}

void cv_principal(const float s[6], bool xz_order, float out[3]) {
    double xx = s[0], yy = s[1], zz = s[2], xy = s[3];
    double yz = xz_order ? s[5] : s[4], zx = xz_order ? s[4] : s[5];
    double p1 = xy * xy + yz * yz + zx * zx;
    double e[3];
    double q = (xx + yy + zz) / 3.0;
    double p2 = (xx - q) * (xx - q) + (yy - q) * (yy - q) + (zz - q) * (zz - q) + 2.0 * p1;
    if (p2 <= 1e-30 * (q * q + 1e-300)) {         /* hydrostatic (or zero): all equal */
        e[0] = e[1] = e[2] = q;
    } else {
        /* closed form for symmetric 3x3 (Smith 1961): B = (A - qI) / p, r = det(B) / 2 */
        double p = sqrt(p2 / 6.0);
        double a = (xx - q) / p, b = (yy - q) / p, c = (zz - q) / p, u = xy / p, v = yz / p, w = zx / p;
        double r = 0.5 * (a * (b * c - v * v) - u * (u * c - v * w) + w * (u * v - b * w));
        double phi = r <= -1 ? M_PI / 3.0 : r >= 1 ? 0.0 : acos(r) / 3.0;
        e[0] = q + 2.0 * p * cos(phi);
        e[2] = q + 2.0 * p * cos(phi + 2.0 * M_PI / 3.0);
        e[1] = 3.0 * q - e[0] - e[2];
    }
    for (int k = 0; k < 3; k++) out[k] = (float)e[k];
}

bool cv_principal_dirs(const float s[6], bool xz_order, float val[3], float vec[3][3]) {
    double a[3][3], v[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    double yz = xz_order ? s[5] : s[4], zx = xz_order ? s[4] : s[5];
    a[0][0] = s[0]; a[1][1] = s[1]; a[2][2] = s[2];
    a[0][1] = a[1][0] = s[3]; a[1][2] = a[2][1] = yz; a[0][2] = a[2][0] = zx;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) if (a[i][j] != a[i][j]) return false;
    for (int sweep = 0; sweep < 30; sweep++) {
        double off = fabs(a[0][1]) + fabs(a[0][2]) + fabs(a[1][2]);
        double diag = fabs(a[0][0]) + fabs(a[1][1]) + fabs(a[2][2]);
        if (off <= 1e-15 * diag || off == 0) break;
        for (int p = 0; p < 2; p++)
            for (int q = p + 1; q < 3; q++) {
                if (a[p][q] == 0) continue;
                double th = (a[q][q] - a[p][p]) / (2 * a[p][q]);
                double t = (th >= 0 ? 1 : -1) / (fabs(th) + sqrt(th * th + 1));
                double c = 1 / sqrt(t * t + 1), sn = t * c;
                for (int k = 0; k < 3; k++) {           /* A <- A J */
                    double kp = a[k][p], kq = a[k][q];
                    a[k][p] = c * kp - sn * kq; a[k][q] = sn * kp + c * kq;
                }
                for (int k = 0; k < 3; k++) {           /* A <- J^T A */
                    double pk = a[p][k], qk = a[q][k];
                    a[p][k] = c * pk - sn * qk; a[q][k] = sn * pk + c * qk;
                }
                for (int k = 0; k < 3; k++) {           /* V <- V J: columns are the directions */
                    double kp = v[k][p], kq = v[k][q];
                    v[k][p] = c * kp - sn * kq; v[k][q] = sn * kp + c * kq;
                }
            }
    }
    int o[3] = { 0, 1, 2 };
    for (int i = 0; i < 2; i++)
        for (int j = i + 1; j < 3; j++) if (a[o[j]][o[j]] > a[o[i]][o[i]]) { int t = o[i]; o[i] = o[j]; o[j] = t; }
    for (int k = 0; k < 3; k++) {
        val[k] = (float)a[o[k]][o[k]];
        for (int c = 0; c < 3; c++) vec[k][c] = (float)v[c][o[k]];
    }
    return true;
}

bool cv_linearize(const float* s, int n, double t, double m[6], double b[6]) {
    if (n < 2 || !(t > 0)) return false;
    for (int c = 0; c < 6; c++) m[c] = b[c] = 0;
    double h = t / (n - 1);
    bool simpson = n % 2 == 1;
    for (int i = 0; i < n; i++) {
        double w = simpson ? (i == 0 || i == n - 1 ? 1 : i % 2 ? 4 : 2) * h / 3 : (i == 0 || i == n - 1 ? 0.5 : 1) * h;
        double x = i * h;
        for (int c = 0; c < 6; c++) {
            double v = s[6 * (size_t)i + c];
            if (v != v) return false;
            m[c] += w * v;
            b[c] += w * v * (t / 2 - x);
        }
    }
    for (int c = 0; c < 6; c++) { m[c] /= t; b[c] *= 6 / (t * t); }
    return true;
}

void cv_lin_at(const double m[6], const double b[6], double t, double x, double out[6]) {
    double f = t > 0 ? 1 - 2 * x / t : 0;
    for (int c = 0; c < 6; c++) out[c] = m[c] + b[c] * f;
}

void cv_bend_mask(double b[6], const float dir[3]) {
    double d[3] = { dir[0], dir[1], dir[2] }, n = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(n > 0)) return;
    for (int k = 0; k < 3; k++) d[k] /= n;
    /* keep P S P with P = I - d d^T: the part of the tensor acting in the plane normal to d */
    double S[3][3] = { { b[0], b[3], b[5] }, { b[3], b[1], b[4] }, { b[5], b[4], b[2] } }, P[3][3], PS[3][3], T[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) P[i][j] = (i == j) - d[i] * d[j];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) PS[i][j] = P[i][0] * S[0][j] + P[i][1] * S[1][j] + P[i][2] * S[2][j];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) T[i][j] = PS[i][0] * P[0][j] + PS[i][1] * P[1][j] + PS[i][2] * P[2][j];
    b[0] = T[0][0]; b[1] = T[1][1]; b[2] = T[2][2]; b[3] = T[0][1]; b[4] = T[1][2]; b[5] = T[2][0];
}

double cv_mises6(const double s[6]) {
    double a = s[0] - s[1], b = s[1] - s[2], c = s[2] - s[0];
    return sqrt(0.5 * (a * a + b * b + c * c) + 3 * (s[3] * s[3] + s[4] * s[4] + s[5] * s[5]));
}

double cv_tresca6(const double s[6], bool xz_order) {
    float f[6], val[3], vec[3][3];
    for (int c = 0; c < 6; c++) f[c] = (float)s[c];
    if (!cv_principal_dirs(f, xz_order, val, vec)) return NAN;
    return (double)val[0] - val[2];
}

void cv_field_scalar(const float* v, int nc, uint32_t n, int comp, float* out) {
    for (uint32_t i = 0; i < n; i++) {
        const float* r = v + (size_t)i * nc;
        if (comp >= 0) out[i] = comp < nc ? r[comp] : NAN;
        else if (comp == CV_COMP_MAG) {
            double s = 0;
            for (int c = 0; c < nc; c++) s += (double)r[c] * r[c];
            out[i] = (float)sqrt(s);                 /* NaN propagates */
        } else if (comp == CV_COMP_MISES && nc >= 6) {
            out[i] = cv_von_mises(r);
        } else if (comp <= CV_COMP_P1 && comp >= CV_COMP_P3_XZ && nc >= 6) {
            float e[3];
            bool xz = comp <= CV_COMP_P1_XZ;
            cv_principal(r, xz, e);                      /* NaN in, NaN out */
            out[i] = e[(xz ? CV_COMP_P1_XZ : CV_COMP_P1) - comp];
        } else {
            out[i] = NAN;
        }
    }
}

bool cv_range(const float* v, size_t n, float* mn, float* mx) {
    float lo = INFINITY, hi = -INFINITY;
    for (size_t i = 0; i < n; i++) {
        float x = v[i];
        if (x != x || isinf(x)) continue;
        if (x < lo) lo = x;
        if (x > hi) hi = x;
    }
    if (lo > hi) { *mn = 0; *mx = 1; return false; }
    *mn = lo; *mx = hi;
    return true;
}

void cv_center_zero(float* mn, float* mx) {
    if (*mn < 0 && *mx > 0) {
        float m = CV_MAX(-*mn, *mx);
        *mn = -m; *mx = m;
    }
}

void cv_elem_mean(const cv_frd* f, const float* nodal, float* out) {
    for (uint32_t e = 0; e < f->n_elems; e++) {
        uint32_t b = f->eoff[e], n = f->eoff[e + 1] - b;
        double s = 0;
        for (uint32_t j = 0; j < n; j++) { CV_ASSERT(f->conn[b + j] < f->n_nodes); s += nodal[f->conn[b + j]]; }
        out[e] = n ? (float)(s / n) : NAN;
    }
}

float cv_auto_deform(float peak, float diag) {
    if (!(peak > 0) || !(diag > 0)) return 1.f;
    float s = 0.1f * diag / peak;
    return s > 1.f ? s : 1.f;
}
