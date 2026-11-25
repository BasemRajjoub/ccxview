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
    (void)row;
    uik_label_row(ctx, lab, help, 44);
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
static void show_field(const cv_field_desc* d, bool active, int comp) { if (!active) app_pick_field(d->name, comp, 0); }

static void vector_rows(struct nk_context* ctx, float row, const cv_field_desc* d, bool active) {
    bool on = active && G.show_vec, was = on;
    uik_check2(ctx, "arrows", &on, "The field as arrows at the nodes, the longest one 'size' % of the model",
               "coloured", &G.vec_colored, "Colour the arrows by the selected scalar (else white)");
    if (on != was) { show_field(d, active, CV_COMP_MAG); G.show_vec = on; app_vectors_changed(); }
    if (log_row(ctx, row, "size %", "Longest arrow, percent of the model's diagonal", &G.vec_pct, 0.2f, 50.f, 5.f, NULL)) app_vectors_changed();
}

static void tensor_rows(struct nk_context* ctx, float s, float row, const cv_field_desc* d, bool active) {
    if (active && G.comp < CV_COMP_MISES) {      /* a principal value: its directions */
        if (uik_check(ctx, "principal directions", &G.show_vec, "The principal direction at the nodes: arrow pairs out for tension, in for compression")) app_vectors_changed();
    }
    bool on = active && G.show_tensor, was = on;
    int st = G.tensor_style;
    uik_check_combo_m(ctx, "glyphs", &on, cv_glyph_names, CV_GLYPH_N, &st,
                      "The tensor as a glyph at each element's centre, the largest one 'size' elements wide",
                      "ellipsoid: semi-axes |s1| |s2| |s3| along the principal directions\n"
                      "superquadric (Kindlmann): the same axes, edged where two values are close,\n"
                      "  so rod, disc and ball tell apart from any side; magnitudes only\n"
                      "cross: a bar per principal value, heads out for tension, in for compression\n"
                      "schultz-kindlmann: superquadrics for any signs: mixed signs pinch the shape\n"
                      "  about the axis normal to the two of the same sign\n"
                      "reynolds: the normal stress on every plane, as distance from the centre\n"
                      "hwy: the shear stress on every plane; waists along the principal directions\n"
                      "The last three are coloured by the normal stress in each direction",
                      "#tensor style");
    if (on != was) { show_field(d, active, CV_COMP_MISES); G.show_tensor = on; app_tensors_changed(); }
    if (st != G.tensor_style) { show_field(d, active, CV_COMP_MISES); G.tensor_style = st; G.show_tensor = true; app_tensors_changed(); }
    if (active && G.show_tensor) {
        if (log_row(ctx, row, "size", "Size of the largest glyph, times the mean element size", &G.tensor_scale, 0.1f, 10.f, 1.f, "#tensor size"))
            app_tensors_changed();
    }
    if (active && (G.show_tensor || G.show_traj)) {
        if (uik_check(ctx, "coloured", &G.tensor_colored, G.tensor_style == CV_GLYPH_CROSS
            ? "Colour each bar by its principal value: blue compression, pale near zero,\nred tension, full colour at the size's reference value (else plain red / blue)"
            : cv_glyph_signed(G.tensor_style)
            ? "Colour the surface by the normal stress in each direction: blue compression,\npale zero, red tension (else grey). Also colours the trajectories"
            : "Colour the glyphs by the selected scalar, the element's mean (else grey);\nthe trajectories by the principal value, blue compression .. red tension"))
            { app_tensors_changed(); app_traj_changed(); }
    }
    /* principal stress trajectories */
    static const char* fam[3] = { "S1 (max)", "S3 (min)", "S1 + S3" };
    on = active && G.show_traj; was = on;
    int f = G.traj_which;
    uik_check_combo_m(ctx, "trajectories", &on, fam, 3, &f,
                      "Principal stress trajectories: curves along the direction of S1 (red)\n"
                      "or S3 (blue) through the solid, the load paths", NULL, "#traj family");
    if (on != was) { show_field(d, active, CV_COMP_MISES); G.show_traj = on; app_traj_changed(); }
    if (f != G.traj_which) { show_field(d, active, CV_COMP_MISES); G.traj_which = f; G.show_traj = true; app_traj_changed(); }
    if (active && G.show_traj) {
        static bool pending;                       /* traced again when the drag ends: a trace takes a moment */
        if (log_row(ctx, row, "spacing", "Distance between trajectories, times the mean element size", &G.traj_spacing, 0.5f, 20.f, 2.f, NULL))
            pending = true;
        if (pending && !ctx->input.mouse.buttons[NK_BUTTON_LEFT].down) { pending = false; app_traj_changed(); }
    }
}

