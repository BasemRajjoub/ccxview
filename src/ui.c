/* ui.c -- the whole interface, frame by frame. ui_frame lays out the windows;
   what they hold lives in files of their own, sharing what ui_int.h declares:
     ui_style.c    theme, palette, font, scale and zoom
     ui_panels.c   the scene sidebar (left): file, layers, groups and sets, fields, export
     ui_view.c     the sidebar's View section: camera, colours, symmetry, cuts, symbol sizes
     ui_bars.c     toolbar, time bar, status bar, legend and its settings, axes gizmo
     ui_windows.c  messages, formula builder, probe, find, overlay, navigation mark, file browser, drop hint
     ui_plots.c    linearization, path, history and convergence plots
   This file also answers the input focus queries and holds the small widget helpers. */
#include "app.h"
#include "ui.h"
#include "cfg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"

/* ---- small helpers ----------------------------------------------------------- */

/* Tooltips are drawn by us, over everything, at the end of the frame (tip_draw),
   not as Nuklear popups: a window has room for one popup, so a tooltip opened
   before its widget kept a list under it from opening, and a window with a popup
   open takes no input, the wheel included. Here a tooltip is only ink. */
static char tip_text[400], tip_last[400];

/* a tooltip this frame; the last one asked for wins */
void tip_show(struct nk_context* ctx, const char* text) {
    (void)ctx;
    snprintf(tip_text, sizeof tip_text, "%s", text);
}
const char* uii_tip_shown(void) { return tip_last; }

/* the tooltip asked for this frame, beside the mouse and inside the window; none
   while a button is down or the wheel turns */
static void tip_draw(struct nk_context* ctx, float s, float fw, float fh) {
    const struct nk_input* in = &ctx->input;
    bool busy = in->mouse.buttons[NK_BUTTON_LEFT].down || in->mouse.buttons[NK_BUTTON_RIGHT].down ||
                in->mouse.buttons[NK_BUTTON_MIDDLE].down || in->mouse.scroll_delta.x != 0 || in->mouse.scroll_delta.y != 0;
    snprintf(tip_last, sizeof tip_last, "%s", busy ? "" : tip_text);
    tip_text[0] = 0;
    if (!tip_last[0]) return;
    const struct nk_user_font* f = ctx->style.font;
    const struct nk_style_window* st = &ctx->style.window;
    float pad = 6 * s, lh = f->height + 3 * s, w = 0;
    int lines = 0;
    for (const char* a = tip_last; *a; lines++) {
        int n = (int)strcspn(a, "\n");
        w = CV_MAX(w, f->width(f->userdata, f->height, a, n));
        a += n + (a[n] == '\n');
    }
    w += 2 * pad;
    float h = lines * lh + 2 * pad - 3 * s;
    float x = in->mouse.pos.x + 14 * s, y = in->mouse.pos.y + 20 * s;
    if (x + w > fw - 2) x = CV_MAX(fw - 2 - w, 2);
    if (y + h > fh - 2) y = CV_MAX(in->mouse.pos.y - 8 * s - h, 2);       /* above the mouse, never under it */
    struct nk_command_buffer* cv = cv_nk_overlay_begin(ctx);
    nk_fill_rect(cv, nk_rect(x, y, w, h), 4 * s, st->background);
    nk_stroke_rect(cv, nk_rect(x, y, w, h), 4 * s, 1, st->tooltip_border_color);
    float ty = y + pad;
    for (const char* a = tip_last; *a; ty += lh) {
        int n = (int)strcspn(a, "\n");
        nk_draw_text(cv, nk_rect(x + pad, ty, w - 2 * pad, f->height), a, n, f, st->background, ctx->style.text.color);
        a += n + (a[n] == '\n');
    }
    cv_nk_overlay_end(ctx);
}

/* Nuklear's nk_begin makes a background window that takes input the active one
   on every frame it is not clicked (iter is the window itself there), so the
   window really in use loses the focus: its panel no longer scrolls with the
   wheel. Give it back; a click on the background window (click_focus) still
   makes that one active. */
bool begin_background(struct nk_context* ctx, const char* name, struct nk_rect r, nk_flags flags) {
    struct nk_window* was = ctx->active;
    bool open = nk_begin(ctx, name, r, flags | NK_WINDOW_BACKGROUND);
    if (was && was != ctx->current && ctx->active == ctx->current && !(was->flags & NK_WINDOW_HIDDEN))
        ctx->active = was;
    return open;
}

