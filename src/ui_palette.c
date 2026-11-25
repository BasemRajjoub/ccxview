/* ui_palette.c -- the command palette (Ctrl+K, Ctrl+Shift+P), VS Code style: a box in the window's upper
   middle, over every other window, with the cursor in it. Typing filters the feature registry (app_cmds.c)
   with the fuzzy search (fsearch.h); empty, it lists the recent features, then a few common ones.
   Up / Down (wrapping), PageUp / PageDown and the mouse select; Enter runs the selected one: a switch flips,
   an action runs, a go-to jumps, a value (list, number) jumps to its place; the selected value row shows its
   live control under it. Ctrl+Enter: the same, the palette stays open. Alt+Enter or the pin: on / off the
   top bar. Esc or a click outside closes. G.ui_palette off: Ctrl+K is the panel's search box (ui_search.c). */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include <ctype.h>
#include "ui_int.h"
#include "app_cmds.h"
#include "fsearch.h"

enum { MAXROW = 12, NREC = 6, PAGE = 5 };
static const char* const COMMON[] = { "open_file", "export_png", "render_image", "view_fit", "find", "settings" };
static const char* const COMMON_EMPTY[] = { "open_file", "settings", "goto_theme", "goto_keys", "about" };   /* no model */
static const char WIN[] = "Palette";

static struct {
    bool open, focus, typing;
    char q[64]; int qlen;
    int sel, n, nrec;
    int step, page;         /* the keys since the last frame: rows to move (wrapping), pages (clamped) */
    int enter;              /* 1 Enter, 2 Ctrl+Enter, 3 Alt+Enter */
    const cv_cmd* list[MAXROW];
} Q;

void ui_palette_open(const char* q) {
    if (!Q.open) { Q.sel = 0; Q.qlen = 0; Q.q[0] = 0; }
    if (q) { snprintf(Q.q, sizeof Q.q, "%s", q); Q.qlen = (int)strlen(Q.q); Q.sel = 0; }
    Q.open = Q.focus = true;
    ui_search_blur();                         /* the panel's box lets the keys go */
    app_wake();
}
void ui_palette_close(void) { Q.open = Q.typing = false; Q.qlen = 0; Q.q[0] = 0; Q.sel = 0; Q.step = Q.page = Q.enter = 0; app_wake(); }
bool ui_palette_shown(void) { return Q.open; }
bool ui_palette_typing(void) { return Q.open && Q.typing; }
int  ui_palette_sel(void) { return Q.sel; }
int  ui_palette_count(void) { return Q.n; }
const char* ui_palette_sel_id(void) { return Q.open && Q.sel < Q.n ? Q.list[Q.sel]->id : NULL; }

void ui_palette_key(int code, bool ctrl, bool alt) {
    switch (code) {
    case SAPP_KEYCODE_ESCAPE:    ui_palette_close(); break;
    case SAPP_KEYCODE_UP:        Q.step--; break;
    case SAPP_KEYCODE_DOWN:      Q.step++; break;
    case SAPP_KEYCODE_PAGE_UP:   Q.page--; break;
    case SAPP_KEYCODE_PAGE_DOWN: Q.page++; break;
    case SAPP_KEYCODE_ENTER: case SAPP_KEYCODE_KP_ENTER: Q.enter = alt ? 3 : ctrl ? 2 : 1; break;
    default: break;
    }
    app_wake();
}

static bool listed(const cv_cmd* c) { for (int i = 0; i < Q.n; i++) if (Q.list[i] == c) return true; return false; }

/* what the query finds, best first; empty: the recent ones, then the common ones */
static void build(void) {
    Q.n = Q.nrec = 0;
    if (Q.qlen) {
        static const char* label[512], *kw[512], *help[512];
        int n = CV_MIN(cv_cmd_count(), 512), o[MAXROW];
        for (int i = 0; i < n; i++) { const cv_cmd* c = cv_cmd_at(i); label[i] = c->label; kw[i] = c->keywords; help[i] = c->help; }
        int k = cv_fs_rank(Q.q, n, label, kw, help, o, MAXROW);
        for (int i = 0; i < k; i++) Q.list[Q.n++] = cv_cmd_at(o[i]);
    } else {
        Q.n = Q.nrec = cv_cmd_recent(Q.list, NREC);
        const char* const* com = G.loaded ? COMMON : COMMON_EMPTY;
        int nc = G.loaded ? (int)CV_COUNT(COMMON) : (int)CV_COUNT(COMMON_EMPTY);
        for (int i = 0; i < nc && Q.n < MAXROW; i++) {
            const cv_cmd* c = cv_cmd_find(com[i]);
            if (c && !listed(c)) Q.list[Q.n++] = c;
        }
    }
    if (Q.n) {
        Q.sel = ((Q.sel + Q.step) % Q.n + Q.n) % Q.n;
        Q.sel = CV_MIN(CV_MAX(Q.sel + Q.page * PAGE, 0), Q.n - 1);
    } else Q.sel = 0;
    Q.step = Q.page = 0;
}

