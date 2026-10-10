/* ui_fopts.c -- a field's own controls in Fields, in an "options" subgroup under its
   components: a vector's arrows, a tensor's glyphs, principal directions and stress
   trajectories, REBAR's design values (ui_rebar.c), the Gauss points of a .dat field.
   Changing one of a field not shown shows that field first. Also the open or closed
   state of each field's subgroups, kept by name while the program runs. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"

int* uii_fnode_state(const char* key, bool open) {
    static struct { char key[48]; int st; } t[96];
    static int n;
    for (int i = 0; i < n; i++) if (!strcmp(t[i].key, key)) return &t[i].st;
    int i = n < (int)CV_COUNT(t) ? n++ : (int)CV_COUNT(t) - 1;   /* full: the last one reused */
    snprintf(t[i].key, sizeof t[i].key, "%s", key);
    t[i].st = open ? NK_MAXIMIZED : NK_MINIMIZED;
    return &t[i].st;
}

/* a size as a logarithmic slider between lo and hi, the value a button back to def */
static bool log_row(struct nk_context* ctx, float row, const char* lab, const char* help, float* v, float lo, float hi, float def, const char* mark) {
    static const float ratio[3] = { 0.3f, 0.5f, 0.2f };
    nk_layout_row(ctx, NK_DYNAMIC, row, 3, ratio);
    tip(ctx, help);
    nk_label(ctx, lab, NK_TEXT_LEFT);
    float t = log10f(CV_MIN(CV_MAX(*v, lo), hi));
    tip(ctx, help);
    if (mark) uii_test_mark(ctx, mark);
    bool ch = ui_slider_float(ctx, log10f(lo), &t, log10f(hi), 0.01f);
    if (ch) *v = powf(10.f, t);
    char b[32], back[48];
    snprintf(b, sizeof b, *v < 10 ? "%.2f" : "%.1f", *v);
    snprintf(back, sizeof back, "Click: back to %g", def);
    tip(ctx, back);
    if (nk_button_label(ctx, b)) { *v = def; ch = true; }
    return ch;
}

/* the field shown, if it is not: its usual component */
static void show_field(const cv_field_desc* d, bool active, int comp) { if (!active) app_select_src(d->name, comp, 0); }

static void vector_rows(struct nk_context* ctx, float row, const cv_field_desc* d, bool active) {
    nk_layout_row_dynamic(ctx, row, 2);
    bool on = active && G.show_vec;
    tip(ctx, "The field as arrows at the nodes, the longest one 'size' % of the model");
    if (nk_checkbox_label(ctx, "arrows", &on)) { show_field(d, active, CV_COMP_MAG); G.show_vec = on; app_vectors_changed(); }
    tip(ctx, "Colour the arrows by the selected scalar (else white)");
    nk_checkbox_label(ctx, "coloured", &G.vec_colored);
    if (log_row(ctx, row, "size %", "Longest arrow, percent of the model's diagonal", &G.vec_pct, 0.2f, 50.f, 5.f, NULL)) app_vectors_changed();
}

