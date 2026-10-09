/* ui_rebar.c -- the Reinforcement window: the design values of the REBAR field
   (rebar.h, app_shell.c), concrete fcd, steel fyd and the cover, in the model's
   units, and which of its components to show. Opened from REBAR in the Fields panel. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "units.h"

/* the model's unit of quantity q ("MPa"), "" when the system is not set */
static const char* in_unit(int q) {
    const cv_unit* u = cv_unit_get(q, cv_unit_input(G.units, cv_sys_temp(G.units), G.unit_in[q], q));
    return u ? u->name : "";
}

/* one value: its name, the field, its unit; true when it changed */
static bool value_row(struct nk_context* ctx, float s, float row, const char* name, const char* id, const char* tp, float* v, int q) {
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 70 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 50 * s);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, name, NK_TEXT_LEFT);
    tip(ctx, tp);
    float was = *v, st = *v * 0.05f + 1e-12f;
    *v = nk_propertyf(ctx, id, 1e-6f, *v, 1e9f, st, st * 0.1f);
    nk_label(ctx, in_unit(q), NK_TEXT_LEFT);
    return *v != was;
}

void window_rebar(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_rebar || !G.loaded) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Reinforcement", NK_SHOWN);
    was_open = true;
    float w = CV_MIN(430 * s, G.vp_w * 0.7f), h = CV_MIN(14 * row, G.vp_h * 0.8f);
    if (nk_begin(ctx, "Reinforcement", nk_rect(G.vp_x + 10 * s, G.vp_y + 10 * s, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "Steel per width for each face, x and y: the sandwich model", NK_TEXT_LEFT, P.dim);
        nk_label_colored(ctx, "(EC2 Annex F), outer layers z = h - 2c apart, Wood-Armer in each", NK_TEXT_LEFT, P.dim);
        bool ch = false;
        ch |= value_row(ctx, s, row, "concrete", "#fcd", "fcd: the concrete's design compressive strength (fck / 1.5 in EC2: 20 for C30/37).\n"
                        "Above it over a layer (2c thick) Crushing is 1; give nu fcd for cracked concrete", &G.rebar_fcd, CV_Q_STRESS);
        ch |= value_row(ctx, s, row, "steel", "#fyd", "fyd: the steel's design yield strength (fyk / 1.15: 435 for B500)", &G.rebar_fyd, CV_Q_STRESS);
        ch |= value_row(ctx, s, row, "cover", "#c", "c: from each face to the centroid of its bars; the lever arm is h - 2c,\n"
                        "h the shell's thickness (no value where 2c >= h)", &G.rebar_cover, CV_Q_LEN);
        if (ch) app_rebar_changed();
        nk_layout_row_dynamic(ctx, row, 1);
        int fi = -1;
        for (int f = 0; G.frd.n_steps && f < G.frd.steps[G.step].nfields; f++)
            if (!strcmp(G.frd.steps[G.step].fields[f].name, "REBAR")) fi = f;
        if (fi < 0) {
            nk_label_colored(ctx, "No REBAR in this step: it needs shells (with the deck) and STRESS", NK_TEXT_LEFT, P.warn);
        } else {
            const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
            char t[96];
            snprintf(t, sizeof t, "Areas in %s (the units window converts them)", app_unit("REBAR", 0)[0] ? app_unit("REBAR", 0) : "model units");
            nk_label_colored(ctx, t, NK_TEXT_LEFT, P.dim);
            nk_layout_row_dynamic(ctx, row, 2);
            for (int c = 0; c < d->ncomp; c++) {
                bool sel = G.field_src == 0 && !strcmp(G.field_name, "REBAR") && G.comp == c;
                if (nk_option_label(ctx, d->comp[c], sel) && !sel) app_select_src("REBAR", c, 0);
            }
        }
        (void)fw; (void)fh;
    }
    if (nk_window_is_hidden(ctx, "Reinforcement")) G.show_rebar = false;
    nk_end(ctx);
}
