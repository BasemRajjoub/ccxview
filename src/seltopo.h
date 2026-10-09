/* seltopo.h -- selection by the mesh's shape: one layer of neighbours more or
   less, the part an element belongs to, the boundary of a set of elements, the
   skin faces up to the feature edges around one face, a chain of feature edges,
   and whether a point lies in a polygon (the lasso). Elements touch through
   shared nodes; only shown elements count (vis, NULL: every one). Lists are as
   in selset.h: sorted, each once, malloc'd, NULL when empty. Headless. */
#ifndef CV_SELTOPO_H
#define CV_SELTOPO_H

#include "mesh.h"

/* the shown elements sharing a node with el, el with them / el without the ones
   sharing a node with a shown element not in el */
uint32_t* cv_sel_grow_elems(const cv_frd* f, const uint8_t* vis, const uint32_t* el, uint32_t ne, uint32_t* n);
uint32_t* cv_sel_shrink_elems(const cv_frd* f, const uint8_t* vis, const uint32_t* el, uint32_t ne, uint32_t* n);
/* nodes: with every node of a shown element holding one of nd / without the ones
   on a shown element that has a node not in nd */
uint32_t* cv_sel_grow_nodes(const cv_frd* f, const uint8_t* vis, const uint32_t* nd, uint32_t nn, uint32_t* n);
uint32_t* cv_sel_shrink_nodes(const cv_frd* f, const uint8_t* vis, const uint32_t* nd, uint32_t nn, uint32_t* n);

/* the shown elements connected to element e through shared nodes */
uint32_t* cv_sel_part(const cv_frd* f, const uint8_t* vis, uint32_t e, uint32_t* n);

/* The boundary of the elements el: a solid's face no other element of el shares,
   a shell's edge no other shell of el shares (a beam has none). Its nodes
   (corners and mid-side nodes) to *nodes, the elements that have such a face to
   *elems. false when out of memory. */
bool cv_sel_boundary(const cv_frd* f, const uint32_t* el, uint32_t ne,
                     uint32_t** nodes, uint32_t* nn, uint32_t** elems, uint32_t* nel);

/* the skin face (index into s->face) triangle t of the skin was cut from, UINT32_MAX none */
uint32_t cv_skin_face_of_tri(const cv_frd* f, const cv_skin* s, size_t t);
/* The skin faces reached from face k without crossing an edge where the faces
   meet at more than crease_deg (normals of the undeformed corners; a shell face
   either way round), differ in material or element type, or where one, three
   or more faces meet. Indices into s->face. */
uint32_t* cv_sel_face_flood(const cv_frd* f, const cv_skin* s, uint32_t k, float crease_deg, uint32_t* n);

/* The nodes of the chain of feature edges (s->fedge) through edge k: on through
   every node where exactly two feature edges meet, turning by at most crease_deg
   (undeformed); a corner ends it. */
uint32_t* cv_sel_edge_chain(const cv_frd* f, const cv_skin* s, size_t k, float crease_deg, uint32_t* n);

/* (x, y) inside the polygon xy[2 * n] (even-odd rule) */
bool cv_point_in_poly(float x, float y, const float* xy, int n);

#endif