/* tooltip for the widget laid out next; none from a window under its own open list */
void tip(struct nk_context* ctx, const char* text) {
    uii_test_mark(ctx, text);
    if (ctx->current && ctx->current->popup.win && ctx->current->popup.active) return;
    if (nk_widget_is_hovered(ctx)) tip_show(ctx, text);
}

/* separators: a faint line across the middle of a cell, or of a thin row */
void uii_vsep(struct nk_context* ctx) {
    struct nk_rect r;
    if (!nk_widget(&r, ctx)) return;
    float x = roundf(r.x + r.w * 0.5f) + 0.5f, m = r.h * 0.18f;
    nk_stroke_line(nk_window_get_canvas(ctx), x, r.y + m, x, r.y + r.h - m, 1.f,
                   mix(P.dim, ctx->style.window.background, 0.45f));
}
void uii_hsep(struct nk_context* ctx, float s) {
    struct nk_rect r;
    nk_layout_row_dynamic(ctx, roundf(7 * s), 1);
    if (!nk_widget(&r, ctx)) return;
    float y = roundf(r.y + r.h * 0.5f) + 0.5f;
    nk_stroke_line(nk_window_get_canvas(ctx), r.x, y, r.x + r.w, y, 1.f,
                   mix(P.dim, ctx->style.window.background, 0.45f));
}

bool g_focus_open;          /* Ctrl+L: put the cursor in the path box */
void ui_focus_open(void) { g_focus_open = true; }

/* Whether a Nuklear window under the mouse should keep the event from the 3D
   view. An empty drop hint and the overlays only display, so they let everything
   through; the legend and the axes gizmo take the buttons (they are dragged by
   any point, see overlay_drag) but not the wheel. */
bool ui_text_focus(struct nk_context* ctx) {
    for (struct nk_window* w = ctx->begin; w; w = w->next)
        if (!(w->flags & NK_WINDOW_HIDDEN) && (w->edit.active || w->property.active)) return true;
    return false;
}

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
   being clicked. Make the one under the wheel active, as a click would (see
   click_focus), so any window, now or later, scrolls without a click first. The
   top one under the mouse; an open popup or combo keeps the wheel, and hints,
   overlays and the gizmo take none. */
void ui_wheel_focus(struct nk_context* ctx) {
    float mx = ctx->input.mouse.pos.x, my = ctx->input.mouse.pos.y;
    for (struct nk_window* w = ctx->begin; w; w = w->next) {
        const struct nk_window* p = w->popup.active && w->popup.type != NK_PANEL_TOOLTIP ? w->popup.win : NULL;
        if (p && !(w->flags & NK_WINDOW_HIDDEN) && NK_INBOX(mx, my, p->bounds.x, p->bounds.y, p->bounds.w, p->bounds.h))
            return;
    }
    for (struct nk_window* w = ctx->end; w; w = w->prev) {
        if (w->flags & (NK_WINDOW_HIDDEN | NK_WINDOW_CLOSED | NK_WINDOW_NO_INPUT)) continue;
        if (!NK_INBOX(mx, my, w->bounds.x, w->bounds.y, w->bounds.w, w->bounds.h)) continue;
        if (w->flags & NK_WINDOW_BACKGROUND) return;
        if (w != ctx->active || (w->flags & NK_WINDOW_ROM)) {
            nk_window_set_focus(ctx, w->name_string);
            w->flags &= ~(nk_flags)NK_WINDOW_ROM;
        }
        return;
    }
}

void fmt_num(char* out, size_t n, double v) {
    if (v != v) { snprintf(out, n, "-"); return; }
    double a = fabs(v);
    if (a != 0 && (a < 1e-3 || a >= 1e5)) snprintf(out, n, "%.3e", v);
    else snprintf(out, n, "%.4g", v);
}

