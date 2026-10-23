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
#include <math.h>
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

/* ---- the Inspect tab ----------------------------------------------------------- */

/* the field at the probe now: the field or the step may have changed since the click
   (an integration point keeps the value found then) */
static float probe_value_now(void) {
    if (!G.has_field) return NAN;
    if (G.probe_ip) return G.probe_value;
    if ((G.elem_mode || G.field_src == 1) && G.elem_val) return G.elem_val[G.probe.elem];
    return G.scalar ? G.scalar[G.probe.node] : NAN;
}

/* "S Mises = 4.5e2", "point 3: S Mises = 4.5e2"; over a face or edge picked, the
   average; "no field" */
void uii_probe_value_text(char* out, size_t n) {
    char num[32];
    uint32_t nd[8];
    if (G.has_field && app_pick_nodes(nd)) {
        fmt_num(num, sizeof num, app_pick_average());
        snprintf(out, n, "%s average %s", G.field_label, num);
        return;
    }
    float v = probe_value_now();
    fmt_num(num, sizeof num, v);
    if (app_field_category(v)) snprintf(num, sizeof num, "%s", app_field_category(v));
    if (!G.has_field) snprintf(out, n, "no field");
    else if (G.probe_ip) snprintf(out, n, "point %d: %s = %s", G.probe_ip, G.field_label, num);
    else snprintf(out, n, "%s%s = %s", G.field_label, G.elem_mode ? " (elem)" : "", num);
}

/* "node 123  element 4  S Mises = 4.5e2": the probe in a line (the status bar) */
void uii_probe_readout(char* out, size_t n) {
    char v[160];
    uint32_t nd[8];
    int k = app_pick_nodes(nd);
    uii_probe_value_text(v, sizeof v);
    const uint32_t* id = G.frd.node_id;
    if (k && G.pick_kind == CV_PK_FACE)
        snprintf(out, n, "face S%u of element %u  %s", (G.probe_face & 7) + 1, G.frd.elem_id[G.probe_face >> 3], v);
    else if (k) snprintf(out, n, "edge %u - %u  %s", id[nd[0]], id[nd[k - 1]], v);
    else if (G.pick_kind == CV_PK_ELEM) snprintf(out, n, "element %u  node %u  %s", G.frd.elem_id[G.probe.elem], id[G.probe.node], v);
    else snprintf(out, n, "node %u  element %u  %s", id[G.probe.node], G.frd.elem_id[G.probe.elem], v);
}

/* what a click picks: node, element, face, edge */
static void kind_row(struct nk_context* ctx, float s, float row) {
    static const char* name[CV_PK_N] = { "Node", "Element", "Face", "Edge" };
    static const char* tips[CV_PK_N] = {
        "A click picks a node (and the element it is on)",
        "A click picks an element: outlined in the view, its details first",
        "A click picks a face of an element: its surfaces, area, normal, the field over it",
        "A click picks the element's edge nearest it: its nodes, length, the field along it",
    };
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 34 * s);
    for (int k = 0; k < CV_PK_N; k++) nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    nk_label_colored(ctx, "pick", NK_TEXT_LEFT, P.dim);
    for (int k = 0; k < CV_PK_N; k++) {
        nk_bool on = G.pick_kind == k;
        tip(ctx, tips[k]);
        nk_style_push_color(ctx, &ctx->style.selectable.text_normal_active, P.accent);
        nk_style_push_color(ctx, &ctx->style.selectable.text_hover_active, P.accent);
        if (nk_selectable_label(ctx, name[k], NK_TEXT_CENTERED, &on)) G.pick_kind = k;
        nk_style_pop_color(ctx); nk_style_pop_color(ctx);
    }
}

/* nothing picked: the model in a few lines */
static void model_summary(struct nk_context* ctx, float s, float row) {
    char t[200], a[32], b[32];
    nk_layout_row_dynamic(ctx, row, 1);
    uii_test_mark(ctx, "#model summary");
    nk_label_colored(ctx, cv_basename(G.path), NK_TEXT_LEFT, P.accent);
    snprintf(t, sizeof t, "%u nodes, %u elements", G.frd.n_nodes, G.frd.n_elems);
    nk_label(ctx, t, NK_TEXT_LEFT);
    if (G.frd.n_steps > 0) {
        const cv_step* st = &G.frd.steps[G.step];
        snprintf(t, sizeof t, "step %d  increment %d  (%d of %d)", st->step, st->inc, G.step + 1, G.frd.n_steps);
        nk_label(ctx, t, NK_TEXT_LEFT);
        snprintf(t, sizeof t, st->modal ? "frequency %g" : "time %g", st->time);
        nk_label(ctx, t, NK_TEXT_LEFT);
    }
    if (G.has_field) {
        snprintf(t, sizeof t, "%s%s", G.field_label, G.elem_mode ? " (per element)" : "");
        nk_label(ctx, t, NK_TEXT_LEFT);
        fmt_num(a, sizeof a, G.data_min); fmt_num(b, sizeof b, G.data_max);
        snprintf(t, sizeof t, "   from %s to %s", a, b);
        nk_label(ctx, t, NK_TEXT_LEFT);
    } else nk_label(ctx, "no field", NK_TEXT_LEFT);
    units_summary(a, sizeof a);
    snprintf(t, sizeof t, "units: %s", a);
    nk_label(ctx, t, NK_TEXT_LEFT);
    uii_hsep(ctx, s);
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label_colored(ctx, "click the model: a node and its element", NK_TEXT_LEFT, P.dim);
}

