/* ui_label.c -- Results > Labels: what the labels on the model show, and how they look. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"

/* a colour: three presets and a picker in a popup, as the Background control */
static void colour_row(struct nk_context* ctx, float s, float row, const char* name, const char* popup, float rgb[3],
                       const float presets[3][3], const char* const preset_names[3]) {
    uik_label_cells(ctx, name, NULL, 3, row * 1.4f / s);
    for (int k = 0; k < 3; k++)
        if (nk_button_label(ctx, preset_names[k])) memcpy(rgb, presets[k], 3 * sizeof *rgb);
    struct nk_rect bb = nk_widget_bounds(ctx);
    static const char* which;
    if (nk_button_color(ctx, nk_rgb_f(rgb[0], rgb[1], rgb[2]))) which = which == popup ? NULL : popup;
    if (which == popup) {
        struct nk_rect clip = ctx->current->layout->clip;
        float pw = 230 * s, ph = 170 * s + 2 * ctx->style.window.spacing.y + 8 * s;
        struct nk_rect pr = nk_rect(bb.x + bb.w - pw - clip.x, bb.y + bb.h - clip.y, pw, ph);
        if (nk_popup_begin(ctx, NK_POPUP_STATIC, popup, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, pr)) {
            nk_layout_row_dynamic(ctx, 170 * s, 1);
            struct nk_colorf cf = nk_color_picker(ctx, (struct nk_colorf){ rgb[0], rgb[1], rgb[2], 1 }, NK_RGB);
            rgb[0] = cf.r; rgb[1] = cf.g; rgb[2] = cf.b;
            if (nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT) && !nk_input_is_mouse_hovering_rect(&ctx->input, pr)) {
                which = NULL; nk_popup_close(ctx);
            }
            nk_popup_end(ctx);
        } else which = NULL;
    }
}

void section_label(struct nk_context* ctx, float s, float row) {
    static const char* tips[CV_LABEL_N] = {
        "No labels",
        "Every node of the surface: its id",
        "Every element with a face on the surface: its id, on that face",
        "The shown field at every surface node, in display units (per element: on the faces)",
        "The per-element value on each element's face",
        "The sets and surfaces ticked in Groups: their name at their centre",
        "Couplings and rigid bodies: their name at the reference node",
        "Loads: kind and value at the symbol (F force, M moment, p pressure, g gravity, bolt preload), in the deck's units",
        "Supports: the held degrees of freedom at the node",
        "Materials: the name at the centre of each material's surface",
        "The field's value at every Gauss point drawn (the Gauss points layer is turned on)",
        "Every Gauss point drawn: element id : point number (the Gauss points layer is turned on)",
        "The field's smallest and largest values at their node or element, as many of each as the count below\n(the Min / max balls are turned on)",
    };
    if (!uik_section(ctx, "Labels", CV_TREE_LABELS)) return;
    for (int k = 1; k < CV_LABEL_N; k++) {           /* any mix; id and value at one point make one label */
        nk_bool on = app_label_on(k);
        if (uik_check(ctx, app_label_name(k), &on, tips[k])) app_label_set(k, on);
    }
    static const char* const ml[1] = { "Measurements..." };
    static const char* const mt[1] = { "Measurements as labels: the distance between two nodes, the angle through three,\n"
                                       "the circle through three; undeformed and deformed. The list, new ones, CSV" };
    if (uik_buttons(ctx, NULL, 1, ml, mt) == 0) G.show_measure = !G.show_measure;
    if (G.label_kinds) {
        const char* shown = strstr(G.label_note, "; ");
        if (shown) uik_note(ctx, shown + 2);   /* "shown 420 of 18 000" */
        if (app_label_on(CV_LABEL_MINMAX)) {
            int n = G.minmax_n;
            uik_int(ctx, "min / max", &n, 1, 100, 1, "How many of the smallest and of the largest values get a ball and a label");
            if (n != G.minmax_n) { G.minmax_n = n; app_refresh_range(); }
        }
        uik_num(ctx, "size", &G.label_px, 6.f, 48.f, 1.f, "Text height in pixels");
        uik_num(ctx, "gap", &G.label_spacing, 0.f, 400.f, 2.f, "The gap kept between labels, pixels; 0 shows every one, overlapping (a carpet on a fine mesh)");
        {   /* where: one of three (both "only"s at once meant their overlap, rarely anything) */
            int at = G.label_probe_only ? 2 : G.label_sel_only ? 1 : 0, was = at;
            static const char* const wn[3] = { "everywhere", "selection", "probed" };
            uik_cols(ctx, uim_on(ctx) ? 1 : 3);
            if (uim_on(ctx)) {
                static const char* const wt[3] = { "Labels on every shown node and element (as the gap allows)",
                    "Only the box selection's nodes and elements", "Only the probed element and its nodes (the Probe's labels box)" };
                uim_segments_t(ctx, wn, 3, &at, wt);
            } else {
            tip(ctx, "Labels on every shown node and element (as the gap allows)");
            if (nk_option_label(ctx, "everywhere", at == 0)) at = 0;
            tip(ctx, "Only the box selection's nodes and elements");
            if (nk_option_label(ctx, "selection", at == 1)) at = 1;
            tip(ctx, "Only the probed element and its nodes (the Probe's labels box)");
            if (nk_option_label(ctx, "probed", at == 2)) at = 2;
            }
            if (at != was) { G.label_sel_only = at == 1; G.label_probe_only = at == 2; app_label_changed(); }
        }
        uik_check(ctx, "always in front", &G.label_front,
                  "Labels in front of everything, never cut by a face (nodes facing away get none anyway).\n"
                  "Off: hidden by what is nearer, as the model is; a face at a steep angle may cut into one");
        static const float tp[3][3] = { { 1.f, 0.93f, 0.6f }, { 1.f, 1.f, 1.f }, { 0.1f, 0.1f, 0.1f } };
        static const float bp[3][3] = { { 0.f, 0.f, 0.f }, { 1.f, 1.f, 1.f }, { 0.15f, 0.25f, 0.5f } };
        static const char* const tn[3] = { "yellow", "white", "black" }, *const bn[3] = { "black", "white", "blue" };
        colour_row(ctx, s, row, "text", "lblpick", G.label_rgb, tp, tn);
        colour_row(ctx, s, row, "box", "lblbox", G.label_box_rgba, bp, bn);
        uik_num(ctx, "box opacity", &G.label_box_rgba[3], 0.f, 1.f, 0.05f, "How solid the box behind the text is; 0 none");
    }
    if (app_label_moved_count()) {                   /* labels dragged on the model */
        char t[48];
        snprintf(t, sizeof t, "Reset moved labels (%d)", app_label_moved_count());
        const char* const rl[1] = { t };
        static const char* const rt[1] = { "Every label dragged on the model goes back beside its point\n(one alone: right-click it, Reset label)" };
        if (uik_buttons(ctx, NULL, 1, rl, rt) == 0) app_label_moved_clear();
    }
    uik_end(ctx);
}
