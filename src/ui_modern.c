/* ui_modern.c -- phone-like widgets (G.ui_modern): a switch for every check box of the panels and
   windows (the top bar, time bar and status line keep their boxes: their widths are measured), a
   segmented control for a list of 2 to 4 short choices, chips for a family of on/off filters, a
   slider with its label and value inside the track (Ctrl+click: type the value), - and + on the
   number boxes. All drawn from the theme's own colours, so glass and light themes follow. Off:
   Nuklear's widgets as they were, untouched (every entry falls back to them). */
#define UI_MODERN_IMPL
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include <stdlib.h>
#include "ui_int.h"

unsigned ui_mouse_mods;

static float lum(struct nk_color c) { return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.f; }
static struct nk_color with_a(struct nk_color c, int a) { c.a = (nk_byte)CV_MAX(0, CV_MIN(255, a)); return c; }
/* text that reads on c */
static struct nk_color ink_on(struct nk_color c) { return lum(c) > 0.62f ? nk_rgb(24, 24, 28) : nk_rgb(255, 255, 255); }
static struct nk_color fill_of(const struct nk_style_item* it) { return it->type == NK_STYLE_ITEM_COLOR ? it->data.color : nk_rgb(70, 70, 76); }

/* the bars keep Nuklear's widgets: their items' widths are measured for them */
bool uim_on(struct nk_context* ctx) {
    if (!G.ui_modern || !ctx->current) return false;
    const struct nk_window* w = ctx->current;
    while (w->parent) w = w->parent;
    const char* n = w->name_string;
    return strcmp(n, "Toolbar") && strcmp(n, "Timebar") && strcmp(n, "Status");
}

/* input for the widget nk_widget just placed: none when read-only, disabled or under a popup */
static const struct nk_input* input_of(struct nk_context* ctx, enum nk_widget_layout_states st) {
    const struct nk_window* w = ctx->current;
    if (st != NK_WIDGET_VALID || (w->layout->flags & NK_WINDOW_ROM) || (w->popup.win && w->popup.active)) return NULL;
    return &ctx->input;
}
static void text_at(struct nk_command_buffer* cb, const struct nk_user_font* f, float x, float y, float w, float h,
                    const char* s, struct nk_color c, int align) {
    int len = (int)strlen(s);
    float tw = f->width(f->userdata, f->height, s, len);
    if (align > 0) x += w - tw; else if (align == 0) x += (w - tw) * 0.5f;
    nk_draw_text(cb, nk_rect(x, y + (h - f->height) * 0.5f, CV_MIN(tw + 1, w), f->height), s, len, f, nk_rgba(0, 0, 0, 0), c);
}
static struct nk_rect shrink(struct nk_rect r, float d) { return nk_rect(r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d); }
static float text_w(struct nk_context* ctx, const char* s) {
    const struct nk_user_font* f = ctx->style.font;
    return f->width(f->userdata, f->height, s, (int)strlen(s));
}

/* ---- switch: Nuklear's check box with its box hidden and room for the pill, so the click, the
   hover and a disabled row stay its own; the pill drawn where the box was */
