/* ui_windows.c -- the floating windows: messages, probe, find, the text overlay
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

void window_probe(struct nk_context* ctx, float s, float row) {
    if (!G.probe_on || !G.loaded) return;
    float w = 260 * s, h = row * 9.8f;
    struct nk_rect r = nk_rect(G.vp_x + 10 * s, G.vp_y + G.vp_h - h - 10 * s, w, h);
    if (!nk_begin(ctx, "Probe", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {
        nk_end(ctx);
        return;
    }
    nk_window_set_bounds(ctx, "Probe", r);
    const cv_pick* p = &G.probe;
    char buf[160], num[32];
    nk_layout_row_dynamic(ctx, row, 1);
    snprintf(buf, sizeof buf, "element %u  (%s)", G.frd.elem_id[p->elem], cv_frd_type_name(G.frd.etype[p->elem]));
    nk_label(ctx, buf, NK_TEXT_LEFT);
    snprintf(buf, sizeof buf, "material %u   group %u", G.frd.emat[p->elem], G.frd.egrp[p->elem]);
    nk_label(ctx, buf, NK_TEXT_LEFT);
    const float* x = G.frd.xyz + 3 * p->node;
    snprintf(buf, sizeof buf, "node %u  (%.4g, %.4g, %.4g)", G.frd.node_id[p->node], x[0], x[1], x[2]);
    nk_label(ctx, buf, NK_TEXT_LEFT);
    fmt_num(num, sizeof num, G.probe_value);
    if (G.probe_ip) snprintf(buf, sizeof buf, "point %d: %s = %s", G.probe_ip, G.field_label, num);
    else snprintf(buf, sizeof buf, "%s%s = %s", G.field_label, G.elem_mode ? " (elem)" : "", num);
    nk_label(ctx, G.has_field ? buf : "no field", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "Node ids of this element, drawn in the view");
    nk_checkbox_label(ctx, "ids", &G.show_ids);
    if (nk_button_label(ctx, "close")) { G.probe_on = false; G.path_arm = false; }
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

/* ---- overlay: text in the 3D view (node / element ids of the picked element) ---- */
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
            nk_fill_rect(cv, r, 0, nk_rgba(120, 180, 255, 40));
            nk_stroke_rect(cv, r, 0, 1.5f * s, nk_rgba(120, 180, 255, 220));
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

void window_overlay(struct nk_context* ctx, float s) {
    if (!G.loaded || !G.probe_on || !G.show_ids || G.probe.elem >= G.frd.n_elems) return;
    struct nk_rect wr = nk_rect(G.vp_x, G.vp_y, G.vp_w, G.vp_h);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    nk_style_push_float(ctx, &ctx->style.window.border, 0);
    if (nk_begin(ctx, "overlay", wr, NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT | NK_WINDOW_BACKGROUND)) {
        nk_window_set_bounds(ctx, "overlay", wr);
        struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
        const struct nk_user_font* font = ctx->style.font;
        float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f;
        uint32_t e = G.probe.elem;
        v3 cen = v3_make(0, 0, 0);
        uint32_t n = G.frd.eoff[e + 1] - G.frd.eoff[e];
        for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) {
            uint32_t i = G.frd.conn[j];
            const float* p = G.frd.xyz + 3 * i;
            const float* d = G.disp ? G.disp + 3 * i : NULL;
            v3 q = v3_make(p[0] + (d ? d[0] * sc : 0), p[1] + (d ? d[1] * sc : 0), p[2] + (d ? d[2] * sc : 0));
            cen = v3_add(cen, v3_scale(q, 1.f / n));
            float sx, sy;
            if (!app_project(q, &sx, &sy)) continue;
            char lab[16];
            snprintf(lab, sizeof lab, "%u", G.frd.node_id[i]);
            float tw = font->width(font->userdata, font->height, lab, (int)strlen(lab));
            nk_fill_rect(cv, nk_rect(sx + 3 * s, sy - font->height * 0.5f, tw + 4 * s, font->height), 2, nk_rgba(0, 0, 0, 160));
            nk_draw_text(cv, nk_rect(sx + 5 * s, sy - font->height * 0.5f, tw + 2, font->height), lab, (int)strlen(lab), font,
                         nk_rgba(0, 0, 0, 0), nk_rgb(255, 230, 120));
        }
        float sx, sy;
        if (app_project(cen, &sx, &sy)) {
            char lab[24];
            snprintf(lab, sizeof lab, "el %u", G.frd.elem_id[e]);
            float tw = font->width(font->userdata, font->height, lab, (int)strlen(lab));
            nk_fill_rect(cv, nk_rect(sx - tw * 0.5f - 2 * s, sy - font->height * 0.5f, tw + 4 * s, font->height), 2, nk_rgba(0, 0, 0, 160));
            nk_draw_text(cv, nk_rect(sx - tw * 0.5f, sy - font->height * 0.5f, tw + 2, font->height), lab, (int)strlen(lab), font,
                         nk_rgba(0, 0, 0, 0), nk_rgb(150, 220, 255));
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
            if (!missing) G.show_ids = true;
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
static bool is_frd(const char* n) { return has_ext(n, "frd") || has_ext(n, "inp") || has_ext(n, "dat") || has_ext(n, "fbd"); }

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
        if (G.dlg_for_compare) app_compare_open(open_path); else app_open(open_path);
        G.dlg_for_compare = false;
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
