/* ui_measure.c -- the Measurements window: the list with both values of each
   (undeformed and deformed), new ones armed from here, what their labels show,
   copy, CSV, delete; and the row of measure buttons the probe and the context
   menu share. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"

int ui_measure_row(struct nk_context* ctx, float s, float row, const char* mark) {
    static const char* tips[CV_MEAS_N] = {
        "The distance from this node to the next one you click, with its dx dy dz:\n"
        "undeformed and deformed (the displacement at true scale)",
        "The angle at a vertex: this node, then the vertex, then the third node",
        "The circle through this node and the next two you click: its radius and centre\n"
        "(three nodes on a hole give the hole's radius)",
    };
    int chosen = -1;
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 58 * s);
    for (int k = 0; k < CV_MEAS_N; k++) nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, "measure", NK_TEXT_LEFT);
    for (int k = 0; k < CV_MEAS_N; k++) {
        char mk[64];
        snprintf(mk, sizeof mk, "%s %s", mark, app_measure_kind_name(k));
        uii_test_mark(ctx, mk);
        tip(ctx, tips[k]);
        bool armed = app_measure_armed() == k + 1;
        if (armed) nk_style_push_color(ctx, &ctx->style.button.text_normal, P.accent);
        if (nk_button_label(ctx, app_measure_kind_name(k))) chosen = k;
        if (armed) nk_style_pop_color(ctx);
    }
    return chosen;
}

void window_measure(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_measure || !G.loaded) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Measurements", NK_SHOWN);
    was_open = true;
    /* top-left of the view at first, under the toolbar */
    float w = CV_MIN(620 * s, G.vp_w * 0.7f), h = CV_MIN((6.5f + 3.6f * CV_MAX(app_measure_count(), 1)) * row, G.vp_h * 0.6f);
    if (nk_begin(ctx, "Measurements", nk_rect(G.vp_x + 10 * s, G.vp_y + 10 * s, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        int k = ui_measure_row(ctx, s, row, "#meas new");
        if (k >= 0) app_measure_arm(k, UINT32_MAX);
        if (app_measure_armed()) {
            nk_layout_row_dynamic(ctx, row, 1);
            nk_label_colored(ctx, app_measure_prompt(), NK_TEXT_LEFT, P.accent);
        }
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 90 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        nk_label(ctx, "labels show", NK_TEXT_LEFT);
        static const char* shows[] = { "undeformed -> deformed", "undeformed", "deformed (true scale)" };
        tip(ctx, "What the labels in the view give: both values (undeformed -> deformed), or one of them.\n"
                 "Deformed: with the step's displacement at true scale, whatever the scale on screen");
        int sh = nk_combo(ctx, shows, 3, G.meas_show, (int)row, nk_vec2(220 * s, 3 * row + 20 * s));
        if (sh != G.meas_show) { G.meas_show = sh; app_label_changed(); }
        int n = app_measure_count(), del = -1;
        if (!n) {
            nk_layout_row_dynamic(ctx, row, 1);
            nk_label_colored(ctx, "none yet: pick a kind above, then click the nodes", NK_TEXT_LEFT, P.dim);
        }
        for (int i = 0; i < n; i++) {
            int kind; uint32_t id[3];
            if (!app_measure_get(i, &kind, id)) continue;
            char t[200];
            uii_hsep(ctx, s);
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, 60 * s);
            nk_layout_row_template_end(ctx);
            if (kind == CV_MEAS_DIST) snprintf(t, sizeof t, "%d  %s  nodes %u, %u", i + 1, app_measure_kind_name(kind), id[0], id[1]);
            else snprintf(t, sizeof t, "%d  %s  nodes %u, %u, %u", i + 1, app_measure_kind_name(kind), id[0], id[1], id[2]);
            nk_label(ctx, t, NK_TEXT_LEFT);
            char mk[32];
            snprintf(mk, sizeof mk, "#meas delete %d", i + 1);
            uii_test_mark(ctx, mk);
            if (nk_button_label(ctx, "delete")) del = i;
            nk_layout_row_dynamic(ctx, row, 1);
            for (int st = 0; st < 2; st++) {
                app_measure_line(i, st, t, sizeof t);
                nk_label_colored(ctx, t, NK_TEXT_LEFT, st ? P.text : P.dim);
            }
        }
        if (del >= 0) app_measure_remove(del);
        uii_hsep(ctx, s);
        nk_layout_row_dynamic(ctx, row, 3);
        tip(ctx, "Every measurement with both values, as text, to the clipboard");
        if (nk_button_label(ctx, "copy") && n) {
            char* txt = malloc((size_t)n * 640 + 1);
            if (txt) { app_measure_copy(txt, (size_t)n * 640 + 1); sapp_set_clipboard_string(txt); free(txt); }
        }
        tip(ctx, "Save them as <model>_measurements.csv beside the model: a row per measurement and state");
        if (nk_button_label(ctx, "save CSV")) app_measure_csv();
        uii_test_mark(ctx, "#meas clear");
        if (nk_button_label(ctx, "clear all")) { app_measure_cancel(); app_measure_clear(); }
    }
    if (nk_window_is_hidden(ctx, "Measurements")) { G.show_measure = false; app_measure_cancel(); }
    nk_end(ctx);
}
