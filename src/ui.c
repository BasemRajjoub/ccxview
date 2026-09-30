/* ui.c -- the whole interface: scene tree (left), toolbar (above the view),
   time bar (below it), legend (right), status bar, probe and message windows. */
#include "app.h"
#include "ui.h"
#include "cfg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "nuklear_style.c"          /* upstream demo themes: set_style() */
#include <math.h>

/* ---- scale + font -------------------------------------------------------------- */

static struct {
    float  zoom;              /* user factor, Ctrl +/- */
    float  scale;             /* applied scale (desktop x zoom) */
    struct nk_font_atlas atlas;
    bool   atlas_live;
    sg_image img; sg_view view; sg_sampler smp; snk_image_t snk;
    struct nk_font* font;
    int    theme;             /* index into themes[] */
    bool   restyle;           /* theme changed: rebuild the style next frame */
} U = { 1.f, 0.f };

/* the colours we draw ourselves (hints, plots, legend), derived from the theme */
static struct {
    struct nk_color text, dim, warn, accent, accent_text, plot_bg, grid, frame, tick;
} P;

/* 0 is ccxview's own look; the rest are the upstream demo themes as they are.
   Only the Catppuccin ones: the older demo themes are flat greys. */
static const struct { const char* name; int nk; } themes[] = {
    { "ccxview",              -1 },
    { "Catppuccin Latte",     THEME_CATPPUCCIN_LATTE },
    { "Catppuccin Frappe",    THEME_CATPPUCCIN_FRAPPE },
    { "Catppuccin Macchiato", THEME_CATPPUCCIN_MACCHIATO },
    { "Catppuccin Mocha",     THEME_CATPPUCCIN_MOCHA },
};
enum { NTHEMES = (int)(sizeof themes / sizeof themes[0]) };

const char* ui_get_theme(void) { return themes[U.theme].name; }
void ui_set_theme(const char* name) {
    for (int i = 0; i < NTHEMES; i++)
        if (name && !strcmp(name, themes[i].name)) { U.theme = i; U.restyle = true; return; }
}

static float desktop_scale(void) {
    const char* e = getenv("CCXVIEW_SCALE");
    if (e && atof(e) >= 0.25) return (float)atof(e);
    float s = sapp_dpi_scale();
#if defined(__linux__)
    /* X11 without an Xft.dpi resource reports 1; honour the toolkit variables. */
    if (s < 1.01f) {
        const char* g = getenv("GDK_SCALE");
        const char* q = getenv("QT_SCALE_FACTOR");
        if (g && atof(g) > 1) s = (float)atof(g);
        else if (q && atof(q) > 1) s = (float)atof(q);
    }
#endif
    return s > 0.25f ? s : 1.f;
}

float ui_scale(void) { return U.scale > 0 ? U.scale : 1.f; }

float ui_get_zoom(void) { return U.zoom; }
void ui_set_zoom(float z) { U.zoom = CV_MIN(CV_MAX(z, 0.5f), 3.f); }

void ui_zoom(int dir) {
    if (dir == 0) U.zoom = 1.f;
    else U.zoom = CV_MIN(CV_MAX(U.zoom * (dir > 0 ? 1.1f : 1.f / 1.1f), 0.5f), 3.f);
}

static void bake_font(struct nk_context* ctx, float px) {
    if (U.atlas_live) {
        snk_destroy_image(U.snk);
        sg_destroy_view(U.view);
        sg_destroy_image(U.img);
        nk_font_atlas_clear(&U.atlas);
    }
    nk_font_atlas_init_default(&U.atlas);
    nk_font_atlas_begin(&U.atlas);
    struct nk_font_config cfg = nk_font_config(px);
    cfg.oversample_h = 3;
    cfg.oversample_v = 2;
    U.font = nk_font_atlas_add_default(&U.atlas, px, &cfg);
    int w, h;
    const void* pixels = nk_font_atlas_bake(&U.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    U.img = sg_make_image(&(sg_image_desc){
        .width = w, .height = h, .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = { pixels, (size_t)w * (size_t)h * 4 },
    });
    U.view = sg_make_view(&(sg_view_desc){ .texture.image = U.img });
    U.snk = snk_make_image(&(snk_image_desc_t){ .texture_view = U.view, .sampler = U.smp });
    nk_font_atlas_end(&U.atlas, snk_nkhandle(U.snk), 0);
    nk_font_atlas_cleanup(&U.atlas);
    nk_style_set_font(ctx, &U.font->handle);
    U.atlas_live = true;
}

#define SV(v) ((v).x *= s, (v).y *= s)

static float lum(struct nk_color c) { return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.f; }

static struct nk_color mix(struct nk_color a, struct nk_color b, float t) {
    return nk_rgb((int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t), (int)(a.b + (b.b - a.b) * t));
}

static void restyle(struct nk_context* ctx, float s) {
    if (themes[U.theme].nk < 0) nk_style_default(ctx);
    else set_style(ctx, (enum theme)themes[U.theme].nk);
    struct nk_style* st = &ctx->style;
    SV(st->window.padding); SV(st->window.spacing); SV(st->window.scrollbar_size);
    SV(st->window.min_size); SV(st->window.group_padding); SV(st->window.popup_padding);
    SV(st->window.combo_padding); SV(st->window.contextual_padding); SV(st->window.menu_padding);
    SV(st->window.tooltip_padding); SV(st->window.header.padding); SV(st->window.header.label_padding);
    SV(st->window.header.spacing);
    st->window.min_row_height_padding *= s;
    SV(st->button.padding); SV(st->button.touch_padding);
    SV(st->contextual_button.padding); SV(st->menu_button.padding);
    SV(st->checkbox.padding); SV(st->checkbox.touch_padding); st->checkbox.spacing *= s;
    SV(st->option.padding); SV(st->option.touch_padding); st->option.spacing *= s;
    SV(st->selectable.padding); SV(st->selectable.touch_padding);
    SV(st->slider.padding); SV(st->slider.spacing); SV(st->slider.cursor_size); st->slider.bar_height *= s;
    SV(st->property.padding); SV(st->property.edit.padding); SV(st->property.inc_button.padding);
    SV(st->property.dec_button.padding);
    SV(st->edit.padding); SV(st->edit.scrollbar_size); st->edit.cursor_size *= s; st->edit.row_padding *= s;
    SV(st->combo.content_padding); SV(st->combo.button_padding); SV(st->combo.spacing);
    SV(st->tab.padding); SV(st->tab.spacing); st->tab.indent *= s;
    SV(st->tab.tab_minimize_button.padding); SV(st->tab.tab_maximize_button.padding);
    SV(st->tab.node_minimize_button.padding); SV(st->tab.node_maximize_button.padding);
    SV(st->scrollh.padding); SV(st->scrollv.padding);

    if (themes[U.theme].nk < 0) {
        /* calmer palette than the default */
        st->window.fixed_background = nk_style_item_color(nk_rgba(38, 38, 40, 245));
        st->window.background = nk_rgba(38, 38, 40, 245);
        st->window.border_color = nk_rgba(64, 64, 68, 255);
        st->tab.background = nk_style_item_color(nk_rgba(48, 48, 52, 255));

        /* the default tick is dark grey on grey: a ticked box read as empty */
        const struct nk_color box = nk_rgb(70, 70, 76), tick = nk_rgb(96, 170, 240);
        st->checkbox.normal = st->checkbox.hover = st->checkbox.active = nk_style_item_color(box);
        st->checkbox.cursor_normal = st->checkbox.cursor_hover = nk_style_item_color(tick);
        st->option.normal = st->option.hover = st->option.active = nk_style_item_color(box);
        st->option.cursor_normal = st->option.cursor_hover = nk_style_item_color(tick);

        P.text = nk_rgb(225, 225, 225); P.dim = nk_rgb(150, 150, 150); P.warn = nk_rgb(200, 180, 120);
        P.accent = tick; P.accent_text = nk_rgb(150, 190, 235);
        P.plot_bg = nk_rgb(28, 28, 30); P.grid = nk_rgb(50, 50, 54); P.frame = nk_rgb(20, 20, 20);
        P.tick = nk_rgb(200, 200, 200);
        return;
    }

    /* an upstream theme: take its colours, only fix a tick that does not show */
    struct nk_color win = st->window.background, box = st->checkbox.normal.data.color;
    struct nk_color acc = st->slider.cursor_normal.data.color;
    bool light = lum(win) > 0.5f;
    if (fabsf(lum(st->checkbox.cursor_normal.data.color) - lum(box)) < 0.2f) {
        struct nk_color tick = fabsf(lum(acc) - lum(box)) >= 0.2f ? acc : st->text.color;
        st->checkbox.cursor_normal = st->checkbox.cursor_hover = nk_style_item_color(tick);
        st->option.cursor_normal = st->option.cursor_hover = nk_style_item_color(tick);
    }
    P.text = st->text.color;
    P.dim = mix(P.text, win, 0.4f);
    P.warn = light ? nk_rgb(160, 100, 20) : nk_rgb(200, 180, 120);
    P.accent = P.accent_text = acc;
    P.plot_bg = st->edit.normal.data.color;
    P.grid = mix(P.plot_bg, P.text, 0.2f);
    P.frame = light ? nk_rgb(60, 60, 60) : nk_rgb(20, 20, 20);
    P.tick = P.dim;
}

static void apply_scale(struct nk_context* ctx) {
    float s = desktop_scale() * U.zoom;
    if (fabsf(s - U.scale) < 0.01f && U.atlas_live) {
        if (U.restyle) { U.restyle = false; restyle(ctx, s); }
        return;
    }
    U.scale = s;
    U.restyle = false;
    bake_font(ctx, roundf(13.f * s));
    restyle(ctx, s);
}

void ui_init(void) {
    U.smp = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR, .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE });
}

/* ---- small helpers ----------------------------------------------------------- */

/* tooltip for the widget laid out next */
static void tip(struct nk_context* ctx, const char* text) {
    if (nk_widget_is_hovered(ctx)) nk_tooltip(ctx, text);
}

static bool g_focus_open;          /* Ctrl+L: put the cursor in the path box */
void ui_focus_open(void) { g_focus_open = true; }

/* Whether a Nuklear window under the mouse should keep the event from the 3D
   view. The legend and an empty drop hint only display, so they let everything
   through; the axes gizmo takes clicks but not the wheel. */
bool ui_mouse_captured(struct nk_context* ctx, bool wheel) {
    float mx = ctx->input.mouse.pos.x, my = ctx->input.mouse.pos.y;
    for (struct nk_window* w = ctx->begin; w; w = w->next) {
        if (w->flags & NK_WINDOW_HIDDEN) continue;
        const struct nk_window* p = w->popup.active ? w->popup.win : NULL;
        if (p && mx >= p->bounds.x && my >= p->bounds.y && mx < p->bounds.x + p->bounds.w && my < p->bounds.y + p->bounds.h)
            return true;
        struct nk_rect b = w->bounds;
        if (!(mx >= b.x && my >= b.y && mx < b.x + b.w && my < b.y + b.h)) continue;
        if ((w->flags & NK_WINDOW_NO_INPUT) && (!strcmp(w->name_string, "hint") || !strcmp(w->name_string, "overlay") || !strcmp(w->name_string, "navmark"))) continue;
        if (wheel && (!strcmp(w->name_string, "axes") || !strcmp(w->name_string, "Legend"))) continue;
        return true;
    }
    return false;
}

/* Nuklear scrolls only its active window, and a window becomes active by
   being clicked. Make the one under the wheel active, so a panel scrolls
   without a click first. */
void ui_wheel_focus(struct nk_context* ctx) {
    float mx = ctx->input.mouse.pos.x, my = ctx->input.mouse.pos.y;
    for (struct nk_window* w = ctx->begin; w; w = w->next) {
        if (w->flags & (NK_WINDOW_HIDDEN | NK_WINDOW_BACKGROUND)) continue;
        struct nk_rect b = w->bounds;
        if (mx >= b.x && my >= b.y && mx < b.x + b.w && my < b.y + b.h) { ctx->active = w; return; }
    }
}

static void fmt_num(char* out, size_t n, double v) {
    if (v != v) { snprintf(out, n, "-"); return; }
    double a = fabs(v);
    if (a != 0 && (a < 1e-3 || a >= 1e5)) snprintf(out, n, "%.3e", v);
    else snprintf(out, n, "%.4g", v);
}

/* the legend's numbers follow the format chosen in its settings window */
#define legend_num(out, n, v) app_legend_fmt(out, n, v)

static void axis_label(char* out, size_t n, int axis, uint32_t v, uint32_t count) {
    if (axis == CV_AXIS_TYPE) snprintf(out, n, "%s  (%u)", cv_frd_type_name((int)v), count);
    else if (axis == CV_AXIS_MAT) {
        const char* nm = deck_material_name(v);          /* the deck names its materials */
        if (nm) snprintf(out, n, "%s  (%u)", nm, count);
        else if (v) snprintf(out, n, "Material %u  (%u)", v, count);
        else snprintf(out, n, "no material  (%u)", count);
    }
    else snprintf(out, n, "Group %u  (%u)", v, count);
}

/* ---- panels --------------------------------------------------------------------- */

