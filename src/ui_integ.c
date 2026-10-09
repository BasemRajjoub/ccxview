/* ui_integ.c -- the Integrals window: the field integrated over a set at every
   step (app_integ.c), the current step's values in a table, one of them plotted
   over the steps, and the CSV. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"

/* what is integrated over: the kind and the region (shown, the selection, a deck
   set or surface that fits the kind); a pick opens the window over it */
static void region_row(struct nk_context* ctx, float s, float row) {
    enum { NT = 256 };
    const char* names[NT];
    int nt = 0, cur = 0;
    names[nt++] = "shown";
    if (G.sel_n || G.seln_n) names[nt++] = "selection";
    const cv_inp* d = deck_get();
    for (int i = 0; d && i < d->nsets && nt < NT; i++)
        if (d->sets[i].is_elem || GI.kind == CV_IK_NODES) names[nt++] = d->sets[i].name;
    for (int i = 0; d && i < d->nsurfs && nt < NT; i++)
        if (GI.kind != CV_IK_VOLUME && (d->surfs[i].n || GI.kind == CV_IK_NODES)) names[nt++] = d->surfs[i].name;
    for (int i = 0; i < nt; i++) if (!strcasecmp(names[i], GI.target)) cur = i;
    static const char* kinds[CV_IK_N] = { "volume of", "surface of", "sum over the nodes of" };
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 170 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    tip(ctx, "Volume: the elements' volume integral and average. Surface: the faces' area integral and the force\n"
             "through them (a surface by its faces, an element set or the shown part by their outer faces).\n"
             "Nodes: a sum (reaction forces)");
    int k = nk_combo(ctx, kinds, CV_IK_N, GI.kind, (int)row, nk_vec2(200 * s, CV_IK_N * row + 20 * s));
    tip(ctx, "Over what: everything shown, the box selection, or a set or surface of the deck");
    int t = nk_combo(ctx, names, nt, cur, (int)row, nk_vec2(220 * s, CV_MIN(nt, 12) * (row + 4 * s) + 20 * s));
    if ((k != GI.kind || t != cur) && !app_integ_open(k, names[t]) && k != GI.kind) app_integ_open(k, "shown");
}