nk_bool uim_checkbox_label(struct nk_context* ctx, const char* label, nk_bool* v) {
    if (!uim_on(ctx)) return nk_checkbox_label(ctx, label, v);
    struct nk_style_toggle* st = &ctx->style.checkbox;
    const struct nk_user_font* f = ctx->style.font;
    float s = U.scale, ph = roundf(f->height * 0.92f), pw = roundf(ph * 1.8f);
    struct nk_color off = fill_of(&st->normal), on = fill_of(&st->cursor_normal);
    struct nk_rect r = nk_widget_bounds(ctx);
    bool dis = ctx->current->widgets_disabled;
    struct nk_style_item hide = nk_style_item_hide();
    nk_style_push_style_item(ctx, &st->normal, hide); nk_style_push_style_item(ctx, &st->hover, hide);
    nk_style_push_style_item(ctx, &st->active, hide);
    nk_style_push_style_item(ctx, &st->cursor_normal, hide); nk_style_push_style_item(ctx, &st->cursor_hover, hide);
    nk_style_push_color(ctx, &st->border_color, nk_rgba(0, 0, 0, 0));
    nk_style_push_float(ctx, &st->spacing, st->spacing + pw - f->height + 3 * s);
    nk_bool ch = nk_checkbox_label(ctx, label, v);
    nk_style_pop_float(ctx); nk_style_pop_color(ctx);
    for (int i = 0; i < 5; i++) nk_style_pop_style_item(ctx);
    if (r.w <= 0) return ch;
    bool over = !dis && nk_input_is_mouse_hovering_rect(&ctx->input, r) && !(ctx->current->popup.win && ctx->current->popup.active);
    struct nk_command_buffer* cb = nk_window_get_canvas(ctx);
    struct nk_rect t = nk_rect(r.x + 1, roundf(r.y + (r.h - ph) * 0.5f), pw, ph);
    struct nk_color tc = *v ? on : off;
    if (over) tc = mix(tc, P.text, 0.12f);
    int a = dis ? tc.a / 2 : tc.a;
    nk_fill_rect(cb, t, ph * 0.5f, with_a(tc, a));
    float kr = ph * 0.5f - 2 * s, kx = *v ? t.x + t.w - ph * 0.5f : t.x + ph * 0.5f;
    struct nk_color kc = *v ? nk_rgb(255, 255, 255) : mix(P.text, off, 0.25f);
    nk_fill_circle(cb, nk_rect(kx - kr, t.y + ph * 0.5f - kr, 2 * kr, 2 * kr), with_a(kc, dis ? 120 : 255));
    return ch;
}

/* ---- segmented control: the choices side by side in one rounded box, the chosen one filled */
bool uim_seg_fits(struct nk_context* ctx, const char* const* items, int n) {
    if (!uim_on(ctx) || n < 2 || n > 4) return false;
    float w = nk_widget_bounds(ctx).w, need = 0;
    for (int i = 0; i < n; i++) need = CV_MAX(need, text_w(ctx, items[i]) + 12 * U.scale);
    return w > 0 && need * n <= w;
}
bool uim_segments(struct nk_context* ctx, const char* const* items, int n, int* cur) { return uim_segments_t(ctx, items, n, cur, NULL); }
/* tips: one per segment (shown on it, and where the interface test finds it), or NULL */
bool uim_segments_t(struct nk_context* ctx, const char* const* items, int n, int* cur, const char* const* tips) {
    struct nk_rect r;
    enum nk_widget_layout_states ws = nk_widget(&r, ctx);
    if (!ws) return false;
    const struct nk_input* in = input_of(ctx, ws);
    const struct nk_style* st = &ctx->style;
    struct nk_command_buffer* cb = nk_window_get_canvas(ctx);
    float s = U.scale, rd = st->combo.rounding, sw = r.w / n;
    struct nk_color bg = fill_of(&st->combo.normal), acc = fill_of(&st->checkbox.cursor_normal);
    bool dis = ws == NK_WIDGET_DISABLED;
    nk_fill_rect(cb, r, rd, bg);
    int was = *cur;
    for (int i = 0; i < n; i++) {
        struct nk_rect c = nk_rect(roundf(r.x + i * sw), r.y, roundf(r.x + (i + 1) * sw) - roundf(r.x + i * sw), r.h);
        bool sel = i == *cur, over = in && nk_input_is_mouse_hovering_rect(in, c);
        if (tips && tips[i]) { uii_test_mark_at(ctx, tips[i], c); if (over) tip_show(ctx, tips[i]); }
        if (sel) nk_fill_rect(cb, shrink(c, 2 * s), rd - s, with_a(acc, dis ? 110 : CV_MAX(bg.a, 220)));
        else if (over) nk_fill_rect(cb, shrink(c, 2 * s), rd - s, with_a(mix(bg, P.text, 0.1f), CV_MAX(bg.a, 160)));
        else if (i > 0 && i - 1 != *cur)        /* a hairline between two plain ones */
            nk_stroke_line(cb, c.x, c.y + 6 * s, c.x, c.y + c.h - 6 * s, 1.f, with_a(mix(bg, P.dim, 0.5f), 200));
        struct nk_color tc = sel ? ink_on(acc) : P.text;
        text_at(cb, st->font, c.x + 3 * s, c.y, c.w - 6 * s, c.h, items[i], with_a(tc, dis ? 120 : 255), 0);
        if (in && nk_input_mouse_clicked(in, NK_BUTTON_LEFT, c)) *cur = i;
    }
    return *cur != was;
}

