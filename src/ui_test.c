/* ui_test.c -- the interface driven by a script (--ui-test DIR), the way a user
   would: mouse moves, presses and wheel turns sent as sokol events through the
   app's own event handler, one step per frame, then what happened is checked.
   The script finds a widget by its tooltip (tip() reports where each one is),
   a window by its name. After every frame it also checks what must hold at any
   time: a press on a window makes that window the active one, able to take input.
   A failed case saves the window as DIR/ui-test-N.png. scripts/ui-test.sh runs it. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include <stdarg.h>
#include "ui_int.h"
#include "app_mesh.h"

enum { OP_END, OP_CASE, OP_AT_TIP, OP_AT_WIN, OP_AT_POPUP, OP_AT_CLOSE, OP_AT_TITLE, OP_AT_VIEW, OP_AT_MODEL, OP_CLICK, OP_PRESS, OP_RELEASE,
       OP_WHEEL, OP_WAIT, OP_DO, OP_EXPECT };

typedef struct {
    int op;
    const char* a; const char* b;       /* window / case name, tooltip / what is expected */
    float x, y;                         /* fractions of the window, popup or view; wheel turns */
    int n;                              /* frames, mouse button */
    void (*fn)(struct nk_context*);
    bool (*ok)(struct nk_context*);
} step;

/* ---- where the widgets are: tip() reports each one it is called for ------------- */
typedef struct { char win[32], text[96]; struct nk_rect r; } mark;
static mark marks[768];
static int n_marks;

static struct {
    bool on, done;
    const char* dir;
    void (*send)(const sapp_event*);
    int pc, wait, phase;                /* step, frames left to wait, phase of a click */
    bool resetting;                     /* a case's start is closing a list left open */
    float mx, my;
    bool down[3];
    const char* name;                   /* the running case */
    int cases, failed, shots;
    bool case_failed, shot;
    char shot_path[1200];
    bool pressed; float px, py;         /* a press sent last frame: the window under it must be active now */
    unsigned windows;                   /* how many windows there were before it */
    uint64_t frame;
} T;

void uii_test_mark(struct nk_context* ctx, const char* text) {
    if (!T.on || n_marks >= (int)(sizeof marks / sizeof marks[0]) || !ctx->current) return;
    mark* m = &marks[n_marks++];
    snprintf(m->win, sizeof m->win, "%s", ctx->current->name_string);
    snprintf(m->text, sizeof m->text, "%s", text);
    m->r = nk_widget_bounds(ctx);
}

static const mark* find_mark(const char* win, const char* text) {
    size_t n = CV_MIN(strlen(text), sizeof marks[0].text - 1);
    for (int i = 0; i < n_marks; i++)
        if ((!win || !strcmp(marks[i].win, win)) && !strncmp(marks[i].text, text, n)) return &marks[i];
    return NULL;
}

/* ---- events ---------------------------------------------------------------------- */
static void send(sapp_event_type type, int button, float scroll) {
    sapp_event ev = { .type = type, .frame_count = T.frame, .mouse_button = (sapp_mousebutton)button,
                      .mouse_x = T.mx, .mouse_y = T.my, .scroll_y = scroll,
                      .window_width = sapp_width(), .window_height = sapp_height(),
                      .framebuffer_width = sapp_width(), .framebuffer_height = sapp_height() };
    T.send(&ev);
}
static void move_to(float x, float y) { T.mx = roundf(x); T.my = roundf(y); send(SAPP_EVENTTYPE_MOUSE_MOVE, 0, 0); }
static void press(int b) { if (!T.down[b]) { T.down[b] = true; send(SAPP_EVENTTYPE_MOUSE_DOWN, b, 0); T.pressed = b == 0; T.px = T.mx; T.py = T.my; } }
static void release(int b) { if (T.down[b]) { T.down[b] = false; send(SAPP_EVENTTYPE_MOUSE_UP, b, 0); } }

/* the window list, bottom to top, with what a failure usually turns on */
static void dump_windows(struct nk_context* ctx) {
    fprintf(stderr, "      mouse %.0f,%.0f  windows:", T.mx, T.my);
    for (struct nk_window* w = ctx->begin; w; w = w->next)
        fprintf(stderr, " %s%s%s%s%s%s%s", w->name_string, w == ctx->active ? "[active]" : "", w == ctx->end ? "[top]" : "",
                w->flags & NK_WINDOW_HIDDEN ? "[hidden]" : "", w->flags & NK_WINDOW_ROM ? "[read only]" : "",
                w->flags & NK_WINDOW_BACKGROUND ? "[bg]" : "", w->popup.win && w->popup.active ? "[popup]" : "");
    fprintf(stderr, "\n");
}

static struct nk_context* g_ctx;
static void fail(const char* fmt, ...) {
    char msg[400];
    va_list ap;
    va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    fprintf(stderr, "FAIL  %s: %s\n", T.name ? T.name : "(start)", msg);
    if (!T.case_failed) {
        T.case_failed = true; T.failed++;
        snprintf(T.shot_path, sizeof T.shot_path, "%s/ui-test-%d.png", T.dir, ++T.shots);
        T.shot = true;
        fprintf(stderr, "      screenshot: %s\n", T.shot_path);
        if (g_ctx) dump_windows(g_ctx);
    }
}