/* a plot axis tick: enough digits that ticks `step` apart read differently */
void tick_num(char* out, size_t n, double v, double step, double big) {
    int sig = step > 0 && big > 0 ? (int)ceil(log10(big / step)) + 1 : 4;
    sig = sig < 3 ? 3 : sig > 8 ? 8 : sig;
    snprintf(out, n, "%.*g", sig, v);
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
bool ui_slider_float(struct nk_context* ctx, float lo, float* v, float hi, float step) {
    bool j = slider_jump(ctx, lo, v, hi, step);
    return nk_slider_float(ctx, lo, v, hi, step) || j;
}
bool ui_slider_int(struct nk_context* ctx, int lo, int* v, int hi, int step) {
    float f = (float)*v;
    bool j = slider_jump(ctx, (float)lo, &f, (float)hi, (float)step);
    *v = (int)lroundf(f);
    return nk_slider_int(ctx, lo, v, hi, step) || j;
}

/* a sub-section of a panel section, open or closed as it was left (saved in the settings) */
bool sub_push(struct nk_context* ctx, const char* title, int t) {
    return nk_tree_state_push(ctx, NK_TREE_NODE, title, (enum nk_collapse_states*)&G.tree[t]);
}

/* ---- frame ------------------------------------------------------------------------ */

/* A press goes to the topmost window under the mouse that takes input. Nuklear
   activates a clicked window only when no window above it covers the point, and
   counts the display-only overlays (NO_INPUT) as covering: once a closed window
   has left a panel read only, a click on it under an overlay never woke it again. */
static void click_focus(struct nk_context* ctx) {
    const struct nk_input* in = &ctx->input;
    if (!nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT) && !nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT) &&
        !nk_input_is_mouse_pressed(in, NK_BUTTON_MIDDLE)) return;
    float mx = in->mouse.pos.x, my = in->mouse.pos.y;
    for (struct nk_window* w = ctx->begin; w; w = w->next) {          /* an open popup has it */
        const struct nk_window* p = w->popup.active && w->popup.type != NK_PANEL_TOOLTIP ? w->popup.win : NULL;
        if (p && !(w->flags & NK_WINDOW_HIDDEN) && NK_INBOX(mx, my, p->bounds.x, p->bounds.y, p->bounds.w, p->bounds.h))
            return;
    }
    for (struct nk_window* w = ctx->end; w; w = w->prev) {
        if (w->flags & (NK_WINDOW_HIDDEN | NK_WINDOW_CLOSED | NK_WINDOW_NO_INPUT)) continue;
        if (!NK_INBOX(mx, my, w->bounds.x, w->bounds.y, w->bounds.w, w->bounds.h)) continue;
        if (w != ctx->active || (w->flags & NK_WINDOW_ROM)) {
            if (!(w->flags & NK_WINDOW_BACKGROUND)) nk_window_set_focus(ctx, w->name_string);
            else ctx->active = w;
            w->flags &= ~(nk_flags)NK_WINDOW_ROM;
        }
        return;
    }
}

void ui_frame(struct nk_context* ctx, int fw, int fh) {
    /* Nuklear, closing a window, hands the focus to the one below without raising
       it; once another window is added on top, that one stays active but read only
       and its buttons ignore clicks. Raise it. */
    if (ctx->active && ctx->active != ctx->end && (ctx->active->flags & NK_WINDOW_ROM) &&
        !(ctx->active->flags & (NK_WINDOW_BACKGROUND | NK_WINDOW_HIDDEN | NK_WINDOW_NO_INPUT))) {
        struct nk_window* w = ctx->active;
        nk_window_set_focus(ctx, w->name_string);
        w->flags &= ~(nk_flags)NK_WINDOW_ROM;
    }
    click_focus(ctx);
    apply_scale(ctx);
    const float s = U.scale;
    const float row = U.px + 10.f * s;
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
    window_legend(ctx, s, row);

    r = nk_rect(0, H - status_h, W, status_h);
    if (!G.hide_panels && nk_begin(ctx, "Status", r, fixed)) {
        nk_window_set_bounds(ctx, "Status", r);
        panel_status(ctx, s, row, r.w - 2 * ctx->style.window.padding.x);
    }
    if (!G.hide_panels) nk_end(ctx);

    drop_hint(ctx, s, row);
    window_axes(ctx, s);
    window_probe(ctx, s, row);
    window_details(ctx, s, row, fw, fh);
    window_about(ctx, s, row, fw, fh);
    window_menu(ctx, s, row, fw, fh);                 /* last: over every other window */
    window_messages(ctx, s, row, fw, fh);
    window_calc_help(ctx, s, row, fw, fh);
    window_units(ctx, s, row, fw, fh);
    window_failure(ctx, s, row, fw, fh);
    window_mesh(ctx, s, row, fw, fh);
    window_convergence(ctx, s, row, fw, fh);
    window_legend_settings(ctx, s, row);
    window_overlay(ctx, s);
    window_nav(ctx, s);
    window_find(ctx, s, row);
    window_path(ctx, s, row, fw, fh);
    window_history(ctx, s, row, fw, fh);
    window_browser(ctx, s, row, fw, fh);
    tip_draw(ctx, s, W, H);
}