static bool value_kind(const cv_cmd* c) { return c->kind == CV_CMD_ENUM || c->kind == CV_CMD_INT || c->kind == CV_CMD_FLOAT; }

/* the label's bytes the query's words match: each word's first place in it (letters scattered by the
   fuzzy match are left plain: a highlight all over the label reads as noise) */
static void match_mask(const char* label, char* m, int n) {
    memset(m, 0, (size_t)n);
    const char* w = Q.q;
    while (*w) {
        while (*w == ' ') w++;
        int k = 0;
        while (w[k] && w[k] != ' ') k++;
        if (!k) break;
        int at = -1;
        for (int i = 0; i + k <= n && at < 0; i++) {
            int j = 0;
            while (j < k && tolower((unsigned char)label[i + j]) == tolower((unsigned char)w[j])) j++;
            if (j == k) at = i;
        }
        if (at >= 0) memset(m + at, 1, (size_t)k);
        w += k;
    }
}

static float tw(const struct nk_user_font* f, const char* t, int n) { return f->width(f->userdata, f->height, t, n); }

/* the label, its matched letters in the accent; the width drawn */
static float draw_label(struct nk_command_buffer* cv, const struct nk_user_font* f, float x, float y, const char* l, struct nk_color base) {
    int n = CV_MIN((int)strlen(l), 63);
    char m[64];
    if (Q.qlen) match_mask(l, m, n); else memset(m, 0, sizeof m);
    float x0 = x;
    for (int i = 0; i < n;) {
        int j = i;
        while (j < n && m[j] == m[i]) j++;
        float w = tw(f, l + i, j - i);
        nk_draw_text(cv, nk_rect(x, y, w + 1, f->height), l + i, j - i, f, nk_rgba(0, 0, 0, 0), m[i] ? P.accent_text : base);
        x += w;
        i = j;
    }
    return x - x0;
}

/* at the row's right: a switch for a bool, the value for a list or a number */
static void draw_value(struct nk_command_buffer* cv, const struct nk_user_font* f, struct nk_rect r, const cv_cmd* c, float s, struct nk_color card) {
    char v[48] = "";
    switch (c->kind) {
    case CV_CMD_BOOL: {
        bool on = cv_cmd_geti(c) != 0;
        float h = roundf(14 * s), w = roundf(26 * s), x = r.x + r.w - w, y = r.y + roundf((r.h - h) * 0.5f);
        nk_fill_rect(cv, nk_rect(x, y, w, h), h * 0.5f, on ? P.accent : mix(card, P.dim, 0.45f));
        float k = h - 4 * s;
        nk_fill_circle(cv, nk_rect(on ? x + w - 2 * s - k : x + 2 * s, y + 2 * s, k, k), on ? nk_rgb(255, 255, 255) : mix(card, P.text, 0.8f));
        return; }
    case CV_CMD_ENUM: { int i = cv_cmd_geti(c); if (i >= 0 && i < c->n) snprintf(v, sizeof v, "%s", c->items[i]); break; }
    case CV_CMD_INT:   snprintf(v, sizeof v, "%d", cv_cmd_geti(c)); break;
    case CV_CMD_FLOAT: snprintf(v, sizeof v, "%.3g", cv_cmd_getf(c)); break;
    default: return;
    }
    int n = (int)strlen(v);
    while (n > 1 && tw(f, v, n) > r.w) n--;
    float w = tw(f, v, n);
    nk_draw_text(cv, nk_rect(r.x + r.w - w, r.y + (r.h - f->height) * 0.5f, w + 1, f->height), v, n, f, nk_rgba(0, 0, 0, 0), P.text);
}