static int g_pick_axis = -1, g_pick_idx = -1;   /* open colour picker (group swatch) */

/* The deck's named sets: element sets are a display group (tick to show only
   them; none ticked = everything), node sets and surfaces are highlights. */
static void panel_deck_sets(struct nk_context* ctx, float s, float row) {
    const cv_inp* d = deck_get();
    if (!d) return;
    bool* on = deck_set_flags();
    bool* son = deck_surf_flags();
    int ne = 0, nn = 0;
    for (int i = 0; i < d->nsets; i++) { if (d->sets[i].is_elem) ne++; else nn++; }
    char lab[96];
    if (ne && nk_tree_push_id(ctx, NK_TREE_NODE, "Element sets", NK_MAXIMIZED, 40)) {
        nk_layout_row_dynamic(ctx, row, 1);
        if (deck_any_elset_on() && nk_button_label(ctx, "show everything")) {
            for (int i = 0; i < d->nsets; i++) if (d->sets[i].is_elem) on[i] = false;
            app_groups_changed();
        }
        int k = 0;
        for (int i = 0; i < d->nsets && k < 500; i++) {
            if (!d->sets[i].is_elem) continue;
            k++;
            snprintf(lab, sizeof lab, "%s  (%u)", d->sets[i].name, d->sets[i].n);
            if (nk_checkbox_label(ctx, lab, &on[i])) app_groups_changed();
        }
        nk_tree_pop(ctx);
    }
    if (nn && nk_tree_push_id(ctx, NK_TREE_NODE, "Node sets", NK_MINIMIZED, 41)) {
        nk_layout_row_dynamic(ctx, row, 1);
        int k = 0;
        for (int i = 0; i < d->nsets && k < 500; i++) {
            if (d->sets[i].is_elem) continue;
            k++;
            snprintf(lab, sizeof lab, "%s  (%u)", d->sets[i].name, d->sets[i].n);
            if (nk_checkbox_label(ctx, lab, &on[i])) { G.show_hl = true; deck_refresh_highlight(); }
        }
        nk_tree_pop(ctx);
    }
    if (d->nlinks && nk_tree_push_id(ctx, NK_TREE_NODE, "Couplings", NK_MINIMIZED, 44)) {
        static const char* kinds[] = { "rigid", "kinematic", "distributing", "equation", "tie", "contact" };
        bool* lon = deck_link_flags();
        nk_layout_row_dynamic(ctx, row, 1);
        for (int i = 0; i < d->nlinks && i < 500; i++) {
            const cv_link* l = &d->links[i];
            if (l->kind == CV_LINK_TIE || l->kind == CV_LINK_CONTACT) {
                const char* a = l->surf[0] >= 0 ? d->surfs[l->surf[0]].name : "?";
                const char* b = l->surf[1] >= 0 ? d->surfs[l->surf[1]].name : "?";
                snprintf(lab, sizeof lab, "%s %s: %s / %s", kinds[l->kind], l->name, a, b);
            } else if (l->surf[0] >= 0) {
                snprintf(lab, sizeof lab, "%s %s  (node %u, %s)", kinds[l->kind], l->name, l->ref, d->surfs[l->surf[0]].name);
            } else {
                snprintf(lab, sizeof lab, "%s %s  (node %u, %u)", kinds[l->kind], l->name, l->ref, l->n);
            }
            if (nk_checkbox_label(ctx, lab, &lon[i])) { G.show_links = true; G.show_hl = true; deck_refresh_highlight(); }
        }
        nk_tree_pop(ctx);
    }
    if (d->nsurfs && nk_tree_push_id(ctx, NK_TREE_NODE, "Surfaces", NK_MINIMIZED, 42)) {
        nk_layout_row_dynamic(ctx, row, 1);
        for (int i = 0; i < d->nsurfs && i < 500; i++) {
            snprintf(lab, sizeof lab, "%s  (%u)", d->surfs[i].name, d->surfs[i].n ? d->surfs[i].n : d->surfs[i].nn);
            if (nk_checkbox_label(ctx, lab, &son[i])) { G.show_hl = true; deck_refresh_highlight(); }
        }
        nk_tree_pop(ctx);
    }
}

/* cgx sets: tick to show only what they hold (their geometry, and their
   elements when cgx meshed the script). */
static void panel_geo_sets(struct nk_context* ctx, float row) {
    const cv_fbd* g = geo_get();
    if (!g || !g->nsets) return;
    bool* on = geo_set_flags();
    if (!nk_tree_push_id(ctx, NK_TREE_NODE, "cgx sets", NK_MAXIMIZED, 43)) return;
    nk_layout_row_dynamic(ctx, row, 1);
    if (geo_any_on() && nk_button_label(ctx, "show everything")) geo_show_all();
    char lab[96];
    for (int i = 0, k = 0; i < g->nsets && k < 500; i++) {
        const cv_gset* st = &g->sets[i];
        if (geo_set_hidden(st->name)) continue;
        k++;
        uint32_t ng = st->npts + st->ncrv + st->nsrf;
        if (st->nel) snprintf(lab, sizeof lab, "%s  (%u el)", st->name, st->nel);
        else if (ng) snprintf(lab, sizeof lab, "%s  (%u)", st->name, ng);
        else snprintf(lab, sizeof lab, "%s  (%u nodes)", st->name, st->nnod);
        if (nk_checkbox_label(ctx, lab, &on[i])) geo_set_toggled(i);
    }
    nk_tree_pop(ctx);
}

/* Nuklear's slider moves only when its knob is grabbed: a press elsewhere on
   the bar first puts the value there (the knob then sits under the mouse and
   the same press drags on). Call right before nk_slider_*; true when it jumped. */
static bool slider_jump(struct nk_context* ctx, float lo, float* v, float hi, float step) {
    /* not nk_widget_is_hovered: that one is false unless this window is Nuklear's
       active one, and the side panels seldom are */
    if (!nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT) || !ctx->current || !ctx->current->layout ||
        (ctx->current->flags & NK_WINDOW_ROM)) return false;           /* ROM: a popup is open over it */
    struct nk_rect b = nk_widget_bounds(ctx), c = ctx->current->layout->clip;
    struct nk_vec2 at = ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos;
    if (!NK_INBOX(at.x, at.y, b.x, b.y, b.w, b.h) || !NK_INBOX(at.x, at.y, c.x, c.y, c.w, c.h)) return false;
    const struct nk_style_slider* st = &ctx->style.slider;
    float x0 = b.x + st->padding.x, w = b.w - 2 * st->padding.x;
    if (w <= 0 || hi <= lo) return false;
    float t = CV_MIN(CV_MAX((at.x - x0) / w, 0.f), 1.f);
    float nv = lo + t * (hi - lo);
    if (step > 0) nv = CV_MIN(lo + roundf((nv - lo) / step) * step, hi);
    bool ch = nv != *v;
    *v = nv;
    return ch;
}
static bool ui_slider_float(struct nk_context* ctx, float lo, float* v, float hi, float step) {
    bool j = slider_jump(ctx, lo, v, hi, step);
    return nk_slider_float(ctx, lo, v, hi, step) || j;
}
static bool ui_slider_int(struct nk_context* ctx, int lo, int* v, int hi, int step) {
    float f = (float)*v;
    bool j = slider_jump(ctx, (float)lo, &f, (float)hi, (float)step);
    *v = (int)lroundf(f);
    return nk_slider_int(ctx, lo, v, hi, step) || j;
}

/* A size as a logarithmic slider (1x sits in the middle of 0.1x .. 10x); the value
   button sets -1, which the caller turns into its default. True when it changed. */
static bool sub_push(struct nk_context* ctx, const char* title, int t);

static bool scale_slider(struct nk_context* ctx, float row, const char* label, const char* help, float* v, float lo, float hi) {
    static const float ratio[3] = { 0.3f, 0.5f, 0.2f };
    nk_layout_row(ctx, NK_DYNAMIC, row, 3, ratio);
    tip(ctx, help);
    nk_label(ctx, label, NK_TEXT_LEFT);
    float old = *v, t = log10f(CV_MIN(CV_MAX(*v, lo), hi));
    tip(ctx, help);
    if (ui_slider_float(ctx, log10f(lo), &t, log10f(hi), 0.01f)) *v = powf(10.f, t);
    char b[32];
    snprintf(b, sizeof b, *v < 10 ? "%.2f" : "%.1f", *v);
    tip(ctx, "Click: back to the default");
    if (nk_button_label(ctx, b)) *v = -1;    /* the caller's default */
    return *v != old;
}

/* Symbol sizes: supports, springs, loads, vector arrows, highlighted sets */
static void symbol_sizes(struct nk_context* ctx, float s, float row) {
    bool deck = deck_has_bc() || deck_has_loads() || deck_has_discrete();
    if (!sub_push(ctx, "Symbol sizes", CV_TREE_SYMBOLS)) return;
    (void)s;
    if (deck) {
        bool ch = false;
        if (deck_has_bc() || deck_has_discrete()) {
            ch |= scale_slider(ctx, row, "supports", "Supports, springs and masses, times the mesh's symbol size (about two elements)", &G.bc_scale, 0.1f, 10.f);
            if (G.bc_scale < 0) G.bc_scale = 1.f;
        }
        if (deck_has_loads()) {
            ch |= scale_slider(ctx, row, "loads", "Load arrows, times the mesh's symbol size", &G.load_scale, 0.1f, 10.f);
            if (G.load_scale < 0) G.load_scale = 1.f;
        }
        if (ch) deck_refresh_highlight();
    }
    if (app_field_is_vector() && G.show_vec) {
        if (scale_slider(ctx, row, "vectors %", "Longest vector arrow, percent of the model diagonal", &G.vec_pct, 0.2f, 50.f)) {
            if (G.vec_pct < 0) G.vec_pct = 5.f;
            app_vectors_changed();
        }
    }
    scale_slider(ctx, row, "sets px", "Balls of a highlighted node set or surface, pixels", &G.hl_size, 2.f, 40.f);
    if (G.hl_size < 0) G.hl_size = 8.f;
    nk_tree_pop(ctx);
}

/* ---- Layers: what is drawn and how it is coloured */
static void section_layers(struct nk_context* ctx, float s, float row) {
    /* layers */
    if (nk_tree_state_push(ctx, NK_TREE_TAB, "Layers", (enum nk_collapse_states*)&G.tree[CV_TREE_LAYERS])) {
        if (geo_loaded()) {                      /* cgx geometry */
            nk_layout_row_dynamic(ctx, row, 3);
            nk_checkbox_label(ctx, "Points", &G.show_geo_pts);
            nk_checkbox_label(ctx, "Lines", &G.show_geo_crv);
            nk_checkbox_label(ctx, "Surfs", &G.show_geo_srf);
            if (G.show_geo_pts) {
                nk_layout_row_dynamic(ctx, row, 1);
                nk_property_float(ctx, "#geometry point size", 1.f, &G.geo_size, 20.f, 1.f, 0.1f);
            }
        }
        if (deck_has_bc() || deck_has_loads() || deck_has_discrete()) {   /* the deck's supports, loads, springs */
            nk_layout_row_dynamic(ctx, row, 2);
            if (deck_has_bc() || deck_has_loads()) {
                tip(ctx, "*BOUNDARY: a cone per fixed dof, tip on the node (double base for a rotation),\ncross for a temperature");
                nk_checkbox_label(ctx, "Supports", &G.show_bc);
                tip(ctx, "*CLOAD as arrows at the nodes, *DLOAD pressures at the faces;\nlength follows the magnitude");
                nk_checkbox_label(ctx, "Loads", &G.show_loads);
            }
            if (deck_has_discrete()) {
                tip(ctx, "Springs as zigzags, dashpots as pistons, gaps as facing bars, masses as cubes.\nA one-node spring points along its dof to a ground bar.");
                nk_checkbox_label(ctx, "Springs, masses", &G.show_disc);
            }
            if (deck_get() && deck_get()->nlinks) {
                tip(ctx, "Rigid bodies, couplings and equations as spiders from the reference node;\ntick single ones under Groups > Couplings");
                nk_checkbox_label(ctx, "Couplings", &G.show_links);
            }
        }
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Balls at the field's minimum and maximum (legend settings: go there)");
        nk_checkbox_label(ctx, "Min / max", &G.show_markers);
        tip(ctx, "The undeformed edges in grey behind the deformed shape");
        nk_checkbox_label(ctx, "Undeformed", &G.show_ghost);
        nk_checkbox_label(ctx, "Faces", &G.show_faces);
        {
            static const char* fm_names[FM_N] = { "field", "by type", "by material", "by group", "plain" };
            int fm = nk_combo(ctx, fm_names, FM_N, G.faces_mode, (int)row, nk_vec2(150 * s, 5 * row + 20 * s));
            if (fm != G.faces_mode) app_set_faces_mode(fm);
        }
        tip(ctx, "Edges of the exterior faces");
        nk_checkbox_label(ctx, "Edges", &G.show_edges);
        tip(ctx, "Paint the field on the edges too (else dark lines)");
        nk_checkbox_label(ctx, "coloured", &G.edges_field);
        if (G.show_edges && G.show_faces) {
            tip(ctx, "Hide the edges while they are denser than ~3 px on screen,\nwhere they would paint the surface black; zoom in to see them");
            nk_checkbox_label(ctx, "hide when dense", &G.edges_auto);
            if (G.edges_auto && G.edges_dense) nk_label_colored(ctx, "hidden: zoom in", NK_TEXT_LEFT, P.warn);
            else nk_spacing(ctx, 1);
        }
        tip(ctx, "Nodes of the visible elements, as small balls");
        nk_checkbox_label(ctx, "Nodes", &G.show_nodes);
        tip(ctx, "Paint the field on the nodes too");
        nk_checkbox_label(ctx, "coloured", &G.nodes_field);
        if (G.show_nodes) {
            nk_layout_row_dynamic(ctx, row, 1);
            nk_property_float(ctx, "#point size", 1.f, &G.point_size, 12.f, 1.f, 0.1f);
        }
        {                                        /* integration points, next to the nodes */
            nk_layout_row_dynamic(ctx, row, 2);
            tip(ctx, "Integration points: real .dat values when a .dat field is selected,\nelse the nodal field interpolated there");
            if (nk_checkbox_label(ctx, "Gauss pts", &G.show_gp)) app_gauss_changed();
            tip(ctx, "Colour the points by the field");
            nk_checkbox_label(ctx, "coloured", &G.gp_colored);
            if (G.show_gp) {
                nk_layout_row_dynamic(ctx, row, 2);
                nk_property_float(ctx, "#size", 1.f, &G.gp_size, 30.f, 1.f, 0.1f);
                tip(ctx, "Draw the points through the faces (they sit inside the elements)");
                nk_checkbox_label(ctx, "x-ray", &G.gp_on_top);
            }
            if (app_field_is_vector()) {             /* arrows of DISP, FORC, FLUX, ... */
                nk_layout_row_dynamic(ctx, row, 2);
                tip(ctx, "The field as arrows at the nodes, the longest one 'vec %' of the model");
                if (nk_checkbox_label(ctx, "Vectors", &G.show_vec)) app_vectors_changed();
                tip(ctx, "Colour the arrows by the selected scalar (else white)");
                nk_checkbox_label(ctx, "coloured", &G.vec_colored);
            }
        }
        nk_tree_pop(ctx);
    }

}

