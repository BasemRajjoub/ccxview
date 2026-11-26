/* ui_kit.c -- the sidebar's UI kit: every row of a panel is one of these helpers, so labels, controls
   and spacing line up everywhere. A label sits in a fixed column (88 px x scale), its control fills the
   rest; one row height (U.px + 10 s), one spacing (Nuklear's). Every helper takes the tooltip text last
   and shows it on the control (and on its label); each returns true when the value changed.

   Which helper for which row
     header of a panel section ........... uik_section(ctx, "View", CV_TREE_VIEW) ... uik_end(ctx)
     sub-section (a tree node) ............ uik_sub(ctx, "Camera", CV_TREE_CAMERA)   ... uik_end(ctx)
     a light heading inside a section ..... uik_group(ctx, "Orbit")   (a label and a thin line, no nesting)
     one checkbox, full width ............. uik_check(ctx, "Axes gizmo", &on, tip)
     two checkboxes on a row .............. uik_check2(ctx, "a", &a, tipa, "b", &b, tipb)
     label + drop-down .................... uik_combo(ctx, "Backdrop", names, n, &cur, tip)   (label NULL: no label)
     a drop-down in a cell you laid out ... uik_combo_cell(ctx, names, n, &cur, tip, mark or NULL)
     the same, a name for ui-test .......... uik_combo_m(ctx, label, names, n, &cur, tip, "name")   (found as "#name"); uik_check_combo_m: the same, the mark on the drop-down
     checkbox + drop-down on a row ........ uik_check_combo(ctx, "Clip plane", &on, names, n, &cur, tipc, tipd)
     label + slider with its value ........ uik_float(ctx, "exposure", &v, lo, hi, step, tip)
     label + integer slider ............... uik_slider_i(ctx, "UI font", &i, lo, hi, "15 px" or NULL, tip)
     label + number box (drag, arrows) .... uik_num(ctx, "gap", &v, lo, hi, step, tip) / uik_int(...)
     label + 2 or 3 number boxes .......... uik_nums(ctx, "X", 2, v, lo, hi, step, tips)   (v[n], tips[n] or NULL)
     label + colour swatch (picker) ....... uik_color(ctx, "Background", rgb3, tip)
     a family of on/off filters ......... if (uim_on(ctx)) uik_chips(ctx, n, labels, ptrs, tips); else <the boxes>
     1 to 7 equal buttons ................. i = uik_buttons(ctx, NULL, n, labels, tips)  (-1: none clicked;
                                            a label first puts them right of the label column)
     n equal cells, your own widgets ...... uik_cols(ctx, n)   (the kit's row height; tip() before each widget)
     label + n cells + a static one ....... uik_label_cells(ctx, "text", tip, n, extra_px)   (as uik_label_row, n dynamic cells)
     a field and its button ............... uik_wide_row(ctx, 56) then the two widgets (the static cell is 56 x scale)
     a dim line of text ................... uik_note(ctx, "render view is off")
     clickable dim text (a link) .......... if (uik_link(ctx, "View > Camera", tip)) ...   (underlined on hover)
     a small toggle closing a row ......... uik_trail(ctx, 36); <any helper above that builds its row>; uik_trail_toggle(ctx, &on, tip)
                                            (the helper's row gets one more static cell; the toggle fills it)
     label + something else ............... uik_label_row(ctx, "label", tip, extra_px) then the widgets: the
                                            dynamic cell, then one static cell of extra_px x scale
                                            (extra_px 0: only the dynamic cell). tip() before each widget.
   Modern widgets (G.ui_modern, ui_modern.c) come through the same helpers: a check box is a switch
   (every nk_checkbox_label of the UI, by a macro in ui_int.h), a list of 2 to 4 short choices that fit
   is a segmented control, uik_float / uik_slider_i put the label and value inside the track, number
   boxes show - and +. A choice laid out by hand: if (uim_on(ctx)) uim_segments_t(...) else <the options>.
   A row that needs a tooltip on a plain Nuklear widget still calls tip(ctx, text) right before it.
   Never nk_layout_row_template_* / nk_layout_row_dynamic in a panel: pick a helper, or uik_label_row.

   Before / after
     nk_layout_row_template_begin(ctx, row);  nk_layout_row_template_push_static(ctx, 84 * s);
     nk_layout_row_template_push_dynamic(ctx);  nk_layout_row_template_end(ctx);
     nk_label(ctx, "Backdrop", NK_TEXT_LEFT);  tip(ctx, "The view's background");
     G.bg_mode = nk_combo(ctx, names, N, G.bg_mode, (int)row, nk_vec2(180 * s, 5 * row + 20 * s));
   becomes   uik_combo(ctx, "Backdrop", names, N, &G.bg_mode, "The view's background");

     nk_layout_row_dynamic(ctx, row, 2);
     tip(ctx, "A floor");   nk_checkbox_label(ctx, "Floor", &G.look_floor);
     tip(ctx, "Shadows");   nk_checkbox_label(ctx, "Shadows", &G.look_shadow);
   becomes   uik_check2(ctx, "Floor", &G.look_floor, "A floor", "Shadows", &G.look_shadow, "Shadows");

     nk_layout_row_template_begin(ctx, row); ... label + dynamic ...;  nk_label(ctx, "exposure", NK_TEXT_LEFT);
     tip(ctx, "The light's scale");  nk_slider_float(ctx, 0.5f, &G.look_exposure, 2.f, 0.01f);
   becomes   uik_float(ctx, "exposure", &G.look_exposure, 0.5f, 2.f, 0.01f, "The light's scale");

   The kit does not touch what a row does: keep the code that follows a change as it was. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"
#include "app_cmds.h"

static float RH(void) { return U.px + 10.f * U.scale; }                /* the row height */
/* the label column: its right edge is the same x at every nesting depth (a sub-section's tree node
   indents its rows, so the column gets that much narrower); depth 1 is a section's own rows */
