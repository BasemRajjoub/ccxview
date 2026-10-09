/* ui_select.c -- the Selection window (S): what is selected and its extremes, how
   what is picked next goes with it (new / add / remove / intersect), what a pick
   takes (elements, nodes, the side facing the eye), the tools (box, click, lasso,
   faces up to the feature edges, a chain of feature edges, the connected part),
   the steps on the selection itself (invert, to nodes, to elements, grow, shrink,
   boundary), filters (a range of x y z or r theta z, the field, a type, a
   material, the facing side), deck sets,
   surfaces, types and materials by name, an id list, and what is done with it
   (hide, isolate, crop, clip, CSV, deck lines, labels, kept by name, its history).
   app_select.c, app_seltools.c, app_selfilter.c and app_seluse.c do the work. */
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
    char sets[400];                                    /* always a line, so the rows below stay where they are */
    sets[0] = 0;
    if (G.sel_n || G.seln_n) app_sel_sets_overlap(sets, sizeof sets);
    snprintf(t, sizeof t, "%s%s", sets[0] ? "in sets: " : "", sets);
    tip(ctx, "The deck sets it shares members with: how many of each set's are selected");
    nk_label_colored(ctx, t, NK_TEXT_LEFT, P.dim);
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

/* ---- filters: keep what passes of the selection (of everything shown when it is empty) ---- */

static const char* filter_verb(void) {
    static const char* v[CV_SEL_MODES] = { "keep", "add", "drop", "keep" };
    return v[G.sel_mode];
}

