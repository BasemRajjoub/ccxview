/* ui_bars.c -- the strips around the 3D view: toolbar, time bar, status bar;
   the legend (colour map picker, settings window) and the axes gizmo, both
   dragged by any point and kept as a view corner. */
#include "app.h"
#include "ui.h"
#include "cfg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"
#include "icons.h"

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
void cmap_combo(struct nk_context* ctx, float s, float row) {
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
void panel_toolbar(struct nk_context* ctx, float s, float row) {
    /* line 1: deformation */
    nk_layout_row_begin(ctx, NK_STATIC, row, 9);
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
    uii_vsep(ctx);
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
    nk_layout_row_push(ctx, 24 * s);
    uii_vsep(ctx);
    nk_layout_row_push(ctx, 64 * s);
    if (nk_button_label(ctx, IC_SCAN "  Fit")) app_fit();
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
    if (G.range_lock) uii_vsep(ctx); else nk_spacing(ctx, 1);
    if (G.range_lock) {
        nk_layout_row_push(ctx, 120 * s);
        char a[32], b[32], lab[80];
        legend_num(a, sizeof a, G.rmin); legend_num(b, sizeof b, G.rmax);
        snprintf(lab, sizeof lab, "locked %s .. %s", a, b);
        nk_label_colored(ctx, lab, NK_TEXT_LEFT, P.warn);
    }
    nk_layout_row_end(ctx);
}

void panel_timebar(struct nk_context* ctx, float s, float row, float width) {
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
    if (nk_button_label(ctx, IC_STEP_BACK)) app_set_step(G.step - 1);
    nk_layout_row_push(ctx, 84 * s);
    tip(ctx, "Step through the increments (Space)");
    if (nk_button_label(ctx, G.playing ? IC_PAUSE "  pause" : IC_PLAY "  play")) { G.playing = !G.playing; G.last_tick = 0; }
    nk_layout_row_push(ctx, 32 * s);
    if (nk_button_label(ctx, IC_STEP_FORWARD)) app_set_step(G.step + 1);
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

/* legend text sits on the 3D view: a faint G.bg plate behind it keeps it
   readable where the model passes behind */
static void ink_text(struct nk_command_buffer* cv, const struct nk_user_font* f, float x, float y, float w,
                     const char* txt, struct nk_color c) {
    int n = (int)strlen(txt);
    float tw = CV_MIN(f->width(f->userdata, f->height, txt, n), w);
    nk_fill_rect(cv, nk_rect(x - 2, y, tw + 4, f->height), 3, uii_bg(110));
    nk_draw_text(cv, nk_rect(x, y, w, f->height), txt, n, f, nk_rgba(0, 0, 0, 0), c);
}

/* nk_label in colour c, with the same plate */
static void ink_label_c(struct nk_context* ctx, const char* txt, struct nk_color c) {
    struct nk_rect b = nk_widget_bounds(ctx);
    const struct nk_user_font* f = ctx->style.font;
    float px = ctx->style.text.padding.x, tw = f->width(f->userdata, f->height, txt, (int)strlen(txt));
    nk_fill_rect(nk_window_get_canvas(ctx), nk_rect(b.x + px - 2, b.y + (b.h - f->height) * 0.5f, CV_MIN(tw + 4, b.w), f->height),
                 3, uii_bg(110));
    nk_label_colored(ctx, txt, NK_TEXT_LEFT, c);
}
static void ink_label(struct nk_context* ctx, const char* txt) { ink_label_c(ctx, txt, uii_on_bg()); }

/* ---- the legend's title: the field, its component, its unit (G.legend_lines), each
   on a line of its own and wrapped when wider than the legend */
enum { HEAD_PIECES = 4 };                        /* lines one title line may wrap to */

/* byte lengths of the pieces txt breaks into to fit w: after a space, comma or
   operator when there is one in the second half, else anywhere (never in a UTF-8
   sequence); the last piece takes what is left. Returns the count. */
static int wrap_pieces(const struct nk_user_font* f, const char* txt, float w, int* len, int max) {
    int n = 0, at = 0, total = (int)strlen(txt);
    while (at < total && n < max) {
        int fit = 0;
        for (;;) {
            int next = fit + 1;
            while (at + next < total && (txt[at + next] & 0xC0) == 0x80) next++;
            if (at + next > total || (fit > 0 && f->width(f->userdata, f->height, txt + at, next) > w)) break;
            fit = next;
            if (at + fit >= total) break;
        }
        if (n == max - 1 || at + fit >= total) fit = total - at;
        else {
            int br = fit;
            while (br > fit / 2 && !strchr(" +-*/,", txt[at + br - 1])) br--;
            if (br > fit / 2) fit = br;
        }
        len[n++] = fit;
        at += fit;
    }
    return n;
}

static float head_row(const struct nk_user_font* f, float row, float s) { return CV_MIN(row, f->height + 6 * s); }

/* the legend width for the title: its widest line, within the legend's limits */
static float head_width(const struct nk_user_font* f, float s) {
    float w = 150 * s;
    for (int k = 0; k < 3; k++) {
        const char* t = G.legend_lines[k];
        w = CV_MAX(w, f->width(f->userdata, f->height, t, (int)strlen(t)) + 24 * s);
    }
    return CV_MIN(w, 260 * s);
}

/* how many rows the title takes in a legend whose text is w wide */
static int head_rows(const struct nk_user_font* f, float w) {
    int n = 0, len[HEAD_PIECES];
    for (int k = 0; k < 3; k++) if (G.legend_lines[k][0]) n += wrap_pieces(f, G.legend_lines[k], w, len, HEAD_PIECES);
    return CV_MAX(n, 1);
}

static void panel_group_legend(struct nk_context* ctx, float s, float row) {
    int a = G.faces_mode - FM_TYPE;
    const cv_axis* ax = &G.groups.axis[a];
    nk_layout_row_dynamic(ctx, row, 1);
    ink_label(ctx, cv_axis_name(a));
    for (int i = 0; i < ax->n && i < 60; i++) {
        if (!ax->on[i]) continue;
        const float* c = G.axis_rgb[a] + 3 * i;
        struct nk_rect r;
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, row);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        if (nk_widget(&r, ctx) != NK_WIDGET_INVALID) {
            struct nk_rect sw = nk_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
            nk_fill_rect(nk_window_get_canvas(ctx), sw, 0, nk_rgb_f(c[0], c[1], c[2]));
            nk_stroke_rect(nk_window_get_canvas(ctx), sw, 0, 1, uii_on_bg());   /* a swatch of the background's colour */
        }
        char lab[64];
        if (a == CV_AXIS_TYPE) snprintf(lab, sizeof lab, "%s", cv_frd_type_name((int)ax->value[i]));
        else snprintf(lab, sizeof lab, "%s %u", a == CV_AXIS_MAT ? "Material" : "Group", ax->value[i]);
        ink_label(ctx, lab);
    }
}

static struct nk_rect legend_title;     /* the field's name and unit: a click opens Units */

static void panel_legend(struct nk_context* ctx, float s, float row) {
    char tt[200];
    {
        char a[32], b[32];
        legend_num(a, sizeof a, G.data_min); legend_num(b, sizeof b, G.data_max);
        snprintf(tt, sizeof tt, "data %s .. %s%s%zu without data.  Click the title: units.  Drag: move it.  Right-click: legend settings",
                 a, b, G.nan_count ? ",  " : ",  ", G.nan_count);
    }
    /* the title, a line each for field, component and unit (dim); a click opens Units */
    const struct nk_user_font* hf = ctx->style.font;
    struct nk_rect cr = nk_window_get_content_region(ctx);
    float hr = head_row(hf, row, s), tw = cr.w - 2 * ctx->style.text.padding.x - 4;
    int nrow = 0;
    for (int k = 0; k < 3; k++) {
        const char* ln = G.legend_lines[k];
        if (!ln[0]) continue;
        int len[HEAD_PIECES], np = wrap_pieces(hf, ln, tw, len, HEAD_PIECES);
        for (int p = 0, at = 0; p < np; at += len[p++]) {
            char piece[96];
            snprintf(piece, sizeof piece, "%.*s", len[p], ln + at);
            nk_layout_row_dynamic(ctx, hr, 1);
            struct nk_rect b = nk_widget_bounds(ctx);
            if (nrow == 0) {
                legend_title = b;
                tip(ctx, tt);
            } else {
                legend_title.w = CV_MAX(legend_title.w, b.x + b.w - legend_title.x);
                legend_title.h = b.y + b.h - legend_title.y;
            }
            ink_label_c(ctx, piece, k == 2 ? uii_on_bg_dim() : uii_on_bg());
            nrow++;
        }
    }
    if (!nrow) { legend_title = nk_rect(0, 0, 0, 0); nk_layout_row_dynamic(ctx, hr, 1); nk_spacing(ctx, 1); nrow = 1; }
    struct nk_rect area;
    nk_layout_row_dynamic(ctx, cr.h - nrow * (hr + ctx->style.window.spacing.y) - 8 * s, 1);
    if (nk_widget(&area, ctx) == NK_WIDGET_INVALID) return;
    struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
    const struct nk_user_font* font = ctx->style.font;
    const struct nk_color ink = uii_on_bg(), dim = uii_on_bg_dim();

    if (!(G.rmax > G.rmin)) {                     /* constant field: one value, not 13 equal labels */
        char txt[48], num[32];
        legend_num(num, sizeof num, G.rmin);
        snprintf(txt, sizeof txt, "uniform  %s", num);
        ink_text(cv, font, area.x, area.y, area.w, txt, ink);
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
            nk_stroke_rect(cv, nk_rect(area.x, yy[k], bar_w, sw), 0, 1, ink);
            ink_text(cv, font, area.x + bar_w + 7 * s, yy[k] + (sw - font->height) * 0.5f, area.w - bar_w - 7 * s, lab[k], dim);
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
    nk_stroke_rect(cv, nk_rect(area.x, top, bar_w, h), 0, 1, ink);   /* a bar end the colour of the background still shows */

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
                       y + font->height * 0.5f, 1, dim);
        ink_text(cv, font, area.x + bar_w + 7 * s, y, area.w - bar_w - 7 * s, txt, ink);
    }
}