static float LW(struct nk_context* ctx) {
    int d = ctx->current && ctx->current->layout ? ctx->current->layout->row.tree_depth : 0;
    float w = 88.f * U.scale - (d > 1 ? (d - 1) * ctx->style.tab.indent : 0);
    return CV_MAX(w, 40.f * U.scale);
}

static float trail;                       /* px (x scale) of a static cell that closes the next row (uik_trail) */
void uik_trail(struct nk_context* ctx, float px) { (void)ctx; trail = px; }
static float take_trail(void) { float t = trail * U.scale; trail = 0; return t; }

/* a tooltip, when there is one */
static void tipn(struct nk_context* ctx, const char* text) { if (text) tip(ctx, text); }

/* the tooltip of a label: shown on hover, not offered to the interface test (the control is) */
static void hover(struct nk_context* ctx, const char* text) {
    if (!text || (ctx->current && ctx->current->popup.win && ctx->current->popup.active)) return;
    if (nk_widget_is_hovered(ctx)) tip_show(ctx, text);
}

/* a row: the label column, n dynamic cells, and when extra > 0 a static cell of extra px (x scale) */
void uik_label_cells(struct nk_context* ctx, const char* label, const char* help, int n, float extra) {
    nk_layout_row_template_begin(ctx, RH());
    nk_layout_row_template_push_static(ctx, LW(ctx));
    for (int i = 0; i < n; i++) nk_layout_row_template_push_dynamic(ctx);
    if (extra > 0) nk_layout_row_template_push_static(ctx, extra * U.scale);
    float t = take_trail();
    if (t > 0) nk_layout_row_template_push_static(ctx, t);
    nk_layout_row_template_end(ctx);
    hover(ctx, help);
    nk_label(ctx, label, NK_TEXT_LEFT);
}
void uik_label_row(struct nk_context* ctx, const char* label, const char* help, float extra) {
    uik_label_cells(ctx, label, help, 1, extra);
}

/* a row: a dynamic cell, then one static cell of extra px (x scale): a field and its button */
void uik_wide_row(struct nk_context* ctx, float extra) {
    nk_layout_row_template_begin(ctx, RH());
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, extra * U.scale);
    nk_layout_row_template_end(ctx);
}

static void row_n(struct nk_context* ctx, int n) {
    float t = n == 1 ? take_trail() : 0;
    if (t <= 0) { nk_layout_row_dynamic(ctx, RH(), n); return; }
    nk_layout_row_template_begin(ctx, RH());
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, t);
    nk_layout_row_template_end(ctx);
}
void uik_cols(struct nk_context* ctx, int n) { row_n(ctx, n); }

bool uik_section(struct nk_context* ctx, const char* title, int tree) {
    struct nk_rect b = nk_widget_bounds(ctx);
    uii_jump_scroll(ctx, tree);
    bool open = nk_tree_state_push(ctx, NK_TREE_TAB, title, (enum nk_collapse_states*)&G.tree[tree]);
    uii_jump_flash(ctx, tree, b);
    return open;
}
bool uik_sub(struct nk_context* ctx, const char* title, int tree) {
    struct nk_rect b = nk_widget_bounds(ctx);
    uii_jump_scroll(ctx, tree);
    bool open = sub_push(ctx, title, tree);
    uii_jump_flash(ctx, tree, b);
    return open;
}

