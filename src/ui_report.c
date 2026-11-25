/* ui_report.c -- the Report builder window: what goes into the model report (its
   parts, pictures of the view from the isometric and the axis directions) and
   whether it is a PDF or Markdown. The pictures and the writing are app_report.c. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "report.h"

#define WIN "Report builder"

static const char* const part_tip[CV_RP_N] = {
    "The files, solver, date and units",
    "Each step's procedure, time period, NLGEOM and increments (from the deck)",
    "Node and element counts, the bounding box, the elements by type",
    "Each node and element set with its size (from the deck)",
    "Aspect ratio, Jacobian, angles ... min, max, mean and how many are past the usual limit",
    "Elastic constants, first yield stress, density, expansion ... (from the deck)",
    "Solid, shell and beam sections, orientations and transforms (from the deck)",
    "Per step: the fixed and prescribed DOFs, forces, pressures, body loads; the amplitudes",
    "Rigid bodies, couplings, equations, ties, contact pairs, surfaces, springs ...",
};

static const char* const pic_tip[CV_REP_PIC_N] = {
    "The view as it is now",
    "From above, turned about the up axis (as the view button)",
    "Looking at the front: from -Y when Z is up, from +Z when Y is up",
    "Looking down the up axis",
    "From +X",
};

/* bits of *mask as checkboxes, two to a row; inv: a set bit is a box unticked */
static void checks(struct nk_context* ctx, int* mask, bool inv, int n, const char* (*name)(int), const char* const* tips) {
    for (int i = 0; i < n; i += 2) {
        nk_bool v[2];
        for (int k = 0; k < 2; k++) v[k] = i + k < n && (((*mask >> (i + k)) & 1) != inv);
        bool ch = i + 1 < n ? uik_check2(ctx, name(i), &v[0], tips[i], name(i + 1), &v[1], tips[i + 1])
                            : uik_check(ctx, name(i), &v[0], tips[i]);
        if (!ch) continue;
        for (int k = 0; k < 2 && i + k < n; k++) {
            if (v[k] != inv) *mask |= 1 << (i + k); else *mask &= ~(1 << (i + k));
        }
    }
}

void uii_window_report(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_report) { was_open = false; return; }
    if (app_report_busy()) return;                    /* taking the pictures: out of the view */
    if (!was_open) nk_window_show(ctx, WIN, NK_SHOWN);
    was_open = true;
    float w = CV_MIN(520 * s, fw * 0.92f), h = CV_MIN(fh * 0.9f, 560 * s);
    if (nk_begin(ctx, WIN, nk_rect((fw - w) / 2, (fh - h) / 2, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(ctx, 2 * row, 1);
        nk_label_colored_wrap(ctx, "The model definition, as read from the files, with pictures of the view. "
                                   "Parts with nothing in the model are left out.", P.dim);

        nk_layout_row_dynamic(ctx, row, 1);
        nk_label(ctx, "Contents", NK_TEXT_LEFT);
        checks(ctx, &G.rep_skip, true, CV_RP_N, cv_report_part_name, part_tip);
        static const char* const al[2] = { "All", "None" };
        static const char* const at[2] = { "Every part", "No part: only the pictures" };
        int hit = uik_buttons(ctx, NULL, 2, al, at);
        if (hit == 0) G.rep_skip = 0;
        if (hit == 1) G.rep_skip = (1 << CV_RP_N) - 1;
        uii_hsep(ctx, s);

        nk_layout_row_dynamic(ctx, row, 1);
        nk_label(ctx, "Pictures", NK_TEXT_LEFT);
        checks(ctx, &G.rep_pics, false, CV_REP_PIC_N, app_report_pic_name, pic_tip);
        nk_layout_row_dynamic(ctx, 2 * row, 1);
        nk_label_colored_wrap(ctx, "Each as the scene is drawn now (field, deformation, legend), the model fitted; "
                                   "the view is put back after.", P.dim);
        uii_hsep(ctx, s);

        static const char* const fm[2] = { "PDF (A4)", "Markdown, pictures as PNG files" };
        int f = G.rep_pdf ? 0 : 1;
        if (uik_combo(ctx, "Format", fm, 2, &f, "A PDF has the pictures in it; Markdown links PNG files saved beside it"))
            G.rep_pdf = f == 0;
        static const char* const cl[1] = { "Create report" };
        static const char* const ct[1] = { "To <model>_report.pdf (or .md) beside the model" };
        if (uik_buttons(ctx, NULL, 1, cl, ct) == 0) app_report_build();
    }
    if (nk_window_is_hidden(ctx, WIN)) G.show_report = false;
    nk_end(ctx);
}
