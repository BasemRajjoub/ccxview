/* ui_windows.c -- the floating windows: messages, the formula builder, probe, find, the text overlay
   and navigation mark in the view, the built-in file browser and the drop hint.
   The plot windows are in ui_plots.c. */
#include "app.h"
#include "ui.h"
#include "cfg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"
#include "calc.h"
#include "app_fail.h"
#include "app_mesh.h"

void window_messages(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open = false;
    if (!G.show_msgs) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Messages", NK_SHOWN);
    was_open = true;
    if (nk_begin(ctx, "Messages", nk_rect(fw * 0.25f, fh * 0.2f, fw * 0.5f, fh * 0.5f),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        const cv_msgs* lists[2] = { &G.frd.msgs, &G.msgs };
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 90 * s);
        nk_layout_row_template_end(ctx);
        nk_label(ctx, "parser and loader messages", NK_TEXT_LEFT);
        if (nk_button_label(ctx, "copy all")) {              /* to the clipboard, one per line */
            CV_VEC(char) txt = {0};
            char line[200];
            for (int l = 0; l < 2; l++)
                for (size_t i = 0; i < lists[l]->n; i++) {
                    const cv_msg* m = &lists[l]->a[i];
                    int k = m->where ? snprintf(line, sizeof line, "%s %llu: %s\n", m->is_offset ? "byte" : "line",
                                                (unsigned long long)m->where, m->text)
                                     : snprintf(line, sizeof line, "%s\n", m->text);
                    if (k > 0 && cv_reserve(txt, txt.n + (size_t)k + 1)) { memcpy(txt.a + txt.n, line, (size_t)k); txt.n += (size_t)k; }
                }
            if (cv_reserve(txt, txt.n + 1)) { txt.a[txt.n] = 0; sapp_set_clipboard_string(txt.a); }
            cv_free_vec(txt);
        }
        nk_layout_row_dynamic(ctx, row, 1);
        for (int l = 0; l < 2; l++) {
            for (size_t i = 0; i < lists[l]->n; i++) {
                const cv_msg* m = &lists[l]->a[i];
                char line[200];
                if (m->where) snprintf(line, sizeof line, "%s %llu: %s", m->is_offset ? "byte" : "line",
                                       (unsigned long long)m->where, m->text);
                else snprintf(line, sizeof line, "%s", m->text);
                nk_label(ctx, line, NK_TEXT_LEFT);
            }
            if (lists[l]->dropped) {
                char line[80];
                snprintf(line, sizeof line, "... and %zu more", lists[l]->dropped);
                nk_label(ctx, line, NK_TEXT_LEFT);
            }
        }
    }
    if (nk_window_is_hidden(ctx, "Messages")) G.show_msgs = false;
    nk_end(ctx);
}

const char* path_dirs[] = { "to a node", "along normal", "along X", "along Y", "along Z" };

/* what a box selects: elements, nodes or both (never neither); a change selects again */
void ui_sel_what(struct nk_context* ctx, float row) {
    nk_layout_row_dynamic(ctx, row, 3);
    nk_label(ctx, "a box selects:", NK_TEXT_LEFT);
    bool e = G.sel_elems, n = G.sel_nodes;
    tip(ctx, "Elements: drag left to right for those wholly inside, right to left for every one touched");
    nk_checkbox_label(ctx, "elements", &G.sel_elems);
    tip(ctx, "Nodes inside the box; the max and min are then taken over them");
    nk_checkbox_label(ctx, "nodes", &G.sel_nodes);
    if (!G.sel_elems && !G.sel_nodes) { if (e) G.sel_nodes = true; else G.sel_elems = true; }   /* never nothing */
    bool vis = G.sel_visible, mx = G.sel_mark_max, mn = G.sel_mark_min;
    nk_layout_row_dynamic(ctx, row, 3);
    tip(ctx, "Only the side facing you: nodes on faces turned toward the camera, elements with such a face.\n"
             "Off: everything inside the box, through the model");
    nk_checkbox_label(ctx, "facing side only", &G.sel_visible);
    tip(ctx, "A red ball on the selection's maximum");
    nk_checkbox_label(ctx, "mark max", &G.sel_mark_max);
    tip(ctx, "A blue ball on the selection's minimum (compression, the cold spot); the value is in the probe either way");
    nk_checkbox_label(ctx, "mark min", &G.sel_mark_min);
    if (e != G.sel_elems || n != G.sel_nodes || vis != G.sel_visible) app_box_reselect();
    else if (mx != G.sel_mark_max || mn != G.sel_mark_min) app_sel_refresh();
}

