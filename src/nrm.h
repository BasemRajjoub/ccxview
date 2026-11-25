/* nrm.h -- smooth normals of the skin at its nodes, for the Studio look (headless).

   Each triangle adds its normal, weighted by its area, to its three corners, on the
   shape as drawn (xyz + scale * disp). Sharp edges need no split vertices: a node on
   one (a triangle at it more than CV_NRM_CREASE degrees off its mean normal) gets 0,
   as does a node no triangle touches or one whose normals cancel (both sides of a
   shell on the same nodes); the shader then uses each triangle's own normal, so the
   faces beside a sharp edge stay flat up to it instead of bending round it. */
#define CV_NRM_CREASE 40.0
#ifndef CV_NRM_H
#define CV_NRM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* out: 4 signed bytes per node (x y z, 0; 127 = 1). disp may be NULL. false: out of memory */
bool cv_nrm_skin(const float* xyz, const float* disp, float scale, uint32_t n_nodes,
                 const uint32_t* tri, size_t n_tri, int8_t* out);

#endif
