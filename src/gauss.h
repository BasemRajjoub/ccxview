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

/* Their derivatives dN[i][a] = dN_i / dxi_a at xi (a = 2 is 0 for the surface kinds).
   Central differences, exact: the shape functions are at most quadratic in each
   natural coordinate. */
bool cv_shape_d(int frd_type, int nn, const double xi[3], double (*dN)[3]);

/* A rule for integrating over a solid element's natural domain (FRD types 1..6),
   exact for polynomials up to degree 5: hex 3x3x3 Gauss, tet 15 points (Keast),
   wedge 7 triangle points (Dunavant) x 3 Gauss. Up to CV_RULE_MAX points into xi,
   weights into w (summing to 8, 1/6, 1); returns how many, 0 for another type. */
enum { CV_RULE_MAX = 27 };
int  cv_solid_rule(int frd_type, double xi[][3], double* w);
/* The same over a face: the square [-1,1]^2 (3x3 Gauss, weights sum 4) or the
   triangle a, b >= 0, a + b <= 1 (7 points, weights sum 1/2); ab[k] = (a, b). */
int  cv_face_rule(bool tri, double ab[][2], double* w);

/* Natural coordinates of node `k` -- used by the tests (N_i(node_j) = delta_ij). */
bool cv_node_param(int frd_type, int nn, int k, double xi[3]);

/* The .frd lists quadratic mid-edge nodes in a different order than the input
   deck: a 20-node hex has its vertical mid-edges at 13-16 and the top ones at
   17-20 (the deck: top 13-16, vertical 17-20); a 15-node wedge likewise has
   vertical 10-12 and top 13-15. Returns the .frd position of CalculiX node i. */
int  cv_frd_node_pos(int frd_type, int nn, int i);

#endif