void window_probe(struct nk_context* ctx, float s, float row) {
    if (!G.probe_on || !G.loaded) return;
    const cv_pick* p = &G.probe;
    /* the lines first: the window is as wide as the longest */
    enum { NPL = 7 };
    char ln[NPL][160], num[32];
    const char* tips[NPL] = { 0 };
    int n = 0;
    snprintf(ln[n++], 160, "element %u  (%s)", G.frd.elem_id[p->elem], cv_frd_type_name(G.frd.etype[p->elem]));
    snprintf(ln[n++], 160, "material %u   group %u", G.frd.emat[p->elem], G.frd.egrp[p->elem]);
    const float* x = G.frd.xyz + 3 * p->node;
    snprintf(ln[n++], 160, "node %u  (%.4g, %.4g, %.4g)", G.frd.node_id[p->node], x[0], x[1], x[2]);
    fmt_num(num, sizeof num, G.probe_value);
    if (!G.has_field) snprintf(ln[n++], 160, "no field");
    else if (G.probe_ip) snprintf(ln[n++], 160, "point %d: %s = %s", G.probe_ip, G.field_label, num);
    else snprintf(ln[n++], 160, "%s%s = %s", G.field_label, G.elem_mode ? " (elem)" : "", num);
    if (G.has_field && fail_probe_text(p->node, p->elem, ln[n], 160))
        tips[n++] = "The governing failure mode (and fracture plane) at this node, or element per element";
    if (G.has_field && mesh_probe_text(p->elem, ln[n], 160))
        tips[n++] = "This element's aspect ratio, scaled Jacobian, Jacobian ratio, skewness and\nwarpage; ! past the usual limit";
    if (G.boxq.on && G.boxq.gen == G.field_gen) {                /* the box this probe came from */
        char a[32], b[32];
        fmt_num(a, sizeof a, G.boxq.vmax); fmt_num(b, sizeof b, G.boxq.vmin);
        snprintf(ln[n], 160, "box: max %s  min %s  (%u %s)", a, b, G.boxq.n, G.boxq.elem ? "elements" : "nodes");
        tips[n++] = "The field's max (probed) and min over the selected elements\n"
                    "(their nodes; per element, the elements). Details: where they are";
    }
    const struct nk_user_font* fnt = ctx->style.font;
    float tw = 0;
    for (int i = 0; i < n; i++) tw = CV_MAX(tw, fnt->width(fnt->userdata, fnt->height, ln[i], (int)strlen(ln[i])));
    float w = CV_MIN(CV_MAX(260 * s, tw + 2 * ctx->style.window.padding.x + 12 * s), CV_MAX(260 * s, G.vp_w * 0.6f));
    float h = row * (n + 6.9f + (G.sel_n || G.seln_n ? 2.2f : 0.f));
    struct nk_rect r = nk_rect(G.vp_x + 10 * s, G.vp_y + G.vp_h - h - 10 * s, w, h);
    if (!nk_begin(ctx, "Probe", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {
        nk_end(ctx);
        return;
    }
    nk_window_set_bounds(ctx, "Probe", r);
    nk_layout_row_dynamic(ctx, row, 1);
    for (int i = 0; i < n; i++) {
        if (tips[i]) tip(ctx, tips[i]);
        nk_label(ctx, ln[i], NK_TEXT_LEFT);
    }
    if (G.sel_n || G.seln_n) ui_sel_what(ctx, row);
    nk_layout_row_dynamic(ctx, row, 3);
    nk_bool lb = G.label_probe_only && G.label_kinds;
    tip(ctx, "The ids of this element and its nodes in the view (Fields > Labels: the kind, size and colours)");
    if (nk_checkbox_label(ctx, "labels", &lb)) {           /* on: this element only; off: no labels at all */
        G.label_probe_only = lb;
        if (lb && !G.label_kinds) G.label_kinds = 1 << CV_LABEL_NODE;
        if (!lb) G.label_kinds = 0;
        app_label_changed();
    }
    tip(ctx, "Everything about this node and element in a window of its own:\n"
             "displacement, every component of the field, sets, nodes, the box selection");
    if (nk_button_label(ctx, "details...")) G.show_details = !G.show_details;
    if (nk_button_label(ctx, "close")) { G.probe_on = false; G.path_arm = false; app_measure_cancel(); app_sel_clear(); }
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
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "The field at this node (element in per-element mode) against time, over every step");
    if (nk_button_label(ctx, "history")) app_hist_open(p->node, p->elem);
    tip(ctx, "Element, node and value to the clipboard");
    if (nk_button_label(ctx, "copy")) {
        char txt[400];
        snprintf(txt, sizeof txt, "element %u node %u %s = %s", G.frd.elem_id[p->elem], G.frd.node_id[p->node], G.field_label, num);
        sapp_set_clipboard_string(txt);
    }
    nk_end(ctx);
}

/* ---- the navigation mark in the 3D view ---- */
/* The navigation mark: while the mouse turns, pans or zooms the view, what it
   happens about -- an axis cross (X red, Y green, Z blue) with a ring at the
   rotation centre, a ring and cross hair at the zoom point. Only while the
   drag lasts: gone with the button. The box of a box zoom always. */
void window_nav(struct nk_context* ctx, float s) {
    if (!G.loaded || !G.nav_live || G.nav_mode == CV_NAV_NONE) return;
    bool box = G.nav_mode == CV_NAV_BOX;
    if (!box && !G.show_pivot) return;
    struct nk_rect wr = nk_rect(G.vp_x, G.vp_y, G.vp_w, G.vp_h);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    nk_style_push_float(ctx, &ctx->style.window.border, 0);
    if (nk_begin(ctx, "navmark", wr, NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT | NK_WINDOW_BACKGROUND)) {
        nk_window_set_bounds(ctx, "navmark", wr);
        struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
        const nk_byte al = 255;
        struct nk_color ink = nk_rgba(255, 220, 60, al), shade = nk_rgba(0, 0, 0, (nk_byte)(al * 0.6f));
        float sx, sy;
        if (box) {
            float x0 = CV_MIN(G.nav_box[0], G.nav_box[2]), y0 = CV_MIN(G.nav_box[1], G.nav_box[3]);
            struct nk_rect r = nk_rect(x0, y0, fabsf(G.nav_box[2] - G.nav_box[0]), fabsf(G.nav_box[3] - G.nav_box[1]));
            /* a selection: window (left to right) blue, crossing (right to left) green; a box zoom grey */
            struct nk_color c = !G.box_pick ? nk_rgba(220, 220, 220, 255)
                              : G.nav_box[2] < G.nav_box[0] ? nk_rgba(90, 210, 110, 255) : nk_rgba(100, 160, 255, 255);
            nk_fill_rect(cv, r, 0, nk_rgba(c.r, c.g, c.b, 40));
            nk_stroke_rect(cv, r, 0, 1.5f * s, nk_rgba(c.r, c.g, c.b, 220));
        } else if (app_project(G.nav_pt, &sx, &sy)) {
            float t = 2.f * s;
            if (G.nav_mode == CV_NAV_ROTATE || G.nav_mode == CV_NAV_ROLL) {
                float L = 28.f * s * app_pixel_size(G.nav_pt);
                static const struct { float x, y, z; nk_byte r, g, b; } ax[3] = {
                    { 1, 0, 0, 235, 70, 70 }, { 0, 1, 0, 80, 210, 80 }, { 0, 0, 1, 80, 140, 255 } };
                for (int k = 0; k < 3; k++) {
                    float ex, ey;
                    v3 e = v3_add(G.nav_pt, v3_make(ax[k].x * L, ax[k].y * L, ax[k].z * L));
                    if (!app_project(e, &ex, &ey)) continue;
                    nk_stroke_line(cv, sx, sy, ex, ey, t + 2 * s, shade);
                    nk_stroke_line(cv, sx, sy, ex, ey, t, nk_rgba(ax[k].r, ax[k].g, ax[k].b, al));
                }
                float R = (G.nav_mode == CV_NAV_ROLL ? 34.f : 9.f) * s;
                nk_stroke_circle(cv, nk_rect(sx - R, sy - R, 2 * R, 2 * R), t + 2 * s, shade);
                nk_stroke_circle(cv, nk_rect(sx - R, sy - R, 2 * R, 2 * R), t, ink);
                nk_fill_circle(cv, nk_rect(sx - 3 * s, sy - 3 * s, 6 * s, 6 * s), ink);
            } else {
                float R = (G.nav_mode == CV_NAV_ZOOM ? 11.f : 6.f) * s, H = R + 6 * s;
                nk_stroke_circle(cv, nk_rect(sx - R, sy - R, 2 * R, 2 * R), t + 2 * s, shade);
                nk_stroke_circle(cv, nk_rect(sx - R, sy - R, 2 * R, 2 * R), t, ink);
                nk_stroke_line(cv, sx - H, sy, sx + H, sy, t, ink);
                nk_stroke_line(cv, sx, sy - H, sx, sy + H, t, ink);
            }
        }
    }
    nk_end(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
}

/* ---- find (Ctrl+F): a node or element by its id -------------------------------- */
void window_find(struct nk_context* ctx, float s, float row) {
    static bool was_open;
    static char buf[16]; static int len;
    static bool element, missing;
    if (!G.find_open || !G.loaded) { was_open = false; return; }
    if (!was_open) { nk_window_show(ctx, "Find", NK_SHOWN); missing = false; }
    was_open = true;
    float w = 240 * s, h = 4.6f * row;
    if (nk_begin(ctx, "Find", nk_rect(G.vp_x + 10 * s, G.vp_y + 10 * s, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, row, 2);
        if (nk_option_label(ctx, "node", !element)) element = false;
        if (nk_option_label(ctx, "element", element)) element = true;
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 50 * s);
        nk_layout_row_template_end(ctx);
        nk_edit_focus(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER);
        nk_flags ev = nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, buf, &len, (int)sizeof buf - 1, nk_filter_decimal);
        buf[len] = 0;
        bool go = nk_button_label(ctx, "go");
        if ((ev & NK_EDIT_COMMITED) || go) {
            uint32_t id = (uint32_t)strtoul(buf, NULL, 10);
            missing = !app_find(id, element);
            if (!missing) { G.label_probe_only = true; if (!G.label_kinds) G.label_kinds = 1 << CV_LABEL_NODE; app_label_changed(); }
        }
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, missing ? "not in this model" : "id as in the file; Enter", NK_TEXT_LEFT,
                         missing ? nk_rgb(240, 120, 120) : P.dim);
    }
    if (nk_window_is_hidden(ctx, "Find")) G.find_open = false;
    nk_end(ctx);
}

