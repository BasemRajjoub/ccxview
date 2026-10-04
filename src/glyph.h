/* glyph.h -- a symmetric tensor (stress, strain) as a shape at a point. Headless;
   app_tensor.c places the glyphs and render.c draws them.

   Ellipsoid:     semi-axes |s1|, |s2|, |s3| along the principal directions.
   Superquadric:  the same axes with Kindlmann's shape (2004): sharp edges where
                  two values are close, round where they differ, so the eye can
                  tell a rod (one large value) from a disc (two) from a ball
                  (three) at any viewing angle, which an ellipsoid hides.
   Cross:         three bars along the principal directions, half-length |s_i|,
                  the colour telling tension from compression, which the two
                  shapes cannot (they show magnitudes). */
#ifndef CV_GLYPH_H
#define CV_GLYPH_H

#include "base.h"

enum { CV_GLYPH_ELLIPSOID, CV_GLYPH_SUPERQUADRIC, CV_GLYPH_CROSS, CV_GLYPH_N };
extern const char* const cv_glyph_names[CV_GLYPH_N];   /* "ellipsoid", "superquadric", "cross" */
int cv_glyph_style(const char* name);                   /* -1 for an unknown name */

/* sharpness of the superquadric edges (Kindlmann's gamma) */
#define CV_GLYPH_GAMMA 3.f

typedef struct {
    float val[3];          /* principal values, the largest magnitude first */
    float axis[3][3];      /* their unit directions, a right-handed frame */
    float len[3];          /* semi-axis lengths: k |val|, never below min_frac len[0] */
    float alpha, beta;     /* superquadric exponents about and along the round axis (1, 1: an ellipsoid) */
    bool  planar;          /* the round axis is axis[2] (disc-like), else axis[0] (rod-like) */
} cv_glyph;

/* The glyph of s (XX YY ZZ XY YZ ZX, or XY XZ YZ shears with xz) with k model
   lengths per unit of the tensor. min_frac keeps a flat tensor (a shell's plane
   stress) a visible disc. false for NaN or a zero tensor. */
bool cv_glyph_make(const float s[6], bool xz, int style, float k, float min_frac, cv_glyph* g);

/* The magnitude glyphs are sized to: the q-quantile (0..1) of m[0..n), NaN-free,
   so one singular peak (a clamped corner, a point load) does not shrink the rest
   to dots. Sorts m. 0 for n = 0. */
float cv_glyph_ref(float* m, size_t n, float q);
/* A glyph whose longest semi-axis exceeds max_len shrunk to it, shape kept. */
void  cv_glyph_cap(cv_glyph* g, float max_len);

/* Kindlmann's exponents from magnitudes l0 >= l1 >= l2 >= 0 */
void cv_superquad_shape(const float l[3], float gamma, float* alpha, float* beta, bool* planar);
/* A point of the unit glyph at theta in [0, 2pi), phi in [0, pi]: what the
   vertex shader in render.c computes (keep the two in step). */
void cv_superquad_point(float alpha, float beta, bool planar, float theta, float phi, float q[3]);

#endif
