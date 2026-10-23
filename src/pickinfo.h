/* pickinfo.h -- what the Inspector shows of a picked face or edge of an element:
   the face's ring of nodes, a polygon's area and normal, an element's edges.
   Headless (frd.h, mesh.h). */
#ifndef CV_PICKINFO_H
#define CV_PICKINFO_H

#include "frd.h"

/* The nodes round face `face` of element e, in order: corner, the mid-side node
   after it (a quadratic face), next corner ... 3, 4, 6 or 8 of them; 0 for none. */
int cv_face_ring(const cv_frd* f, uint32_t e, int face, uint32_t out[8]);

/* Area and unit normal (Newell) of the polygon through n points (xyz each); a
   ring through mid-side nodes follows a bent face. nrm may be NULL. */
float cv_poly_area(const float* p, int n, float nrm[3]);

/* The edges of element e, each { corner, corner, mid-side node or UINT32_MAX },
   once each, from its faces (a line element: its ends); at most max; their count. */
int cv_elem_edges(const cv_frd* f, uint32_t e, uint32_t out[][3], int max);

#endif
