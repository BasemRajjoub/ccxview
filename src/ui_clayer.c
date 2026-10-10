/* ui_clayer.c -- the interface layer's rows in the Contact window (what it is coloured
   by, its opacity, outline, least thickness, colour ends) and its colour bar in the
   contact key (app_clayer.c). */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "contact.h"
#include <math.h>

/* a label and the row's widgets after it in a column of the same width as the window's others */
static void lead(struct nk_context* ctx, float s, float row, const char* lab, const char* help, float right) {
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 110 * s);
    nk_layout_row_template_push_dynamic(ctx);
    if (right > 0) nk_layout_row_template_push_static(ctx, right);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, lab, NK_TEXT_LEFT);
    tip(ctx, help);
}

/* a length or value that is 0 for automatic: its box, and the value in use beside it */
static bool auto_box(struct nk_context* ctx, float s, float row, const char* lab, const char* id, float* v, float used, const char* help) {
    lead(ctx, s, row, lab, help, 110 * s);
    float before = *v, step = CV_MAX(fabsf(used), 1e-9f) * 0.1f;
    nk_property_float(ctx, id, 0.f, v, 1e30f, step, step * 0.1f);
    char t[48], n[24];
    fmt_num(n, sizeof n, used);
    snprintf(t, sizeof t, *v > 0 ? "%s" : "auto %s", n);
    nk_label_colored(ctx, t, NK_TEXT_RIGHT, P.dim);
    return *v != before;
}

int uii_clayer_rows(void) { return (G.cel_mode & CV_CMODE_LAYER) ? 4 + (G.cel_layer_by == CV_CLBY_CPRESS || G.cel_layer_by == CV_CLBY_CSLIP || G.cel_layer_by == CV_CLBY_CSHEAR ? 2 : 0) : 0; }

void uii_section_clayer(struct nk_context* ctx, float s, float row) {
    if (!(G.cel_mode & CV_CMODE_LAYER)) return;
    const cv_cinfo* I = app_cdisp_info();
    bool ch = false;
    lead(ctx, s, row, "layer coloured by",
         "What colours the interface layer, interpolated over its faces: the gap (the gap colours),\n"
         "CPRESS (0 where open), |CSLIP|, |CSHEAR|, or the status (a patch per corner, its fixed colours)", 0);
    int by = G.cel_layer_by >= 0 && G.cel_layer_by < CV_CLBY_N ? G.cel_layer_by : 0;
    int nb = nk_combo(ctx, cv_clby_names, CV_CLBY_N, by, (int)row, nk_vec2(200 * s, CV_CLBY_N * (row + 4 * s) + 20 * s));
    if (nb != by) { G.cel_layer_by = nb; ch = true; }
    lead(ctx, s, row, "layer opacity", "The interface layer's opacity: less to see its far side and the faces inside it", 36 * s);
    ch |= ui_slider_float(ctx, 0.05f, &G.cel_layer_alpha, 1.f, 0.05f);
    char v[16];
    snprintf(v, sizeof v, "%.2f", G.cel_layer_alpha);
    nk_label(ctx, v, NK_TEXT_RIGHT);
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "The interface layer's edges outlined (a face with a corner penetrating its master is always\n"
             "outlined, in the penetrating colour)");
    ch |= nk_checkbox_label(ctx, "layer outlined", &G.cel_layer_edges);
    lead(ctx, s, row, "least thickness", "The layer at least this thick (model units), so a closed contact still shows a thin\n"
                                         "coloured skin, grown towards the master; 0: as thick as the gap, nothing where closed", 110 * s);
    float before = G.cel_layer_min, step = CV_MAX(I->near, 1e-9f) * 0.1f;
    nk_property_float(ctx, "#min", 0.f, &G.cel_layer_min, 1e30f, step, step * 0.1f);
    nk_label_colored(ctx, G.cel_layer_min > 0 ? "" : "none", NK_TEXT_RIGHT, P.dim);
    ch |= G.cel_layer_min != before;
    if (by == CV_CLBY_CPRESS || by == CV_CLBY_CSLIP || by == CV_CLBY_CSHEAR) {
        ch |= auto_box(ctx, s, row, "layer colours from", "#lo", &G.cel_layer_lo, I->llo,
                       "The low end of the layer's colours (blue): 0 for 0");
        ch |= auto_box(ctx, s, row, "layer colours to", "#hi", &G.cel_layer_hi, I->lhi,
                       "The high end of the layer's colours (red): 0 for the largest shown");
    }
    if (ch) app_contact_refresh();
}

/* the layer's colour at value x of its range */
static void layer_rgb(const cv_cinfo* I, float x, float c[3]) {
    if (I->lby == CV_CLBY_GAP) cv_contact_gap_rgb(CV_CGAP_LINKS, x, I->llo, I->lhi, I->tol, c);
    else app_clayer_map((x - I->llo) / CV_MAX(I->lhi - I->llo, 1e-30f), c);
}

void uii_clayer_bar(struct nk_command_buffer* cv, const struct nk_user_font* f, float x, float y, float w, float lh,
                    struct nk_color ink, float s) {
    const cv_cinfo* I = app_cdisp_info();
    int n = 48;
    float bh = lh * 0.42f, bw = w / (float)n;
    for (int i = 0; i < n; i++) {
        float c[3];
        layer_rgb(I, I->llo + (I->lhi - I->llo) * ((float)i + 0.5f) / (float)n, c);
        nk_fill_rect(cv, nk_rect(x + bw * (float)i, y + lh * 0.3f, bw + 1.f, bh), 0, nk_rgb_f(c[0], c[1], c[2]));
    }
    nk_stroke_rect(cv, nk_rect(x, y + lh * 0.3f, w, bh), 0, 1, ink);
    char a[24], b[24];
    fmt_num(a, sizeof a, I->llo); fmt_num(b, sizeof b, I->lhi);
    float ty = y + lh, wa = ink_width(f, a, (int)strlen(a)), wb = ink_width(f, b, (int)strlen(b));
    ink_text(cv, f, x, ty, wa + 4, a, ink);
    ink_text(cv, f, x + w - wb, ty, wb + 4, b, ink);
    if (I->lby == CV_CLBY_GAP && I->llo < 0 && I->lhi > 0) {     /* 0 marked, as on the gap bars */
        float z = x + w * (-I->llo) / (I->lhi - I->llo), w0 = ink_width(f, "0", 1);
        nk_stroke_line(cv, z, y + lh * 0.18f, z, y + lh * 0.3f + bh + 2 * s, 1.5f * s, ink);
        float zx = CV_MAX(x + wa + 6 * s, CV_MIN(z - w0 * 0.5f, x + w - wb - w0 - 6 * s));
        ink_text(cv, f, zx, ty, w0 + 4, "0", ink);
    }
}
