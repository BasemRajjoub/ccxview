/* render_ao.h -- screen-space ambient occlusion for the Studio look (render.c only).

   From the faces' depth (the depth pass before the frame, cv_render_depth_*), at half
   the viewport's resolution: 12 samples on a spiral per pixel, turned by a per-pixel
   noise, each counting how far the surface near it rises above its tangent plane,
   fading with distance; then a 4 x 4 blur that keeps to pixels of about the same depth,
   so the noise goes and no dark halo crosses an outline. About 1 ms at 1080p on a
   desktop GPU whatever the element count: it works on pixels, not on the mesh. */
#ifndef CV_RENDER_AO_H
#define CV_RENDER_AO_H

#include "sokol_gfx.h"

/* the GLSL header both render files put in front of their shaders */
#ifdef __EMSCRIPTEN__
#define GLSL_HDR "#version 300 es\nprecision highp float; precision highp int; precision highp sampler2D;\n"
#else
#define GLSL_HDR "#version 410\n"
#endif

/* depth: the depth pass's texture, w x h; proj: the projection of that pass; radius:
   how far a point occludes, in view units (a fraction of the model's size) */
void    cv_ao_run(sg_view depth, int w, int h, const float proj[16], float radius);
void    cv_ao_sample(int i);  /* the noise turns by a golden-ratio step per i (Render image's accumulation); 0 = as ever */
sg_view cv_ao_view(void);     /* the result, one channel, 1 = open; {0} before the first run */
void    cv_ao_free(void);

#endif
