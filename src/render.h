/* render.h -- GPU side. One shader, three pipelines (faces, edges, points) over
   shared per-node vertex buffers. Only index buffers change with visibility;
   colour, range, banding and deformation are uniforms. */
#ifndef CV_RENDER_H
#define CV_RENDER_H

#include "label.h"
#include "base.h"

enum { CV_CMAP_FAST, CV_CMAP_COOLWARM, CV_CMAP_VIRIDIS, CV_CMAP_TURBO, CV_CMAP_HEAT, CV_CMAP_RAINBOW, CV_CMAP_JET, CV_CMAP_INFERNO,
       CV_CMAP_RAINBOW_DESAT, CV_CMAP_CIVIDIS, CV_CMAP_PLASMA, CV_CMAP_BLACKBODY, CV_CMAP_KINDLMANN, CV_CMAP_WARM, CV_CMAP_COOL, CV_CMAP_N };
extern const char* const cv_cmap_names[CV_CMAP_N];
void cv_colormap_rgb(int cmap, float t, float rgb[3]);
/* with the range locked: values above the max light grey, below the min darker grey
   (as in Abaqus; the darker one still reads on the dark background).
   Keep in step with the shader in render.c. */
#define CV_OOR_ABOVE 0.85f
#define CV_OOR_BELOW 0.48f

enum { CV_COLOR_SOLID = 0, CV_COLOR_NODAL = 1, CV_COLOR_ELEM = 2, CV_COLOR_GROUP = 3 };
/* the colours of ties and contact (app_contact.c), shared with the key over the view:
   master surface, slave surface, a tied slave node, one not tied, a slave node in
   contact, the line from it to its master face */
enum { CV_KEY_MASTER, CV_KEY_SLAVE, CV_KEY_TIED, CV_KEY_FREE, CV_KEY_CSLAVE, CV_KEY_LINK, CV_KEY_N };
extern const float cv_key_rgb[CV_KEY_N][3];
enum { CV_MESH_N = 16 };     /* imported geometry: layers (cv_render_mesh) */

typedef struct {
    float mvp[16], mv[16];
    float proj[16];            /* projection alone: points need it to place ball depths */
    float diag;                /* model size: the depth pull of edges is a fraction of it */
    float def_scale;           /* 0 = undeformed */
    float def_scale2;          /* on the second displacement (imaginary part of a harmonic response) */
    float rmin, rmax;
    int   bands;               /* 0 = smooth */
    float oor[2][4];           /* a locked range: values above [0] and below [1] it: rgb and a mode, 0 the map's end colour, 1 the rgb, 3 hidden */
    bool  faces, edges, points;
    bool  outline;             /* the feature edges: never hidden with the dense mesh */
    bool  shade;               /* light + shadow on faces; off = exact colours */
    int   faces_color, edges_color, points_color;   /* CV_COLOR_* */
    float point_size;
    float face_rgb[3], edge_rgb[3], point_rgb[3];
    bool  gauss_points;
    bool  gauss_on_top;        /* drawn through the faces (they sit inside elements) */
    int   gauss_points_color;  /* CV_COLOR_SOLID or CV_COLOR_NODAL */
    float gauss_size, gauss_rgb[3];
    bool  highlights;          /* deck node sets (balls) and surfaces (faces); ties and contact pairs */
    bool  contact;             /* the contact elements of a .cel (CV_AUX_C*) */
    bool  contact_front;       /* ... their slave nodes and lines in front of everything */
    bool  mirrored;            /* this instance is a mirror image: its triangles turn the other way */
    float hl_size;
    bool  geo_points, geo_curves, geo_surfaces;   /* cgx geometry */
    bool  supports, loads;     /* deck *BOUNDARY glyphs and *CLOAD / *DLOAD arrows */
    bool  discrete;            /* springs, dashpots, masses, gaps as symbols */
    bool  links;               /* coupling spiders: rigid body, kinematic, distributing, equation */
    bool  vectors;             /* arrows of a 3-component field at the nodes */
    bool  tensors;             /* glyphs of a stress / strain tensor (cv_render_glyphs, CV_INST_TENS / COMP) */
    int   tensors_color;       /* the glyphs: CV_COLOR_SOLID or CV_COLOR_NODAL */
    bool  glyph_signed;        /* the glyphs are coloured by the normal stress in each direction */
    bool  traj;                /* principal stress trajectories (CV_INST_TRAJ1 / TRAJ3) */
    float sign_lim;            /* > 0: the cross, the trajectories and signed glyphs coloured by value on a
                                  fixed cool-warm scale -sign_lim .. +sign_lim; 0: plain red / blue / grey */
    bool  ghost;               /* the undeformed edges in grey behind the deformed shape */
    bool  markers;             /* min / max balls */
    bool  path;                /* the plotted line on the surface */
    bool  clip;                /* discard what lies beyond the plane n . x > d */
    float clip_n[3], clip_d;
    float marker_size;
    bool  labels;              /* the label layer (cv_render_labels) */
    bool  labels_on_top;       /* through the faces: names at centres inside the model */
    float label_rgb[3], label_box_rgba[4];
    float label_px;            /* how far a label reaches from its point, px: the depth pull (app_label_frame) */
    int   vectors_color;       /* CV_COLOR_SOLID or CV_COLOR_NODAL */
    float geo_size;
    int   vp_x, vp_y, vp_w, vp_h; /* viewport in framebuffer pixels, origin top-left */
    struct { bool on; float rgb[3], alpha; } mesh[CV_MESH_N];   /* imported geometry: shown, its colour, 0..1 */
    int   mesh_order[CV_MESH_N];   /* the see-through ones drawn in this order, back to front (mesh_n_order of them; 0: as listed) */
    int   mesh_n_order;
} cv_draw;