/* ---- what the script looks at ---------------------------------------------------- */
static struct nk_window* win_of(struct nk_context* ctx, const char* name) {
    struct nk_window* w = nk_window_find(ctx, name);
    return w && !(w->flags & (NK_WINDOW_HIDDEN | NK_WINDOW_CLOSED)) ? w : NULL;
}
static bool popup_open(struct nk_context* ctx, const char* name) {
    struct nk_window* w = win_of(ctx, name);
    return w && w->popup.win && w->popup.active && w->popup.type != NK_PANEL_TOOLTIP;
}
static bool any_popup(struct nk_context* ctx) {
    for (struct nk_window* w = ctx->begin; w; w = w->next)
        if (!(w->flags & NK_WINDOW_HIDDEN) && w->popup.win && w->popup.active && w->popup.type != NK_PANEL_TOOLTIP) return true;
    return false;
}
/* the topmost window taking input under a point, NULL over the 3D view or a popup */
static struct nk_window* input_window_at(struct nk_context* ctx, float x, float y) {
    for (struct nk_window* w = ctx->begin; w; w = w->next) {
        const struct nk_window* p = w->popup.win && w->popup.active ? w->popup.win : NULL;
        if (p && !(w->flags & NK_WINDOW_HIDDEN) && NK_INBOX(x, y, p->bounds.x, p->bounds.y, p->bounds.w, p->bounds.h)) return NULL;
    }
    for (struct nk_window* w = ctx->end; w; w = w->prev) {
        if (w->flags & (NK_WINDOW_HIDDEN | NK_WINDOW_CLOSED | NK_WINDOW_NO_INPUT)) continue;
        if (NK_INBOX(x, y, w->bounds.x, w->bounds.y, w->bounds.w, w->bounds.h)) return w;
    }
    return NULL;
}

/* Holds after every frame, whatever the script does: a press on a window leaves
   that window active and taking input. Not when the press opened a list in it (the
   window is read only under its own list) or opened a new window (that one is active). */
static void check_always(struct nk_context* ctx) {
    if (T.pressed) {
        struct nk_window* w = input_window_at(ctx, T.px, T.py);
        bool list = w && w->popup.win && w->popup.active;
        bool opened = ctx->count > T.windows;
        if (w && !list && !opened && !(w->flags & NK_WINDOW_BACKGROUND) && (ctx->active != w || (w->flags & NK_WINDOW_ROM)))
            fail("a press at %.0f,%.0f on '%s' left it %s (active: '%s')", T.px, T.py, w->name_string,
                 ctx->active != w ? "inactive" : "read only", ctx->active ? ctx->active->name_string : "none");
    }
    T.pressed = false;
    T.windows = ctx->count;
}

/* ---- the script's own state and checks ------------------------------------------- */
static struct { int cmap, bands, faces_mode, units, cyc_axis, tensor_style, traj_which; bool deform, markers, edges; float dist, scroll; cv_camera cam; } was;

static void close_all(struct nk_context* ctx) {
    G.show_units = G.show_msgs = G.show_calc_help = G.show_conv = G.legend_edit = G.find_open = G.browser_open = false;
    G.hist_open = G.path_open = false;
}
static void snapshot(struct nk_context* ctx) {
    was.cmap = G.cmap; was.bands = G.bands; was.faces_mode = G.faces_mode; was.units = G.units; was.cyc_axis = G.cyc_axis;
    was.tensor_style = G.tensor_style; was.traj_which = G.traj_which;
    was.deform = G.deform; was.markers = G.show_markers; was.edges = G.show_edges;
    was.cam = G.cam;
    struct nk_window* w = win_of(ctx, "Scene");
    was.scroll = w ? (float)w->scrollbar.y : 0;
}
static void open_units(struct nk_context* ctx) { G.show_units = true; }
static void open_messages(struct nk_context* ctx) { G.show_msgs = true; }
static void open_formula(struct nk_context* ctx) { G.show_calc_help = true; }
static void open_convergence(struct nk_context* ctx) { G.show_conv = true; }
static void open_legend_settings(struct nk_context* ctx) { G.legend_edit = true; }
static void open_find(struct nk_context* ctx) { G.find_open = true; }
static void open_browser(struct nk_context* ctx) { G.browser_open = true; }
static void open_mesh(struct nk_context* ctx) { G.show_mesh = true; }
static bool mesh_gone(struct nk_context* ctx) { return !G.show_mesh; }
static bool mesh_limit_set(struct nk_context* ctx) { return G.mq_lim[CV_MQ_ASPECT] != 0; }
static void mesh_limits_usual(struct nk_context* ctx) { memset(G.mq_lim, 0, sizeof G.mq_lim); G.show_mesh = false; }
static void sections_open(struct nk_context* ctx) {
    G.tree[CV_TREE_LAYERS] = G.tree[CV_TREE_VIEW] = G.tree[CV_TREE_CAMERA] = G.tree[CV_TREE_COLOURS] = 1;
    G.tree[CV_TREE_CYCLIC] = G.tree[CV_TREE_DISPLAY] = 1;
}

