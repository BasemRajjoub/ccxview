/* vmath.h -- the few vector/matrix helpers the viewer needs. Column-major mat4
   (m[col*4 + row]), matching GLSL. */
#ifndef CV_VMATH_H
#define CV_VMATH_H

#include <math.h>

typedef struct { float x, y, z; } v3;

static inline v3    v3_make(float x, float y, float z) { v3 r = { x, y, z }; return r; }
static inline v3    v3_add(v3 a, v3 b)   { return v3_make(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline v3    v3_sub(v3 a, v3 b)   { return v3_make(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline v3    v3_scale(v3 a, float s) { return v3_make(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(v3 a, v3 b)   { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline v3    v3_cross(v3 a, v3 b) {
    return v3_make(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline v3 v3_norm(v3 a) {
    float l = sqrtf(v3_dot(a, a));
    return l > 0 ? v3_scale(a, 1.f / l) : a;
}

static inline void m4_mul(float* r, const float* a, const float* b) {   /* r = a * b */
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
            t[c * 4 + rr] = s;
        }
    for (int i = 0; i < 16; i++) r[i] = t[i];
}

static inline void m4_look_at(float* m, v3 eye, v3 fwd, v3 right, v3 up) {
    float r[16] = {
        right.x, up.x, -fwd.x, 0,
        right.y, up.y, -fwd.y, 0,
        right.z, up.z, -fwd.z, 0,
        -v3_dot(right, eye), -v3_dot(up, eye), v3_dot(fwd, eye), 1,
    };
    for (int i = 0; i < 16; i++) m[i] = r[i];
}

static inline void m4_perspective(float* m, float fovy, float aspect, float n, float f) {
    float t = 1.f / tanf(fovy * 0.5f);
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = t / aspect;
    m[5] = t;
    m[10] = (f + n) / (n - f);
    m[11] = -1;
    m[14] = 2 * f * n / (n - f);
}

static inline void m4_ortho(float* m, float hw, float hh, float n, float f) {
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = 1.f / hw;
    m[5] = 1.f / hh;
    m[10] = -2.f / (f - n);
    m[14] = -(f + n) / (f - n);
    m[15] = 1;
}

#endif
