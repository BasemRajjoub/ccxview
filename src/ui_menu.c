/* ui_menu.c -- the context menu: a right click in the view (without a drag) opens
   it at the cursor with what fits there -- the element and node under it, the box
   selection if there is one, and the view. One click on an item does it and closes
   the menu; a click anywhere else or Esc only closes it. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include <math.h>
#include <stdio.h>
#include <stdarg.h>

enum {
    M_SEP, M_LOOK, M_PAIR, M_MEAS,                     /* a separator, the look-from buttons, a hide | only row, the measure buttons */
    M_PROBE, M_DETAILS, M_CENTRE, M_NORMAL, M_ZOOM, M_HIST, M_PATH, M_WALL, M_CLIP, M_COPY,
    M_HIDE_EL, M_HIDE_MAT, M_ONLY_MAT, M_HIDE_TYPE, M_ONLY_TYPE, M_HIDE_SET, M_ONLY_SET, M_INTEG_SET,
    M_SEL_HIDE, M_SEL_ONLY, M_SEL_MAX, M_SEL_MIN, M_SEL_IDS, M_SEL_CSV, M_SEL_INTEG, M_SEL_CLEAR,
    M_FIT, M_RESET, M_BACK, M_FWD, M_ORTHO, M_FLY, M_SHOW_ALL, M_PNG, M_LABEL_RESET,
};

typedef struct { int what; char label[96]; int set; int hide, only; } item;   /* hide, only: an M_PAIR's two */
enum { NI = 48, NSETS = 3 };
static item it[NI];
static int ni;
static int set_ix[NSETS];                              /* deck set indices the element is in */

static void add(int what, int set, const char* fmt, ...) {
    if (ni >= NI) return;
    item* m = &it[ni++];
    m->what = what; m->set = set; m->label[0] = 0;
    if (fmt) { va_list ap; va_start(ap, fmt); vsnprintf(m->label, sizeof m->label, fmt, ap); va_end(ap); }
}

/* "label  [hide] [only]" on one row */
static void pair(int hide, int only, int set, const char* fmt, ...) {
    if (ni >= NI) return;
    item* m = &it[ni++];
    m->what = M_PAIR; m->hide = hide; m->only = only; m->set = set;
    va_list ap; va_start(ap, fmt); vsnprintf(m->label, sizeof m->label, fmt, ap); va_end(ap);
}

static bool any_hidden(void) {
    if (G.hide) return true;
    for (int a = 0; a < CV_AXIS_N; a++)
        for (int i = 0; i < G.groups.axis[a].n; i++) if (!G.groups.axis[a].on[i]) return true;
    return false;
}

static bool in_set(const cv_set* s, uint32_t id) {
    uint32_t lo = 0, hi = s->n;
    while (lo < hi) { uint32_t m = (lo + hi) / 2; if (s->ids[m] < id) lo = m + 1; else hi = m; }
    return lo < s->n && s->ids[lo] == id;
}

static void build(void) {
    ni = 0;
    const cv_pick* p = &G.menu_pick;
    const cv_frd* f = &G.frd;
    if (app_label_menu_moved()) {                     /* on a label moved by hand */
        add(M_LABEL_RESET, 0, "Reset label");
        add(M_SEP, 0, NULL);
    }
    if (p->hit) {
        uint32_t e = p->elem;
        add(M_PROBE, 0, "Probe here");
        add(M_DETAILS, 0, "Details...");
        add(M_CENTRE, 0, "Centre the view here");
        add(M_NORMAL, 0, "Look straight at this face");
        add(M_ZOOM, 0, "Zoom to element %u", f->elem_id[e]);
        add(M_HIST, 0, "History of node %u", f->node_id[p->node]);
        add(M_PATH, 0, "Path from node %u...", f->node_id[p->node]);
        add(M_WALL, 0, "Through the wall (linearize)");
        add(M_MEAS, 0, NULL);
        add(M_CLIP, 0, "Clip here");
        add(M_COPY, 0, "Copy ids and value");
        add(M_SEP, 0, NULL);
        add(M_HIDE_EL, 0, "Hide element %u", f->elem_id[e]);
        const char* mat = deck_material_name(f->emat[e]);
        char mn[72];
        if (mat) snprintf(mn, sizeof mn, "material %s", mat); else snprintf(mn, sizeof mn, "material %u", f->emat[e]);
        pair(M_HIDE_MAT, M_ONLY_MAT, 0, "%s", mn);
        pair(M_HIDE_TYPE, M_ONLY_TYPE, 0, "every %s", cv_frd_type_name(f->etype[e]));
        const cv_inp* d = deck_get();
        int ns = 0;
        for (int i = 0; d && i < d->nsets && ns < NSETS; i++) {
            if (!d->sets[i].is_elem || !in_set(&d->sets[i], f->elem_id[e])) continue;
            set_ix[ns] = i;
            pair(M_HIDE_SET, M_ONLY_SET, ns, "set %s", d->sets[i].name);
            ns++;
        }
        for (int k = 0; k < ns; k++) add(M_INTEG_SET, k, "Integrate over set %s", d->sets[set_ix[k]].name);
    }
    if (G.sel_n || G.seln_n) {
        add(M_SEP, 0, NULL);
        if (G.sel_n) {
            add(M_SEL_HIDE, 0, "Hide the %u selected", G.sel_n);
            add(M_SEL_ONLY, 0, "Show only the selected");
        }
        if (G.boxq.on && G.boxq.gen == G.field_gen) {
            add(M_SEL_MAX, 0, "Go to the selection's max");
            add(M_SEL_MIN, 0, "Go to the selection's min");
        }
        add(M_SEL_IDS, 0, G.sel_n ? "Copy the selected element ids" : "Copy the selected node ids");
        add(M_SEL_CSV, 0, "Save the selection as CSV");
        add(M_SEL_INTEG, 0, G.sel_n ? "Integrate over the selection" : "Sum over the selected nodes");
        add(M_SEL_CLEAR, 0, "Clear the selection");
    }
    add(M_SEP, 0, NULL);
    if (any_hidden()) add(M_SHOW_ALL, 0, "Show all");
    add(M_FIT, 0, "Fit (F)");
    add(M_LOOK, 0, NULL);
    if (!p->hit) {
        add(M_RESET, 0, "Reset the view (R)");
        add(M_BACK, 0, "View back (Ctrl+Z)");
        add(M_FWD, 0, "View forward (Ctrl+Y)");
        add(M_ORTHO, 0, G.cam.ortho ? "Perspective" : "Orthographic");
        add(M_FLY, 0, "Free flight (G)");
        add(M_PNG, 0, "Save a picture (PNG)");
    }
}