static void tensor_rows(struct nk_context* ctx, float s, float row, const cv_field_desc* d, bool active) {
    if (active && G.comp < CV_COMP_MISES) {      /* a principal value: its directions */
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "The principal direction at the nodes: arrow pairs out for tension, in for compression");
        if (nk_checkbox_label(ctx, "principal directions", &G.show_vec)) app_vectors_changed();
    }
    nk_layout_row_dynamic(ctx, row, 2);
    bool on = active && G.show_tensor;
    tip(ctx, "The tensor as a glyph at each element's centre, the largest one 'size' elements wide");
    if (nk_checkbox_label(ctx, "glyphs", &on)) { show_field(d, active, CV_COMP_MISES); G.show_tensor = on; app_tensors_changed(); }
    tip(ctx, "ellipsoid: semi-axes |s1| |s2| |s3| along the principal directions\n"
             "superquadric (Kindlmann): the same axes, edged where two values are close,\n"
             "  so rod, disc and ball tell apart from any side; magnitudes only\n"
             "cross: a bar per principal value, heads out for tension, in for compression\n"
             "schultz-kindlmann: superquadrics for any signs: mixed signs pinch the shape\n"
             "  about the axis normal to the two of the same sign\n"
             "reynolds: the normal stress on every plane, as distance from the centre\n"
             "hwy: the shear stress on every plane; waists along the principal directions\n"
             "The last three are coloured by the normal stress in each direction");
    uii_test_mark(ctx, "#tensor style");
    int st = nk_combo(ctx, (const char**)cv_glyph_names, CV_GLYPH_N, G.tensor_style, (int)row,
                      nk_vec2(170 * s, CV_GLYPH_N * (row + 4 * s) + 20 * s));
    if (st != G.tensor_style) { show_field(d, active, CV_COMP_MISES); G.tensor_style = st; G.show_tensor = true; app_tensors_changed(); }
    if (active && G.show_tensor) {
        if (log_row(ctx, row, "size", "Size of the largest glyph, times the mean element size", &G.tensor_scale, 0.1f, 10.f, 1.f, "#tensor size"))
            app_tensors_changed();
    }
    if (active && (G.show_tensor || G.show_traj)) {
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, G.tensor_style == CV_GLYPH_CROSS
            ? "Colour each bar by its principal value: blue compression, pale near zero,\nred tension, full colour at the size's reference value (else plain red / blue)"
            : cv_glyph_signed(G.tensor_style)
            ? "Colour the surface by the normal stress in each direction: blue compression,\npale zero, red tension (else grey). Also colours the trajectories"
            : "Colour the glyphs by the selected scalar, the element's mean (else grey);\nthe trajectories by the principal value, blue compression .. red tension");
        if (nk_checkbox_label(ctx, "coloured", &G.tensor_colored)) { app_tensors_changed(); app_traj_changed(); }
    }
    /* principal stress trajectories */
    static const char* fam[3] = { "S1 (max)", "S3 (min)", "S1 + S3" };
    nk_layout_row_dynamic(ctx, row, 2);
    on = active && G.show_traj;
    tip(ctx, "Principal stress trajectories: curves along the direction of S1 (red)\n"
             "or S3 (blue) through the solid, the load paths");
    if (nk_checkbox_label(ctx, "trajectories", &on)) { show_field(d, active, CV_COMP_MISES); G.show_traj = on; app_traj_changed(); }
    uii_test_mark(ctx, "#traj family");
    int f = nk_combo(ctx, fam, 3, G.traj_which, (int)row, nk_vec2(150 * s, 3 * (row + 4 * s) + 20 * s));
    if (f != G.traj_which) { show_field(d, active, CV_COMP_MISES); G.traj_which = f; G.show_traj = true; app_traj_changed(); }
    if (active && G.show_traj) {
        static bool pending;                       /* traced again when the drag ends: a trace takes a moment */
        if (log_row(ctx, row, "spacing", "Distance between trajectories, times the mean element size", &G.traj_spacing, 0.5f, 20.f, 2.f, NULL))
            pending = true;
        if (pending && !ctx->input.mouse.buttons[NK_BUTTON_LEFT].down) { pending = false; app_traj_changed(); }
    }
}

void uii_field_options(struct nk_context* ctx, float s, float row, const cv_field_desc* d, bool active) {
    bool ten = cv_tensor_order(d) != 0, vec = d->ncomp == 3 && !ten, rb = !strcmp(d->name, "REBAR");
    if (!ten && !vec && !rb) return;
    char key[48];
    snprintf(key, sizeof key, "%s/options", d->name);
    int* st = uii_fnode_state(key, false);
    if (!strcmp(G.fields_open, d->name)) *st = NK_MAXIMIZED;    /* asked open: its options too */
    char mark[64];
    snprintf(mark, sizeof mark, "#%s options", d->name);
    uii_test_mark_row(ctx, mark);
    if (!nk_tree_state_push(ctx, NK_TREE_NODE, "options", (enum nk_collapse_states*)st)) return;
    if (vec) vector_rows(ctx, row, d, active);
    if (ten) tensor_rows(ctx, s, row, d, active);
    if (rb) rows_rebar(ctx, s, row);
    nk_tree_state_pop(ctx);
}

void uii_gauss_options(struct nk_context* ctx, float s, float row, const char* name) {
    (void)s;
    char key[48];
    snprintf(key, sizeof key, "%s.dat/options", name);
    if (!nk_tree_state_push(ctx, NK_TREE_NODE, "options", (enum nk_collapse_states*)uii_fnode_state(key, false))) return;
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "The integration points, where the .dat has its values (also under Layers)");
    if (nk_checkbox_label(ctx, "Gauss points", &G.show_gp)) app_gauss_changed();
    tip(ctx, "Draw the points through the faces (they sit inside the elements)");
    nk_checkbox_label(ctx, "x-ray", &G.gp_on_top);
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "The points' size, pixels");
    nk_property_float(ctx, "#point size", 1.f, &G.gp_size, 30.f, 1.f, 0.1f);
    nk_tree_state_pop(ctx);
}
