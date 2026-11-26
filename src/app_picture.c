/* app_picture.c -- "Export picture": the one way a still of the view is saved (Ctrl+E, the
   Export picture window, the Render tab's and the top bar's Render image). G.pic_* say how
   it looks: quick (the view as drawn) or rendered (app_render.c's accumulation of
   G.render_samples), the render view or the plain one with the floor, shadows, occlusion
   and backdrop it has, which overlays are in it, and its background. They change the
   picture only: as the Report builder does, the view is set for a few frames with the
   window out of the way, read back (export_now: the Save dialog, the name), and put back.
   --shot and --render do not come here. */
#include "app_int.h"
#include "render_bg.h"

#define WARM 3          /* frames drawn with the picture's settings before it is read back */

void export_now(void);                       /* app.c: the read-back, the Save dialog */

static struct {
    bool  on, rendering;
    int   warm;
    bool  look;                              /* render_view flipped: the look rebuilt both ways */
    /* the view's own settings, put back after */
    bool  render_view, rv_forced, floor, shadow, ao, legend, axes, title, markers, tag;
    int   bg_mode;
    float bg[3];
} S;

bool app_picture_busy(void) { return S.on; }
bool app_picture_alpha(void) { return S.on && G.pic_bg == CV_PIC_BG_ALPHA && G.pic_quality == 0; }   /* rendered: opaque */
bool app_picture_labels_off(void) { return S.on && !G.pic_tags; }

static void apply(void) {
    S.render_view = G.render_view; S.rv_forced = G.render_view_forced;
    S.floor = G.look_floor; S.shadow = G.look_shadow; S.ao = G.look_ao;
    S.legend = G.hide_legend; S.axes = G.hide_axes; S.title = G.title_on; S.markers = G.show_markers; S.tag = G.probe_tag;
    S.bg_mode = G.bg_mode; memcpy(S.bg, G.bg, sizeof S.bg);
    if (G.pic_look) {
        bool rv = G.pic_look == CV_PIC_LOOK_RENDER;
        S.look = rv != G.render_view;
        G.render_view = rv;
        if (rv) { G.look_floor = G.pic_floor; G.look_shadow = G.pic_shadow; G.look_ao = G.pic_ao; G.bg_mode = G.pic_backdrop; }
    } else S.look = false;
    if (G.pic_bg == CV_PIC_BG_WHITE) { G.bg_mode = CV_BG_WHITE; G.bg[0] = G.bg[1] = G.bg[2] = 1.f; }
    if (!G.pic_legend) G.hide_legend = true;          /* what is left out; nothing hidden is added */
    if (!G.pic_axes) G.hide_axes = true;
    if (!G.pic_title) G.title_on = false;
    if (!G.pic_markers) G.show_markers = false;
    if (!G.pic_tags) G.probe_tag = false;
    if (S.look) app_look_changed();
}

static void restore(void) {
    G.render_view = S.render_view; G.render_view_forced = S.rv_forced;
    G.look_floor = S.floor; G.look_shadow = S.shadow; G.look_ao = S.ao;
    G.hide_legend = S.legend; G.hide_axes = S.axes; G.title_on = S.title; G.show_markers = S.markers; G.probe_tag = S.tag;
    G.bg_mode = S.bg_mode; memcpy(G.bg, S.bg, sizeof G.bg);
    if (S.look) app_look_changed();
    S.on = S.rendering = false;
    app_wake();
}

/* Ctrl+E, Save picture: the picture with the settings as they are */
void app_export_png(void) {
    if (!G.loaded || S.on || app_render_active() || app_report_busy() || G.seq_left > 0 || G.video) return;
    apply();
    S.on = true; S.rendering = false; S.warm = WARM;
    app_wake();
}

void app_picture_window(bool rendered) {
    if (rendered) G.pic_quality = 1;
    G.show_picture = true;
}

/* each frame, after drawing: the warm frames, then the read-back (quick) or the samples and
   the read-back of their average with the overlays drawn over it (rendered) */
void app_picture_tick(void) {
    if (!S.on) return;
    if (!G.loaded) { restore(); return; }                 /* the model went away */
    if (app_busy()) return;
    if (S.warm > 0) { S.warm--; return; }
    if (G.pic_quality == 0) { export_now(); restore(); return; }
    if (!S.rendering) { S.rendering = true; app_render_still(G.render_samples); return; }
    if (app_render_finished()) { export_now(); restore(); return; }
    if (!app_render_active()) restore();                  /* stopped: Esc, a camera move */
}