static bool toolbar_popup(struct nk_context* ctx) { return popup_open(ctx, "Toolbar"); }
static bool toolbar_no_popup(struct nk_context* ctx) { return !popup_open(ctx, "Toolbar"); }
static bool scene_popup(struct nk_context* ctx) { return popup_open(ctx, "Scene"); }
static bool units_popup(struct nk_context* ctx) { return popup_open(ctx, "Units"); }
static bool cmap_changed(struct nk_context* ctx) { return G.cmap != was.cmap; }
static bool bands_changed(struct nk_context* ctx) { return G.bands != was.bands; }
static bool faces_changed(struct nk_context* ctx) { return G.faces_mode != was.faces_mode; }
static void select_stress(struct nk_context* ctx) { app_select("STRESS", CV_COMP_MISES); G.tensor_style = 0; }
static bool tensor_picked(struct nk_context* ctx) { return G.tensor_style != was.tensor_style && G.show_tensor; }
static void tensor_small(struct nk_context* ctx) { G.tensor_scale = 0.2f; }
static bool tensor_resized(struct nk_context* ctx) { return G.tensor_scale > 0.5f; }
static bool traj_picked(struct nk_context* ctx) { return G.traj_which != was.traj_which && G.show_traj; }
static void traj_off(struct nk_context* ctx) { G.show_traj = false; G.traj_which = 0; app_traj_changed(); }
static void tensor_off(struct nk_context* ctx) { G.show_tensor = false; app_tensors_changed(); }
static void scale_big(struct nk_context* ctx) { G.deform_scale = 50.f; G.deform_auto = true; }
static bool scale_true(struct nk_context* ctx) { return G.deform_scale == 1.f && !G.deform_auto && G.deform; }
static bool box_armed(struct nk_context* ctx) { return G.box_arm; }
static void arm_box(struct nk_context* ctx) { G.box_arm = true; }
static bool nodes_selected(struct nk_context* ctx) { return G.sel_nodes && G.seln_n > 0 && G.boxq.on; }
static void nodes_off(struct nk_context* ctx) { G.sel_nodes = false; G.sel_elems = true; }
static bool box_crossing(struct nk_context* ctx) { return G.sel_n > 0 && G.sel_crossing && G.probe_on; }
static bool box_window(struct nk_context* ctx) { return G.sel_n > 0 && !G.sel_crossing; }
static void open_details(struct nk_context* ctx) { app_probe_at(0, false); G.show_details = true; }
static bool details_gone(struct nk_context* ctx) { return !G.show_details; }
static void open_about(struct nk_context* ctx) { G.show_about = true; }
static bool about_gone(struct nk_context* ctx) { return !G.show_about; }
static bool about_shown(struct nk_context* ctx) { return G.show_about && win_of(ctx, "About"); }
static bool turned(struct nk_context* ctx);
static void fit_view(struct nk_context* ctx) { app_view(CV_VIEW_ISO); }
static bool menu_open(struct nk_context* ctx) { return G.menu_on && win_of(ctx, "Menu"); }
static bool menu_closed(struct nk_context* ctx) { return !G.menu_on; }
static bool menu_closed_turned(struct nk_context* ctx) { return !G.menu_on && turned(ctx); }
static bool one_hidden(struct nk_context* ctx) {
    uint32_t n = 0;
    for (uint32_t e = 0; G.hide && e < G.frd.n_elems; e++) n += G.hide[e];
    return n == 1 && G.vis && !G.menu_on;
}
static bool sel_hidden(struct nk_context* ctx) {
    uint32_t n = 0;
    for (uint32_t e = 0; G.hide && e < G.frd.n_elems; e++) n += G.hide[e];
    return n > 1 && !G.sel_n;
}
static bool all_shown(struct nk_context* ctx) { return !G.hide; }
static bool clipped(struct nk_context* ctx) { return G.clip_on && !G.menu_on; }
static void unclip(struct nk_context* ctx) { G.clip_on = false; }
/* the selection CSV beside the model: a header and a row per selected element */
static bool csv_saved(struct nk_context* ctx) {
    char path[1100];
    snprintf(path, sizeof path, "%s", G.path);
    char* dot = strrchr(path, '.');
    if (dot) *dot = 0;
    strncat(path, "_selection.csv", sizeof path - strlen(path) - 1);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    int lines = 0, c;
    while ((c = fgetc(f)) != EOF) lines += c == '\n';
    fclose(f);
    return lines == (int)G.sel_n + 1;
}
/* a box round the whole view: its max is the field's max, the probe on it */
static bool box_found(struct nk_context* ctx) {
    return !G.box_arm && G.boxq.on && G.probe_on && G.boxq.vmax == G.data_max && G.boxq.vmin <= G.boxq.vmax &&
           G.probe_value == G.boxq.vmax;
}
static bool deform_toggled(struct nk_context* ctx) { return G.deform != was.deform; }
static bool markers_toggled(struct nk_context* ctx) { return G.show_markers != was.markers; }
static bool edges_toggled(struct nk_context* ctx) { return G.show_edges != was.edges; }
static bool units_changed(struct nk_context* ctx) { return G.units != was.units; }
static bool units_shown(struct nk_context* ctx) { return G.show_units && win_of(ctx, "Units"); }
static bool units_over_sidebar(struct nk_context* ctx) { struct nk_window* w = win_of(ctx, "Units"); return w && w->bounds.x < G.vp_x - 50; }
static bool units_gone(struct nk_context* ctx) { return !G.show_units && !win_of(ctx, "Units"); }
static bool messages_gone(struct nk_context* ctx) { return !G.show_msgs; }
static bool formula_gone(struct nk_context* ctx) { return !G.show_calc_help; }
static bool convergence_gone(struct nk_context* ctx) { return !G.show_conv; }
static bool legend_settings_shown(struct nk_context* ctx) { return G.legend_edit && win_of(ctx, "Legend settings"); }
static bool legend_settings_gone(struct nk_context* ctx) { return !G.legend_edit; }
static bool find_gone(struct nk_context* ctx) { return !G.find_open; }
static bool browser_gone(struct nk_context* ctx) { return !G.browser_open; }
static bool zoomed(struct nk_context* ctx) { return G.cam.dist != was.cam.dist; }
static bool not_zoomed(struct nk_context* ctx) { return G.cam.dist == was.cam.dist; }
static bool turned(struct nk_context* ctx) { return memcmp(&G.cam, &was.cam, sizeof G.cam) != 0; }
static bool scene_scrolled(struct nk_context* ctx) { struct nk_window* w = win_of(ctx, "Scene"); return w && (float)w->scrollbar.y != was.scroll; }
/* pan: the point of the model under the cursor when the drag began is still under it */
static v3 grabbed; static bool grabbed_on;
/* the orbit target three times as deep, the eye where it was: the same picture, but
   the model no longer at the target's depth, as after zooming in on a detail */
