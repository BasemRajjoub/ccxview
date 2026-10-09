/* app_linearize.c -- stress linearization along a straight line through the
   solid, and the point location in the solid elements (line_locate,
   line_interp) that the straight path plot samples with too. */
#include "app_int.h"
#include "gauss.h"
#include "path.h"
#include "export.h"     /* cv_fprintf */
#include <math.h>

/* ---- stress linearization: a tensor along a straight line through the solid ------
   Sampled at LIN_N points by locating each in a solid element (Newton on the shape
   functions) and interpolating the nodal values there. */

#define LIN_N 41

/* the tensor linearized: the field shown when it is an .frd tensor, else STRESS */
static int lin_field(void) {
    int fi = G.field_src == 0 ? find_field(G.step, G.field_name) : -1;
    if (fi >= 0 && cv_tensor_order(&G.frd.steps[G.step].fields[fi]) == 1) return fi;
    fi = find_field(G.step, "STRESS");
    return fi >= 0 && cv_tensor_order(&G.frd.steps[G.step].fields[fi]) == 1 ? fi : -1;
}

/* natural coordinates of p in solid element e; N: its shape functions there */
bool elem_locate(uint32_t e, const double p[3], double N[20]) {
    int t = G.frd.etype[e];
    uint32_t b = G.frd.eoff[e], nn = G.frd.eoff[e + 1] - b;
    if (t < 1 || t > 6 || nn > 20) return false;
    double X[20][3], xi[3];
    for (uint32_t i = 0; i < nn; i++) {
        const float* q = G.frd.xyz + 3 * (size_t)G.frd.conn[b + (uint32_t)cv_frd_node_pos(t, (int)nn, (int)i)];
        for (int k = 0; k < 3; k++) X[i][k] = q[k];
    }
    bool tet = t == 3 || t == 6, wedge = t == 2 || t == 5;
    xi[0] = xi[1] = tet ? 0.25 : wedge ? 1.0 / 3 : 0; xi[2] = tet ? 0.25 : 0;
    for (int it = 0; it < 30; it++) {
        double f[3] = { -p[0], -p[1], -p[2] }, J[3][3] = { { 0 } }, dN[20][3];
        if (!cv_shape(t, (int)nn, xi, N) || !cv_shape_d(t, (int)nn, xi, dN)) return false;
        for (uint32_t i = 0; i < nn; i++)                /* x(xi) - p and its derivatives */
            for (int k = 0; k < 3; k++) {
                f[k] += N[i] * X[i][k];
                for (int c = 0; c < 3; c++) J[k][c] += dN[i][c] * X[i][k];
            }
        double det = J[0][0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1]) - J[0][1] * (J[1][0] * J[2][2] - J[1][2] * J[2][0])
                   + J[0][2] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]);
        if (!(fabs(det) > 1e-300)) return false;
        double d[3];
        for (int c = 0; c < 3; c++) {                   /* Cramer: J d = -f */
            double A[3][3];
            memcpy(A, J, sizeof A);
            for (int k = 0; k < 3; k++) A[k][c] = -f[k];
            d[c] = (A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
                  + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0])) / det;
        }
        for (int k = 0; k < 3; k++) xi[k] = CV_MAX(-3.0, CV_MIN(3.0, xi[k] + d[k]));
        if (fabs(d[0]) + fabs(d[1]) + fabs(d[2]) < 1e-10) break;
    }
    const double e1 = 1e-3;
    bool in = tet   ? xi[0] >= -e1 && xi[1] >= -e1 && xi[2] >= -e1 && xi[0] + xi[1] + xi[2] <= 1 + e1
            : wedge ? xi[0] >= -e1 && xi[1] >= -e1 && xi[0] + xi[1] <= 1 + e1 && fabs(xi[2]) <= 1 + e1
                    : fabs(xi[0]) <= 1 + e1 && fabs(xi[1]) <= 1 + e1 && fabs(xi[2]) <= 1 + e1;
    return in && cv_shape(t, (int)nn, xi, N);
}

/* Where n points evenly on the straight line A..B sit in the solid: the element
   (UINT32_MAX outside) and its 20 shape-function weights. One pass keeps the
   elements whose box the segment actually crosses (slab test), with their boxes;
   each point then tries the last hit, and the Newton inversion only in the
   candidates whose box holds it. */
typedef struct { uint32_t e; float lo[3], hi[3]; } line_cand;