/* ---- slider with its label and value inside: drag anywhere on it; Ctrl+click types the value */
static const void* ed_id;                     /* the slider being typed into */
static char ed_buf[32];
static bool ed_new;

bool uim_slider(struct nk_context* ctx, const void* id, const char* label, float* v, float lo, float hi, float step, const char* shown) {
    const struct nk_style* st = &ctx->style;
    float s = U.scale, old = *v;
    if (ed_id == id) {                        /* typing: Enter or a click elsewhere sets it */
        if (ed_new) {                         /* focused, the old value selected: typing replaces it */
            nk_edit_focus(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER);
            struct nk_edit_state* e = &ctx->current->edit;
            e->sel_start = 0; e->sel_end = e->cursor = (int)strlen(ed_buf);
            ed_new = false;
        }
        nk_flags f = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, ed_buf, sizeof ed_buf, nk_filter_float);
        if (f & (NK_EDIT_COMMITED | NK_EDIT_DEACTIVATED)) {
            char* e;
            float x = strtof(ed_buf, &e);
            if (e != ed_buf) *v = CV_MIN(CV_MAX(x, lo), hi);
            ed_id = NULL;
        }
        return *v != old;
    }
    struct nk_rect r;
    enum nk_widget_layout_states ws = nk_widget(&r, ctx);
    if (!ws) return false;
    const struct nk_input* in = input_of(ctx, ws);
    bool dis = ws == NK_WIDGET_DISABLED;
    float pad = 6 * s, x0 = r.x + pad, w = r.w - 2 * pad;
    if (in && hi > lo && w > 0 && nk_input_has_mouse_click_down_in_rect(in, NK_BUTTON_LEFT, r, nk_true)) {
        if (!shown && in->mouse.buttons[NK_BUTTON_LEFT].clicked && (ui_mouse_mods & (SAPP_MODIFIER_CTRL | SAPP_MODIFIER_SUPER))) {
            snprintf(ed_buf, sizeof ed_buf, "%g", *v);
            ed_id = id; ed_new = true;
            return false;
        }
        float t = CV_MIN(CV_MAX((in->mouse.pos.x - x0) / w, 0.f), 1.f), nv = lo + t * (hi - lo);
        if (step > 0) nv = CV_MIN(lo + roundf((nv - lo) / step) * step, hi);
        *v = nv;
    }
    bool over = in && nk_input_is_mouse_hovering_rect(in, r);
    struct nk_command_buffer* cb = nk_window_get_canvas(ctx);
    struct nk_color bg = fill_of(over ? &st->property.hover : &st->property.normal), acc = fill_of(&st->checkbox.cursor_normal);
    float rd = CV_MIN(r.h * 0.5f, 7 * s), t = hi > lo ? CV_MIN(CV_MAX((*v - lo) / (hi - lo), 0.f), 1.f) : 0;
    nk_fill_rect(cb, r, rd, bg);
    float fx = x0 + t * w;                    /* the filled part, the accent's tint; a bar at the value */
    struct nk_color fc = with_a(mix(bg, acc, 0.32f), dis ? bg.a / 2 : CV_MAX(bg.a, 150));
    if (fx - r.x >= 2 * rd) nk_fill_rect(cb, nk_rect(r.x, r.y, fx - r.x, r.h), rd, fc);
    else if (fx > r.x + 1) nk_fill_rect(cb, nk_rect(r.x, r.y, fx - r.x, r.h), (fx - r.x) * 0.5f, fc);
    nk_fill_rect(cb, nk_rect(roundf(fx - s), r.y + 4 * s, CV_MAX(2 * s, 2.f), r.h - 8 * s), s, with_a(acc, dis ? 110 : 255));
    char b[24];
    if (!shown) { snprintf(b, sizeof b, fabsf(*v) >= 100 ? "%.0f" : "%.3g", *v); shown = b; }
    struct nk_color tc = with_a(P.text, dis ? 120 : 255);
    float vw = text_w(ctx, shown);
    text_at(cb, st->font, r.x + 10 * s, r.y, r.w - vw - 26 * s, r.h, label, tc, -1);
    text_at(cb, st->font, r.x + 10 * s, r.y, r.w - 20 * s, r.h, shown, tc, 1);
    return *v != old;
}

