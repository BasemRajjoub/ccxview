/* mesh.h -- what gets drawn: automatic groups, visibility, exterior skin, edges,
   points, and ray picking. Headless (no GPU). */
#ifndef CV_MESH_H
#define CV_MESH_H

#include "frd.h"

/* ---- groups ----------------------------------------------------------------
   Three automatic grouping axes. An element is visible iff its value is ticked
   on every axis (AND across axes, OR within one). Values never overlap inside an
   axis, so ticking and unticking always does what it says. */
enum { CV_AXIS_TYPE, CV_AXIS_MAT, CV_AXIS_GRP, CV_AXIS_N };

typedef struct {
    int        n;          /* distinct values, sorted ascending */
    uint32_t*  value;      /* raw value: FRD type code / material no. / group no. */
    uint32_t*  count;      /* elements carrying it */
    bool*      on;
    uint16_t*  of_elem;    /* per element: index into value[] */
} cv_axis;

typedef struct { cv_axis axis[CV_AXIS_N]; } cv_groups;

bool cv_groups_build(cv_groups* g, const cv_frd* f);           /* false on OOM */
void cv_groups_free(cv_groups* g);
void cv_groups_mask(const cv_groups* g, uint32_t n_elems, uint8_t* vis);  /* 1 = shown */
const char* cv_axis_name(int axis);

/* Crop box: clear vis[e] for every element whose centroid (undeformed) lies
   outside [lo, hi]. ANDs with what is already in vis. */
void cv_crop_mask(const cv_frd* f, const float lo[3], const float hi[3], uint8_t* vis);

/* Plane: beyond[i] = 1 for every node whose shown place (xyz + f1 disp + f2 disp2;
   disp, disp2 3 per node or NULL) has n . p > d. Then cv_node_mask clears vis[e]
   for every element with such a node (ANDs with vis). */
void cv_plane_nodes(const cv_frd* f, const float* disp, float f1, const float* disp2, float f2,
                    const float n[3], float d, uint8_t* beyond);
void cv_node_mask(const cv_frd* f, const uint8_t* beyond, uint8_t* vis);

/* Box selection, CAD style: from inside[i] (node i in the box), the shown elements
   (vis NULL: all) with every node inside (window) or any (crossing), appended to
   out as element indices; returns how many. */
uint32_t cv_box_elems(const cv_frd* f, const uint8_t* vis, const uint8_t* inside, bool crossing, uint32_t* out);

/* ---- skin ------------------------------------------------------------------
   Exterior faces of the visible solids (+ every visible shell face) as
   triangles over their nodes (cv_skin_build_opt), the unique edges of those faces (+ beams), and
   the nodes of visible elements. Indices are dense node indices.
   Feature edges are the part's outline, which stays readable on a mesh too
   fine for its element edges: skin edges used by one face only (shell borders,
   open meshes), by three or more, or by two faces that differ in material or
   element type or meet at more than the crease angle; and every beam. The
   angle is taken on the undeformed quads and triangles (not the split
   triangles, so a flat quad has no crease along its diagonal); a shell has no
   inside, so its faces count as parallel whichever way their normals point. */
typedef struct {
    uint32_t* tri;       size_t n_tri;    /* 3 per triangle */
    uint32_t* tri_elem;                   /* element of each triangle */
    uint32_t* edge;      size_t n_edge;   /* 2 per edge */
    uint32_t* pt;        size_t n_pt;
    uint32_t* face;      size_t n_face;   /* exterior faces as element << 3 | local face */
    uint32_t* fedge;     size_t n_fedge;  /* feature edges, 2 per edge */
} cv_skin;

#define CV_CREASE_DEG 30.f

bool cv_skin_build(cv_skin* s, const cv_frd* f, const uint8_t* vis);   /* vis may be NULL; CV_CREASE_DEG */
bool cv_skin_build_crease(cv_skin* s, const cv_frd* f, const uint8_t* vis, float crease_deg);
/* mid: the faces of quadratic elements (C3D20, C3D15, C3D10, S8, S6, ...) through
   their mid-side nodes -- a quad as six triangles, a triangle as four, an edge as
   two pieces -- so colours and shape follow every node. Without it corners only:
   a third of the triangles, but a value or a bend at a mid node does not show. */
bool cv_skin_build_opt(cv_skin* s, const cv_frd* f, const uint8_t* vis, float crease_deg, bool mid);

/* Corner nodes (dense indices) of face `face` (0-based, CalculiX order: S1 -> 0)
   of element e. Returns 3 or 4, 0 if the element has no such face. Shells have
   one face, whatever S-number the deck uses. */
int  cv_elem_face_corners(const cv_frd* f, uint32_t e, int face, uint32_t out[4]);
/* Every node of that face: its corners, then (a quadratic element) the mid-side
   nodes, the one between corners 0 and 1 first. Returns 3, 4, 6 or 8; 0 for none. */
int  cv_elem_face_nodes(const cv_frd* f, uint32_t e, int face, uint32_t out[8]);
int  cv_elem_nfaces(const cv_frd* f, uint32_t e);     /* 0 for a beam; 1 for a shell */
void cv_skin_free(cv_skin* s);

/* ---- picking ------------------------------------------------------------ */
typedef struct {
    bool     hit;
    uint32_t elem;      /* element index */
    uint32_t node;      /* nearest node of that element to the hit point */
    float    t;         /* ray parameter */
    uint32_t tri;       /* the skin triangle hit */
} cv_pick;

/* Deformed position = xyz + disp * scale (disp may be NULL). */
cv_pick cv_pick_ray(const cv_frd* f, const cv_skin* s, const float* disp, float scale,
                    const float org[3], const float dir[3]);

#endif