/* the selected value row's own control, under it */
static void inline_control(struct nk_context* ctx, const cv_cmd* c) {
    switch (c->kind) {
    case CV_CMD_ENUM: { int v = cv_cmd_geti(c); if (uik_combo(ctx, "value", c->items, c->n, &v, c->help)) { cv_cmd_seti(c, v); cv_cmd_used(c->id); } break; }
    case CV_CMD_INT: {
        int v = cv_cmd_geti(c);
        if (uik_int(ctx, "value", &v, (int)c->lo, (int)c->hi, (int)CV_MAX(c->step, 1.f), c->help)) { cv_cmd_seti(c, v); cv_cmd_used(c->id); }
        break; }
    case CV_CMD_FLOAT: {
        float v = cv_cmd_getf(c);
        bool ch = c->hi - c->lo <= 1000.f ? uik_float(ctx, "value", &v, c->lo, c->hi, c->step, c->help)
                                          : uik_num(ctx, "value", &v, c->lo, c->hi, c->step, c->help);
        if (ch) { cv_cmd_setf(c, v); cv_cmd_used(c->id); }
        break; }
    default: break;
    }
}

static float group_row(struct nk_context* ctx, float h, const char* t) {   /* the row's bottom */
    nk_layout_row_dynamic(ctx, h, 1);
    struct nk_rect r;
    if (!nk_widget(&r, ctx)) return r.y + r.h;
    const struct nk_user_font* f = ctx->style.font;
    nk_draw_text(nk_window_get_canvas(ctx), nk_rect(r.x + 6, r.y + (r.h - f->height) * 0.5f + 1, r.w, f->height), t, (int)strlen(t), f, nk_rgba(0, 0, 0, 0), P.dim);
    return r.y + r.h;
}

/* what a key or a click asked for, done after the window */
static void run(const cv_cmd* c, int mode) {
    if (mode == 3) { cv_cmd_pin(c->id, !cv_cmd_pinned(c->id)); return; }
    bool keep = mode == 2;
    switch (c->kind) {
    case CV_CMD_BOOL:   cv_cmd_seti(c, !cv_cmd_geti(c)); cv_cmd_used(c->id); if (!keep) ui_palette_close(); break;
    case CV_CMD_ACTION: cv_cmd_used(c->id); if (!keep) ui_palette_close(); cv_cmd_run(c); break;
    default:            ui_palette_close(); ui_search_jump(c); break;   /* a go-to, a value: its place */
    }
}

/* before the frame's windows: the palette stays the active window, on top */
void uii_palette_prefocus(struct nk_context* ctx) {
    struct nk_window* w = Q.open ? nk_window_find(ctx, WIN) : NULL;
    if (!w) return;
    if (ctx->end != w || ctx->active != w) nk_window_set_focus(ctx, WIN);
    w->flags &= ~(nk_flags)NK_WINDOW_ROM;
}