/* ---- chips: rounded toggles, the ones on filled; one in the next cell, or a wrapping row of them */
bool uim_chip(struct nk_context* ctx, const char* label, nk_bool* v) {
    const struct nk_style* st = &ctx->style;
    struct nk_rect r;
    enum nk_widget_layout_states ws = nk_widget(&r, ctx);
    if (!ws) return false;
    const struct nk_input* in = input_of(ctx, ws);
    float s = U.scale;
    struct nk_color bg = fill_of(&st->combo.normal), acc = fill_of(&st->checkbox.cursor_normal);
    struct nk_command_buffer* cb = nk_window_get_canvas(ctx);
    struct nk_rect c = nk_rect(r.x, r.y + 2 * s, r.w, r.h - 4 * s);
    bool on = *v, over = in && nk_input_is_mouse_hovering_rect(in, r);
    struct nk_color f = on ? with_a(acc, CV_MAX(bg.a, 220)) : over ? with_a(mix(bg, P.text, 0.1f), CV_MAX(bg.a, 140)) : bg;
    nk_fill_rect(cb, c, c.h * 0.5f, f);
    if (!on) nk_stroke_rect(cb, c, c.h * 0.5f, 1.f, with_a(mix(bg, P.dim, 0.6f), 220));
    text_at(cb, st->font, c.x + 4 * s, c.y, c.w - 8 * s, c.h, label, on ? ink_on(acc) : P.text, 0);
    if (in && nk_input_mouse_clicked(in, NK_BUTTON_LEFT, r)) { *v = !*v; return true; }
    return false;
}
bool uim_chips(struct nk_context* ctx, float row, int n, const char* const* labels, nk_bool* const* v, const char* const* tips) {
    float s = U.scale, gap = ctx->style.window.spacing.x, avail = ctx->current->layout->bounds.w;
    bool ch = false;
    for (int i = 0; i < n;) {
        int k = 0;
        float used = 0;
        while (i + k < n) {
            float cw = text_w(ctx, labels[i + k]) + 24 * s;
            if (k && used + gap + cw > avail) break;
            used += (k ? gap : 0) + cw; k++;
        }
        nk_layout_row_begin(ctx, NK_STATIC, row, k);
        for (int j = 0; j < k; j++, i++) {
            nk_layout_row_push(ctx, CV_MIN(text_w(ctx, labels[i]) + 24 * s, avail));
            if (tips && tips[i]) tip(ctx, tips[i]);
            ch |= uim_chip(ctx, labels[i], v[i]);
        }
        nk_layout_row_end(ctx);
    }
    return ch;
}

/* ---- number boxes: - and + for the arrows (the kit calls it round its nk_property) */
void uim_stepper_begin(struct nk_context* ctx, int* save) {
    save[0] = -1;
    if (!uim_on(ctx)) return;
    save[0] = ctx->style.property.sym_left; save[1] = ctx->style.property.sym_right;
    ctx->style.property.sym_left = NK_SYMBOL_MINUS; ctx->style.property.sym_right = NK_SYMBOL_PLUS;
}
void uim_stepper_end(struct nk_context* ctx, const int* save) {
    if (save[0] < 0) return;
    ctx->style.property.sym_left = (enum nk_symbol_type)save[0]; ctx->style.property.sym_right = (enum nk_symbol_type)save[1];
}