static void target_far(struct nk_context* ctx) {
    v3 eye, f, r, u;
    cam_basis(&G.cam, &eye, &f, &r, &u);
    G.cam.dist *= 3;
    G.cam.target = v3_add(eye, v3_scale(f, G.cam.dist));
}
static void grab(struct nk_context* ctx) { grabbed_on = false; app_cursor_point(T.mx, T.my, &grabbed, &grabbed_on); }
static bool grab_on_model(struct nk_context* ctx) { return grabbed_on; }
static bool grab_follows(struct nk_context* ctx) {
    float sx, sy;
    return app_project(grabbed, &sx, &sy) && fabsf(sx - T.mx) < 3 && fabsf(sy - T.my) < 3;
}
/* flying, with only View > Camera open and the sidebar at its top: the flight's rows in sight */
static int fly_tree[CV_TREE_N];
static void fly_on(struct nk_context* ctx) {
    app_set_flight(true); G.fly_clip = CV_EYE_OFF;
    memcpy(fly_tree, G.tree, sizeof fly_tree);
    for (int k = 0; k < CV_TREE_N; k++) G.tree[k] = k == CV_TREE_VIEW || k == CV_TREE_CAMERA;
    struct nk_window* w = win_of(ctx, "Scene");
    if (w) w->scrollbar.y = 0;
}
static void fly_off(struct nk_context* ctx) { app_set_flight(false); G.fly_clip = CV_EYE_OFF; memcpy(G.tree, fly_tree, sizeof fly_tree); }
static bool fly_cuts(struct nk_context* ctx) { return G.fly_clip == CV_EYE_CUT; }
static bool fly_hides(struct nk_context* ctx) { return G.fly_clip == CV_EYE_HIDE; }
static void fly_depth_low(struct nk_context* ctx) { G.fly_clip_depth = 0.f; }
static bool fly_depth_up(struct nk_context* ctx) { return G.fly_clip_depth > 0.f; }
static bool scene_still(struct nk_context* ctx) { return !scene_scrolled(ctx); }
static bool tip_cmap(struct nk_context* ctx) { return !strncmp(uii_tip_shown(), "Colour map", 10); }
static bool tip_none(struct nk_context* ctx) { return !uii_tip_shown()[0]; }

#define TIP_CMAP    "Colour map for the field"
#define TIP_BANDS   "Contour bands"
#define TIP_DEFORM  "Draw the shape displaced"
#define TIP_TRUE    "True scale: the displacement as computed"
#define TIP_BOX     "Drag a box in the view next"
#define TIP_MARKERS "Balls at the field's minimum"
#define TIP_EDGES   "Edges of the exterior faces"
#define TIP_UNITS   "Input units (what the model was built in) and"
#define TIP_FLYCLIP "What lies just in front of the eye"
#define TIP_FLYDEPTH "How far ahead of the eye the cut lies"

#define CASE(name)          { OP_CASE, name }
#define AT_TIP(win, text)   { OP_AT_TIP, win, text }
/* the same, and the next step (a click) in that very frame: the mouse was not seen
   hovering there first, as after a fast move */
#define JUMP_TIP(win, text) { OP_AT_TIP, win, text, .n = 1 }
/* at a fraction fx across the widget: the arrows at a property's ends */
#define AT_TIP_X(win, text, fx) { OP_AT_TIP, win, text, fx }
#define AT_WIN(win, fx, fy) { OP_AT_WIN, win, NULL, fx, fy }
#define AT_POPUP(win, fx, fy) { OP_AT_POPUP, win, NULL, fx, fy }
#define AT_CLOSE(win)       { OP_AT_CLOSE, win }
#define AT_TITLE(win)       { OP_AT_TITLE, win }
#define AT_VIEW(fx, fy)     { OP_AT_VIEW, NULL, NULL, fx, fy }
#define AT_MODEL            { OP_AT_MODEL }
#define CLICK               { OP_CLICK }
#define RCLICK              { OP_CLICK, .n = 1 }
#define PRESS               { OP_PRESS }
#define RELEASE             { OP_RELEASE }
#define RPRESS              { OP_PRESS, .n = 1 }
#define RRELEASE            { OP_RELEASE, .n = 1 }
#define WHEEL(turns)        { OP_WHEEL, .y = turns }
#define WAIT(frames)        { OP_WAIT, .n = frames }
#define DO(f)               { OP_DO, .fn = f }
#define EXPECT(f, what)     { OP_EXPECT, NULL, what, .ok = f }

/* the panels still answer: a tick box in the toolbar and one in the sidebar toggle,
   a list in the toolbar opens and closes */
#define PANELS_ANSWER \
    DO(snapshot), JUMP_TIP("Toolbar", TIP_DEFORM), CLICK, EXPECT(deform_toggled, "the Deform box toggles"), \
    JUMP_TIP("Scene", TIP_MARKERS), CLICK, EXPECT(markers_toggled, "the Min / max box toggles"), \
    AT_TIP("Toolbar", TIP_CMAP), CLICK, EXPECT(toolbar_popup, "the colour map list opens"), \
    AT_VIEW(0.5f, 0.6f), CLICK, EXPECT(toolbar_no_popup, "a click outside closes the list")
/* a window opened, closed by its close button, and the panels after it */
#define CLOSED_THEN_PANELS(win, open, gone) \
    CASE(win ": closed by its button, the panels still answer"), \
    DO(open), WAIT(3), AT_TITLE(win), CLICK, AT_CLOSE(win), CLICK, WAIT(2), EXPECT(gone, win " is closed"), \
    PANELS_ANSWER