void line_locate(const float A[3], const float B[3], int n, uint32_t* el, float* w) {
    float tol = 1e-4f * G.diag, D[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
    CV_VEC(line_cand) cand = {0};
    for (uint32_t e = 0; e < G.frd.n_elems; e++) {
        int t = G.frd.etype[e];
        if (t < 1 || t > 6) continue;
        line_cand c = { e, { INFINITY, INFINITY, INFINITY }, { -INFINITY, -INFINITY, -INFINITY } };
        for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) {
            const float* q = G.frd.xyz + 3 * (size_t)G.frd.conn[j];
            for (int k = 0; k < 3; k++) { c.lo[k] = fminf(c.lo[k], q[k]); c.hi[k] = fmaxf(c.hi[k], q[k]); }
        }
        float t0 = 0.f, t1 = 1.f;
        for (int k = 0; k < 3 && t0 <= t1; k++) {
            c.lo[k] -= tol; c.hi[k] += tol;
            if (fabsf(D[k]) < 1e-30f) { if (A[k] < c.lo[k] || A[k] > c.hi[k]) t0 = 2.f; continue; }
            float a = (c.lo[k] - A[k]) / D[k], b = (c.hi[k] - A[k]) / D[k];
            if (a > b) { float s = a; a = b; b = s; }
            t0 = fmaxf(t0, a); t1 = fminf(t1, b);
        }
        if (t0 <= t1) cv_push(cand, c);
    }
    uint32_t hint = UINT32_MAX;
    for (int i = 0; i < n; i++) {
        double f = n > 1 ? (double)i / (n - 1) : 0, p[3], N[20];
        for (int k = 0; k < 3; k++) p[k] = A[k] + f * D[k];
        el[i] = UINT32_MAX;
        bool ok = hint != UINT32_MAX && elem_locate(hint, p, N);
        for (size_t c = 0; !ok && c < cand.n; c++) {
            const line_cand* q = &cand.a[c];
            if (p[0] < q->lo[0] || p[1] < q->lo[1] || p[2] < q->lo[2] || p[0] > q->hi[0] || p[1] > q->hi[1] || p[2] > q->hi[2]) continue;
            if ((ok = elem_locate(q->e, p, N))) hint = q->e;
        }
        if (!ok) continue;
        el[i] = hint;
        for (int k = 0; k < 20; k++) w[20 * i + k] = (float)N[k];
    }
    cv_free_vec(cand);
}

/* nc values per node interpolated in element e with weights w; false where one is missing */
bool line_interp(uint32_t e, const float* w, const float* v, int nc, float* out) {
    if (e == UINT32_MAX) return false;
    int t = G.frd.etype[e];
    uint32_t b = G.frd.eoff[e], nn = G.frd.eoff[e + 1] - b;
    double s[6] = { 0 };
    for (uint32_t i = 0; i < nn; i++) {
        const float* q = v + nc * (size_t)G.frd.conn[b + (uint32_t)cv_frd_node_pos(t, (int)nn, (int)i)];
        for (int c = 0; c < nc; c++) s[c] += w[i] * q[c];
    }
    for (int c = 0; c < nc; c++) { if (s[c] != s[c]) return false; out[c] = (float)s[c]; }
    return true;
}

void refresh_lin(void) {
    if (!G.lin_open || !G.loaded) return;
    int fi = lin_field();
    char key[200];
    const float *A = G.lin_p[0], *B = G.lin_p[1];
    snprintf(key, sizeof key, "%d|%d|%d|%g|%g|%g|%g|%g|%g|%g|%g|%g", G.step, fi, G.csys, G.csys_o[0], G.csys_o[1], G.csys_o[2],
             A[0], A[1], A[2], B[0], B[1], B[2]);
    if (!strcmp(key, G.lin_key)) return;
    snprintf(G.lin_key, sizeof G.lin_key, "%s", key);
    free(G.lin_s); G.lin_s = NULL; G.lin_n = 0;
    G.lin_fi = fi;
    G.lin_t = sqrtf((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2]));
    if (fi < 0) return;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
    const float* v = cache_get(G.step, fi);
    if (!v) return;
    float* tv = to_csys(d, &G.frd, v);
    const float* S = tv ? tv : v;
    static uint32_t el[LIN_N];
    static float w[LIN_N * 20];
    static char where[200];
    G.lin_s = malloc(LIN_N * 6 * sizeof(float));
    if (!G.lin_s) { free(tv); return; }
    char wk[200];
    snprintf(wk, sizeof wk, "%p|%u|%g|%g|%g|%g|%g|%g", (void*)G.frd.xyz, G.frd.n_elems, A[0], A[1], A[2], B[0], B[1], B[2]);
    if (strcmp(wk, where)) { line_locate(A, B, LIN_N, el, w); snprintf(where, sizeof where, "%s", wk); }
    for (int i = 0; i < LIN_N; i++) {
        float* o = G.lin_s + 6 * i;
        uint32_t nd = i == 0 ? G.lin_a : i == LIN_N - 1 ? G.lin_b : UINT32_MAX;   /* an end on a node: its own value */
        if (line_interp(el[i], w + 20 * i, S, 6, o)) continue;
        for (int c = 0; c < 6; c++) o[c] = nd < G.frd.n_nodes ? S[6 * (size_t)nd + c] : NAN;
    }
    G.lin_n = LIN_N;
    free(tv);
}