void uii_window_integrals(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open, by_step;
    static int plot_q = -1;
    if (!GI.open) { was_open = false; plot_q = -1; return; }
    if (!was_open) {
        nk_window_show(ctx, "Integrals", NK_SHOWN);
        by_step = false;                        /* time, unless it does not run forward (modal: frequencies) */
        for (int i = 1; i < GI.n; i++) if (GI.t[i] < GI.t[i - 1] || G.frd.steps[GI.step[i]].modal) by_step = true;
    }
    was_open = true;
    if (plot_q < 0 || plot_q >= GI.nq) {        /* the component shown, else the invariant shown (the last integral) */
        plot_q = 0;
        for (int q = 0; q < GI.nq; q++) if (GI.how[q] == CV_IQ_INTEG) plot_q = q;
        if (G.comp >= 0 && G.comp < GI.nq) plot_q = G.comp;
    }
    if (nk_begin(ctx, "Integrals", nk_rect(fw * 0.3f, fh * 0.25f, fw * 0.42f, fh * 0.66f),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        char txt[260], num[32];
        float used = 0, sp = ctx->style.window.spacing.y;
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 70 * s);
        nk_layout_row_template_push_static(ctx, 60 * s);
        nk_layout_row_template_end(ctx);
        snprintf(txt, sizeof txt, "%s, %s", GI.what, G.field_label[0] ? G.field_label : "no field");
        tip(ctx, GI.kind == CV_IK_VOLUME ? "The field integrated over the elements' volume (undeformed), and its volume average:\n"
                                           "the homogenised value of an RVE, <S> = (1/V) \xe2\x88\xab S dV per component"
               : GI.kind == CV_IK_SURFACE ? "The field integrated over the faces' area (undeformed), its area average, and the force\n"
                                            "the stress carries through them (\xe2\x88\xab S n dA, n outward) or a pressure puts on them"
               : "The field summed over the nodes (reaction forces RF: the total force), and its moment about a point");
        nk_label(ctx, txt, NK_TEXT_LEFT);
        tip(ctx, "x axis: step number instead of time (modal steps store the frequency as time)");
        nk_checkbox_label(ctx, "by step", &by_step);
        char vp[1100];
        app_integ_csv_path(vp, sizeof vp);
        tip(ctx, "Every step's row (step, time, volume or area, integral and average per component) to <model>_integral_<set>.csv");
        if (nk_button_label(ctx, "CSV")) {
            snprintf(G.note, sizeof G.note, app_integ_csv(vp) ? "saved %s" : "could not write %s", vp); G.note_t = cv_now();
        }
        used += row + sp;
        region_row(ctx, s, row);
        used += row + sp;
        if (GI.kind == CV_IK_NODES) {
            static const char* nm[3] = { "#x", "#y", "#z" };
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 110 * s);
            for (int k = 0; k < 3; k++) nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);
            nk_label(ctx, "moment about", NK_TEXT_LEFT);
            float st = G.diag * 0.01f + 1e-30f;
            for (int k = 0; k < 3; k++) {
                tip(ctx, "The point the moment of the forces is taken about");
                float v = nk_propertyf(ctx, nm[k], -1e30f, (float)GI.about[k], 1e30f, st, st * 0.1f);
                if (v != (float)GI.about[k]) { GI.about[k] = v; app_integ_refresh(); }
            }
            used += row + sp;
        }
        if (GI.note[0]) {
            nk_layout_row_dynamic(ctx, row, 1);
            tip(ctx, GI.note);
            nk_label_colored(ctx, GI.note, NK_TEXT_LEFT, P.warn);
            used += row + sp;
        }
        /* this step's row: the table */
        int cur = -1;
        for (int i = 0; i < GI.n; i++) if (GI.step[i] == G.step) cur = i;
        int lines = 2 + GI.nq;
        float th = CV_MIN(lines, 12) * (row + sp) + 2 * ctx->style.window.group_padding.y + 2 * s;
        nk_layout_row_dynamic(ctx, th, 1);
        used += th + sp;
        if (nk_group_begin(ctx, "#integ table", lines > 12 ? 0 : NK_WINDOW_NO_SCROLLBAR)) {
            bool sum = GI.kind == CV_IK_NODES;
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 150 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);
            snprintf(txt, sizeof txt, cur >= 0 ? "step %d" : "step %d: not there", G.step + 1);
            nk_label_colored(ctx, txt, NK_TEXT_LEFT, P.dim);
            nk_label_colored(ctx, sum ? "sum" : "integral", NK_TEXT_RIGHT, P.dim);
            nk_label_colored(ctx, sum ? "" : "average", NK_TEXT_RIGHT, P.dim);
            const char* size = GI.kind == CV_IK_NODES ? "nodes" : GI.kind == CV_IK_SURFACE ? "area" : "volume";
            const char* lu = app_unit("DISP", 0);
            if (GI.kind != CV_IK_NODES && lu[0]) {          /* in the length shown, as the values */
                snprintf(txt, sizeof txt, "%s [%s^%d]", size, lu, GI.kind == CV_IK_SURFACE ? 2 : 3);
                size = txt;
            }
            nk_label(ctx, size, NK_TEXT_LEFT);
            if (cur >= 0) fmt_num(num, sizeof num, GI.size[cur]); else snprintf(num, sizeof num, "-");
            nk_label(ctx, num, NK_TEXT_RIGHT);
            nk_label(ctx, "", NK_TEXT_RIGHT);
            for (int q = 0; q < GI.nq; q++) {
                double v = cur >= 0 ? GI.val[(size_t)cur * GI.nq + q] : NAN;
                tip(ctx, "Plot this one over the steps (an integral's average)");
                if (nk_option_label(ctx, GI.name[q], plot_q == q)) plot_q = q;
                fmt_num(num, sizeof num, v);
                nk_label(ctx, cur >= 0 ? num : "-", NK_TEXT_RIGHT);
                if (GI.how[q] == CV_IQ_INTEG && cur >= 0 && GI.size[cur] > 0) fmt_num(num, sizeof num, v / GI.size[cur]);
                else snprintf(num, sizeof num, GI.how[q] == CV_IQ_INTEG ? "-" : "");
                nk_label(ctx, num, NK_TEXT_RIGHT);
            }
            nk_group_end(ctx);
        }
        /* the plot: the quantity picked, over the steps */
        struct nk_rect area;
        nk_layout_row_dynamic(ctx, row, 1);
        bool avg = plot_q < GI.nq && GI.how[plot_q] == CV_IQ_INTEG;
        snprintf(txt, sizeof txt, "%s %s over the steps", plot_q < GI.nq ? GI.name[plot_q] : "", avg ? "average" : GI.kind == CV_IK_NODES ? "sum" : "");
        nk_label_colored(ctx, txt, NK_TEXT_LEFT, P.dim);
        used += row + sp;
        nk_layout_row_dynamic(ctx, CV_MAX(nk_window_get_content_region(ctx).h - used - 4 * s, row), 1);
        if (nk_widget(&area, ctx) != NK_WIDGET_INVALID && GI.n > 0 && plot_q < GI.nq) {
            float* y = malloc((size_t)GI.n * sizeof *y);
            for (int i = 0; y && i < GI.n; i++) {
                double v = GI.val[(size_t)i * GI.nq + plot_q];
                y[i] = (float)(avg ? (GI.size[i] > 0 ? v / GI.size[i] : NAN) : v);
            }
            if (y) uii_plot_steps(ctx, s, area, GI.n, GI.t, GI.step, y, by_step);
            free(y);
        }
    }
    if (nk_window_is_hidden(ctx, "Integrals")) app_integ_close();
    nk_end(ctx);
}