static const step script[] = {
    CASE("toolbar: the colour map list opens under its tooltip and picks a map"),
    DO(snapshot), AT_TIP("Toolbar", TIP_CMAP), WAIT(30), EXPECT(tip_cmap, "the tooltip shows"),
    CLICK, EXPECT(toolbar_popup, "the list opens"), EXPECT(tip_none, "no tooltip over an open list"),
    AT_POPUP("Toolbar", 0.6f, 0.55f), CLICK, EXPECT(cmap_changed, "the colour map changes"),
    EXPECT(toolbar_no_popup, "the list closes"),

    CASE("toolbar: the bands list opens and picks"),
    DO(snapshot), AT_TIP("Toolbar", TIP_BANDS), WAIT(30), CLICK, EXPECT(toolbar_popup, "the list opens"),
    AT_POPUP("Toolbar", 0.5f, 0.2f), CLICK, EXPECT(bands_changed, "the bands change"),

    CASE("toolbar: the 1:1 button sets the true scale"),
    DO(scale_big), WAIT(2), AT_TIP("Toolbar", TIP_TRUE), CLICK, EXPECT(scale_true, "the scale is 1, auto off"),

    CASE("max in a box: armed by its button, a drag over the view probes the max"),
    DO(open_legend_settings), WAIT(3), AT_TIP("Legend settings", TIP_BOX), CLICK, EXPECT(box_armed, "the next drag is a box"),
    DO(close_all), WAIT(2), AT_VIEW(0.01f, 0.01f), PRESS, AT_VIEW(0.5f, 0.5f), AT_VIEW(0.99f, 0.99f), RELEASE, WAIT(2),
    EXPECT(box_found, "the probe sits on the field's max, the box line holds max and min"),
    EXPECT(box_window, "left to right: a window selection"),

    CASE("box selection: right to left is a crossing"),
    DO(arm_box), AT_VIEW(0.6f, 0.6f), PRESS, AT_VIEW(0.5f, 0.5f), AT_VIEW(0.4f, 0.4f), RELEASE, WAIT(2),
    EXPECT(box_crossing, "a crossing selection, the probe on its max"),
    AT_TIP("Probe", "Nodes inside the box"), CLICK, WAIT(2), EXPECT(nodes_selected, "ticking nodes selects the nodes too"),
    DO(nodes_off), WAIT(2),
    AT_VIEW(0.02f, 0.5f), CLICK, PANELS_ANSWER,

    CASE("toolbar: one list after the other, no pause"),
    DO(snapshot), AT_TIP("Toolbar", TIP_CMAP), CLICK, EXPECT(toolbar_popup, "the colour map list opens"),
    AT_TIP("Toolbar", TIP_BANDS), CLICK, EXPECT(toolbar_no_popup, "a click on the other box closes it"),
    CLICK, EXPECT(toolbar_popup, "the bands list opens"),
    AT_POPUP("Toolbar", 0.5f, 0.35f), CLICK, EXPECT(bands_changed, "the bands change"),

    CASE("sidebar: a list opens and picks"),
    DO(sections_open), DO(snapshot), AT_TIP("Scene", "#faces mode"), WAIT(30), CLICK, EXPECT(scene_popup, "the faces list opens"),
    AT_POPUP("Scene", 0.5f, 0.75f), CLICK, EXPECT(faces_changed, "the faces mode changes"),

    CASE("sidebar: the tensor glyph list opens and picks"),
    DO(sections_open), DO(select_stress), WAIT(3), DO(snapshot), AT_TIP("Scene", "#tensor style"), WAIT(30), CLICK,
    EXPECT(scene_popup, "the glyph list opens"),
    AT_POPUP("Scene", 0.5f, 0.85f), CLICK, EXPECT(tensor_picked, "the style changes, the glyphs show"),
    DO(tensor_small), WAIT(2), AT_TIP("Scene", "#tensor size"), CLICK, EXPECT(tensor_resized, "the size slider answers"),
    DO(tensor_off),

    CASE("sidebar: the trajectory list opens and picks"),
    DO(sections_open), DO(select_stress), DO(traj_off), WAIT(3), DO(snapshot), AT_TIP("Scene", "#traj family"), WAIT(30), CLICK,
    EXPECT(scene_popup, "the trajectory list opens"),
    AT_POPUP("Scene", 0.5f, 0.85f), CLICK, EXPECT(traj_picked, "the family changes, the trajectories show"), DO(traj_off),

    CASE("sidebar and toolbar tick boxes"),
    PANELS_ANSWER,

    CASE("wheel: scrolls the sidebar under it, zooms the view under it"),
    DO(sections_open), WAIT(2), DO(snapshot), AT_WIN("Scene", 0.5f, 0.5f), WHEEL(-3), WAIT(2),
    EXPECT(scene_scrolled, "the sidebar scrolls"), EXPECT(not_zoomed, "the view does not zoom"),
    DO(snapshot), AT_VIEW(0.5f, 0.5f), WHEEL(-3), WAIT(2), EXPECT(zoomed, "the view zooms"),
    EXPECT(scene_still, "the sidebar does not scroll with it"),
    DO(snapshot), WHEEL(3), WAIT(2), EXPECT(scene_still, "nor on the way back"),
    AT_WIN("Scene", 0.5f, 0.5f), WHEEL(30), WAIT(2),

    CASE("view: a drag turns the model"),
    DO(snapshot), AT_VIEW(0.5f, 0.5f), PRESS, AT_VIEW(0.6f, 0.55f), AT_VIEW(0.7f, 0.6f), RELEASE, EXPECT(turned, "the camera turns"),

    CASE("view: a right drag pans, the point grabbed stays under the cursor"),
    DO(target_far), WAIT(2), AT_MODEL, DO(grab), EXPECT(grab_on_model, "the cursor is on the model"),
    RPRESS, AT_VIEW(0.45f, 0.6f), AT_VIEW(0.6f, 0.35f), EXPECT(grab_follows, "the grabbed point follows the cursor"),
    RRELEASE, DO(snapshot), AT_VIEW(0.05f, 0.05f), DO(grab), RPRESS, AT_VIEW(0.2f, 0.2f), RRELEASE,
    EXPECT(turned, "a drag off the model pans too"),

    CASE("free flight: the eye list opens and picks, its depth answers"),
    DO(fly_on), WAIT(3), AT_TIP("Scene", TIP_FLYCLIP), WAIT(30), CLICK, EXPECT(scene_popup, "the eye list opens"),
    AT_POPUP("Scene", 0.5f, 0.5f), CLICK, EXPECT(fly_cuts, "the eye cuts"),
    DO(fly_depth_low), WAIT(2), AT_TIP_X("Scene", TIP_FLYDEPTH, 0.95f), CLICK, EXPECT(fly_depth_up, "the depth answers"),
    AT_TIP("Scene", TIP_FLYCLIP), CLICK, EXPECT(scene_popup, "the eye list opens again"),
    AT_POPUP("Scene", 0.5f, 0.85f), CLICK, EXPECT(fly_hides, "the eye hides elements"), WAIT(10),
    DO(fly_off), WAIT(10), PANELS_ANSWER,

    CASE("units: opened from the status bar, a list in it, closed"),
    AT_TIP("Status", TIP_UNITS), CLICK, EXPECT(units_shown, "the Units window opens"),
    DO(snapshot), AT_TIP("Units", "#unit set"), WAIT(30), CLICK, EXPECT(units_popup, "the unit system list opens"),
    AT_POPUP("Units", 0.2f, 0.5f), CLICK, EXPECT(units_changed, "the unit system changes"),
    AT_CLOSE("Units"), CLICK, WAIT(2), EXPECT(units_gone, "Units is closed"),
    PANELS_ANSWER,

    CASE("units: the panels answer while it is open, and it answers after them"),
    DO(open_units), WAIT(3), DO(snapshot), AT_TIP("Toolbar", TIP_DEFORM), CLICK, EXPECT(deform_toggled, "the Deform box toggles"),
    AT_TIP("Scene", TIP_EDGES), CLICK, EXPECT(edges_toggled, "the Edges box toggles"),
    AT_CLOSE("Units"), CLICK, WAIT(2), EXPECT(units_gone, "Units is closed"),
    PANELS_ANSWER,

    CASE("legend: a right click opens its settings, a list in them opens"),
    AT_WIN("Legend", 0.5f, 0.6f), RCLICK, WAIT(2), EXPECT(legend_settings_shown, "the legend settings open"),
    AT_CLOSE("Legend settings"), CLICK, WAIT(2), EXPECT(legend_settings_gone, "the legend settings close"),
    PANELS_ANSWER,

    CLOSED_THEN_PANELS("Messages", open_messages, messages_gone),
    CLOSED_THEN_PANELS("Formula", open_formula, formula_gone),
    CLOSED_THEN_PANELS("Convergence", open_convergence, convergence_gone),
    CLOSED_THEN_PANELS("Legend settings", open_legend_settings, legend_settings_gone),
    CLOSED_THEN_PANELS("Find", open_find, find_gone),
    CLOSED_THEN_PANELS("Open file", open_browser, browser_gone),
    CLOSED_THEN_PANELS("Mesh quality", open_mesh, mesh_gone),
    CLOSED_THEN_PANELS("Details", open_details, details_gone),
    CLOSED_THEN_PANELS("About", open_about, about_gone),

    CASE("menu: a right click on the model opens it, an item acts and closes it"),
    DO(close_all), DO(fit_view), WAIT(2), AT_MODEL, RCLICK, WAIT(2), EXPECT(menu_open, "the menu opens"),
    DO(snapshot), AT_TIP("Menu", "#menu Centre the view here"), CLICK, WAIT(2), EXPECT(menu_closed_turned, "the view centres, the menu closes"),

    CASE("menu: a click beside it only closes it"),
    DO(fit_view), WAIT(2), AT_MODEL, RCLICK, WAIT(2), EXPECT(menu_open, "the menu opens"),
    AT_VIEW(0.03f, 0.5f), CLICK, WAIT(2), EXPECT(menu_closed, "the menu closes"), PANELS_ANSWER,

    CASE("menu: hide an element, then show all from empty space"),
    DO(fit_view), WAIT(2), AT_MODEL, RCLICK, WAIT(2), AT_TIP("Menu", "#menu Hide element"), CLICK, WAIT(10), EXPECT(one_hidden, "one element hidden"),
    AT_VIEW(0.04f, 0.06f), RCLICK, WAIT(2), EXPECT(menu_open, "the menu opens on empty space"),
    AT_TIP("Menu", "#menu Show all"), CLICK, WAIT(10), EXPECT(all_shown, "everything shown again"),

    CASE("menu: hide this element type, then show all"),
    DO(fit_view), WAIT(2), AT_MODEL, RCLICK, WAIT(2), AT_TIP("Menu", "#menu hide every"), CLICK, WAIT(10), EXPECT(menu_closed, "the menu closes"),
    AT_VIEW(0.04f, 0.06f), RCLICK, WAIT(2), AT_TIP("Menu", "#menu Show all"), CLICK, WAIT(10), EXPECT(all_shown, "everything shown again"),

    CASE("menu: clip here"),
    DO(fit_view), WAIT(2), AT_MODEL, RCLICK, WAIT(2), AT_TIP("Menu", "#menu Clip here"), CLICK, WAIT(3), EXPECT(clipped, "the clip plane is on"),
    DO(unclip), WAIT(2),

    CASE("menu: look from +X"),
    AT_VIEW(0.04f, 0.06f), RCLICK, WAIT(2), DO(snapshot), AT_TIP("Menu", "#menu look +X"), CLICK, WAIT(2),
    EXPECT(menu_closed_turned, "the view turns, the menu closes"),
    AT_VIEW(0.04f, 0.06f), RCLICK, WAIT(2), AT_TIP("Menu", "#menu Fit"), CLICK, WAIT(2),

    CASE("menu: a box selection hidden from the menu"),
    DO(arm_box), AT_VIEW(0.3f, 0.3f), PRESS, AT_VIEW(0.5f, 0.5f), AT_VIEW(0.7f, 0.7f), RELEASE, WAIT(2), EXPECT(box_window, "a selection"),
    AT_VIEW(0.04f, 0.06f), RCLICK, WAIT(2), AT_TIP("Menu", "#menu Save the selection"), CLICK, WAIT(2), EXPECT(csv_saved, "the CSV holds the selection"),
    AT_VIEW(0.04f, 0.06f), RCLICK, WAIT(2), AT_TIP("Menu", "#menu Hide the"), CLICK, WAIT(10), EXPECT(sel_hidden, "the selection is hidden"),
    AT_VIEW(0.04f, 0.06f), RCLICK, WAIT(2), AT_TIP("Menu", "#menu Show all"), CLICK, WAIT(10), EXPECT(all_shown, "everything shown again"),
    PANELS_ANSWER,

    CASE("about: opened from the status bar"),
    AT_TIP("Status", "Who made ccxview"), CLICK, WAIT(2), EXPECT(about_shown, "the About window opens"),
    AT_CLOSE("About"), CLICK, WAIT(2), EXPECT(about_gone, "About is closed"), PANELS_ANSWER,

    CASE("mesh quality: the limits open, a limit answers"),
    DO(open_mesh), WAIT(5), AT_TIP("Mesh quality", "Set your own limits"), CLICK, WAIT(2),
    AT_TIP_X("Mesh quality", "The limit for every element type", 0.95f), CLICK, EXPECT(mesh_limit_set, "the aspect limit changes"),
    DO(mesh_limits_usual), WAIT(3), PANELS_ANSWER,

    CASE("a window closed by the program (OK, Escape), the panels still answer"),
    DO(open_formula), WAIT(3), AT_TITLE("Formula"), CLICK, DO(close_all), WAIT(1), PANELS_ANSWER,
    DO(open_units), WAIT(3), AT_TITLE("Units"), CLICK, DO(close_all), AT_TIP("Scene", TIP_EDGES),
    DO(snapshot), CLICK, EXPECT(edges_toggled, "the Edges box toggles at once"),

    CASE("a window dragged over the sidebar and closed there: the sidebar under it answers at once"),
    DO(open_units), WAIT(3), AT_TITLE("Units"), PRESS, AT_WIN("Scene", 0.6f, 0.3f), AT_WIN("Scene", 0.5f, 0.25f), RELEASE,
    EXPECT(units_over_sidebar, "Units follows the drag over the sidebar"),
    AT_CLOSE("Units"), CLICK, WAIT(1), EXPECT(units_gone, "Units is closed"),
    DO(snapshot), JUMP_TIP("Scene", TIP_EDGES), CLICK, EXPECT(edges_toggled, "the Edges box toggles"),
    DO(open_messages), WAIT(3), AT_TITLE("Messages"), PRESS, AT_WIN("Scene", 0.6f, 0.3f), AT_WIN("Scene", 0.5f, 0.25f), RELEASE,
    DO(snapshot), JUMP_TIP("Scene", TIP_EDGES), CLICK, EXPECT(edges_toggled, "the Edges box toggles beside the open window"),
    DO(close_all), DO(snapshot), JUMP_TIP("Scene", TIP_MARKERS), CLICK, EXPECT(markers_toggled, "the Min / max box toggles the frame it closed"),

    CASE("the view after a window closed over it: a drag turns the model at once"),
    DO(open_units), WAIT(3), AT_CLOSE("Units"), CLICK, EXPECT(units_gone, "Units is closed"),
    DO(snapshot), PRESS, AT_VIEW(0.6f, 0.55f), AT_VIEW(0.7f, 0.6f), RELEASE, EXPECT(turned, "the camera turns"),

    CASE("two windows: closing the top one leaves the other and the panels usable"),
    DO(open_messages), WAIT(3), DO(open_units), WAIT(3), AT_CLOSE("Units"), CLICK, WAIT(2), EXPECT(units_gone, "Units is closed"),
    AT_CLOSE("Messages"), CLICK, WAIT(2), EXPECT(messages_gone, "Messages is closed"),
    PANELS_ANSWER,

    CASE("a window closed while a list of the toolbar is open"),
    DO(open_units), WAIT(3), AT_TIP("Toolbar", TIP_CMAP), CLICK, EXPECT(toolbar_popup, "the colour map list opens"),
    DO(close_all), WAIT(3), AT_VIEW(0.5f, 0.6f), CLICK,
    PANELS_ANSWER,

    { OP_END }
};

