/* ui_select.c -- the Selection window (S): what is selected and its extremes, how
   what is picked next goes with it (new / add / remove / intersect), what a pick
   takes (elements, nodes, the side facing the eye), the tools (box, click, lasso,
   faces up to the feature edges, a chain of feature edges, the connected part),
   the steps on the selection itself (invert, to nodes, to elements, grow, shrink,
   boundary), deck sets,
   surfaces, types and materials by name, and an id list. app_select.c and
   app_seltools.c do the work. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include <strings.h>
#include "ui_int.h"

static const char* mode_verb[CV_SEL_MODES] = { "select", "add", "remove", "intersect" };

/* "label:" then the cells of a row */
static void row_head(struct nk_context* ctx, float s, float row, const char* label, int cells) {
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 70 * s);
    for (int i = 0; i < cells; i++) nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    nk_label_colored(ctx, label, NK_TEXT_LEFT, P.dim);
}

/* what is selected, its extremes */
static void summary(struct nk_context* ctx, float s, float row) {
    char t[200], a[32], b[32];
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 70 * s);
    nk_layout_row_template_end(ctx);
    if (G.sel_n || G.seln_n) snprintf(t, sizeof t, "%u elements, %u nodes selected", G.sel_n, G.seln_n);
    else snprintf(t, sizeof t, "nothing selected");
    nk_label(ctx, t, NK_TEXT_LEFT);
    tip(ctx, "Nothing selected (Esc in the view does the same)");
    if (nk_button_label(ctx, "clear")) { app_sel_clear(); G.sel_note[0] = 0; }
    nk_layout_row_dynamic(ctx, row, 1);
    if (G.boxq.on && G.boxq.gen == G.field_gen) {
        const uint32_t* id = G.boxq.elem ? G.frd.elem_id : G.frd.node_id;
        const char* w = G.boxq.elem ? "element" : "node";
        fmt_num(a, sizeof a, G.boxq.vmax); fmt_num(b, sizeof b, G.boxq.vmin);
        snprintf(t, sizeof t, "max %s at %s %u, min %s at %s %u", a, w, id[G.boxq.max_at], b, w, id[G.boxq.min_at]);
        tip(ctx, "The field shown, over the selected nodes (the selected elements' nodes; per element: the elements).\n"
                 "The probe sits on the max");
        nk_label(ctx, t, NK_TEXT_LEFT);
    } else nk_label_colored(ctx, G.sel_n || G.seln_n ? "no field values over it" : "", NK_TEXT_LEFT, P.dim);
}

/* new / add / remove / intersect */
static void mode_row(struct nk_context* ctx, float s, float row) {
    static const char* tips[CV_SEL_MODES] = {
        "What you pick next replaces the selection (the click tool: a click on something selected takes it out)",
        "What you pick next joins the selection",
        "What you pick next leaves the selection",
        "Only what is both selected and picked next stays",
    };
    row_head(ctx, s, row, "mode", CV_SEL_MODES);
    for (int m = 0; m < CV_SEL_MODES; m++) {
        tip(ctx, tips[m]);
        if (nk_option_label(ctx, mode_verb[m], G.sel_mode == m)) G.sel_mode = m;
    }
}

/* what a pick takes: elements, nodes or both (never neither); the facing side; a change takes the box again */
static void takes_rows(struct nk_context* ctx, float s, float row) {
    row_head(ctx, s, row, "takes", 3);
    bool e = G.sel_elems, n = G.sel_nodes;
    tip(ctx, "Elements: drag left to right for those wholly inside, right to left for every one touched");
    nk_checkbox_label(ctx, "elements", &G.sel_elems);
    tip(ctx, "Nodes inside the box; the max and min are then taken over them");
    nk_checkbox_label(ctx, "nodes", &G.sel_nodes);
    if (!G.sel_elems && !G.sel_nodes) { if (e) G.sel_nodes = true; else G.sel_elems = true; }   /* never nothing */
    bool vis = G.sel_visible, mx = G.sel_mark_max, mn = G.sel_mark_min;
    tip(ctx, "Only the side facing you: nodes on faces turned toward the camera, elements with such a face.\n"
             "Off: everything inside the box, through the model");
    nk_checkbox_label(ctx, "facing side", &G.sel_visible);
    row_head(ctx, s, row, "mark", 3);
    tip(ctx, "A red ball on the selection's maximum");
    nk_checkbox_label(ctx, "max", &G.sel_mark_max);
    tip(ctx, "A blue ball on the selection's minimum (compression, the cold spot); the value is in the probe either way");
    nk_checkbox_label(ctx, "min", &G.sel_mark_min);
    nk_label(ctx, "", NK_TEXT_LEFT);
    if (e != G.sel_elems || n != G.sel_nodes || vis != G.sel_visible) app_box_reselect();
    else if (mx != G.sel_mark_max || mn != G.sel_mark_min) app_sel_refresh();
}

