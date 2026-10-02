/* ui_plots.c -- the plot windows: stress linearization and the field along a
   path (one window), the history at the probed node, the run's convergence. */
#include "app.h"
#include "ui.h"
#include "cfg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"

/* ---- stress linearization: membrane, bending, peak along a line through the wall --- */
static double lin_q(const double t[6]) {
    return G.lin_q == 0 ? cv_mises6(t) : G.lin_q == 1 ? cv_tresca6(t, false) :
           G.lin_q == 8 ? t[0] + t[1] + t[2] : t[CV_MIN(G.lin_q - 2, 5)];
}

/* the linearized quantity: von Mises, Tresca, a component or S1+S2+S3 (the trace) */
static void lin_quantity(struct nk_context* ctx, float s, float row) {
    const cv_field_desc* d = G.lin_fi >= 0 ? &G.frd.steps[G.step].fields[G.lin_fi] : NULL;
    char names[9][16] = { "von Mises", "Tresca" };
    const char* items[9];
    for (int c = 0; c < 6; c++) {
        if (!d) { snprintf(names[2 + c], 16, "%d", c + 1); continue; }
        if (G.csys > 0) cv_cyl_comp_name(d, c, names[2 + c]); else snprintf(names[2 + c], 16, "%s", d->comp[c]);
    }
    snprintf(names[8], 16, "S1+S2+S3");
    for (int k = 0; k < 9; k++) items[k] = names[k];
    tip(ctx, "The linearized quantity in the table and the plot (components in the coordinates chosen under Fields;\n"
             "S1+S2+S3, the sum of the principal stresses, for the triaxial limit of ASME VIII-2 5.3.2)");
    G.lin_q = nk_combo(ctx, items, 9, G.lin_q, (int)row, nk_vec2(130 * s, 9 * row + 20 * s));
}