/* ---- the run ---------------------------------------------------------------------- */
void ui_test_start(const char* dir, void (*send_event)(const sapp_event*)) {
    T.on = true; T.dir = dir; T.send = send_event;
}
bool ui_test_on(void) { return T.on; }
int  ui_test_result(void) { return T.done ? T.failed : -1; }
const char* ui_test_shot(void) {
    if (!T.shot) return NULL;
    T.shot = false;
    return T.shot_path;
}

/* the point a step aims at; false when its window or widget is not there */
static bool target(struct nk_context* ctx, const step* s, float* x, float* y) {
    const struct nk_style* st = &ctx->style;
    if (s->op == OP_AT_VIEW) { *x = G.vp_x + s->x * G.vp_w; *y = G.vp_y + s->y * G.vp_h; return true; }
    if (s->op == OP_AT_MODEL) {                 /* a point of the view that lies on the model, the nearest to its middle */
        float best = 1e30f;
        for (int j = 1; j < 16; j++) for (int i = 1; i < 16; i++) {
            float px = G.vp_x + G.vp_w * i / 16.f, py = G.vp_y + G.vp_h * j / 16.f, d2 = (i - 8.f) * (i - 8.f) + (j - 8.f) * (j - 8.f);
            v3 p; bool on = false;
            if (d2 < best && app_cursor_point(px, py, &p, &on) && on) { best = d2; *x = px; *y = py; }
        }
        if (best > 1e29f) fail("no point of the view lies on the model");
        return best < 1e29f;
    }
    if (s->op == OP_AT_TIP) {
        const mark* m = find_mark(s->a, s->b);
        struct nk_window* w = m ? win_of(ctx, m->win) : NULL;
        if (!w) { fail("no widget with the tooltip '%s' in '%s'", s->b, s->a ? s->a : "any window"); return false; }
        *x = m->r.x + m->r.w * (s->x > 0 ? s->x : 0.5f); *y = m->r.y + m->r.h * 0.5f;
        if (!NK_INBOX(*x, *y, w->bounds.x, w->bounds.y, w->bounds.w, w->bounds.h)) {
            fail("the widget '%s' is scrolled out of '%s'", s->b, m->win);
            return false;
        }
        return true;
    }
    struct nk_window* w = win_of(ctx, s->a);
    if (!w) { fail("no window '%s'", s->a); return false; }
    struct nk_rect b = w->bounds;
    if (s->op == OP_AT_POPUP) {
        if (!w->popup.win || !w->popup.active) { fail("no open list in '%s'", s->a); return false; }
        b = w->popup.win->bounds;
    }
    if (s->op == OP_AT_CLOSE || s->op == OP_AT_TITLE) {     /* the header: its close button at the right end, its title */
        float h = st->font->height + 2 * st->window.header.padding.y + 2 * st->window.header.label_padding.y;
        *x = s->op == OP_AT_CLOSE ? b.x + b.w - st->window.header.padding.x - h * 0.5f : b.x + b.w * 0.3f;
        *y = b.y + h * 0.5f;
        return true;
    }
    *x = b.x + s->x * b.w; *y = b.y + s->y * b.h;
    return true;
}

