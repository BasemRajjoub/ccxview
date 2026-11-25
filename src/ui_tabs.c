/* ui_tabs.c -- the left panel's tabs: Model | Results | View | Render | Export, a gear for the Settings
   window at the strip's end. The strip sits under the search box (ui_search.c draws it, so it stays
   while results show); what each tab holds is the sections the other ui files make. The strip is the
   Inspector's too (uii_tab_strip). With the icon rail (ui_rail.c) the strip gives way to the tab's name. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"
#include "icons.h"
#include "app_cmds.h"

static const char* const ptab_tip[CV_PTAB_N] = {
    "Model: what is drawn (layers), groups and sets, imported geometry",
    "Results: the fields of the step, labels, contact, failure, mesh quality",
    "View: look from, camera, clip and crop, copies, colours and legend, symbol sizes",
    "Render: the studio look, light, backdrop, ambient occlusion, a still image",
    "Export: pictures, animations, data, the file",
};

/* a row of tabs, the one chosen in the accent colour; gear_tip: a settings button closes the row (true when clicked) */
bool uii_tab_strip(struct nk_context* ctx, float row, const char* const* names, const char* const* tips, int n, int* cur,
                   const char* gear_tip) {
    bool gear = false;
    if (gear_tip) {
        nk_layout_row_template_begin(ctx, row);
        for (int t = 0; t < n; t++) nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, row * 1.2f);
        nk_layout_row_template_end(ctx);
    } else nk_layout_row_dynamic(ctx, row, n);
    for (int t = 0; t < n; t++) {
        nk_bool on = *cur == t;
        tip(ctx, tips[t]);
        nk_style_push_color(ctx, &ctx->style.selectable.text_normal_active, P.accent);
        nk_style_push_color(ctx, &ctx->style.selectable.text_hover_active, P.accent);
        if (nk_selectable_label(ctx, names[t], NK_TEXT_CENTERED, &on)) *cur = t;
        nk_style_pop_color(ctx); nk_style_pop_color(ctx);
    }
    if (gear_tip) {
        tip(ctx, gear_tip);
        uii_test_mark(ctx, "#settings gear");
        gear = nk_button_label(ctx, IC_SETTINGS);
    }
    return gear;
}

void uii_scene_tabs(struct nk_context* ctx, float row) {
    int was = G.panel_tab;
    G.panel_tab = CV_MIN(CV_MAX(G.panel_tab, 0), CV_PTAB_N - 1);
    if (uii_rail_w(ui_scale()) > 0) {           /* the rail picks the tab: its name alone, small and dim */
        nk_layout_row_dynamic(ctx, roundf(row * 0.8f), 1);
        nk_label_colored(ctx, CV_PTAB_NAMES[G.panel_tab], NK_TEXT_LEFT, P.dim);
        return;
    }
    if (uii_tab_strip(ctx, row, CV_PTAB_NAMES, ptab_tip, CV_PTAB_N, &G.panel_tab, "Settings: the interface, display, mouse, 3D mouse, background, keys"))
        G.show_settings = !G.show_settings;
    if (G.panel_tab != was) ui_search_clear();
}

/* what the tab holds */
void uii_scene_body(struct nk_context* ctx, float s, float row) {
    switch (G.panel_tab) {
    case CV_PTAB_MODEL:
        section_layers(ctx, s, row);
        section_groups(ctx, s, row);
        section_stl(ctx, s, row);
        break;
    case CV_PTAB_RESULTS:
        section_fields(ctx, s, row);
        section_label(ctx, s, row);
        break;
    case CV_PTAB_VIEW: section_view(ctx, s, row); break;
    case CV_PTAB_RENDER: section_render(ctx, s, row); break;
    case CV_PTAB_EXPORT:
        section_export(ctx, s, row);
        section_file(ctx, s, row);
        break;
    }
}