/* the range a coordinate spans over the model, to start from */
static void coord_span(const cv_selfilter* q, float* lo, float* hi) {
    const float b0[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, b1[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
    if (q->coord <= CV_SC_Z) { *lo = b0[q->coord]; *hi = b1[q->coord]; return; }
    if (q->coord == CV_SC_THETA) { *lo = -180; *hi = 180; return; }
    *lo = INFINITY; *hi = -INFINITY;
    for (int c = 0; c < 8; c++) {                      /* the box's corners */
        float p[3] = { c & 1 ? b1[0] : b0[0], c & 2 ? b1[1] : b0[1], c & 4 ? b1[2] : b0[2] };
        float v = cv_selfilter_coord(q, p);
        *lo = CV_MIN(*lo, v); *hi = CV_MAX(*hi, v);
    }
    if (q->coord == CV_SC_R) *lo = 0;
}

static int filter_rows(struct nk_context* ctx, float s, float row) {
    static cv_selfilter q = { .axis = 2 };
    static int coord = -1, field_kind;
    static float value = 0, top = 5;
    static int tm;
    int rows = 0;
    if (coord != q.coord || !(q.hi >= q.lo)) { coord = q.coord; coord_span(&q, &q.lo, &q.hi); }
    float step = (q.hi - q.lo) * 0.01f + 1e-6f;
    /* a coordinate in a range */
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 70 * s);
    nk_layout_row_template_push_static(ctx, 70 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 60 * s);
    nk_layout_row_template_end(ctx);
    nk_label_colored(ctx, "where", NK_TEXT_LEFT, P.dim);
    static const char* coords[CV_SC_N] = { "x", "y", "z", "r", "theta", "axial" };
    tip(ctx, "The coordinate: x y z, or r, theta (degrees) and the axial one about the axis below (undeformed;\n"
             "an element by its centre)");
    q.coord = nk_combo(ctx, coords, CV_SC_N, q.coord, (int)row, nk_vec2(110 * s, CV_SC_N * row + 20 * s));
    tip(ctx, "From");
    q.lo = nk_propertyf(ctx, "#from", -1e30f, q.lo, 1e30f, step, step * 0.2f);
    tip(ctx, "To");
    q.hi = nk_propertyf(ctx, "#to", -1e30f, q.hi, 1e30f, step, step * 0.2f);
    tip(ctx, "Keep what lies in this range (add: what lies in it anywhere joins; remove: it leaves)");
    if (nk_button_label(ctx, filter_verb())) { cv_selfilter f = q; f.kind = CV_SQ_COORD; app_sel_filter(&f, G.sel_mode); }
    rows++;
    if (q.coord >= CV_SC_R) {                          /* the axis r, theta and the axial coordinate are about */
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 70 * s);
        nk_layout_row_template_push_static(ctx, 70 * s);
        for (int k = 0; k < 3; k++) nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        nk_label_colored(ctx, "about", NK_TEXT_LEFT, P.dim);
        static const char* axes[3] = { "X axis", "Y axis", "Z axis" };
        tip(ctx, "The axis, through the point beside it; theta turns from the next axis (X: from Y, Y: from Z, Z: from X)");
        int a = nk_combo(ctx, axes, 3, q.axis, (int)row, nk_vec2(110 * s, 3 * row + 20 * s));
        if (a != q.axis) { q.axis = a; coord_span(&q, &q.lo, &q.hi); }
        static const char* nm[3] = { "#x", "#y", "#z" };
        float st = G.diag * 0.01f + 1e-6f;
        for (int k = 0; k < 3; k++) { tip(ctx, "The point the axis goes through"); q.at[k] = nk_propertyf(ctx, nm[k], -1e30f, q.at[k], 1e30f, st, st * 0.2f); }
        rows++;
    }
    /* the field */
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 70 * s);
    nk_layout_row_template_push_static(ctx, 100 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 60 * s);
    nk_layout_row_template_end(ctx);
    nk_label_colored(ctx, "field", NK_TEXT_LEFT, P.dim);
    static const char* fk[3] = { "above", "below", "top %" };
    tip(ctx, "The field shown: above or below a value, or its top N % (an element by its highest node, its lowest for below)");
    field_kind = nk_combo(ctx, fk, 3, field_kind, (int)row, nk_vec2(120 * s, 3 * row + 20 * s));
    float* v = field_kind == 2 ? &top : &value;
    float vs = field_kind == 2 ? 1.f : (G.data_max - G.data_min) * 0.01f + 1e-6f;
    tip(ctx, field_kind == 2 ? "Percent of the entries, the highest values" : "The value");
    *v = nk_propertyf(ctx, field_kind == 2 ? "#%" : "#value", field_kind == 2 ? 0.f : -1e30f, *v, field_kind == 2 ? 100.f : 1e30f, vs, vs * 0.2f);
    tip(ctx, "Keep what the field puts there (add: from everything shown; remove: drop it)");
    if (nk_button_label(ctx, filter_verb())) {
        cv_selfilter f = { .kind = field_kind == 0 ? CV_SQ_ABOVE : field_kind == 1 ? CV_SQ_BELOW : CV_SQ_TOP, .value = *v };
        app_sel_filter(&f, G.sel_mode);
    }
    rows++;
    /* an element type or a material */
    enum { NT = 128 };
    const char* names[NT];
    static char lab[NT][64];
    int kind[NT], nt = 0;
    uint32_t code[NT];
    for (int a = CV_AXIS_TYPE; a <= CV_AXIS_MAT; a++)
        for (int i = 0; i < G.groups.axis[a].n && nt < NT; i++, nt++) {
            uint32_t c = G.groups.axis[a].value[i];
            const char* mn = a == CV_AXIS_MAT ? deck_material_name(c) : NULL;
            if (a == CV_AXIS_TYPE) snprintf(lab[nt], 64, "type %s", cv_frd_type_name((int)c));
            else if (mn) snprintf(lab[nt], 64, "material %s", mn);
            else snprintf(lab[nt], 64, "material %u", c);
            names[nt] = lab[nt]; kind[nt] = a == CV_AXIS_TYPE ? CV_SQ_TYPE : CV_SQ_MAT; code[nt] = c;
        }
    if (tm >= nt) tm = 0;
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 70 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 60 * s);
    nk_layout_row_template_end(ctx);
    nk_label_colored(ctx, "of", NK_TEXT_LEFT, P.dim);
    tip(ctx, "An element type or a material");
    if (nt) tm = nk_combo(ctx, names, nt, tm, (int)row, nk_vec2(220 * s, CV_MIN(nt, 10) * row + 20 * s));
    else nk_label(ctx, "", NK_TEXT_LEFT);
    tip(ctx, "Keep the elements of this type or material (the nodes stay)");
    if (nk_button_label(ctx, filter_verb()) && nt) { cv_selfilter f = { .kind = kind[tm], .code = code[tm] }; app_sel_filter(&f, G.sel_mode); }
    rows++;
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 70 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 60 * s);
    nk_layout_row_template_end(ctx);
    nk_label_colored(ctx, "facing", NK_TEXT_LEFT, P.dim);
    nk_label(ctx, "the side facing you now", NK_TEXT_LEFT);
    tip(ctx, "Keep the side facing the camera now: nodes on faces turned toward it, elements with such a face");
    if (nk_button_label(ctx, filter_verb())) app_sel_filter_facing(G.sel_mode);
    return rows + 1;
}

