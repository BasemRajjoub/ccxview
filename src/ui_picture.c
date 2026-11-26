/* ui_picture.c -- the Export picture window: how the PNG of the view looks (quick or
   rendered, the render view or the plain one, the background) and what is on it (legend,
   axes, title block, tags and labels, min / max). The choices are G.pic_*, kept in the ini;
   they change the picture only. Taking it is app_picture.c. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "render_bg.h"

#define WIN "Export picture"

void uii_window_picture(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_picture) { was_open = false; return; }
    if (app_picture_busy()) return;                   /* being taken: out of the view */
    if (!was_open) nk_window_show(ctx, WIN, NK_SHOWN);
    was_open = true;
    float w = CV_MIN(460 * s, fw * 0.92f), h = CV_MIN(fh * 0.9f, 540 * s);
    if (nk_begin(ctx, WIN, nk_rect((fw - w) / 2, (fh - h) / 2, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(ctx, 2 * row, 1);
        nk_label_colored_wrap(ctx, "A PNG of the view. These choices change the picture only: "
                                   "the view is put back as it was after.", P.dim);

        static const char* const ql[2] = { "Quick (as on screen)", "Rendered" };
        uik_combo(ctx, "Quality", ql, 2, &G.pic_quality,
                  "Quick: the view as drawn now. Rendered: samples accumulated (jittered camera, occlusion and light: "
                  "smooth edges, little noise); Esc or moving the view stops it");
        if (G.pic_quality == 1) {
            static const char* const sl[3] = { "16", "64", "256" };
            static const int sv[3] = { 16, 64, 256 };
            int k = G.render_samples <= 16 ? 0 : G.render_samples <= 64 ? 1 : 2;
            if (uik_combo(ctx, "Samples", sl, 3, &k, "Samples per picture: more is smoother and slower")) G.render_samples = sv[k];
        }
        uii_hsep(ctx, s);

        static const char* const ll[3] = { "as shown", "render view", "plain view" };
        uik_combo(ctx, "Look", ll, 3, &G.pic_look,
                  "As shown: the view's look. Render view: the studio light with the floor, shadows, occlusion and backdrop "
                  "below. Plain view: flat or headlight light, the background colour (key V switches the view's own)");
        if (G.pic_look == CV_PIC_LOOK_RENDER) {
            uik_check2(ctx, "Floor", &G.pic_floor, "A floor under the model, fading out to the backdrop, with a soft shadow",
                       "Shadows", &G.pic_shadow, "The key light's shadows");
            uik_check(ctx, "Ambient occlusion", &G.pic_ao, "Darkens creases and corners");
            uik_combo(ctx, "Backdrop", (const char* const*)cv_bg_names, CV_BG_N, &G.pic_backdrop,
                      "The picture's backdrop: plain (the background colour), the gradients of Abaqus or ANSYS, a studio vignette, or white");
        }
        static const char* const bl[3] = { "as shown", "transparent", "white" };
        uik_combo(ctx, "Background", bl, 3, &G.pic_bg,
                  "As shown: the backdrop of the look. Transparent: only the model and the overlays on the page (no backdrop, "
                  "no floor). White: a white page");
        if (G.pic_bg == CV_PIC_BG_ALPHA && G.pic_quality == 1) uik_note(ctx, "a rendered picture is opaque: transparent is Quick's");
        uii_hsep(ctx, s);

        nk_layout_row_dynamic(ctx, row, 1);
        nk_label(ctx, "Include", NK_TEXT_LEFT);
        uik_check2(ctx, "Legend", &G.pic_legend, "The colour legend or group key, when the view shows it",
                   "Axes", &G.pic_axes, "The axes gizmo, when the view shows it");
        uik_check2(ctx, "Title block", &G.pic_title, "The title block, when the view shows it",
                   "Min / max", &G.pic_markers, "The balls at the field's minimum and maximum, when the view shows them");
        uik_check(ctx, "Probe tag and labels", &G.pic_tags, "The probe's tag, the labels and the measurements, when the view shows them");
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "what the view shows; untick to leave it out", NK_TEXT_LEFT, P.dim);
        uii_hsep(ctx, s);

        static const char* const cl[1] = { "Save picture..." };
        static const char* const ct[1] = { "Take the picture, then the Save dialog: <model>_stepN.png beside the model offered (Ctrl+E saves with these settings)" };
        if (uik_buttons(ctx, NULL, 1, cl, ct) == 0) app_export_png();
    }
    if (nk_window_is_hidden(ctx, WIN)) G.show_picture = false;
    nk_end(ctx);
}
