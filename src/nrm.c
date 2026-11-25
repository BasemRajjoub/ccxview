/* nrm.c -- smooth normals of the skin at its nodes (nrm.h) */
#include "nrm.h"
#include <math.h>
#include <stdlib.h>

/* the triangle's normal (twice its area long) on the shape as drawn; floats: a skin of
   millions of triangles in tens of ms, which is plenty for shading */
static void tri_n(const float* xyz, const float* disp, float s, const uint32_t* v, float n[3]) {
    float p[3][3];
    for (int j = 0; j < 3; j++)
        for (int k = 0; k < 3; k++) p[j][k] = xyz[3 * (size_t)v[j] + k] + (disp ? s * disp[3 * (size_t)v[j] + k] : 0.f);
    float u[3] = { p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2] };
    float w[3] = { p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2] };
    n[0] = u[1] * w[2] - u[2] * w[1]; n[1] = u[2] * w[0] - u[0] * w[2]; n[2] = u[0] * w[1] - u[1] * w[0];
}

bool cv_nrm_skin(const float* xyz, const float* disp, float scale, uint32_t nn,
                 const uint32_t* tri, size_t nt, int8_t* out) {
    float* acc = calloc((size_t)nn * 3 + 1, sizeof(float));
    if (!acc) return false;
    for (size_t t = 0; t < nt; t++) {
        const uint32_t* v = tri + 3 * t;
        if (v[0] >= nn || v[1] >= nn || v[2] >= nn) continue;
        float n[3];
        tri_n(xyz, disp, scale, v, n);
        for (int j = 0; j < 3; j++)                   /* |n| = twice the area: weighted by area */
            for (int k = 0; k < 3; k++) acc[3 * (size_t)v[j] + k] += n[k];
    }
    for (uint32_t i = 0; i < nn; i++) {      /* unit; what is left of normals that (nearly) cancel is no direction: 0 */
        float* n = acc + 3 * (size_t)i;
        double l = sqrt((double)n[0] * n[0] + (double)n[1] * n[1] + (double)n[2] * n[2]);
        for (int k = 0; k < 3; k++) n[k] = l > 1e-30 ? (float)(n[k] / l) : 0.f;
        out[4 * (size_t)i + 3] = 0;           /* 1 while building: on a crease */
    }
    const double lim = cos(CV_NRM_CREASE * 3.14159265358979 / 180.0);
    for (size_t t = 0; t < nt; t++) {
        const uint32_t* v = tri + 3 * t;
        if (v[0] >= nn || v[1] >= nn || v[2] >= nn) continue;
        float n[3];
        tri_n(xyz, disp, scale, v, n);
        float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), cut = (float)lim * l;
        if (!(l > 0)) continue;
        for (int j = 0; j < 3; j++) {
            const float* m = acc + 3 * (size_t)v[j];
            if (n[0] * m[0] + n[1] * m[1] + n[2] * m[2] < cut) out[4 * (size_t)v[j] + 3] = 1;
        }
    }
    for (uint32_t i = 0; i < nn; i++) {
        bool crease = out[4 * (size_t)i + 3] != 0;
        for (int k = 0; k < 3; k++) out[4 * (size_t)i + k] = crease ? 0 : (int8_t)lrint(acc[3 * (size_t)i + k] * 127.f);
        out[4 * (size_t)i + 3] = 0;
    }
    free(acc);
    return true;
}