/* ---- built-in file browser (used when no native dialog exists) --------------- */

static struct {
    char       dir[1024];         /* directory the list below belongs to */
    cv_dirent* e;
    int        n;                 /* -1: unreadable */
    int        sel;
    bool       all;
    char       edit[1024];
    int        edit_len;
    double     last_click;
    int        last_idx;
} B = { "", NULL, 0, -1 };

static void browser_load(const char* dir) {
    free(B.e);
    B.e = NULL;
    snprintf(B.dir, sizeof B.dir, "%s", dir);
    B.n = cv_list_dir(dir, &B.e);
    B.sel = -1;
    snprintf(B.edit, sizeof B.edit, "%s", dir);
    B.edit_len = (int)strlen(B.edit);
}

static void browser_enter(const char* name) {        /* ".." = parent */
    char nd[1024];
    char sep = cv_path_sep();
    if (!strcmp(name, "..")) {
        snprintf(nd, sizeof nd, "%s", G.browse_dir);
        char* p = strrchr(nd, sep);
        if (p && p != nd) *p = 0;
        else if (p) p[1] = 0;                           /* "/" */
    } else {
        size_t l = strlen(G.browse_dir);
        bool has_sep = l && G.browse_dir[l - 1] == sep;
        snprintf(nd, sizeof nd, "%s%s%s", G.browse_dir, has_sep ? "" : (char[2]){ sep, 0 }, name);
    }
    snprintf(G.browse_dir, sizeof G.browse_dir, "%s", nd);
}

static bool has_ext(const char* n, const char* ext) {
    size_t l = strlen(n);
    return l > 4 && n[l - 4] == '.' && (n[l - 3] | 32) == ext[0] && (n[l - 2] | 32) == ext[1] && (n[l - 1] | 32) == ext[2];
}
static bool is_frd(const char* n) { return has_ext(n, "frd") || has_ext(n, "inp") || has_ext(n, "dat") || has_ext(n, "fbd") || has_ext(n, "stl"); }

static void fmt_size(char* out, size_t n, uint64_t b) {
    if (b >= (1ull << 30)) snprintf(out, n, "%.1f GB", b / 1073741824.0);
    else if (b >= (1ull << 20)) snprintf(out, n, "%.1f MB", b / 1048576.0);
    else if (b >= 1024) snprintf(out, n, "%.0f KB", b / 1024.0);
    else snprintf(out, n, "%u B", (unsigned)b);
}