void panel_status(struct nk_context* ctx, float s, float row, float width) {
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
    float sw = 14 * s;
    float ub = 150 * s;                    /* the units button */
    nk_layout_row_begin(ctx, NK_STATIC, row, (exporting ? 5 : loading ? 4 : 3) + 4);
    nk_layout_row_push(ctx, pw);
    nk_label(ctx, G.loaded ? cv_basename(G.path) : "", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, sw); uii_vsep(ctx);
    float midw = CV_MAX(width - pw - bw - ub - 7 * ctx->style.window.spacing.x - 3 * (sw + ctx->style.window.spacing.x), 10);
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
    nk_layout_row_push(ctx, sw); uii_vsep(ctx);
    nk_layout_row_push(ctx, ub);
    {
        char u[64], t[80];
        units_summary(u, sizeof u);
        snprintf(t, sizeof t, "units: %s", u);
        tip(ctx, "Input units (what the model was built in) and display units");
        if (nk_button_label(ctx, t)) G.show_units = !G.show_units;
    }
    nk_layout_row_push(ctx, sw); uii_vsep(ctx);
    nk_layout_row_push(ctx, bw);
    if (nm) {
        if (nk_button_label(ctx, badge)) G.show_msgs = !G.show_msgs;
    } else {
        nk_label(ctx, badge, NK_TEXT_RIGHT);
    }
    nk_layout_row_end(ctx);
}

