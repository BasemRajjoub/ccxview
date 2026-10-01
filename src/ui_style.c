/* ui_style.c -- the look: the theme list and the palette P taken from it, the
   font baked at the current scale, and the scale itself (desktop x Ctrl +/- zoom).
   The only file that includes nuklear_style.c. */
#include "app.h"
#include "ui.h"
#include "cfg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "nuklear_style.c"          /* upstream demo themes: set_style() */
#include <math.h>
#include "ui_int.h"

/* ---- scale + font -------------------------------------------------------------- */

struct uii_scale U = { 1.f, 0.f };

/* the colours we draw ourselves (hints, plots, legend), derived from the theme */
struct uii_palette P;

/* 0 is ccxview's own look; the rest are the upstream demo themes as they are.
   Only the Catppuccin ones: the older demo themes are flat greys. */
const struct uii_theme themes[] = {      /* as many as NTHEMES in ui_int.h */
    { "ccxview",              -1 },
    { "Catppuccin Latte",     THEME_CATPPUCCIN_LATTE },
    { "Catppuccin Frappe",    THEME_CATPPUCCIN_FRAPPE },
    { "Catppuccin Macchiato", THEME_CATPPUCCIN_MACCHIATO },
    { "Catppuccin Mocha",     THEME_CATPPUCCIN_MOCHA },
};

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

struct nk_color mix(struct nk_color a, struct nk_color b, float t) {
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

void apply_scale(struct nk_context* ctx) {
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
