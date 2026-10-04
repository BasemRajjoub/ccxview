/* glyph.h -- a symmetric tensor (stress, strain) as a shape at a point. Headless;
   app_tensor.c places the glyphs and render.c draws them.

   Ellipsoid:     semi-axes |s1|, |s2|, |s3| along the principal directions.
   Superquadric:  the same axes with Kindlmann's shape (2004): sharp edges where
                  two values are close, round where they differ, so the eye can
                  tell a rod (one large value) from a disc (two) from a ball
                  (three) at any viewing angle, which an ellipsoid hides. Made
                  for tensors of one sign: it shows magnitudes only.
   Cross:         three bars along the principal directions, half-length |s_i|,
                  heads out for tension, in for compression.
   Schultz-Kindlmann (2010): superquadrics over the whole range of signs. Mixed
                  signs give concave (pinched) shapes whose axis is normal to the
                  two directions of the same sign, so tension and compression
                  differ in shape, not only in colour; the shape changes
                  continuously as a value crosses zero.
   Reynolds:      the surface r(n) = |n.S.n|, the normal stress on the plane of
                  normal n in every direction (peanut shapes).
   HWY (Hashash, Yao, Wotring 2003): r(n) = |S.n - (n.S.n) n|, the shear stress
                  on that plane: lobes where shear is large, waists along the
                  principal directions (where there is none). */
#ifndef CV_GLYPH_H
#define CV_GLYPH_H

#include "base.h"

/* saved by index in the settings: new styles go at the end */
enum { CV_GLYPH_ELLIPSOID, CV_GLYPH_SUPERQUADRIC, CV_GLYPH_CROSS, CV_GLYPH_SK, CV_GLYPH_REYNOLDS, CV_GLYPH_HWY,
       CV_GLYPH_N };
/* "ellipsoid", "superquadric", "cross", "schultz-kindlmann", "reynolds", "hwy" */
extern const char* const cv_glyph_names[CV_GLYPH_N];
int  cv_glyph_style(const char* name);                  /* -1 for an unknown name */
/* the styles coloured by the sign of the normal stress in each direction */
bool cv_glyph_signed(int style);

/* what the glyph shader draws: a superquadric (eq. 13 of Schultz-Kindlmann, with
   their hybrid profile cee), or the normal / shear stress surface */
enum { CV_GSHAPE_SQ, CV_GSHAPE_REYNOLDS, CV_GSHAPE_HWY };

/* sharpness of the Kindlmann superquadric edges (gamma), and the largest beta of the
   Schultz-Kindlmann glyphs (4 in the paper; 3, which they suggest, is milder) */
#define CV_GLYPH_GAMMA 3.f
#define CV_GLYPH_BETA_MAX 3.f

typedef struct {
    float val[3];          /* principal values, the largest magnitude first */
    float axis[3][3];      /* their unit directions, a right-handed frame */
    int   shape;           /* CV_GSHAPE_* */
    float base[3][3];      /* the unit glyph's x, y, z axes in the model (unit vectors) */
    float len[3];          /* model length per unit along each: k |eigenvalue| for a
                              superquadric (never below min_frac of the largest),
                              k for Reynolds / HWY */
    float lam[3];          /* the signed eigenvalue along each base axis */
    float alpha, beta, cee;/* superquadric exponents (1, 1, 1: a sphere) */
} cv_glyph;

/* The glyph of s (XX YY ZZ XY YZ ZX, or XY XZ YZ shears with xz) with k model
   lengths per unit of the tensor. min_frac keeps a flat tensor (a shell's plane
   stress) a visible disc. false for NaN or a zero tensor. */
bool  cv_glyph_make(const float s[6], bool xz, int style, float k, float min_frac, cv_glyph* g);
/* the largest distance of the glyph's surface from its centre */
float cv_glyph_extent(const cv_glyph* g);
/* A glyph reaching further than max_len shrunk to it, shape kept. */
void  cv_glyph_cap(cv_glyph* g, float max_len);

/* The magnitude glyphs are sized to: the q-quantile (0..1) of m[0..n), NaN-free,
   so one singular peak (a clamped corner, a point load) does not shrink the rest
   to dots. Sorts m. 0 for n = 0. */
float cv_glyph_ref(float* m, size_t n, float q);

/* Kindlmann's exponents from magnitudes l0 >= l1 >= l2 >= 0; planar: the round
   axis is the smallest one's, else the largest one's */
void cv_superquad_shape(const float l[3], float gamma, float* alpha, float* beta, bool* planar);
/* Schultz-Kindlmann: the (u, v) shape coordinates of eigenvalues l0 >= l1 >= l2,
   the (alpha, beta, cee) there, and whether the round axis is the first
   eigenvector (two values <= 0 with the first > 0, or one sign with the middle
   value nearer the last), else the last */
void cv_sk_uv(const float l[3], float uv[2]);
void cv_sk_abc(const float uv[2], float beta_max, float abc[3]);
bool cv_sk_axis_first(const float l[3]);
/* A point of the unit superquadric at theta in [0, 2pi), phi in [0, pi], round about
   z, with the hybrid profile cee along y: what the vertex shader in render.c
   computes (keep the two in step). */
void cv_superquad_point(float alpha, float beta, float cee, float theta, float phi, float q[3]);

#endif