/* ---- the legend's range, number format, band count, reverse and greyscale:
   in View > Colours & legend, and in a small floating window opened by
   right-clicking the legend. */
void legend_controls(struct nk_context* ctx, float s, float row) {
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
        tip(ctx, "Input units (what the model was built in; CalculiX has none) and display units");
        {
            char u[64], t[80];
            units_summary(u, sizeof u);
            snprintf(t, sizeof t, "Units: %s ...", u);
            if (nk_button_label(ctx, t)) G.show_units = !G.show_units;
        }
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Centre the view on the field's minimum / maximum");
        if (nk_button_label(ctx, "go to min") && G.min_at != UINT32_MAX) { G.show_markers = true; app_find(G.elem_mode ? G.frd.elem_id[G.min_at] : G.frd.node_id[G.min_at], G.elem_mode); }
        if (nk_button_label(ctx, "go to max") && G.max_at != UINT32_MAX) { G.show_markers = true; app_find(G.elem_mode ? G.frd.elem_id[G.max_at] : G.frd.node_id[G.max_at], G.elem_mode); }
    }
}

void window_legend_settings(struct nk_context* ctx, float s, float row) {
    static bool was_open;
    if (!G.legend_edit || !G.loaded) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Legend settings", NK_SHOWN);
    was_open = true;
    float w = 300 * s, h = 15.9f * row;
    if (nk_begin(ctx, "Legend settings", nk_rect(G.vp_x + G.vp_w - w - 180 * s, G.vp_y + 10 * s, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        legend_controls(ctx, s, row);
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "Back to the defaults: legend top-right, gizmo bottom-left. Both are moved by dragging them");
        if (nk_button_label(ctx, "Reset legend and gizmo positions")) G.legend_pos.set = G.gizmo_pos.set = false;
        if (nk_button_label(ctx, "close")) G.legend_edit = false;
    }
    if (nk_window_is_hidden(ctx, "Legend settings")) G.legend_edit = false;
    nk_end(ctx);
}

/* ---- legend and gizmo: dragged by any point, kept as a view corner -------------
   Nuklear moves a window only by its title bar, which the legend does not
   have and nobody finds; so a press anywhere on the overlay and a drag moves it.
   Up to 4 px it stays a click (a gizmo tip, the gizmo centre). While moving, the
   anchor follows: the corner nearest the box and the gap from it (anchor.h). */
typedef struct { bool down, moving, dropped; float px, py, ox, oy; } ov_drag;

static cv_box view_box(void) { return (cv_box){ (float)G.vp_x, (float)G.vp_y, (float)G.vp_w, (float)G.vp_h }; }

/* the window on top at (x, y); NULL over a popup (a combo, a menu) */
static struct nk_window* window_at(struct nk_context* ctx, float x, float y) {
    struct nk_window* top = NULL;
    for (struct nk_window* w = ctx->begin; w; w = w->next) {
        if (w->flags & NK_WINDOW_HIDDEN) continue;
        const struct nk_window* p = w->popup.active ? w->popup.win : NULL;
        if (p && x >= p->bounds.x && y >= p->bounds.y && x < p->bounds.x + p->bounds.w && y < p->bounds.y + p->bounds.h)
            top = NULL;
        else if (x >= w->bounds.x && y >= w->bounds.y && x < w->bounds.x + w->bounds.w && y < w->bounds.y + w->bounds.h)
            top = w;
    }
    return top;
}

/* at: where the overlay is this frame; keep_right: a strip on its right that
   is not a handle (the group legend's scrollbar) */
static void overlay_drag(struct nk_context* ctx, ov_drag* d, const char* name, cv_box at, float keep_right,
                         cv_anchor* pos, float s) {
    const struct nk_input* in = &ctx->input;
    const struct nk_mouse_button* lb = &in->mouse.buttons[NK_BUTTON_LEFT];
    float mx = in->mouse.pos.x, my = in->mouse.pos.y;
    d->dropped = false;
    if (!d->down && lb->down && lb->clicked) {
        float cx = lb->clicked_pos.x, cy = lb->clicked_pos.y;
        struct nk_window* w = window_at(ctx, cx, cy);
        if (w && !strcmp(w->name_string, name) && cx >= at.x && cy >= at.y && cx < at.x + at.w - keep_right && cy < at.y + at.h) {
            d->down = true; d->moving = false;
            d->px = cx; d->py = cy; d->ox = cx - at.x; d->oy = cy - at.y;
        }
    }
    if (!d->down) return;
    if (!lb->down) { d->dropped = d->moving; d->down = d->moving = false; return; }
    if (!d->moving && fabsf(mx - d->px) + fabsf(my - d->py) > 4) d->moving = true;
    if (d->moving) *pos = cv_anchor_from_box((cv_box){ mx - d->ox, my - d->oy, at.w, at.h }, view_box(), s);
}

static ov_drag legend_drag, gizmo_drag;

/* the gizmo's box this frame; by default bottom-left, as in ParaView, out of the legend's way */
#define GIZMO_PX 90                      /* the gizmo's square, before UI scale */

static cv_box gizmo_box(float s) {
    cv_anchor a = G.gizmo_pos.set ? G.gizmo_pos : (cv_anchor){ true, CV_BL, 12, 12 };
    return cv_anchor_place(a, view_box(), GIZMO_PX * s, GIZMO_PX * s, s);
}

/* ---- legend: the field's colour bar or the group list, in a corner of the view */
void window_legend(struct nk_context* ctx, float s, float row) {
    struct nk_rect r;
    if (G.loaded) {
        bool by_group = G.faces_mode >= FM_TYPE && G.faces_mode <= FM_GRP && G.show_faces;
        bool by_field = G.has_field && !by_group;
        if ((by_field || by_group) && !G.hide_legend) {
            float lw = 150 * s, lh = CV_MIN(440 * s, G.vp_h - 20 * s);
            if (by_field) {                       /* as wide as the title's widest line; taller by its extra lines */
                const struct nk_user_font* f = ctx->style.font;
                lw = head_width(f, s);
                float tw = lw - 2 * ctx->style.window.padding.x - 2 * ctx->style.text.padding.x - 4;
                lh = CV_MIN(lh + (head_rows(f, tw) - 1) * (head_row(f, row, s) + ctx->style.window.spacing.y),
                            G.vp_h - 20 * s);
            }
            /* top-right until dragged; then the corner it was dropped nearest (anchor.h) */
            cv_anchor la = G.legend_pos.set ? G.legend_pos : (cv_anchor){ true, CV_TR, 10, 10 };
            float keep = by_group ? ctx->style.window.scrollbar_size.x + ctx->style.window.padding.x : 0;
            overlay_drag(ctx, &legend_drag, "Legend", cv_anchor_place(la, view_box(), lw, lh, s), keep, &G.legend_pos, s);
            if (G.legend_pos.set) la = G.legend_pos;
            cv_box lb = cv_anchor_place(la, view_box(), lw, lh, s);
            /* a small screen: shorter, rather than over the gizmo; the gizmo never moves */
            if (!G.hide_axes) lb = cv_legend_fit(lb, gizmo_box(s), 10 * s, 160 * s);
            lh = lb.h;
            r = nk_rect(lb.x, lb.y, lb.w, lb.h);
            if (nk_window_find(ctx, "Legend")) nk_window_set_bounds(ctx, "Legend", r);
            /* no box, no frame: the text takes its colour from G.bg (uii_on_bg); the colour bar stays opaque */
            nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
            nk_style_push_float(ctx, &ctx->style.window.border, 0);
            nk_flags lf = by_group ? 0 : NK_WINDOW_NO_SCROLLBAR;
            if (lh > 80 * s && nk_begin(ctx, "Legend", r, lf)) {
                struct nk_rect b = nk_window_get_bounds(ctx);
                if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_RIGHT, b)) G.legend_edit = true;
                if (by_group) panel_group_legend(ctx, s, row);
                else panel_legend(ctx, s, row);
                /* a click on the title, not the end of a drag: the units of that field */
                const struct nk_input* in = &ctx->input;
                struct nk_window* top = window_at(ctx, in->mouse.pos.x, in->mouse.pos.y);
                if (by_field && !legend_drag.dropped && top && !strcmp(top->name_string, "Legend") &&
                    nk_input_is_mouse_released(in, NK_BUTTON_LEFT) &&
                    nk_input_is_mouse_hovering_rect(in, legend_title) &&
                    NK_INBOX(in->mouse.buttons[NK_BUTTON_LEFT].clicked_pos.x, in->mouse.buttons[NK_BUTTON_LEFT].clicked_pos.y,
                             legend_title.x, legend_title.y, legend_title.w, legend_title.h))
                    G.show_units = true;
            }
            if (lh > 80 * s) nk_end(ctx);
            nk_style_pop_float(ctx);
            nk_style_pop_style_item(ctx);
        }
    }
}

