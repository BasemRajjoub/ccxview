/* integ.h -- integrals of a nodal field over solid elements and over their faces,
   and sums over nodes. Headless: a mesh and nodal values in, numbers out.

   On the undeformed shape, with each element's own shape functions and a Gauss
   rule exact to degree 5 (gauss.h): the value at a point is interpolated from the
   element's nodes, the mid-side nodes of a quadratic element included. Solids only
   (FRD types 1..6: hex 8/20, wedge 6/15, tet 4/10); CalculiX writes shells, beams
   and plane elements to the .frd as the solids it expanded them to.

   The volume average <f> = (1/V) int f dV = (1/V) sum_e int_e f dV is the
   homogenised value of an RVE: <S> and <E> per component. */
#ifndef CV_INTEG_H
#define CV_INTEG_H

#include "frd.h"

enum { CV_INTEG_MAXC = 32 };

typedef struct {
    double   size;                    /* volume (area) of what was counted */
    double   integ[CV_INTEG_MAXC];    /* int v_c dV (dA), per component */
    double   push[3];                 /* faces: -int v_0 n dA, n outward: the force of a pressure v_0 */
    double   traction[3];             /* faces, the first 6 components a tensor (XX YY ZZ XY YZ ZX):
                                         int S n dA, the force the rest of the body puts on the
                                         elements through these faces */
    uint32_t n;                       /* elements (faces) counted */
    uint32_t skipped;                 /* not a solid, or no such face */
    uint32_t missing;                 /* left out: one of its nodes has no value (NaN) */
} cv_integ;

/* Over the elements elems[0..n) (dense indices; NULL: every element of f).
   vals: nc values per node, vals[node * nc + c], NULL for the volume alone. */
void cv_integ_volume(const cv_frd* f, const uint32_t* elems, uint32_t n, const float* vals, int nc, cv_integ* out);

/* Over the faces faces[k] (0-based CalculiX numbering, S1 -> 0) of the elements
   elems[k], k < n. As cv_integ_volume otherwise. */
void cv_integ_faces(const cv_frd* f, const uint32_t* elems, const uint8_t* faces, uint32_t n,
                    const float* vals, int nc, cv_integ* out);

/* Sums over nodes (reaction forces, FORC): a sum, not an integral. With 3 or more
   components the first three are a force and moment = sum (x - about) x F. */
typedef struct {
    double   sum[CV_INTEG_MAXC];
    double   moment[3];
    uint32_t n, missing;              /* nodes counted, nodes left out (NaN) */
} cv_nsum;
void cv_integ_nodes(const cv_frd* f, const uint32_t* nodes, uint32_t n, const float* vals, int nc,
                    const double about[3], cv_nsum* out);

/* The nodes of face `face` of element e, the mid-side ones too (dense indices,
   up to 8); 0 when it has no such face. */
int cv_face_nodes(const cv_frd* f, uint32_t e, int face, uint32_t out[8]);

/* One element: its volume; x its nn nodes in .frd order. NaN: not a solid. */
double cv_elem_volume(int frd_type, int nn, const double (*x)[3]);

#endif