/* ---- Groups: element type / material / group, deck and cgx sets */
static void section_groups(struct nk_context* ctx, float s, float row) {
    /* groups */
    if (nk_tree_state_push(ctx, NK_TREE_TAB, "Groups", (enum nk_collapse_states*)&G.tree[CV_TREE_GROUPS])) {
        for (int a = 0; a < CV_AXIS_N; a++) {
            cv_axis* ax = &G.groups.axis[a];
            if (!nk_tree_push_id(ctx, NK_TREE_NODE, cv_axis_name(a), a == 0 ? NK_MAXIMIZED : NK_MINIMIZED, a))
                continue;
            if (ax->n > 1) {
                nk_layout_row_dynamic(ctx, row, 2);
                if (nk_button_label(ctx, "all"))  { for (int i = 0; i < ax->n; i++) ax->on[i] = true;  app_groups_changed(); }
                if (nk_button_label(ctx, "none")) { for (int i = 0; i < ax->n; i++) ax->on[i] = false; app_groups_changed(); }
            }
            int shown = CV_MIN(ax->n, 500);            /* keep the list usable */
            for (int i = 0; i < shown; i++) {
                char lab[64];
                axis_label(lab, sizeof lab, a, ax->value[i], ax->count[i]);
                nk_layout_row_template_begin(ctx, row);
                nk_layout_row_template_push_static(ctx, row * 1.4f);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_end(ctx);
                /* colour swatch: a plain colour button; clicking it opens Nuklear's
                   picker in a popup. Picking also colours the faces by this axis. */
                float* rgb = G.axis_rgb[a] ? G.axis_rgb[a] + 3 * i : NULL;
                struct nk_rect sb = nk_widget_bounds(ctx);
                struct nk_color sw = rgb ? nk_rgb_f(rgb[0], rgb[1], rgb[2]) : nk_rgb(128, 128, 128);
                if (nk_button_color(ctx, sw) && rgb) {
                    bool same = g_pick_axis == a && g_pick_idx == i;
                    g_pick_axis = same ? -1 : a;
                    g_pick_idx = same ? -1 : i;
                }
                if (rgb && g_pick_axis == a && g_pick_idx == i) {
                    struct nk_rect clip = ctx->current->layout->clip;
                    float pw = 230 * s, ph = 170 * s + 3 * row + 6 * ctx->style.window.spacing.y;
                    struct nk_rect pr = nk_rect(sb.x + sb.w + 4 * s - clip.x, sb.y + sb.h - clip.y, pw, ph);
                    if (nk_popup_begin(ctx, NK_POPUP_STATIC, "pick", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, pr)) {
                        nk_layout_row_dynamic(ctx, 170 * s, 1);
                        struct nk_colorf cf = { rgb[0], rgb[1], rgb[2], 1 };
                        struct nk_colorf nf = nk_color_picker(ctx, cf, NK_RGB);
                        nk_layout_row_dynamic(ctx, row, 3);
                        nf.r = nk_propertyf(ctx, "#R", 0, nf.r, 1, 0.01f, 0.005f);
                        nf.g = nk_propertyf(ctx, "#G", 0, nf.g, 1, 0.01f, 0.005f);
                        nf.b = nk_propertyf(ctx, "#B", 0, nf.b, 1, 0.01f, 0.005f);
                        if (nf.r != rgb[0] || nf.g != rgb[1] || nf.b != rgb[2]) {
                            rgb[0] = nf.r; rgb[1] = nf.g; rgb[2] = nf.b;
                            if (G.faces_mode != FM_TYPE + a) app_set_faces_mode(FM_TYPE + a);
                            else app_group_colors_changed();
                        }
                        nk_layout_row_dynamic(ctx, row, 1);
                        if (nk_button_label(ctx, "done")) {
                            g_pick_axis = g_pick_idx = -1;
                            nk_popup_close(ctx);
                        }
                        nk_popup_end(ctx);
                    } else {
                        g_pick_axis = g_pick_idx = -1;
                    }
                }
                if (nk_checkbox_label(ctx, lab, &ax->on[i])) app_groups_changed();
            }
            if (ax->n > shown) {
                nk_layout_row_dynamic(ctx, row, 1);
                nk_label(ctx, "(list truncated)", NK_TEXT_LEFT);
            }
            nk_tree_pop(ctx);
        }
        if (geo_loaded()) panel_geo_sets(ctx, row);   /* cgx sets drive the mesh's too */
        else panel_deck_sets(ctx, s, row);
        nk_tree_pop(ctx);
    }

}

/* ---- Fields of the current step, and the .dat ones */
static void section_fields(struct nk_context* ctx, float s, float row) {
    /* fields of the current step */
    if (nk_tree_state_push(ctx, NK_TREE_TAB, "Fields", (enum nk_collapse_states*)&G.tree[CV_TREE_FIELDS])) {
        nk_layout_row_dynamic(ctx, row, 1);
        if (G.frd.n_steps == 0) {
            nk_label(ctx, "No results in this file.", NK_TEXT_LEFT);
        } else {
            const cv_step* st = &G.frd.steps[G.step];
            nk_bool em = G.elem_mode;
            tip(ctx, "One value per element (mean of its nodes) instead of a smooth nodal field");
            if (nk_checkbox_label(ctx, "Per element (flat)", &em)) app_set_elem_mode(em);
            {   /* components of vectors and tensors in a cylindrical system */
                static const char* cs[] = { "coordinates: global x y z", "cylindrical about X", "cylindrical about Y", "cylindrical about Z" };
                tip(ctx, "Vector and tensor components in a cylindrical system: r radial, t hoop, a axial (.frd fields; invariants stay)");
                int c = nk_combo(ctx, cs, 4, G.csys, (int)row, nk_vec2(240 * s, 4 * row + 20 * s));
                bool ch = c != G.csys;
                G.csys = c;
                if (G.csys) {
                    float st = G.diag * 0.01f + 1e-30f;
                    static const char* nm[3] = { "#x0", "#y0", "#z0" };
                    nk_layout_row_dynamic(ctx, row, 3);
                    for (int k = 0; k < 3; k++) {
                        tip(ctx, "A point on the axis");
                        float v = nk_propertyf(ctx, nm[k], -1e30f, G.csys_o[k], 1e30f, st, st * 0.1f);
                        if (v != G.csys_o[k]) { G.csys_o[k] = v; ch = true; }
                    }
                }
                if (ch) app_select(G.field_name, G.comp);
            }
            if (G.cmp_on) {
                char lab[96];
                snprintf(lab, sizeof lab, "minus %s", cv_basename(G.cmp_path));
                tip(ctx, "Show this field minus the same field of the comparison run (same step index)");
                if (nk_checkbox_label(ctx, lab, &G.diff_mode)) app_select(G.field_name, G.comp);
            }
            for (int f = 0; f < st->nfields; f++) {
                const cv_field_desc* d = &st->fields[f];
                bool active = G.field_src == 0 && strcmp(d->name, G.field_name) == 0;
                if (!nk_tree_push_id(ctx, NK_TREE_NODE, d->name, active ? NK_MAXIMIZED : NK_MINIMIZED, 100 + f))
                    continue;
                cv_scalar_opt opts[CV_MAX_OPTS];
                int n = app_field_options(d, opts, CV_MAX_OPTS);
                nk_layout_row_dynamic(ctx, row, 1);
                for (int i = 0; i < n; i++) {
                    bool sel = active && G.comp == opts[i].comp;
                    if (nk_option_label(ctx, opts[i].label, sel) && !sel) app_select_src(d->name, opts[i].comp, 0);
                }
                nk_tree_pop(ctx);
            }
        }
        /* integration-point fields from the .dat, at this increment */
        if (gp_loaded()) {
            const char* names[32];
            int nf = gp_fields(names, 32);
            if (nf == 0) {
                nk_layout_row_dynamic(ctx, row, 1);
                nk_label_colored(ctx, "(.dat: nothing at this increment)", NK_TEXT_LEFT, P.dim);
            }
            for (int f = 0; f < nf; f++) {
                cv_field_desc d;
                if (!gp_desc(names[f], &d)) continue;
                bool active = G.field_src == 1 && strcmp(names[f], G.field_name) == 0;
                char title[64];
                snprintf(title, sizeof title, "%s (.dat)", names[f]);
                if (!nk_tree_push_id(ctx, NK_TREE_NODE, title, active ? NK_MAXIMIZED : NK_MINIMIZED, 300 + f))
                    continue;
                cv_scalar_opt opts[CV_MAX_OPTS];
                int n = cv_field_options(&d, opts, CV_MAX_OPTS);
                nk_layout_row_dynamic(ctx, row, 1);
                for (int i = 0; i < n; i++) {
                    bool sel = active && G.comp == opts[i].comp;
                    if (nk_option_label(ctx, opts[i].label, sel) && !sel) app_select_src(names[f], opts[i].comp, 1);
                }
                nk_tree_pop(ctx);
            }
        }
        nk_tree_pop(ctx);
    }

}

