/* render_bg.h -- the backdrop of the 3D view: plain colour, gradients, a studio vignette.
   A fullscreen triangle drawn first in the main pass inside the viewport, no depth. */
#ifndef CV_RENDER_BG_H
#define CV_RENDER_BG_H

#include "sokol_gfx.h"

enum { CV_BG_PLAIN, CV_BG_ABAQUS, CV_BG_ANSYS, CV_BG_STUDIO, CV_BG_WHITE, CV_BG_N };
extern const char* const cv_bg_names[CV_BG_N];

/* the colour at the top and at the bottom of the view (plain: the same, `plain`), as sRGB;
   mean: the colour the text on the view is read against */
void cv_bg_colors(int mode, const float plain[3], float top[3], float bot[3], float mean[3]);

/* mode, the plain colour, the viewport in framebuffer pixels (origin top-left) and the
   framebuffer's height. Plain: nothing drawn (the clear colour is the backdrop). */
void cv_bg_draw(int mode, const float plain[3], int vp_x, int vp_y, int vp_w, int vp_h, int fb_h);
void cv_bg_free(void);

/* The floor under the model, Studio only: a horizontal square of the backdrop's colour,
   a touch lighter on a dark one (darker on a light one), fading out radially to the
   backdrop so it has no edge, carrying the contact shadow (render_shadow.h) as darkening.
   Drawn after the backdrop, before the model, no depth. Not seen from below. */
typedef struct {
    float mvp[16], mv[16];     /* model -> clip, model -> view */
    int   up;                  /* the model axis that points up: 1 y, 2 z */
    float c[3];                /* the centre (its up component unused) */
    float level, half;         /* the floor's height and half its side */
    float hc;                  /* half the side of the contact map, centred alike */
    float zfar, zfade;         /* the far plane's distance, and how far before it the floor fades out */
    float strength;            /* of the contact shadow, 0 .. 1 */
    int   bg_mode;  float bg_plain[3];
    int   vp_x, vp_y, vp_w, vp_h, fb_h;
    sg_view contact;  sg_sampler smp;
    /* the key light's shadow on it (render_shadow.h): the up axis in view space, the map and its
       sampler, view -> light clip, kinfo = u_sh (strength, texel, jitter x y), 1 / size */
    float up_view[3];
    sg_view kmap;  sg_sampler kcmp;
    float klm[16], kinfo[4], kinv;
} cv_floor_draw;
void cv_floor_draw_now(const cv_floor_draw* f);

#endif
