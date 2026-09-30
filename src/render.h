/* render.h -- GPU side. One shader, three pipelines (faces, edges, points) over
   shared per-node vertex buffers. Only index buffers change with visibility;
   colour, range, banding and deformation are uniforms. */
#ifndef CV_RENDER_H
#define CV_RENDER_H

#include "base.h"

enum { CV_CMAP_FAST, CV_CMAP_COOLWARM, CV_CMAP_VIRIDIS, CV_CMAP_TURBO, CV_CMAP_HEAT, CV_CMAP_RAINBOW, CV_CMAP_JET, CV_CMAP_INFERNO,
       CV_CMAP_RAINBOW_DESAT, CV_CMAP_N };
extern const char* const cv_cmap_names[CV_CMAP_N];
void cv_colormap_rgb(int cmap, float t, float rgb[3]);
/* with the range locked: values above the max light grey, below the min darker grey
   (as in Abaqus; the darker one still reads on the dark background).
   Keep in step with the shader in render.c. */
#define CV_OOR_ABOVE 0.85f
#define CV_OOR_BELOW 0.48f

enum { CV_COLOR_SOLID = 0, CV_COLOR_NODAL = 1, CV_COLOR_ELEM = 2, CV_COLOR_GROUP = 3 };

typedef struct {
    float mvp[16], mv[16];
    float proj[16];            /* projection alone: points need it to place ball depths */
    float def_scale;           /* 0 = undeformed */
    float def_scale2;          /* on the second displacement (imaginary part of a harmonic response) */
    float rmin, rmax;
    int   bands;               /* 0 = smooth */
    bool  grey_out_of_range;   /* only while the range is locked */
    bool  faces, edges, points;
    bool  shade;               /* light + shadow on faces; off = exact colours */
    int   faces_color, edges_color, points_color;   /* CV_COLOR_* */
    float point_size;
    float face_rgb[3], edge_rgb[3], point_rgb[3];
    bool  gauss_points;
    bool  gauss_on_top;        /* drawn through the faces (they sit inside elements) */
    int   gauss_points_color;  /* CV_COLOR_SOLID or CV_COLOR_NODAL */
    float gauss_size, gauss_rgb[3];
    bool  highlights;          /* deck node sets (balls) and surfaces (faces) */
    float hl_size;
    bool  geo_points, geo_curves, geo_surfaces;   /* cgx geometry */
    bool  supports, loads;     /* deck *BOUNDARY glyphs and *CLOAD / *DLOAD arrows */
    bool  discrete;            /* springs, dashpots, masses, gaps as symbols */
    bool  links;               /* coupling spiders: rigid body, kinematic, distributing, equation */
    bool  vectors;             /* arrows of a 3-component field at the nodes */
    bool  ghost;               /* the undeformed edges in grey behind the deformed shape */
    bool  markers;             /* min / max balls */
    bool  path;                /* the plotted line on the surface */
    bool  clip;                /* discard what lies beyond the plane n . x > d */
    float clip_n[3], clip_d;
    float marker_size;
    int   vectors_color;       /* CV_COLOR_SOLID or CV_COLOR_NODAL */
    float geo_size;
    int   vp_x, vp_y, vp_w, vp_h; /* viewport in framebuffer pixels, origin top-left */
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
void cv_render_colormap(int cmap, bool reverse, bool grey);

/* Non-indexed vertex sets drawn with the same shader. NULL/0 clears.
   CV_AUX_ELEMTRI: the skin triangles with one value per triangle, expanded to
   three vertices each -- per-element colouring without gl_PrimitiveID, which
   Mesa's llvmpipe gets wrong. Built by app_field.c; when it is empty (huge
   meshes) the faces fall back to the gl_PrimitiveID texture path. */
/* Gauss points, set highlights, cgx geometry (points, curve segments as vertex
   pairs, surface triangles) */
enum { CV_AUX_GP, CV_AUX_HLPT, CV_AUX_HLTRI, CV_AUX_GEOPT, CV_AUX_GEOLN, CV_AUX_GEOTRI,
       CV_AUX_BCLN, CV_AUX_LDLN, CV_AUX_DISCLN, CV_AUX_LINKLN, CV_AUX_VECLN, CV_AUX_MARK, CV_AUX_PATHLN, CV_AUX_PICKPT, CV_AUX_ELEMTRI, CV_AUX_CAPTRI, CV_AUX_N };   /* BCLN: support glyphs, LDLN: load arrows,
                                                             DISCLN: springs / dashpots / masses (line pairs) */
void cv_render_aux(int which, const float* pos, const float* disp, const float* scal, uint32_t n);
/* the same with the second displacement part (harmonic: -DISPI, scaled by def_scale2) */
void cv_render_aux2(int which, const float* pos, const float* disp, const float* disp2, const float* scal, uint32_t n);

void cv_render_draw(const cv_draw* d);

#endif
