/* ui_rail.c -- the icon rail (G.ui_rail): the left panel's tabs as icons in a thin strip at the window's
   left edge, VS Code's activity bar. A click on a tab's icon opens the panel on it, on the open one hides
   the panel (G.scene_on: the view grows); Search (the palette) and Settings sit at the strip's foot. The
   icons are drawn as lines, so no assets and crisp at any scale. Off: the text strip of ui_tabs.c. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"

float uii_rail_w(float s) { return G.ui_rail && G.loaded && !G.hide_panels ? roundf(40.f * s) : 0.f; }

void ui_panel_tab(int t) {
    if (t < 0 || t >= CV_PTAB_N) return;
    if (G.panel_tab != t) ui_search_clear();
    G.panel_tab = t; G.scene_on = true;
    app_wake();
}

enum { IC_MODEL, IC_RESULTS, IC_VIEWI, IC_RENDER, IC_EXPORT, IC_SEARCHI, IC_GEAR };

/* an icon in a square of half size h round (x, y), lines t thick */
static void icon(struct nk_command_buffer* cv, int k, float x, float y, float h, float t, struct nk_color c) {
    switch (k) {
    case IC_MODEL: {                                   /* a cube from above: a hexagon, a Y inside */
        float a = 0.87f * h, p[14] = { x, y - h, x + a, y - 0.5f * h, x + a, y + 0.5f * h, x, y + h,
                                       x - a, y + 0.5f * h, x - a, y - 0.5f * h, x, y - h };
        nk_stroke_polyline(cv, p, 7, t, c);
        nk_stroke_line(cv, x, y, x - a, y - 0.5f * h, t, c);
        nk_stroke_line(cv, x, y, x + a, y - 0.5f * h, t, c);
        nk_stroke_line(cv, x, y, x, y + h, t, c);
        break;
    }
    case IC_RESULTS:                                   /* a bar chart, rising */
        for (int i = 0; i < 3; i++) {
            float bx = x + (i - 1) * 0.72f * h, top = y + h - (0.75f + 0.6f * i) * h;
            nk_fill_rect(cv, nk_rect(bx - 0.24f * h, top, 0.48f * h, y + h - top), 0.12f * h, c);
        }
        break;
    case IC_VIEWI: {                                   /* an eye: two lids, a pupil */
        float w = 1.05f * h;
        nk_stroke_curve(cv, x - w, y, x - 0.45f * w, y - 0.82f * h, x + 0.45f * w, y - 0.82f * h, x + w, y, t, c);
        nk_stroke_curve(cv, x - w, y, x - 0.45f * w, y + 0.82f * h, x + 0.45f * w, y + 0.82f * h, x + w, y, t, c);
        float r = 0.34f * h;
        nk_stroke_circle(cv, nk_rect(x - r, y - r, 2 * r, 2 * r), t, c);
        break;
    }
    case IC_RENDER: {                                  /* a sun: a disc, eight rays */
        float r = 0.42f * h;
        nk_stroke_circle(cv, nk_rect(x - r, y - r, 2 * r, 2 * r), t, c);
        for (int i = 0; i < 8; i++) {
            float a = i * 0.785398f, ca = cosf(a), sa = sinf(a);
            nk_stroke_line(cv, x + 0.68f * h * ca, y + 0.68f * h * sa, x + 1.02f * h * ca, y + 1.02f * h * sa, t, c);
        }
        break;
    }
    case IC_EXPORT: {                                  /* an arrow up out of a tray */
        float p[8] = { x - 0.9f * h, y + 0.1f * h, x - 0.9f * h, y + 0.95f * h, x + 0.9f * h, y + 0.95f * h, x + 0.9f * h, y + 0.1f * h };
        nk_stroke_polyline(cv, p, 4, t, c);
        nk_stroke_line(cv, x, y + 0.45f * h, x, y - h, t, c);
        float q[6] = { x - 0.45f * h, y - 0.55f * h, x, y - h, x + 0.45f * h, y - 0.55f * h };
        nk_stroke_polyline(cv, q, 3, t, c);
        break;
    }
    case IC_SEARCHI: {                                 /* a magnifier */
        float r = 0.6f * h, cx = x - 0.18f * h, cy = y - 0.18f * h;
        nk_stroke_circle(cv, nk_rect(cx - r, cy - r, 2 * r, 2 * r), t, c);
        nk_stroke_line(cv, cx + 0.72f * r, cy + 0.72f * r, x + 0.95f * h, y + 0.95f * h, t * 1.3f, c);
        break;
    }
    case IC_GEAR: {                                    /* a gear: a ring, eight teeth, a hole */
        float r = 0.6f * h, ri = 0.24f * h;
        nk_stroke_circle(cv, nk_rect(x - r, y - r, 2 * r, 2 * r), t, c);
        nk_stroke_circle(cv, nk_rect(x - ri, y - ri, 2 * ri, 2 * ri), t, c);
        for (int i = 0; i < 6; i++) {
            float a = i * 1.047198f, ca = cosf(a), sa = sinf(a);
            nk_stroke_line(cv, x + r * ca, y + r * sa, x + 0.95f * h * ca, y + 0.95f * h * sa, 1.8f * t, c);
        }
        break;
    }
    }
}

