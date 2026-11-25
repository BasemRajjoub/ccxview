/* ui_cpal.c -- the Colour palettes window (#53): the user's own legend colours, to
   match another solver's plots. Made from the current map or typed in, a band per
   colour or blended; kept in ccxview.ini, imported from and exported to JSON or
   XML (cpal.h). A palette is picked like any map, in the legend's map box. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "cpal.h"
#include "cfg.h"
#include "web.h"
#include <math.h>

#define WIN "Colour palettes"

static int sel = -1;

/* the legend shows palette i: its colours, a band each unless blended */
void app_cpal_use(int i) {
    const cv_pal* p = cv_pal_at(i);
    if (!p) return;
    if (!p->blend) G.bands = p->n;
    app_colormap(CV_CMAP_N + i);
}

/* palette i's colours changed: the view follows when it shows it */
static void changed(int i) {
    cv_pal_touch();
    if (G.cmap == CV_CMAP_N + i) app_cpal_use(i);
}

static void remove_pal(int i) {
    int cur = G.cmap - CV_CMAP_N;
    cv_pal_remove(i);
    if (cur == i) app_colormap(CV_CMAP_FAST);
    else if (cur > i) app_colormap(G.cmap - 1);
}

/* what the legend shows now, as a palette: its bands, or 12 points of a smooth map */
static int from_current(void) {
    cv_pal p;
    memset(&p, 0, sizeof p);
    p.blend = G.bands < 2;
    p.n = p.blend ? 12 : CV_MIN(G.bands, CV_PAL_COLS);
    for (int k = 0; k < p.n; k++) {
        float c[3], t = p.blend ? (float)k / (float)(p.n - 1) : ((float)k + 0.5f) / (float)p.n;
        app_cmap_rgb(t, c);
        for (int j = 0; j < 3; j++) p.rgb[k][j] = (uint8_t)(c[j] * 255.f + 0.5f);
    }
    char base[CV_PAL_NAME];
    snprintf(base, sizeof base, "%.40s copy", cv_cmap_name(G.cmap));
    cv_pal_unique(base, p.name, sizeof p.name);
    return cv_pal_add(&p);
}

/* ---- files ------------------------------------------------------------------------ */