static void lin_body(struct nk_context* ctx, float s, float row) {
    {
        char txt[200];
        double m[6], b[6];
        bool ok = app_lin_mb(m, b);
        if (!ok) {
            int out = 0;
            for (int i = 0; i < G.lin_n; i++) out += G.lin_s[6 * i] != G.lin_s[6 * i];
            bool fix = G.lin_n && G.path_dir != 1 && G.path_end[0] < G.frd.n_nodes;   /* offer the line through the wall */
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_dynamic(ctx);
            if (fix) nk_layout_row_template_push_static(ctx, 140 * s);
            nk_layout_row_template_end(ctx);
            if (G.lin_n) snprintf(txt, sizeof txt, "Leaves the solid (%d of %d points): not across the wall", out, G.lin_n);
            else snprintf(txt, sizeof txt, "Nothing to linearize: this step has no stress tensor");
            tip(ctx, G.lin_n ? "Linearization needs a straight line through the wall: pick a node on the opposite face,\n"
                               "or go along the normal from the first node" : txt);
            nk_label_colored(ctx, txt, NK_TEXT_LEFT, nk_rgb(230, 120, 60));
            if (fix) {
                char b[64];
                snprintf(b, sizeof b, "From node %u along the surface normal to the far face", G.frd.node_id[G.path_end[0]]);
                tip(ctx, b);
                if (nk_button_label(ctx, "along the normal")) { G.path_dir = 1; app_path_rebuild(); return; }
            }
        }
        if (ok) {
            nk_layout_row_dynamic(ctx, row, 1);
            tip(ctx, "ASME VIII-2 5-A.4.1.2: the bending stress is taken only from the components normal to the\n"
                     "line (hoop, meridional and their shear); the component along the line and the through-\n"
                     "thickness shears carry membrane and peak only. Off: all six components bend (Mecway's default).");
            if (nk_checkbox_label(ctx, "bending from the components normal to the line (ASME)", &G.lin_asme)) ok = app_lin_mb(m, b);
        }
        if (ok) {
            const float* s0 = G.lin_s; const float* s1 = G.lin_s + 6 * (G.lin_n - 1);
            double mb0[6], mb1[6], t0[6], t1[6], f0[6], f1[6];
            for (int c = 0; c < 6; c++) {
                mb0[c] = m[c] + b[c]; mb1[c] = m[c] - b[c]; t0[c] = s0[c]; t1[c] = s1[c];
                f0[c] = t0[c] - mb0[c]; f1[c] = t1[c] - mb1[c];
            }
            const char* rows[4] = { "membrane", "membrane + bending", "peak", "total" };
            const double* a[4] = { m, mb0, f0, t0 };
            const double* z[4] = { m, mb1, f1, t1 };
            /* the largest value anywhere on the line (what Mecway and Abaqus report) */
            double mx[4] = { lin_q(m), -INFINITY, -INFINITY, -INFINITY };
            for (int i = 0; i < G.lin_n; i++) {
                double x = G.lin_t * i / (G.lin_n - 1), l[6], tt[6], p[6];
                cv_lin_at(m, b, G.lin_t, x, l);
                for (int c = 0; c < 6; c++) { tt[c] = G.lin_s[6 * i + c]; p[c] = tt[c] - l[c]; }
                mx[1] = CV_MAX(mx[1], lin_q(l)); mx[2] = CV_MAX(mx[2], lin_q(p)); mx[3] = CV_MAX(mx[3], lin_q(tt));
            }
            static const char* tips[4] = {
                "The mean over the line: (1/t) \xe2\x88\xab \xcf\x83 dx",
                "The linear part at the two ends: membrane \xc2\xb1 bending, bending = (6/t\xc2\xb2) \xe2\x88\xab \xcf\x83 (t/2 - x) dx",
                "What the line does not carry: total - (membrane + bending)",
                "The stress itself at the two ends" };
            for (int r = -1; r < 4; r++) {
                nk_layout_row_template_begin(ctx, row);
                nk_layout_row_template_push_static(ctx, 150 * s);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_end(ctx);
                if (r < 0) {
                    nk_label(ctx, "", NK_TEXT_LEFT);
                    if (G.lin_a < G.frd.n_nodes) snprintf(txt, sizeof txt, "start (node %u)", G.frd.node_id[G.lin_a]);
                    else snprintf(txt, sizeof txt, "start");
                    nk_label_colored(ctx, txt, NK_TEXT_RIGHT, P.dim);
                    if (G.lin_b < G.frd.n_nodes) snprintf(txt, sizeof txt, "end (node %u)", G.frd.node_id[G.lin_b]);
                    else snprintf(txt, sizeof txt, "end (far face)");
                    nk_label_colored(ctx, txt, NK_TEXT_RIGHT, P.dim);
                    tip(ctx, "The largest value anywhere along the line, over its sampled points");
                    nk_label_colored(ctx, "max on line", NK_TEXT_RIGHT, P.dim);
                    continue;
                }
                tip(ctx, tips[r]);
                nk_label(ctx, rows[r], NK_TEXT_LEFT);
                legend_num(txt, sizeof txt, (float)lin_q(a[r])); nk_label(ctx, txt, NK_TEXT_RIGHT);
                legend_num(txt, sizeof txt, (float)lin_q(z[r])); nk_label(ctx, txt, NK_TEXT_RIGHT);
                legend_num(txt, sizeof txt, (float)mx[r]); nk_label(ctx, txt, NK_TEXT_RIGHT);
            }
        }
        struct nk_rect area;
        float used = (ok ? 8 : 3) * (row + ctx->style.window.spacing.y);   /* two header rows, the option and the table, or the note */
        nk_layout_row_dynamic(ctx, CV_MAX(nk_window_get_content_region(ctx).h - used - 4 * s, row), 1);
        if (nk_widget(&area, ctx) != NK_WIDGET_INVALID && G.lin_n > 1) {
            struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
            const struct nk_user_font* font = ctx->style.font;
            float lm = 76 * s, x0 = area.x + lm, y0 = area.y + 4 * s, w = area.w - lm - 6 * s, h = area.h - font->height - 8 * s;
            nk_fill_rect(cv, nk_rect(x0, y0, w, h), 0, P.plot_bg);
            enum { NP = 81, LIN_PLOT_MAX = 256 };
            float tot[LIN_PLOT_MAX], lin[NP], mem = ok ? (float)lin_q(m) : NAN;
            int n = CV_MIN(G.lin_n, LIN_PLOT_MAX);
            for (int i = 0; i < n; i++) {
                double t[6];
                for (int c = 0; c < 6; c++) t[c] = G.lin_s[6 * i + c];
                tot[i] = (float)lin_q(t);
            }
            for (int i = 0; i < NP; i++) {
                double t[6];
                if (ok) cv_lin_at(m, b, 1, (double)i / (NP - 1), t); else for (int c = 0; c < 6; c++) t[c] = NAN;
                lin[i] = (float)lin_q(t);
            }
            float lo = 1e30f, hi = -1e30f;
            for (int i = 0; i < n; i++) if (tot[i] == tot[i]) { lo = CV_MIN(lo, tot[i]); hi = CV_MAX(hi, tot[i]); }
            for (int i = 0; i < NP; i++) if (lin[i] == lin[i]) { lo = CV_MIN(lo, lin[i]); hi = CV_MAX(hi, lin[i]); }
            if (G.lin_q < 2) lo = CV_MIN(lo, 0.f);          /* equivalent stresses: from zero */
            if (lo > hi) { lo = 0; hi = 1; }
            if (hi <= lo) { hi = lo + fabsf(lo) * 0.01f + 1e-30f; lo -= hi - lo; }
            for (int k = 0; k <= 4; k++) {
                float v = lo + (hi - lo) * k / 4.f, y = y0 + h * (1 - k / 4.f);
                nk_stroke_line(cv, x0, y, x0 + w, y, 1, P.grid);
                tick_num(txt, sizeof txt, v, (hi - lo) / 4, CV_MAX(fabsf(lo), fabsf(hi)));
                nk_draw_text(cv, nk_rect(area.x, y - font->height * 0.5f, lm - 4 * s, font->height), txt, (int)strlen(txt), font, nk_rgba(0, 0, 0, 0), P.dim);
            }
            #define LY(v) (y0 + h * (1 - ((v) - lo) / (hi - lo)))
            if (mem == mem) nk_stroke_line(cv, x0, LY(mem), x0 + w, LY(mem), 1.5f, nk_rgb(150, 150, 160));
            for (int i = 1; i < NP; i++)
                if (lin[i] == lin[i] && lin[i - 1] == lin[i - 1])
                    nk_stroke_line(cv, x0 + w * (i - 1) / (NP - 1), LY(lin[i - 1]), x0 + w * i / (NP - 1), LY(lin[i]), 2.f, nk_rgb(70, 140, 230));
            for (int i = 0; i < n; i++) {
                if (tot[i] != tot[i]) continue;
                float x = x0 + w * i / (n - 1), y = LY(tot[i]);
                if (i && tot[i - 1] == tot[i - 1]) nk_stroke_line(cv, x0 + w * (i - 1) / (n - 1), LY(tot[i - 1]), x, y, 2.f, nk_rgb(255, 140, 30));
                nk_fill_circle(cv, nk_rect(x - 2 * s, y - 2 * s, 4 * s, 4 * s), nk_rgb(255, 200, 120));
            }
            #undef LY
            float tx = x0;
            const char* keys[3] = { "total", "membrane + bending", "membrane" };
            struct nk_color kc[3] = { nk_rgb(255, 140, 30), nk_rgb(70, 140, 230), nk_rgb(150, 150, 160) };
            for (int k = 0; k < 3; k++) {
                float ty = y0 + h + 2 * s + font->height * 0.5f;
                nk_stroke_line(cv, tx, ty, tx + 16 * s, ty, 2.f, kc[k]);
                float tw = font->width(font->userdata, font->height, keys[k], (int)strlen(keys[k]));
                nk_draw_text(cv, nk_rect(tx + 20 * s, y0 + h + 2 * s, tw + 4, font->height), keys[k], (int)strlen(keys[k]), font, nk_rgba(0, 0, 0, 0), P.dim);
                tx += tw + 36 * s;
            }
            snprintf(txt, sizeof txt, "0 .. %.4g", G.lin_t);
            float tw = font->width(font->userdata, font->height, txt, (int)strlen(txt));
            nk_draw_text(cv, nk_rect(x0 + w - tw - 2, y0 + h + 2 * s, tw + 2, font->height), txt, (int)strlen(txt), font, nk_rgba(0, 0, 0, 0), P.dim);
        }
    }
}