/* one icon button; on: the chosen one (filled, accent), lit: in the accent without the fill */
static bool rail_button(struct nk_context* ctx, float s, int k, const char* tiptext, bool on, bool lit) {
    struct nk_rect b = nk_widget_bounds(ctx);
    tip(ctx, tiptext);
    struct nk_style_button st = ctx->style.button;
    st.normal = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    st.hover = nk_style_item_color(mix(ctx->style.window.background, P.text, 0.12f));
    st.active = nk_style_item_color(mix(ctx->style.window.background, P.text, 0.2f));
    st.border = 0; st.rounding = roundf(6.f * s);
    if (on) st.normal = st.hover = nk_style_item_color(nk_rgba(P.accent.r, P.accent.g, P.accent.b, 56));
    bool hit = nk_button_label_styled(ctx, &st, "");
    bool hover = nk_input_is_mouse_hovering_rect(&ctx->input, b);
    struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
    struct nk_color c = on || lit ? P.accent : hover ? P.text : mix(P.dim, P.text, 0.35f);
    float h = roundf(CV_MIN(b.w, b.h) * 0.32f), t = CV_MAX(1.5f * s, 1.2f);
    icon(cv, k, roundf(b.x + b.w * 0.5f), roundf(b.y + b.h * 0.5f), h, t, c);
    return hit;
}

static const char* const rail_tip[CV_PTAB_N] = {
    "Model: what is drawn (layers), groups and sets, imported geometry\nCtrl+1; a click on the open tab hides the panel",
    "Results: the fields of the step, labels, contact, failure, mesh quality\nCtrl+2; a click on the open tab hides the panel",
    "View: look from, camera, clip and crop, copies, colours and legend, symbol sizes\nCtrl+3; a click on the open tab hides the panel",
    "Render: the studio look, light, backdrop, ambient occlusion, a still image\nCtrl+4; a click on the open tab hides the panel",
    "Export: pictures, animations, data, the file\nCtrl+5; a click on the open tab hides the panel",
};

/* the rail's window at r */
void uii_rail(struct nk_context* ctx, float s, struct nk_rect r) {
    if (uii_pane_begin(ctx, "Rail", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_window_set_bounds(ctx, "Rail", r);
        const struct nk_vec2 pad = ctx->style.window.padding;
        const float sp = ctx->style.window.spacing.y;
        const float ih = r.w - 2 * pad.x;                  /* square buttons */
        float room = r.h - 2 * pad.y - 7 * (ih + sp);
        G.panel_tab = CV_MIN(CV_MAX(G.panel_tab, 0), CV_PTAB_N - 1);
        for (int t = 0; t < CV_PTAB_N; t++) {
            nk_layout_row_dynamic(ctx, ih, 1);
            bool cur = G.panel_tab == t;
            if (rail_button(ctx, s, t, rail_tip[t], cur && G.scene_on, cur && !G.scene_on)) {
                if (cur && G.scene_on) G.scene_on = false;
                else ui_panel_tab(t);
            }
        }
        {   /* the rest of the strip's height, less the two at its foot */
            const struct nk_panel* l = ctx->current->layout;
            struct nk_rect cr = nk_window_get_content_region(ctx);
            room = cr.y + cr.h - (l->at_y + l->row.height + sp) - 2 * (ih + sp) - sp;
        }
        if (room > 0) { struct nk_rect gap; nk_layout_row_dynamic(ctx, room, 1); nk_widget(&gap, ctx); }   /* nk_spacing would skip a row more */
        nk_layout_row_dynamic(ctx, ih, 1);
        if (rail_button(ctx, s, IC_SEARCHI, "Search every setting and action (Ctrl+K)", false, false)) {
            if (G.ui_palette) ui_palette_open(NULL); else ui_focus_search();
        }
        if (rail_button(ctx, s, IC_GEAR, "Settings: the interface, display, mouse, 3D mouse, background, keys", false, G.show_settings))
            G.show_settings = !G.show_settings;
    }
    uii_pane_end(ctx);
}
