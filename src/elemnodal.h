/* elemnodal.h -- results written per element and node (FRD IRTYPE 3, FEMaster's
   STRESS / STRPOS / STRNEG): which fields belong together, which surface of a shell
   shows, and the optional averaging at the nodes. Headless.

   A value lives in a slot, a position in conn[] (cv_frd_read_elnod), so two elements
   meeting at a node each keep their own.

   Entries: a step with element-nodal STRESS and STRPOS and/or STRNEG offers one
   "Stress" (named after STRESS, which carries the solids; STRPOS / STRNEG are folded
   in); STRPOS and STRNEG alone make one "Shell stress" (named after STRPOS). Every
   other field keeps its own entry.

   Sides: a shell's STRPOS is on the face its normal points out of (right-hand rule on
   its connectivity), STRNEG on the reverse. Both: each on its own face; Top / Bottom:
   one surface on both faces. A surface the file does not have stays missing (NaN).

   Averaging: the value of every element at a node is replaced by the mean of those that
   belong together -- visible, solids with solids, shells with shells, the same group key
   (the caller's section and material), and for shells a normal pointing the same way
   (dot > 0). Missing values stay missing. */
#ifndef CV_ELEMNODAL_H
#define CV_ELEMNODAL_H

#include "frd.h"
#include "nodalrec.h"

enum { CV_EN_BOTH, CV_EN_TOP, CV_EN_BOTTOM, CV_EN_SIDE_N };
enum { CV_EN_AVG_NONE, CV_EN_AVG_SCALAR, CV_EN_AVG_TENSOR, CV_EN_AVG_N };
enum { CV_EN_ONE, CV_EN_STRESS, CV_EN_SHELL };       /* an entry: a field alone, Stress, Shell stress */

typedef struct { int kind, stress, pos, neg; } cv_en_group;     /* field indices of the step, -1 none */

/* what field fi of the step stands for: its entry (kind CV_EN_ONE: itself alone), or, for a
   field folded into another's entry, CV_EN_ONE with *folded set (g then holds that entry's
   fields, kind its kind). g may be NULL. */
int         cv_en_entry(const cv_step* st, int fi, cv_en_group* g, bool* folded);
/* The name shown for field fi: "Stress", "Shell stress", else cv_field_title. */
const char* cv_en_title(const cv_step* st, int fi);
bool        cv_en_is_shell(int frd_type);            /* S3 S6 S4 S8: 7..10 */
bool        cv_en_is_solid(int frd_type);            /* 1..6 */
/* unit normal of a shell (right-hand rule on its connectivity, undeformed), zero for others */
void        cv_en_normal(const cv_frd* f, uint32_t e, float n[3]);
/* per element normals, 3 each; NULL on OOM */
float*      cv_en_normals(const cv_frd* f);

/* The values on a shell face (face 0: the positive-normal side, 1: the reverse) of an
   element of type t, from the arrays of STRESS / STRPOS / STRNEG (NULL: not in the file);
   the array to read, NULL for missing. A group of kind CV_EN_ONE has the one array in solid. */
const float* cv_en_source(int kind, int frd_type, int face, int side, const float* solid, const float* pos, const float* neg);

/* in[slot * nc + c] -> out, averaged as described above. group per element or NULL (one),
   nrm from cv_en_normals (NULL: shells all alike), vis per element or NULL. Hidden elements
   give NaN. */
void cv_en_average(const cv_frd* f, const cv_nr_topo* t, const uint8_t* vis, const uint32_t* group,
                   const float* nrm, const float* in, int nc, float* out);

/* The scalar (component comp of cv_field_scalar) of the shown entry at every slot, for the
   face of a shell that looks along +normal (front) and the reverse (back); solids have the
   same on both. s: the decoded arrays, slots * nc each, NULL for one not in the file. avg:
   scalars averaged (CV_EN_AVG_SCALAR) or the components first (CV_EN_AVG_TENSOR). front and
   back hold cv_frd_slots floats; false on OOM. */
typedef struct { int kind; const float *solid, *pos, *neg; } cv_en_src;
bool cv_en_scalars(const cv_frd* f, const cv_nr_topo* t, const cv_en_src* s, int nc, int comp, int side, int avg,
                   const uint8_t* vis, const uint32_t* group, const float* nrm, float* front, float* back);

#endif
