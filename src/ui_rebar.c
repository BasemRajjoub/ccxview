/* ui_rebar.c -- the design values of the REBAR field (rebar.h, app_shell.c): concrete
   fcd, steel fyd and the cover, in the model's units, in REBAR's options in Fields. */
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
    nk_layout_row_template_push_static(ctx, 62 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 40 * s);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, name, NK_TEXT_LEFT);
    tip(ctx, tp);
    float was = *v, st = *v * 0.05f + 1e-12f;
    *v = nk_propertyf(ctx, id, 1e-6f, *v, 1e9f, st, st * 0.1f);
    nk_label(ctx, in_unit(q), NK_TEXT_LEFT);
    return *v != was;
}

void rows_rebar(struct nk_context* ctx, float s, float row) {
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "Steel per width for each face, x and y: the sandwich model (EC2 Annex F),\n"
             "outer layers z = h - 2c apart, Wood-Armer in each");
    nk_label_colored(ctx, "sandwich model, Wood-Armer", NK_TEXT_LEFT, P.dim);
    bool ch = false;
    ch |= value_row(ctx, s, row, "concrete", "#fcd", "fcd: the concrete's design compressive strength (fck / 1.5 in EC2: 20 for C30/37).\n"
                    "Above it over a layer (2c thick) Crushing is 1; give nu fcd for cracked concrete", &G.rebar_fcd, CV_Q_STRESS);
    ch |= value_row(ctx, s, row, "steel", "#fyd", "fyd: the steel's design yield strength (fyk / 1.15: 435 for B500)", &G.rebar_fyd, CV_Q_STRESS);
    ch |= value_row(ctx, s, row, "cover", "#c", "c: from each face to the centroid of its bars; the lever arm is h - 2c,\n"
                    "h the shell's thickness (no value where 2c >= h)", &G.rebar_cover, CV_Q_LEN);
    if (ch) app_rebar_changed();
}