/* ---- path: the field along the line (or the surface path) between two picked
   nodes; the same line linearized (ASME) on demand -------------------------------- */
static void path_plot(struct nk_context* ctx, float s, float row) {
    char txt[200];
    struct nk_rect area;
    float sp = row + ctx->style.window.spacing.y;
    nk_layout_row_dynamic(ctx, CV_MAX(nk_window_get_content_region(ctx).h - 3 * sp - 4 * s, row), 1);   /* two header rows, the stats */
    if (G.elem_mode || !G.has_field) {
        nk_label(ctx, G.elem_mode ? "(per-element mode: switch to nodal values to plot)" : "(no field)", NK_TEXT_LEFT);
        return;
    }
    float L = CV_MAX(G.path_dist[G.path_n - 1], 1e-30f), lo = 1e30f, hi = -1e30f, sum = 0, dlo = 0, dhi = 0;
    int nv = 0;
    for (uint32_t i = 0; i < G.path_n; i++) {
        float v = app_path_value(i);
        if (v != v) continue;
        if (v < lo) { lo = v; dlo = G.path_dist[i]; }
        if (v > hi) { hi = v; dhi = G.path_dist[i]; }
        sum += v; nv++;
    }
    if (nk_widget(&area, ctx) != NK_WIDGET_INVALID) {
        struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
        const struct nk_user_font* font = ctx->style.font;
        float lm = 60 * s, x0 = area.x + lm, y0 = area.y + 4 * s, w = area.w - lm - 6 * s, h = area.h - font->height - 8 * s;
        nk_fill_rect(cv, nk_rect(x0, y0, w, h), 0, P.plot_bg);
        float a = nv ? lo : 0, b = nv ? hi : 1;
        if (b <= a) { b = a + fabsf(a) * 0.01f + 1e-30f; a -= b - a; }
        for (int k = 0; k <= 4; k++) {                  /* four bands of the value axis */
            float v = a + (b - a) * k / 4.f, y = y0 + h * (1 - k / 4.f);
            nk_stroke_line(cv, x0, y, x0 + w, y, 1, P.grid);
            tick_num(txt, sizeof txt, v, (b - a) / 4, CV_MAX(fabsf(a), fabsf(b)));
            nk_draw_text(cv, nk_rect(area.x, y - font->height * 0.5f, lm - 4 * s, font->height), txt, (int)strlen(txt), font, nk_rgba(0, 0, 0, 0), P.dim);
        }
        #define PY(v) (y0 + h * (1 - ((v) - a) / (b - a)))
        float px = 0, py = 0;
        bool prev = false;
        for (uint32_t i = 0; i < G.path_n; i++) {
            float v = app_path_value(i);
            float x = x0 + w * G.path_dist[i] / L, y = PY(v);
            if (v != v) { prev = false; continue; }          /* outside the solid: a gap */
            if (prev) nk_stroke_line(cv, px, py, x, y, 2.f, nk_rgb(255, 140, 30));
            if (G.path_surface) nk_fill_circle(cv, nk_rect(x - 2 * s, y - 2 * s, 4 * s, 4 * s), nk_rgb(255, 200, 120));
            px = x; py = y; prev = true;
        }
        /* the value under the mouse: the nearest sample */
        struct nk_rect pr = nk_rect(x0, y0, w, h);
        if (nk_input_is_mouse_hovering_rect(&ctx->input, pr)) {
            float d = (ctx->input.mouse.pos.x - x0) / w * L;
            uint32_t best = 0;
            for (uint32_t i = 1; i < G.path_n; i++)
                if (fabsf(G.path_dist[i] - d) < fabsf(G.path_dist[best] - d)) best = i;
            float v = app_path_value(best), x = x0 + w * G.path_dist[best] / L;
            nk_stroke_line(cv, x, y0, x, y0 + h, 1, P.dim);
            if (v == v) {
                nk_fill_circle(cv, nk_rect(x - 4 * s, PY(v) - 4 * s, 8 * s, 8 * s), nk_rgb(255, 140, 30));
                char num[32];
                legend_num(num, sizeof num, v);
                snprintf(txt, sizeof txt, "%s at %.4g", num, G.path_dist[best]);
                float tw = font->width(font->userdata, font->height, txt, (int)strlen(txt));
                float tx = x + 6 * s + tw > x0 + w ? x - 6 * s - tw : x + 6 * s;
                nk_fill_rect(cv, nk_rect(tx - 2, y0 + 2 * s, tw + 4, font->height + 2), 0, P.plot_bg);
                nk_draw_text(cv, nk_rect(tx, y0 + 2 * s, tw + 2, font->height), txt, (int)strlen(txt), font, nk_rgba(0, 0, 0, 0), P.dim);
            }
        }
        #undef PY
        snprintf(txt, sizeof txt, "distance along the %s: 0 .. %.4g", G.path_surface ? "surface" : "line", L);
        nk_draw_text(cv, nk_rect(x0, y0 + h + 2 * s, w, font->height), txt, (int)strlen(txt), font, nk_rgba(0, 0, 0, 0), P.dim);
    }
    nk_layout_row_dynamic(ctx, row, 1);
    if (nv) {
        char l[32], m[32], hh[32], e0[32], e1[32];
        float v0 = app_path_value(0), v1 = app_path_value(G.path_n - 1);
        legend_num(l, sizeof l, lo); legend_num(hh, sizeof hh, hi); legend_num(m, sizeof m, sum / nv);
        legend_num(e0, sizeof e0, v0); legend_num(e1, sizeof e1, v1);
        snprintf(txt, sizeof txt, "min %s at %.4g   max %s at %.4g   mean %s   ends %s, %s", l, dlo, hh, dhi, m,
                 v0 == v0 ? e0 : "-", v1 == v1 ? e1 : "-");
    } else snprintf(txt, sizeof txt, "the line does not pass through the solid");
    tip(ctx, txt);
    nk_label_colored(ctx, txt, NK_TEXT_LEFT, P.dim);
}

