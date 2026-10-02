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
#include "icons.h"

/* the embedded fonts (font_data.c, from scripts/embed-fonts.py): Inter for text,
   the math symbols Inter lacks from Noto Sans Math, and the Lucide icons */
extern const unsigned char cv_font_text[], cv_font_math[], cv_font_icons[];
extern const unsigned cv_font_text_size, cv_font_math_size, cv_font_icons_size;

/* ---- scale + font -------------------------------------------------------------- */

struct uii_scale U = { .zoom = 1.f, .font_size = 15.f };

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

float ui_get_font_size(void) { return U.font_size; }
bool ui_get_pixel_font(void) { return U.pixel_font; }
void ui_set_pixel_font(bool on) { U.pixel_font = on; }
void ui_set_font_size(float px) { U.font_size = CV_MIN(CV_MAX(roundf(px), 10.f), 24.f); }

void ui_zoom(int dir) {
    if (dir == 0) U.zoom = 1.f;
    else U.zoom = CV_MIN(CV_MAX(U.zoom * (dir > 0 ? 1.1f : 1.f / 1.1f), 0.5f), 3.f);
}

#define ICON_SCALE 1.2f

/* After the bake. Nuklear puts every glyph ascent + 0.5 below the line top, a
   fraction of a pixel off the grid, so the linear sampler smears each one over
   two rows: move the glyphs onto whole pixels. And centre the icons (baked a
   size up, on their own em box) on the capitals' ink, where the eye reads the
   middle of a label. */
static void snap_glyphs(void) {
    struct nk_font_glyph* g = U.atlas.glyphs;
    float cap = 0;
    for (int i = 0; i < U.atlas.glyph_count; i++)
        if (g[i].codepoint == 'H') { cap = (g[i].y0 + g[i].y1) * 0.5f; break; }
    for (int i = 0; i < U.atlas.glyph_count; i++) {
        float y = g[i].y0;
        if (g[i].codepoint >= 0xE000 && cap > 0) y += cap - (g[i].y0 + g[i].y1) * 0.5f;
        float d = roundf(y) - g[i].y0;
        g[i].y0 += d; g[i].y1 += d;
    }
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
    static const nk_rune text[] = { FONT_TEXT_RANGES, 0 }, math[] = { FONT_MATH_RANGES, 0 };
    static const nk_rune icons[] = { FONT_ICON_RANGES, 0 };
    if (U.pixel_font) {                /* Nuklear's ProggyClean (ASCII, Latin-1) as before; */
        struct nk_font_config pc = nk_font_config(px);   /* the rest from Inter, merged below */
        pc.oversample_h = 3; pc.oversample_v = 2;
        U.font = nk_font_atlas_add_default(&U.atlas, px, &pc);
    }
    struct nk_font_config cfg = nk_font_config(px);
    cfg.range = text;
    cfg.merge_mode = U.pixel_font;
    /* whole pixels, not 3x oversampling at fractional pens: the stems stay one pixel wide */
    cfg.oversample_h = 1; cfg.oversample_v = 1; cfg.pixel_snap = nk_true;
    struct nk_font* inter = nk_font_atlas_add_from_memory(&U.atlas, (void*)cv_font_text, cv_font_text_size, px, &cfg);
    if (!U.pixel_font) U.font = inter;
    struct nk_font_config mc = nk_font_config(px);       /* merged: found after Inter's ranges */
    mc.merge_mode = nk_true; mc.range = math;
    mc.oversample_h = 1; mc.oversample_v = 1; mc.pixel_snap = nk_true;
    nk_font_atlas_add_from_memory(&U.atlas, (void*)cv_font_math, cv_font_math_size, px, &mc);
    float ipx = roundf(px * ICON_SCALE);                 /* thin strokes: a size up */
    struct nk_font_config ic = nk_font_config(ipx);
    ic.merge_mode = nk_true; ic.range = icons;
    ic.oversample_h = 2; ic.oversample_v = 2;
    nk_font_atlas_add_from_memory(&U.atlas, (void*)cv_font_icons, cv_font_icons_size, ipx, &ic);
    int w, h;
    const void* pixels = nk_font_atlas_bake(&U.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    snap_glyphs();
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

struct nk_color uii_bg(int alpha) {
    return nk_rgba((int)(G.bg[0] * 255), (int)(G.bg[1] * 255), (int)(G.bg[2] * 255), alpha);
}
struct nk_color uii_on_bg(void) {
    float l = 0.299f * G.bg[0] + 0.587f * G.bg[1] + 0.114f * G.bg[2];
    return l > 0.5f ? nk_rgb(20, 20, 20) : nk_rgb(235, 235, 235);
}
struct nk_color uii_on_bg_dim(void) { return mix(uii_on_bg(), uii_bg(255), 0.4f); }

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
    /* a selected radio button is filled with the mark colour, as a ticked box is: a small dot
       in a grey ring barely showed in the light and the pastel themes */
    st->option.padding = nk_vec2(1, 1);
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

    {   /* rounded controls, square windows */
        st->button.rounding = 4 * s; st->contextual_button.rounding = 3 * s; st->menu_button.rounding = 3 * s;
        st->combo.rounding = 4 * s; st->combo.button.rounding = 3 * s;
        st->property.rounding = 4 * s; st->property.edit.rounding = 3 * s;
        st->edit.rounding = 4 * s; st->selectable.rounding = 3 * s;
        st->slider.rounding = 3 * s; st->progress.rounding = 3 * s; st->progress.cursor_rounding = 3 * s;
        st->scrollv.rounding = st->scrollh.rounding = 4 * s;
        st->scrollv.rounding_cursor = st->scrollh.rounding_cursor = 4 * s;
        st->tab.rounding = 4 * s; st->window.rounding = 0;
        st->window.popup_border = 1; st->checkbox.border = 0;
    }
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
    float px = roundf((U.pixel_font ? 13.f : U.font_size) * s);   /* ProggyClean is drawn for 13 */
    if (fabsf(s - U.scale) < 0.01f && px == U.px && U.pixel_font == U.baked_pixel && U.atlas_live) {
        if (U.restyle) { U.restyle = false; restyle(ctx, s); }
        return;
    }
    U.scale = s;
    U.restyle = false;
    U.px = px;
    U.baked_pixel = U.pixel_font;
    bake_font(ctx, U.px);
    restyle(ctx, s);
}

void ui_init(void) {
    U.smp = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR, .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE });
}
