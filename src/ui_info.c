/* ui_info.c -- the Details window: everything about the probed node and element
   (and the box selection it came from) that the small Probe has no room for; and
   the About window: who made ccxview, its licence, what it is built with. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "app_fail.h"
#include "app_mesh.h"
#include "os.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

enum { NL = 96, LW = 200 };
static char L[NL][LW];
static bool head[NL];                    /* a section title */
static int nl;

static void line(bool h, const char* fmt, ...) {
    if (nl >= NL) return;
    va_list ap;
    va_start(ap, fmt); vsnprintf(L[nl], LW, fmt, ap); va_end(ap);
    head[nl++] = h;
}

/* ids, a row at a time */
static void id_rows(const char* label, const uint32_t* ids, uint32_t n) {
    char t[LW];
    size_t o = (size_t)snprintf(t, sizeof t, "%s (%u):", label, n);
    for (uint32_t i = 0; i < n; i++) {
        if (o > 48) { line(false, "%s", t); o = (size_t)snprintf(t, sizeof t, "   "); }
        o += (size_t)snprintf(t + o, sizeof t - o, " %u", ids[i]);
    }
    line(false, "%s", t);
}

static bool in_set(const cv_set* s, uint32_t id) {        /* ids sorted */
    uint32_t lo = 0, hi = s->n;
    while (lo < hi) { uint32_t m = (lo + hi) / 2; if (s->ids[m] < id) lo = m + 1; else hi = m; }
    return lo < s->n && s->ids[lo] == id;
}

static void build(void) {
    const cv_pick* p = &G.probe;
    const cv_frd* f = &G.frd;
    uint32_t n = p->node, e = p->elem;
    char a[32], b[32], c[32], d[32];
    nl = 0;

    /* the node */
    line(true, "Node %u", f->node_id[n]);
    const float* x = f->xyz + 3 * n;
    line(false, "position   %.6g, %.6g, %.6g", x[0], x[1], x[2]);
    if (G.disp) {
        const float* u = G.disp + 3 * n;
        fmt_num(a, sizeof a, u[0]); fmt_num(b, sizeof b, u[1]); fmt_num(c, sizeof c, u[2]);
        fmt_num(d, sizeof d, sqrt((double)u[0] * u[0] + (double)u[1] * u[1] + (double)u[2] * u[2]));
        line(false, "displacement   %s, %s, %s   (size %s)", a, b, c, d);
        line(false, "displaced to   %.6g, %.6g, %.6g", x[0] + u[0], x[1] + u[1], x[2] + u[2]);
    }
    if (G.has_field) {
        fmt_num(a, sizeof a, G.elem_mode ? NAN : G.scalar[n]);
        line(false, "%s = %s%s", G.field_label, a, G.elem_mode ? "  (shown per element, below)" : "");
    }
    char names[CV_MAX_COMP][12];
    float v[CV_MAX_COMP];
    int k = app_field_comps(n, names, v, CV_MAX_COMP);
    if (k) {
        line(false, "%s, every component (global, input units):", G.field_name);
        char t[LW];
        size_t o = 0;
        t[0] = 0;
        for (int i = 0; i < k; i++) {
            fmt_num(a, sizeof a, v[i]);
            o += (size_t)snprintf(t + o, sizeof t - o, "   %s %s", names[i], a);
            if (i % 3 == 2 || i == k - 1) { line(false, "%s", t); o = 0; t[0] = 0; }
        }
    }
    static uint32_t around_for = UINT32_MAX, around[64], na;    /* elements on the node: a scan, so kept */
    static const cv_frd* around_f;
    if (around_for != n || around_f != f) {
        around_for = n; around_f = f; na = 0;
        for (uint32_t q = 0; q < f->n_elems && na < 64; q++)
            for (uint32_t j = f->eoff[q]; j < f->eoff[q + 1]; j++) if (f->conn[j] == n) { around[na++] = f->elem_id[q]; break; }
    }
    id_rows("elements on it", around, na);

    /* the element */
    line(true, "Element %u   %s", f->elem_id[e], cv_frd_type_name(f->etype[e]));
    const char* mat = deck_material_name(f->emat[e]);
    line(false, "material %u%s%s   group %u", f->emat[e], mat ? "  " : "", mat ? mat : "", f->egrp[e]);
    const cv_inp* dk = deck_get();
    if (dk) {
        char t[LW];
        size_t o = (size_t)snprintf(t, sizeof t, "element sets:");
        int ns = 0;
        for (int i = 0; i < dk->nsets && o < sizeof t - 70; i++)
            if (dk->sets[i].is_elem && in_set(&dk->sets[i], f->elem_id[e])) { o += (size_t)snprintf(t + o, sizeof t - o, " %s", dk->sets[i].name); ns++; }
        line(false, "%s", ns ? t : "element sets: none");
    }
    uint32_t ids[32], m = CV_MIN(f->eoff[e + 1] - f->eoff[e], 32u);
    for (uint32_t j = 0; j < m; j++) ids[j] = f->node_id[f->conn[f->eoff[e] + j]];
    id_rows("nodes", ids, m);
    if (G.has_field && G.elem_mode) { fmt_num(a, sizeof a, G.elem_val[e]); line(false, "%s (element) = %s", G.field_label, a); }
    char t[LW];
    if (G.has_field && fail_probe_text(n, e, t, sizeof t)) line(false, "failure   %s", t);
    if (G.has_field && mesh_probe_text(e, t, sizeof t)) line(false, "mesh   %s", t);

    /* the box it came from */
    if (G.sel_n || G.seln_n) {
        line(true, "Box selection: %u elements, %u nodes (%s)", G.sel_n, G.seln_n,
             G.sel_crossing ? "crossing: elements with any node inside" : "window: elements wholly inside");
        if (G.boxq.on && G.boxq.gen == G.field_gen) {
            const uint32_t* id = G.boxq.elem ? f->elem_id : f->node_id;
            const char* what = G.boxq.elem ? "element" : "node";
            fmt_num(a, sizeof a, G.boxq.vmax); fmt_num(b, sizeof b, G.boxq.vmin);
            line(false, "max %s at %s %u", a, what, id[G.boxq.max_at]);
            line(false, "min %s at %s %u", b, what, id[G.boxq.min_at]);
            line(false, "over %u %s", G.boxq.n, G.boxq.elem ? "elements" : "nodes");
        }
    }
}