void window_browser(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.browser_open) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Open file", NK_SHOWN);
    was_open = true;
    if (strcmp(B.dir, G.browse_dir) != 0) browser_load(G.browse_dir);

    float w = CV_MIN(720 * s, fw * 0.9f), h = CV_MIN(560 * s, fh * 0.85f);
    char open_path[2048] = "";
    if (nk_begin(ctx, "Open file", nk_rect((fw - w) / 2, (fh - h) / 2, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE |
                 NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        /* folder: editable, Enter jumps */
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 48 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 90 * s);
        nk_layout_row_template_end(ctx);
        if (nk_button_label(ctx, "Up")) browser_enter("..");
        nk_flags ev = nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, B.edit, &B.edit_len,
                                     (int)sizeof B.edit - 1, nk_filter_default);
        B.edit[B.edit_len] = 0;
        if (ev & NK_EDIT_COMMITED) {
            if (cv_is_dir(B.edit)) snprintf(G.browse_dir, sizeof G.browse_dir, "%s", B.edit);
            else snprintf(open_path, sizeof open_path, "%s", B.edit);   /* a typed file */
        }
        nk_checkbox_label(ctx, "all files", &B.all);

        /* listing */
        float list_h = nk_window_get_content_region(ctx).h - 3 * row - 4 * ctx->style.window.spacing.y;
        nk_layout_row_dynamic(ctx, CV_MAX(list_h, row), 1);
        if (nk_group_begin(ctx, "files", NK_WINDOW_BORDER)) {
            if (B.n < 0) {
                nk_layout_row_dynamic(ctx, row, 1);
                nk_label(ctx, "cannot read this folder", NK_TEXT_LEFT);
            }
            for (int i = 0; i < B.n; i++) {
                const cv_dirent* e = &B.e[i];
                if (!e->dir && !B.all && !is_frd(e->name)) continue;
                nk_layout_row_template_begin(ctx, row);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_push_static(ctx, 90 * s);
                nk_layout_row_template_end(ctx);
                char lab[300], sz[32] = "";
                snprintf(lab, sizeof lab, e->dir ? "[%s]" : "%s", e->name);
                if (!e->dir) fmt_size(sz, sizeof sz, e->size);
                nk_bool sel = B.sel == i;
                if (nk_selectable_label(ctx, lab, NK_TEXT_LEFT, &sel)) {
                    double now = cv_now();
                    bool dbl = B.last_idx == i && now - B.last_click < 0.4;
                    B.last_click = now; B.last_idx = i;
                    B.sel = i;
                    if (e->dir) { browser_enter(e->name); nk_group_end(ctx); goto listed; }
                    if (dbl) {
                        size_t l = strlen(B.dir);
                        bool has_sep = l && B.dir[l - 1] == cv_path_sep();
                        snprintf(open_path, sizeof open_path, "%s%s%s", B.dir,
                                 has_sep ? "" : (char[2]){ cv_path_sep(), 0 }, e->name);
                    }
                }
                nk_label_colored(ctx, sz, NK_TEXT_RIGHT, P.dim);
            }
            nk_group_end(ctx);
        }
    listed:
        /* buttons */
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 90 * s);
        nk_layout_row_template_push_static(ctx, 90 * s);
        nk_layout_row_template_end(ctx);
        const char* hint = (B.sel >= 0 && B.sel < B.n && !B.e[B.sel].dir) ? B.e[B.sel].name : "double-click a file";
        nk_label_colored(ctx, hint, NK_TEXT_LEFT, P.dim);
        if (nk_button_label(ctx, "Cancel")) G.browser_open = false;
        if (nk_button_label(ctx, "Open") && B.sel >= 0 && B.sel < B.n && !B.e[B.sel].dir) {
            size_t l = strlen(B.dir);
            bool has_sep = l && B.dir[l - 1] == cv_path_sep();
            snprintf(open_path, sizeof open_path, "%s%s%s", B.dir,
                     has_sep ? "" : (char[2]){ cv_path_sep(), 0 }, B.e[B.sel].name);
        }
    }
    if (nk_window_is_hidden(ctx, "Open file")) G.browser_open = false;
    nk_end(ctx);
    if (open_path[0]) {
        G.browser_open = false;
        app_dialog_done(open_path);
    }
}

/* Big hint in the empty view: this is where files go. */
void drop_hint(struct nk_context* ctx, float s, float row) {
    if (G.loaded || app_busy()) return;
    const char* recent[CV_CFG_RECENT];
    int nr = settings_recent(recent, CV_CFG_RECENT);
    int rows = 2 + (nr ? 2 + nr : 0);          /* + a gap, "Recent files" and one button each */
    float w = 420 * s, h = rows * row + 2 * ctx->style.window.padding.y + rows * ctx->style.window.spacing.y;
    struct nk_rect r = nk_rect(G.vp_x + (G.vp_w - w) / 2, G.vp_y + (G.vp_h - h) / 2, w, h);
    /* input only with buttons in it: an empty hint must not keep a drag from the view.
       With buttons it is a normal window, a background one loses its clicks to the overlay. */
    nk_flags fl = NK_WINDOW_NO_SCROLLBAR | (nr ? 0 : NK_WINDOW_BACKGROUND | NK_WINDOW_NO_INPUT);
    if (nk_begin(ctx, "hint", r, fl)) {
        nk_window_set_bounds(ctx, "hint", r);
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label(ctx, "Drop a .frd, .inp or .fbd file here", NK_TEXT_CENTERED);
        nk_label_colored(ctx, "or Open... (Ctrl+O)", NK_TEXT_CENTERED, P.dim);
        if (nr) { nk_label(ctx, "", NK_TEXT_LEFT); recent_buttons(ctx, row); }
    }
    nk_end(ctx);
}

/* ---- Formula builder: the formula of the Calculated box, typed or put together
   from buttons. Each button inserts at the cursor: this file's fields and their
   components, the derived results, operators, functions; an example replaces it all. */
static struct nk_text_edit fb_te;
static char fb_mem[sizeof G.calc_expr];
static bool fb_focus;                                /* give the box the keyboard back next frame */

static void fb_set(const char* t) {
    nk_textedit_init_fixed(&fb_te, fb_mem, sizeof fb_mem - 1);
    nk_str_append_text_char(&fb_te.string, t, (int)strlen(t));
    fb_te.cursor = nk_str_len(&fb_te.string);
}

static const char* fb_text(void) {                   /* fb_mem without a terminator of its own */
    static char t[sizeof fb_mem];
    int n = CV_MIN(nk_str_len_char(&fb_te.string), (int)sizeof t - 1);
    memcpy(t, fb_mem, (size_t)n);
    t[n] = 0;
    return t;
}

static void fb_insert(const char* t) {
    fb_te.mode = NK_TEXT_EDIT_MODE_INSERT;          /* nk_textedit_text types only in insert mode */
    nk_textedit_text(&fb_te, t, (int)strlen(t));
    fb_focus = true;
}

/* a button that inserts ins (label when NULL), with a tooltip */
static void fb_key(struct nk_context* ctx, const char* label, const char* ins, const char* why) {
    if (why) tip(ctx, why);
    if (nk_button_label(ctx, label)) fb_insert(ins ? ins : label);
}

static void fb_head(struct nk_context* ctx, float s, float row, const char* t) {
    nk_layout_row_dynamic(ctx, 4 * s, 1);
    nk_spacing(ctx, 1);
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label_colored(ctx, t, NK_TEXT_LEFT, P.accent);
}

