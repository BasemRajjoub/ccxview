/* cap.h -- the filled cut of a plane through the solid elements. Headless.

   Each solid the plane crosses is split into tetrahedra over its nodes, and each
   tetrahedron gives one or two triangles. With `mid`, a quadratic element is split
   through its mid nodes, so the cut shows what every node says inside the element
   (the neutral plane of a shell one element thick): a C3D10 as eight tetrahedra, a
   C3D20 as eight bricks over its 20 nodes, the middles of its faces and its own, a
   C3D15 as eight wedges. The added points take position, displacement and value
   from the element's shape functions.

   A vertex carries the undeformed position (the second displacement part baked in),
   the displacement, and the value, all interpolated along the cut edge, so a shader
   that adds f1 x displacement puts it exactly on the plane; a hair (eps) behind it,
   so the clip does not discard it.

   Moving the plane is the common case (a slider): cv_cap_prepare projects every
   node on the plane's normal once and keeps each element's range, and cv_cap_cut
   then visits only the elements whose range holds the plane -- a scan of two
   numbers per element, whatever the size of the model. */
#ifndef CV_CAP_H
#define CV_CAP_H

#include "frd.h"

typedef struct {
    const cv_frd*  f;
    const uint8_t* vis;           /* per element, NULL: all */
    const float*   disp;          /* 3 per node, NULL: none; scaled by f1 */
    const float*   disp2;         /* 3 per node, NULL: none; scaled by f2 and baked into the position */
    float          f1, f2;
    float          n[3];          /* the plane's normal; the cut is where n . p = d */
    bool           mid;           /* quadratic elements through their mid nodes */
} cv_cap_model;

typedef struct {
    float*   s;                   /* per node: n . (deformed position) */
    float*   lo;  float* hi;      /* per element: the range of s over its nodes; lo > hi: no solid, or hidden */
    uint32_t n_nodes, n_elems;
} cv_cap_prep;

typedef struct { cv_fvec pos, disp, val; } cv_cap_out;   /* 3, 3 (when the model has disp), 1 per vertex */

bool cv_cap_prepare(cv_cap_prep* p, const cv_cap_model* m);      /* false: out of memory */
void cv_cap_prep_free(cv_cap_prep* p);
/* the cut at n . p = d. node_val (per node) or elem_val (per element) colour it;
   both NULL: 0. Appends to out. */
void cv_cap_cut(const cv_cap_model* m, const cv_cap_prep* p, float d, float eps,
                const float* node_val, const float* elem_val, cv_cap_out* out);
void cv_cap_out_free(cv_cap_out* o);

/* The plane that cuts away what lies between an eye and `depth` ahead of it along
   the view direction fwd (need not be unit): n . p > d is the part to discard. */
void cv_cap_eye_plane(const float eye[3], const float fwd[3], float depth, float n[3], float* d);

/* The cells, for the tests: the tetrahedra a solid of .frd `type` is cut as (indices
   into its points; the count is returned, at most `max` are written), and its points
   beyond the nodes filled in from q[0 .. nodes) (w numbers per point) by the shape
   functions; returns the number of points. */
int cv_cap_cells(int type, bool mid, int tets[][4], int max);
int cv_cap_points(int type, bool mid, float* q, int w);

#endif