/* the probed node and element: the value, what can be done from them, then all about them */
static void probe_rows(struct nk_context* ctx, float s, float row) {
    const cv_pick* p = &G.probe;
    char t[200];
    uii_probe_readout(t, sizeof t);
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "What is probed and the field there (below: all about it, in groups that fold)");
    nk_label_colored(ctx, t, NK_TEXT_LEFT, P.accent);
    uint32_t nd[8];
    if ((G.pick_kind == CV_PK_FACE || G.pick_kind == CV_PK_EDGE) && !app_pick_nodes(nd))
        nk_label_colored(ctx, G.pick_kind == CV_PK_FACE ? "no face picked: click one on the model" : "no edge picked: click near one",
                         NK_TEXT_LEFT, P.dim);
    nk_layout_row_dynamic(ctx, row, 3);
    nk_bool lb = G.label_probe_only && G.label_kinds;
    tip(ctx, "The ids of this element and its nodes in the view (Fields > Labels: the kind, size and colours)");
    if (nk_checkbox_label(ctx, "labels", &lb)) {           /* on: this element only; off: no labels at all */
        G.label_probe_only = lb;
        if (lb) G.label_sel_only = false;            /* one place at a time (Fields > Labels) */
        if (lb && !G.label_kinds) G.label_kinds = 1 << CV_LABEL_NODE;
        if (!lb) G.label_kinds = 0;
        app_label_changed();
    }
    tip(ctx, "The field at this node (element in per-element mode) against time, over every step");
    if (nk_button_label(ctx, "history")) app_hist_open(p->node, p->elem);
    tip(ctx, "Clear the probe (and a pending path or measurement, the selection)");
    if (nk_button_label(ctx, "clear")) { G.probe_on = false; G.path_arm = false; app_measure_cancel(); app_sel_clear(); return; }
    static int pick_dir;
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "Where the path from this node goes: to a node you click next, or straight into the\n"
             "solid along the surface normal or an axis, up to the face where it comes out");
    pick_dir = nk_combo(ctx, path_dirs, 5, pick_dir, (int)row, nk_vec2(130 * s, 5 * row + 20 * s));
    tip(ctx, pick_dir ? "The field on the line and the stress linearized on it (ASME), through the wall"
                      : G.path_arm ? "Now click the end node (Esc cancels)"
                      : "The field along the line from this node to the next one you click, and the\n"
                        "stress linearized on it (ASME): for that, pick the ends facing each other across the wall");
    if (nk_button_label(ctx, pick_dir ? "path" : G.path_arm ? "path: to?" : "path from")) {
        if (pick_dir) app_path_ray(p->node, pick_dir); else app_path_start(p->node);
    }
    int mk = ui_measure_row(ctx, s, row, "#probe measure");
    if (mk >= 0) app_measure_arm(mk, p->node);
    nk_layout_row_dynamic(ctx, row, 3);
    tip(ctx, "A tag in the view beside what is probed: what it is and the value there, in pictures too;\n"
             "drag it where it reads best (T)");
    nk_checkbox_label(ctx, "tag in view", &G.probe_tag);
    tip(ctx, "Element, node and value to the clipboard");
    if (nk_button_label(ctx, "copy")) {
        uii_probe_readout(t, sizeof t);
        sapp_set_clipboard_string(t);
    }
    tip(ctx, "All of it below to the clipboard, a line each");
    if (nk_button_label(ctx, "copy all")) uii_details_copy();
    uii_hsep(ctx, s);
    uii_details_body(ctx, s, row);
}

