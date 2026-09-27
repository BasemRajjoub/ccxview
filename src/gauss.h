/* gauss.h -- integration points of CalculiX solid elements: where they are, and
   how they map to space.

   Point tables and their ORDER are CalculiX's own (src/gauss.f): the .dat
   numbers points in that order, so position k of a table is point k+1.
   Shape functions follow CalculiX node numbering (C3D8/20, C3D4/10, C3D6/15).

   Which table an element uses is decided by its FRD type and how many points
   the .dat printed for it: hex 1/8/27, tet 1/4/15, wedge 1/2/6/9/18,
   quad 1/4/9, tri 1/3/7 (surface elements: the points lie on the face). */
#ifndef CV_GAUSS_H
#define CV_GAUSS_H

#include "base.h"

/* Natural coordinates of point `ip` (0-based). false if the combination is unknown. */
bool cv_ip_param(int frd_type, int nip, int ip, double xi[3]);

/* Shape functions of an element with `nn` nodes (8/20, 4/10, 6/15, 4/8, 3/6) at xi. */
bool cv_shape(int frd_type, int nn, const double xi[3], double* N);

/* Natural coordinates of node `k` -- used by the tests (N_i(node_j) = delta_ij). */
bool cv_node_param(int frd_type, int nn, int k, double xi[3]);

/* The .frd lists quadratic mid-edge nodes in a different order than the input
   deck: a 20-node hex has its vertical mid-edges at 13-16 and the top ones at
   17-20 (the deck: top 13-16, vertical 17-20); a 15-node wedge likewise has
   vertical 10-12 and top 13-15. Returns the .frd position of CalculiX node i. */
int  cv_frd_node_pos(int frd_type, int nn, int i);

#endif