/* ---- Export: pictures, animations, data and the view state ------------------- */
static void section_export(struct nk_context* ctx, float s, float row) {
    if (!nk_tree_state_push(ctx, NK_TREE_TAB, "Export", (enum nk_collapse_states*)&G.tree[CV_TREE_EXPORT])) return;
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "The 3D view with legend and axes as <model>_stepN.png beside the model, numbered, never overwritten");
    if (nk_button_label(ctx, "Image of the view (Ctrl+E)")) app_export_png();

    /* animation: what loops, in which form, how long */
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "One full swing of the deformation of this step (Animate)");
    if (nk_option_label(ctx, "deformation cycle", G.exp_kind == 0)) G.exp_kind = 0;
    tip(ctx, "Every increment once, each held as long as the play speed (fps in the time bar) shows it");
    if (nk_option_label(ctx, "every step", G.exp_kind == 1)) G.exp_kind = 1;
    if (nk_option_label(ctx, "MP4 video", G.exp_video)) G.exp_video = true;
    tip(ctx, "One PNG per frame; for 'every step' one per increment");
    if (nk_option_label(ctx, "PNG sequence", !G.exp_video)) G.exp_video = false;
    if (G.exp_kind == 0) {
        tip(ctx, "How often the swing repeats in the clip");
        nk_property_int(ctx, "#cycles", 1, &G.exp_cycles, 20, 1, 0.1f);
    } else {
        nk_spacing(ctx, 1);
    }
    tip(ctx, "Frames per second of the video (and per cycle of a PNG sequence)");
    nk_property_int(ctx, "#fps", 1, &G.exp_fps, 60, 1, 0.2f);
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "Keep the legend's range fixed while recording, so colours mean the same in every frame");
    nk_checkbox_label(ctx, "lock the colour range while recording", &G.exp_lock_range);
    {
        char lab[96];
        int fps = CV_MAX(G.exp_fps, 1);
        if (G.exp_kind == 0) {
            int per = CV_MAX(2, (int)(fps * CV_MAX(G.anim_period, 0.5f))) * CV_MAX(G.exp_cycles, 1);
            snprintf(lab, sizeof lab, "Export animation  (%d frames, %.1f s)", per, (float)per / fps);
        } else {
            int hold = G.exp_video ? CV_MAX(1, (int)((float)fps / CV_MAX(G.fps, 0.5f) + 0.5f)) : 1;
            snprintf(lab, sizeof lab, "Export animation  (%d steps%s)", G.frd.n_steps,
                     G.exp_video ? "" : ", one PNG each");
            if (G.exp_video) snprintf(lab, sizeof lab, "Export animation  (%d steps, %.1f s)", G.frd.n_steps, (float)G.frd.n_steps * hold / fps);
        }
        if (G.seq_left > 0) nk_label_colored(ctx, "exporting... (stop: status bar)", NK_TEXT_CENTERED, P.warn);
        else if (nk_button_label(ctx, lab)) app_export_animation();
    }

    /* data and the view state */
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "id, x, y, z, displacement and the field per node, visible elements only");
    if (nk_button_label(ctx, "CSV nodes")) app_export_data(false);
    tip(ctx, "Legacy VTK unstructured grid with the field, for ParaView");
    if (nk_button_label(ctx, "VTK mesh")) app_export_data(true);
    {
        char vp[1100];
        snprintf(vp, sizeof vp, "%.*s_view.ini", (int)(strrchr(G.path, '.') && strrchr(G.path, '.') > strrchr(G.path, cv_path_sep()) ? strrchr(G.path, '.') - G.path : (int)strlen(G.path)), G.path);
        tip(ctx, "Camera, step, field, layers, clip and crop to <model>_view.ini, to reproduce this picture later");
        if (nk_button_label(ctx, "save view state")) { snprintf(G.note, sizeof G.note, app_view_save(vp) ? "saved %s" : "could not write %s", vp); G.note_t = cv_now(); }
        if (nk_button_label(ctx, "load view state")) { snprintf(G.note, sizeof G.note, app_view_load(vp) ? "loaded %s" : "no %s", vp); G.note_t = cv_now(); }
    }
    nk_tree_state_pop(ctx);
}

/* a sub-section of a panel section, open or closed as it was left (saved in the settings) */
static bool sub_push(struct nk_context* ctx, const char* title, int t) {
    return nk_tree_state_push(ctx, NK_TREE_NODE, title, (enum nk_collapse_states*)&G.tree[t]);
}

static void cmap_combo(struct nk_context* ctx, float s, float row);
static void legend_controls(struct nk_context* ctx, float s, float row);

/* replicate: rows of copies of a periodic model along X / Y / Z */
static void view_replicate(struct nk_context* ctx, float s, float row) {
    static const char* ax[3] = { "Along X", "Along Y", "Along Z" };
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "With Deform on, each copy sits one deformed cell edge from the next, so a periodic\n"
             "cell under shear or stretch stays joined (from the mean displacement of opposite faces).\n"
             "Off: the copies keep the undeformed spacing");
    nk_checkbox_label(ctx, "follow deformation", &G.rep_follow);
    for (int k = 0; k < 3; k++) {
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 80 * s);
        if (G.rep[k]) { nk_layout_row_template_push_dynamic(ctx); nk_layout_row_template_push_dynamic(ctx); }
        nk_layout_row_template_end(ctx);
        tip(ctx, "Draw the model again in a row of copies along this axis\n(a periodic model: several cells side by side). Axes combine into a grid");
        if (nk_checkbox_label(ctx, ax[k], &G.rep[k])) app_sym_changed();
        if (!G.rep[k]) continue;
        int n = G.rep_n[k];
        tip(ctx, "Copies along this axis, the model included");
        nk_property_int(ctx, "#n", 2, &n, 100, 1, 0.2f);
        if (n != G.rep_n[k]) { G.rep_n[k] = n; app_sym_changed(); }
        char pitch[32], tp[128];
        fmt_num(pitch, sizeof pitch, app_rep_pitch(k));
        snprintf(tp, sizeof tp, "Space between neighbouring copies (0: they touch).\nPitch now %s", pitch);
        tip(ctx, tp);
        float g = G.rep_gap[k], step = CV_MAX(G.diag * 0.01f, 1e-6f);
        nk_property_float(ctx, "#gap", -1e9f, &g, 1e9f, step, step * 0.1f);
        if (g != G.rep_gap[k]) { G.rep_gap[k] = g; app_sym_changed(); }
    }
}

/* ---- View: camera, colours, display, symmetry, cuts, the file */
static void section_view(struct nk_context* ctx, float s, float row) {
    if (!nk_tree_state_push(ctx, NK_TREE_TAB, "View", (enum nk_collapse_states*)&G.tree[CV_TREE_VIEW])) return;
    static const char* views[] = { "Iso", "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label(ctx, "Look from", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, row, 7);
    for (int v = 0; v < 7; v++)
        if (nk_button_label(ctx, views[v])) app_view(v);
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "Parallel projection: no perspective, distances compare directly");
    nk_checkbox_label(ctx, "orthographic", &G.cam.ortho);
    if (nk_button_label(ctx, "Fit (F)")) app_fit();

    if (sub_push(ctx, "Camera", CV_TREE_CAMERA)) {
        nk_layout_row_dynamic(ctx, row, 3);
        nk_label(ctx, "Up axis", NK_TEXT_LEFT);
        {
            bool z0 = G.up_z;
            tip(ctx, "Dragging turns the model about this world axis, which stays vertical on screen");
            if (nk_option_label(ctx, "Y", !G.up_z)) G.up_z = false;
            tip(ctx, "Dragging turns the model about this world axis, which stays vertical on screen");
            if (nk_option_label(ctx, "Z", G.up_z)) G.up_z = true;
            if (G.up_z != z0) app_view(CV_VIEW_ISO);
        }
        nk_label(ctx, "Rotation", NK_TEXT_LEFT);
        tip(ctx, "Turntable: the up axis stays vertical on screen, like FreeCAD's turntable");
        if (nk_option_label(ctx, "turntable", !G.orbit_free)) app_set_orbit_free(false);
        tip(ctx, "Free: drag turns the model any way round (trackball), the up axis is not kept.\nA view preset (1-6, Iso) sets it upright again");
        if (nk_option_label(ctx, "free", G.orbit_free)) app_set_orbit_free(true);
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "Drag rotates about the point of the model you grab;\noff, or when grabbing empty space, about the view centre");
        nk_checkbox_label(ctx, "Rotate about the cursor", &G.orbit_cursor);
        tip(ctx, "The wheel zooms toward the point under the cursor; off, toward the view centre");
        nk_checkbox_label(ctx, "Zoom toward the cursor", &G.zoom_cursor);
        tip(ctx, "Wheel up zooms out instead of in");
        nk_checkbox_label(ctx, "Invert wheel zoom", &G.wheel_invert);
        tip(ctx, "While navigating, mark the point the view turns or zooms about:\nan axis cross at the rotation centre, a ring at the zoom point");
        nk_checkbox_label(ctx, "Show rotation centre", &G.show_pivot);
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "The view before the last change (Ctrl+Z)");
        if (nk_button_label(ctx, "< view back")) app_view_undo(-1);
        tip(ctx, "Forward again (Ctrl+Y)");
        if (nk_button_label(ctx, "view forward >")) app_view_undo(+1);
        nk_layout_row_dynamic(ctx, row, 1);
        nk_bool fl = G.flight;
        tip(ctx, "Walk through the model: WASD, E/Space up, Q down, Shift fast, drag to look");
        if (nk_checkbox_label(ctx, "Free flight (G)", &fl)) app_set_flight(fl);
        if (G.flight) {
            nk_property_float(ctx, "#speed", 0.005f, &G.fly_speed, 10.f, 0.05f, 0.005f);
            nk_label_colored(ctx, "WASD move  E/Space up  Q down", NK_TEXT_LEFT, P.dim);
            nk_label_colored(ctx, "Shift fast  drag look  wheel speed", NK_TEXT_LEFT, P.dim);
            nk_label_colored(ctx, "Esc or G: back to orbit", NK_TEXT_LEFT, P.dim);
        }
        nk_tree_state_pop(ctx);
    }

    if (sub_push(ctx, "Colours & legend", CV_TREE_COLOURS)) {
        nk_layout_row_dynamic(ctx, row, 2);
        cmap_combo(ctx, s, row);
        {
            nk_bool leg = !G.hide_legend;
            tip(ctx, "The colour legend / group key in the view (also in exports)");
            if (nk_checkbox_label(ctx, "show legend", &leg)) G.hide_legend = !leg;
        }
        legend_controls(ctx, s, row);
        nk_tree_state_pop(ctx);
    }

    if (sub_push(ctx, "Display", CV_TREE_DISPLAY)) {
        nk_layout_row_dynamic(ctx, row, 2);
        {
            nk_bool ax = !G.hide_axes;
            tip(ctx, "The axes gizmo in the corner (click its tips to look from there)");
            if (nk_checkbox_label(ctx, "Axes gizmo", &ax)) G.hide_axes = !ax;
        }
        tip(ctx, "Light the faces. Off: exact colours, as in the legend");
        nk_checkbox_label(ctx, "Shading", &G.shading);

        /* background: presets and a picker; exports use it too */
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 84 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, row * 1.4f);
        nk_layout_row_template_end(ctx);
        nk_label(ctx, "Background", NK_TEXT_LEFT);
        if (nk_button_label(ctx, "white")) { G.bg[0] = G.bg[1] = G.bg[2] = 1.f; }
        if (nk_button_label(ctx, "grey"))  { G.bg[0] = 0.33f; G.bg[1] = 0.32f; G.bg[2] = 0.31f; }
        if (nk_button_label(ctx, "black")) { G.bg[0] = G.bg[1] = G.bg[2] = 0.f; }
        static bool bg_pick;
        struct nk_rect bb = nk_widget_bounds(ctx);
        if (nk_button_color(ctx, nk_rgb_f(G.bg[0], G.bg[1], G.bg[2]))) bg_pick = !bg_pick;
        if (bg_pick) {
            struct nk_rect clip = ctx->current->layout->clip;
            float pw = 230 * s, ph = 170 * s + 3 * row + 6 * ctx->style.window.spacing.y;
            struct nk_rect pr = nk_rect(bb.x + bb.w - pw - clip.x, bb.y + bb.h - clip.y, pw, ph);
            if (nk_popup_begin(ctx, NK_POPUP_STATIC, "bgpick", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, pr)) {
                nk_layout_row_dynamic(ctx, 170 * s, 1);
                struct nk_colorf cf = nk_color_picker(ctx, (struct nk_colorf){ G.bg[0], G.bg[1], G.bg[2], 1 }, NK_RGB);
                G.bg[0] = cf.r; G.bg[1] = cf.g; G.bg[2] = cf.b;
                nk_layout_row_dynamic(ctx, row, 3);
                G.bg[0] = nk_propertyf(ctx, "#R", 0, G.bg[0], 1, 0.01f, 0.005f);
                G.bg[1] = nk_propertyf(ctx, "#G", 0, G.bg[1], 1, 0.01f, 0.005f);
                G.bg[2] = nk_propertyf(ctx, "#B", 0, G.bg[2], 1, 0.01f, 0.005f);
                nk_layout_row_dynamic(ctx, row, 1);
                if (nk_button_label(ctx, "done")) { bg_pick = false; nk_popup_close(ctx); }
                nk_popup_end(ctx);
            } else {
                bg_pick = false;
            }
        }
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 84 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        nk_label(ctx, "UI theme", NK_TEXT_LEFT);
        {
            const char* names[NTHEMES];
            for (int i = 0; i < NTHEMES; i++) names[i] = themes[i].name;
            tip(ctx, "Colours of the panels and the legend (Nuklear demo themes)");
            int t = nk_combo(ctx, names, NTHEMES, U.theme, (int)row, nk_vec2(200 * s, 6 * row + 20 * s));
            if (t != U.theme) { U.theme = t; U.restyle = true; }
        }
        nk_tree_state_pop(ctx);
    }

    symbol_sizes(ctx, s, row);
    if (sub_push(ctx, "Mirror", CV_TREE_MIRROR)) {
        /* the model mirrored across planes normal to X / Y / Z */
        {
            static const char* ax[3] = { "Mirror X", "Mirror Y", "Mirror Z" };
            const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
            for (int k = 0; k < 3; k++) {
                nk_layout_row_dynamic(ctx, row, G.sym[k] ? 2 : 1);
                tip(ctx, "Draw the model again reflected across a plane normal to this axis\n(a half or quarter model shown whole)");
                bool was = G.sym[k];
                if (nk_checkbox_label(ctx, ax[k], &G.sym[k])) app_sym_toggled(k);
                if (!was) continue;
                char a0[32], a1[32], it[CV_SYM_N][48];
                fmt_num(a0, sizeof a0, lo[k]);
                fmt_num(a1, sizeof a1, hi[k]);
                snprintf(it[CV_SYM_ZERO], sizeof it[0], "at %c = 0", 'x' + k);
                snprintf(it[CV_SYM_MIN], sizeof it[0], "at min %s", a0);
                snprintf(it[CV_SYM_MAX], sizeof it[0], "at max %s", a1);
                const char* items[CV_SYM_N] = { it[0], it[1], it[2] };
                int s2 = nk_combo(ctx, items, CV_SYM_N, G.sym_at[k], (int)row, nk_vec2(160 * s, 4 * row + 20 * s));
                if (s2 != G.sym_at[k]) { G.sym_at[k] = s2; app_sym_changed(); }
            }
        }

        nk_tree_state_pop(ctx);
    }

    if (sub_push(ctx, "Replicate", CV_TREE_REPLICATE)) {
        view_replicate(ctx, s, row);
        nk_tree_state_pop(ctx);
    }

    if (sub_push(ctx, "Clip & crop", CV_TREE_CLIP)) {
        /* clip plane: cut at draw time along an axis; hollow inside (no cap) */
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Cut the drawing at a plane (the inside shows hollow; the crop box below removes whole elements)");
        nk_checkbox_label(ctx, "Clip plane", &G.clip_on);
        if (G.clip_on) {
            static const char* axes[] = { "normal X", "normal Y", "normal Z" };
            G.clip_axis = nk_combo(ctx, axes, 3, G.clip_axis, (int)row, nk_vec2(120 * s, 3 * row + 20 * s));
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 50 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);
            tip(ctx, "Keep the other side instead");
            nk_checkbox_label(ctx, "flip", &G.clip_flip);
            ui_slider_float(ctx, 0.f, &G.clip_pos, 1.f, 0.002f);
        }
        /* crop box: elements whose centre is outside are removed, so the cut
           shows real element faces, not a hollow shell */
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Remove elements whose centre lies outside the box, so the cut shows real element faces");
        if (nk_checkbox_label(ctx, "Crop box", &G.crop_on)) app_groups_changed();
        if (G.crop_on && nk_button_label(ctx, "reset")) {
            for (int k = 0; k < 3; k++) { G.crop_lo[k] = 0.f; G.crop_hi[k] = 1.f; }
            app_groups_changed();
        }
        if (G.crop_on) {
            /* one row per axis: label, min box, max box (world units, drag or arrows);
               the fractions behind them stay clamped to the model box */
            const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
            static const char* ax[3] = { "X", "Y", "Z" };
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 16 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);
            for (int k = 0; k < 3; k++) {
                float ext = hi[k] - lo[k], step = CV_MAX(ext * 0.01f, 1e-6f);
                float a = lo[k] + ext * G.crop_lo[k], b = lo[k] + ext * G.crop_hi[k], a0 = a, b0 = b;
                nk_label(ctx, ax[k], NK_TEXT_LEFT);
                tip(ctx, "Lower cut along this axis: drag, arrows, or click to type");
                a = nk_propertyf(ctx, "#", lo[k], a, hi[k], step, step * 0.1f);
                tip(ctx, "Upper cut along this axis");
                b = nk_propertyf(ctx, "#", lo[k], b, hi[k], step, step * 0.1f);
                if (a != a0 || b != b0) {
                    if (a > b) { if (a != a0) a = b; else b = a; }         /* keep min <= max */
                    G.crop_lo[k] = ext > 0 ? (a - lo[k]) / ext : 0.f;
                    G.crop_hi[k] = ext > 0 ? (b - lo[k]) / ext : 1.f;
                    app_groups_changed();
                }
            }
        }
        nk_tree_state_pop(ctx);
    }

    if (sub_push(ctx, "File", CV_TREE_FILE)) {
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Open a second .frd of the same mesh and show fields as the difference (Fields > minus ...)");
        if (nk_button_label(ctx, G.cmp_on ? "Compare: off" : "Compare with...")) {
            if (G.cmp_on) { app_compare_close(); app_select(G.field_name, G.comp); }
            else { G.dlg_for_compare = true; app_open_dialog(); }
        }
        nk_spacing(ctx, 1);
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Reload when the file changes on disk (a running solver): camera, step and field stay");
        nk_checkbox_label(ctx, "Watch file", &G.watch);
        tip(ctx, "Reopen the file now, keeping camera, step and field");
        if (nk_button_label(ctx, "Reload")) app_reload();

        nk_tree_state_pop(ctx);
    }
    nk_tree_state_pop(ctx);
}

