/* glyph.c -- tensor glyphs: principal axes, lengths and superquadric shape. */
#include "glyph.h"
#include "field.h"
#include <math.h>

const char* const cv_glyph_names[CV_GLYPH_N] = { "ellipsoid", "superquadric", "cross" };

int cv_glyph_style(const char* name) {
    for (int i = 0; i < CV_GLYPH_N; i++)
        if (name && !strcmp(name, cv_glyph_names[i])) return i;
    return -1;
}

void cv_superquad_shape(const float l[3], float gamma, float* alpha, float* beta, bool* planar) {
    double sum = (double)l[0] + l[1] + l[2];
    double cl = sum > 0 ? (l[0] - l[1]) / sum : 0;           /* linear: one value stands out */
    double cp = sum > 0 ? 2 * (l[1] - l[2]) / sum : 0;       /* planar: two do */
    *planar = cp > cl;
    double a = pow(1 - (*planar ? cl : cp), gamma), b = pow(1 - (*planar ? cp : cl), gamma);
    /* 0 would be a box with zero-width edges, and pow(0, 0) in the shader */
    *alpha = (float)CV_MAX(a, 0.05);
    *beta = (float)CV_MAX(b, 0.05);
}

static float spow(float x, float e) { return x < 0 ? -powf(-x, e) : powf(x, e); }

void cv_superquad_point(float alpha, float beta, bool planar, float theta, float phi, float q[3]) {
    float ct = cosf(theta), st = sinf(theta), cf = cosf(phi), sf = sinf(phi);
    if (planar) {                                            /* round about z */
        q[0] = spow(ct, alpha) * spow(sf, beta);
        q[1] = spow(st, alpha) * spow(sf, beta);
        q[2] = spow(cf, beta);
    } else {                                                 /* round about x */
        q[0] = spow(cf, beta);
        q[1] = -spow(st, alpha) * spow(sf, beta);
        q[2] = spow(ct, alpha) * spow(sf, beta);
    }
}

static int cmp_float(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return (x > y) - (x < y);
}

float cv_glyph_ref(float* m, size_t n, float q) {
    if (!n) return 0;
    qsort(m, n, sizeof *m, cmp_float);
    q = CV_MIN(CV_MAX(q, 0.f), 1.f);
    return m[(size_t)(q * (float)(n - 1) + 0.5f)];
}

void cv_glyph_cap(cv_glyph* g, float max_len) {
    if (!(g->len[0] > max_len)) return;
    float f = max_len / g->len[0];
    for (int i = 0; i < 3; i++) g->len[i] *= f;
}

bool cv_glyph_make(const float s[6], bool xz, int style, float k, float min_frac, cv_glyph* g) {
    float val[3], vec[3][3];
    if (!cv_principal_dirs(s, xz, val, vec)) return false;
    int o[3] = { 0, 1, 2 };                                  /* by magnitude, largest first */
    for (int i = 0; i < 2; i++)
        for (int j = i + 1; j < 3; j++)
            if (fabsf(val[o[j]]) > fabsf(val[o[i]])) { int t = o[i]; o[i] = o[j]; o[j] = t; }
    float big = fabsf(val[o[0]]);
    if (!(big > 0)) return false;
    for (int i = 0; i < 3; i++) {
        g->val[i] = val[o[i]];
        memcpy(g->axis[i], vec[o[i]], sizeof g->axis[i]);
    }
    const float* a = g->axis[0], *b = g->axis[1];            /* right-handed: axis 2 = 0 x 1 */
    g->axis[2][0] = a[1] * b[2] - a[2] * b[1];
    g->axis[2][1] = a[2] * b[0] - a[0] * b[2];
    g->axis[2][2] = a[0] * b[1] - a[1] * b[0];
    float l[3];
    for (int i = 0; i < 3; i++) {
        l[i] = fabsf(g->val[i]);
        g->len[i] = k * CV_MAX(l[i], min_frac * big);
    }
    g->alpha = g->beta = 1;
    g->planar = false;
    if (style == CV_GLYPH_SUPERQUADRIC) cv_superquad_shape(l, CV_GLYPH_GAMMA, &g->alpha, &g->beta, &g->planar);
    return true;
}