void cv_render_init(void);
void cv_render_shutdown(void);
void cv_render_clear_model(void);

/* uploads (each replaces the previous buffer) */
void cv_render_positions(const float* xyz, uint32_t n_nodes);
void cv_render_displacement(const float* disp, uint32_t n_nodes);   /* NULL = none */
void cv_render_displacement2(const float* disp, uint32_t n_nodes);  /* NULL = none */
void cv_render_scalar(const float* s, uint32_t n_nodes);           /* NULL = none */
void cv_render_tri_values(const float* v, size_t n_tri);            /* per triangle */
/* CV_COLOR_GROUP: the skin triangles re-ordered by group (3 indices each) and,
   per group, its first triangle and colour -- one solid-colour draw per group. */
void cv_render_groups(const uint32_t* tri, size_t n_tri, const uint32_t* first, const float* rgb, int ngroups);
void cv_render_indices(const uint32_t* tri, size_t n_tri, const uint32_t* edge, size_t n_edge,
                       const uint32_t* pt, size_t n_pt);
void cv_render_outline(const uint32_t* fedge, size_t n_fedge);   /* the skin's feature edges */
void cv_render_colormap(int cmap, bool reverse, bool grey);

/* Non-indexed vertex sets drawn with the same shader. NULL/0 clears.
   CV_AUX_ELEMTRI: the skin triangles with one value per triangle, expanded to
   three vertices each -- per-element colouring without gl_PrimitiveID, which
   Mesa's llvmpipe gets wrong. Built by app_field.c; when it is empty (huge
   meshes) the faces fall back to the gl_PrimitiveID texture path. */
/* Gauss points, set highlights, cgx geometry (points, curve segments as vertex
   pairs, surface triangles) */
enum { CV_AUX_GP, CV_AUX_HLPT, CV_AUX_HLTRI, CV_AUX_GEOPT, CV_AUX_GEOLN, CV_AUX_GEOTRI,
       CV_AUX_MARK, CV_AUX_PATHLN, CV_AUX_RAYLN, CV_AUX_PICKPT, CV_AUX_ELEMTRI, CV_AUX_CAPTRI,
       CV_AUX_SELLN, CV_AUX_SELTRI, CV_AUX_SELPT, CV_AUX_SELMAX, CV_AUX_SELMIN, CV_AUX_MEASLN, CV_AUX_MEASPT,
       CV_AUX_PMST, CV_AUX_PSLV, CV_AUX_PTIED, CV_AUX_PFREE, CV_AUX_CMST, CV_AUX_CMLN, CV_AUX_CSLV, CV_AUX_N };
       /* the box selection: outline (vertex pairs), faces (toned), nodes (dots), its max and min;
          the measurements: their lines (vertex pairs) and nodes;
          ties and contact pairs (app_contact.c, CV_KEY_* colours): master faces, slave faces
          (half see-through, over the master's), the tied slave nodes, those not tied (in front);
          the contact elements of a .cel: their master faces and outlines, slave nodes (their
          lines: CV_INST_CLINK, CV_INST_CSLN) */
void cv_render_aux(int which, const float* pos, const float* disp, const float* scal, uint32_t n);
/* the same with the second displacement part (harmonic: -DISPI, scaled by def_scale2) */
void cv_render_aux2(int which, const float* pos, const float* disp, const float* disp2, const float* scal, uint32_t n);

