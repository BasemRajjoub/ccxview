/* nodalrec.h -- nodal values recovered from integration-point values (a .dat),
   and Abaqus' averaging rule with a threshold. Headless.

   1. cv_nr_matrix: one element's point values -> its nodes, a linear map.
      Corner nodes: the least-squares fit of the lower-order field through the
      points (hex trilinear, tet linear, wedge linear in the plane x in the
      thickness, quad bilinear, tri linear), evaluated at the corner; a single
      point (or a type / point count without a rule) gives the constant.
      Mid-side nodes: the mean of their two corners, as CalculiX does.
      Linear, so apply it to the components and take Mises etc. at the node.
   2. cv_nr_average: per node, the values of the elements around it; the
      relative variation (max - min) / (range of the scalar over the shown
      model) at or below the threshold: all take the node's mean, above: each
      keeps its own. The scalar is computed per element-node BEFORE averaging
      (averaging components and then taking Mises would be a different thing). */
#ifndef CV_NODALREC_H
#define CV_NODALREC_H

#include "base.h"

enum { CV_NR_NN = 20, CV_NR_NIP = 64 };

/* M[nn * nip], row-major, rows in the element's .frd connectivity order (as
   conn[] lists them); false: no rule for this type / point count, M is the
   constant (the mean of the points). */
bool cv_nr_matrix(int frd_type, int nn, int nip, float* M);

/* node -> the connectivity slots (positions in conn[]) around it */
typedef struct {
    uint32_t  n_nodes, n_slots;
    uint32_t* start;                    /* n_nodes + 2 */
    uint32_t* slot;                     /* n_slots */
    uint32_t* elem;                     /* element of every slot */
} cv_nr_topo;

bool cv_nr_topo_build(cv_nr_topo* t, uint32_t n_nodes, uint32_t n_elems, const uint32_t* eoff, const uint32_t* conn);
void cv_nr_topo_free(cv_nr_topo* t);

/* v: a value per slot (NaN: none), vis: per element, NULL all shown; mat: per
   element, NULL = average across materials. pct 0 never averages, 100 always.
   out: per slot, NaN for hidden elements. Returns the range used. */
double cv_nr_average(const cv_nr_topo* t, const float* v, const uint8_t* vis, const uint32_t* mat,
                     float pct, float* out);

#endif