/* element-nodal results (app_enodal.c): a shell's surface, and whether the elements' values are averaged at the nodes */
static void enodal_rows(struct nk_context* ctx, const cv_field_desc* d, bool active) {
    static const char* const sides[CV_EN_SIDE_N] = { "both: top and bottom faces", "top on both faces", "bottom on both faces" };
    static const char* const avgs[CV_EN_AVG_N] = { "not averaged", "average scalars", "average components" };
    if (app_en_has_shells()) {
        int v = G.en_side;
        if (uik_combo_m(ctx, "shell side", sides, CV_EN_SIDE_N, &v,
                        "Which surface of a shell shows: both (STRPOS on the face its normal leaves by the right-hand rule on its\n"
                        "connectivity, STRNEG on the reverse), or the top or the bottom on both faces. Missing data stays missing",
                        "#shell side")) {
            G.en_side = v;
            if (active) app_en_changed(); else show_field(d, active, CV_COMP_MISES);
        }
    }
    int a = G.en_avg;
    if (uik_combo_m(ctx, "average", avgs, CV_EN_AVG_N, &a,
                    "Average the elements' values at a shared node. Not averaged keeps each element's own (the jumps show). Only\n"
                    "visible elements of one section and material average, solids apart from shells, shells whose normals\n"
                    "agree; scalars: after von Mises or the component, components: before. Needs the .inp beside the .frd",
                    "#elem avg")) {
        G.en_avg = a;
        if (active) app_en_changed(); else show_field(d, active, CV_COMP_MISES);
    }
}

void uii_field_options(struct nk_context* ctx, float s, float row, const cv_field_desc* d, bool active) {
    bool en = d->elnod && G.en_on;
    bool ten = cv_tensor_order(d) != 0 && !en, vec = cv_vec_comps(d) && !ten && !en, rb = !strcmp(d->name, "REBAR");
    if (!ten && !vec && !rb && !en) return;
    char key[48];
    snprintf(key, sizeof key, "%s/options", d->name);
    int* st = uii_fnode_state(key, false);
    if (!strcmp(G.fields_open, d->name)) *st = NK_MAXIMIZED;    /* asked open: its options too */
    char mark[64];
    snprintf(mark, sizeof mark, "#%s options", d->name);
    uii_test_mark_row(ctx, mark);
    if (!nk_tree_state_push(ctx, NK_TREE_NODE, "options", (enum nk_collapse_states*)st)) return;
    if (en) enodal_rows(ctx, d, active);
    if (vec) vector_rows(ctx, row, d, active);
    if (ten) tensor_rows(ctx, s, row, d, active);
    if (rb) rows_rebar(ctx, s, row);
    nk_tree_state_pop(ctx);
}

/* nodal values extrapolated from the points, averaged at the nodes by Abaqus' threshold rule (app_nrec.c) */
static void gauss_recovery_rows(struct nk_context* ctx, float row, const char* name) {
    static bool pending;                           /* a change of the threshold; on a huge model it applies on release */
    bool active = G.field_src == 1 && strcmp(name, G.field_name) == 0;
    bool on = G.nodal_recovery;
    if (uik_check(ctx, "from integration points", &on,
                  "Nodal values extrapolated from the integration points of each element (the .frd's are the solver's, already averaged).\n"
                  "Needs the points in the .dat (*EL PRINT)")) {
        G.nodal_recovery = on;
        if (active) app_select(G.field_name, G.comp);
        else if (on) {
            cv_field_desc d;
            cv_scalar_opt o[CV_MAX_OPTS];
            int n = gp_desc(name, &d) ? cv_field_options(&d, o, CV_MAX_OPTS) : 0, c = n ? o[0].comp : 0;
            for (int i = 0; i < n; i++) if (o[i].comp == CV_COMP_MISES) c = o[i].comp;
            app_pick_field(name, c, 1);
        }
    }
    if (!G.nodal_recovery) return;
    char lab[48];
    snprintf(lab, sizeof lab, G.avg_pct < 0.5f ? "average below 0 %% (not averaged)" : "average below %d %%", (int)(G.avg_pct + 0.5f));
    uik_cols(ctx, 1);
    nk_label(ctx, lab, NK_TEXT_LEFT);
    tip(ctx, "A node shared by elements is averaged where their values differ by at most this share of the range shown;\n"
             "above it each element keeps its own value (the jump stays). 0 = nothing averaged, 100 = everything");
    if (ui_slider_float(ctx, 0.f, &G.avg_pct, 100.f, 1.f)) pending = true;
    bool apart = !G.avg_across_materials;
    if (uik_check(ctx, "keep materials apart", &apart, "Do not average where elements of different materials meet")) { G.avg_across_materials = !apart; pending = true; }
    if (pending && (!nrec_big() || !nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT))) { pending = false; app_nrec_changed(); }
}

void uii_gauss_options(struct nk_context* ctx, float s, float row, const char* name) {
    (void)s;
    char key[48];
    snprintf(key, sizeof key, "%s.dat/options", name);
    if (!nk_tree_state_push(ctx, NK_TREE_NODE, "options", (enum nk_collapse_states*)uii_fnode_state(key, false))) return;
    gauss_recovery_rows(ctx, row, name);
    bool gp = G.show_gp;
    uik_check2(ctx, "Gauss points", &G.show_gp, "The integration points, where the .dat has its values (also under Layers)",
               "x-ray", &G.gp_on_top, "Draw the points through the faces (they sit inside the elements)");
    if ((bool)G.show_gp != gp) app_gauss_changed();
    uik_num(ctx, "point size", &G.gp_size, 1.f, 30.f, 1.f, "The points' size, pixels");
    nk_tree_state_pop(ctx);
}
