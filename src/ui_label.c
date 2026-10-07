/* ui_label.c -- Fields > Labels: what the labels on the model show, and how they look. */
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
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 50 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, row * 1.4f);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, name, NK_TEXT_LEFT);
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
    };
    if (!nk_tree_state_push(ctx, NK_TREE_NODE, "Labels", (enum nk_collapse_states*)&G.tree[CV_TREE_LABELS])) return;
    nk_layout_row_dynamic(ctx, row, 1);
    for (int k = 0; k < CV_LABEL_N; k++) {
        bool on = G.label_kind == k;
        tip(ctx, tips[k]);
        if (nk_option_label(ctx, app_label_name(k), on) && !on) { G.label_kind = k; app_label_changed(); }
    }
    if (G.label_kind != CV_LABEL_NONE) {
        if (G.label_note[0]) nk_label_colored(ctx, G.label_note + 8, NK_TEXT_LEFT, P.dim);   /* "shown 420 of 18 000" */
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Text height in pixels");
        nk_property_float(ctx, "#size", 6.f, &G.label_px, 48.f, 1.f, 0.2f);
        tip(ctx, "The gap kept between labels, pixels; 0 shows every one, overlapping (a carpet on a fine mesh)");
        nk_property_float(ctx, "#gap", 0.f, &G.label_spacing, 400.f, 2.f, 0.5f);
        tip(ctx, "Only the box selection's nodes and elements");
        if (nk_checkbox_label(ctx, "selection only", &G.label_sel_only)) app_label_changed();
        tip(ctx, "Only the probed element and its nodes (the Probe's labels box)");
        if (nk_checkbox_label(ctx, "probed element only", &G.label_probe_only)) app_label_changed();
        static const float tp[3][3] = { { 1.f, 0.93f, 0.6f }, { 1.f, 1.f, 1.f }, { 0.1f, 0.1f, 0.1f } };
        static const float bp[3][3] = { { 0.f, 0.f, 0.f }, { 1.f, 1.f, 1.f }, { 0.15f, 0.25f, 0.5f } };
        static const char* const tn[3] = { "yellow", "white", "black" }, *const bn[3] = { "black", "white", "blue" };
        colour_row(ctx, s, row, "text", "lblpick", G.label_rgb, tp, tn);
        colour_row(ctx, s, row, "box", "lblbox", G.label_box_rgba, bp, bn);
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "How solid the box behind the text is; 0 none");
        nk_property_float(ctx, "#box opacity", 0.f, &G.label_box_rgba[3], 1.f, 0.05f, 0.005f);
    }
    nk_tree_state_pop(ctx);
}