/* a tool button: lit while armed, a click arms it or puts it down */
static void tool(struct nk_context* ctx, const char* label, int t, const char* tiptext) {
    nk_bool on = G.sel_tool == t;
    tip(ctx, tiptext);
    if (nk_selectable_label(ctx, label, NK_TEXT_CENTERED, &on)) G.sel_tool = on ? t : CV_ST_NONE;
}

static void tools_rows(struct nk_context* ctx, float s, float row) {
    row_head(ctx, s, row, "pick", 3);
    tip(ctx, "Drag a box in the view next (Ctrl+Shift+drag at any time): left to right the elements\n"
             "wholly inside, right to left every one touched; it goes in by the mode");
    nk_bool box = G.box_arm;
    if (nk_selectable_label(ctx, G.box_arm ? "box: drag..." : "box", NK_TEXT_CENTERED, &box)) G.box_arm = box;
    tool(ctx, "click", CV_ST_CLICK, "Click elements (nodes) in the view: each one goes in by the mode;\n"
                                     "with 'select' a click on a selected one takes it out. Esc puts the tool down");
    tool(ctx, "lasso", CV_ST_LASSO, "Draw round what you want with the left button held: the elements wholly inside\n"
                                     "(nodes inside, when ticked), by the mode. Esc puts the tool down");
    row_head(ctx, s, row, "", 3);
    tool(ctx, "faces", CV_ST_FACE, "Click an outer face: every face reached from it without crossing a feature edge\n"
                                    "(the outline's crease angle, Layers > Outline): their elements and nodes");
    tool(ctx, "edge chain", CV_ST_CHAIN, "Click near a feature edge: the nodes along it, on through smooth turns up to\n"
                                          "a corner (the outline's crease angle)");
    tool(ctx, "part", CV_ST_PART, "Click an element: every shown element connected to it through shared nodes");
    row_head(ctx, s, row, "turn", 4);
    tip(ctx, "Invert: what is shown and not selected (elements, nodes or both, as selected or ticked)");
    if (nk_button_label(ctx, "invert")) app_sel_invert();
    tip(ctx, "Elements to nodes: the selected elements' nodes, the elements dropped");
    if (nk_button_label(ctx, "nodes")) app_sel_to_nodes();
    tip(ctx, "Nodes to elements: the shown elements with every node selected, the nodes dropped");
    if (nk_button_label(ctx, "elements")) app_sel_to_elems(false);
    tip(ctx, "Nodes to elements: the shown elements with any node selected, the nodes dropped");
    if (nk_button_label(ctx, "touching")) app_sel_to_elems(true);
    row_head(ctx, s, row, "layer", 3);
    tip(ctx, "Grow: one layer more, the shown elements (nodes) sharing an element's node with the selection");
    if (nk_button_label(ctx, "grow")) app_sel_grow(false);
    tip(ctx, "Shrink: one layer less, without the elements (nodes) touching what is shown and not selected");
    if (nk_button_label(ctx, "shrink")) app_sel_grow(true);
    tip(ctx, "Boundary: the nodes on the outside of the selected elements (faces no other selected element shares),\n"
             "and the elements that have such a face");
    if (nk_button_label(ctx, "boundary")) app_sel_boundary();
}

/* the deck's sets and surfaces, the element types and materials: a list, the mode's button */
static void names_row(struct nk_context* ctx, float s, float row) {
    enum { NN = 256 };
    static char key[NN][72], label[NN][80];
    static int cur;
    int n = 0;
    const cv_inp* d = deck_get();
    for (int pass = 0; pass < 2; pass++)                 /* element sets first */
        for (int i = 0; d && i < d->nsets && n < NN; i++) {
            if (d->sets[i].is_elem != !pass) continue;
            snprintf(key[n], 72, "set:%s", d->sets[i].name);
            snprintf(label[n], 80, "%s set %s (%u)", d->sets[i].is_elem ? "element" : "node", d->sets[i].name, d->sets[i].n);
            n++;
        }
    for (int i = 0; d && i < d->nsurfs && n < NN; i++, n++) {
        snprintf(key[n], 72, "surf:%s", d->surfs[i].name);
        snprintf(label[n], 80, "surface %s", d->surfs[i].name);
    }
    for (int a = CV_AXIS_TYPE; a <= CV_AXIS_MAT; a++) {
        const cv_axis* ax = &G.groups.axis[a];
        for (int i = 0; i < ax->n && n < NN; i++, n++) {
            uint32_t v = ax->value[i];
            const char* mn = a == CV_AXIS_MAT ? deck_material_name(v) : NULL;
            if (a == CV_AXIS_TYPE) snprintf(key[n], 72, "type:%s", cv_frd_type_name((int)v));
            else if (mn) snprintf(key[n], 72, "mat:%s", mn);
            else snprintf(key[n], 72, "mat:%u", v);
            snprintf(label[n], 80, "%s %s (%u)", a == CV_AXIS_TYPE ? "type" : "material", key[n] + (a == CV_AXIS_TYPE ? 5 : 4), ax->count[i]);
        }
    }
    if (cur >= n) cur = 0;
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 70 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 80 * s);
    nk_layout_row_template_end(ctx);
    nk_label_colored(ctx, "by name", NK_TEXT_LEFT, P.dim);
    tip(ctx, "A deck element or node set, a surface (its elements and the nodes of its faces),\n"
             "every element of a type or a material");
    if (n && nk_combo_begin_label(ctx, label[cur], nk_vec2(300 * s, CV_MIN(n, 14) * (row + 4 * s) + 20 * s))) {
        nk_layout_row_dynamic(ctx, row, 1);
        for (int i = 0; i < n; i++) if (nk_combo_item_label(ctx, label[i], NK_TEXT_LEFT)) cur = i;
        nk_combo_end(ctx);
    } else if (!n) nk_label_colored(ctx, "no sets", NK_TEXT_LEFT, P.dim);
    tip(ctx, "Take it by the mode: select, add, remove or intersect");
    if (nk_button_label(ctx, mode_verb[G.sel_mode]) && n) app_sel_by_name(key[cur], G.sel_mode);
}