void app_lin_open(const float A[3], const float B[3], uint32_t na, uint32_t nb) {
    G.lin_open = true;
    memcpy(G.lin_p[0], A, sizeof G.lin_p[0]); memcpy(G.lin_p[1], B, sizeof G.lin_p[1]);
    G.lin_a = na; G.lin_b = nb;
    G.lin_key[0] = 0;
    refresh_lin();
    refresh_path();
}

void app_lin_close(void) {
    free(G.lin_s); G.lin_s = NULL; G.lin_n = 0;
    bool was = G.lin_open;
    G.lin_open = false; G.lin_key[0] = 0;
    if (was) refresh_path();
}

bool app_lin_mb(double m[6], double b[6]) {
    if (!G.lin_n || !cv_linearize(G.lin_s, G.lin_n, G.lin_t, m, b)) return false;
    if (!G.lin_asme) return true;
    float d[3] = { G.lin_p[1][0] - G.lin_p[0][0], G.lin_p[1][1] - G.lin_p[0][1], G.lin_p[1][2] - G.lin_p[0][2] };
    if (G.csys > 0) {                        /* the samples are in cylindrical components: the line's direction too, at its middle */
        float mid[3], Q[3][3], l[3];
        for (int k = 0; k < 3; k++) mid[k] = 0.5f * (G.lin_p[0][k] + G.lin_p[1][k]);
        cv_cyl_basis(mid, G.csys_o, G.csys - 1, Q);
        for (int a = 0; a < 3; a++) l[a] = Q[a][0] * d[0] + Q[a][1] * d[1] + Q[a][2] * d[2];
        memcpy(d, l, sizeof d);
    }
    cv_bend_mask(b, d);
    return true;
}

bool app_lin_csv(const char* path) {
    if (!G.lin_n || G.lin_fi < 0) return false;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[G.lin_fi];
    double m[6], b[6];
    bool ok = app_lin_mb(m, b);
    FILE* o = fopen(path, "wb");
    if (!o) return false;
    cv_fprintf(o, "# %s linearized from (%.9g, %.9g, %.9g) to (%.9g, %.9g, %.9g), t = %.9g, step %d\n", d->name,
            G.lin_p[0][0], G.lin_p[0][1], G.lin_p[0][2], G.lin_p[1][0], G.lin_p[1][1], G.lin_p[1][2], G.lin_t, G.step + 1);
    if (ok) {
        double mb[6], mb2[6];
        for (int c = 0; c < 6; c++) { mb[c] = m[c] + b[c]; mb2[c] = m[c] - b[c]; }
        cv_fprintf(o, "# bending from %s\n", G.lin_asme ? "the components normal to the line only (5-A.4.1.2)" : "all six components");
        cv_fprintf(o, "# membrane: von Mises %.9g, Tresca %.9g\n", cv_mises6(m), cv_tresca6(m, false));
        cv_fprintf(o, "# membrane + bending at start: von Mises %.9g, Tresca %.9g\n", cv_mises6(mb), cv_tresca6(mb, false));
        cv_fprintf(o, "# membrane + bending at end: von Mises %.9g, Tresca %.9g\n", cv_mises6(mb2), cv_tresca6(mb2, false));
        cv_fprintf(o, "# S1+S2+S3 (ASME VIII-2 5.3.2): membrane + bending at start %.9g, at end %.9g\n",
                   mb[0] + mb[1] + mb[2], mb2[0] + mb2[1] + mb2[2]);
        double pk[2] = { -INFINITY, -INFINITY }, tt[2] = { -INFINITY, -INFINITY };   /* the largest anywhere on the line */
        for (int i = 0; i < G.lin_n; i++) {
            double l[6], s[6], p[6];
            cv_lin_at(m, b, G.lin_t, G.lin_t * i / (G.lin_n - 1), l);
            for (int c = 0; c < 6; c++) { s[c] = G.lin_s[6 * i + c]; p[c] = s[c] - l[c]; }
            pk[0] = CV_MAX(pk[0], cv_mises6(p)); pk[1] = CV_MAX(pk[1], cv_tresca6(p, false));
            tt[0] = CV_MAX(tt[0], cv_mises6(s)); tt[1] = CV_MAX(tt[1], cv_tresca6(s, false));
        }
        cv_fprintf(o, "# peak, max on the line: von Mises %.9g, Tresca %.9g\n", pk[0], pk[1]);
        cv_fprintf(o, "# total, max on the line: von Mises %.9g, Tresca %.9g\n", tt[0], tt[1]);
    } else {
        cv_fprintf(o, "# the line leaves the solid: no linearization\n");
    }
    cv_fprintf(o, "x");
    for (int c = 0; c < 6; c++) cv_fprintf(o, ",%s", d->comp[c]);
    for (int c = 0; c < 6; c++) cv_fprintf(o, ",%s_lin", d->comp[c]);
    cv_fprintf(o, ",mises,mises_lin\n");
    for (int i = 0; i < G.lin_n; i++) {
        double x = G.lin_t * i / (G.lin_n - 1), s[6], l[6];
        const float* v = G.lin_s + 6 * i;
        cv_fprintf(o, "%.9g", x);
        if (ok) cv_lin_at(m, b, G.lin_t, x, l); else for (int c = 0; c < 6; c++) l[c] = NAN;
        for (int c = 0; c < 6; c++) { s[c] = v[c]; cv_fprintf(o, ",%.9g", v[c]); }
        for (int c = 0; c < 6; c++) cv_fprintf(o, ",%.9g", l[c]);
        cv_fprintf(o, ",%.9g,%.9g\n", cv_mises6(s), ok ? cv_mises6(l) : NAN);
    }
    return fclose(o) == 0;
}