void window_details(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_details || !G.probe_on || !G.loaded) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Details", NK_SHOWN);
    was_open = true;
    build();
    /* as wide as its longest line; above the Probe at the left, so the model stays free to turn */
    const struct nk_user_font* fnt = ctx->style.font;
    float tw = 0;
    for (int i = 0; i < nl; i++) tw = CV_MAX(tw, fnt->width(fnt->userdata, fnt->height, L[i], (int)strlen(L[i])));
    float w = CV_MIN(CV_MAX(300 * s, tw + 2 * ctx->style.window.padding.x + 30 * s), G.vp_w * 0.6f);
    const struct nk_window* pw = nk_window_find(ctx, "Probe");
    float bottom = pw ? pw->bounds.y - 6 * s : G.vp_y + G.vp_h - 10 * s;
    float h = CV_MIN(bottom - G.vp_y - 10 * s, row * (nl + 3.5f) + 16 * s);
    if (nk_begin(ctx, "Details", nk_rect(G.vp_x + 10 * s, bottom - h, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(ctx, row, 1);
        for (int i = 0; i < nl; i++) {
            if (head[i] && i) { uii_hsep(ctx, s); nk_layout_row_dynamic(ctx, row, 1); }
            if (head[i]) nk_label_colored(ctx, L[i], NK_TEXT_LEFT, P.accent); else nk_label(ctx, L[i], NK_TEXT_LEFT);
        }
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "All of it to the clipboard, a line each");
        if (nk_button_label(ctx, "copy all")) {
            static char txt[NL * LW];
            size_t o = 0;
            for (int i = 0; i < nl && o < sizeof txt - LW - 2; i++) o += (size_t)snprintf(txt + o, sizeof txt - o, "%s\n", L[i]);
            sapp_set_clipboard_string(txt);
        }
    }
    if (nk_window_is_hidden(ctx, "Details")) G.show_details = false;
    nk_end(ctx);
}