void app_cpal_import(const char* path) {
    char m[1200];
    FILE* f = fopen(path, "rb");
    if (!f) { snprintf(m, sizeof m, "palettes: cannot read %s", path); cv_msg_add(&G.msgs, 0, false, m); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* t = sz >= 0 && sz < (1 << 22) ? malloc((size_t)sz + 1) : NULL;
    if (!t) { fclose(f); cv_msg_add(&G.msgs, 0, false, "palettes: the file is too large"); return; }
    t[fread(t, 1, (size_t)sz, f)] = 0;
    fclose(f);
    int bad = 0, was = cv_pal_count(), add = cv_pal_import(t, &bad);
    free(t);
    if (add) snprintf(m, sizeof m, "palettes: %d read from %s", add, path);
    else snprintf(m, sizeof m, "palettes: none in %s (JSON \"UserDefinedPalettes\": { \"name\": [\"r,g,b,a\", ...] }, or the same in XML)", path);
    cv_msg_add(&G.msgs, 0, false, m);
    if (bad) {
        snprintf(m, sizeof m, "palettes: %d colour%s could not be read", bad, bad == 1 ? "" : "s");
        cv_msg_add(&G.msgs, 0, false, m);
    }
    if (add) {
        if (cv_pal_count() > was) sel = cv_pal_count() - 1;
        if (G.cmap >= CV_CMAP_N) app_cpal_use(G.cmap - CV_CMAP_N);   /* one of the same name: replaced */
        G.show_cpal = true;
    }
    snprintf(G.note, sizeof G.note, "%d palette%s imported", add, add == 1 ? "" : "s");
    G.note_t = cv_now();
}

/* beside the model (<model>_palettes.json), else beside the settings */
static void export_json(int only) {
    char base[1024], path[1100];
    if (G.loaded) snprintf(base, sizeof base, "%s", G.path);
    else if (!cv_cfg_default_path(base, sizeof base)) snprintf(base, sizeof base, "ccxview");
    char* dot = strrchr(base, '.');
    char* sep = strrchr(base, cv_path_sep());
    if (dot && (!sep || dot > sep)) *dot = 0;
    const cv_pal* p = cv_pal_at(only);
    if (p) {
        char nm[CV_PAL_NAME];
        snprintf(nm, sizeof nm, "%s", p->name);
        for (char* c = nm; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-')) *c = '_';
        snprintf(path, sizeof path, "%s_palette_%s.json", base, nm);
    } else snprintf(path, sizeof path, "%s_palettes.json", base);
    char* j = cv_pal_export_json(only);
    FILE* f = j ? fopen(path, "wb") : NULL;
    bool ok = f && fputs(j, f) >= 0;
    if (f && fclose(f)) ok = false;
    free(j);
    snprintf(G.note, sizeof G.note, ok ? "saved %s" : "could not write %s", path);
    G.note_t = cv_now();
    cv_msg_add(&G.msgs, 0, false, G.note);
    if (ok) CV_EXPORTED(path);
}

/* ---- the window ----------------------------------------------------------------------- */

static void strip(struct nk_context* ctx, const cv_pal* p) {
    struct nk_rect r;
    if (nk_widget(&r, ctx) == NK_WIDGET_INVALID) return;
    struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
    r.y += 3; r.h -= 6;
    enum { NS = 48 };
    for (int i = 0; i < NS; i++) {
        float c[3];
        cv_pal_sample(p, ((float)i + 0.5f) / NS, c);
        nk_fill_rect(cv, nk_rect(r.x + r.w * i / NS, r.y, r.w / NS + 1, r.h), 0, nk_rgb_f(c[0], c[1], c[2]));
    }
}

/* colour k of palette i: its swatch opens a picker with 0..255 boxes */
static void color_row(struct nk_context* ctx, float s, float row, int i, int k) {
    static int open_pal = -1, open_k = -1;
    cv_pal* p = cv_pal_at(i);
    uint8_t* c = p->rgb[k];
    char t[32];
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 34 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 96 * s);
    for (int b = 0; b < 4; b++) nk_layout_row_template_push_static(ctx, row);
    nk_layout_row_template_end(ctx);
    snprintf(t, sizeof t, "%d", k + 1);
    nk_label_colored(ctx, t, NK_TEXT_LEFT, P.dim);
    struct nk_rect bb = nk_widget_bounds(ctx);
    tip(ctx, "Click to change the colour");
    bool is_open = open_pal == i && open_k == k;
    if (nk_button_color(ctx, nk_rgb(c[0], c[1], c[2]))) { is_open = !is_open; open_pal = is_open ? i : -1; open_k = is_open ? k : -1; }
    snprintf(t, sizeof t, "%d,%d,%d", c[0], c[1], c[2]);
    nk_label(ctx, t, NK_TEXT_LEFT);
    int act = -1;
    tip(ctx, "Move it up the scale (to higher values)");
    if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_UP)) act = 0;
    tip(ctx, "Move it down the scale (to lower values)");
    if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_DOWN)) act = 1;
    tip(ctx, "A copy of it, just above");
    if (nk_button_symbol(ctx, NK_SYMBOL_PLUS)) act = 2;
    tip(ctx, "Remove it (two stay at least)");
    if (nk_button_symbol(ctx, NK_SYMBOL_X)) act = 3;
    if (is_open) {
        struct nk_rect clip = ctx->current->layout->clip;
        float pw = 240 * s, ph = 160 * s + 3 * row + 6 * ctx->style.window.spacing.y;
        struct nk_rect pr = nk_rect(bb.x - clip.x, bb.y + bb.h - clip.y, pw, ph);
        if (nk_popup_begin(ctx, NK_POPUP_STATIC, "cpalpick", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, pr)) {
            nk_layout_row_dynamic(ctx, 160 * s, 1);
            struct nk_colorf cf = nk_color_picker(ctx, nk_color_cf(nk_rgb(c[0], c[1], c[2])), NK_RGB);
            int v[3] = { (int)(cf.r * 255.f + 0.5f), (int)(cf.g * 255.f + 0.5f), (int)(cf.b * 255.f + 0.5f) };
            nk_layout_row_dynamic(ctx, row, 3);
            static const char* const nm[3] = { "#R", "#G", "#B" };
            for (int j = 0; j < 3; j++) nk_property_int(ctx, nm[j], 0, &v[j], 255, 1, 1);
            bool ch = false;
            for (int j = 0; j < 3; j++) if (v[j] != c[j]) { c[j] = (uint8_t)v[j]; ch = true; }
            if (ch) changed(i);
            nk_layout_row_dynamic(ctx, row, 1);
            if (nk_button_label(ctx, "done")) { open_pal = open_k = -1; nk_popup_close(ctx); }
            nk_popup_end(ctx);
        } else open_pal = open_k = -1;
    }
    if (act == 0 && k + 1 < p->n) {
        uint8_t x[3]; memcpy(x, p->rgb[k + 1], 3); memcpy(p->rgb[k + 1], c, 3); memcpy(c, x, 3);
        changed(i);
    } else if (act == 1 && k > 0) {
        uint8_t x[3]; memcpy(x, p->rgb[k - 1], 3); memcpy(p->rgb[k - 1], c, 3); memcpy(c, x, 3);
        changed(i);
    } else if (act == 2 && p->n < CV_PAL_COLS) {
        memmove(p->rgb[k + 1], p->rgb[k], (size_t)(p->n - k) * 3);
        p->n++;
        changed(i);
    } else if (act == 3 && p->n > 2) {
        memmove(p->rgb[k], p->rgb[k + 1], (size_t)(p->n - k - 1) * 3);
        p->n--;
        if (open_pal == i) open_pal = open_k = -1;
        changed(i);
    }
}