static void next_case(void) {
    while (script[T.pc].op != OP_CASE && script[T.pc].op != OP_END) T.pc++;
}

/* Once a frame, before the frame's input goes to Nuklear: check the frame before,
   then send the next step's events. ctx is NULL until the first frame is drawn. */
void ui_test_frame(struct nk_context* ctx) {
    if (!T.on || T.done || !ctx || !G.loaded || app_busy()) return;
    T.frame++;
    g_ctx = ctx;
    check_always(ctx);
    if (T.case_failed && script[T.pc].op != OP_CASE) next_case();
    while (T.wait == 0) {
        const step* s = &script[T.pc];
        float x, y;
        if (T.case_failed && s->op != OP_CASE && s->op != OP_END) { next_case(); continue; }
        switch (s->op) {
        case OP_END:
            if (T.name && !T.case_failed) fprintf(stderr, "ok    %s\n", T.name);
            fprintf(stderr, "ui-test: %d cases, %d failed\n", T.cases, T.failed);
            T.done = true;
            n_marks = 0;
            return;
        case OP_CASE:                           /* every case starts from the same state */
            if (!T.resetting) {
                if (T.name && !T.case_failed) fprintf(stderr, "ok    %s\n", T.name);
                T.name = s->a; T.cases++; T.case_failed = false; T.phase = 0;
            }
            for (int b = 0; b < 3; b++) release(b);
            close_all(ctx);
            move_to(G.vp_x + G.vp_w * 0.5f, G.vp_y + G.vp_h * 0.6f);
            if (!T.resetting && any_popup(ctx)) {       /* a list left open: a click beside it closes it */
                press(0); T.pressed = false; T.resetting = true; n_marks = 0;
                return;
            }
            T.resetting = false; T.wait = 4;
            break;
        case OP_AT_TIP: case OP_AT_WIN: case OP_AT_POPUP: case OP_AT_CLOSE: case OP_AT_TITLE: case OP_AT_VIEW: case OP_AT_MODEL:
            if (target(ctx, s, &x, &y)) { move_to(x, y); T.wait = s->n ? 0 : 2; }    /* n: the next step in the same frame */
            break;
        case OP_CLICK:                          /* down in one frame, up in the next, as a hand does */
            if (T.phase == 0) { press(s->n); T.phase = 1; n_marks = 0; return; }
            release(s->n); T.phase = 0; T.wait = 2;
            break;
        case OP_PRESS:   press(s->n); T.wait = 1; break;
        case OP_RELEASE: release(s->n); T.wait = 2; break;
        case OP_WHEEL:   send(SAPP_EVENTTYPE_MOUSE_SCROLL, 0, s->y); T.wait = 1; break;
        case OP_WAIT:    T.wait = s->n; break;
        case OP_DO:      s->fn(ctx); break;
        case OP_EXPECT:  if (!s->ok(ctx)) fail("expected: %s", s->b); break;
        }
        T.pc++;
    }
    T.wait--;
    n_marks = 0;                                /* the frame about to be laid out reports them again */
}