/* ---- using it: hide, isolate, crop, clip; CSV, deck lines, labels; kept by name ---- */

static char name_buf[48];
void uii_select_name(const char* text) { snprintf(name_buf, sizeof name_buf, "%s", text); }

static void use_rows(struct nk_context* ctx, float s, float row) {
    bool any = G.sel_n || G.seln_n;
    row_head(ctx, s, row, "show", 4);
    tip(ctx, "Hide the selected elements (right click > Show all brings them back)");
    if (nk_button_label(ctx, "hide") && G.sel_n) app_hide_elems(G.sel, G.sel_n);
    tip(ctx, "Show only the selected elements");
    if (nk_button_label(ctx, "isolate") && G.sel_n) app_isolate_elems(G.sel, G.sel_n);
    tip(ctx, "The crop box round the selection (View > Crop box: its edges, reset)");
    if (nk_button_label(ctx, "crop") && any) app_sel_crop();
    tip(ctx, "The clip plane through the selection's centre, across the clip axis (View > Clip)");
    if (nk_button_label(ctx, "clip") && any) app_sel_clip();
    row_head(ctx, s, row, "out", 4);
    tip(ctx, "The selected elements (or nodes) with their place and values to <model>_selection.csv");
    if (nk_button_label(ctx, "CSV") && any) app_sel_csv();
    const char* nm = name_buf[0] ? name_buf : "SELECTION";
    tip(ctx, "*ELSET / *NSET lines of the selection to the clipboard, named as the box below says");
    if (nk_button_label(ctx, "copy sets") && any) {
        char* t = app_sel_inp_text(nm);
        if (t) { sapp_set_clipboard_string(t); snprintf(G.sel_note, sizeof G.sel_note, "*ELSET / *NSET %s copied", nm); }
        free(t);
    }
    char path[1200];
    app_sel_inp_path(nm, path, sizeof path);
    char tt[1300];
    snprintf(tt, sizeof tt, "The same lines to %s, to *INCLUDE in a deck\n(a file of its own: the model's input is not touched)", path);
    tip(ctx, tt);
    if (nk_button_label(ctx, "save .inp") && any) app_sel_inp_save(nm);
    nk_bool lo = G.label_sel_only;
    tip(ctx, "Labels on the selection only (Fields > Labels: what they show; node ids when none is chosen)");
    if (nk_checkbox_label(ctx, "labels", &lo)) {
        G.label_sel_only = lo;
        if (lo && !G.label_kinds) G.label_kinds = 1 << CV_LABEL_NODE;
        app_label_changed();
    }
}