/* what a field holds, for the names CalculiX (and FEMaster) write */
static const char* fb_about(const char* f) {
    static const char* const k[][2] = {
        { "DISP", "displacement" }, { "STRESS", "stress" }, { "TOSTRAIN", "total strain" },
        { "MESTRAIN", "mechanical strain" }, { "THSTRAIN", "thermal strain" }, { "PE", "equiv. plastic strain" },
        { "FORC", "nodal force" }, { "EXTFORC", "external force" }, { "ENER", "energy density" },
        { "ERROR", "error estimate" }, { "HERROR", "heat error estimate" }, { "ZZS", "smoothed stress" },
        { "NDTEMP", "temperature" }, { "FLUX", "heat flux" }, { "RFL", "heat reaction" },
        { "CONTACT", "contact" }, { "SDV", "state variables" }, { "VELO", "velocity" },
        { "STRPOS", "stress, shell top" }, { "STRNEG", "stress, shell bottom" }, { "STRMID", "stress, shell middle" },
        { "SHR", "shell resultants" }, { "PDISP", "displacement, complex" }, { "PSTRESS", "stress, complex" },
    };
    for (size_t i = 0; i < CV_COUNT(k); i++) if (!strcmp(f, k[i][0])) return k[i][1];
    return "";
}

void window_calc_help(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open = false;
    if (!G.show_calc_help) { was_open = false; return; }
    static char seen[sizeof G.calc_expr];
    if (!was_open) { nk_window_show(ctx, "Formula", NK_SHOWN); fb_set(calc_draft()); fb_focus = true; }
    else if (strcmp(seen, G.calc_expr)) fb_set(G.calc_expr);   /* set elsewhere: --calc, the examples list */
    snprintf(seen, sizeof seen, "%s", G.calc_expr);
    was_open = true;
    float w = CV_MIN(720 * s, fw * 0.92f), h = CV_MIN(fh * 0.86f, 900 * s);
    if (nk_begin(ctx, "Formula", nk_rect((fw - w) / 2, (fh - h) / 2, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        float cw = nk_window_get_content_region(ctx).w, sp = ctx->style.window.spacing.x;

        /* the formula, and what to do with it */
        bool apply = false, close = false;
        nk_layout_row_template_begin(ctx, row * 1.3f);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 34 * s);
        nk_layout_row_template_push_static(ctx, 34 * s);
        nk_layout_row_template_push_static(ctx, 60 * s);
        nk_layout_row_template_push_static(ctx, 60 * s);
        nk_layout_row_template_end(ctx);
        if (fb_focus) { nk_edit_focus(ctx, 0); fb_focus = false; }
        nk_flags ev = nk_edit_buffer(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, &fb_te, nk_filter_default);
        if (ev & NK_EDIT_COMMITED) apply = true;
        tip(ctx, "Delete the character before the cursor");
        if (nk_button_label(ctx, "\xe2\x86\x90")) {
            if (fb_te.select_start != fb_te.select_end) nk_textedit_delete_selection(&fb_te);
            else if (fb_te.cursor > 0) { nk_textedit_delete(&fb_te, fb_te.cursor - 1, 1); fb_te.cursor--; }
            fb_focus = true;
        }
        tip(ctx, "Clear the formula");
        if (nk_button_label(ctx, "C")) { fb_set(""); fb_focus = true; }
        tip(ctx, "Colour the model by the formula; the window stays open (Enter does the same)");
        if (nk_button_label(ctx, "Show")) apply = true;
        tip(ctx, "Colour the model by the formula and close");
        if (nk_button_label(ctx, "OK")) apply = close = true;
        const char* t = fb_text();
        if (apply) {
            calc_draft_set(t);
            if (!t[0]) { G.calc_err[0] = 0; }
            else if (!app_calc_set(t)) close = false;      /* keep the window: the error shows below */
        }
        nk_layout_row_dynamic(ctx, row, 1);
        if (G.calc_err[0] && strcmp(t, G.calc_expr)) nk_label_colored(ctx, G.calc_err, NK_TEXT_LEFT, P.warn);
        else if (t[0] && !strcmp(t, G.calc_expr)) nk_label_colored(ctx, "shown", NK_TEXT_LEFT, P.dim);
        else nk_label_colored(ctx, "Type, or click the buttons below: they insert at the cursor.", NK_TEXT_LEFT, P.dim);

        /* this file's fields, from cv_calc_names: "NAME: C1 C2 ..." per line, then "X Y Z TIME" */
        static char names[2048];
        static unsigned gen;
        static const cv_frd* of;
        if (of != &G.frd || gen != G.field_gen || !names[0]) {
            cv_calc_names(&G.frd, names, sizeof names);
            of = &G.frd; gen = G.field_gen;
        }
        bool has_stress = false, has_strain = false;
        fb_head(ctx, s, row, "Fields in this file");
        float name_w = 80 * s, about_w = 130 * s, cell = 64 * s;
        int per = CV_MAX(1, (int)((cw - name_w - about_w - 2 * sp) / (cell + sp)));
        for (const char* l = names; *l;) {
            const char* e = strchr(l, '\n');
            size_t n = e ? (size_t)(e - l) : strlen(l);
            char line[512], *tok[24];
            snprintf(line, sizeof line, "%.*s", (int)CV_MIN(n, sizeof line - 1), l);
            l += n + (e ? 1 : 0);
            char* colon = strchr(line, ':');
            if (!colon) continue;                        /* X Y Z TIME: under Results */
            *colon = 0;
            const char* field = line;
            has_stress |= !strcmp(field, "STRESS");
            has_strain |= !strcmp(field, "TOSTRAIN");
            int nt = 0;
            for (char* p = colon + 1; *p && nt < 24;) {   /* split at spaces */
                while (*p == ' ') *p++ = 0;
                if (!*p) break;
                tok[nt++] = p;
                while (*p && *p != ' ') p++;
            }
            for (int k = 0; k < nt; k += per) {
                int m = CV_MIN(per, nt - k);
                nk_layout_row_begin(ctx, NK_STATIC, row, 2 + per);
                nk_layout_row_push(ctx, name_w);
                nk_label(ctx, k ? "" : field, NK_TEXT_LEFT);
                nk_layout_row_push(ctx, about_w);
                nk_label_colored(ctx, k ? "" : fb_about(field), NK_TEXT_LEFT, P.dim);
                for (int j = 0; j < m; j++) {
                    char full[64];
                    if (nt > 1) snprintf(full, sizeof full, "%s_%s", field, tok[k + j]);
                    else snprintf(full, sizeof full, "%s", field);   /* one component: the field alone */
                    nk_layout_row_push(ctx, cell);
                    fb_key(ctx, tok[k + j], full, full);
                }
                nk_layout_row_end(ctx);
            }
        }

        /* shortcuts and the node values; only what this file can give */
        fb_head(ctx, s, row, "Results   (values in global axes)");
        static const char* const res[][3] = {
            { "MISES", "S", "von Mises stress of STRESS" },
            { "S1", "S", "largest principal stress" }, { "S2", "S", "middle principal stress" },
            { "S3", "S", "smallest principal stress" },
            { "E1", "E", "largest principal strain (TOSTRAIN)" }, { "E2", "E", "middle principal strain" },
            { "E3", "E", "smallest principal strain" },
            { "X", "", "node x, undeformed" }, { "Y", "", "node y, undeformed" }, { "Z", "", "node z, undeformed" },
            { "TIME", "", "the step's time (frequency, load factor)" },
        };
        int pr = CV_MAX(1, (int)((cw + sp) / (cell + sp)));
        nk_layout_row_static(ctx, row, (int)cell, pr);
        for (size_t i = 0; i < CV_COUNT(res); i++) {
            if ((res[i][1][0] == 'S' && !has_stress) || (res[i][1][0] == 'E' && !has_strain)) continue;
            fb_key(ctx, res[i][0], NULL, res[i][2]);
        }

        /* operators and numbers, a keypad */
        fb_head(ctx, s, row, "Operators and numbers");
        static const char* const ops[][3] = {
            { "+", " + ", "add" }, { "-", " - ", "subtract" }, { "*", " * ", "multiply" }, { "/", " / ", "divide" },
            { "^", "^", "power: D1^2 (-a^2 is -(a^2))" }, { "%", " % ", "remainder" },
            { "(", "(", NULL }, { ")", ")", NULL }, { ",", ", ", "between the arguments of a function" },
            { "<", " < ", "less: 1 if true, else 0" }, { "<=", " <= ", "less or equal" },
            { ">", " > ", "greater" }, { ">=", " >= ", "greater or equal" },
            { "==", " == ", "equal" }, { "!=", " != ", "not equal" },
            { "&&", " && ", "and" }, { "||", " || ", "or" }, { "!", "!", "not" },
        };
        float key = 44 * s;
        int pk = CV_MAX(1, (int)((cw + sp) / (key + sp)));
        nk_layout_row_static(ctx, row, (int)key, pk);
        for (size_t i = 0; i < CV_COUNT(ops); i++) fb_key(ctx, ops[i][0], ops[i][1], ops[i][2]);
        nk_layout_row_static(ctx, row, (int)key, pk);
        static const char* const digits[] = { "7", "8", "9", "4", "5", "6", "1", "2", "3", "0", ".", "e" };
        for (size_t i = 0; i < CV_COUNT(digits); i++)
            fb_key(ctx, digits[i], NULL, digits[i][0] == 'e' ? "exponent: 2.1e5 (e alone is 2.718...)" : NULL);

        fb_head(ctx, s, row, "Functions");
        static const char* const fns[][3] = {
            { "if", "if(", "if(c, a, b): a where c is true (not 0), else b" },
            { "min", "min(", "min(a, b): the smaller" }, { "max", "max(", "max(a, b): the larger" },
            { "clamp", "clamp(", "clamp(x, lo, hi): x held between lo and hi" },
            { "abs", "abs(", "absolute value" }, { "sign", "sign(", "-1, 0 or 1" },
            { "sqrt", "sqrt(", "square root" }, { "pow", "pow(", "pow(x, y) is x^y" },
            { "exp", "exp(", "e^x" }, { "ln", "ln(", "natural log (log is ln too)" }, { "log10", "log10(", "base-10 log" },
            { "sin", "sin(", "sine, radians" }, { "cos", "cos(", "cosine, radians" }, { "tan", "tan(", "tangent, radians" },
            { "asin", "asin(", "arc sine" }, { "acos", "acos(", "arc cosine" }, { "atan", "atan(", "arc tangent" },
            { "atan2", "atan2(", "atan2(y, x): the angle of (x, y)" },
            { "sinh", "sinh(", NULL }, { "cosh", "cosh(", NULL }, { "tanh", "tanh(", NULL },
            { "floor", "floor(", "round down" }, { "ceil", "ceil(", "round up" },
            { "pi", "pi", "3.14159..." },
        };
        nk_layout_row_static(ctx, row, (int)cell, pr);
        for (size_t i = 0; i < CV_COUNT(fns); i++) fb_key(ctx, fns[i][0], fns[i][1], fns[i][2]);

        fb_head(ctx, s, row, "Examples   (replace the formula)");
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 230 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        for (int i = 0; i < calc_example_count; i++) {
            if (!calc_example_ok(i)) continue;
            if (nk_button_label(ctx, calc_examples[i][0])) { fb_set(calc_examples[i][0]); fb_focus = true; }
            nk_label_colored(ctx, calc_examples[i][2], NK_TEXT_LEFT, P.dim);
        }
        if (close) G.show_calc_help = false;           /* not begun next frame: gone */
    }
    if (nk_window_is_hidden(ctx, "Formula")) G.show_calc_help = false;
    nk_end(ctx);
}

/* ---- units: what each quantity is in, in the file (a consistent set, or chosen one
   by one), and what it is shown in (a preset, or chosen one by one) */

static int unit_in_now(int q) { return cv_unit_input(G.units, cv_sys_temp(G.units), G.unit_in[q], q); }
static int unit_shown_now(int q) {
    return cv_unit_shown(G.units, cv_sys_temp(G.units), G.unit_in[q], q, G.unit_show[q]);
}
static const char* unit_label(const cv_unit* u) { return u ? (u->name[0] ? u->name : "ratio") : "?"; }

/* "mm, MPa"; when shown in other units than the file's: "mm, MPa -> in, psi" */
void units_summary(char* out, size_t n) {
    int l = unit_shown_now(CV_Q_LEN), p = unit_shown_now(CV_Q_STRESS);
    if (l < 0 && p < 0) { snprintf(out, n, "not set"); return; }
    int temp = cv_sys_temp(G.units);
    int li = G.unit_in[CV_Q_LEN] >= 0 ? G.unit_in[CV_Q_LEN] : cv_sys_unit(G.units, temp, CV_Q_LEN);
    int pi = G.unit_in[CV_Q_STRESS] >= 0 ? G.unit_in[CV_Q_STRESS] : cv_sys_unit(G.units, temp, CV_Q_STRESS);
    if (li == l && pi == p)
        snprintf(out, n, "%s, %s", l < 0 ? "?" : unit_label(cv_unit_get(CV_Q_LEN, l)), p < 0 ? "?" : unit_label(cv_unit_get(CV_Q_STRESS, p)));
    else
        snprintf(out, n, "%s, %s -> %s, %s", li < 0 ? "?" : unit_label(cv_unit_get(CV_Q_LEN, li)), pi < 0 ? "?" : unit_label(cv_unit_get(CV_Q_STRESS, pi)),
                 l < 0 ? "?" : unit_label(cv_unit_get(CV_Q_LEN, l)), p < 0 ? "?" : unit_label(cv_unit_get(CV_Q_STRESS, p)));
}

/* the input side: true when some quantity was chosen apart from the set */
static bool units_in_custom(void) {
    for (int q = 0; q < CV_Q_N; q++)
        if (G.unit_in[q] >= 0 && G.unit_in[q] != cv_sys_unit(G.units, cv_sys_temp(G.units), q)) return true;
    return false;
}

/* the display preset all the shown units match, -1 when chosen one by one */
static int units_show_preset(void) {
    for (int p = 0; p < CV_SHOW_N; p++) {
        bool all = true;
        for (int q = 0; q < CV_Q_N && all; q++) all = G.unit_show[q] == cv_show_unit(p, q);
        if (all) return p;
    }
    return -1;
}

/* a choice among several: true when clicked while not the chosen one */
static bool unit_chip(struct nk_context* ctx, const char* label, bool on) {
    nk_bool v = on;
    return nk_selectable_label(ctx, label, NK_TEXT_CENTERED, &v) && !on;
}

/* chips as wide as the widest label of a row, as many to a line as fit */
static void chip_rows(struct nk_context* ctx, float s, float row, float cw, const char* const* labels, int n) {
    const struct nk_user_font* f = ctx->style.font;
    float w = 64 * s, sp = ctx->style.window.spacing.x;
    for (int i = 0; i < n; i++)
        w = CV_MAX(w, f->width(f->userdata, f->height, labels[i], (int)strlen(labels[i])) + 20 * s);
    w = CV_MIN(w, cw);
    nk_layout_row_static(ctx, row, (int)w, CV_MAX(1, (int)((cw + sp) / (w + sp))));
}

/* the name of a quantity's row, the accent when it is the field on screen */
static void unit_row_head(struct nk_context* ctx, float s, float row, int q, bool here, const char* note) {
    char t[128];
    nk_layout_row_dynamic(ctx, 4 * s, 1);
    nk_spacing(ctx, 1);
    nk_layout_row_dynamic(ctx, row, 1);
    snprintf(t, sizeof t, "%s%s%s", cv_quantity_name(q), here ? "   (the field shown)" : "", note);
    nk_label_colored(ctx, t, NK_TEXT_LEFT, here ? P.accent : P.text);
}

/* tooltip of unit i: its size against the first (SI) unit */
static void unit_tip(struct nk_context* ctx, int q, int i) {
    if (q == CV_Q_TEMP || i == 0) return;
    char t[96];
    const cv_unit *u = cv_unit_get(q, i), *si = cv_unit_get(q, 0);
    snprintf(t, sizeof t, "1 %s = %.6g %s", unit_label(u), u->si, unit_label(si));
    tip(ctx, t);
}

/* input: every unit the quantity can be in; the set's own unit undoes a choice */
static bool unit_row_in(struct nk_context* ctx, float s, float row, float cw, int q, bool here) {
    int sysu = cv_sys_unit(G.units, cv_sys_temp(G.units), q), cur = unit_in_now(q);
    bool own = G.unit_in[q] >= 0 && G.unit_in[q] != sysu;
    unit_row_head(ctx, s, row, q, here, own ? "   - changed" : "");
    const char* lab[32];
    int n = CV_MIN(cv_unit_count(q), 32);
    for (int i = 0; i < n; i++) lab[i] = unit_label(cv_unit_get(q, i));
    chip_rows(ctx, s, row, cw, lab, n);
    bool changed = false;
    for (int i = 0; i < n; i++) {
        if (i == sysu) tip(ctx, "The unit of the set chosen above");
        else unit_tip(ctx, q, i);
        if (unit_chip(ctx, lab[i], cur == i)) { G.unit_in[q] = i == sysu ? -1 : i; changed = true; }
    }
    return changed;
}

/* display: as input, or any unit of the quantity */
static bool unit_row_show(struct nk_context* ctx, float s, float row, float cw, int q, bool here) {
    char t[64];
    int in = unit_in_now(q);
    unit_row_head(ctx, s, row, q, here, "");
    if (in < 0) {
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "its input unit is not set: shown as in the file", NK_TEXT_LEFT, P.dim);
        return false;
    }
    const char* lab[33];
    int n = CV_MIN(cv_unit_count(q), 32);
    snprintf(t, sizeof t, "as input (%s)", unit_label(cv_unit_get(q, in)));
    lab[0] = t;
    for (int i = 0; i < n; i++) lab[i + 1] = unit_label(cv_unit_get(q, i));
    chip_rows(ctx, s, row, cw, lab, n + 1);
    bool changed = false;
    tip(ctx, "No conversion");
    if (unit_chip(ctx, t, G.unit_show[q] < 0)) { G.unit_show[q] = -1; changed = true; }
    for (int i = 0; i < n; i++) {
        unit_tip(ctx, q, i);
        if (unit_chip(ctx, lab[i + 1], G.unit_show[q] == i)) { G.unit_show[q] = i; changed = true; }
    }
    return changed;
}

