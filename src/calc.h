/* calc.h -- calculated fields: a formula over the .frd fields, evaluated per node.
   Headless. The formula is ordinary maths (TinyExpr): + - * / ^ %, comparisons
   < <= > >= == != (1 or 0), && || !, parentheses, and the functions
     abs sqrt exp ln log (natural) log10 pow sin cos tan asin acos atan atan2
     sinh cosh tanh floor ceil min max clamp(x,lo,hi) sign if(c,a,b), pi, e.
   Names, upper or lower case:
     FIELD_COMP     a component: STRESS_SXX, DISP_D1, TOSTRAIN_EXY
     FIELD_MAG      magnitude of a vector; FIELD_MISES, FIELD_P1..P3 von Mises and
                    principal values of a tensor field (STRESS_MISES, TOSTRAIN_P1)
     COMP           a component on its own when only one field has it (SXX, D1)
     FIELD          a one-component field's value (NDTEMP), a vector's magnitude (DISP)
     MISES S1 S2 S3 von Mises and principal stresses, E1 E2 E3 principal strains
     X Y Z          undeformed node coordinates;  TIME  the step's time / frequency
   Values are the stored global components, NaN where a node has none. */
#ifndef CV_CALC_H
#define CV_CALC_H

#include "frd.h"

typedef struct cv_calc cv_calc;

/* The decoded values of field `field` of step `step` (vals[node * ncomp + c]); they
   need to stay valid only until the next call. NULL: cannot (out of memory). */
typedef const float* (*cv_calc_get_fn)(void* ud, int step, int field);

/* Compiles `expr` against the fields of every step of f. NULL with a message in
   err (e.g. "unknown name SXY2 at 7") when it does not compile. */
cv_calc* cv_calc_compile(const cv_frd* f, const char* expr, char* err, size_t errn);

/* The value at the nodes `nodes` (NULL: every node, n = f->n_nodes) of step `step`
   into out[n]. false (out all NaN) when a field the formula uses is missing in that
   step -- cv_calc_missing() names it -- or the getter failed. */
bool cv_calc_eval(cv_calc* c, const cv_frd* f, int step, cv_calc_get_fn get, void* ud,
                  const uint32_t* nodes, uint32_t n, float* out);
const char* cv_calc_missing(const cv_calc* c);

/* true when the formula reads a field (false: only X Y Z TIME and numbers) */
bool cv_calc_uses_fields(const cv_calc* c);

void cv_calc_free(cv_calc* c);

/* The names a formula can use for the fields of f, one line per field
   ("STRESS: SXX SYY SZZ SXY SYZ SZX MISES P1 P2 P3"), into out. Returns the length. */
size_t cv_calc_names(const cv_frd* f, char* out, size_t cap);

#endif
