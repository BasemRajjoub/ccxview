/* ui_inspector.c -- the panes at the sides of the view: their widths (dragged by the
   inner edge, kept in the settings), the buttons and keys that hide and show them
   ([ the Scene pane, ] the Inspector), and the Inspector itself: a fixed window at
   the right with its tabs, Inspect | Selection | Measure. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "icons.h"
#include "ui_int.h"

/* ---- widths and the edges ------------------------------------------------------- */

static struct {
    float scene_x, insp_x, y0, y1;  /* the inner edges in the last frame, -1 none; the Inspector's top and bottom */
    int drag;                       /* 1 the Scene's edge, 2 the Inspector's, 0 none */
    float x0, w0;                   /* where the drag began, the width then (px) */
    bool cursor;                    /* the resize cursor is ours to put back */
} E = { -1, -1, 0, 0, 0, 0, 0, false };

/* the panes' widths in px this frame, 0 when hidden; the view keeps at least 40 % */
void uii_pane_widths(float W, float s, float* sw, float* iw) {
    bool loaded = G.loaded && !G.hide_panels;
    float a = G.hide_panels ? 0 : !loaded || G.scene_on ? CV_MAX(G.scene_w, 120.f) * s : 0;
    float b = loaded && G.insp_on ? CV_MAX(G.insp_w, 120.f) * s : 0;
    float room = 0.6f * W;
    if (a + b > room && a + b > 0) { float k = room / (a + b); a *= k; b *= k; }
    *sw = roundf(a); *iw = roundf(b);
}

/* a grab zone a few pixels wide, on the view's side of each inner edge */
static int edge_at(float x, float y, float s) {
    float g = 5 * s;
    if (E.scene_x >= 0 && x >= E.scene_x - 1 && x < E.scene_x + g && y < E.y1) return 1;
    if (E.insp_x >= 0 && x > E.insp_x - g && x <= E.insp_x + 1 && y >= E.y0 && y < E.y1) return 2;
    return 0;
}

bool uii_pane_edge_captured(float x, float y) { return E.drag || edge_at(x, y, U.scale); }

/* the edges this frame (-1: no edge), then a press on one drags it */
void uii_pane_edges(struct nk_context* ctx, float s, float W, float scene_x, float insp_x, float y0, float y1) {
    E.scene_x = scene_x; E.insp_x = insp_x; E.y0 = y0; E.y1 = y1;
    const struct nk_input* in = &ctx->input;
    float mx = in->mouse.pos.x, my = in->mouse.pos.y;
    int over = edge_at(mx, my, s);
    bool popup = false;
    for (struct nk_window* w = ctx->begin; w; w = w->next)
        if (!(w->flags & NK_WINDOW_HIDDEN) && w->popup.win && w->popup.active && w->popup.type != NK_PANEL_TOOLTIP) popup = true;
    if (!E.drag && over && !popup && nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT)) {
        E.drag = over; E.x0 = mx; E.w0 = over == 1 ? scene_x : W - insp_x;
    }
    if (E.drag && !in->mouse.buttons[NK_BUTTON_LEFT].down) E.drag = 0;
    float other = E.drag == 1 ? (insp_x >= 0 ? W - insp_x : 0) : scene_x >= 0 ? scene_x : 0;
    float most = CV_MAX((0.6f * W - other) / s, 120.f);        /* the view keeps its 40 % */
    if (E.drag == 1) G.scene_w = CV_MIN(CV_MAX((E.w0 + mx - E.x0) / s, 120.f), most);
    if (E.drag == 2) G.insp_w = CV_MIN(CV_MAX((E.w0 - (mx - E.x0)) / s, 120.f), most);
    bool want = (over && !popup) || E.drag;
    if (want != E.cursor) sapp_set_mouse_cursor(want ? SAPP_MOUSECURSOR_RESIZE_EW : SAPP_MOUSECURSOR_DEFAULT);
    E.cursor = want;
}

/* ---- the buttons that hide and show them ----------------------------------------- */

/* a small button in the row laid out: which 0 the Scene pane, 1 the Inspector */
void uii_pane_button(struct nk_context* ctx, int which) {
    bool* on = which ? &G.insp_on : &G.scene_on;
    bool left = which ? !*on : *on;              /* the chevron points where the pane goes */
    tip(ctx, which ? (*on ? "Hide the Inspector (])" : "Show the Inspector: what was clicked, the selection, the measurements (])")
                   : (*on ? "Hide the Scene pane ([)" : "Show the Scene pane: layers, groups, fields, view, export ([)"));
    if (nk_button_label(ctx, left ? IC_CHEVRON_LEFT : IC_CHEVRON_RIGHT)) *on = !*on;
}

/* ---- the Inspector ------------------------------------------------------------- */

static const char* tab_name[CV_TAB_N] = { "Inspect", "Selection", "Measure" };
static const char* tab_tip[CV_TAB_N] = {
    "Inspect: what a click in the view picked, or the model when nothing is",
    "Selection (S): box, click, by set or id, add / remove / intersect, invert",
    "Measure: distances, angles and circles between nodes",
};

static void tab_bar(struct nk_context* ctx, float row) {
    nk_layout_row_dynamic(ctx, row, CV_TAB_N);
    for (int t = 0; t < CV_TAB_N; t++) {
        nk_bool on = G.insp_tab == t;
        tip(ctx, tab_tip[t]);
        nk_style_push_color(ctx, &ctx->style.selectable.text_normal_active, P.accent);
        nk_style_push_color(ctx, &ctx->style.selectable.text_hover_active, P.accent);
        if (nk_selectable_label(ctx, tab_name[t], NK_TEXT_CENTERED, &on)) G.insp_tab = t;
        nk_style_pop_color(ctx); nk_style_pop_color(ctx);
    }
}

void uii_panel_inspector(struct nk_context* ctx, float s, float row) {
    tab_bar(ctx, row);
    uii_hsep(ctx, s);
    nk_layout_row_dynamic(ctx, row, 1);
    switch (G.insp_tab) {
    case CV_TAB_INSPECT: nk_label_colored(ctx, "click the model to probe it", NK_TEXT_LEFT, P.dim); break;
    case CV_TAB_SELECT:  nk_label_colored(ctx, "S: the Selection window", NK_TEXT_LEFT, P.dim); break;
    case CV_TAB_MEASURE: nk_label_colored(ctx, "the Measurements window: View", NK_TEXT_LEFT, P.dim); break;
    }
}
