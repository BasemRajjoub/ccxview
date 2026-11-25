/* ui_stl.c -- Model > Imported geometry: STL files shown with the results (parts of
   the assembly that were not analysed), each with its colour, opacity and unit scale
   (app_stl.c). */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"

static int pick = -1;                    /* the layer whose colour picker is open */

static const float scales[] = { 0.001f, 0.01f, 0.1f, 1.f, 10.f, 100.f, 1000.f };
static const char* const scale_names[] = { "x0.001", "x0.01", "x0.1", "x1", "x10", "x100", "x1000" };

/* the colour swatch of layer i: a colour button, Nuklear's picker in a popup beside it */
static void swatch(struct nk_context* ctx, float s, float row, int i, cv_stl_layer* l) {
    struct nk_rect sb = nk_widget_bounds(ctx);
    tip(ctx, "Its colour");
    if (nk_button_color(ctx, nk_rgb_f(l->rgb[0], l->rgb[1], l->rgb[2]))) pick = pick == i ? -1 : i;
    if (pick != i) return;
    struct nk_rect clip = ctx->current->layout->clip;
    float pw = 230 * s, ph = 170 * s + 3 * row + 6 * ctx->style.window.spacing.y;
    struct nk_rect pr = nk_rect(sb.x + sb.w + 4 * s - clip.x, sb.y + sb.h - clip.y, pw, ph);
    if (!nk_popup_begin(ctx, NK_POPUP_STATIC, "pick geometry colour", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, pr)) { pick = -1; return; }
    nk_layout_row_dynamic(ctx, 170 * s, 1);
    struct nk_colorf nf = nk_color_picker(ctx, (struct nk_colorf){ l->rgb[0], l->rgb[1], l->rgb[2], 1 }, NK_RGB);
    uik_cols(ctx, 3);
    nf.r = nk_propertyf(ctx, "#R", 0, nf.r, 1, 0.01f, 0.005f);
    nf.g = nk_propertyf(ctx, "#G", 0, nf.g, 1, 0.01f, 0.005f);
    nf.b = nk_propertyf(ctx, "#B", 0, nf.b, 1, 0.01f, 0.005f);
    l->rgb[0] = nf.r; l->rgb[1] = nf.g; l->rgb[2] = nf.b;
    uik_cols(ctx, 1);
    if (nk_button_label(ctx, "done")) { pick = -1; nk_popup_close(ctx); }
    nk_popup_end(ctx);
}

void section_stl(struct nk_context* ctx, float s, float row) {
    static const char* const il[1] = { "Import STL..." };
    static const char* const it[1] = { "Add an STL file: parts of the assembly that were not analysed, shown with the results" };
    if (!app_stl_count()) {                  /* none yet: just the button, no section */
        if (uik_buttons(ctx, NULL, 1, il, it) == 0) app_open_dialog(CV_DLG_STL);
        return;
    }
    if (!uik_section(ctx, "Imported geometry", CV_TREE_IMPORT)) return;
    if (uik_buttons(ctx, NULL, 1, il, it) == 0) app_open_dialog(CV_DLG_STL);
    int remove = -1;
    for (int i = 0; i < app_stl_count(); i++) {
        cv_stl_layer l, was;
        if (!app_stl_get(i, &l)) continue;
        was = l;
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, row * 1.4f);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, row * 1.4f);
        nk_layout_row_template_end(ctx);
        swatch(ctx, s, row, i, &l);
        char lab[300];
        snprintf(lab, sizeof lab, "%s  (%u)", app_stl_name(i), app_stl_tris(i));
        tip(ctx, "Show this imported file; the number is its triangles");
        nk_checkbox_label(ctx, lab, &l.visible);
        tip(ctx, "Remove this file from the view");
        if (nk_button_label(ctx, "x")) remove = i;

        uik_float(ctx, "opacity", &l.alpha, 0.05f, 1.f, 0.05f, "Opacity: 1 solid, less to see the results through it");
        int k = 3;
        for (int j = 0; j < (int)CV_COUNT(scales); j++) if (l.scale > scales[j] * 0.999f && l.scale < scales[j] * 1.001f) k = j;
        if (uik_combo(ctx, "scale", scale_names, (int)CV_COUNT(scales), &k,
                      "Its lengths times this: x1000 for a file in metres beside a model in mm, x0.001 the other way")) l.scale = scales[k];
        if (memcmp(&l, &was, sizeof l)) app_stl_set(i, &l);
    }
    if (remove >= 0) { app_stl_remove(remove); if (pick >= remove) pick = -1; }
    uik_end(ctx);
}
