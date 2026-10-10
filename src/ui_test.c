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
#include "stl.h"
#include "tbtext.h"

enum { OP_END, OP_CASE, OP_AT_TIP, OP_AT_WIN, OP_AT_POPUP, OP_AT_CLOSE, OP_AT_TITLE, OP_AT_VIEW, OP_AT_MODEL, OP_CLICK, OP_PRESS, OP_RELEASE,
       OP_WHEEL, OP_TYPE, OP_WAIT, OP_DO, OP_EXPECT };

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
static struct { int cmap, bands, faces_mode, units, cyc_axis, tensor_style, traj_which, comp; bool deform, markers, edges; float dist, scroll; cv_camera cam; } was;

static void close_all(struct nk_context* ctx) {
    struct nk_window* sc = nk_window_find(ctx, "Scene");     /* every case starts with the sidebar at its top */
    if (sc) sc->scrollbar.y = 0;
    G.show_units = G.show_msgs = G.show_calc_help = G.show_conv = G.legend_edit = G.find_open = G.browser_open = false;
    G.show_details = G.show_deck = false;
    G.label_probe_only = false; G.label_kinds = 0;
    G.hist_open = G.path_open = false;
    G.title_edit = false;
    G.show_measure = false; app_measure_cancel();
    G.show_rebar = false;
    G.show_select = false; G.sel_tool = CV_ST_NONE; G.sel_mode = CV_SEL_NEW;
    G.sel_open[CV_SELG_PICK] = G.sel_open[CV_SELG_NAMES] = G.sel_open[CV_SELG_USE] = true; G.sel_open[CV_SELG_NAMED] = false;
}
static void snapshot(struct nk_context* ctx) {
    was.cmap = G.cmap; was.bands = G.bands; was.faces_mode = G.faces_mode; was.units = G.units; was.cyc_axis = G.cyc_axis;
    was.tensor_style = G.tensor_style; was.traj_which = G.traj_which;
    was.deform = G.deform; was.markers = G.show_markers; was.edges = G.show_edges; was.comp = G.comp;
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
static bool measure_popup(struct nk_context* ctx) { return popup_open(ctx, "Measurements"); }
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
static void nodes_off(struct nk_context* ctx) { G.sel_nodes = false; G.sel_elems = true; G.sel_visible = true; }
/* the nodes a box took stay its nodes: the camera turned, elements unticked, the same count */
static uint32_t seln_was;
static void remember_nodes(struct nk_context* ctx) { seln_was = G.seln_n; }
static bool nodes_more(struct nk_context* ctx) { return !G.sel_visible && G.seln_n > seln_was; }
static bool min_marked(struct nk_context* ctx) { return G.sel_mark_min && G.boxq.on; }
static void remember_nodes_turn(struct nk_context* ctx) { seln_was = G.seln_n; G.cam.yaw += 0.7f; G.cam.pitch += 0.3f; }
static bool nodes_kept(struct nk_context* ctx) { return !G.sel_elems && G.sel_n == 0 && G.seln_n == seln_was && seln_was > 0; }
/* the Selection window */
static void open_select(struct nk_context* ctx) { G.show_select = true; }
static bool select_gone(struct nk_context* ctx) { return !G.show_select; }
static bool select_shown(struct nk_context* ctx) { return G.show_select && win_of(ctx, "Selection"); }
static uint32_t sel_was, seln_was2;
static void sel_remember(struct nk_context* ctx) { sel_was = G.sel_n; seln_was2 = G.seln_n; }
static void sel_none(struct nk_context* ctx) { app_sel_clear(); G.sel_elems = true; G.sel_nodes = false; G.sel_visible = true; G.probe_on = false; }
static void ids_1_20(struct nk_context* ctx) { uii_select_ids("1-20, 9999, x9", false); }
static void ids_10_30(struct nk_context* ctx) { uii_select_ids("10-30", false); }
static bool sel_20(struct nk_context* ctx) { return G.sel_n == 20 && !G.seln_n && G.boxq.on && G.sel_note[0]; }   /* 9999 is not there, x9 is no id */
static bool sel_20k(struct nk_context* ctx) { return G.sel_n == 20 && !G.seln_n; }
static bool sel_30(struct nk_context* ctx) { return G.sel_n == 30; }      /* 1-20 and 10-30 */
static bool sel_9(struct nk_context* ctx) { return G.sel_n == 9; }        /* 1-30 without 10-30 */
static bool sel_11(struct nk_context* ctx) { return G.sel_n == 11; }      /* 10-20: both */
static bool sel_inverted(struct nk_context* ctx) { return G.sel_n == G.frd.n_elems - sel_was && G.sel_n > 0; }
static bool sel_to_nodes(struct nk_context* ctx) { return !G.sel_n && G.seln_n > sel_was && G.sel_nodes && !G.sel_elems; }
static bool sel_back(struct nk_context* ctx) { return G.sel_n == sel_was && !G.seln_n && G.sel_elems; }
static bool sel_touching(struct nk_context* ctx) { return G.sel_n > sel_was; }
static bool sel_empty(struct nk_context* ctx) { return !G.sel_n && !G.seln_n && !G.boxq.on; }
static bool sel_all(struct nk_context* ctx) { return G.sel_n == G.frd.n_elems && !G.seln_n; }
static void app_nsel_clear_ctx(struct nk_context* ctx) { app_nsel_clear(); }
static bool sel_any(struct nk_context* ctx) { return (G.sel_n || G.seln_n) && !G.sel_note[0]; }
static bool sel_one(struct nk_context* ctx) { return G.sel_n == 1 && G.sel_tool == CV_ST_CLICK; }
static bool click_armed(struct nk_context* ctx) { return G.sel_tool == CV_ST_CLICK; }
static bool sel_mode_add(struct nk_context* ctx) { return G.sel_mode == CV_SEL_ADD; }
static bool sel_mode_remove(struct nk_context* ctx) { return G.sel_mode == CV_SEL_REMOVE; }
static bool sel_mode_and(struct nk_context* ctx) { return G.sel_mode == CV_SEL_AND; }
static bool select_popup(struct nk_context* ctx) { return popup_open(ctx, "Selection"); }
/* a box in add mode keeps what was there: more than the first box alone */
static bool box_added(struct nk_context* ctx) { return G.sel_n > sel_was && sel_was > 0; }
static bool lasso_armed(struct nk_context* ctx) { return G.sel_tool == CV_ST_LASSO; }
static bool tool_down(struct nk_context* ctx) { return G.sel_tool == CV_ST_NONE && G.show_select; }
static bool lasso_took(struct nk_context* ctx) { return G.sel_n > 0 && G.sel_n < G.frd.n_elems && G.boxq.on; }
static bool faces_took(struct nk_context* ctx) { return G.sel_tool == CV_ST_FACE && G.sel_n > 0 && G.sel_note[0]; }
static bool chain_took(struct nk_context* ctx) { return G.sel_tool == CV_ST_CHAIN && G.seln_n > 1 && !G.sel_n; }
static bool chain_armed(struct nk_context* ctx) { return G.sel_tool == CV_ST_CHAIN; }
static bool part_took(struct nk_context* ctx) { return G.sel_tool == CV_ST_PART && G.sel_n == G.frd.n_elems; }   /* one part */
static bool sel_grew(struct nk_context* ctx) { return G.sel_n > sel_was; }
static bool sel_shrank(struct nk_context* ctx) { return G.sel_n < sel_was && G.sel_n > 0; }
static bool sel_boundary(struct nk_context* ctx) { return G.sel_n > 0 && G.seln_n > 0; }
static void ids_some(struct nk_context* ctx) { uii_select_ids("100-110", false); }
static void name_holea(struct nk_context* ctx) { uii_select_name("HOLEA"); }
static void name_hb(struct nk_context* ctx) { uii_select_name("HB"); }
static bool kept_one(struct nk_context* ctx) { return app_nsel_count() == 1 && !strcmp(app_nsel_at(0)->name, "HOLEA") && app_nsel_at(0)->ne == 20; }
static bool renamed(struct nk_context* ctx) { return app_nsel_count() == 1 && !strcmp(app_nsel_at(0)->name, "HB"); }
static bool none_kept(struct nk_context* ctx) { return app_nsel_count() == 0; }
static bool cropped(struct nk_context* ctx) { return G.crop_on && G.sel_n > 0; }
static bool sel_clipped(struct nk_context* ctx) { return G.clip_on; }
static bool labels_sel(struct nk_context* ctx) { return G.label_sel_only && G.label_kinds; }
static bool inp_saved(struct nk_context* ctx) {
    char path[1200];
    app_sel_inp_path("HOLEA", path, sizeof path);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[200]; int sets = 0;
    while (fgets(line, sizeof line, f)) sets += !strncmp(line, "*ELSET, ELSET=HOLEA", 19);
    fclose(f);
    return sets == 1;
}
static bool isolated(struct nk_context* ctx) { uint32_t n = 0; for (uint32_t e = 0; G.hide && e < G.frd.n_elems; e++) n += !G.hide[e]; return G.hide && n == 20; }
static void use_end(struct nk_context* ctx) {
    G.crop_on = G.clip_on = G.label_sel_only = false; G.label_kinds = 0; app_label_changed();
    app_sel_clear(); app_show_all(); app_nsel_clear();
}
static void filters_on(struct nk_context* ctx) { G.sel_filters = true; G.sel_open[CV_SELG_USE] = false; }   /* room for them */
static void filters_off(struct nk_context* ctx) { G.sel_filters = false; G.sel_open[CV_SELG_USE] = true; app_sel_clear(); }
static void named_open(struct nk_context* ctx) { G.sel_open[CV_SELG_NAMED] = true; }
static bool pick_folded(struct nk_context* ctx) { return !G.sel_open[CV_SELG_PICK] && G.sel_open[CV_SELG_USE]; }
static bool pick_open(struct nk_context* ctx) { return G.sel_open[CV_SELG_PICK]; }
static bool named_unfolded(struct nk_context* ctx) { return G.sel_open[CV_SELG_NAMED]; }
static bool sel_some(struct nk_context* ctx) { return G.sel_n > sel_was && G.sel_n < G.frd.n_elems; }
static bool details_shown(struct nk_context* ctx) { return G.show_details && win_of(ctx, "Details"); }
static void ids_on(struct nk_context* ctx) { G.label_kinds = 1 << CV_LABEL_NODE; G.label_probe_only = true; app_label_changed(); }
static void ids_off(struct nk_context* ctx) { G.label_kinds = 0; G.label_probe_only = false; app_label_changed(); }
/* only Fields open, the panel scrolled to its end: the Labels tree is Fields' last item */
static void labels_tree_open(struct nk_context* ctx) {
    for (int k = 0; k < CV_TREE_N; k++) G.tree[k] = k == CV_TREE_FIELDS || k == CV_TREE_LABELS;
    struct nk_window* w = win_of(ctx, "Scene"); if (w) w->scrollbar.y = 100000;   /* Nuklear clamps it to the content */
}
static bool labels_node(struct nk_context* ctx) { return app_label_on(CV_LABEL_NODE) && !G.label_probe_only; }
static void labels_small(struct nk_context* ctx) { G.label_px = 8.f; }
static bool labels_bigger(struct nk_context* ctx) { return G.label_px > 8.f; }
static bool labels_behind(struct nk_context* ctx) { return !G.label_front; }
static bool labels_shown(struct nk_context* ctx);
static bool labels_gauss(struct nk_context* ctx) { return app_label_on(CV_LABEL_GPVALUE) && app_label_on(CV_LABEL_NODE) && G.show_gp && labels_shown(ctx); }
static void gauss_off(struct nk_context* ctx) { G.show_gp = false; app_gauss_changed(); }
static void labels_front(struct nk_context* ctx) { G.label_front = true; }
static bool labels_shown(struct nk_context* ctx) { return strstr(G.label_note, "shown ") && !strstr(G.label_note, "shown 0 of"); }
static bool labels_none(struct nk_context* ctx) { return strstr(G.label_note, "shown 0 of 0") != NULL; }
static void probe_close(struct nk_context* ctx) { G.probe_on = false; }
static void labels_off(struct nk_context* ctx) { G.label_kinds = 0; G.label_px = 13.f; app_label_changed(); sections_open(ctx); }
/* labels moved by hand: node ids in front, a label found by its box near the view's middle */
static bool turned(struct nk_context* ctx);
static void labels_movable(struct nk_context* ctx) {
    close_all(ctx);
    G.label_kinds = 1 << CV_LABEL_NODE; G.label_front = true; G.label_spacing = 40.f;
    app_label_moved_clear(); app_label_changed();
}
static bool label_centre(struct nk_context* ctx, bool moved_only, float* x, float* y) {
    float r[4], best = 1e30f, cx = G.vp_x + 0.5f * G.vp_w, cy = G.vp_y + 0.5f * G.vp_h;
    bool any = false;
    for (int k = 0; app_label_box(k, r); k++) {
        float mx = r[0] + 0.5f * r[2], my = r[1] + 0.5f * r[3], d = (mx - cx) * (mx - cx) + (my - cy) * (my - cy);
        cv_label_off key;
        if (d >= best || input_window_at(ctx, mx, my) || r[0] < G.vp_x || r[1] < G.vp_y || r[0] + r[2] > G.vp_x + G.vp_w || r[1] + r[3] > G.vp_y + G.vp_h) continue;
        if (!app_label_at(mx, my, &key) || (moved_only && key.dx == 0 && key.dy == 0)) continue;
        best = d; *x = mx; *y = my; any = true;
    }
    return any;
}
static void at_label(struct nk_context* ctx) { float x, y; if (label_centre(ctx, false, &x, &y)) move_to(x, y); }
static void at_moved_label(struct nk_context* ctx) { float x, y; if (label_centre(ctx, true, &x, &y)) move_to(x, y); }
static void nudge(struct nk_context* ctx) { move_to(T.mx + 30, T.my - 20); }
static bool on_label(struct nk_context* ctx) { cv_label_off k; return app_label_at(T.mx, T.my, &k); }
static bool label_moved_cam_kept(struct nk_context* ctx) {
    cv_label_off o;
    float s = ui_scale();
    return app_label_moved_count() == 1 && app_label_moved_get(0, &o) && fabsf(o.dx * s - 60) < 2 && fabsf(o.dy * s + 40) < 2 && !turned(ctx) && !app_label_dragging();
}
static bool label_reset(struct nk_context* ctx) { return app_label_moved_count() == 0 && !G.menu_on; }
static void label_move_one(struct nk_context* ctx) { app_label_move("node", "1", 25, 25); }
static bool labels_moved_one(struct nk_context* ctx) { return app_label_moved_count() == 1; }
static void labels_back(struct nk_context* ctx) { G.label_spacing = 10.f; labels_off(ctx); }

/* the Details window leaves the view's upper right free: a drag there turns the model */
static bool details_clear_of_view(struct nk_context* ctx) {
    struct nk_window* w = win_of(ctx, "Details");
    return w && w->bounds.x + w->bounds.w < G.vp_x + G.vp_w * 0.6f;
}
static bool box_crossing(struct nk_context* ctx) { return G.sel_n > 0 && G.sel_crossing && G.probe_on; }
static bool box_window(struct nk_context* ctx) { return G.sel_n > 0 && !G.sel_crossing; }
static void open_details(struct nk_context* ctx) { app_probe_at(0, false); G.show_details = true; }
static bool details_gone(struct nk_context* ctx) { return !G.show_details; }
static void open_about(struct nk_context* ctx) { G.show_about = true; }
static void open_deck(struct nk_context* ctx) { G.show_deck = true; }
/* the Contact window with the contact sample's .cel read into the model on screen:
   its iterations listed (their nodes need not be this model's) */
static void open_contact(struct nk_context* ctx) {
    if (!app_contact_cel()) app_contact_open("samples/contact/contact.cel");
    G.cel_pick = -1; G.model_alpha = 1.f; app_see_refresh();
    G.show_contact = true;
}
static bool contact_gone(struct nk_context* ctx) { return !G.show_contact; }
static bool contact_listed(struct nk_context* ctx) { return app_contact_cel() && app_contact_cel()->nsets == 11 && win_of(ctx, "Contact"); }
static bool contact_popup(struct nk_context* ctx) { return popup_open(ctx, "Contact"); }
static bool contact_picked(struct nk_context* ctx) { return G.cel_pick >= 0 && app_contact_cel_set() == G.cel_pick && !popup_open(ctx, "Contact"); }
static bool see_through(struct nk_context* ctx) { return G.model_alpha < 0.9f && cv_render_see_on(); }
static void contact_end(struct nk_context* ctx) {
    G.show_contact = false; G.cel_pick = -1; G.model_alpha = 1.f;
    app_contact_clear(); app_see_refresh();
}
static void layers_alpha(struct nk_context* ctx) { G.model_alpha = 1.f; app_see_refresh(); }
/* the contact display's choices in the Contact window */
static bool cmode_status(struct nk_context* ctx) { return (G.cel_mode & CV_CMODE_STATUS) && (G.cel_mode & CV_CMODE_LINKS); }
static bool cmode_gap(struct nk_context* ctx) { return (G.cel_mode & CV_CMODE_GAP) != 0; }
static bool cmode_solids(struct nk_context* ctx) { return (G.cel_mode & CV_CMODE_SOLIDS) != 0; }
static bool ctrue_on(struct nk_context* ctx) { return G.cel_true; }
static void cdisp_back(struct nk_context* ctx) { G.cel_mode = CV_CMODE_LINKS; G.cel_true = false; app_contact_refresh(); }
static bool deck_gone(struct nk_context* ctx) { return !G.show_deck; }
static bool deck_shown(struct nk_context* ctx) { return G.show_deck && win_of(ctx, "Deck"); }
static void groups_first(struct nk_context* ctx) { G.tree[CV_TREE_LAYERS] = 0; G.tree[CV_TREE_GROUPS] = 1; }
static void layers_first(struct nk_context* ctx) { G.tree[CV_TREE_LAYERS] = 1; G.tree[CV_TREE_GROUPS] = 0; }
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
static void display_open(struct nk_context* ctx) {
    for (int k = 0; k < CV_TREE_N; k++) G.tree[k] = k == CV_TREE_VIEW || k == CV_TREE_DISPLAY;
    struct nk_window* w = nk_window_find(ctx, "Scene");
    if (w) w->scrollbar.y = 0;
}
static bool title_shown(struct nk_context* ctx) { return G.title_on && win_of(ctx, "title"); }
static bool title_settings_shown(struct nk_context* ctx) { return G.title_edit && win_of(ctx, "Title block") && !G.menu_on; }
static bool title_settings_gone(struct nk_context* ctx) { return !G.title_edit; }
static void open_title_settings(struct nk_context* ctx) { G.title_edit = true; }
static bool title_user_off(struct nk_context* ctx) { return !G.title_line[CV_TB_USER]; }
static void title_off(struct nk_context* ctx) { G.title_on = false; G.title_line[CV_TB_USER] = true; sections_open(ctx); }
/* the title block's text: made from the boxes, today's date in the first format */
static void title_text_open(struct nk_context* ctx) {
    G.title_on = G.title_edit = true; G.title_hand = false;
    snprintf(G.title_date_fmt, sizeof G.title_date_fmt, "%s", cv_tb_date_fmt[0]);
}
/* the block drawn has a line with `what` in it */
static bool title_has(const char* what) {
    char units[96];
    units_summary(units, sizeof units);
    static cv_title_line L[CV_TB_LINES];
    int n = app_title_lines(L, CV_TB_LINES, units);
    for (int i = 0; i < n; i++) if (strstr(L[i].text, what) || strstr(L[i].label, what)) return true;
    return false;
}
static bool title_typed(struct nk_context* ctx) { return G.title_hand && strstr(G.title_text, "Qzx") && title_has("Qzx"); }
static bool title_reset(struct nk_context* ctx) { return !G.title_hand && !strstr(G.title_text, "Qzx") && !title_has("Qzx") && title_has(app_title_line_name(CV_TB_FILE)); }
static bool title_fmt_picked(struct nk_context* ctx) {
    if (!strcmp(G.title_date_fmt, cv_tb_date_fmt[0]) || popup_open(ctx, "Title block")) return false;
    for (int i = 1; i < CV_TB_DATE_N; i++) if (!strcmp(G.title_date_fmt, cv_tb_date_fmt[i])) return true;
    return false;
}
static bool title_fmt_list(struct nk_context* ctx) { return popup_open(ctx, "Title block"); }
static void title_text_off(struct nk_context* ctx) {
    G.title_on = false; G.title_hand = false;
    snprintf(G.title_date_fmt, sizeof G.title_date_fmt, "%s", cv_tb_date_fmt[0]);
}
static bool ui_smaller(struct nk_context* ctx) { return ui_get_zoom() < 0.99f; }
static bool ui_normal(struct nk_context* ctx) { return fabsf(ui_get_zoom() - 1.f) < 0.01f; }
static void symbols_open(struct nk_context* ctx) {
    for (int k = 0; k < CV_TREE_N; k++) G.tree[k] = k == CV_TREE_VIEW || k == CV_TREE_SYMBOLS;
    struct nk_window* w = nk_window_find(ctx, "Scene");
    if (w) w->scrollbar.y = 0;
}
static bool thin_on(struct nk_context* ctx) { return G.sym_thin; }
static void thin_off(struct nk_context* ctx) { G.sym_thin = false; sections_open(ctx); }
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
static void open_integrals(struct nk_context* ctx) { app_integ_open(CV_IK_VOLUME, "shown"); }
static bool integrals_gone(struct nk_context* ctx) { return !GI.open; }
static bool integrals_shown(struct nk_context* ctx) { return GI.open && win_of(ctx, "Integrals") && GI.n > 0; }
static bool integrals_popup(struct nk_context* ctx) { return popup_open(ctx, "Integrals"); }
static bool integrals_nodes(struct nk_context* ctx) { return GI.open && GI.kind == CV_IK_NODES && GI.n > 0; }
static bool integrals_set(struct nk_context* ctx) { return GI.open && GI.kind == CV_IK_VOLUME && !strcasecmp(GI.target, "EALL") && GI.n > 0; }
static bool integrals_sel(struct nk_context* ctx) { return GI.open && !strcmp(GI.target, "selection") && GI.n > 0; }
static void integrals_close(struct nk_context* ctx) { app_integ_close(); }
/* only Fields open, the panel at its top: the integrate button in sight */
static void fields_open(struct nk_context* ctx) {
    for (int k = 0; k < CV_TREE_N; k++) G.tree[k] = k == CV_TREE_FIELDS;
    struct nk_window* w = win_of(ctx, "Scene"); if (w) w->scrollbar.y = 0;
}
static void app_sel_clear_ctx(struct nk_context* ctx) { app_sel_clear(); G.probe_on = false; }
static void integrals_end(struct nk_context* ctx) { app_integ_close(); app_sel_clear(); sections_open(ctx); }
static bool deform_toggled(struct nk_context* ctx) { return G.deform != was.deform; }
static bool markers_toggled(struct nk_context* ctx) { return G.show_markers != was.markers; }
static void first_field(struct nk_context* ctx) { app_select_src("DISP", CV_COMP_MAG, 0); }
static bool ortho_on(struct nk_context* ctx) { return G.cam.ortho; }
static bool ortho_off(struct nk_context* ctx) { return !G.cam.ortho; }
static bool field_other(struct nk_context* ctx) { return G.field_src == 0 && strcmp(G.field_name, "DISP") != 0; }
static bool field_disp(struct nk_context* ctx) { return G.field_src == 0 && !strcmp(G.field_name, "DISP"); }
static bool comp_changed(struct nk_context* ctx) { return G.comp != was.comp; }
static bool symbols_off(struct nk_context* ctx) { return !G.show_bc && !G.show_loads && !G.show_links && !G.show_disc; }
static bool symbols_on(struct nk_context* ctx) { return G.show_bc && G.show_loads && G.show_links && G.show_disc; }
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
static void range_locked(struct nk_context* ctx) { G.range_lock = true; }
static bool oor_list_open(struct nk_context* ctx) { return popup_open(ctx, "Legend settings"); }
static bool oor_list_gone(struct nk_context* ctx) { return !popup_open(ctx, "Legend settings"); }
static void range_free(struct nk_context* ctx) { G.range_lock = false; app_refresh_range(); }
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
/* the Reinforcement window: its design values */
static void open_rebar(struct nk_context* ctx) { G.show_rebar = true; }
static bool rebar_gone(struct nk_context* ctx) { return !G.show_rebar; }
static void rebar_usual(struct nk_context* ctx) { G.rebar_fcd = 20.f; G.rebar_fyd = 435.f; G.rebar_cover = 40.f; app_rebar_changed(); }
static bool rebar_cover_up(struct nk_context* ctx) { return G.rebar_cover > 40.f; }
/* measurements: armed from the probe, the menu or their window; picked by clicks */
static void open_measure(struct nk_context* ctx) { G.show_measure = true; }
static bool measure_gone(struct nk_context* ctx) { return !G.show_measure; }
static bool measure_shown(struct nk_context* ctx) { return G.show_measure && win_of(ctx, "Measurements"); }
static void meas_clear(struct nk_context* ctx) { app_measure_cancel(); app_measure_clear(); G.meas_show = CV_MSHOW_BOTH; }
static void meas_turn(struct nk_context* ctx) { G.cam.yaw += 0.7f; G.cam.pitch += 0.3f; }   /* the next click on the same pixel hits another node */
static bool meas_dist_armed(struct nk_context* ctx) { return app_measure_armed() == CV_MEAS_DIST + 1 && strstr(app_measure_prompt(), "(2 of 2"); }
static bool meas_dist_made(struct nk_context* ctx) {
    int k; uint32_t id[3];
    return !app_measure_armed() && app_measure_count() == 1 && app_measure_get(0, &k, id) && k == CV_MEAS_DIST && id[0] != id[1];
}
static bool meas_circle_armed(struct nk_context* ctx) { return app_measure_armed() == CV_MEAS_CIRCLE + 1 && strstr(app_measure_prompt(), "(2 of 3") && !G.menu_on; }
static bool meas_angle_armed(struct nk_context* ctx) { return app_measure_armed() == CV_MEAS_ANGLE + 1 && strstr(app_measure_prompt(), "(1 of 3"); }
static void meas_cancel(struct nk_context* ctx) { app_pick_cancel(); }      /* as Esc */
static bool meas_not_armed(struct nk_context* ctx) { return !app_measure_armed(); }
static bool meas_shows_def(struct nk_context* ctx) { return G.meas_show != CV_MSHOW_BOTH; }
static bool meas_none(struct nk_context* ctx) { return app_measure_count() == 0; }
static bool meas_labelled(struct nk_context* ctx) { return app_measure_count() == 1 && !G.label_kinds; }
/* the CSV beside the model: a header and a row per state of the one measurement */
static bool meas_csv_saved(struct nk_context* ctx) {
    char path[1100];
    snprintf(path, sizeof path, "%s", G.path);
    char* dot = strrchr(path, '.');
    if (dot) *dot = 0;
    strncat(path, "_measurements.csv", sizeof path - strlen(path) - 1);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    int lines = 0, c;
    while ((c = fgetc(f)) != EOF) lines += c == '\n';
    fclose(f);
    remove(path);
    return lines == 3;
}
static void meas_add_ids(struct nk_context* ctx) {
    uint32_t id[3] = { G.frd.node_id[0], G.frd.node_id[G.frd.n_nodes - 1], 0 };
    meas_clear(ctx); app_measure_add(CV_MEAS_DIST, id);
}
/* imported geometry: a tetrahedron written under build/, listed, Groups > Imported geometry in sight */
static void stl_add(struct nk_context* ctx) {
    static const float t[36] = { 0, 0, 0, 0, 60, 0, 60, 0, 0,  0, 0, 0, 60, 0, 0, 0, 0, 60,
                                 0, 0, 0, 0, 0, 60, 0, 60, 0,  60, 0, 0, 0, 60, 0, 0, 0, 60 };
    cv_stl_write("build/ui-test/tet.stl", t, 4);
    app_stl_clear();
    app_stl_add("build/ui-test/tet.stl");
    for (int k = 0; k < CV_TREE_N; k++) G.tree[k] = k == CV_TREE_GROUPS || k == CV_TREE_IMPORT;
    struct nk_window* w = win_of(ctx, "Scene"); if (w) w->scrollbar.y = 0;
}
static cv_stl_layer stl_l(void) { cv_stl_layer l = { 0 }; app_stl_get(0, &l); return l; }
static bool stl_listed(struct nk_context* ctx) { return app_stl_count() == 1 && stl_l().visible; }
static bool stl_hidden(struct nk_context* ctx) { return app_stl_count() == 1 && !stl_l().visible; }
static void stl_faint(struct nk_context* ctx) { cv_stl_layer l = stl_l(); l.alpha = 0.1f; app_stl_set(0, &l); }
static bool stl_more_opaque(struct nk_context* ctx) { return stl_l().alpha > 0.5f; }
static bool stl_scaled(struct nk_context* ctx) { return stl_l().scale > 1.5f && !popup_open(ctx, "Scene"); }
static bool stl_gone(struct nk_context* ctx) { return app_stl_count() == 0; }
/* Import STL... (#23): with no system dialog the built-in browser opens, for an STL;
   in the model's folder it lists tet.stl and not the model's own files */
static void stl_tree_open(struct nk_context* ctx) {
    app_stl_clear();
    for (int k = 0; k < CV_TREE_N; k++) G.tree[k] = k == CV_TREE_GROUPS || k == CV_TREE_IMPORT;
    struct nk_window* w = win_of(ctx, "Scene"); if (w) w->scrollbar.y = 0;
}
static void stl_file_tree_open(struct nk_context* ctx) {
    app_stl_clear();
    for (int k = 0; k < CV_TREE_N; k++) G.tree[k] = k == CV_TREE_VIEW || k == CV_TREE_FILE;
    struct nk_window* w = win_of(ctx, "Scene"); if (w) w->scrollbar.y = 0;
}
static bool browser_for_stl(struct nk_context* ctx) {
    return G.browser_open && G.dlg_kind == CV_DLG_STL && !G.dlg_running && win_of(ctx, "Open file");
}
static void browse_model_dir(struct nk_context* ctx) {
    static const float t[36] = { 0, 0, 0, 0, 60, 0, 60, 0, 0,  0, 0, 0, 60, 0, 0, 0, 0, 60,
                                 0, 0, 0, 0, 0, 60, 0, 60, 0,  60, 0, 0, 0, 60, 0, 0, 0, 60 };
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", G.path);
    char* s = strrchr(dir, cv_path_sep());
    if (s) *s = 0;
    char stl[1100];
    snprintf(stl, sizeof stl, "%s%ctet.stl", dir, cv_path_sep());
    cv_stl_write(stl, t, 4);
    if (!cv_abs_path(dir, G.browse_dir, sizeof G.browse_dir)) snprintf(G.browse_dir, sizeof G.browse_dir, "%s", dir);
}
static bool browser_lists_stl_only(struct nk_context* ctx) {
    return find_mark("Open file", "#file tet.stl") && !find_mark("Open file", "#file showcase.frd") && !find_mark("Open file", "#file showcase.inp");
}
static bool stl_picked(struct nk_context* ctx) {
    return !G.browser_open && G.dlg_kind == CV_DLG_MODEL && app_stl_count() == 1 && !strcmp(app_stl_name(0), "tet.stl");
}
/* the pin of the showcase as imported geometry, the whole model in sight: a pixel on
   the pin where the model is not, so that only the layer can answer there */
static float pin_x, pin_y;
static void pin_layer(struct nk_context* ctx) {
    app_stl_clear();
    app_stl_add("samples/showcase/pin.stl");
    app_view(CV_VIEW_ISO);
    pin_x = pin_y = -1;
    float best = 1e30f;
    for (int j = 1; j < 40; j++) for (int i = 1; i < 40; i++) {
        float px = G.vp_x + G.vp_w * i / 40.f, py = G.vp_y + G.vp_h * j / 40.f, o[3], d[3];
        cv_pick pk;
        if (app_pick(px, py, &pk, o, d)) continue;
        float t = app_stl_ray(o, d, NULL), d2 = (i - 20.f) * (i - 20.f) + (j - 20.f) * (j - 20.f);
        if (t < INFINITY && d2 < best) { best = d2; pin_x = px; pin_y = py; }
    }
}
static bool in_pin(v3 p) {
    return p.x > -32.5f && p.x < 32.5f && p.y > -32.5f && p.y < 75.5f && p.z > -24.5f && p.z < 19.5f;   /* pin.stl's box */
}
static bool pin_under_cursor(struct nk_context* ctx) {
    v3 p; bool on = false;
    return pin_x >= 0 && app_cursor_point(pin_x, pin_y, &p, &on) && on && in_pin(p);
}
static bool pin_centred(struct nk_context* ctx) {
    return pin_x >= 0 && app_center_at(pin_x, pin_y) && in_pin(G.cam.target);
}
static void at_pin(struct nk_context* ctx) { if (pin_x >= 0) move_to(pin_x, pin_y); }
static bool pin_menu(struct nk_context* ctx) { return G.menu_on && G.menu_stl == 0 && win_of(ctx, "Menu"); }
static void pin_hide(struct nk_context* ctx) {
    cv_stl_layer l; app_stl_get(0, &l); l.visible = false; app_stl_set(0, &l); G.menu_on = false;
}
static bool pin_hidden_misses(struct nk_context* ctx) {
    v3 p; bool on = true;
    return app_cursor_point(pin_x, pin_y, &p, &on) && !on;    /* hidden: the ray goes through it */
}
static void pin_clear(struct nk_context* ctx) { app_stl_clear(); G.menu_on = false; app_view(CV_VIEW_ISO); }
/* files dropped together (app_open_files): the model first, the STL then joins it */
static void drop_model_and_pin(struct nk_context* ctx) {
    static char model[1024];
    snprintf(model, sizeof model, "%s", G.path);
    const char* p[2] = { "samples/showcase/pin.stl", model };
    app_stl_clear();
    app_open_files(p, 2);
}
static bool pin_joined(struct nk_context* ctx) {
    return G.loaded && !G.stl_model && G.frd.n_steps > 0 && app_stl_count() == 1 && !strcmp(app_stl_name(0), "pin.stl");
}
/* Groups alone open, the sidebar at its top: the element sets in sight */
static int groups_tree[CV_TREE_N];
static void groups_open(struct nk_context* ctx) {
    memcpy(groups_tree, G.tree, sizeof groups_tree);
    for (int k = 0; k < CV_TREE_N; k++) G.tree[k] = k == CV_TREE_GROUPS;
    struct nk_window* w = win_of(ctx, "Scene");
    if (w) w->scrollbar.y = 0;
}
static void groups_close(struct nk_context* ctx) { memcpy(G.tree, groups_tree, sizeof groups_tree); }
static bool set_hidden(struct nk_context* ctx) { return deck_any_elset_hidden() && G.vis && !app_busy(); }
static bool sets_shown(struct nk_context* ctx) { return !deck_any_elset_hidden(); }
/* a linearization line between two nodes of the model, its Path window open */
static void lin_open(struct nk_context* ctx) {
    app_scl_clear();
    G.path_lin = true; G.path_surface = false;
    app_path_start(0); app_path_end(G.frd.n_nodes / 2);
}
static bool lin_kept(struct nk_context* ctx) { return G.scl_n == 1 && app_scl_current() == 0; }
static void lin_other(struct nk_context* ctx) { app_path_start(0); app_path_end(G.frd.n_nodes / 3); }
static bool lin_other_shown(struct nk_context* ctx) { return G.path_n && app_scl_current() < 0; }
static bool lin_list_open(struct nk_context* ctx) { return popup_open(ctx, "Path"); }
static bool lin_back(struct nk_context* ctx) { return G.path_n && G.path_open && app_scl_current() == 0; }
static bool lin_forgotten(struct nk_context* ctx) { return G.scl_n == 0 && G.path_n; }
static void lin_close(struct nk_context* ctx) { app_path_clear(); app_scl_clear(); }
/* View > File alone open, the model's .ccxview written */
static int file_tree[CV_TREE_N];
static void file_open(struct nk_context* ctx) {
    memcpy(file_tree, G.tree, sizeof file_tree);
    for (int k = 0; k < CV_TREE_N; k++) G.tree[k] = k == CV_TREE_VIEW || k == CV_TREE_FILE;
    struct nk_window* w = win_of(ctx, "Scene");
    if (w) w->scrollbar.y = 0;
    app_sidecar_save();
}
static void file_close(struct nk_context* ctx) { memcpy(G.tree, file_tree, sizeof file_tree); }
static bool sidecar_gone(struct nk_context* ctx) {
    char p[1100];
    return app_sidecar_path(p, sizeof p) && cv_file_size(p) == 0 && G.loaded && !app_busy();
}
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
#define TYPE(text)          { OP_TYPE, NULL, text }     /* characters, as typed: to the active text box */
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
    DO(open_select), WAIT(3),
    AT_TIP("Selection", "Nodes inside the box"), CLICK, WAIT(2), EXPECT(nodes_selected, "ticking nodes selects the nodes too"),
    DO(remember_nodes), AT_TIP("Selection", "Only the side facing you"), CLICK, WAIT(2), EXPECT(nodes_more, "through the model: more nodes"),
    AT_TIP("Selection", "Only the side facing you"), CLICK, WAIT(2),
    AT_TIP("Selection", "A blue ball on the selection's minimum"), CLICK, WAIT(2), EXPECT(min_marked, "the min gets its ball"),
    AT_TIP("Selection", "A blue ball on the selection's minimum"), CLICK, WAIT(2),
    DO(remember_nodes_turn), WAIT(2), AT_TIP("Selection", "Elements: drag left to right"), CLICK, WAIT(2),
    EXPECT(nodes_kept, "unticking elements after a turn keeps the same nodes, no elements"),
    DO(nodes_off), WAIT(2),

    CLOSED_THEN_PANELS("Selection", open_select, select_gone),

    CASE("selection: the top bar's Select opens it, a box in add mode joins the first"),
    DO(sel_none), DO(fit_view), AT_TIP("Toolbar", "The Selection window (S)"), CLICK, WAIT(3), EXPECT(select_shown, "the window opens"),
    DO(arm_box), AT_VIEW(0.48f, 0.4f), PRESS, AT_VIEW(0.56f, 0.6f), AT_VIEW(0.66f, 0.8f), RELEASE, WAIT(2), DO(sel_remember),
    AT_TIP("Selection", "What you pick next joins the selection"), CLICK, WAIT(2), EXPECT(sel_mode_add, "add mode"),
    AT_TIP("Selection", "Drag a box in the view next"), CLICK, WAIT(2), EXPECT(box_armed, "the box tool arms the box"),
    AT_VIEW(0.68f, 0.4f), PRESS, AT_VIEW(0.75f, 0.6f), AT_VIEW(0.85f, 0.8f), RELEASE, WAIT(2), EXPECT(box_added, "the second box adds"),
    DO(sel_none), WAIT(2),

    CASE("selection: the context menu opens the window"),
    DO(sel_none), DO(fit_view), WAIT(2), AT_MODEL, RCLICK, WAIT(2), AT_TIP("Menu", "#menu Selection..."), CLICK, WAIT(3),
    EXPECT(select_shown, "the Selection window opens"),

    CASE("selection: by id select, add, remove, intersect; invert; to nodes and back; clear"),
    DO(sel_none), DO(open_select), WAIT(3), DO(ids_1_20), WAIT(2),
    AT_TIP("Selection", "Take them by the mode"), CLICK, WAIT(2), EXPECT(sel_20, "ids 1-20, the bad ones reported"),
    AT_TIP("Selection", "What you pick next joins the selection"), CLICK, DO(ids_10_30), WAIT(2),
    AT_TIP("Selection", "Take them by the mode"), CLICK, WAIT(2), EXPECT(sel_30, "add: 1-30"),
    AT_TIP("Selection", "What you pick next leaves the selection"), CLICK, WAIT(2), EXPECT(sel_mode_remove, "remove mode"),
    AT_TIP("Selection", "Take them by the mode"), CLICK, WAIT(2), EXPECT(sel_9, "remove: 1-9"),
    DO(ids_1_20), AT_TIP("Selection", "What you pick next replaces"), CLICK, WAIT(2), AT_TIP("Selection", "Take them by the mode"), CLICK, WAIT(2),
    DO(ids_10_30), AT_TIP("Selection", "Only what is both selected"), CLICK, WAIT(2), EXPECT(sel_mode_and, "intersect mode"),
    AT_TIP("Selection", "Take them by the mode"), CLICK, WAIT(2), EXPECT(sel_11, "intersect: 10-20"),
    DO(sel_remember), AT_TIP("Selection", "Invert: what is shown"), CLICK, WAIT(2), EXPECT(sel_inverted, "invert: every other element"),
    AT_TIP("Selection", "Invert: what is shown"), CLICK, WAIT(2), EXPECT(sel_11, "inverted twice: the same"),
    DO(sel_remember), AT_TIP("Selection", "Elements to nodes"), CLICK, WAIT(2), EXPECT(sel_to_nodes, "their nodes, the elements dropped"),
    AT_TIP("Selection", "Nodes to elements: the shown elements with every node"), CLICK, WAIT(2), EXPECT(sel_back, "the same elements back"),
    AT_TIP("Selection", "Elements to nodes"), CLICK, WAIT(2),
    AT_TIP("Selection", "Nodes to elements: the shown elements with any node"), CLICK, WAIT(2), EXPECT(sel_touching, "touching: more elements"),
    AT_TIP("Selection", "Nothing selected"), CLICK, WAIT(2), EXPECT(sel_empty, "clear"),

    CASE("selection: the lasso takes what it is drawn round, Esc puts it down"),
    DO(sel_none), DO(fit_view), DO(open_select), WAIT(3), AT_TIP("Selection", "Draw round what you want"), CLICK, WAIT(2),
    EXPECT(lasso_armed, "the lasso armed"),
    AT_VIEW(0.45f, 0.3f), PRESS, AT_VIEW(0.65f, 0.28f), AT_VIEW(0.85f, 0.35f), AT_VIEW(0.88f, 0.6f), AT_VIEW(0.8f, 0.8f),
    AT_VIEW(0.6f, 0.82f), AT_VIEW(0.46f, 0.7f), RELEASE, WAIT(2), EXPECT(lasso_took, "elements inside it, with their extremes"),
    AT_TIP("Selection", "Draw round what you want"), CLICK, WAIT(2), EXPECT(tool_down, "the tool put down, the window stays"),
    DO(sel_none), WAIT(2),

    CASE("selection: faces up to the feature edges, an edge chain, the part"),
    DO(sel_none), DO(fit_view), DO(open_select), WAIT(3), AT_TIP("Selection", "Click an outer face"), CLICK, WAIT(2),
    AT_MODEL, CLICK, WAIT(2), EXPECT(faces_took, "the faces' elements"),
    DO(sel_none), AT_TIP("Selection", "Click near a feature edge"), CLICK, WAIT(2), EXPECT(chain_armed, "the edge chain armed"), AT_MODEL, CLICK, WAIT(2),
    EXPECT(chain_took, "the nodes along a feature edge"),
    DO(sel_none), AT_TIP("Selection", "Click an element: every shown element"), CLICK, WAIT(2), AT_MODEL, CLICK, WAIT(2),
    EXPECT(part_took, "the whole part"),
    DO(sel_none), WAIT(2),

    CASE("selection: grow, shrink, boundary"),
    DO(sel_none), DO(open_select), WAIT(3), DO(ids_some), WAIT(2), AT_TIP("Selection", "Take them by the mode"), CLICK, WAIT(2),
    DO(sel_remember), AT_TIP("Selection", "Grow: one layer more"), CLICK, WAIT(2), EXPECT(sel_grew, "grow: more elements"),
    DO(sel_remember), AT_TIP("Selection", "Shrink: one layer less"), CLICK, WAIT(2), EXPECT(sel_shrank, "shrink: fewer"),
    AT_TIP("Selection", "Boundary: the nodes on the outside"), CLICK, WAIT(2), EXPECT(sel_boundary, "its boundary nodes and elements"),
    DO(sel_none), WAIT(2),

    CASE("selection: filters select where, add the field's top %, drop a type"),
    DO(sel_none), DO(filters_on), DO(open_select), WAIT(3),
    AT_TIP("Selection", "Keep what lies in this range"), CLICK, WAIT(2), EXPECT(sel_all, "nothing selected: every element in the range"),
    DO(ids_1_20), AT_TIP("Selection", "Take them by the mode"), CLICK, WAIT(2), DO(sel_remember),
    AT_TIP("Selection", "What you pick next joins the selection"), CLICK, WAIT(2),
    AT_TIP("Selection", "The field shown: above or below"), CLICK, WAIT(2), EXPECT(select_popup, "the field list opens"),
    AT_POPUP("Selection", 0.5f, 0.82f), CLICK, WAIT(2),
    AT_TIP("Selection", "Keep what the field puts there"), CLICK, WAIT(2), EXPECT(sel_some, "add: the field's top 5 % joins"),
    AT_TIP("Selection", "What you pick next leaves the selection"), CLICK, WAIT(2),
    AT_TIP("Selection", "Keep the elements of this type"), CLICK, WAIT(2), EXPECT(sel_empty, "remove: every Hex20 leaves"),
    DO(filters_off), WAIT(2),

    CASE("selection: its parts fold and open by their headers"),
    DO(sel_none), DO(open_select), WAIT(3), AT_TIP("Selection", "Pick: the mode"), CLICK, WAIT(2), EXPECT(pick_folded, "pick folded, use still open"),
    AT_TIP("Selection", "Pick: the mode"), CLICK, WAIT(2), EXPECT(pick_open, "pick open again"),
    AT_TIP("Selection", "Kept by name: the selection kept"), CLICK, WAIT(2), EXPECT(named_unfolded, "kept by name opens"),

    CASE("selection: kept by name, taken back, renamed, forgotten"),
    DO(sel_none), DO(app_nsel_clear_ctx), DO(named_open), DO(open_select), WAIT(3), DO(ids_1_20), AT_TIP("Selection", "Take them by the mode"), CLICK, WAIT(2),
    DO(name_holea), AT_TIP("Selection", "Keep the selection under this name"), CLICK, WAIT(3), EXPECT(kept_one, "kept as HOLEA"),
    AT_TIP("Selection", "Nothing selected"), CLICK, WAIT(2), EXPECT(sel_empty, "cleared"),
    AT_TIP("Selection", "#named sel HOLEA"), CLICK, WAIT(2), EXPECT(sel_20k, "HOLEA selected again"),
    DO(name_hb), AT_TIP("Selection", "#named ren HOLEA"), CLICK, WAIT(2), EXPECT(renamed, "renamed HB"),
    AT_TIP("Selection", "#named del HB"), CLICK, WAIT(2), EXPECT(none_kept, "forgotten"),
    DO(use_end), WAIT(2),

    CASE("selection: crop, clip, labels, deck lines to a file, CSV, isolate"),
    DO(sel_none), DO(open_select), WAIT(3), DO(ids_1_20), AT_TIP("Selection", "Take them by the mode"), CLICK, WAIT(2), DO(name_holea),
    AT_TIP("Selection", "The clip plane through the selection"), CLICK, WAIT(2), EXPECT(sel_clipped, "the clip plane on"),
    AT_TIP("Selection", "Labels on the selection only"), CLICK, WAIT(2), EXPECT(labels_sel, "labels on the selection"),
    AT_TIP("Selection", "The same lines to"), CLICK, WAIT(2), EXPECT(inp_saved, "<model>_HOLEA.inp with its *ELSET"),
    AT_TIP("Selection", "The selected elements (or nodes) with their place"), CLICK, WAIT(2), EXPECT(csv_saved, "the CSV"),
    AT_TIP("Selection", "The crop box round the selection"), CLICK, WAIT(10), EXPECT(cropped, "cropped, the selection kept"),
    AT_TIP("Selection", "Show only the selected elements"), CLICK, WAIT(10), EXPECT(isolated, "only the 20 shown"),
    DO(use_end), WAIT(10),

    CASE("selection: a name from the list, the click tool toggles an element"),
    DO(sel_none), DO(open_select), WAIT(3), AT_TIP("Selection", "A deck element or node set"), CLICK, WAIT(2),
    EXPECT(select_popup, "the name list opens"), AT_POPUP("Selection", 0.5f, 0.25f), CLICK, WAIT(2),
    AT_TIP("Selection", "Take it by the mode"), CLICK, WAIT(2), EXPECT(sel_any, "the name picked is selected"),
    DO(sel_none), DO(fit_view), WAIT(2), AT_TIP("Selection", "Click elements (nodes) in the view"), CLICK, WAIT(2), EXPECT(click_armed, "the click tool armed"),
    AT_MODEL, CLICK, WAIT(2), EXPECT(sel_one, "a click selects the element under it"),
    AT_MODEL, CLICK, WAIT(2), EXPECT(sel_empty, "a second click takes it out"),
    DO(sel_none), WAIT(2),

    CASE("labels: the tree picks a kind, labels show, the size answers, a drag still turns the model"),
    DO(labels_tree_open), WAIT(3), AT_TIP("Scene", "Every node of the surface"), CLICK, WAIT(3), EXPECT(labels_node, "node ids chosen"),
    EXPECT(labels_shown, "some labels are shown"),
    DO(labels_small), WAIT(2), AT_TIP_X("Scene", "Text height in pixels", 0.95f), CLICK, EXPECT(labels_bigger, "the size answers"),
    AT_TIP("Scene", "Labels in front of everything"), CLICK, EXPECT(labels_behind, "the front box toggles"), DO(labels_front),
    AT_TIP("Scene", "The field's value at every Gauss point"), CLICK, WAIT(4), EXPECT(labels_gauss, "Gauss point values show, the layer on"),
    DO(gauss_off),
    DO(snapshot), AT_VIEW(0.75f, 0.3f), PRESS, AT_VIEW(0.8f, 0.35f), AT_VIEW(0.85f, 0.4f), RELEASE, EXPECT(turned, "the model turns with labels up"),
    DO(labels_off), WAIT(2),

    CASE("ids shown: a drag in the view still turns the model; closing the probe clears them"),
    DO(fit_view), WAIT(2), AT_MODEL, CLICK, WAIT(2), DO(ids_on), WAIT(3), EXPECT(labels_shown, "the probed element's ids show"),
    DO(probe_close), WAIT(3), EXPECT(labels_none, "closing the probe leaves no labels"), AT_MODEL, CLICK, WAIT(3), DO(snapshot),
    AT_VIEW(0.75f, 0.3f), PRESS, AT_VIEW(0.8f, 0.35f), AT_VIEW(0.85f, 0.4f), RELEASE, EXPECT(turned, "the camera turns with the ids overlay on"),
    DO(ids_off), DO(close_all), DO(fit_view), WAIT(2),

    CASE("labels: a label dragged moves, not the camera; the menu and the panel reset it"),
    DO(fit_view), DO(labels_movable), WAIT(4), DO(at_label), WAIT(2), EXPECT(on_label, "a node label under the cursor"),
    DO(snapshot), PRESS, WAIT(1), DO(nudge), WAIT(1), DO(nudge), WAIT(1), RELEASE, WAIT(2),
    EXPECT(label_moved_cam_kept, "the label keeps its offset, the camera stays"),
    DO(at_moved_label), WAIT(2), RCLICK, WAIT(2), EXPECT(menu_open, "the menu opens on the moved label"),
    AT_TIP("Menu", "#menu Reset label"), CLICK, WAIT(2), EXPECT(label_reset, "the label goes back, the menu closes"),
    DO(label_move_one), DO(labels_tree_open), WAIT(3), EXPECT(labels_moved_one, "one label moved"),
    AT_TIP("Scene", "Every label dragged on the model"), CLICK, WAIT(2), EXPECT(label_reset, "the panel resets them"),
    DO(labels_back), WAIT(2),

    CASE("details: open beside the probe, the model still turns"),
    DO(fit_view), WAIT(2), AT_MODEL, CLICK, WAIT(2), AT_TIP("Probe", "Everything about this node"), CLICK, WAIT(3),
    EXPECT(details_shown, "the Details window opens"), EXPECT(details_clear_of_view, "it sits at the left, off the model's middle"),
    DO(snapshot), AT_VIEW(0.75f, 0.3f), PRESS, AT_VIEW(0.8f, 0.35f), AT_VIEW(0.85f, 0.4f), RELEASE, EXPECT(turned, "a drag beside it turns the model"),
    DO(close_all), DO(fit_view), WAIT(2),
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
    AT_TIP("Toolbar", "Every symbol layer at once"), CLICK, WAIT(2), EXPECT(symbols_off, "one box hides every symbol layer"),
    AT_TIP("Toolbar", "Every symbol layer at once"), CLICK, WAIT(2), EXPECT(symbols_on, "and shows them all again"),

    CASE("toolbar: the ortho box, the view, labels, field and component lists"),
    AT_TIP("Toolbar", "Parallel projection"), CLICK, WAIT(2), EXPECT(ortho_on, "ortho on"), AT_TIP("Toolbar", "Parallel projection"), CLICK, WAIT(2), EXPECT(ortho_off, "ortho off"),
    AT_TIP("Toolbar", "Labels on the model"), CLICK, WAIT(2), EXPECT(toolbar_popup, "the labels list opens"), AT_POPUP("Toolbar", 0.5f, 0.06f), CLICK, WAIT(3), EXPECT(labels_node, "node ids ticked from the bar"),
    AT_VIEW(0.5f, 0.6f), CLICK, WAIT(2), DO(labels_off), WAIT(2),
    AT_TIP("Toolbar", "Look from a side"), CLICK, WAIT(2), EXPECT(toolbar_popup, "the view list opens"), AT_POPUP("Toolbar", 0.5f, 0.21f), CLICK, WAIT(3), EXPECT(toolbar_no_popup, "the list closes on a pick"),
    AT_TIP("Toolbar", "The result field shown"), CLICK, WAIT(2), EXPECT(toolbar_popup, "the field list opens"), AT_POPUP("Toolbar", 0.5f, 0.5f), CLICK, WAIT(3), EXPECT(field_other, "another field picked from the list"),
    DO(snapshot), AT_TIP("Toolbar", "The component or invariant"), CLICK, WAIT(2), EXPECT(toolbar_popup, "the component list opens"), AT_POPUP("Toolbar", 0.5f, 0.5f), CLICK, WAIT(3), EXPECT(comp_changed, "another component picked"),
    DO(first_field), WAIT(3), EXPECT(field_disp, "DISP again"), PANELS_ANSWER,

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
    DO(range_locked), WAIT(2), AT_TIP("Legend settings", "Values above the locked range"), CLICK, WAIT(2),
    EXPECT(oor_list_open, "the out-of-range list opens"), AT_VIEW(0.5f, 0.6f), CLICK, WAIT(2), EXPECT(oor_list_gone, "a click outside closes it"), DO(range_free),
    AT_CLOSE("Legend settings"), CLICK, WAIT(2), EXPECT(legend_settings_gone, "the legend settings close"),
    PANELS_ANSWER,

    CASE("title block: the Display box shows it, a right click on it opens its settings"),
    DO(display_open), WAIT(2), AT_TIP("Scene", "A title block in the view"), CLICK, WAIT(3), EXPECT(title_shown, "the title block is in the view"),
    AT_WIN("title", 0.5f, 0.5f), RCLICK, WAIT(2), EXPECT(title_settings_shown, "its settings open, not the menu"),
    AT_TIP("Title block", "Who you are logged in as"), CLICK, WAIT(2), EXPECT(title_user_off, "the user line goes"),
    AT_CLOSE("Title block"), CLICK, WAIT(2), EXPECT(title_settings_gone, "the settings close"),
    DO(title_off), WAIT(2), PANELS_ANSWER,

    CASE("title block: typing into its text changes the block, reset to the boxes makes it again"),
    DO(title_text_open), WAIT(3), AT_TIP("Title block", "The title block's text, one line"), CLICK, WAIT(2), TYPE("Qzx"), WAIT(2),
    EXPECT(title_typed, "the text is edited by hand and the block shows it"),
    AT_TIP("Title block", "Make the text from the boxes again"), CLICK, WAIT(2), EXPECT(title_reset, "the boxes' text is back"),
    DO(title_text_off), WAIT(2), PANELS_ANSWER,

    CASE("title block: the date format list picks a format"),
    DO(title_text_open), WAIT(3), AT_TIP("Title block", "How {date} and {date_file}"), CLICK, WAIT(2), EXPECT(title_fmt_list, "the list opens"),
    AT_POPUP("Title block", 0.5f, 0.45f), CLICK, WAIT(2), EXPECT(title_fmt_picked, "another format is chosen, the list closes"),
    DO(title_text_off), WAIT(2), PANELS_ANSWER,

    CLOSED_THEN_PANELS("Messages", open_messages, messages_gone),
    CLOSED_THEN_PANELS("Formula", open_formula, formula_gone),
    CLOSED_THEN_PANELS("Convergence", open_convergence, convergence_gone),
    CLOSED_THEN_PANELS("Legend settings", open_legend_settings, legend_settings_gone),
    CLOSED_THEN_PANELS("Find", open_find, find_gone),
    CLOSED_THEN_PANELS("Open file", open_browser, browser_gone),
    CLOSED_THEN_PANELS("Mesh quality", open_mesh, mesh_gone),
    CLOSED_THEN_PANELS("Details", open_details, details_gone),
    CLOSED_THEN_PANELS("About", open_about, about_gone),
    CLOSED_THEN_PANELS("Title block", open_title_settings, title_settings_gone),
    CLOSED_THEN_PANELS("Measurements", open_measure, measure_gone),
    CLOSED_THEN_PANELS("Reinforcement", open_rebar, rebar_gone),

    CASE("measurements: armed from the probe, a second click on the model makes a distance"),
    DO(close_all), DO(meas_clear), DO(fit_view), WAIT(2), AT_MODEL, CLICK, WAIT(2),
    AT_TIP("Probe", "#probe measure distance"), CLICK, WAIT(2), EXPECT(meas_dist_armed, "a distance waits for its second node"),
    DO(meas_turn), WAIT(2), AT_MODEL, CLICK, WAIT(3), EXPECT(meas_dist_made, "a distance between two nodes"),
    EXPECT(meas_labelled, "it is there with no label kind on"),
    DO(fit_view), WAIT(2),

    CASE("measurements: a circle armed from the menu, Esc cancels it"),
    DO(close_all), DO(meas_clear), WAIT(2), AT_MODEL, RCLICK, WAIT(2), EXPECT(menu_open, "the menu opens"),
    AT_TIP("Menu", "#menu measure circle"), CLICK, WAIT(2), EXPECT(meas_circle_armed, "the circle waits for two more nodes, the menu closed"),
    DO(meas_cancel), WAIT(2), EXPECT(meas_not_armed, "Esc cancels it"), PANELS_ANSWER,

    CASE("measurements: the window arms one, its list picks what the labels show, a row is deleted"),
    DO(close_all), DO(meas_add_ids), DO(open_measure), WAIT(3), EXPECT(measure_shown, "the window opens"),
    AT_TIP("Measurements", "#meas new angle"), CLICK, WAIT(2), EXPECT(meas_angle_armed, "an angle waits for its first node"),
    DO(meas_cancel), WAIT(2),
    AT_TIP("Measurements", "What the labels in the view give"), WAIT(30), CLICK, WAIT(2), EXPECT(measure_popup, "the list opens"),
    AT_POPUP("Measurements", 0.5f, 0.85f), CLICK, WAIT(2), EXPECT(meas_shows_def, "the labels show one value"),
    AT_TIP("Measurements", "Save them as"), CLICK, WAIT(2), EXPECT(meas_csv_saved, "the CSV holds both states"),
    AT_TIP("Measurements", "#meas delete 1"), CLICK, WAIT(2), EXPECT(meas_none, "the row is deleted"),
    DO(meas_clear), DO(close_all), WAIT(2), PANELS_ANSWER,
    CLOSED_THEN_PANELS("Integrals", open_integrals, integrals_gone),

    CASE("integrals: opened from Fields, the kind list picks the nodes, closed again"),
    DO(close_all), DO(fields_open), WAIT(3), AT_TIP("Scene", "The field integrated over the shown elements"), CLICK, WAIT(3),
    EXPECT(integrals_shown, "the Integrals window opens with rows"),
    AT_TIP("Integrals", "Volume: the elements' volume"), CLICK, WAIT(2), EXPECT(integrals_popup, "the kind list opens"),
    AT_POPUP("Integrals", 0.5f, 0.85f), CLICK, WAIT(3), EXPECT(integrals_nodes, "a sum over the nodes"),
    DO(integrals_close), WAIT(2),

    CASE("integrals: a right click on an element set integrates over it"),
    DO(groups_open), WAIT(3), AT_TIP("Scene", "#set menu"), RCLICK, WAIT(2), EXPECT(scene_popup, "the set's menu opens"),
    AT_POPUP("Scene", 0.5f, 0.08f), CLICK, WAIT(3), EXPECT(integrals_set, "the volume of set EALL"),
    DO(integrals_close), DO(sections_open), WAIT(2), PANELS_ANSWER,

    CASE("selection: a right click on an element set selects it"),
    DO(sel_none), DO(groups_open), WAIT(3), AT_TIP("Scene", "#set menu"), RCLICK, WAIT(2), EXPECT(scene_popup, "the set's menu opens"),
    AT_POPUP("Scene", 0.5f, 0.6f), CLICK, WAIT(3), EXPECT(sel_all, "set EALL selected"),
    DO(sel_none), DO(sections_open), WAIT(2), PANELS_ANSWER,

    CASE("integrals: from the probe over a box selection, and from the menu over a set"),
    DO(close_all), DO(fit_view), WAIT(2), DO(arm_box), AT_VIEW(0.3f, 0.3f), PRESS, AT_VIEW(0.5f, 0.5f), AT_VIEW(0.7f, 0.7f), RELEASE, WAIT(2),
    AT_TIP("Probe", "The field integrated over the selected elements"), CLICK, WAIT(3), EXPECT(integrals_sel, "over the selection"),
    DO(integrals_close), DO(app_sel_clear_ctx), DO(fit_view), WAIT(2), AT_MODEL, RCLICK, WAIT(2), AT_TIP("Menu", "#menu Integrate over set"), CLICK, WAIT(3),
    EXPECT(integrals_set, "the volume of the element's set"),
    DO(integrals_end), WAIT(2), PANELS_ANSWER,
    CLOSED_THEN_PANELS("Deck", open_deck, deck_gone),

    CASE("contact: the .cel's iterations listed, one picked; the model see-through from the window"),
    DO(close_all), DO(open_contact), WAIT(3), EXPECT(contact_listed, "the window lists the 11 iterations"),
    AT_TIP("Contact", "Which contact elements"), CLICK, WAIT(2), EXPECT(contact_popup, "the list opens"),
    AT_POPUP("Contact", 0.5f, 0.45f), CLICK, WAIT(3), EXPECT(contact_picked, "an iteration is drawn"),
    AT_TIP("Contact", "The faces' opacity: less to see into the model (a contact"), CLICK, WAIT(3), EXPECT(see_through, "the model is see-through"),
    DO(contact_end), WAIT(2), PANELS_ANSWER,
    CASE("contact display: status, gap and solids ticked beside the links; links and solids at true scale"),
    DO(close_all), DO(open_contact), WAIT(3),
    AT_TIP("Contact", "Status: the slave faces in a patch per node"), CLICK, WAIT(2), EXPECT(cmode_status, "status drawn with the links"),
    AT_TIP("Contact", "Gap: the slave faces coloured by the gap"), CLICK, WAIT(2), EXPECT(cmode_gap, "the gap contour drawn"),
    AT_TIP("Contact", "Solids: the contact elements of the .cel"), CLICK, WAIT(2), EXPECT(cmode_solids, "the solids drawn"),
    AT_TIP("Contact", "With the shape exaggerated (deformation scale not 1)"), CLICK, WAIT(2), EXPECT(ctrue_on, "at true scale"),
    DO(cdisp_back), DO(contact_end), WAIT(2), PANELS_ANSWER,
    CLOSED_THEN_PANELS("Contact", open_contact, contact_gone),
    CASE("layers: the opacity slider makes the model see-through"),
    DO(contact_end), DO(close_all), DO(layers_first), WAIT(3), AT_TIP("Scene", "Opacity of the faces: less to see into the model"),
    CLICK, WAIT(3), EXPECT(see_through, "the model is see-through"), DO(layers_alpha), WAIT(2), PANELS_ANSWER,

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

    CASE("groups: the eye hides an element set, a second click shows it again"),
    DO(groups_open), WAIT(3), AT_TIP("Scene", "Hide this element set"), CLICK, WAIT(10), EXPECT(set_hidden, "the set is hidden"),
    AT_TIP("Scene", "Hidden: click to show"), CLICK, WAIT(10), EXPECT(sets_shown, "every set shown again"),
    DO(groups_close), WAIT(2), PANELS_ANSWER,

    CASE("linearization: a line kept, another shown, the kept one picked from the list, forgotten"),
    DO(lin_open), WAIT(3), AT_TIP("Path", "Keep this line"), CLICK, WAIT(2), EXPECT(lin_kept, "the line is kept, the list shows it"),
    DO(lin_other), WAIT(3), EXPECT(lin_other_shown, "another line is not a kept one"),
    AT_TIP("Path", "Kept stress classification lines"), CLICK, WAIT(2), EXPECT(lin_list_open, "the list of kept lines opens"),
    AT_POPUP("Path", 0.5f, 0.3f), CLICK, WAIT(3), EXPECT(lin_back, "the kept line is shown again"),
    AT_TIP("Path", "Forget this kept line"), CLICK, WAIT(2), EXPECT(lin_forgotten, "forgotten, the line still shown"),
    DO(lin_close), WAIT(2), PANELS_ANSWER,

    CASE("display: the UI size buttons shrink the interface and reset it"),
    DO(display_open), WAIT(3), AT_TIP("Scene", "The whole interface a step smaller"), CLICK, WAIT(4), EXPECT(ui_smaller, "the UI shrinks"),
    AT_TIP("Scene", "Click: back to 100 %"), CLICK, WAIT(4), EXPECT(ui_normal, "back to 100 %"), DO(sections_open), WAIT(2), PANELS_ANSWER,

    CASE("symbol sizes: thin crowded symbols"),
    DO(symbols_open), WAIT(3), AT_TIP("Scene", "Where supports or loads crowd"), CLICK, EXPECT(thin_on, "thinning turns on"),
    DO(thin_off), WAIT(2), PANELS_ANSWER,

    CASE("about: opened from the status bar"),
    AT_TIP("Status", "Who made ccxview"), CLICK, WAIT(2), EXPECT(about_shown, "the About window opens"),
    AT_CLOSE("About"), CLICK, WAIT(2), EXPECT(about_gone, "About is closed"), PANELS_ANSWER,

    CASE("deck: opened from Groups, its steps listed"),
    DO(close_all), DO(groups_first), WAIT(3), AT_TIP("Scene", "The deck's steps and their times"), CLICK, WAIT(3),
    EXPECT(deck_shown, "the Deck window opens"),
    AT_CLOSE("Deck"), CLICK, WAIT(2), EXPECT(deck_gone, "Deck is closed"), DO(layers_first), WAIT(3), PANELS_ANSWER,

    CASE("mesh quality: the limits open, a limit answers"),
    DO(open_mesh), WAIT(5), AT_TIP("Mesh quality", "Set your own limits"), CLICK, WAIT(2),
    AT_TIP_X("Mesh quality", "The limit for every element type", 0.95f), CLICK, EXPECT(mesh_limit_set, "the aspect limit changes"),
    DO(mesh_limits_usual), WAIT(3), PANELS_ANSWER,

    CASE("reinforcement: its window opens, the cover answers"),
    DO(close_all), DO(rebar_usual), DO(open_rebar), WAIT(5),
    AT_TIP_X("Reinforcement", "c: from each face", 0.95f), CLICK, WAIT(2), EXPECT(rebar_cover_up, "the cover changes"),
    DO(rebar_usual), DO(close_all), WAIT(3), PANELS_ANSWER,

    CASE("imported geometry: listed in Groups; shown, opacity, scale list, colour, removed"),
    DO(stl_add), WAIT(3), EXPECT(stl_listed, "the STL is listed and shown"),
    AT_TIP("Scene", "Show this imported file"), CLICK, WAIT(2), EXPECT(stl_hidden, "its box hides it"),
    DO(stl_faint), WAIT(2), AT_TIP_X("Scene", "Opacity: 1 solid", 0.95f), CLICK, WAIT(2), EXPECT(stl_more_opaque, "the opacity slider answers"),
    AT_TIP("Scene", "Its lengths times this"), WAIT(30), CLICK, EXPECT(scene_popup, "the scale list opens"),
    AT_POPUP("Scene", 0.5f, 0.92f), CLICK, WAIT(2), EXPECT(stl_scaled, "a larger scale picked, the list closed"),
    AT_TIP("Scene", "Its colour"), CLICK, WAIT(2), EXPECT(scene_popup, "the colour picker opens"),
    AT_TIP("Scene", "Its colour"), CLICK, WAIT(2),
    AT_TIP("Scene", "Remove this file"), CLICK, WAIT(2), EXPECT(stl_gone, "the remove button takes it out"),
    DO(sections_open), WAIT(2), PANELS_ANSWER,

    CASE("import STL (#23): the Groups button opens the browser for an STL, a pick adds a layer"),
    DO(stl_tree_open), WAIT(3), AT_TIP("Scene", "Add an STL file"), CLICK, WAIT(3), EXPECT(browser_for_stl, "the built-in browser opens to import an STL"),
    DO(browse_model_dir), WAIT(4), EXPECT(browser_lists_stl_only, "it lists the .stl, not the model's files"),
    AT_TIP("Open file", "#file tet.stl"), CLICK, WAIT(2), AT_TIP("Open file", "#browser open"), CLICK, WAIT(3),
    EXPECT(stl_picked, "the picked STL is listed as imported geometry, the browser closed"),
    DO(stl_tree_open), DO(sections_open), WAIT(2), PANELS_ANSWER,

    CASE("import STL (#23): View > File > Import STL opens the browser for an STL too"),
    DO(stl_file_tree_open), WAIT(3), AT_TIP("Scene", "Import geometry that was not analysed"), CLICK, WAIT(3),
    EXPECT(browser_for_stl, "the built-in browser opens to import an STL"),
    AT_CLOSE("Open file"), CLICK, WAIT(2), EXPECT(browser_gone, "it closes"),
    DO(file_close), DO(sections_open), WAIT(2), PANELS_ANSWER,

    CASE("imported geometry under the cursor: the view turns about it and centres on it; the menu hides it"),
    DO(close_all), DO(pin_layer), WAIT(3), EXPECT(pin_under_cursor, "the point under the cursor lies on the pin"),
    EXPECT(pin_centred, "centre here (middle click, C) puts the pin's point in the middle"),
    DO(fit_view), WAIT(2), DO(at_pin), WAIT(2), RCLICK, WAIT(3), EXPECT(pin_menu, "the menu there offers to hide the pin"),
    DO(pin_hide), WAIT(2), EXPECT(pin_hidden_misses, "hidden, it is not under the cursor any more"),
    DO(pin_clear), WAIT(3), PANELS_ANSWER,
    CASE("file: forget post-processing deletes the model's .ccxview and opens it afresh"),
    DO(file_open), WAIT(3), AT_TIP("Scene", "Forget what was set up"), CLICK, WAIT(20), EXPECT(sidecar_gone, "the file is gone, the model open"),
    DO(file_close), WAIT(2), DO(sections_open), WAIT(2), PANELS_ANSWER,

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

    CASE("a model and an STL dropped together: the model opens, the STL joins it as imported geometry"),
    DO(drop_model_and_pin), WAIT(10), EXPECT(pin_joined, "the model is open with pin.stl listed"),
    DO(pin_clear), WAIT(3), PANELS_ANSWER,

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
        float x = 0, y = 0;
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
        case OP_TYPE:                           /* one character a frame */
            if (s->b[T.phase]) {
                sapp_event ev = { .type = SAPP_EVENTTYPE_CHAR, .frame_count = T.frame, .char_code = (uint32_t)(unsigned char)s->b[T.phase],
                                  .mouse_x = T.mx, .mouse_y = T.my, .window_width = sapp_width(), .window_height = sapp_height(),
                                  .framebuffer_width = sapp_width(), .framebuffer_height = sapp_height() };
                T.send(&ev);
                T.phase++;
                n_marks = 0;
                return;
            }
            T.phase = 0; T.wait = 2;
            break;
        case OP_WAIT:    T.wait = s->n; break;
        case OP_DO:      s->fn(ctx); break;
        case OP_EXPECT:  if (!s->ok(ctx)) fail("expected: %s", s->b); break;
        }
        T.pc++;
    }
    T.wait--;
    n_marks = 0;                                /* the frame about to be laid out reports them again */
}