static void axis_only(int a, int keep, bool only) {
    cv_axis* ax = &G.groups.axis[a];
    for (int i = 0; i < ax->n; i++) ax->on[i] = only ? i == keep : ax->on[i] && i != keep;
    G.probe_on = false;
    app_groups_changed();
}

static void act(const item* m) {
    const cv_pick* p = &G.menu_pick;
    float x = G.menu_x, y = G.menu_y;
    uint32_t e = p->elem;
    char t[400], num[32];
    switch (m->what) {
    case M_PROBE:   app_probe_pixel(x, y); break;
    case M_DETAILS: app_probe_pixel(x, y); G.show_details = true; break;
    case M_CENTRE:  app_view_push(); app_center_at(x, y); break;
    case M_NORMAL:  app_view_push(); app_normal_to(x, y); break;
    case M_ZOOM:    app_view_push(); app_fit_element(e); break;
    case M_HIST:    app_hist_open(p->node, e); break;
    case M_PATH:    app_probe_pixel(x, y); app_path_start(p->node); break;
    case M_WALL:    app_probe_pixel(x, y); app_path_ray(p->node, 1); break;
    case M_CLIP:    app_clip_at(G.menu_p, G.menu_n); break;
    case M_COPY:
        fmt_num(num, sizeof num, !G.has_field ? NAN : G.elem_mode ? G.elem_val[e] : G.scalar[p->node]);
        snprintf(t, sizeof t, "element %u node %u %s = %s", G.frd.elem_id[e], G.frd.node_id[p->node], G.field_label, num);
        sapp_set_clipboard_string(t);
        break;
    case M_HIDE_EL:   app_hide_elems(&e, 1); break;
    case M_HIDE_MAT:  axis_only(CV_AXIS_MAT, G.groups.axis[CV_AXIS_MAT].of_elem[e], false); break;
    case M_ONLY_MAT:  axis_only(CV_AXIS_MAT, G.groups.axis[CV_AXIS_MAT].of_elem[e], true); break;
    case M_HIDE_TYPE: axis_only(CV_AXIS_TYPE, G.groups.axis[CV_AXIS_TYPE].of_elem[e], false); break;
    case M_ONLY_TYPE: axis_only(CV_AXIS_TYPE, G.groups.axis[CV_AXIS_TYPE].of_elem[e], true); break;
    case M_HIDE_SET: case M_ONLY_SET: {
        const cv_inp* d = deck_get();
        if (d) app_hide_set(d->sets[set_ix[m->set]].name, m->what == M_ONLY_SET);
        break;
    }
    case M_INTEG_SET: {
        const cv_inp* d = deck_get();
        if (d) app_integ_open(CV_IK_VOLUME, d->sets[set_ix[m->set]].name);
        break;
    }
    case M_SEL_INTEG: app_integ_open(G.sel_n ? CV_IK_VOLUME : CV_IK_NODES, "selection"); break;
    case M_SEL_HIDE: app_hide_elems(G.sel, G.sel_n); break;
    case M_SEL_ONLY: app_isolate_elems(G.sel, G.sel_n); break;
    case M_SEL_MAX: case M_SEL_MIN: {
        uint32_t i = m->what == M_SEL_MAX ? G.boxq.max_at : G.boxq.min_at;
        bool el = G.boxq.elem;
        app_view_push();
        app_find(el ? G.frd.elem_id[i] : G.frd.node_id[i], el);
        break;
    }
    case M_SEL_IDS: {
        size_t n = (size_t)CV_MAX(G.sel_n, G.seln_n) * 12 + 64;
        char* s = malloc(n);
        if (s) { app_sel_ids(s, n); sapp_set_clipboard_string(s); free(s); }
        break;
    }
    case M_SEL_CSV:   app_sel_csv(); break;
    case M_SEL_CLEAR: app_sel_clear(); break;
    case M_SHOW_ALL:  app_show_all(); break;
    case M_FIT:       app_view_push(); app_fit(); break;
    case M_RESET:     app_view_push(); app_view(CV_VIEW_ISO); break;
    case M_BACK:      app_view_undo(-1); break;
    case M_FWD:       app_view_undo(+1); break;
    case M_ORTHO:     G.cam.ortho = !G.cam.ortho; break;
    case M_FLY:       app_set_flight(true); break;
    case M_PNG:       app_export_png(); break;
    case M_LABEL_RESET: app_label_menu_reset(); break;
    }
}