/* a value changed by its control: it goes to the search box's Recent list */
static bool used(bool ch, const void* p) { if (ch) cv_cmd_used_ptr(p); return ch; }
void uik_end(struct nk_context* ctx) { nk_tree_state_pop(ctx); }

/* a heading: its text in the dim colour, a thin line to the right */
void uik_group(struct nk_context* ctx, const char* title) {
    const struct nk_user_font* f = ctx->style.font;
    float w = f->width(f->userdata, f->height, title, (int)strlen(title)) + 8 * U.scale;
    nk_layout_row_template_begin(ctx, RH());
    nk_layout_row_template_push_static(ctx, w);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    nk_label_colored(ctx, title, NK_TEXT_LEFT, P.dim);
    struct nk_rect r;
    if (!nk_widget(&r, ctx)) return;
    float y = roundf(r.y + r.h * 0.5f) + 0.5f;
    nk_stroke_line(nk_window_get_canvas(ctx), r.x, y, r.x + r.w, y, 1.f, mix(P.dim, ctx->style.window.background, 0.45f));
}

void uik_note(struct nk_context* ctx, const char* text) {
    row_n(ctx, 1);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, P.dim);
}

/* dim text that acts as a link: brighter and underlined under the mouse; true when clicked */
bool uik_link(struct nk_context* ctx, const char* text, const char* help) {
    row_n(ctx, 1);
    return uik_link_cell(ctx, text, help, P.dim, NULL);
}

bool uik_link_cell(struct nk_context* ctx, const char* text, const char* help, struct nk_color idle, bool* hovered) {
    struct nk_rect r;
    if (hovered) *hovered = false;
    tipn(ctx, help);
    if (!nk_widget(&r, ctx)) return false;
    bool over = nk_input_is_mouse_hovering_rect(&ctx->input, r) && !(ctx->current->popup.win && ctx->current->popup.active);
    if (hovered) *hovered = over;
    const struct nk_user_font* f = ctx->style.font;
    struct nk_color c = over ? P.text : idle;
    float px = ctx->style.text.padding.x, tw = f->width(f->userdata, f->height, text, (int)strlen(text));
    nk_draw_text(nk_window_get_canvas(ctx), nk_rect(r.x + px, r.y + (r.h - f->height) * 0.5f, r.w - px, f->height), text, (int)strlen(text), f, nk_rgba(0, 0, 0, 0), c);
    if (over) {
        float y = r.y + (r.h + f->height) * 0.5f + 1;
        nk_stroke_line(nk_window_get_canvas(ctx), r.x + px, y, r.x + px + CV_MIN(tw, r.w - px), y, 1.f, c);
    }
    return over && nk_input_mouse_clicked(&ctx->input, NK_BUTTON_LEFT, r);
}

/* a file just saved, named in the status bar or the messages: a click opens it with the system's
   app (picture viewer, PDF reader ...); plain text on the web, where it went to the downloads */
bool uik_file_link(struct nk_context* ctx, const char* text, const char* path, bool* hovered) {
#ifdef __EMSCRIPTEN__
    (void)path;
    if (hovered) *hovered = false;
    nk_label(ctx, text, NK_TEXT_LEFT);
    return false;
#else
    if (!uik_link_cell(ctx, text, "Open it with the app the system has for it", P.accent_text, hovered)) return false;
    if (!cv_open_file(path)) {
        snprintf(G.note, sizeof G.note, "could not open %s", path);
        G.note_t = cv_now();
    }
    return true;
#endif
}

/* the small toggle in the cell uik_trail made: "pin" in text, dim, brighter on hover, accent when on */
bool uik_trail_toggle(struct nk_context* ctx, nk_bool* on, const char* label, const char* help) {
    struct nk_rect r;
    tipn(ctx, help);
    if (!nk_widget(&r, ctx)) return false;
    bool over = nk_input_is_mouse_hovering_rect(&ctx->input, r) && !(ctx->current->popup.win && ctx->current->popup.active);
    const struct nk_user_font* f = ctx->style.font;
    struct nk_color c = *on ? P.accent_text : over ? P.text : P.dim;
    float tw = f->width(f->userdata, f->height, label, (int)strlen(label));
    nk_draw_text(nk_window_get_canvas(ctx), nk_rect(r.x + (r.w - tw) * 0.5f, r.y + (r.h - f->height) * 0.5f, tw + 2, f->height), label, (int)strlen(label), f, nk_rgba(0, 0, 0, 0), c);
    bool hit = over && nk_input_mouse_clicked(&ctx->input, NK_BUTTON_LEFT, r);
    if (hit) *on = !*on;
    return hit;
}