/* ---- axes gizmo (bottom-left of the view, or where it was dragged) ---------------
   World X/Y/Z projected with the camera's rotation only, so it turns with the
   view. Far axes are drawn first. Clicking a tip looks from that direction;
   clicking the centre returns to the iso view. */
void window_axes(struct nk_context* ctx, float s) {
    if (!G.loaded || G.hide_axes) return;
    float size = GIZMO_PX * s, r = size * 0.36f;
    overlay_drag(ctx, &gizmo_drag, "axes", gizmo_box(s), 0, &G.gizmo_pos, s);
    cv_box gb = gizmo_box(s);
    struct nk_rect wr = nk_rect(gb.x, gb.y, gb.w, gb.h);
    bool dragging = gizmo_drag.moving || gizmo_drag.dropped;    /* a drag that ends on a tip is no click */
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
            if (!dragging && nk_input_is_mouse_click_in_rect(in, NK_BUTTON_LEFT, hit))
                clicked_view = t->neg ? view_neg[t->axis] : view_pos[t->axis];
        }
        struct nk_rect centre = nk_rect(cx - 5 * s, cy - 5 * s, 10 * s, 10 * s);
        nk_fill_circle(cv, centre, uii_on_bg());
        if (!dragging && clicked_view < 0 && nk_input_is_mouse_click_in_rect(in, NK_BUTTON_LEFT, centre)) clicked_view = CV_VIEW_ISO;
        const char* proj = G.cam.ortho ? "ortho" : "persp";
        float pw = font->width(font->userdata, font->height, proj, (int)strlen(proj));
        ink_text(cv, font, cx - pw * 0.5f, wr.y + size - font->height - 2 * s, pw + 1, proj, uii_on_bg());
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