void window_menu(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.menu_on || !G.loaded) { if (was_open) nk_window_show(ctx, "Menu", NK_HIDDEN); was_open = false; return; }
    build();
    float sep = 7 * s, h = 2 * ctx->style.window.padding.y + 4 * s;
    for (int i = 0; i < ni; i++) h += it[i].what == M_SEP ? sep : row + ctx->style.window.spacing.y;
    float w = 275 * s;                                /* the measure row: three words beside its label */
    float x = CV_MIN(G.menu_x, fw - w - 4), y = CV_MIN(G.menu_y, fh - h - 4);
    struct nk_rect r = nk_rect(CV_MAX(x, 4), CV_MAX(y, 4), w, h);
    if (!was_open) { nk_window_show(ctx, "Menu", NK_SHOWN); nk_window_set_focus(ctx, "Menu"); }
    was_open = true;
    int chosen = -1;
    if (nk_begin(ctx, "Menu", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_window_set_bounds(ctx, "Menu", r);
        for (int i = 0; i < ni; i++) {
            if (it[i].what == M_SEP) { if (i) uii_hsep(ctx, s); continue; }
            if (it[i].what == M_LOOK) {                 /* look from: seven small buttons in a row */
                static const char* lk[] = { "iso", "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
                nk_layout_row_dynamic(ctx, row, 7);
                for (int k = 0; k < 7; k++) {
                    char mk[24];
                    snprintf(mk, sizeof mk, "#menu look %s", lk[k]);
                    uii_test_mark(ctx, mk);
                    if (nk_button_label(ctx, lk[k])) { app_view_push(); app_view(CV_VIEW_ISO + k); G.menu_on = false; }
                }
                continue;
            }
            if (it[i].what == M_MEAS) {                 /* measure from this node: the next clicks give the others */
                int k = ui_measure_row(ctx, s, row, "#menu measure");
                if (k >= 0) { G.menu_on = false; app_probe_pixel(G.menu_x, G.menu_y); app_measure_arm(k, G.menu_pick.node); }
                continue;
            }
            if (it[i].what == M_PAIR) {
                nk_layout_row_template_begin(ctx, row);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_push_static(ctx, 52 * s);
                nk_layout_row_template_push_static(ctx, 52 * s);
                nk_layout_row_template_end(ctx);
                nk_label(ctx, it[i].label, NK_TEXT_LEFT);
                char mk[120];
                snprintf(mk, sizeof mk, "#menu hide %s", it[i].label);
                uii_test_mark(ctx, mk);
                if (nk_button_label(ctx, "hide")) { chosen = i; it[i].what = it[i].hide; }
                snprintf(mk, sizeof mk, "#menu only %s", it[i].label);
                uii_test_mark(ctx, mk);
                if (nk_button_label(ctx, "only")) { chosen = i; it[i].what = it[i].only; }
                continue;
            }
            nk_layout_row_dynamic(ctx, row, 1);
            char mk[110];
            snprintf(mk, sizeof mk, "#menu %s", it[i].label);
            uii_test_mark(ctx, mk);
            if (nk_select_label(ctx, it[i].label, NK_TEXT_LEFT, nk_false)) chosen = i;
        }
        /* a press anywhere outside closes it */
        const struct nk_input* in = &ctx->input;
        if ((nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT) || nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT)) &&
            !nk_input_is_mouse_hovering_rect(in, r))
            G.menu_on = false;
    }
    nk_end(ctx);
    if (chosen >= 0) { G.menu_on = false; act(&it[chosen]); }
}
