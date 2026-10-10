/* ui_stl.c -- Groups > Imported geometry: STL files shown with the results (parts of
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
    nk_layout_row_dynamic(ctx, row, 3);
    nf.r = nk_propertyf(ctx, "#R", 0, nf.r, 1, 0.01f, 0.005f);
    nf.g = nk_propertyf(ctx, "#G", 0, nf.g, 1, 0.01f, 0.005f);
    nf.b = nk_propertyf(ctx, "#B", 0, nf.b, 1, 0.01f, 0.005f);
    l->rgb[0] = nf.r; l->rgb[1] = nf.g; l->rgb[2] = nf.b;
    nk_layout_row_dynamic(ctx, row, 1);
    if (nk_button_label(ctx, "done")) { pick = -1; nk_popup_close(ctx); }
    nk_popup_end(ctx);
}

void section_stl(struct nk_context* ctx, float s, float row) {
    if (!sub_push(ctx, "Imported geometry", CV_TREE_IMPORT)) return;
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "Add an STL file: parts of the assembly that were not analysed, shown with the results");
    if (nk_button_label(ctx, "Import STL...")) app_open_dialog(CV_DLG_STL);
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

        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 54 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 34 * s);
        nk_layout_row_template_push_static(ctx, 66 * s);
        nk_layout_row_template_end(ctx);
        nk_label(ctx, "opacity", NK_TEXT_LEFT);
        tip(ctx, "Opacity: 1 solid, less to see the results through it");
        ui_slider_float(ctx, 0.05f, &l.alpha, 1.f, 0.05f);
        char a[16];
        snprintf(a, sizeof a, "%.2f", l.alpha);
        nk_label(ctx, a, NK_TEXT_RIGHT);
        int k = 3;
        for (int j = 0; j < (int)CV_COUNT(scales); j++) if (l.scale > scales[j] * 0.999f && l.scale < scales[j] * 1.001f) k = j;
        tip(ctx, "Its lengths times this: x1000 for a file in metres beside a model in mm, x0.001 the other way");
        int nk2 = nk_combo(ctx, scale_names, (int)CV_COUNT(scales), k, (int)row, nk_vec2(90 * s, 8 * row + 20 * s));
        if (nk2 != k) l.scale = scales[nk2];
        if (memcmp(&l, &was, sizeof l)) app_stl_set(i, &l);
    }
    if (remove >= 0) { app_stl_remove(remove); if (pick >= remove) pick = -1; }
    nk_tree_pop(ctx);
}
