/* ui.h -- Nuklear panels. Works in framebuffer pixels and scales itself by the
   desktop scale (sokol's dpi scale, CCXVIEW_SCALE, GDK_SCALE) x a user zoom. */
#ifndef CV_UI_H
#define CV_UI_H

#include <stdbool.h>

struct nk_context;

void  ui_init(void);
void  ui_frame(struct nk_context* ctx, int fb_w, int fb_h);  /* also sets G.vp_* */
float ui_scale(void);
void  ui_zoom(int dir);      /* +1 bigger, -1 smaller, 0 reset */
float ui_get_zoom(void);
void  ui_set_zoom(float z);
const char* ui_get_theme(void);          /* theme by name, as saved in the settings */
void  ui_set_theme(const char* name);    /* unknown names are ignored */
void  ui_focus_open(void);   /* Ctrl+L: cursor into the path box */
/* a panel under the mouse keeps the event from the 3D view (legend and hint
   never do; the axes gizmo lets the wheel through) */
bool  ui_mouse_captured(struct nk_context* ctx, bool wheel);
/* a text or number field is being edited: keys are typing, not shortcuts */
bool  ui_text_focus(struct nk_context* ctx);
void  ui_wheel_focus(struct nk_context* ctx);   /* wheel scrolls the panel under it */

#endif
