/* glyph.c -- tensor glyphs: principal axes, lengths and superquadric shapes. */
#include "glyph.h"
#include "field.h"
#include <math.h>

const char* const cv_glyph_names[CV_GLYPH_N] = { "ellipsoid", "superquadric", "cross", "schultz-kindlmann",
                                                  "reynolds", "hwy" };

int cv_glyph_style(const char* name) {
    for (int i = 0; i < CV_GLYPH_N; i++)
        if (name && !strcmp(name, cv_glyph_names[i])) return i;
    return -1;
}

bool cv_glyph_signed(int style) { return style == CV_GLYPH_SK || style == CV_GLYPH_REYNOLDS || style == CV_GLYPH_HWY; }

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

/* ---- Schultz-Kindlmann 2010 ---------------------------------------------------------
   Eigenvalues l0 >= l1 >= l2 scaled by the largest magnitude lie on a lune that the
   paper unfolds onto the unit square (u, v): u from the middle value, v from the last
   (or the first, past the indefinite diagonal). The square is cut into ten triangles
   whose corners carry hand-picked superquadric parameters (their Fig. 5d): inside a
   triangle (alpha, beta, cee) blend barycentrically. */

void cv_sk_uv(const float l[3], float uv[2]) {
    float m = CV_MAX(fabsf(l[0]), CV_MAX(fabsf(l[1]), fabsf(l[2])));
    if (!(m > 0)) { uv[0] = uv[1] = 0; return; }
    float x = l[0] / m, y = l[1] / m, z = l[2] / m;
    uv[0] = 0.5f * (y + 1);
    uv[1] = (x > -z ? 0.5f * (z + 1) : 0.5f * (x - 1)) - uv[0] + 1;
}

static int sk_zone(float u, float v) {
    if (u > 0.5f) {
        if (u + v > 1.5f) return u < v ? 0 : 1;
        return 2 * u + v > 2 ? 2 : u + v > 1 ? 3 : 4;
    }
    if (u + v > 0.5f) return u + v > 1 ? 5 : 2 * u + v > 1 ? 6 : 7;
    return u < v ? 8 : 9;
}

void cv_sk_abc(const float uv[2], float beta_max, float abc[3]) {
    static const float corner[11][2] = { { 1, 1 }, { .5f, 1 }, { .75f, .75f }, { 1, .5f }, { 1, 0 }, { .5f, .5f },
                                         { 0, 1 }, { 0, .5f }, { .25f, .25f }, { .5f, 0 }, { 0, 0 } };
    enum { BALL, CYLI, FUNK, THRN, OCTA, CONE, HALF };
    const float shape[7][3] = { { 1, 1, 1 }, { 1, 0, 0 }, { 0, beta_max, 2 }, { 1, beta_max, 3 },
                                { 0, 2, 2 }, { 1, 2, 2 }, { .5f, .5f, .5f } };
    /* per zone: three corners and the shape at each */
    static const unsigned char tri[10][3][2] = {
        { { 0, BALL }, { 1, CYLI }, { 2, HALF } }, { { 0, BALL }, { 2, HALF }, { 3, CYLI } },
        { { 1, OCTA }, { 3, CONE }, { 4, THRN } }, { { 1, OCTA }, { 4, THRN }, { 5, FUNK } },
        { { 4, THRN }, { 5, FUNK }, { 9, CONE } }, { { 1, CONE }, { 5, FUNK }, { 6, THRN } },
        { { 5, FUNK }, { 6, THRN }, { 9, OCTA } }, { { 6, THRN }, { 7, CONE }, { 9, OCTA } },
        { { 7, CYLI }, { 8, HALF }, { 10, BALL } }, { { 8, HALF }, { 9, CYLI }, { 10, BALL } } };
    const unsigned char (*t)[2] = tri[sk_zone(uv[0], uv[1])];
    const float *a = corner[t[0][0]], *b = corner[t[1][0]], *c = corner[t[2][0]];
    /* barycentric weights from the sub-triangle areas */
    float w[3] = { fabsf((b[0] - uv[0]) * (c[1] - uv[1]) - (c[0] - uv[0]) * (b[1] - uv[1])),
                   fabsf((c[0] - uv[0]) * (a[1] - uv[1]) - (a[0] - uv[0]) * (c[1] - uv[1])),
                   fabsf((a[0] - uv[0]) * (b[1] - uv[1]) - (b[0] - uv[0]) * (a[1] - uv[1])) };
    float sw = w[0] + w[1] + w[2];
    for (int i = 0; i < 3; i++) {
        abc[i] = 0;
        for (int k = 0; k < 3; k++) abc[i] += (sw > 0 ? w[k] / sw : 1.f / 3) * shape[t[k][1]][i];
    }
}

bool cv_sk_axis_first(const float l[3]) {
    if (l[1] > 0 && l[2] > 0) return l[0] - l[1] > l[1] - l[2];     /* positive: rod-like about the first */
    if (l[1] > 0) return false;                                        /* two positive: normal to them */
    if (l[0] > 0) return true;                                         /* two negative: normal to them */
    return l[0] - l[1] > l[1] - l[2];                                  /* negative: disc-like about the first */
}

