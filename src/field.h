/* field.h -- scalars that can be painted: components, magnitude, von Mises,
   principal values;
   ranges, per-element means, auto deformation scale. Headless. */
#ifndef CV_FIELD_H
#define CV_FIELD_H

#include "frd.h"

/* Derived components. Principal values need to know where the shears are:
   .frd writes XY YZ ZX, the .dat xy xz yz, hence two sets of codes. */
enum { CV_COMP_MAG = -1, CV_COMP_MISES = -2,
       CV_COMP_P1 = -3, CV_COMP_P2 = -4, CV_COMP_P3 = -5,               /* shears XY YZ ZX */
       CV_COMP_P1_XZ = -6, CV_COMP_P2_XZ = -7, CV_COMP_P3_XZ = -8 };     /* shears xy xz yz */
#define CV_MAX_OPTS (CV_MAX_COMP + 5)   /* components + magnitude, von Mises, 3 principal */

typedef struct {
    char label[40];
    int  comp;            /* component index, or one of the CV_COMP_ codes above */
} cv_scalar_opt;

/* Options for one field, derived ones first (magnitude / von Mises / principal). */
int   cv_field_options(const cv_field_desc* d, cv_scalar_opt* out, int max);

/* One scalar per node from decoded values (vals[node*ncomp + c]). NaN stays NaN. */
void  cv_field_scalar(const float* vals, int ncomp, uint32_t n, int comp, float* out);

float cv_von_mises(const float s[6]);   /* SXX SYY SZZ SXY SYZ SZX */
/* Principal values of a symmetric tensor, largest first. Shears XY YZ ZX, or
   xy xz yz with xz_order. Strains must hold tensor shears (CalculiX does). */
void  cv_principal(const float s[6], bool xz_order, float out[3]);

/* min/max ignoring NaN. false if every value is NaN. */
bool  cv_range(const float* v, size_t n, float* mn, float* mx);

/* Where a value t in [0,1] lands with `bands` discrete steps: the centre of its
   band, so the colour is the same as the shader's (render.c mirrors this). */
static inline float cv_band_center(float t, int bands) {
    if (bands <= 0) return t;
    float b = (float)bands, k = t * b;
    if (k < 0) k = 0;
    k = (float)(int)k;                       /* floor for k >= 0 */
    if (k > b - 1) k = b - 1;
    return (k + 0.5f) / b;
}

/* Signed data (mn < 0 < mx) gets a range symmetric about zero. */
void  cv_center_zero(float* mn, float* mx);

/* Mean of each element's node values (NaN if any node is NaN). */
void  cv_elem_mean(const cv_frd* f, const float* nodal, float* per_elem);

/* Magnify small displacements to ~10% of the model; never shrink real motion. */
float cv_auto_deform(float peak_disp, float model_diag);

#endif