void window_path(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static const char* in_words[] = { "", "along the normal", "along X", "along Y", "along Z" };
    static bool was_open;
    if (!G.path_open || !G.path_n) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Path", NK_SHOWN);
    was_open = true;
    if (nk_begin(ctx, "Path", nk_rect(fw * 0.5f, fh * 0.06f, fw * 0.45f, fh * 0.5f),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        char txt[300], where[120];
        nk_layout_row_template_begin(ctx, row);           /* the controls; the description gets its own row */
        nk_layout_row_template_push_static(ctx, 115 * s);
        if (!G.path_dir) nk_layout_row_template_push_static(ctx, 115 * s);
        nk_layout_row_template_push_static(ctx, 90 * s);
        if (G.path_lin) nk_layout_row_template_push_static(ctx, 110 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 50 * s);
        nk_layout_row_template_end(ctx);
        tip(ctx, "From the first node: to the second picked node, or along the surface normal or an\n"
                 "axis into the solid, up to the face where the line comes out");
        int dir = nk_combo(ctx, path_dirs, 5, G.path_dir, (int)row, nk_vec2(130 * s, 5 * row + 20 * s));
        if (dir != G.path_dir) {
            if (dir) { G.path_dir = dir; app_path_rebuild(); }
            else if (G.path_to < G.frd.n_nodes && G.path_to != G.path_end[0]) { G.path_dir = 0; G.path_end[1] = G.path_to; app_path_rebuild(); }
            else app_path_start(G.path_end[0]);     /* no end node yet: pick one */
            if (!G.path_n) { nk_end(ctx); return; }
        }
        if (!G.path_dir) {
            tip(ctx, "Plot over the shortest path on the surface edges instead of the straight line\n"
                     "(the linearization always uses the straight line)");
            bool surf = G.path_surface;
            nk_checkbox_label(ctx, "along surface", &G.path_surface);
            if (surf != G.path_surface) {
                if (G.path_surface) G.path_lin = false;     /* a surface path is plotted, not linearized */
                app_path_rebuild();
                if (!G.path_n) { nk_end(ctx); return; }
            }
        }
        tip(ctx, "ASME VIII-2 5-A stress linearization on the straight line: membrane, bending and peak\n"
                 "(off: the field along the path). The line should cross the wall.");
        if (nk_checkbox_label(ctx, "linearize", &G.path_lin) && G.path_lin && G.path_surface) {
            G.path_surface = false;                         /* linearization is on the straight line */
            app_path_rebuild();
            if (!G.path_n) { nk_end(ctx); return; }
        }
        if (G.path_lin) lin_quantity(ctx, s, row);
        nk_spacing(ctx, 1);
        tip(ctx, G.path_lin ? "Save the linearization to a CSV next to the result file" : "Save the path values to a CSV next to the result file");
        if (nk_button_label(ctx, "CSV")) {
            char vp[1100];
            snprintf(vp, sizeof vp, G.path_lin ? "%.*s_linearized.csv" : "%.*s_path.csv", (int)(strrchr(G.path, '.') && strrchr(G.path, '.') > strrchr(G.path, cv_path_sep()) ? strrchr(G.path, '.') - G.path : (int)strlen(G.path)), G.path);
            snprintf(G.note, sizeof G.note, (G.path_lin ? app_lin_csv(vp) : app_path_csv(vp)) ? "saved %s" : "could not write %s", vp); G.note_t = cv_now();
        }
        if (G.path_dir) snprintf(where, sizeof where, "node %u %s", G.frd.node_id[G.path_end[0]], in_words[CV_MIN(G.path_dir, 4)]);
        else snprintf(where, sizeof where, "nodes %u .. %u", G.frd.node_id[G.path_end[0]], G.frd.node_id[G.path_end[1]]);
        if (G.path_lin) {
            const cv_field_desc* d = G.lin_fi >= 0 ? &G.frd.steps[G.step].fields[G.lin_fi] : NULL;
            snprintf(txt, sizeof txt, "%s linearized, %s, t = %.4g (ASME VIII-2 5-A)", d ? d->name : "no stress", where, G.lin_t);
        } else if (G.path_surface)
            snprintf(txt, sizeof txt, "%s, %s over %u surface nodes, length %.4g", G.has_field ? G.field_label : "no field", where, G.path_n, G.path_dist[G.path_n - 1]);
        else
            snprintf(txt, sizeof txt, "%s, %s, length %.4g", G.has_field ? G.field_label : "no field", where, G.path_dist[G.path_n - 1]);
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, txt);
        nk_label(ctx, txt, NK_TEXT_LEFT);
        if (G.path_lin) lin_body(ctx, s, row);
        else path_plot(ctx, s, row);
    }
    if (nk_window_is_hidden(ctx, "Path")) app_path_clear();
    nk_end(ctx);
}