void uii_window_palette(struct nk_context* ctx, float s, float row, int fw, int fh) {
    if (!Q.open) return;
    const struct nk_input* in = &ctx->input;
    const float m = roundf(16 * s), pad = roundf(8 * s), eh = roundf(row * 1.35f), rh = roundf(row * 1.2f),
                gh = roundf(row * 0.95f), sp = roundf(9 * s), pinw = roundf(30 * s), vw = roundf(100 * s), rnd = roundf(10 * s);
    const float W = (float)fw, H = (float)fh;
    struct nk_window* win = nk_window_find(ctx, WIN);
    bool popup = win && win->popup.win && win->popup.active;
    static struct nk_rect card;
    if (win && !popup && (nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT) || nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT) ||
                          nk_input_is_mouse_pressed(in, NK_BUTTON_MIDDLE)) &&
        !NK_INBOX(in->mouse.pos.x, in->mouse.pos.y, card.x, card.y, card.w, card.h)) { ui_palette_close(); return; }   /* a click outside */

    build();
    const cv_cmd* sel = Q.n ? Q.list[Q.sel] : NULL;
    bool ctl = sel && value_kind(sel);
    static float ctl_h = -1;                               /* the inline control's height, as measured */
    if (ctl_h < 0) ctl_h = row + 2 * pad;
    float h = eh + sp + (Q.n ? Q.n * rh : rh) + (ctl ? ctl_h : 0) + sp + row;
    if (!Q.qlen) h += (Q.nrec ? gh : 0) + (Q.n > Q.nrec ? gh : 0);
    float w = CV_MIN(roundf(560 * s), W - 2 * m);
    card = nk_rect(roundf((W - w) * 0.5f), roundf(H * 0.15f), w, h + 2 * pad);
    struct nk_rect r = nk_rect(card.x - m, card.y - m, card.w + 2 * m, card.h + 2 * m);

    struct nk_style* st = &ctx->style;
    struct nk_color bg = st->window.fixed_background.data.color;
    bg.a = 255;
    bool light = (bg.r * 0.2126f + bg.g * 0.7152f + bg.b * 0.0722f) / 255.f >= 0.5f;
    nk_style_push_style_item(ctx, &st->window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    nk_style_push_float(ctx, &st->window.border, 0);
    nk_style_push_vec2(ctx, &st->window.padding, nk_vec2(m + pad, m + pad));
    nk_style_push_vec2(ctx, &st->window.spacing, nk_vec2(roundf(4 * s), 0));
    const cv_cmd* pend = NULL;
    int mode = 0;
    if (nk_begin(ctx, WIN, r, NK_WINDOW_NO_SCROLLBAR)) {
        nk_window_set_bounds(ctx, WIN, r);
        struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
        const struct nk_user_font* f = st->font;
        struct nk_rect clip = cv->clip;
        nk_push_scissor(cv, r);
        for (int k = 6; k >= 1; k--) {                     /* a soft shadow, a little below */
            float e = m * k / 6.f;
            nk_fill_rect(cv, nk_rect(card.x - e, card.y - e + 4 * s, card.w + 2 * e, card.h + 2 * e), rnd + e,
                         nk_rgba(0, 0, 0, light ? 7 : 12));
        }
        nk_fill_rect(cv, card, rnd, bg);
        nk_stroke_rect(cv, card, rnd, 1.f, light ? nk_rgba(0, 0, 0, 50) : nk_rgba(255, 255, 255, 36));
        nk_push_scissor(cv, clip);

        /* the box */
        nk_layout_row_dynamic(ctx, eh, 1);
        bool other = win && (win->property.active || popup);
        if (Q.focus || (win && !win->edit.active && !other && !nk_input_is_mouse_down(in, NK_BUTTON_LEFT))) {
            nk_edit_focus(ctx, NK_EDIT_FIELD);
            if (win) { win->edit.cursor = win->edit.sel_start = win->edit.sel_end = Q.qlen; Q.focus = false; }   /* typing goes on at the end */
        }
        struct nk_rect eb = nk_widget_bounds(ctx);
        uii_test_mark(ctx, "#palette box");
        int was = Q.qlen;
        nk_flags ev = nk_edit_string(ctx, NK_EDIT_FIELD, Q.q, &Q.qlen, (int)sizeof Q.q - 1, nk_filter_default);
        Q.q[Q.qlen] = 0;
        Q.typing = (ev & NK_EDIT_ACTIVE) != 0;
        if (!Q.qlen) {
            const char* hint = "Type a setting or a command";
            float px = st->edit.padding.x + st->edit.border + 2 * s;
            nk_draw_text(cv, nk_rect(eb.x + px, eb.y + (eb.h - f->height) * 0.5f, eb.w - px, f->height), hint, (int)strlen(hint), f, nk_rgba(0, 0, 0, 0), P.dim);
        }
        if (Q.qlen != was) { Q.sel = 0; build(); sel = Q.n ? Q.list[0] : NULL; ctl = sel && value_kind(sel); }
        nk_layout_row_dynamic(ctx, sp, 1);
        struct nk_rect lr;
        if (nk_widget(&lr, ctx)) nk_stroke_line(cv, card.x, lr.y + sp * 0.5f, card.x + card.w, lr.y + sp * 0.5f, 1.f, light ? nk_rgba(0, 0, 0, 30) : nk_rgba(255, 255, 255, 24));

        /* the list */
        const float full = card.w - 2 * pad;
        bool moved = in->mouse.delta.x != 0 || in->mouse.delta.y != 0;
        bool ctrl = nk_input_is_key_down(in, NK_KEY_CTRL);
        for (int i = 0; i < Q.n; i++) {
            const cv_cmd* c = Q.list[i];
            if (!Q.qlen && i == 0 && Q.nrec) group_row(ctx, gh, "recently used");
            if (!Q.qlen && i == Q.nrec) group_row(ctx, gh, "suggested");
            nk_layout_row_template_begin(ctx, rh);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, pinw);
            nk_layout_row_template_end(ctx);
            struct nk_rect b;
            tip(ctx, c->help);
            if (!nk_widget(&b, ctx)) { nk_spacing(ctx, 1); continue; }
            struct nk_rect R = nk_rect(b.x, b.y, full, b.h);
            bool over = nk_input_is_mouse_hovering_rect(in, R) && !popup;
            if (over && moved) Q.sel = i;
            bool on = i == Q.sel;
            if (on) {                                      /* a neutral tint and an accent bar: the matched letters stay legible */
                nk_fill_rect(cv, R, roundf(5 * s), mix(bg, P.text, light ? 0.10f : 0.12f));
                nk_fill_rect(cv, nk_rect(R.x, R.y + roundf(5 * s), roundf(3 * s), R.h - roundf(10 * s)), roundf(1.5f * s), P.accent);
            }
            float ty = b.y + (b.h - f->height) * 0.5f, x = b.x + roundf(8 * s);
            float lw = draw_label(cv, f, x, ty, c->label, P.text);
            struct nk_rect vr = nk_rect(b.x + b.w - vw - roundf(6 * s), b.y, vw, b.h);
            draw_value(cv, f, vr, c, s, bg);
            char path[160];
            cv_cmd_path(c, path, sizeof path);
            float pw = tw(f, path, (int)strlen(path)), room = vr.x - roundf(14 * s) - (x + lw + roundf(18 * s));
            if (pw > room) { snprintf(path, sizeof path, "%s", c->section); pw = tw(f, path, (int)strlen(path)); }
            if (pw <= room) nk_draw_text(cv, nk_rect(vr.x - roundf(14 * s) - pw, ty, pw + 1, f->height), path, (int)strlen(path), f, nk_rgba(0, 0, 0, 0), P.dim);
            if (!popup && nk_input_mouse_clicked(in, NK_BUTTON_LEFT, b)) {
                Q.sel = i;
                if (!value_kind(c)) { pend = c; mode = ctrl ? 2 : 1; }   /* a value row: selected, its control shows */
            }
            /* the pin, at the row's end: shown when pinned or on the row */
            struct nk_rect pr;
            tip(ctx, "Pin to the top bar (Alt+Enter)");
            if (nk_widget(&pr, ctx)) {
                bool pin = cv_cmd_pinned(c->id), po = nk_input_is_mouse_hovering_rect(in, pr) && !popup;
                if (pin || on || over) {
                    const char* t = "pin";
                    float w2 = tw(f, t, 3);
                    nk_draw_text(cv, nk_rect(pr.x + (pr.w - w2) * 0.5f, ty, w2 + 1, f->height), t, 3, f, nk_rgba(0, 0, 0, 0),
                                 pin ? P.accent_text : po ? P.text : P.dim);
                }
                if (!popup && nk_input_mouse_clicked(in, NK_BUTTON_LEFT, pr)) { pend = c; mode = 3; }
            }
            if (on && ctl && c == sel) {
                nk_layout_row_dynamic(ctx, pad, 1); nk_spacing(ctx, 1);
                inline_control(ctx, c);
                nk_layout_row_dynamic(ctx, pad, 1); nk_spacing(ctx, 1);
            }
        }
        if (!Q.n) group_row(ctx, rh, "nothing found");
        nk_layout_row_dynamic(ctx, sp, 1);
        if (nk_widget(&lr, ctx)) nk_stroke_line(cv, card.x, lr.y + sp * 0.5f, card.x + card.w, lr.y + sp * 0.5f, 1.f, light ? nk_rgba(0, 0, 0, 30) : nk_rgba(255, 255, 255, 24));
        float bot = group_row(ctx, row, "Enter run    Ctrl+Enter keep open    Alt+Enter pin    Esc close");
        if (ctl && fabsf(bot + pad - (card.y + card.h)) > 0.5f) { ctl_h += bot + pad - (card.y + card.h); app_wake(); }   /* fits next frame */
        if (Q.enter && Q.n && !pend) { pend = Q.list[Q.sel]; mode = Q.enter; }
        Q.enter = 0;
    }
    nk_end(ctx);
    nk_style_pop_vec2(ctx);
    nk_style_pop_vec2(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
    if (pend) run(pend, mode);
}