static int named_rows(struct nk_context* ctx, float s, float row) {
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 70 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 80 * s);
    nk_layout_row_template_end(ctx);
    nk_label_colored(ctx, "keep as", NK_TEXT_LEFT, P.dim);
    tip(ctx, "A name (letters, digits, _ -): to keep the selection by, for the deck lines, to rename one below");
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, name_buf, (int)sizeof name_buf, nk_filter_default);
    tip(ctx, "Keep the selection under this name, with the model (its .ccxview): back when the model opens");
    if (nk_button_label(ctx, "keep")) app_nsel_save(name_buf);
    int n = app_nsel_count(), drop = -1;
    for (int i = 0; i < n; i++) {
        const cv_namedsel* q = app_nsel_at(i);
        char lab[96], mk[80];
        snprintf(lab, sizeof lab, "%s  (%u el, %u nd)", q->name, q->ne, q->nn);
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_dynamic(ctx);
        for (int k = 0; k < 5; k++) nk_layout_row_template_push_static(ctx, 34 * s);
        nk_layout_row_template_end(ctx);
        nk_label(ctx, lab, NK_TEXT_LEFT);
        static const char* bl[5] = { "sel", "add", "rem", "ren", "del" };
        static const char* bt[5] = { "Select it", "Add it to the selection", "Remove it from the selection",
                                     "Rename it to the name in the box above", "Forget it" };
        for (int k = 0; k < 5; k++) {
            snprintf(mk, sizeof mk, "#named %s %s", bl[k], q->name);
            uii_test_mark(ctx, mk);
            tip(ctx, bt[k]);
            if (!nk_button_label(ctx, bl[k])) continue;
            if (k < 3) app_nsel_take(i, k == 0 ? CV_SEL_NEW : k == 1 ? CV_SEL_ADD : CV_SEL_REMOVE);
            else if (k == 3 && !app_nsel_rename(i, name_buf)) snprintf(G.sel_note, sizeof G.sel_note, "rename: a new name in the box first");
            else if (k == 4) drop = i;
        }
    }
    if (drop >= 0) app_nsel_delete(drop);
    return 1 + n;
}

void uii_window_select(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_select || !G.loaded) { if (was_open) G.sel_tool = CV_ST_NONE; was_open = false; return; }
    if (!was_open) { nk_window_show(ctx, "Selection", NK_SHOWN); G.sel_note[0] = 0; }
    was_open = true;
    /* as tall as its rows, again when filters open or close or a selection is kept or forgotten */
    static int shape = -1;
    int nk = CV_MIN(app_nsel_count(), 5), now = (G.sel_filters ? 100 : 0) + nk;
    float lines = 18.f + (G.sel_filters ? 5.f : 0.f) + nk;
    float w = CV_MIN(430 * s, fw * 0.5f), h = CV_MIN(lines * (row + ctx->style.window.spacing.y) + 2.6f * row, fh * 0.85f);
    if (shape >= 0 && shape != now && nk_window_find(ctx, "Selection"))
        nk_window_set_size(ctx, "Selection", nk_vec2(nk_window_find(ctx, "Selection")->bounds.w, h));
    shape = now;
    if (nk_begin(ctx, "Selection", nk_rect(G.vp_x + 10 * s, G.vp_y + 10 * s, w, h),   /* top left: the legend is top right */
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        summary(ctx, s, row);
        mode_row(ctx, s, row);
        takes_rows(ctx, s, row);
        tools_rows(ctx, s, row);
        names_row(ctx, s, row);
        ids_row(ctx, s, row);
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "Filters: keep what lies in a range of x y z (or r theta z about an axis), what the field puts above\n"
                 "or below a value or in its top N %, a type, a material, the side facing you; of the selection, or\n"
                 "of everything shown when nothing is selected");
        nk_checkbox_label(ctx, "filters", &G.sel_filters);
        if (G.sel_filters) filter_rows(ctx, s, row);
        nk_layout_row_dynamic(ctx, row, 1);
        if (G.sel_note[0]) { tip(ctx, G.sel_note); nk_label_colored(ctx, G.sel_note, NK_TEXT_LEFT, P.warn); }
        else if (G.sel_tool != CV_ST_NONE)
            nk_label_colored(ctx, G.sel_tool == CV_ST_LASSO ? "draw in the view (Esc: done)" : "click in the view (Esc: done)", NK_TEXT_LEFT, P.accent);
        else nk_label(ctx, "", NK_TEXT_LEFT);
        use_rows(ctx, s, row);
        named_rows(ctx, s, row);
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Its history: the field integrated over the selected elements at every step (volume integral and\n"
                 "average), or summed over the selected nodes (reaction forces), plotted over the steps");
        if (nk_button_label(ctx, "history (integrals)...") && (G.sel_n || G.seln_n)) app_integ_open(G.sel_n ? CV_IK_VOLUME : CV_IK_NODES, "selection");
        tip(ctx, "Close (S)");
        if (nk_button_label(ctx, "close")) G.show_select = false;
    }
    if (nk_window_is_hidden(ctx, "Selection")) G.show_select = false;
    nk_end(ctx);
}