bool uik_check(struct nk_context* ctx, const char* label, nk_bool* v, const char* help) {
    row_n(ctx, 1);
    tipn(ctx, help);
    return used(nk_checkbox_label(ctx, label, v), v);
}

bool uik_check2(struct nk_context* ctx, const char* la, nk_bool* a, const char* ta, const char* lb, nk_bool* b, const char* tb) {
    row_n(ctx, 2);
    tipn(ctx, ta);
    bool ch = nk_checkbox_label(ctx, la, a);
    tipn(ctx, tb);
    return used(nk_checkbox_label(ctx, lb, b), b) || used(ch, a);
}

static bool combo(struct nk_context* ctx, const char* const* items, int n, int* cur, const char* help, const char* mark) {
    struct nk_rect b = nk_widget_bounds(ctx);
    tipn(ctx, help);
    if (mark) uii_test_mark(ctx, mark);
    if (uim_seg_fits(ctx, items, n)) return used(uim_segments(ctx, items, n, cur), cur);   /* a few short choices */
    int k = nk_combo(ctx, (const char**)items, n, *cur, (int)RH(), nk_vec2(CV_MAX(b.w, 120.f * U.scale), n * RH() + 20 * U.scale));
    bool ch = k != *cur;
    *cur = k;
    return used(ch, cur);
}

/* a drop-down in the cell laid out next (a toolbar's or a row of your own) */
bool uik_combo_cell(struct nk_context* ctx, const char* const* items, int n, int* cur, const char* help, const char* mark) {
    return combo(ctx, items, n, cur, help, mark);
}

/* mark: a name for the interface test (found as "#name"), or NULL */
bool uik_combo_m(struct nk_context* ctx, const char* label, const char* const* items, int n, int* cur, const char* help, const char* mark) {
    if (label) uik_label_row(ctx, label, help, 0); else row_n(ctx, 1);
    return combo(ctx, items, n, cur, help, mark);
}
bool uik_combo(struct nk_context* ctx, const char* label, const char* const* items, int n, int* cur, const char* help) {
    return uik_combo_m(ctx, label, items, n, cur, help, NULL);
}

bool uik_check_combo_m(struct nk_context* ctx, const char* label, nk_bool* on, const char* const* items, int n, int* cur,
                       const char* tip_check, const char* tip_combo, const char* mark) {
    row_n(ctx, 2);
    tipn(ctx, tip_check);
    bool ch = nk_checkbox_label(ctx, label, on);
    return combo(ctx, items, n, cur, tip_combo, mark) || used(ch, on);
}
bool uik_check_combo(struct nk_context* ctx, const char* label, nk_bool* on, const char* const* items, int n, int* cur,
                     const char* tip_check, const char* tip_combo) {
    return uik_check_combo_m(ctx, label, on, items, n, cur, tip_check, tip_combo, NULL);
}

bool uik_float(struct nk_context* ctx, const char* label, float* v, float lo, float hi, float step, const char* help) {
    if (uim_on(ctx)) {                        /* the label and the value inside the track */
        row_n(ctx, 1);
        tipn(ctx, help);
        return used(uim_slider(ctx, v, label, v, lo, hi, step, NULL), v);
    }
    uik_label_row(ctx, label, help, 44);
    tipn(ctx, help);
    bool ch = used(ui_slider_float(ctx, lo, v, hi, step), v);
    char b[24];
    snprintf(b, sizeof b, fabsf(*v) >= 100 ? "%.0f" : "%.3g", *v);
    nk_label(ctx, b, NK_TEXT_RIGHT);
    return ch;
}

bool uik_slider_i(struct nk_context* ctx, const char* label, int* v, int lo, int hi, const char* shown, const char* help) {
    if (uim_on(ctx)) {
        row_n(ctx, 1);
        tipn(ctx, help);
        float f = (float)*v;
        bool ch = uim_slider(ctx, v, label, &f, (float)lo, (float)hi, 1.f, shown);   /* shown NULL: the number */
        *v = (int)lroundf(f);
        return used(ch, v);
    }
    uik_label_row(ctx, label, help, 44);
    tipn(ctx, help);
    bool ch = used(ui_slider_int(ctx, lo, v, hi, 1), v);
    char b[24];
    if (!shown) { snprintf(b, sizeof b, "%d", *v); shown = b; }
    nk_label(ctx, shown, NK_TEXT_RIGHT);
    return ch;
}