/* The recent files as one left-aligned button each (full path in the tooltip),
   in the empty view: no file open yet, so they are what you most likely want. */
static void recent_buttons(struct nk_context* ctx, float row) {
    const char* recent[CV_CFG_RECENT];
    int nr = settings_recent(recent, CV_CFG_RECENT);
    if (!nr || app_busy()) return;
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label_colored(ctx, "Recent files", NK_TEXT_LEFT, P.dim);
    nk_style_push_flags(ctx, &ctx->style.button.text_alignment, NK_TEXT_LEFT);
    for (int i = 0; i < nr; i++) {
        tip(ctx, recent[i]);
        if (nk_button_label(ctx, cv_basename(recent[i]))) app_open(recent[i]);
    }
    nk_style_pop_flags(ctx);
}

static void panel_scene(struct nk_context* ctx, float s, float row) {
    /* open */
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 76 * s);
    nk_layout_row_template_end(ctx);
    if (g_focus_open) { nk_edit_focus(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER); g_focus_open = false; }
    tip(ctx, "Path of a .frd / .inp / .fbd / .dat, then Enter  (Ctrl+L)");
    nk_flags ev = nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, G.open_buf, &G.open_len,
                                 (int)sizeof G.open_buf - 1, nk_filter_default);
    G.open_buf[G.open_len] = 0;
    if (ev & NK_EDIT_COMMITED) app_open(G.open_buf);          /* typed path + Enter */
    if (nk_button_label(ctx, G.dlg_running ? "..." : "Open...")) app_open_dialog();
    if (G.loaded) {                          /* recent files: a drop-down under the path box (the empty view lists them) */
        const char* recent[CV_CFG_RECENT];
        int nr = settings_recent(recent, CV_CFG_RECENT);
        if (nr) {
            nk_layout_row_dynamic(ctx, row, 1);
            tip(ctx, "Files opened before");
            if (nk_combo_begin_label(ctx, "recent files", nk_vec2(300 * s, (nr + 1) * (row + ctx->style.window.spacing.y) + 8 * s))) {
                nk_layout_row_dynamic(ctx, row, 1);
                for (int i = 0; i < nr; i++)
                    if (nk_combo_item_label(ctx, cv_basename(recent[i]), NK_TEXT_LEFT)) app_open(recent[i]);
                nk_combo_end(ctx);
            }
        }
    }

    if (!G.loaded) {
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label(ctx, app_busy() ? "Loading..." : "Open... (Ctrl+O), drop a .frd/.inp/.fbd", NK_TEXT_LEFT);
        if (!app_busy()) nk_label(ctx, "on the window, or type a path + Enter.", NK_TEXT_LEFT);
        return;
    }

    /* a cgx script that was not evaluated: say what it is, offer to run it */
    if (geo_loaded() && geo_get()->needs_cgx && !geo_evaluated()) {
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label(ctx, "This .fbd is a cgx script.", NK_TEXT_LEFT);
        if (nk_button_label(ctx, "Evaluate with cgx")) app_eval_cgx();
        nk_label_colored(ctx, "Runs its commands in a temporary copy", NK_TEXT_LEFT, P.warn);
        nk_label_colored(ctx, "of its folder. Only for scripts you trust.", NK_TEXT_LEFT, P.warn);
    }

    section_layers(ctx, s, row);
    section_groups(ctx, s, row);
    section_fields(ctx, s, row);
    section_view(ctx, s, row);
    section_export(ctx, s, row);

    nk_layout_row_dynamic(ctx, row, 1);
    nk_label(ctx, "", NK_TEXT_LEFT);
    nk_label_colored(ctx, "drag: orbit   shift/right/middle: pan", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "hold X/Y/Z + drag: about that axis", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "ctrl drag: box zoom   ctrl right: zoom", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "alt drag: roll   middle click, C: centre", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "N: normal to face   ctrl Z/Y: view back/fwd", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "ctrl arrows: turn 15 (shift 90)   alt: roll", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "wheel: zoom   click: probe   F: fit", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "space: play   arrows: step", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "+/-: deform scale   R: reset view   1-6: views", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "G: free flight   H: view only", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "ctrl +/-/0: UI size", NK_TEXT_LEFT, P.dim);
}

/* a strip of colour map cm across r */
static void cmap_strip(struct nk_command_buffer* cv, struct nk_rect r, int cm) {
    enum { NS = 24 };
    for (int i = 0; i < NS; i++) {
        float c[3];
        cv_colormap_rgb(cm, ((float)i + 0.5f) / NS, c);
        nk_fill_rect(cv, nk_rect(r.x + r.w * i / NS, r.y, r.w / NS + 1, r.h), 0, nk_rgb_f(c[0], c[1], c[2]));
    }
}

/* The colour map picker: each map shows as a strip beside its name, the closed
   box too -- a name alone ("Fast") says little about what the colours are. */
static void cmap_combo(struct nk_context* ctx, float s, float row) {
    struct nk_command_buffer* wc = nk_window_get_canvas(ctx);
    struct nk_rect b = nk_widget_bounds(ctx);
    const struct nk_user_font* f = ctx->style.font;
    float sw = 40 * s, spw = f->width(f->userdata, f->height, " ", 1);
    char lab[64];
    int ns = spw > 0 ? (int)ceilf((sw + 6 * s) / spw) : 0;
    snprintf(lab, sizeof lab, "%*s%s", CV_MIN(ns, 30), "", cv_cmap_names[G.cmap]);
    tip(ctx, "Colour map for the field");
    bool open = nk_combo_begin_label(ctx, lab, nk_vec2(230 * s, CV_CMAP_N * (row + ctx->style.window.spacing.y) + 12 * s));
    if (open) {
        for (int cm = 0; cm < CV_CMAP_N; cm++) {
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 48 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);
            struct nk_rect r;
            if (nk_widget(&r, ctx) != NK_WIDGET_INVALID)
                cmap_strip(nk_window_get_canvas(ctx), nk_rect(r.x, r.y + 3, r.w, r.h - 6), cm);
            if (nk_combo_item_label(ctx, cv_cmap_names[cm], NK_TEXT_LEFT) && cm != G.cmap) app_colormap(cm);
        }
        nk_combo_end(ctx);
    }
    /* over the closed box, after it is drawn (and after an open list, which it does not overlap) */
    float pad = ctx->style.combo.content_padding.x + 2 * s, h = b.h * 0.5f;
    cmap_strip(wc, nk_rect(b.x + pad, b.y + (b.h - h) * 0.5f, sw, h), G.cmap);
}

/* Two lines: what the shape does (deform, animate) above, how the field
   looks (colormap, bands, range) below. Camera controls live in the View tree. */
static void panel_toolbar(struct nk_context* ctx, float s, float row) {
    /* line 1: deformation */
    nk_layout_row_begin(ctx, NK_STATIC, row, 8);
    nk_layout_row_push(ctx, 80 * s);
    tip(ctx, "Draw the shape displaced by DISP x scale");
    nk_checkbox_label(ctx, "Deform", &G.deform);
    nk_layout_row_push(ctx, 140 * s);
    float before = G.deform_scale;
    tip(ctx, "Displacement multiplier. Editing it switches auto off");
    nk_property_float(ctx, "#scale", 0.f, &G.deform_scale, 1e9f, CV_MAX(G.deform_scale * 0.1f, 0.01f),
                      CV_MAX(G.deform_scale * 0.01f, 0.001f));
    if (G.deform_scale != before) G.deform_auto = false;
    nk_layout_row_push(ctx, 50 * s);
    tip(ctx, "Scale so the largest displacement is ~10% of the model; never below 1");
    if (nk_button_label(ctx, "auto")) { G.deform_auto = true; G.deform_scale = G.auto_scale; }
    nk_layout_row_push(ctx, 24 * s);
    nk_spacing(ctx, 1);
    nk_layout_row_push(ctx, 90 * s);
    tip(ctx, "Swing the deformation of this increment over time");
    if (nk_checkbox_label(ctx, "Animate", &G.anim_on) && G.anim_on) G.deform = true;
    static const char* anim_names[2] = { "0..1", "-1..1" };
    nk_layout_row_push(ctx, 76 * s);
    tip(ctx, G.harmonic ? "Steady-state response: the shape turns through its phase (DISP cos wt - DISPI sin wt)"
                        : "0..1: grow and relax.  -1..1: swing both ways (mode shapes)");
    if (G.harmonic) { nk_label_colored(ctx, "phase", NK_TEXT_CENTERED, P.accent_text); }
    else G.anim_mode = nk_combo(ctx, anim_names, 2, G.anim_mode, (int)row, nk_vec2(90 * s, 2 * row + 20 * s));
    nk_layout_row_push(ctx, 120 * s);
    tip(ctx, "Seconds per animation cycle");
    nk_property_float(ctx, "#sec", 0.2f, &G.anim_period, 20.f, 0.25f, 0.02f);
    nk_layout_row_push(ctx, 50 * s);
    if (nk_button_label(ctx, "Fit")) app_fit();
    nk_layout_row_end(ctx);

    /* line 2: colours */
    nk_layout_row_begin(ctx, NK_STATIC, row, 8);
    nk_layout_row_push(ctx, 150 * s);
    cmap_combo(ctx, s, row);
    /* the combo shows the usual counts; any other number (legend settings) shows as "N bands" */
    static const int band_vals[] = { 0, 6, 12, 24 };
    char custom[24];
    snprintf(custom, sizeof custom, "%d bands", G.bands);
    const char* band_names[] = { "smooth", "6 bands", "12 bands", "24 bands", custom };
    int bi = 4;
    for (int i = 0; i < 4; i++) if (band_vals[i] == G.bands) bi = i;
    nk_layout_row_push(ctx, 100 * s);
    tip(ctx, "Contour bands: discrete colour steps, or a smooth gradient (any count: legend settings)");
    int bj = nk_combo(ctx, band_names, bi == 4 ? 5 : 4, bi, (int)row, nk_vec2(110 * s, 180 * s));
    if (bj != bi && bj < 4) G.bands = band_vals[bj];
    nk_layout_row_push(ctx, 24 * s);
    nk_spacing(ctx, 1);
    if (G.range_lock) {
        nk_layout_row_push(ctx, 120 * s);
        char a[32], b[32], lab[80];
        legend_num(a, sizeof a, G.rmin); legend_num(b, sizeof b, G.rmax);
        snprintf(lab, sizeof lab, "locked %s .. %s", a, b);
        nk_label_colored(ctx, lab, NK_TEXT_LEFT, P.warn);
    }
    nk_layout_row_end(ctx);
}