/* Symbols: supports, loads, springs, links and vector arrows. Each is made of round
   bodies between two points, a radius at either end: a tube (equal radii), a cone
   (one of them 0), a plate (a short tube). One such body is kept on the GPU and
   drawn once per instance, so a symbol costs 21 numbers whatever its shape:
     a[3], b[3], radius at a, radius at b, scalar (colour, when the layer is coloured
     by the field), disp[3], disp2[3] (how end a moves with the shape), dispb[3],
     disp2b[3] (how end b moves: the same for a rigid symbol, the far node's own for
     a line between two nodes -- a spider leg, a spring -- so it stretches with them).
   A body is never drawn thinner than about a pixel, so symbols stay visible from far
   away and a radius of nearly 0 gives a line of constant width. NULL / 0 clears. */
/* CV_INST_TENS / COMP: the bars of the principal cross in tension / compression */
/* CV_INST_TRAJ1 / TRAJ3: principal stress trajectories of S1 / S3 */
enum { CV_INST_BC, CV_INST_LD, CV_INST_MOM, CV_INST_HEAT, CV_INST_DISC, CV_INST_LINK, CV_INST_VEC,
       CV_INST_TENS, CV_INST_COMP, CV_INST_TRAJ1, CV_INST_TRAJ3, CV_INST_BOLTLD, CV_INST_BOLTBC, CV_INST_SUB,
       CV_INST_CLINK, CV_INST_CSLN, CV_INST_N };
/* BOLTLD / BOLTBC: bolt preloads (load colour) and held bolts (support colour), drawn in
   front of everything: a pretension section lies inside the bolt. SUB: what the global
   model drives in a submodel (*BOUNDARY, SUBMODEL; *DSLOAD, SUBMODEL), in a colour of its own.
   CLINK / CSLN: the contact elements' lines slave -> master and slave face outlines, a
   couple of pixels wide whatever the zoom (app_contact.c) */
#define CV_INST_FLOATS 21
void cv_render_inst(int which, const float* inst, uint32_t n);

/* Tensor glyphs (glyph.h): one unit shape drawn per instance, lit, coloured by its
   scalar like the faces or by the normal stress in each direction. CV_GLYPH_FLOATS
   per instance:
     centre[3], scalar, base x[3] * len, alpha, base y[3] * len, beta,
     base z[3] * len, cee, eigenvalue along x, y, z, kind (CV_GSHAPE_*, + 4 coloured
     by the normal stress), disp[3], disp2[3]. NULL / 0 clears. */
#define CV_GLYPH_FLOATS 26
void cv_render_glyphs(const float* inst, uint32_t n);

/* Labels (label.h): a quad per glyph or box, anchored to a model point, sized in pixels.
   The atlas is the font's alpha (R8); boxes first, then glyphs. NULL / 0 clears. */
void cv_render_label_atlas(const unsigned char* a8, int w, int h);
void cv_render_labels(const float* box, uint32_t nb, const float* gly, uint32_t ng);
void cv_render_label_depth(const cv_draw* d);   /* before the frame's pass, while labels are on: the faces' depth for their test */

void cv_render_draw(const cv_draw* d);

/* See-through faces (app_see.c): the skin triangles in runs, each drawn with one
   opacity. ids: skin triangle numbers, run after run; a solid run is drawn in its
   rgb (faces coloured by group), the others as the faces are coloured. The opaque
   runs (alpha 1) are the faces of cv_render_draw; the others are drawn by
   cv_render_see_draw over everything, blended, the far sides first and the near ones
   over them (a triangle turns counter-clockwise seen from outside its element; one
   whose id has CV_SEE_FLIP is turned around), so a glass body shows what lies
   behind it and its own far side, dimmed. Kept until the
   skin's triangles are uploaded again (cv_render_indices clears it); NULL / 0 clears:
   every face opaque. Faces coloured per element without the expanded stream (the
   gl_PrimitiveID path of huge meshes) stay opaque. */
typedef struct { uint32_t first, n; float alpha; float rgb[3]; bool solid; } cv_see_run;
#define CV_SEE_FLIP 0x80000000u
void cv_render_see(const uint32_t* ids, uint32_t n, const cv_see_run* runs, int nruns);
void cv_render_see_draw(const cv_draw* d);
bool cv_render_see_on(void);

/* Imported geometry (STL, app_stl.c): triangles that never move, lit like the faces,
   one solid colour, not on the mirror copies; with their outline (vertex pairs), drawn
   darker while d->outline is on. A slot per layer; NULL / 0 clears it. */
void cv_render_mesh(int slot, const float* xyz, uint32_t n_vert, const uint32_t* tri, uint32_t n_tri,
                    const uint32_t* edge, uint32_t n_edge);
/* the shown slots: the opaque ones (alpha 1), or with transparent those see-through,
   blended over what is drawn, back faces then front faces, leaving the depth as it is
   (not sorted: two see-through layers overlapping blend in the order they are listed) */
void cv_render_meshes(const cv_draw* d, bool transparent);

#endif