/* ---- kept lines: several stress classification lines in one model ------------------
   Each is the path that made it (two nodes, or a node and a direction), so showing
   it again rebuilds the same straight line on whatever step is shown. */

/* "SCL n" for the first n no kept line is called */
static void scl_next_name(void) {
    for (int k = 1;; k++) {
        char nm[32];
        snprintf(nm, sizeof nm, "SCL %d", k);
        int i = 0;
        while (i < G.scl_n && strcmp(G.scl[i].name, nm)) i++;
        if (i == G.scl_n) { snprintf(G.scl_name, sizeof G.scl_name, "%s", nm); return; }
    }
}

int app_scl_current(void) {
    if (!G.path_n || G.path_surface) return -1;
    for (int i = 0; i < G.scl_n; i++) {
        const cv_scl* l = &G.scl[i];
        if (l->a == G.path_end[0] && l->dir == G.path_dir && (l->dir || l->b == G.path_end[1])) return i;
    }
    return -1;
}

int app_scl_keep(const char* name) {
    if (!G.loaded || !G.path_n || G.path_surface || G.path_end[0] >= G.frd.n_nodes) return -1;
    int i = app_scl_current();
    if (i < 0) {                                         /* new; the same line again is only renamed */
        cv_scl* n = realloc(G.scl, (size_t)(G.scl_n + 1) * sizeof *n);
        if (!n) return -1;
        G.scl = n;
        i = G.scl_n++;
    }
    cv_scl* l = &G.scl[i];
    char nm[32];
    if (!G.scl_name[0]) scl_next_name();                 /* the box emptied */
    snprintf(nm, sizeof nm, "%s", name && *name ? name : G.scl_name);   /* name may be G.scl_name */
    snprintf(l->name, sizeof l->name, "%s", nm);
    l->a = G.path_end[0]; l->b = G.path_dir ? UINT32_MAX : G.path_end[1]; l->dir = G.path_dir;
    memcpy(l->p, G.path_p, sizeof l->p);
    scl_next_name();
    return i;
}

void app_scl_show(int i) {
    if (i < 0 || i >= G.scl_n || !G.loaded) return;
    const cv_scl l = G.scl[i];
    if (l.a >= G.frd.n_nodes || (!l.dir && l.b >= G.frd.n_nodes)) return;
    G.path_surface = false;
    G.path_lin = true;
    if (l.dir) app_path_ray(l.a, l.dir);
    else { app_path_start(l.a); app_path_end(l.b); }
}

void app_scl_delete(int i) {
    if (i < 0 || i >= G.scl_n) return;
    memmove(&G.scl[i], &G.scl[i + 1], (size_t)(G.scl_n - i - 1) * sizeof *G.scl);
    G.scl_n--;
    scl_next_name();
}

bool app_scl_add(const cv_scl* l) {
    cv_scl* n = realloc(G.scl, (size_t)(G.scl_n + 1) * sizeof *n);
    if (!n) return false;
    G.scl = n;
    G.scl[G.scl_n++] = *l;
    scl_next_name();
    return true;
}

void app_scl_clear(void) {
    free(G.scl); G.scl = NULL; G.scl_n = 0;
    scl_next_name();
}