static void panel_timebar(struct nk_context* ctx, float s, float row, float width) {
    int n = G.frd.n_steps;
    char info[160];
    if (n == 0) snprintf(info, sizeof info, "no steps");
    else {
        const cv_step* st = &G.frd.steps[G.step];
        snprintf(info, sizeof info, "%d/%d  step %d inc %d  t=%g", G.step + 1, n, st->step, st->inc, st->time);
    }
    float fixed = (32 + 84 + 32 + 110 + 260) * s + 8 * ctx->style.window.spacing.x;
    nk_layout_row_begin(ctx, NK_STATIC, row, 6);
    nk_layout_row_push(ctx, 32 * s);
    if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_LEFT)) app_set_step(G.step - 1);
    nk_layout_row_push(ctx, 84 * s);
    tip(ctx, "Step through the increments (Space)");
    if (nk_button_symbol_label(ctx, G.playing ? NK_SYMBOL_RECT_SOLID : NK_SYMBOL_TRIANGLE_RIGHT,
                               G.playing ? "pause" : "play", NK_TEXT_RIGHT)) { G.playing = !G.playing; G.last_tick = 0; }
    nk_layout_row_push(ctx, 32 * s);
    if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_RIGHT)) app_set_step(G.step + 1);
    nk_layout_row_push(ctx, CV_MAX(width - fixed, 60 * s));
    struct nk_rect sb = nk_widget_bounds(ctx);
    int st = G.step;
    if (n > 1 && nk_input_is_mouse_hovering_rect(&ctx->input, sb) && ctx->input.mouse.scroll_delta.y != 0) {
        app_set_step(G.step + (ctx->input.mouse.scroll_delta.y > 0 ? 1 : -1));    /* wheel steps increments */
        ctx->input.mouse.scroll_delta.y = 0;
        st = G.step;
    }
    if (n > 1 && ui_slider_int(ctx, 0, &st, n - 1, 1) && st != G.step) app_set_step(st);
    if (n <= 1) nk_spacing(ctx, 1);
    if (n > 1) {
        /* Ticks under the slider: a short one per increment when they fit, a tall
           bright one where a new *STEP begins, labelled with its step number. */
        struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
        const struct nk_user_font* font = ctx->style.font;
        float cur = ctx->style.slider.cursor_size.x;
        float x0 = sb.x + ctx->style.slider.padding.x + cur * 0.5f;
        float span = sb.w - 2 * ctx->style.slider.padding.x - cur;
        float y1 = sb.y + sb.h, dx = span / (float)(n - 1);
        float last_label = -1e9f;
        for (int i = 0; i < n; i++) {
            float x = x0 + span * (float)i / (float)(n - 1);
            bool major = i == 0 || G.frd.steps[i].step != G.frd.steps[i - 1].step;
            if (major) {
                nk_stroke_line(cv, x, y1 - sb.h * 0.45f, x, y1, 2.f * s, P.accent);
                char lab[16];
                snprintf(lab, sizeof lab, "%d", G.frd.steps[i].step);
                float tw = font->width(font->userdata, font->height, lab, (int)strlen(lab));
                if (x + 3 * s - last_label > tw + 6 * s && x + 3 * s + tw < sb.x + sb.w) {
                    nk_draw_text(cv, nk_rect(x + 3 * s, y1 - font->height, tw + 2, font->height), lab,
                                 (int)strlen(lab), font, nk_rgba(0, 0, 0, 0), P.accent_text);
                    last_label = x + 3 * s;
                }
            } else if (dx >= 4 * s) {
                nk_stroke_line(cv, x, y1 - sb.h * 0.22f, x, y1, 1.f, mix(P.dim, P.plot_bg, 0.25f));
            }
        }
    }
    nk_layout_row_push(ctx, 110 * s);
    tip(ctx, "Increments per second while playing");
    nk_property_float(ctx, "#fps", 0.5f, &G.fps, 60.f, 1.f, 0.1f);
    nk_layout_row_push(ctx, 260 * s);
    nk_label(ctx, info, NK_TEXT_LEFT);
    nk_layout_row_end(ctx);
}

static void panel_group_legend(struct nk_context* ctx, float s, float row) {
    int a = G.faces_mode - FM_TYPE;
    const cv_axis* ax = &G.groups.axis[a];
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label(ctx, cv_axis_name(a), NK_TEXT_LEFT);
    for (int i = 0; i < ax->n && i < 60; i++) {
        if (!ax->on[i]) continue;
        const float* c = G.axis_rgb[a] + 3 * i;
        struct nk_rect r;
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, row);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        if (nk_widget(&r, ctx) != NK_WIDGET_INVALID)
            nk_fill_rect(nk_window_get_canvas(ctx), nk_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4), 0,
                         nk_rgb_f(c[0], c[1], c[2]));
        char lab[64];
        if (a == CV_AXIS_TYPE) snprintf(lab, sizeof lab, "%s", cv_frd_type_name((int)ax->value[i]));
        else snprintf(lab, sizeof lab, "%s %u", a == CV_AXIS_MAT ? "Material" : "Group", ax->value[i]);
        nk_label(ctx, lab, NK_TEXT_LEFT);
    }
}

static void panel_legend(struct nk_context* ctx, float s, float row) {
    nk_layout_row_dynamic(ctx, row, 1);
    {
        char a[32], b[32], tt[200];
        legend_num(a, sizeof a, G.data_min); legend_num(b, sizeof b, G.data_max);
        snprintf(tt, sizeof tt, "data %s .. %s%s%zu without data.  Right-click: legend settings",
                 a, b, G.nan_count ? ",  " : ",  ", G.nan_count);
        tip(ctx, tt);
    }
    nk_label(ctx, G.field_label, NK_TEXT_LEFT);
    struct nk_rect area;
    nk_layout_row_dynamic(ctx, nk_window_get_content_region(ctx).h - row - 8 * s, 1);
    if (nk_widget(&area, ctx) == NK_WIDGET_INVALID) return;
    struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
    const struct nk_user_font* font = ctx->style.font;

    if (!(G.rmax > G.rmin)) {                     /* constant field: one value, not 13 equal labels */
        char txt[48], num[32];
        legend_num(num, sizeof num, G.rmin);
        snprintf(txt, sizeof txt, "uniform  %s", num);
        nk_draw_text(cv, nk_rect(area.x, area.y, area.w, font->height), txt, (int)strlen(txt), font,
                     nk_rgba(0, 0, 0, 0), P.text);
        return;
    }

    float bar_w = 22 * s, top = area.y + row * 0.5f, h = area.h - row;
    if (G.range_lock) {                           /* swatches for the out-of-range colours, above and below the bar */
        float sw = row * 0.8f, gap = 4 * s;
        h -= 2 * (sw + gap);
        if (h < 20) return;
        const float yy[2] = { top, top + h + 2 * gap + sw };
        const float gv[2] = { CV_OOR_ABOVE, CV_OOR_BELOW };
        const char* lab[2] = { "above", "below" };
        for (int k = 0; k < 2; k++) {
            int g = (int)(gv[k] * 255);
            nk_fill_rect(cv, nk_rect(area.x, yy[k], bar_w, sw), 0, nk_rgb(g, g, g));
            nk_stroke_rect(cv, nk_rect(area.x, yy[k], bar_w, sw), 0, 1, P.frame);
            nk_draw_text(cv, nk_rect(area.x + bar_w + 7 * s, yy[k] + (sw - font->height) * 0.5f, area.w - bar_w - 7 * s, font->height),
                         lab[k], (int)strlen(lab[k]), font, nk_rgba(0, 0, 0, 0), P.dim);
        }
        top += sw + gap;
    }
    if (h < 20) return;
    int nb = G.bands > 0 ? G.bands : 64;
    for (int i = 0; i < nb; i++) {
        float t = G.bands > 0 ? cv_band_center((float)i / (float)nb, G.bands) : (float)i / (float)(nb - 1);
        float c[3];
        app_cmap_rgb(t, c);
        float y1 = top + h * (1.f - (float)(i + 1) / (float)nb);
        float y0 = top + h * (1.f - (float)i / (float)nb);
        nk_fill_rect(cv, nk_rect(area.x, y1, bar_w, y0 - y1 + 1), 0,
                     nk_rgb((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255)));
    }
    nk_stroke_rect(cv, nk_rect(area.x, top, bar_w, h), 0, 1, P.frame);

    /* labels on band boundaries, every k-th so they never overlap */
    int nlab = G.bands > 0 ? G.bands : 8;
    int every = 1;
    while ((h / (float)nlab) * every < font->height * 1.3f) every++;
    for (int i = 0; i <= nlab; i += every) {
        float t = (float)i / (float)nlab;
        float v = G.rmin + (G.rmax - G.rmin) * t;
        char txt[32];
        legend_num(txt, sizeof txt, v);
        float y = top + h * (1.f - t) - font->height * 0.5f;
        nk_stroke_line(cv, area.x + bar_w, y + font->height * 0.5f, area.x + bar_w + 4 * s,
                       y + font->height * 0.5f, 1, P.tick);
        nk_draw_text(cv, nk_rect(area.x + bar_w + 7 * s, y, area.w - bar_w - 7 * s, font->height),
                     txt, (int)strlen(txt), font, nk_rgba(0, 0, 0, 0), P.text);
    }
}

static void panel_status(struct nk_context* ctx, float s, float row, float width) {
    char mid[160], badge[48];
    if (G.note[0] && cv_now() - G.note_t < 5.0)
        snprintf(mid, sizeof mid, "%s", G.note);
    else if (G.loaded && G.flight)
        snprintf(mid, sizeof mid, "FREE FLIGHT   WASD move  E/Space up  Q down  Shift fast  drag look  wheel speed  Esc exit");
    else if (G.loaded && geo_loaded())
        snprintf(mid, sizeof mid, "%u points   %u lines   %u surfaces   %u nodes   %u elements   loaded in %.2f s%s",
                 geo_get()->npts, geo_get()->ncrv, geo_get()->nsrf, G.frd.n_nodes, G.frd.n_elems, G.load_seconds,
                 geo_evaluated() ? "   (evaluated by cgx)" : "");
    else if (G.loaded)
        snprintf(mid, sizeof mid, "%u nodes   %u elements   %d steps   %zu triangles   loaded in %.2f s%s",
                 G.frd.n_nodes, G.frd.n_elems, G.frd.n_steps, G.skin.n_tri, G.load_seconds,
                 gp_loaded() ? (deck_loaded() ? "   + .inp + .dat" : "   + .dat") : (deck_loaded() ? "   + .inp" : ""));
    else if (app_busy())
        snprintf(mid, sizeof mid, "loading... %.1f s", cv_now() - G.job.started);
    else
        snprintf(mid, sizeof mid, "no file");
    size_t nm = app_total_msgs();
    if (nm) snprintf(badge, sizeof badge, "%zu message%s", nm, nm == 1 ? "" : "s");
    else snprintf(badge, sizeof badge, "no messages");
    float bw = 120 * s, pw = CV_MIN(360 * s, width * 0.35f);
    int done = 0, total = 0;
    bool exporting = app_export_progress(&done, &total) != 0;
    bool loading = !G.loaded && app_busy();
    nk_layout_row_begin(ctx, NK_STATIC, row, exporting ? 5 : loading ? 4 : 3);
    nk_layout_row_push(ctx, pw);
    nk_label(ctx, G.loaded ? cv_basename(G.path) : "", NK_TEXT_LEFT);
    float midw = CV_MAX(width - pw - bw - 6 * ctx->style.window.spacing.x, 10);
    if (exporting) {                       /* frame export: a real bar, the count, a stop button */
        nk_size cur = (nk_size)done;
        nk_layout_row_push(ctx, midw * 0.45f);
        nk_style_push_style_item(ctx, &ctx->style.progress.cursor_normal, nk_style_item_color(P.accent));
        nk_progress(ctx, &cur, (nk_size)total, NK_FIXED);
        nk_style_pop_style_item(ctx);
        nk_layout_row_push(ctx, midw * 0.35f);
        snprintf(mid, sizeof mid, "exporting frame %d / %d", done, total);
        nk_label(ctx, mid, NK_TEXT_LEFT);
        nk_layout_row_push(ctx, midw * 0.2f - 2 * ctx->style.window.spacing.x);
        if (nk_button_label(ctx, "stop")) app_export_cancel();
    } else if (loading) {                  /* no total known: a pulse that shows life */
        nk_size cur = (nk_size)(fmod(cv_now() - G.job.started, 1.5) / 1.5 * 100.0);
        nk_layout_row_push(ctx, midw * 0.45f);
        nk_style_push_style_item(ctx, &ctx->style.progress.cursor_normal, nk_style_item_color(P.accent));
        nk_progress(ctx, &cur, 100, NK_FIXED);
        nk_style_pop_style_item(ctx);
        nk_layout_row_push(ctx, midw * 0.55f - 2 * ctx->style.window.spacing.x);
        nk_label(ctx, mid, NK_TEXT_LEFT);
    } else {
        nk_layout_row_push(ctx, midw);
        nk_label(ctx, mid, NK_TEXT_LEFT);
    }
    nk_layout_row_push(ctx, bw);
    if (nm) {
        if (nk_button_label(ctx, badge)) G.show_msgs = !G.show_msgs;
    } else {
        nk_label(ctx, badge, NK_TEXT_RIGHT);
    }
    nk_layout_row_end(ctx);
}

