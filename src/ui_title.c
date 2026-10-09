/* ui_title.c -- the title block: "Label: value" lines (app_title.c) in a corner
   of the 3D view, so screenshots and exports carry them, drawn in the legend's
   ink (or dark on its white box). Bottom-right until dragged, like the legend;
   a right click opens its settings window: which lines, the date, free text. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include <math.h>
#include <stdio.h>

enum { PIECES = 4 };            /* rows one value may wrap to */
static ov_drag drag;
static cv_box  shown_box;
static bool    shown;

bool title_box(cv_box* b) {
    if (shown) *b = shown_box;
    return shown;
}

static float text_w(const struct nk_user_font* f, const char* t) { return f->width(f->userdata, f->height, t, (int)strlen(t)); }

void window_title(struct nk_context* ctx, float s, float row) {
    shown = false;
    if (!G.loaded || !G.title_on) return;
    char units[96];
    units_summary(units, sizeof units);
    cv_title_line L[CV_TB_N + CV_TB_FREE];
    int n = app_title_lines(L, CV_TB_N + CV_TB_FREE, units);
    if (!n) return;

    /* two columns: the labels (dim) and the values, as wide as the widest of each;
       a value wider than the block may grow wraps onto more rows */
    const struct nk_user_font* f = ctx->style.font;
    float pad = 8 * s, gap = 8 * s, lh = f->height + 5 * s, lw = 0, vw = 0;
    char lab[CV_TB_N + CV_TB_FREE][40];
    for (int i = 0; i < n; i++) {
        snprintf(lab[i], sizeof lab[i], "%s:", L[i].label[0] ? L[i].label : "");
        lw = CV_MAX(lw, text_w(f, lab[i]));
        vw = CV_MAX(vw, text_w(f, L[i].text));
    }
    float w = CV_MIN(2 * pad + lw + gap + vw + 4 * s, CV_MIN(560 * s, G.vp_w * 0.6f));
    float vavail = CV_MAX(w - 2 * pad - lw - gap - 2 * s, 40 * s);
    int np[CV_TB_N + CV_TB_FREE], len[CV_TB_N + CV_TB_FREE][PIECES], rows = 0;
    for (int i = 0; i < n; i++) {
        len[i][0] = (int)strlen(L[i].text);
        rows += np[i] = CV_MAX(wrap_pieces(f, L[i].text, vavail, len[i], PIECES), 1);
    }
    float h = 2 * pad + rows * lh - 5 * s;
    cv_anchor a = G.title_pos.set ? G.title_pos : (cv_anchor){ true, CV_BR, 10, 10 };
    overlay_drag(ctx, &drag, "title", cv_anchor_place(a, view_box(), w, h, s), 0, &G.title_pos, s);
    if (G.title_pos.set) a = G.title_pos;
    shown_box = cv_anchor_place(a, view_box(), w, h, s);
    shown = true;

    struct nk_rect r = nk_rect(shown_box.x, shown_box.y, shown_box.w, shown_box.h);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(G.legend_box ? nk_rgba(255, 255, 255, 235) : nk_rgba(0, 0, 0, 0)));
    nk_style_push_float(ctx, &ctx->style.window.border, G.legend_box ? 1.f : 0.f);
    nk_style_push_color(ctx, &ctx->style.window.border_color, nk_rgb(90, 90, 90));
    if (begin_background(ctx, "title", r, NK_WINDOW_NO_SCROLLBAR | (G.legend_box ? NK_WINDOW_BORDER : 0))) {
        nk_window_set_bounds(ctx, "title", r);
        struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
        nk_push_scissor(cv, r);
        struct nk_color ink = legend_ink(), dim = legend_dim();
        float x = r.x + pad, y = r.y + pad, vx = x + lw + gap;
        for (int i = 0; i < n; i++) {
            ink_text(cv, f, x, y, lw + 1, lab[i], dim);
            for (int p = 0, at = 0; p < np[i]; at += len[i][p++], y += lh) {
                char piece[160];
                snprintf(piece, sizeof piece, "%.*s", len[i][p], L[i].text + at);
                ink_text(cv, f, vx, y, r.x + r.w - pad - vx, p ? piece + strspn(piece, " ") : piece, ink);
            }
        }
        const struct nk_input* in = &ctx->input;
        if (nk_input_is_mouse_click_in_rect(in, NK_BUTTON_RIGHT, r)) G.title_edit = true;
        if (nk_input_is_mouse_hovering_rect(in, r) && !drag.moving)
            tip_show(ctx, "Title block.  Drag: move it.  Right-click: its lines and free text");
    }
    nk_end(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
}