bool uik_num(struct nk_context* ctx, const char* label, float* v, float lo, float hi, float step, const char* help) {
    uik_label_row(ctx, label, help, 0);
    tipn(ctx, help);
    int sv[2];
    uim_stepper_begin(ctx, sv);
    float n = nk_propertyf(ctx, "#", lo, *v, hi, step, step * 0.1f);
    uim_stepper_end(ctx, sv);
    bool ch = n != *v;
    *v = n;
    return used(ch, v);
}

bool uik_int(struct nk_context* ctx, const char* label, int* v, int lo, int hi, int step, const char* help) {
    uik_label_row(ctx, label, help, 0);
    tipn(ctx, help);
    int sv[2];
    uim_stepper_begin(ctx, sv);
    int n = nk_propertyi(ctx, "#", lo, *v, hi, step, 0.2f);
    uim_stepper_end(ctx, sv);
    bool ch = n != *v;
    *v = n;
    return used(ch, v);
}

/* label + a swatch: the click opens a picker (a colour wheel and R G B boxes) under it */
bool uik_color(struct nk_context* ctx, const char* label, float* rgb, const char* help) {
    static const float* open;
    uik_label_row(ctx, label, help, 0);
    struct nk_rect bb = nk_widget_bounds(ctx);
    tipn(ctx, help);
    if (nk_button_color(ctx, nk_rgb_f(rgb[0], rgb[1], rgb[2]))) open = open == rgb ? NULL : rgb;
    if (open != rgb) return false;
    bool ch = false;
    struct nk_rect clip = ctx->current->layout->clip;
    float s = U.scale, pw = 230 * s, ph = 170 * s + 3 * RH() + 6 * ctx->style.window.spacing.y;
    struct nk_rect pr = nk_rect(bb.x + bb.w - pw - clip.x, bb.y + bb.h - clip.y, pw, ph);
    if (nk_popup_begin(ctx, NK_POPUP_STATIC, "uikpick", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, pr)) {
        nk_layout_row_dynamic(ctx, 170 * s, 1);
        struct nk_colorf cf = nk_color_picker(ctx, (struct nk_colorf){ rgb[0], rgb[1], rgb[2], 1 }, NK_RGB);
        nk_layout_row_dynamic(ctx, RH(), 3);
        static const char* nm[3] = { "#R", "#G", "#B" };
        float c[3] = { cf.r, cf.g, cf.b };
        for (int k = 0; k < 3; k++) c[k] = nk_propertyf(ctx, nm[k], 0, c[k], 1, 0.01f, 0.005f);
        for (int k = 0; k < 3; k++) if (c[k] != rgb[k]) { rgb[k] = c[k]; ch = true; }
        nk_layout_row_dynamic(ctx, RH(), 1);
        if (nk_button_label(ctx, "done")) { open = NULL; nk_popup_close(ctx); }
        nk_popup_end(ctx);
    } else open = NULL;
    return ch;
}

int uik_buttons(struct nk_context* ctx, const char* label, int n, const char* const* labels, const char* const* tips) {
    int hit = -1;
    if (label) {
        nk_layout_row_template_begin(ctx, RH());
        nk_layout_row_template_push_static(ctx, LW(ctx));
        for (int i = 0; i < n; i++) nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        nk_label(ctx, label, NK_TEXT_LEFT);
    } else row_n(ctx, n);
    for (int i = 0; i < n; i++) {
        if (tips && tips[i]) tipn(ctx, tips[i]);
        if (nk_button_label(ctx, labels[i])) hit = i;
    }
    return hit;
}

/* label + n number boxes side by side (v[0..n-1], one range); tips[i] each, or NULL */
bool uik_nums(struct nk_context* ctx, const char* label, int n, float* v, float lo, float hi, float step, const char* const* tips) {
    nk_layout_row_template_begin(ctx, RH());
    nk_layout_row_template_push_static(ctx, LW(ctx));
    for (int i = 0; i < n; i++) nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, label, NK_TEXT_LEFT);
    bool ch = false;
    for (int i = 0; i < n; i++) {
        if (tips && tips[i]) tipn(ctx, tips[i]);
        int sv[2];
        uim_stepper_begin(ctx, sv);
        float x = nk_propertyf(ctx, "#", lo, v[i], hi, step, step * 0.1f);
        uim_stepper_end(ctx, sv);
        if (x != v[i]) { v[i] = x; ch = true; }
    }
    return ch;
}

/* a family of on/off filters as chips in a wrapping row (callers: when uim_on, else their boxes) */
bool uik_chips(struct nk_context* ctx, int n, const char* const* labels, nk_bool* const* v, const char* const* tips) {
    bool ch = uim_chips(ctx, RH(), n, labels, v, tips);
    for (int i = 0; ch && i < n; i++) cv_cmd_used_ptr(v[i]);
    return ch;
}