static void window_messages(struct nk_context* ctx, float s, float row, int fw, int fh) {
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

static void window_probe(struct nk_context* ctx, float s, float row) {
    if (!G.probe_on || !G.loaded) return;
    float w = 260 * s, h = row * 8.8f;
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
    nk_layout_row_dynamic(ctx, row, 3);
    tip(ctx, "Node ids of this element, drawn in the view");
    nk_checkbox_label(ctx, "ids", &G.show_ids);
    tip(ctx, G.path_arm ? "Now click the end node" : "Plot the field along the surface from this node to the next one you click");
    if (nk_button_label(ctx, G.path_arm ? "path: end?" : "path from")) app_path_start(p->node);
    if (nk_button_label(ctx, "close")) G.probe_on = false;
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "Element, node and value to the clipboard");
    if (nk_button_label(ctx, "copy")) {
        char txt[400];
        snprintf(txt, sizeof txt, "element %u node %u %s = %s", G.frd.elem_id[p->elem], G.frd.node_id[p->node], G.field_label, num);
        sapp_set_clipboard_string(txt);
    }
    nk_end(ctx);
}

/* ---- path plot: the field along the picked surface path ------------------------- */
static void window_path(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.path_open || !G.path_n) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Path", NK_SHOWN);
    was_open = true;
    if (nk_begin(ctx, "Path", nk_rect(fw * 0.35f, fh * 0.55f, fw * 0.4f, fh * 0.35f),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        char txt[200];
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 60 * s);
        nk_layout_row_template_end(ctx);
        snprintf(txt, sizeof txt, "%s along %u nodes, length %.4g   (nodes %u .. %u)", G.has_field ? G.field_label : "no field",
                 G.path_n, G.path_dist[G.path_n - 1], G.frd.node_id[G.path_nodes[0]], G.frd.node_id[G.path_nodes[G.path_n - 1]]);
        nk_label(ctx, txt, NK_TEXT_LEFT);
        if (nk_button_label(ctx, "CSV")) {
            char vp[1100];
            snprintf(vp, sizeof vp, "%.*s_path.csv", (int)(strrchr(G.path, '.') && strrchr(G.path, '.') > strrchr(G.path, cv_path_sep()) ? strrchr(G.path, '.') - G.path : (int)strlen(G.path)), G.path);
            snprintf(G.note, sizeof G.note, app_path_csv(vp) ? "saved %s" : "could not write %s", vp); G.note_t = cv_now();
        }
        struct nk_rect area;
        nk_layout_row_dynamic(ctx, nk_window_get_content_region(ctx).h - row - 4 * s, 1);
        if (nk_widget(&area, ctx) != NK_WIDGET_INVALID && G.has_field && !G.elem_mode) {
            struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
            const struct nk_user_font* font = ctx->style.font;
            float lm = 60 * s, x0 = area.x + lm, y0 = area.y + 4 * s, w = area.w - lm - 6 * s, h = area.h - font->height - 8 * s;
            nk_fill_rect(cv, nk_rect(x0, y0, w, h), 0, P.plot_bg);
            float lo = 1e30f, hi = -1e30f, L = CV_MAX(G.path_dist[G.path_n - 1], 1e-30f);
            for (uint32_t i = 0; i < G.path_n; i++) { float v = G.scalar[G.path_nodes[i]]; if (v == v) { lo = CV_MIN(lo, v); hi = CV_MAX(hi, v); } }
            if (lo > hi) { lo = 0; hi = 1; }
            if (hi <= lo) hi = lo + 1;
            for (int k = 0; k <= 4; k++) {                  /* four bands of the value axis */
                float v = lo + (hi - lo) * k / 4.f, y = y0 + h * (1 - k / 4.f);
                nk_stroke_line(cv, x0, y, x0 + w, y, 1, P.grid);
                legend_num(txt, sizeof txt, v);
                nk_draw_text(cv, nk_rect(area.x, y - font->height * 0.5f, lm - 4 * s, font->height), txt, (int)strlen(txt), font, nk_rgba(0, 0, 0, 0), P.dim);
            }
            float px = 0, py = 0;
            for (uint32_t i = 0; i < G.path_n; i++) {
                float v = G.scalar[G.path_nodes[i]];
                float x = x0 + w * G.path_dist[i] / L, y = y0 + h * (1 - (v - lo) / (hi - lo));
                if (v != v) continue;
                if (i) nk_stroke_line(cv, px, py, x, y, 2.f, nk_rgb(255, 140, 30));
                nk_fill_circle(cv, nk_rect(x - 2 * s, y - 2 * s, 4 * s, 4 * s), nk_rgb(255, 200, 120));
                px = x; py = y;
            }
            snprintf(txt, sizeof txt, "distance along the surface: 0 .. %.4g", L);
            nk_draw_text(cv, nk_rect(x0, y0 + h + 2 * s, w, font->height), txt, (int)strlen(txt), font, nk_rgba(0, 0, 0, 0), P.dim);
        } else if (G.elem_mode) {
            nk_label(ctx, "(per-element mode: switch to nodal values to plot)", NK_TEXT_LEFT);
        }
    }
    if (nk_window_is_hidden(ctx, "Path")) app_path_clear();
    nk_end(ctx);
}

/* ---- overlay: text in the 3D view (node / element ids of the picked element) ---- */
/* The navigation mark: while the mouse turns, pans or zooms the view, what it
   happens about -- an axis cross (X red, Y green, Z blue) with a ring at the
   rotation centre, a ring and cross hair at the zoom point. Only while the
   drag lasts: gone with the button. The box of a box zoom always. */