static float spow(float x, float e) { return x < 0 ? -powf(-x, e) : powf(x, e); }

void cv_superquad_point(float alpha, float beta, float cee, float theta, float phi, float q[3]) {
    float ct = cosf(theta), st = sinf(theta), cf = cosf(phi), sf = sinf(phi), sm = spow(sf, beta);
    q[0] = spow(ct, alpha) * sm;
    q[1] = spow(st, alpha) * sm;
    q[2] = spow(cf, beta);
    if (cee != beta && sm != 0) {                  /* the profile along y as if beta were cee */
        float z = CV_MIN(1.f, CV_MAX(-1.f, spow(q[2], 1 / cee)));
        q[1] *= spow(sinf(acosf(z)), cee) / sm;
    }
}

/* ---- the glyph ------------------------------------------------------------------- */

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

float cv_glyph_extent(const cv_glyph* g) {
    float r = 0;
    if (g->shape == CV_GSHAPE_SQ) {
        for (int i = 0; i < 3; i++) r = CV_MAX(r, g->len[i]);
    } else {
        float hi = CV_MAX(g->lam[0], CV_MAX(g->lam[1], g->lam[2])), lo = CV_MIN(g->lam[0], CV_MIN(g->lam[1], g->lam[2]));
        r = g->len[0] * (g->shape == CV_GSHAPE_HWY ? 0.5f * (hi - lo) : CV_MAX(fabsf(hi), fabsf(lo)));
    }
    return r;
}

void cv_glyph_cap(cv_glyph* g, float max_len) {
    float r = cv_glyph_extent(g);
    if (!(r > max_len)) return;
    for (int i = 0; i < 3; i++) g->len[i] *= max_len / r;
}

/* base axes x, y, z = directions a, b, c (rows of d) with their eigenvalues */
static void set_base(cv_glyph* g, const float d[3][3], const float* lam, int a, int b, int c) {
    const int o[3] = { a, b, c };
    for (int i = 0; i < 3; i++) { memcpy(g->base[i], d[o[i]], sizeof g->base[i]); g->lam[i] = lam[o[i]]; }
}

bool cv_glyph_make(const float s[6], bool xz, int style, float k, float min_frac, cv_glyph* g) {
    float sv[3], sd[3][3];                                   /* by value, largest first */
    if (!cv_principal_dirs(s, xz, sv, sd)) return false;
    int o[3] = { 0, 1, 2 };                                  /* by magnitude, largest first */
    for (int i = 0; i < 2; i++)
        for (int j = i + 1; j < 3; j++)
            if (fabsf(sv[o[j]]) > fabsf(sv[o[i]])) { int t = o[i]; o[i] = o[j]; o[j] = t; }
    float big = fabsf(sv[o[0]]);
    if (!(big > 0)) return false;
    for (int i = 0; i < 3; i++) {
        g->val[i] = sv[o[i]];
        memcpy(g->axis[i], sd[o[i]], sizeof g->axis[i]);
    }
    const float* a = g->axis[0], *b = g->axis[1];            /* right-handed: axis 2 = 0 x 1 */
    g->axis[2][0] = a[1] * b[2] - a[2] * b[1];
    g->axis[2][1] = a[2] * b[0] - a[0] * b[2];
    g->axis[2][2] = a[0] * b[1] - a[1] * b[0];
    memcpy(sd[o[2]], g->axis[2], sizeof sd[0]);              /* the same frame by value */

    g->shape = CV_GSHAPE_SQ;
    g->alpha = g->beta = g->cee = 1;
    float l[3] = { fabsf(g->val[0]), fabsf(g->val[1]), fabsf(g->val[2]) };
    if (style == CV_GLYPH_SUPERQUADRIC) {
        bool planar;
        cv_superquad_shape(l, CV_GLYPH_GAMMA, &g->alpha, &g->beta, &planar);
        g->cee = g->beta;
        if (planar) set_base(g, g->axis, g->val, 0, 1, 2);   /* round about the smallest */
        else set_base(g, g->axis, g->val, 2, 1, 0);          /* round about the largest */
    } else if (style == CV_GLYPH_SK) {
        float uv[2], abc[3];
        cv_sk_uv(sv, uv);
        cv_sk_abc(uv, CV_GLYPH_BETA_MAX, abc);
        g->alpha = CV_MAX(abc[0], 0.02f); g->beta = CV_MAX(abc[1], 0.02f); g->cee = CV_MAX(abc[2], 0.02f);
        if (cv_sk_axis_first(sv)) set_base(g, sd, sv, 2, 1, 0);   /* the middle direction is always y */
        else set_base(g, sd, sv, 0, 1, 2);
    } else if (style == CV_GLYPH_REYNOLDS || style == CV_GLYPH_HWY) {
        g->shape = style == CV_GLYPH_HWY ? CV_GSHAPE_HWY : CV_GSHAPE_REYNOLDS;
        set_base(g, sd, sv, 0, 1, 2);
        for (int i = 0; i < 3; i++) g->len[i] = k;
        return true;
    } else {
        set_base(g, g->axis, g->val, 0, 1, 2);
    }
    for (int i = 0; i < 3; i++) g->len[i] = k * CV_MAX(fabsf(g->lam[i]), min_frac * big);
    return true;
}