/* an id list: "1-100, 205" */
static char ids_buf[512];
static int ids_nodes;
void uii_select_ids(const char* text, bool nodes) { snprintf(ids_buf, sizeof ids_buf, "%s", text); ids_nodes = nodes; }

static void ids_row(struct nk_context* ctx, float s, float row) {
    char* buf = ids_buf;
    int nodes = ids_nodes;
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 70 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 90 * s);
    nk_layout_row_template_push_static(ctx, 80 * s);
    nk_layout_row_template_end(ctx);
    nk_label_colored(ctx, "by id", NK_TEXT_LEFT, P.dim);
    tip(ctx, "Ids as you would write them: 1-100, 205, 300-310 (spaces, commas or semicolons; Enter takes them)");
    nk_flags ev = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, buf, (int)sizeof ids_buf, nk_filter_default);
    static const char* kinds[] = { "elements", "nodes" };
    tip(ctx, "Whose ids these are");
    ids_nodes = nodes = nk_combo(ctx, kinds, 2, nodes, (int)row, nk_vec2(110 * s, 2 * row + 20 * s));
    tip(ctx, "Take them by the mode: select, add, remove or intersect");
    if (nk_button_label(ctx, mode_verb[G.sel_mode]) || (ev & NK_EDIT_COMMITED)) app_sel_by_ids(buf, nodes == 1, G.sel_mode);
}

void uii_window_select(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_select || !G.loaded) { if (was_open) G.sel_tool = CV_ST_NONE; was_open = false; return; }
    if (!was_open) { nk_window_show(ctx, "Selection", NK_SHOWN); G.sel_note[0] = 0; }
    was_open = true;
    float lines = 13.f;                                  /* the rows below, the title and padding */
    float w = CV_MIN(430 * s, fw * 0.5f), h = CV_MIN(lines * (row + ctx->style.window.spacing.y) + 2.6f * row, fh * 0.8f);
    if (nk_begin(ctx, "Selection", nk_rect(G.vp_x + 10 * s, G.vp_y + 10 * s, w, h),   /* top left: the legend is top right */
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        summary(ctx, s, row);
        mode_row(ctx, s, row);
        takes_rows(ctx, s, row);
        tools_rows(ctx, s, row);
        names_row(ctx, s, row);
        ids_row(ctx, s, row);
        nk_layout_row_dynamic(ctx, row, 1);
        if (G.sel_note[0]) { tip(ctx, G.sel_note); nk_label_colored(ctx, G.sel_note, NK_TEXT_LEFT, P.warn); }
        else if (G.sel_tool != CV_ST_NONE)
            nk_label_colored(ctx, G.sel_tool == CV_ST_LASSO ? "draw in the view (Esc: done)" : "click in the view (Esc: done)", NK_TEXT_LEFT, P.accent);
        else nk_label(ctx, "", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "The field integrated over the selected elements at every step (volume integral and average),\n"
                 "or summed over the selected nodes (reaction forces)");
        if (nk_button_label(ctx, "integrate...") && (G.sel_n || G.seln_n)) app_integ_open(G.sel_n ? CV_IK_VOLUME : CV_IK_NODES, "selection");
        tip(ctx, "Close (S)");
        if (nk_button_label(ctx, "close")) G.show_select = false;
    }
    if (nk_window_is_hidden(ctx, "Selection")) G.show_select = false;
    nk_end(ctx);
}