static void window_nav(struct nk_context* ctx, float s) {
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

static void window_overlay(struct nk_context* ctx, float s) {
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
static void window_find(struct nk_context* ctx, float s, float row) {
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

/* ---- the legend's range, number format, band count, reverse and greyscale:
   in View > Colours & legend, and in a small floating window opened by
   right-clicking the legend. */
static void legend_controls(struct nk_context* ctx, float s, float row) {
    {
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "Keep min/max fixed across steps and components; values above show light grey, below dark grey");
        if (nk_checkbox_label(ctx, "lock range", &G.range_lock) && !G.range_lock) app_refresh_range();
        if (G.range_lock) {
            float step = fabsf(G.rmax - G.rmin) * 0.01f + 1e-30f;
            nk_layout_row_dynamic(ctx, row, 2);
            nk_property_float(ctx, "#min", -1e30f, &G.rmin, 1e30f, step, step * 0.1f);
            nk_property_float(ctx, "#max", -1e30f, &G.rmax, 1e30f, step, step * 0.1f);
            nk_layout_row_dynamic(ctx, row, 1);
        }
        tip(ctx, "A signed field gets a range symmetric about zero, so the middle colour means 0 (unlocks the range)");
        if (nk_checkbox_label(ctx, "centre on zero", &G.center_zero)) { G.range_lock = false; app_refresh_range(); }
        tip(ctx, "Discrete colour steps (0 = smooth)");
        int bands = G.bands;
        nk_property_int(ctx, "#bands", 0, &bands, 64, 1, 0.2f);
        G.bands = bands;
        nk_layout_row_dynamic(ctx, row, 2);
        if (nk_checkbox_label(ctx, "reverse", &G.legend_reverse)) app_colormap(G.cmap);
        tip(ctx, "Greyscale, for printing");
        if (nk_checkbox_label(ctx, "greyscale", &G.legend_grey)) app_colormap(G.cmap);
        nk_layout_row_dynamic(ctx, row, 1);
        static const char* fmts[] = { "numbers: auto", "numbers: fixed", "numbers: scientific" };
        G.legend_fmt = nk_combo(ctx, fmts, 3, G.legend_fmt, (int)row, nk_vec2(200 * s, 3 * row + 20 * s));
        if (G.legend_fmt) nk_property_int(ctx, "#decimals", 0, &G.legend_decimals, 9, 1, 0.2f);
        tip(ctx, "The consistent unit set the model was built in (CalculiX has none): shown in [] in the legend, probe and plots");
        {
            int u = nk_combo(ctx, (const char**)cv_units_name, CV_UNITS_N, G.units, (int)row, nk_vec2(260 * s, CV_UNITS_N * row + 20 * s));
            if (u != G.units) { bool lk = G.range_lock; float a = G.rmin, b = G.rmax; G.units = u; app_select(G.field_name, G.comp); if (lk) { G.range_lock = true; G.rmin = a; G.rmax = b; } }
        }
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Centre the view on the field's minimum / maximum");
        if (nk_button_label(ctx, "go to min") && G.min_at != UINT32_MAX) { G.show_markers = true; app_find(G.elem_mode ? G.frd.elem_id[G.min_at] : G.frd.node_id[G.min_at], G.elem_mode); }
        if (nk_button_label(ctx, "go to max") && G.max_at != UINT32_MAX) { G.show_markers = true; app_find(G.elem_mode ? G.frd.elem_id[G.max_at] : G.frd.node_id[G.max_at], G.elem_mode); }
    }
}

static void window_legend_settings(struct nk_context* ctx, float s, float row) {
    static bool was_open;
    if (!G.legend_edit || !G.loaded) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Legend settings", NK_SHOWN);
    was_open = true;
    float w = 300 * s, h = 14.7f * row;
    if (nk_begin(ctx, "Legend settings", nk_rect(G.vp_x + G.vp_w - w - 180 * s, G.vp_y + 10 * s, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        legend_controls(ctx, s, row);
        nk_layout_row_dynamic(ctx, row, 1);
        if (nk_button_label(ctx, "close")) G.legend_edit = false;
    }
    if (nk_window_is_hidden(ctx, "Legend settings")) G.legend_edit = false;
    nk_end(ctx);
}

/* ---- convergence window: the run's iterations from .cvg, increments from .sta ----
   Residual force (percent, log scale) per Newton iteration, one colour per
   step; increment boundaries as ticks, cutbacks in red. */
static void window_convergence(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_conv) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Convergence", NK_SHOWN);
    was_open = true;
    if (nk_begin(ctx, "Convergence", nk_rect(fw * 0.3f, fh * 0.55f, fw * 0.45f, fh * 0.35f),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        const cv_sta* t = &G.sta;
        uint32_t cut = 0, iters = 0;
        for (uint32_t i = 0; i < t->ninc; i++) { cut += t->inc[i].cutback; iters += (uint32_t)t->inc[i].iters; }
        char txt[160];
        nk_layout_row_dynamic(ctx, row, 1);
        if (!t->ninc && !t->nit) snprintf(txt, sizeof txt, "no .sta / .cvg beside the results");
        else snprintf(txt, sizeof txt, "%u increments, %u cutbacks, %u iterations   (residual force %% per iteration, ticks: increments)",
                      t->ninc, cut, iters);
        nk_label(ctx, txt, NK_TEXT_LEFT);
        struct nk_rect area;
        nk_layout_row_dynamic(ctx, nk_window_get_content_region(ctx).h - row - 4 * s, 1);
        if (nk_widget(&area, ctx) != NK_WIDGET_INVALID && t->nit > 1) {
            struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
            const struct nk_user_font* font = ctx->style.font;
            float lm = 44 * s, x0 = area.x + lm, y0 = area.y + 4 * s, w = area.w - lm - 6 * s, h = area.h - font->height - 8 * s;
            nk_fill_rect(cv, nk_rect(x0, y0, w, h), 0, P.plot_bg);
            float lo = 1e30f, hi = -1e30f;
            for (uint32_t i = 0; i < t->nit; i++) {
                float v = t->it[i].resid_force;
                if (v > 0) { float l = log10f(v); lo = CV_MIN(lo, l); hi = CV_MAX(hi, l); }
            }
            if (lo > hi) { lo = -1; hi = 1; }
            lo = floorf(lo); hi = ceilf(hi); if (hi <= lo) hi = lo + 1;
            for (float d = lo; d <= hi; d += 1) {          /* decades */
                float y = y0 + h * (1 - (d - lo) / (hi - lo));
                nk_stroke_line(cv, x0, y, x0 + w, y, 1, P.grid);
                snprintf(txt, sizeof txt, "1e%g", d);
                nk_draw_text(cv, nk_rect(area.x, y - font->height * 0.5f, lm - 4 * s, font->height), txt, (int)strlen(txt), font,
                             nk_rgba(0, 0, 0, 0), P.dim);
            }
            float dx = w / (float)(t->nit - 1);
            static const struct nk_color pal[6] = { {96,170,240,255}, {240,170,80,255}, {120,210,120,255}, {220,120,200,255}, {230,230,120,255}, {130,220,220,255} };
            float px = 0, py = 0;
            for (uint32_t i = 0; i < t->nit; i++) {
                const cv_cvg_iter* q = &t->it[i];
                float x = x0 + dx * i;
                if (i && (q->inc != t->it[i - 1].inc || q->step != t->it[i - 1].step || q->att != t->it[i - 1].att)) {
                    bool cb = false;                       /* this attempt ended in a cutback? */
                    for (uint32_t k = 0; k < t->ninc; k++)
                        if (t->inc[k].step == t->it[i - 1].step && t->inc[k].inc == t->it[i - 1].inc && t->inc[k].att == t->it[i - 1].att) cb = t->inc[k].cutback;
                    nk_stroke_line(cv, x - dx * 0.5f, y0, x - dx * 0.5f, y0 + h, cb ? 2.f : 1.f, cb ? nk_rgb(230, 80, 80) : mix(P.grid, P.dim, 0.2f));
                }
                float v = q->resid_force > 0 ? log10f(q->resid_force) : lo;
                float y = y0 + h * (1 - (v - lo) / (hi - lo));
                struct nk_color c = pal[(q->step - 1) % 6 < 0 ? 0 : (q->step - 1) % 6];
                if (i && q->step == t->it[i - 1].step && q->inc == t->it[i - 1].inc && q->att == t->it[i - 1].att)
                    nk_stroke_line(cv, px, py, x, y, 1.5f, c);
                nk_fill_circle(cv, nk_rect(x - 2 * s, y - 2 * s, 4 * s, 4 * s), c);
                px = x; py = y;
            }
            snprintf(txt, sizeof txt, "iteration 1 .. %u", t->nit);
            nk_draw_text(cv, nk_rect(x0, y0 + h + 2 * s, w, font->height), txt, (int)strlen(txt), font, nk_rgba(0, 0, 0, 0), P.dim);
        }
    }
    if (nk_window_is_hidden(ctx, "Convergence")) G.show_conv = false;
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

static void window_browser(struct nk_context* ctx, float s, float row, int fw, int fh) {
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
static void drop_hint(struct nk_context* ctx, float s, float row) {
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

/* ---- axes gizmo (bottom-right of the view) -------------------------------------
   World X/Y/Z projected with the camera's rotation only, so it turns with the
   view. Far axes are drawn first. Clicking a tip looks from that direction;
   clicking the centre returns to the iso view. */
static void window_axes(struct nk_context* ctx, float s) {
    if (!G.loaded || G.hide_axes) return;
    float size = 120 * s, r = size * 0.36f;
    struct nk_rect wr = nk_rect(G.vp_x + G.vp_w - size - 12 * s,
                                G.vp_y + G.vp_h - size - 12 * s, size, size);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    nk_style_push_float(ctx, &ctx->style.window.border, 0);
    if (nk_begin(ctx, "axes", wr, NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        nk_window_set_bounds(ctx, "axes", wr);
        struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
        const struct nk_user_font* font = ctx->style.font;
        const struct nk_input* in = &ctx->input;
        v3 eye, fwd, right, up;
        cam_basis(&G.cam, &eye, &fwd, &right, &up);
        float cx = wr.x + size * 0.5f, cy = wr.y + size * 0.5f - 6 * s;

        typedef struct { float x, y, depth; int axis; bool neg; } tip_t;
        tip_t tips[6];
        static const struct nk_color col[3] = { {230, 80, 80, 255}, {110, 200, 90, 255}, {80, 140, 240, 255} };
        static const char* names[3] = { "X", "Y", "Z" };
        static const int view_pos[3] = { CV_VIEW_PX, CV_VIEW_PY, CV_VIEW_PZ };
        static const int view_neg[3] = { CV_VIEW_NX, CV_VIEW_NY, CV_VIEW_NZ };
        for (int a = 0; a < 3; a++) {
            v3 e = v3_make(a == 0, a == 1, a == 2);
            float sx = v3_dot(e, right), sy = -v3_dot(e, up), d = v3_dot(e, fwd);
            tips[2 * a]     = (tip_t){ cx + sx * r, cy + sy * r, d, a, false };
            tips[2 * a + 1] = (tip_t){ cx - sx * r, cy - sy * r, -d, a, true };
        }
        /* painter's order: largest depth (furthest along the view direction) first */
        for (int i = 1; i < 6; i++)
            for (int j = i; j > 0 && tips[j].depth > tips[j - 1].depth; j--) {
                tip_t t = tips[j]; tips[j] = tips[j - 1]; tips[j - 1] = t;
            }
        nk_fill_circle(cv, nk_rect(cx - r * 1.25f, cy - r * 1.25f, r * 2.5f, r * 2.5f), nk_rgba(30, 30, 32, 110));
        float tip_r = 9 * s;
        int clicked_view = -1;
        for (int i = 0; i < 6; i++) {
            const tip_t* t = &tips[i];
            struct nk_color c = col[t->axis];
            struct nk_rect hit = nk_rect(t->x - tip_r, t->y - tip_r, 2 * tip_r, 2 * tip_r);
            bool hover = nk_input_is_mouse_hovering_rect(in, hit);
            if (t->neg) {
                struct nk_color f = nk_rgba(c.r, c.g, c.b, hover ? 200 : 90);
                nk_fill_circle(cv, nk_rect(t->x - tip_r * 0.6f, t->y - tip_r * 0.6f, tip_r * 1.2f, tip_r * 1.2f), f);
            } else {
                nk_stroke_line(cv, cx, cy, t->x, t->y, 2.5f * s, c);
                nk_fill_circle(cv, hit, hover ? nk_rgb(255, 255, 255) : c);
                float tw = font->width(font->userdata, font->height, names[t->axis], 1);
                nk_draw_text(cv, nk_rect(t->x - tw * 0.5f, t->y - font->height * 0.5f, tw + 1, font->height),
                             names[t->axis], 1, font, nk_rgba(0, 0, 0, 0), nk_rgb(20, 20, 20));
            }
            if (nk_input_is_mouse_click_in_rect(in, NK_BUTTON_LEFT, hit))
                clicked_view = t->neg ? view_neg[t->axis] : view_pos[t->axis];
        }
        struct nk_rect centre = nk_rect(cx - 5 * s, cy - 5 * s, 10 * s, 10 * s);
        nk_fill_circle(cv, centre, nk_rgb(200, 200, 200));
        if (clicked_view < 0 && nk_input_is_mouse_click_in_rect(in, NK_BUTTON_LEFT, centre)) clicked_view = CV_VIEW_ISO;
        const char* proj = G.cam.ortho ? "ortho" : "persp";
        float pw = font->width(font->userdata, font->height, proj, (int)strlen(proj));
        nk_draw_text(cv, nk_rect(cx - pw * 0.5f, wr.y + size - font->height - 2 * s, pw + 1, font->height),
                     proj, (int)strlen(proj), font, nk_rgba(0, 0, 0, 0), nk_rgb(200, 200, 200));
        if (clicked_view >= 0) {
            float d = G.cam.dist;                      /* snap the direction, keep the zoom */
            v3 t = G.cam.target;
            app_view(clicked_view);
            G.cam.dist = d;
            G.cam.target = t;
        }
    }
    nk_end(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
}

/* ---- frame ------------------------------------------------------------------------ */

void ui_frame(struct nk_context* ctx, int fw, int fh) {
    apply_scale(ctx);
    const float s = U.scale;
    const float row = roundf(13.f * s) + 10.f * s;
    const float pad = ctx->style.window.padding.y * 2;
    const float panel_w = roundf(310 * s);
    const float trow = roundf(row * 1.35f);              /* time bar: room for step ticks */
    const float bar_h = trow + pad + 2;
    const float tool_h = 2 * row + ctx->style.window.spacing.y + pad + 2;
    const float status_h = row + pad + 2;
    const float W = (float)fw, H = (float)fh;
    const nk_flags fixed = NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR;

    G.vp_x = (int)panel_w;
    G.vp_y = (int)tool_h;
    G.vp_w = CV_MAX((int)(W - panel_w), 1);
    G.vp_h = CV_MAX((int)(H - status_h - bar_h - tool_h), 1);
    if (G.hide_panels && G.loaded) {         /* H: the view alone, legend and gizmo stay */
        G.vp_x = 0; G.vp_y = 0; G.vp_w = fw; G.vp_h = fh;
    }

    struct nk_rect r = nk_rect(0, 0, panel_w, H - status_h);
    if (!G.hide_panels && nk_begin(ctx, "Scene", r, NK_WINDOW_BORDER)) {
        nk_window_set_bounds(ctx, "Scene", r);
        panel_scene(ctx, s, row);
    }
    if (!G.hide_panels) nk_end(ctx);

    if (G.loaded && !G.hide_panels) {
        r = nk_rect(panel_w, 0, W - panel_w, tool_h);
        if (nk_begin(ctx, "Toolbar", r, fixed)) {
            nk_window_set_bounds(ctx, "Toolbar", r);
            panel_toolbar(ctx, s, row);
        }
        nk_end(ctx);

        r = nk_rect(panel_w, H - status_h - bar_h, W - panel_w, bar_h);
        if (nk_begin(ctx, "Timebar", r, fixed)) {
            nk_window_set_bounds(ctx, "Timebar", r);
            panel_timebar(ctx, s, trow, r.w - 2 * ctx->style.window.padding.x);
        }
        nk_end(ctx);

    }
    if (G.loaded) {
        bool by_group = G.faces_mode >= FM_TYPE && G.faces_mode <= FM_GRP && G.show_faces;
        bool by_field = G.has_field && !by_group;
        if ((by_field || by_group) && !G.hide_legend) {
            float lw = 150 * s, lh = CV_MIN(440 * s, G.vp_h - 20 * s);
            if (by_field) {                       /* as wide as the title (field, component, unit) */
                const struct nk_user_font* f = ctx->style.font;
                float tw = f->width(f->userdata, f->height, G.field_label, (int)strlen(G.field_label));
                lw = CV_MIN(CV_MAX(lw, tw + 24 * s), 380 * s);
            }
            r = nk_rect(W - lw - 10 * s, G.vp_y + 10 * s, lw, lh);
            /* draggable (by its top line); once moved it keeps its place, clamped to the view */
            struct nk_window* lw_win = nk_window_find(ctx, "Legend");
            if (lw_win) {
                r.x = CV_MIN(CV_MAX(lw_win->bounds.x, (float)G.vp_x), W - lw);
                r.y = CV_MIN(CV_MAX(lw_win->bounds.y, (float)G.vp_y), G.vp_y + G.vp_h - lh);
            }
            nk_flags lf = NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | (by_group ? 0 : NK_WINDOW_NO_SCROLLBAR);
            if (lh > 80 * s && nk_begin(ctx, "Legend", r, lf)) {
                struct nk_rect b = nk_window_get_bounds(ctx);
                if (b.w != lw || b.h != lh) nk_window_set_bounds(ctx, "Legend", nk_rect(b.x, b.y, lw, lh));
                if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_RIGHT, b)) G.legend_edit = true;
                if (by_group) panel_group_legend(ctx, s, row);
                else panel_legend(ctx, s, row);
            }
            if (lh > 80 * s) nk_end(ctx);
        }
    }

    r = nk_rect(0, H - status_h, W, status_h);
    if (!G.hide_panels && nk_begin(ctx, "Status", r, fixed)) {
        nk_window_set_bounds(ctx, "Status", r);
        panel_status(ctx, s, row, r.w - 2 * ctx->style.window.padding.x);
    }
    if (!G.hide_panels) nk_end(ctx);

    drop_hint(ctx, s, row);
    window_axes(ctx, s);
    window_probe(ctx, s, row);
    window_messages(ctx, s, row, fw, fh);
    window_convergence(ctx, s, row, fw, fh);
    window_legend_settings(ctx, s, row);
    window_overlay(ctx, s);
    window_nav(ctx, s);
    window_find(ctx, s, row);
    window_path(ctx, s, row, fw, fh);
    window_browser(ctx, s, row, fw, fh);
}