/* ---- settings: which lines, the date, three free lines, the box, the place */
void window_title_settings(struct nk_context* ctx, float s, float row) {
    static bool was_open;
    if (!G.title_edit || !G.loaded) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Title block", NK_SHOWN);
    was_open = true;
    const struct nk_style_window* ws = &ctx->style.window;     /* 13 rows under the header */
    float w = 340 * s, h = ctx->style.font->height + 2 * (ws->header.padding.y + ws->header.label_padding.y)
                          + 13 * (row + ws->spacing.y) + 2 * ws->padding.y + 4 * s;
    if (nk_begin(ctx, "Title block", nk_rect(G.vp_x + G.vp_w - w - 180 * s, G.vp_y + 40 * s, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "The title block in the view (also in pictures and videos). Lines without data are left out");
        nk_checkbox_label(ctx, "show the title block", &G.title_on);
        nk_layout_row_dynamic(ctx, row, 2);
        static const char* const tips[CV_TB_N] = {
            "The deck's *HEADING line (or the one the .frd repeats)",
            "The result file's name",
            "The solver and its version, from the .frd header",
            "The procedure of the step shown: *STATIC, *FREQUENCY, *HEAT TRANSFER ...",
            "The step shown: its increment and time, or its mode and frequency",
            "The deformation scale of the picture",
            "The units shown (the Units window sets them)",
            "Who you are logged in as",
            "Today's date, or the result file's (below)",
        };
        for (int k = 0; k < CV_TB_N; k++) {
            tip(ctx, tips[k]);
            nk_checkbox_label(ctx, app_title_line_name(k), &G.title_line[k]);
        }
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 64 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        nk_label(ctx, "Date of", NK_TEXT_LEFT);
        tip(ctx, "The date line: today's");
        if (nk_option_label(ctx, "today", !G.title_file_date)) G.title_file_date = false;
        tip(ctx, "The date line: when the solver wrote the result file (its header), else the file's time");
        if (nk_option_label(ctx, "result file", G.title_file_date)) G.title_file_date = true;

        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "Free lines: label, text (empty text: no line)", NK_TEXT_LEFT, P.dim);
        for (int i = 0; i < CV_TB_FREE; i++) {
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 100 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);
            char mk[24];
            snprintf(mk, sizeof mk, "#title label %d", i + 1);
            uii_test_mark(ctx, mk);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.title_free[i][0], (int)sizeof G.title_free[i][0], nk_filter_default);
            snprintf(mk, sizeof mk, "#title text %d", i + 1);
            uii_test_mark(ctx, mk);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.title_free[i][1], (int)sizeof G.title_free[i][1], nk_filter_default);
        }
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "A white box with a border behind the title block and the legend, for a picture on a page");
        nk_checkbox_label(ctx, "white box (as the legend's)", &G.legend_box);
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Back to the bottom-right corner (it moves by dragging it)");
        if (nk_button_label(ctx, "reset position")) G.title_pos.set = false;
        if (nk_button_label(ctx, "close")) G.title_edit = false;
    }
    if (nk_window_is_hidden(ctx, "Title block")) G.title_edit = false;
    nk_end(ctx);
}