/* ---- history plot: the field at the probed node over every step ---------------- */
void window_history(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.hist_open) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "History", NK_SHOWN);
    was_open = true;
    if (nk_begin(ctx, "History", nk_rect(fw * 0.3f, fh * 0.5f, fw * 0.42f, fh * 0.36f),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        char txt[200];
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 70 * s);
        nk_layout_row_template_push_static(ctx, 60 * s);
        nk_layout_row_template_end(ctx);
        snprintf(txt, sizeof txt, "%s at %s %u, %d steps", G.field_label, G.elem_mode ? "element" : "node",
                 G.elem_mode ? G.frd.elem_id[G.hist_elem] : G.frd.node_id[G.hist_node], G.hist_n);
        nk_label(ctx, txt, NK_TEXT_LEFT);
        tip(ctx, "x axis: step number instead of time (modal steps store the frequency as time)");
        nk_checkbox_label(ctx, "by step", &G.hist_by_step);
        if (nk_button_label(ctx, "CSV")) {
            char vp[1100];
            snprintf(vp, sizeof vp, "%.*s_history.csv", (int)(strrchr(G.path, '.') && strrchr(G.path, '.') > strrchr(G.path, cv_path_sep()) ? strrchr(G.path, '.') - G.path : (int)strlen(G.path)), G.path);
            snprintf(G.note, sizeof G.note, app_hist_csv(vp) ? "saved %s" : "could not write %s", vp); G.note_t = cv_now();
        }
        struct nk_rect area;
        nk_layout_row_dynamic(ctx, nk_window_get_content_region(ctx).h - row - 4 * s, 1);
        if (nk_widget(&area, ctx) != NK_WIDGET_INVALID && G.hist_n > 0) {
            struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
            const struct nk_user_font* font = ctx->style.font;
            float lm = 76 * s, x0 = area.x + lm, y0 = area.y + 4 * s, w = area.w - lm - 6 * s, h = area.h - font->height - 8 * s;
            nk_fill_rect(cv, nk_rect(x0, y0, w, h), 0, P.plot_bg);
            float lo = 1e30f, hi = -1e30f, xa = 1e30f, xb = -1e30f;
            for (int i = 0; i < G.hist_n; i++) {
                float v = G.hist_v[i], x = G.hist_by_step ? (float)(G.hist_step[i] + 1) : G.hist_t[i];
                if (v == v) { lo = CV_MIN(lo, v); hi = CV_MAX(hi, v); }
                xa = CV_MIN(xa, x); xb = CV_MAX(xb, x);
            }
            if (lo > hi) { lo = 0; hi = 1; }
            if (hi <= lo) { hi = lo + fabsf(lo) * 0.01f + 1e-30f; lo -= hi - lo; }
            if (xb <= xa) { xb = xa + 1; }
            for (int k = 0; k <= 4; k++) {
                float v = lo + (hi - lo) * k / 4.f, y = y0 + h * (1 - k / 4.f);
                nk_stroke_line(cv, x0, y, x0 + w, y, 1, P.grid);
                legend_num(txt, sizeof txt, v);
                nk_draw_text(cv, nk_rect(area.x, y - font->height * 0.5f, lm - 4 * s, font->height), txt, (int)strlen(txt), font, nk_rgba(0, 0, 0, 0), P.dim);
            }
            int near = -1; float nd = 1e30f;
            struct nk_vec2 m = ctx->input.mouse.pos;
            bool inside = nk_input_is_mouse_hovering_rect(&ctx->input, nk_rect(x0, y0, w, h));
            float px = 0, py = 0;
            bool have = false;
            for (int i = 0; i < G.hist_n; i++) {
                float xv = G.hist_by_step ? (float)(G.hist_step[i] + 1) : G.hist_t[i];
                float x = x0 + w * (xv - xa) / (xb - xa);
                if (G.hist_step[i] == G.step) nk_stroke_line(cv, x, y0, x, y0 + h, 1.5f, P.accent);
                if (fabsf(x - m.x) < nd) { nd = fabsf(x - m.x); near = i; }
                float v = G.hist_v[i];
                if (v != v) { have = false; continue; }
                float y = y0 + h * (1 - (v - lo) / (hi - lo));
                if (have) nk_stroke_line(cv, px, py, x, y, 2.f, nk_rgb(255, 140, 30));
                nk_fill_circle(cv, nk_rect(x - 2 * s, y - 2 * s, 4 * s, 4 * s), nk_rgb(255, 200, 120));
                px = x; py = y; have = true;
            }
            if (inside && near >= 0) {                   /* hover: the point's values; click: go to that step */
                char num[32];
                fmt_num(num, sizeof num, G.hist_v[near]);
                snprintf(txt, sizeof txt, "step %d  t=%.4g  %s", G.hist_step[near] + 1, G.hist_t[near], num);
                tip_show(ctx, txt);
                if (nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) app_set_step(G.hist_step[near]);
            }
            snprintf(txt, sizeof txt, "%s %.4g .. %.4g   (click: go to that step)", G.hist_by_step ? "step" : "time", xa, xb);
            nk_draw_text(cv, nk_rect(x0, y0 + h + 2 * s, w, font->height), txt, (int)strlen(txt), font, nk_rgba(0, 0, 0, 0), P.dim);
        } else if (G.field_src == 1) {
            nk_label(ctx, "(history of .frd fields only)", NK_TEXT_LEFT);
        }
    }
    if (nk_window_is_hidden(ctx, "History")) app_hist_close();
    nk_end(ctx);
}

/* ---- convergence window: the run's iterations from .cvg, increments from .sta ----
   Residual force (percent, log scale) per Newton iteration, one colour per
   step; increment boundaries as ticks, cutbacks in red. */
void window_convergence(struct nk_context* ctx, float s, float row, int fw, int fh) {
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