/* ---- About ------------------------------------------------------------------------- */

static const struct { const char *name, *by, *licence, *url; } credits[] = {
    { "sokol",            "Andre Weissflog",                 "zlib",        "https://github.com/floooh/sokol" },
    { "Nuklear",          "Micha Mettke and contributors",   "MIT / PD",    "https://github.com/Immediate-Mode-UI/Nuklear" },
    { "stb_image_write, stb_sprintf", "Sean Barrett",        "MIT / PD",    "https://github.com/nothings/stb" },
    { "tinyfiledialogs",  "Guillaume Vareille",              "zlib",        "https://sourceforge.net/projects/tinyfiledialogs/" },
    { "minih264, minimp4", "Lieff",                          "CC0",         "https://github.com/lieff/minih264" },
    { "TinyExpr",         "Lewis Van Winkle",                "zlib",        "https://github.com/codeplea/tinyexpr" },
    { "miniz",            "Rich Geldreich and contributors", "MIT",         "https://github.com/richgel999/miniz" },
    { "Inter (font)",     "Rasmus Andersson",                "OFL 1.1",     "https://github.com/rsms/inter" },
    { "Noto Sans Math (font)", "Google, Noto project",       "OFL 1.1",     "https://github.com/notofonts/math" },
    { "Lucide (icons)",   "Lucide contributors, Feather",    "ISC",         "https://github.com/lucide-icons/lucide" },
    { "ProggyClean (font)", "Tristan Grimmer",               "MIT",         "https://github.com/bluescan/proggyfonts" },
};

static void link_button(struct nk_context* ctx, const char* label, const char* url) {
    tip(ctx, url);
    if (nk_button_label(ctx, label) && !cv_open_url(url))
        cv_msg_add(&G.msgs, 0, false, "could not open the browser");
}

void window_about(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_about) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "About", NK_SHOWN);
    was_open = true;
    float w = CV_MIN(560 * s, fw * 0.9f), h = CV_MIN(fh * 0.85f, row * 24);
    if (nk_begin(ctx, "About", nk_rect((fw - w) / 2, (fh - h) / 2, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        char t[160];
        nk_layout_row_dynamic(ctx, row, 1);
        snprintf(t, sizeof t, "ccxview %s", app_version());
        nk_label_colored(ctx, t, NK_TEXT_LEFT, P.accent);
        nk_label(ctx, "A fast viewer for CalculiX results and models.", NK_TEXT_LEFT);
        nk_label_colored(ctx, "Free software under the GPL, version 3 or later.", NK_TEXT_LEFT, P.dim);
        uii_hsep(ctx, s);
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label(ctx, "Made by Basem Rajjoub, structural engineer and researcher", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, row, 3);
        link_button(ctx, "website", "https://basemrajjoub.com/");
        link_button(ctx, "LinkedIn", "https://www.linkedin.com/in/rajjoub/");
        link_button(ctx, "GitHub", "https://github.com/BasemRajjoub");
        nk_layout_row_dynamic(ctx, row, 2);
        link_button(ctx, "ccxview on GitHub", "https://github.com/BasemRajjoub/ccxview");
        link_button(ctx, "report a problem", "https://github.com/BasemRajjoub/ccxview/issues");
        uii_hsep(ctx, s);
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "Built with, and thanks to their authors:", NK_TEXT_LEFT, P.dim);
        const float cols[] = { 0.38f, 0.42f, 0.2f };
        for (size_t i = 0; i < CV_COUNT(credits); i++) {
            nk_layout_row(ctx, NK_DYNAMIC, row, 3, cols);
            link_button(ctx, credits[i].name, credits[i].url);
            nk_label(ctx, credits[i].by, NK_TEXT_LEFT);
            nk_label_colored(ctx, credits[i].licence, NK_TEXT_LEFT, P.dim);
        }
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "Their licence texts ship in the licenses folder.", NK_TEXT_LEFT, P.dim);
    }
    if (nk_window_is_hidden(ctx, "About")) G.show_about = false;
    nk_end(ctx);
}