void uii_window_cpal(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    static char name[CV_PAL_NAME];
    static int name_len, name_for = -2;
    if (!G.show_cpal) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, WIN, NK_SHOWN);
    was_open = true;
    if (sel < 0 && G.cmap >= CV_CMAP_N) sel = G.cmap - CV_CMAP_N;
    if (sel >= cv_pal_count()) sel = cv_pal_count() - 1;
    float w = CV_MIN(480 * s, fw * 0.92f), h = CV_MIN(fh * 0.86f, 760 * s);
    if (nk_begin(ctx, WIN, nk_rect((fw - w) / 2, (fh - h) / 2, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        static const char* const tl[3] = { "New from the legend", "Import...", "Export all" };
        static const char* const tt[3] = { "A palette of the colours the legend shows now: its bands, or 12 points of a smooth map",
                                           "Palettes from a JSON or XML file: \"UserDefinedPalettes\": { \"name\": [\"r,g,b,a,label\", ...] }\n"
                                           "(a palette of the same name is replaced)",
                                           "Every palette to one JSON file, beside the model" };
        int hit = uik_buttons(ctx, NULL, 3, tl, tt);
        if (hit == 0) { int k = from_current(); if (k >= 0) sel = k; }
        if (hit == 1) app_open_dialog(CV_DLG_PALETTES);
        if (hit == 2 && cv_pal_count()) export_json(-1);

        if (!cv_pal_count()) {
            nk_layout_row_dynamic(ctx, 3 * row, 1);
            nk_label_colored_wrap(ctx, "None yet: make one from what the legend shows, or import another program's "
                                       "palette file. Each colour is a band, the first the lowest values.", P.dim);
        }
        for (int i = 0; i < cv_pal_count(); i++) {
            const cv_pal* p = cv_pal_at(i);
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 64 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);
            strip(ctx, p);
            char t[96];
            snprintf(t, sizeof t, "%s  (%d%s)%s", p->name, p->n, p->blend ? ", blended" : " bands", G.cmap == CV_CMAP_N + i ? "  - in use" : "");
            nk_bool on = i == sel;
            if (nk_selectable_label(ctx, t, NK_TEXT_LEFT, &on) && on) sel = i;
        }

        cv_pal* p = cv_pal_at(sel);
        if (p) {
            uii_hsep(ctx, s);
            if (name_for != sel || (strcmp(name, p->name) && !nk_window_has_focus(ctx))) {
                snprintf(name, sizeof name, "%s", p->name);
                name_len = (int)strlen(name);
                name_for = sel;
            }
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 50 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, 60 * s);
            nk_layout_row_template_push_static(ctx, 64 * s);
            nk_layout_row_template_end(ctx);
            nk_label(ctx, "Name", NK_TEXT_LEFT);
            tip(ctx, "Enter to rename");
            nk_flags ev = nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, name, &name_len, (int)sizeof name - 1, nk_filter_default);
            name[name_len] = 0;
            if ((ev & (NK_EDIT_COMMITED | NK_EDIT_DEACTIVATED)) && strcmp(name, p->name) && !cv_pal_rename(sel, name)) {
                snprintf(name, sizeof name, "%s", p->name);
                name_len = (int)strlen(name);
            }
            tip(ctx, "Colour the field with it (or pick it in the legend's map box)");
            if (nk_button_label(ctx, "Use")) app_cpal_use(sel);
            tip(ctx, "Remove this palette");
            if (nk_button_label(ctx, "Delete")) { remove_pal(sel); sel = -1; p = NULL; }
        }
        if (p) {
            nk_bool bl = p->blend;
            if (uik_check(ctx, "blend between the colours", &bl, "Off: one band per colour, as other solvers' legends; on: a smooth scale through them")) {
                p->blend = bl;
                changed(sel);
            }
            static const char* const bl2[2] = { "Reverse", "Export" };
            static const char* const bt2[2] = { "The colours the other way round", "This palette to a JSON file, beside the model" };
            int h2 = uik_buttons(ctx, NULL, 2, bl2, bt2);
            if (h2 == 0) {
                for (int a = 0, b = p->n - 1; a < b; a++, b--) { uint8_t x[3]; memcpy(x, p->rgb[a], 3); memcpy(p->rgb[a], p->rgb[b], 3); memcpy(p->rgb[b], x, 3); }
                changed(sel);
            }
            if (h2 == 1) export_json(sel);
            nk_layout_row_dynamic(ctx, row, 1);
            nk_label_colored(ctx, "Top: the highest values", NK_TEXT_LEFT, P.dim);
            for (int k = p->n - 1; k >= 0 && p; k--) color_row(ctx, s, row, sel, k);
        }
        nk_layout_row_dynamic(ctx, 8 * s, 1);
        nk_spacing(ctx, 1);
        nk_layout_row_dynamic(ctx, 2 * row, 1);
        nk_label_colored_wrap(ctx, "Kept in ccxview.ini (paletteN), for every model; a model remembers which one it used.", P.dim);
    }
    if (nk_window_is_hidden(ctx, WIN)) G.show_cpal = false;
    nk_end(ctx);
}