/* "Unit set  [combo]": the label; the combo takes the rest of the row */
static void set_row(struct nk_context* ctx, float s, float row, const char* label, const char* what) {
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 150 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    tip(ctx, what);
    nk_label(ctx, label, NK_TEXT_LEFT);
}

void window_units(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open = false;
    static int tab = 0;                           /* 0 input units, 1 display units */
    if (!G.show_units) { was_open = false; return; }
    if (!was_open) {
        nk_window_show(ctx, "Units", NK_SHOWN);
        tab = G.units > 0 || units_in_custom();   /* the input first, until it is set */
    }
    was_open = true;
    float w = CV_MIN(700 * s, fw * 0.92f), h = CV_MIN(fh * 0.86f, 860 * s);
    if (nk_begin(ctx, "Units", nk_rect((fw - w) / 2, (fh - h) / 2, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        float cw = nk_window_get_content_region(ctx).w;
        bool changed = false, custom_in = units_in_custom();
        int preset = units_show_preset();
        char t[160];

        /* both sides at a glance */
        nk_layout_row_dynamic(ctx, row, 1);
        snprintf(t, sizeof t, "Input: %s%s      Display: %s", cv_sys_name(G.units),
                 custom_in ? " + changes" : "", preset < 0 ? "Custom" : cv_show_name(preset));
        nk_label_colored(ctx, t, NK_TEXT_LEFT, P.accent);

        /* the two tabs */
        nk_layout_row_dynamic(ctx, row * 1.2f, 2);
        if (unit_chip(ctx, "Input units", tab == 0)) tab = 0;
        if (unit_chip(ctx, "Display units", tab == 1)) tab = 1;
        uii_hsep(ctx, s);

        int here = G.has_field && G.field_src != 2 ? cv_field_quantity(G.field_name, G.comp) : -1;
        if (tab == 0) {
            nk_layout_row_dynamic(ctx, row, 1);
            nk_label_colored(ctx, "What the model was built in. CalculiX has no units: results come in that set.",
                             NK_TEXT_LEFT, P.dim);
            set_row(ctx, s, row, "Unit set", "A consistent set: length, mass, time; force and stress follow from them");
            snprintf(t, sizeof t, "%s%s", cv_sys_name(G.units), custom_in ? "  + changes" : "");
            /* SI first, then US, then the rest; each with its base units beside it */
            static const int order[CV_SYS_N] = { CV_SYS_NONE, CV_SYS_MM_T_S, CV_SYS_M_KG_S, CV_SYS_MM_KG_MS,
                                                 CV_SYS_MM_G_MS, CV_SYS_IN_LBF_S, CV_SYS_FT_SLUG_S, CV_SYS_CM_G_S };
            uii_test_mark(ctx, "#unit set");
            if (nk_combo_begin_label(ctx, t, nk_vec2(cw - 150 * s, CV_SYS_N * (row + 4 * s) + 20 * s))) {
                nk_layout_row_dynamic(ctx, row, 2);
                for (int k = 0; k < CV_SYS_N; k++) {
                    int i = order[k];
                    if (nk_combo_item_label(ctx, cv_sys_name(i), NK_TEXT_LEFT)) {
                        G.units = i;
                        for (int q = 0; q < CV_Q_N; q++) G.unit_in[q] = -1;   /* the whole set */
                        changed = true;
                    }
                    nk_label_colored(ctx, cv_sys_base(i), NK_TEXT_LEFT, P.dim);
                }
                nk_combo_end(ctx);
            }
            if (G.units > 0) {
                nk_layout_row_template_begin(ctx, row);
                nk_layout_row_template_push_static(ctx, 150 * s);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_end(ctx);
                nk_spacing(ctx, 1);
                snprintf(t, sizeof t, "%s, %s", cv_sys_base(G.units), unit_label(cv_unit_get(CV_Q_TEMP, cv_sys_temp(G.units))));
                nk_label_colored(ctx, t, NK_TEXT_LEFT, P.dim);
            }
            if (custom_in) {
                nk_layout_row_dynamic(ctx, 2 * row, 1);
                nk_label_colored_wrap(ctx, "Some quantities are set apart from the set. CalculiX solves in one consistent "
                                      "set, so check those rows match what the model really used.", P.warn);
            }
            if (here >= 0) changed |= unit_row_in(ctx, s, row, cw, here, true);
            for (int q = 0; q < CV_Q_N; q++) if (q != here) changed |= unit_row_in(ctx, s, row, cw, q, false);
        } else {
            nk_layout_row_dynamic(ctx, row, 1);
            nk_label_colored(ctx, "What to read the results in. Values are converted from the input units.",
                             NK_TEXT_LEFT, P.dim);
            set_row(ctx, s, row, "Show in", "A whole set at once; change any quantity below");
            if (nk_combo_begin_label(ctx, preset < 0 ? "Custom" : cv_show_name(preset),
                                     nk_vec2(cw - 150 * s, CV_SHOW_N * (row + 4 * s) + 20 * s))) {
                nk_layout_row_dynamic(ctx, row, 1);
                for (int p = 0; p < CV_SHOW_N; p++)
                    if (nk_combo_item_label(ctx, cv_show_name(p), NK_TEXT_LEFT)) {
                        for (int q = 0; q < CV_Q_N; q++) G.unit_show[q] = cv_show_unit(p, q);
                        changed = true;
                    }
                nk_combo_end(ctx);
            }
            if (G.units <= 0 && !custom_in) {
                nk_layout_row_dynamic(ctx, row, 1);
                nk_label_colored(ctx, "Set the input units first: until then only strains convert.", NK_TEXT_LEFT, P.warn);
            }
            if (here >= 0) changed |= unit_row_show(ctx, s, row, cw, here, true);
            for (int q = 0; q < CV_Q_N; q++) if (q != here) changed |= unit_row_show(ctx, s, row, cw, q, false);
        }

        nk_layout_row_dynamic(ctx, 8 * s, 1);
        nk_spacing(ctx, 1);
        nk_layout_row_dynamic(ctx, 2 * row, 1);
        nk_label_colored_wrap(ctx, "Formulas see the values as shown. Node coordinates and the deformed shape stay in model units.",
                              P.dim);
        if (changed) app_units_changed();
    }
    if (nk_window_is_hidden(ctx, "Units")) G.show_units = false;
    nk_end(ctx);
}