/* a selection: its size, its extremes (a click goes there), what is done with it */
static void selection_rows(struct nk_context* ctx, float s, float row) {
    char t[200], a[32];
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 60 * s);
    nk_layout_row_template_end(ctx);
    snprintf(t, sizeof t, "Selection: %u elements, %u nodes", G.sel_n, G.seln_n);
    nk_label_colored(ctx, t, NK_TEXT_LEFT, P.accent);
    tip(ctx, "Nothing selected (Esc in the view does the same)");
    if (nk_button_label(ctx, "clear")) { app_sel_clear(); G.sel_note[0] = 0; return; }
    if (G.boxq.on && G.boxq.gen == G.field_gen) {
        const uint32_t* id = G.boxq.elem ? G.frd.elem_id : G.frd.node_id;
        const char* w = G.boxq.elem ? "element" : "node";
        for (int k = 0; k < 2; k++) {
            uint32_t at = k ? G.boxq.min_at : G.boxq.max_at;
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, 60 * s);
            nk_layout_row_template_end(ctx);
            fmt_num(a, sizeof a, k ? G.boxq.vmin : G.boxq.vmax);
            snprintf(t, sizeof t, "%s %s at %s %u", k ? "min" : "max", a, w, id[at]);
            nk_label(ctx, t, NK_TEXT_LEFT);
            tip(ctx, k ? "Probe the selection's min, the view centred on it" : "Probe the selection's max, the view centred on it");
            if (nk_button_label(ctx, "go to")) {
                char q[sizeof G.boxq];                /* probing elsewhere forgets the box; the selection keeps it */
                memcpy(q, &G.boxq, sizeof q);
                app_view_push();
                app_find(id[at], G.boxq.elem);
                memcpy(&G.boxq, q, sizeof q);
            }
        }
    }
    nk_layout_row_dynamic(ctx, row, 4);
    tip(ctx, "The field integrated over the selected elements at every step (volume integral and average),\n"
             "or summed over the selected nodes (reaction forces)");
    if (nk_button_label(ctx, "integrate")) app_integ_open(G.sel_n ? CV_IK_VOLUME : CV_IK_NODES, "selection");
    tip(ctx, "Hide the selected elements (right click > Show all brings them back)");
    if (nk_button_label(ctx, "hide") && G.sel_n) app_hide_elems(G.sel, G.sel_n);
    tip(ctx, "Show only the selected elements");
    if (nk_button_label(ctx, "isolate") && G.sel_n) app_isolate_elems(G.sel, G.sel_n);
    nk_bool lo = G.label_sel_only && G.label_kinds;
    tip(ctx, "Labels on the selection only (Fields > Labels: what they show; node ids when none is chosen)");
    if (nk_checkbox_label(ctx, "ids", &lo)) {
        G.label_sel_only = lo;
        if (lo) G.label_probe_only = false;
        if (lo && !G.label_kinds) G.label_kinds = 1 << CV_LABEL_NODE;
        app_label_changed();
    }
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "The Selection tab (S): what a box or a click takes, add / remove / intersect, invert, by name or id");
    if (nk_button_label(ctx, "more: the Selection tab")) G.insp_tab = CV_TAB_SELECT;
    uii_hsep(ctx, s);
}

/* ---- the Inspector ------------------------------------------------------------- */

/* S, the toolbar's Select, the menu, the View buttons set G.show_select (show_measure):
   the pane shows on that tab, and back to Inspect when it is set false. Then they
   follow the tab shown: a selection tool works while its tab is up. */
void uii_tabs_sync(void) {
    static bool sel_was, meas_was;
    if (G.show_select && !sel_was) { G.insp_on = true; G.insp_tab = CV_TAB_SELECT; G.sel_note[0] = 0; }
    else if (!G.show_select && sel_was && G.insp_tab == CV_TAB_SELECT) G.insp_tab = CV_TAB_INSPECT;
    if (G.show_measure && !meas_was) { G.insp_on = true; G.insp_tab = CV_TAB_MEASURE; }
    else if (!G.show_measure && meas_was && G.insp_tab == CV_TAB_MEASURE) G.insp_tab = CV_TAB_INSPECT;
    bool on = G.loaded && G.insp_on;
    G.show_select = on && G.insp_tab == CV_TAB_SELECT;
    G.show_measure = on && G.insp_tab == CV_TAB_MEASURE;
    if (sel_was && !G.show_select) G.sel_tool = CV_ST_NONE;
    if (meas_was && !G.show_measure) app_measure_cancel();
    sel_was = G.show_select; meas_was = G.show_measure;
}

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
    switch (G.insp_tab) {
    case CV_TAB_INSPECT:
        kind_row(ctx, s, row);
        if (G.sel_n || G.seln_n) selection_rows(ctx, s, row);
        if (G.probe_on) probe_rows(ctx, s, row); else model_summary(ctx, s, row);
        break;
    case CV_TAB_SELECT:  uii_select_body(ctx, s, row); break;
    case CV_TAB_MEASURE: uii_measure_body(ctx, s, row); break;
    }
}
