/* ui_panels.c -- the scene sidebar on the left: the file box and recent files,
   Layers, Groups (with the deck's and cgx's sets), Fields and Export. Its View
   section is in ui_view.c. */
#include "app.h"
#include "calc.h"
#include "ui.h"
#include "cfg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"
#include "icons.h"

/* ---- panels --------------------------------------------------------------------- */

static void axis_label(char* out, size_t n, int axis, uint32_t v, uint32_t count) {
    if (axis == CV_AXIS_TYPE) snprintf(out, n, "%s  (%u)", cv_frd_type_name((int)v), count);
    else if (axis == CV_AXIS_MAT) {
        const char* nm = deck_material_name(v);          /* the deck names its materials */
        if (nm) snprintf(out, n, "%s  (%u)", nm, count);
        else if (v) snprintf(out, n, "Material %u  (%u)", v, count);
        else snprintf(out, n, "no material  (%u)", count);
    }
    else snprintf(out, n, "Group %u  (%u)", v, count);
}

static int g_pick_axis = -1, g_pick_idx = -1;   /* open colour picker (group swatch) */

/* A right click on a set's row (laid out next): integrate over it, select it. kind:
   what the set holds (elements: a volume, faces: a surface, nodes: a sum). */
static void set_menu(struct nk_context* ctx, float s, float row, int kind, const char* name, bool surf) {
    struct nk_rect b = nk_widget_bounds(ctx);
    uii_test_mark(ctx, "#set menu");
    static const char* what[CV_IK_N] = { "Integrate over set %s", "Integrate over surface %s", "Sum over set %s" };
    char lab[96];
    snprintf(lab, sizeof lab, what[kind], name);
    if (nk_contextual_begin(ctx, 0, nk_vec2(240 * s, 6.4f * row + 10 * s), b)) {
        nk_layout_row_dynamic(ctx, row, 1);
        if (nk_contextual_item_label(ctx, lab, NK_TEXT_LEFT)) app_integ_open(kind, name);
        if (kind == CV_IK_VOLUME && nk_contextual_item_label(ctx, "... its outer faces (surface)", NK_TEXT_LEFT)) app_integ_open(CV_IK_SURFACE, name);
        if (kind != CV_IK_NODES && nk_contextual_item_label(ctx, "... sum over its nodes", NK_TEXT_LEFT)) app_integ_open(CV_IK_NODES, name);
        char key[80];                                      /* the selection: this one, or with it, or without it */
        snprintf(key, sizeof key, "%s:%s", surf ? "surf" : "set", name);
        uii_test_mark(ctx, "#set menu select");
        if (nk_contextual_item_label(ctx, "Select it", NK_TEXT_LEFT)) app_sel_by_name(key, CV_SEL_NEW);
        if (nk_contextual_item_label(ctx, "Add it to the selection", NK_TEXT_LEFT)) app_sel_by_name(key, CV_SEL_ADD);
        if (nk_contextual_item_label(ctx, "Remove it from the selection", NK_TEXT_LEFT)) app_sel_by_name(key, CV_SEL_REMOVE);
        nk_contextual_end(ctx);
    }
}

/* The deck's named sets: element sets are a display group (tick to show only
   them; none ticked = everything; the eye hides one, whatever is ticked), node
   sets and surfaces are highlights. */
static void panel_deck_sets(struct nk_context* ctx, float s, float row) {
    const cv_inp* d = deck_get();
    if (!d) return;
    bool* on = deck_set_flags();
    bool* son = deck_surf_flags();
    int ne = 0, nn = 0;
    for (int i = 0; i < d->nsets; i++) { if (d->sets[i].is_elem) ne++; else nn++; }
    char lab[96];
    if (d->nsteps || d->namps || d->nsubs) {
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "The deck's steps and their times, the elements *MODEL CHANGE takes out,\n"
                 "what a submodel's global model drives, the amplitudes and their points");
        if (nk_button_label(ctx, "Steps, amplitudes ...")) G.show_deck = !G.show_deck;
    }
    if (ne && nk_tree_push_id(ctx, NK_TREE_NODE, "Element sets", NK_MAXIMIZED, 40)) {
        bool* hid = deck_set_hidden_flags();
        if (deck_any_elset_on() || deck_any_elset_hidden()) {
            nk_layout_row_dynamic(ctx, row, 1);
            if (nk_button_label(ctx, "show everything")) {
                for (int i = 0; i < d->nsets; i++) if (d->sets[i].is_elem) on[i] = hid[i] = false;
                app_groups_changed();
            }
        }
        int k = 0;
        for (int i = 0; i < d->nsets && k < 500; i++) {
            if (!d->sets[i].is_elem) continue;
            k++;
            snprintf(lab, sizeof lab, "%s  (%u)", d->sets[i].name, d->sets[i].n);
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, row * 1.4f);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);
            tip(ctx, hid[i] ? "Hidden: click to show this element set again"
                            : "Hide this element set, everything else stays (tick: show only the ticked sets)");
            if (nk_button_label(ctx, hid[i] ? IC_EYE_OFF : IC_EYE)) { hid[i] = !hid[i]; app_groups_changed(); }
            if (hid[i]) {                            /* a hidden set's name dimmed */
                struct nk_style_toggle* t = &ctx->style.checkbox;
                nk_style_push_color(ctx, &t->text_normal, P.dim);
                nk_style_push_color(ctx, &t->text_hover, P.dim);
                nk_style_push_color(ctx, &t->text_active, P.dim);
            }
            set_menu(ctx, s, row, CV_IK_VOLUME, d->sets[i].name, false);
            if (nk_checkbox_label(ctx, lab, &on[i])) app_groups_changed();
            if (hid[i]) for (int c = 0; c < 3; c++) nk_style_pop_color(ctx);
        }
        nk_tree_pop(ctx);
    }
    if (nn && nk_tree_push_id(ctx, NK_TREE_NODE, "Node sets", NK_MINIMIZED, 41)) {
        nk_layout_row_dynamic(ctx, row, 1);
        int k = 0;
        for (int i = 0; i < d->nsets && k < 500; i++) {
            if (d->sets[i].is_elem) continue;
            k++;
            snprintf(lab, sizeof lab, "%s  (%u)", d->sets[i].name, d->sets[i].n);
            set_menu(ctx, s, row, CV_IK_NODES, d->sets[i].name, false);
            if (nk_checkbox_label(ctx, lab, &on[i])) { G.show_hl = true; deck_refresh_highlight(); }
        }
        nk_tree_pop(ctx);
    }
    if (d->nlinks && nk_tree_push_id(ctx, NK_TREE_NODE, "Couplings", NK_MINIMIZED, 44)) {
        static const char* kinds[] = { "rigid", "kinematic", "distributing", "equation", "tie", "contact" };
        bool* lon = deck_link_flags();
        nk_layout_row_dynamic(ctx, row, 1);
        for (int i = 0; i < d->nlinks && i < 500; i++) {
            const cv_link* l = &d->links[i];
            if (l->kind == CV_LINK_TIE || l->kind == CV_LINK_CONTACT) {
                const char* a = l->surf[0] >= 0 ? d->surfs[l->surf[0]].name : "?";
                const char* b = l->surf[1] >= 0 ? d->surfs[l->surf[1]].name : "?";
                snprintf(lab, sizeof lab, "%s %s: %s / %s", kinds[l->kind], l->name, a, b);
            } else if (l->surf[0] >= 0) {
                snprintf(lab, sizeof lab, "%s %s  (node %u, %s)", kinds[l->kind], l->name, l->ref, d->surfs[l->surf[0]].name);
            } else {
                snprintf(lab, sizeof lab, "%s %s  (node %u, %u)", kinds[l->kind], l->name, l->ref, l->n);
            }
            if (nk_checkbox_label(ctx, lab, &lon[i])) { G.show_links = true; G.show_hl = true; deck_refresh_highlight(); }
        }
        nk_tree_pop(ctx);
    }
    if (d->nsurfs && nk_tree_push_id(ctx, NK_TREE_NODE, "Surfaces", NK_MINIMIZED, 42)) {
        nk_layout_row_dynamic(ctx, row, 1);
        for (int i = 0; i < d->nsurfs && i < 500; i++) {
            snprintf(lab, sizeof lab, "%s  (%u)", d->surfs[i].name, d->surfs[i].n ? d->surfs[i].n : d->surfs[i].nn);
            set_menu(ctx, s, row, d->surfs[i].n ? CV_IK_SURFACE : CV_IK_NODES, d->surfs[i].name, true);
            if (nk_checkbox_label(ctx, lab, &son[i])) { G.show_hl = true; deck_refresh_highlight(); }
        }
        nk_tree_pop(ctx);
    }
}

/* cgx sets: tick to show only what they hold (their geometry, and their
   elements when cgx meshed the script). */
static void panel_geo_sets(struct nk_context* ctx, float row) {
    const cv_fbd* g = geo_get();
    if (!g || !g->nsets) return;
    bool* on = geo_set_flags();
    if (!nk_tree_push_id(ctx, NK_TREE_NODE, "cgx sets", NK_MAXIMIZED, 43)) return;
    nk_layout_row_dynamic(ctx, row, 1);
    if (geo_any_on() && nk_button_label(ctx, "show everything")) geo_show_all();
    char lab[96];
    for (int i = 0, k = 0; i < g->nsets && k < 500; i++) {
        const cv_gset* st = &g->sets[i];
        if (geo_set_hidden(st->name)) continue;
        k++;
        uint32_t ng = st->npts + st->ncrv + st->nsrf;
        if (st->nel) snprintf(lab, sizeof lab, "%s  (%u el)", st->name, st->nel);
        else if (ng) snprintf(lab, sizeof lab, "%s  (%u)", st->name, ng);
        else snprintf(lab, sizeof lab, "%s  (%u nodes)", st->name, st->nnod);
        if (nk_checkbox_label(ctx, lab, &on[i])) geo_set_toggled(i);
    }
    nk_tree_pop(ctx);
}

/* ---- Layers: what is drawn and how it is coloured */
static void section_layers(struct nk_context* ctx, float s, float row) {
    /* layers */
    if (nk_tree_state_push(ctx, NK_TREE_TAB, "Layers", (enum nk_collapse_states*)&G.tree[CV_TREE_LAYERS])) {
        if (geo_loaded()) {                      /* cgx geometry */
            nk_layout_row_dynamic(ctx, row, 3);
            nk_checkbox_label(ctx, "Points", &G.show_geo_pts);
            nk_checkbox_label(ctx, "Lines", &G.show_geo_crv);
            nk_checkbox_label(ctx, "Surfs", &G.show_geo_srf);
            if (G.show_geo_pts) {
                nk_layout_row_dynamic(ctx, row, 1);
                nk_property_float(ctx, "#geometry point size", 1.f, &G.geo_size, 20.f, 1.f, 0.1f);
            }
        }
        if (deck_has_bc() || deck_has_loads() || deck_has_discrete()) {   /* the deck's supports, loads, springs */
            nk_layout_row_dynamic(ctx, row, 2);
            if (deck_has_bc() || deck_has_loads()) {
                tip(ctx, "Supports of the step on screen: a cone per held DOF (double base for a rotation), along the\nnode's *TRANSFORM axes; a prescribed displacement as an arrow with a bar across its tail,\na prescribed rotation as a turning arrow, a temperature as a cross");
                nk_checkbox_label(ctx, "Supports", &G.show_bc);
                tip(ctx, "Loads of the step on screen. Yellow: forces, pressures, edge loads (bar across the tail),\ngravity (block arrow), centrifugal (dashed axis, turning arc), bolt preload (ring with arrows).\nMagenta: moments. Red: heat (zigzag), film (zigzag and bar), radiation (rays), given temperature (diamond).\nLength follows the magnitude within each kind");
                nk_checkbox_label(ctx, "Loads", &G.show_loads);
            }
            if (deck_has_discrete()) {
                tip(ctx, "Springs as zigzags, dashpots as pistons, gaps as facing bars, masses as cubes.\nA one-node spring points along its dof to a ground bar.");
                nk_checkbox_label(ctx, "Springs, masses", &G.show_disc);
            }
            if (deck_get() && deck_get()->nlinks) {
                tip(ctx, "Rigid bodies, couplings and equations as spiders from the reference node;\ntick single ones under Groups > Couplings");
                nk_checkbox_label(ctx, "Couplings", &G.show_links);
            }
        }
        if ((geo_loaded() || deck_has_bc() || deck_has_loads() || deck_has_discrete())) uii_hsep(ctx, s);
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Balls at the field's minimum and maximum (legend settings: go there)");
        nk_checkbox_label(ctx, "Min / max", &G.show_markers);
        tip(ctx, "The undeformed edges in grey behind the deformed shape");
        nk_checkbox_label(ctx, "Undeformed", &G.show_ghost);
        uii_hsep(ctx, s); nk_layout_row_dynamic(ctx, row, 2);
        nk_checkbox_label(ctx, "Faces", &G.show_faces);
        {
            static const char* fm_names[FM_N] = { "field", "by type", "by material", "by group", "plain" };
            uii_test_mark(ctx, "#faces mode");
            int fm = nk_combo(ctx, fm_names, FM_N, G.faces_mode, (int)row, nk_vec2(150 * s, 5 * row + 20 * s));
            if (fm != G.faces_mode) app_set_faces_mode(fm);
        }
        tip(ctx, "Edges of the exterior faces");
        nk_checkbox_label(ctx, "Edges", &G.show_edges);
        tip(ctx, "Paint the field on the edges too (else dark lines)");
        nk_checkbox_label(ctx, "coloured", &G.edges_field);
        tip(ctx, "Part outline: borders, creases sharper than the angle, material and type changes.\nStays when the mesh edges hide.");
        nk_checkbox_label(ctx, "Outline", &G.show_outline);
        if (G.show_outline) {
            float a = G.outline_angle;
            tip(ctx, "Faces meeting at more than this many degrees make a crease");
            nk_property_float(ctx, "#crease", 1.f, &a, 180.f, 5.f, 0.5f);
            if (a != G.outline_angle) { G.outline_angle = a; app_groups_changed(); }
        } else nk_spacing(ctx, 1);
        bool dense = G.edges_auto && G.show_faces && G.edges_dense;
        if ((G.show_edges || G.show_nodes || G.show_gp) && G.show_faces) {
            tip(ctx, "Hide the edges, nodes and Gauss points while the mesh is denser than ~3 px on\nscreen, where they would paint the surface black; zoom in to see them");
            nk_checkbox_label(ctx, "hide when dense", &G.edges_auto);
            if (dense) nk_label_colored(ctx, "hidden: zoom in", NK_TEXT_LEFT, P.warn);
            else nk_label(ctx, "", NK_TEXT_LEFT);   /* nk_spacing ending a row opens the next one */
        }
        if (G.show_faces) {
            tip(ctx, "Faces of quadratic elements (C3D20, C3D10, S8, ...) drawn through their mid-side nodes:\ncolours and shape follow every node. Off: corners only, a third of the triangles,\nbut a value at a mid node (bending through one shell layer) does not show");
            if (nk_checkbox_label(ctx, "mid-side nodes", &G.mid_faces)) app_groups_changed();
            nk_label(ctx, "", NK_TEXT_LEFT);
        }
        uii_hsep(ctx, s); nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Nodes of the visible elements, as small balls");
        nk_checkbox_label(ctx, "Nodes", &G.show_nodes);
        tip(ctx, "Paint the field on the nodes too");
        nk_checkbox_label(ctx, "coloured", &G.nodes_field);
        if (G.show_nodes) {
            nk_layout_row_dynamic(ctx, row, 1);
            nk_property_float(ctx, "#point size", 1.f, &G.point_size, 12.f, 1.f, 0.1f);
        }
        {                                        /* integration points, next to the nodes */
            nk_layout_row_dynamic(ctx, row, 2);
            tip(ctx, "Integration points: real .dat values when a .dat field is selected,\nelse the nodal field interpolated there");
            if (nk_checkbox_label(ctx, "Gauss pts", &G.show_gp)) app_gauss_changed();
            tip(ctx, "Colour the points by the field");
            nk_checkbox_label(ctx, "coloured", &G.gp_colored);
            if (G.show_gp) {
                nk_layout_row_dynamic(ctx, row, 2);
                nk_property_float(ctx, "#size", 1.f, &G.gp_size, 30.f, 1.f, 0.1f);
                tip(ctx, "Draw the points through the faces (they sit inside the elements)");
                nk_checkbox_label(ctx, "x-ray", &G.gp_on_top);
            }
            if (app_field_is_vector()) {             /* arrows of DISP, FORC, FLUX, ... */
                nk_layout_row_dynamic(ctx, row, 2);
                tip(ctx, G.comp < CV_COMP_MISES ? "The principal direction at the nodes: arrow pairs out for tension, in for compression"
                                                : "The field as arrows at the nodes, the longest one 'vec %' of the model");
                if (nk_checkbox_label(ctx, G.comp < CV_COMP_MISES ? "Directions" : "Vectors", &G.show_vec)) app_vectors_changed();
                tip(ctx, "Colour the arrows by the selected scalar (else white)");
                nk_checkbox_label(ctx, "coloured", &G.vec_colored);
            }
            if (app_field_is_tensor()) {             /* stress, strain: a glyph per element */
                nk_layout_row_dynamic(ctx, row, 2);
                tip(ctx, "The tensor as a glyph at each element's centre, the largest one 'size' elements wide");
                if (nk_checkbox_label(ctx, "Tensors", &G.show_tensor)) app_tensors_changed();
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
                if (st != G.tensor_style) { G.tensor_style = st; G.show_tensor = true; app_tensors_changed(); }
                if (G.show_tensor) {                 /* size: log slider, the number resets it */
                    static const float ratio[3] = { 0.3f, 0.5f, 0.2f };
                    const char* help = "Size of the largest glyph, times the mean element size";
                    nk_layout_row(ctx, NK_DYNAMIC, row, 3, ratio);
                    tip(ctx, help);
                    nk_label(ctx, "size", NK_TEXT_LEFT);
                    float t = log10f(CV_MIN(CV_MAX(G.tensor_scale, 0.1f), 10.f));
                    tip(ctx, help);
                    uii_test_mark(ctx, "#tensor size");
                    bool ch = ui_slider_float(ctx, -1.f, &t, 1.f, 0.01f);
                    if (ch) G.tensor_scale = powf(10.f, t);
                    char b[32];
                    snprintf(b, sizeof b, "%.2f", G.tensor_scale);
                    tip(ctx, "Click: back to 1 (one element)");
                    if (nk_button_label(ctx, b)) { G.tensor_scale = 1.f; ch = true; }
                    if (ch) app_tensors_changed();
                    nk_layout_row_dynamic(ctx, row, 2);
                    nk_label(ctx, "", NK_TEXT_LEFT);
                    tip(ctx, G.tensor_style == CV_GLYPH_CROSS
                        ? "Colour each bar by its principal value: blue compression, pale near zero,\nred tension, full colour at the size's reference value (else plain red / blue)"
                        : cv_glyph_signed(G.tensor_style)
                        ? "Colour the surface by the normal stress in each direction: blue compression,\npale zero, red tension (else grey). Also colours the trajectories"
                        : "Colour the glyphs by the selected scalar, the element's mean (else grey)");
                    if (nk_checkbox_label(ctx, "coloured", &G.tensor_colored)) app_tensors_changed();
                }
                {                                /* principal stress trajectories */
                    static const char* fam[3] = { "S1 (max)", "S3 (min)", "S1 + S3" };
                    nk_layout_row_dynamic(ctx, row, 2);
                    tip(ctx, "Principal stress trajectories: curves along the direction of S1 (red)\n"
                             "or S3 (blue) through the solid, the load paths. 'coloured' above:\n"
                             "by the principal value, blue compression .. red tension");
                    if (nk_checkbox_label(ctx, "Trajectories", &G.show_traj)) app_traj_changed();
                    uii_test_mark(ctx, "#traj family");
                    int f = nk_combo(ctx, fam, 3, G.traj_which, (int)row, nk_vec2(150 * s, 3 * (row + 4 * s) + 20 * s));
                    if (f != G.traj_which) { G.traj_which = f; G.show_traj = true; app_traj_changed(); }
                    if (G.show_traj) {
                        static const float ratio[3] = { 0.3f, 0.5f, 0.2f };
                        const char* help = "Distance between trajectories, times the mean element size";
                        nk_layout_row(ctx, NK_DYNAMIC, row, 3, ratio);
                        tip(ctx, help);
                        nk_label(ctx, "spacing", NK_TEXT_LEFT);
                        float t = log10f(CV_MIN(CV_MAX(G.traj_spacing, 0.5f), 20.f));
                        tip(ctx, help);
                        bool ch = ui_slider_float(ctx, log10f(0.5f), &t, log10f(20.f), 0.01f);
                        if (ch) G.traj_spacing = powf(10.f, t);
                        char b[32];
                        snprintf(b, sizeof b, "%.2f", G.traj_spacing);
                        tip(ctx, "Click: back to 2");
                        if (nk_button_label(ctx, b)) { G.traj_spacing = 2.f; ch = true; }
                        /* traced again when the drag ends: a trace takes a moment on big models */
                        static bool pending;
                        if (ch) pending = true;
                        if (pending && !ctx->input.mouse.buttons[NK_BUTTON_LEFT].down) { pending = false; app_traj_changed(); }
                        if (!G.show_tensor) {         /* the glyphs' box, when they are off */
                            nk_layout_row_dynamic(ctx, row, 2);
                            nk_label(ctx, "", NK_TEXT_LEFT);
                            tip(ctx, "Colour the trajectories by the principal value: blue compression .. red tension");
                            nk_checkbox_label(ctx, "coloured", &G.tensor_colored);
                        }
                    }
                }
            }
        }
        nk_tree_pop(ctx);
    }

}

/* ---- Groups: element type / material / group, deck and cgx sets */
static void section_groups(struct nk_context* ctx, float s, float row) {
    /* groups */
    if (nk_tree_state_push(ctx, NK_TREE_TAB, "Groups", (enum nk_collapse_states*)&G.tree[CV_TREE_GROUPS])) {
        for (int a = 0; a < CV_AXIS_N; a++) {
            cv_axis* ax = &G.groups.axis[a];
            if (!nk_tree_push_id(ctx, NK_TREE_NODE, cv_axis_name(a), a == 0 ? NK_MAXIMIZED : NK_MINIMIZED, a))
                continue;
            if (ax->n > 1) {
                nk_layout_row_dynamic(ctx, row, 2);
                if (nk_button_label(ctx, "all"))  { for (int i = 0; i < ax->n; i++) ax->on[i] = true;  app_groups_changed(); }
                if (nk_button_label(ctx, "none")) { for (int i = 0; i < ax->n; i++) ax->on[i] = false; app_groups_changed(); }
            }
            int shown = CV_MIN(ax->n, 500);            /* keep the list usable */
            for (int i = 0; i < shown; i++) {
                char lab[64];
                axis_label(lab, sizeof lab, a, ax->value[i], ax->count[i]);
                nk_layout_row_template_begin(ctx, row);
                nk_layout_row_template_push_static(ctx, row * 1.4f);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_end(ctx);
                /* colour swatch: a plain colour button; clicking it opens Nuklear's
                   picker in a popup. Picking also colours the faces by this axis. */
                float* rgb = G.axis_rgb[a] ? G.axis_rgb[a] + 3 * i : NULL;
                struct nk_rect sb = nk_widget_bounds(ctx);
                struct nk_color sw = rgb ? nk_rgb_f(rgb[0], rgb[1], rgb[2]) : nk_rgb(128, 128, 128);
                if (nk_button_color(ctx, sw) && rgb) {
                    bool same = g_pick_axis == a && g_pick_idx == i;
                    g_pick_axis = same ? -1 : a;
                    g_pick_idx = same ? -1 : i;
                }
                if (rgb && g_pick_axis == a && g_pick_idx == i) {
                    struct nk_rect clip = ctx->current->layout->clip;
                    float pw = 230 * s, ph = 170 * s + 3 * row + 6 * ctx->style.window.spacing.y;
                    struct nk_rect pr = nk_rect(sb.x + sb.w + 4 * s - clip.x, sb.y + sb.h - clip.y, pw, ph);
                    if (nk_popup_begin(ctx, NK_POPUP_STATIC, "pick", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, pr)) {
                        nk_layout_row_dynamic(ctx, 170 * s, 1);
                        struct nk_colorf cf = { rgb[0], rgb[1], rgb[2], 1 };
                        struct nk_colorf nf = nk_color_picker(ctx, cf, NK_RGB);
                        nk_layout_row_dynamic(ctx, row, 3);
                        nf.r = nk_propertyf(ctx, "#R", 0, nf.r, 1, 0.01f, 0.005f);
                        nf.g = nk_propertyf(ctx, "#G", 0, nf.g, 1, 0.01f, 0.005f);
                        nf.b = nk_propertyf(ctx, "#B", 0, nf.b, 1, 0.01f, 0.005f);
                        if (nf.r != rgb[0] || nf.g != rgb[1] || nf.b != rgb[2]) {
                            rgb[0] = nf.r; rgb[1] = nf.g; rgb[2] = nf.b;
                            if (G.faces_mode != FM_TYPE + a) app_set_faces_mode(FM_TYPE + a);
                            else app_group_colors_changed();
                        }
                        nk_layout_row_dynamic(ctx, row, 1);
                        if (nk_button_label(ctx, "done")) {
                            g_pick_axis = g_pick_idx = -1;
                            nk_popup_close(ctx);
                        }
                        nk_popup_end(ctx);
                    } else {
                        g_pick_axis = g_pick_idx = -1;
                    }
                }
                if (nk_checkbox_label(ctx, lab, &ax->on[i])) app_groups_changed();
            }
            if (ax->n > shown) {
                nk_layout_row_dynamic(ctx, row, 1);
                nk_label(ctx, "(list truncated)", NK_TEXT_LEFT);
            }
            nk_tree_pop(ctx);
        }
        if (geo_loaded()) panel_geo_sets(ctx, row);   /* cgx sets drive the mesh's too */
        else panel_deck_sets(ctx, s, row);
        section_stl(ctx, s, row);
        nk_tree_pop(ctx);
    }

}

/* ---- Calculated field: a formula over the .frd fields (calc.h) */
static char calc_buf[sizeof G.calc_expr], calc_seen[sizeof G.calc_expr];   /* the formula being typed */
static int calc_len;
const char* calc_draft(void) { return calc_buf; }
void calc_draft_set(const char* t) { snprintf(calc_buf, sizeof calc_buf, "%s", t); calc_len = (int)strlen(calc_buf); }

/* typical formulas: { formula, fields it needs (S stress, E strain, D displacement), what it is };
   shared with the formula builder */
const char* const calc_examples[][3] = {
    { "S1 + S2 + S3",                  "S", "sum of principal stresses, triaxial check (ASME VIII-2 5.3.2)" },
    { "S1 - S3",                       "S", "Tresca stress intensity" },
    { "(S1 - S3) / 2",                 "S", "largest shear stress" },
    { "(S1 + S2 + S3) / 3",            "S", "hydrostatic (mean) stress" },
    { "-(S1 + S2 + S3) / 3",           "S", "pressure, positive in compression" },
    { "(S1 + S2 + S3) / 3 / MISES",    "S", "stress triaxiality" },
    { "MISES * sign(S1 + S2 + S3)",    "S", "signed von Mises: the sign of the mean stress (fatigue)" },
    { "max(S1, 0)",                    "S", "largest tensile stress (Rankine), compression shown as 0" },
    { "max(abs(S1), abs(S3))",         "S", "largest principal stress by size, either sign" },
    { "(2*S2 - S1 - S3) / (S1 - S3)",  "S", "Lode parameter: -1 tension, 0 shear, 1 compression" },
    { "MISES / 235",                   "S", "utilisation for a yield of 235 (S235 steel, MPa)" },
    { "235 / MISES",                   "S", "safety factor against a yield of 235" },
    { "if(MISES > 235, 1, 0)",         "S", "where von Mises is above 235: 1, else 0" },
    { "if(Z > 0, MISES, 0)",           "S", "von Mises only where z > 0" },
    { "STRESS_SXX * 1e-6",             "S", "SXX from Pa to MPa" },
    { "E1",                            "E", "largest principal strain" },
    { "E1 - E3",                       "E", "largest engineering shear strain" },
    { "E1 + E2 + E3",                  "E", "volumetric strain" },
    { "DISP_MAG * 1000",               "D", "displacement from m to mm" },
    { "sqrt(DISP_D1^2 + DISP_D2^2)",   "D", "in-plane (xy) displacement" },
    { "sqrt(X^2 + Y^2)",               "",  "radius about the z axis" },
    { "atan2(Y, X) * 180 / pi",        "",  "angle about the z axis, degrees" },
};
const int calc_example_count = (int)CV_COUNT(calc_examples);

/* whether this file has the fields example i needs */
bool calc_example_ok(int i) {
    static char names[2048];
    static unsigned gen;
    static const cv_frd* of;
    if (of != &G.frd || gen != G.field_gen || !names[0]) {
        cv_calc_names(&G.frd, names, sizeof names);
        of = &G.frd; gen = G.field_gen;
    }
    const char* need = calc_examples[i][1];
    const char* f = need[0] == 'S' ? "STRESS:" : need[0] == 'E' ? "TOSTRAIN:" : need[0] == 'D' ? "DISP:" : NULL;
    if (!f) return true;
    size_t n = strlen(f);
    for (const char* l = names; l && *l; l = strchr(l, '\n'), l = l ? l + 1 : NULL)
        if (!strncmp(l, f, n)) return true;
    return false;
}

static void section_calc(struct nk_context* ctx, float s, float row) {
    if (strcmp(calc_seen, G.calc_expr)) {                 /* set elsewhere: --calc, a view file */
        snprintf(calc_seen, sizeof calc_seen, "%s", G.calc_expr);
        snprintf(calc_buf, sizeof calc_buf, "%s", G.calc_expr);
        calc_len = (int)strlen(calc_buf);
    }
    bool active = G.field_src == 2;
    if (!nk_tree_push_id(ctx, NK_TREE_NODE, "Calculated", active ? NK_MAXIMIZED : NK_MINIMIZED, 200)) return;
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 56 * s);
    nk_layout_row_template_end(ctx);
    tip(ctx, "A formula of the fields, then Enter: STRESS_SXX - STRESS_SYY, sqrt(D1^2 + D2^2), if(MISES > 200, 1, 0)");
    nk_flags ev = nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, calc_buf, &calc_len, (int)sizeof calc_buf - 1, nk_filter_default);
    calc_buf[calc_len] = 0;
    bool go = (ev & NK_EDIT_COMMITED) != 0;
    tip(ctx, "Colour the model by the formula");
    if (nk_button_label(ctx, "Show")) go = true;
    if (go && calc_buf[0]) {
        if (app_calc_set(calc_buf)) snprintf(calc_seen, sizeof calc_seen, "%s", G.calc_expr);
    } else if (go) {
        G.calc_err[0] = 0;
    }
    if (G.calc_err[0] && strcmp(calc_buf, G.calc_expr)) {
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, G.calc_err, NK_TEXT_LEFT, P.warn);
    } else if (strcmp(calc_buf, G.calc_expr) && calc_buf[0]) {
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "Enter or Show to apply", NK_TEXT_LEFT, P.dim);
    }
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "Build a formula from buttons: this file's fields, operators, functions and examples");
    if (nk_button_label(ctx, "Formula...")) G.show_calc_help = !G.show_calc_help;
    /* a list, not a combo: a combo this low in the panel opens past its edge, clipped away */
    if (nk_tree_push_id(ctx, NK_TREE_NODE, "Examples", NK_MINIMIZED, 201)) {
        nk_layout_row_dynamic(ctx, row, 1);
        for (int i = 0; i < calc_example_count; i++) {
            if (!calc_example_ok(i)) continue;
            tip(ctx, calc_examples[i][2]);
            if (nk_button_label(ctx, calc_examples[i][0])) {
                calc_draft_set(calc_examples[i][0]);
                app_calc_set(calc_buf);
                snprintf(calc_seen, sizeof calc_seen, "%s", G.calc_expr);
            }
        }
        nk_tree_pop(ctx);
    }
    nk_tree_pop(ctx);
}

/* ---- Fields of the current step, and the .dat ones */
static void section_fields(struct nk_context* ctx, float s, float row) {
    /* fields of the current step */
    if (nk_tree_state_push(ctx, NK_TREE_TAB, "Fields", (enum nk_collapse_states*)&G.tree[CV_TREE_FIELDS])) {
        nk_layout_row_dynamic(ctx, row, 1);
        if (G.frd.n_steps == 0) {
            nk_label(ctx, "No results in this file.", NK_TEXT_LEFT);
            section_mesh(ctx, s, row);
            section_label(ctx, s, row);
        } else {
            const cv_step* st = &G.frd.steps[G.step];
            nk_bool em = G.elem_mode;
            tip(ctx, "One value per element (mean of its nodes) instead of a smooth nodal field");
            if (nk_checkbox_label(ctx, "Per element (flat)", &em)) app_set_elem_mode(em);
            {   /* components of vectors and tensors in a cylindrical system */
                static const char* cs[] = { "coordinates: global x y z", "cylindrical about X", "cylindrical about Y", "cylindrical about Z" };
                tip(ctx, "Vector and tensor components in a cylindrical system: r radial, t hoop, a axial (.frd fields; invariants stay)");
                int c = nk_combo(ctx, cs, 4, G.csys, (int)row, nk_vec2(240 * s, 4 * row + 20 * s));
                bool ch = c != G.csys;
                G.csys = c;
                if (G.csys) {
                    float st = G.diag * 0.01f + 1e-30f;
                    static const char* nm[3] = { "#x0", "#y0", "#z0" };
                    nk_layout_row_dynamic(ctx, row, 3);
                    for (int k = 0; k < 3; k++) {
                        tip(ctx, "A point on the axis");
                        float v = nk_propertyf(ctx, nm[k], -1e30f, G.csys_o[k], 1e30f, st, st * 0.1f);
                        if (v != G.csys_o[k]) { G.csys_o[k] = v; ch = true; }
                    }
                }
                if (ch) app_select(G.field_name, G.comp);
            }
            if (G.cmp_on) {
                char lab[96];
                snprintf(lab, sizeof lab, "minus %s", cv_basename(G.cmp_path));
                tip(ctx, "Show this field minus the same field of the comparison run (same step index)");
                if (nk_checkbox_label(ctx, lab, &G.diff_mode)) app_select(G.field_name, G.comp);
            }
            for (int f = 0; f < st->nfields; f++) {
                const cv_field_desc* d = &st->fields[f];
                bool active = G.field_src == 0 && strcmp(d->name, G.field_name) == 0;
                bool sf = !strcmp(d->name, "SHELL"), rb = !strcmp(d->name, "REBAR");   /* worked out from STRESS with the deck (app_shell.c) */
                if (sf) tip(ctx, "Shell section forces per width from the stresses through the thickness, in the shell's axes:\n"
                                 "N membrane, M bending (positive: the +normal side pulls; about the OFFSET surface), Q transverse shear (rough)");
                if (rb) tip(ctx, "Reinforcement of a concrete shell from its section forces: the steel per width each face needs\n"
                                 "in x and y (sandwich model, Wood-Armer), and where the concrete is crushed");
                if (!nk_tree_push_id(ctx, NK_TREE_NODE, sf ? "SHELL (shell forces)" : rb ? "REBAR (reinforcement)" : d->name,
                                     active ? NK_MAXIMIZED : NK_MINIMIZED, 100 + f))
                    continue;
                cv_scalar_opt opts[CV_MAX_OPTS];
                int n = app_field_options(d, opts, CV_MAX_OPTS);
                nk_layout_row_dynamic(ctx, row, 1);
                for (int i = 0; i < n; i++) {
                    bool sel = active && G.comp == opts[i].comp;
                    if (nk_option_label(ctx, opts[i].label, sel) && !sel) app_select_src(d->name, opts[i].comp, 0);
                }
                if (rb) {
                    tip(ctx, "The design values: concrete fcd, steel fyd, the cover to the bars");
                    if (nk_button_label(ctx, "Reinforcement...")) G.show_rebar = !G.show_rebar;
                }
                nk_tree_pop(ctx);
            }
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, 100 * s);
            nk_layout_row_template_end(ctx);
            nk_spacing(ctx, 1);
            tip(ctx, "The field integrated over the shown elements at every step: volume integral and average\n"
                     "(the homogenised <S>, <E> of an RVE); in its window a set, a surface or node sums instead");
            if (nk_button_label(ctx, "integrate...")) {
                if (GI.open) app_integ_close(); else app_integ_open(CV_IK_VOLUME, "shown");
            }
            section_calc(ctx, s, row);
            section_failure(ctx, s, row);
            section_mesh(ctx, s, row);
            section_label(ctx, s, row);
        }
        /* integration-point fields from the .dat, at this increment */
        if (gp_loaded()) {
            const char* names[32];
            int nf = gp_fields(names, 32);
            if (nf == 0) {
                nk_layout_row_dynamic(ctx, row, 1);
                nk_label_colored(ctx, "(.dat: nothing at this increment)", NK_TEXT_LEFT, P.dim);
            }
            for (int f = 0; f < nf; f++) {
                cv_field_desc d;
                if (!gp_desc(names[f], &d)) continue;
                bool active = G.field_src == 1 && strcmp(names[f], G.field_name) == 0;
                char title[64];
                snprintf(title, sizeof title, "%s (.dat)", names[f]);
                if (!nk_tree_push_id(ctx, NK_TREE_NODE, title, active ? NK_MAXIMIZED : NK_MINIMIZED, 300 + f))
                    continue;
                cv_scalar_opt opts[CV_MAX_OPTS];
                int n = cv_field_options(&d, opts, CV_MAX_OPTS);
                nk_layout_row_dynamic(ctx, row, 1);
                for (int i = 0; i < n; i++) {
                    bool sel = active && G.comp == opts[i].comp;
                    if (nk_option_label(ctx, opts[i].label, sel) && !sel) app_select_src(names[f], opts[i].comp, 1);
                }
                nk_tree_pop(ctx);
            }
        }
        nk_tree_pop(ctx);
    }

}

/* ---- Export: pictures, animations, data and the view state ------------------- */
static void section_export(struct nk_context* ctx, float s, float row) {
    if (!nk_tree_state_push(ctx, NK_TREE_TAB, "Export", (enum nk_collapse_states*)&G.tree[CV_TREE_EXPORT])) return;
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "The 3D view with legend and axes as <model>_stepN.png beside the model, numbered, never overwritten");
    if (nk_button_label(ctx, "Image of the view (Ctrl+E)")) app_export_png();
    tip(ctx, "The PNG with a transparent background: only the model, legend and axes on the page");
    nk_checkbox_label(ctx, "transparent background", &G.png_alpha);

    /* animation: what loops, in which form, how long */
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "One full swing of the deformation of this step (Animate)");
    if (nk_option_label(ctx, "deformation cycle", G.exp_kind == 0)) G.exp_kind = 0;
    tip(ctx, "Every increment once, each held as long as the play speed (fps in the time bar) shows it");
    if (nk_option_label(ctx, "every step", G.exp_kind == 1)) G.exp_kind = 1;
    if (nk_option_label(ctx, "MP4 video", G.exp_video)) G.exp_video = true;
    tip(ctx, "One PNG per frame; for 'every step' one per increment");
    if (nk_option_label(ctx, "PNG sequence", !G.exp_video)) G.exp_video = false;
    if (G.exp_kind == 0) {
        tip(ctx, "How often the swing repeats in the clip");
        nk_property_int(ctx, "#cycles", 1, &G.exp_cycles, 20, 1, 0.1f);
    } else {
        nk_spacing(ctx, 1);
    }
    tip(ctx, "Frames per second of the video (and per cycle of a PNG sequence)");
    nk_property_int(ctx, "#fps", 1, &G.exp_fps, 60, 1, 0.2f);
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "Keep the legend's range fixed while recording, so colours mean the same in every frame");
    nk_checkbox_label(ctx, "lock the colour range while recording", &G.exp_lock_range);
    {
        char lab[96];
        int fps = CV_MAX(G.exp_fps, 1);
        if (G.exp_kind == 0) {
            int per = CV_MAX(2, (int)(fps * CV_MAX(G.anim_period, 0.5f))) * CV_MAX(G.exp_cycles, 1);
            snprintf(lab, sizeof lab, "Export animation  (%d frames, %.1f s)", per, (float)per / fps);
        } else {
            int hold = G.exp_video ? CV_MAX(1, (int)((float)fps / CV_MAX(G.fps, 0.5f) + 0.5f)) : 1;
            snprintf(lab, sizeof lab, "Export animation  (%d steps%s)", G.frd.n_steps,
                     G.exp_video ? "" : ", one PNG each");
            if (G.exp_video) snprintf(lab, sizeof lab, "Export animation  (%d steps, %.1f s)", G.frd.n_steps, (float)G.frd.n_steps * hold / fps);
        }
        if (G.seq_left > 0) nk_label_colored(ctx, "exporting... (stop: status bar)", NK_TEXT_CENTERED, P.warn);
        else if (nk_button_label(ctx, lab)) app_export_animation();
    }

    /* data and the view state */
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "id, x, y, z, displacement and the field per node, visible elements only");
    if (nk_button_label(ctx, "CSV nodes")) app_export_data(false);
    tip(ctx, "Legacy VTK unstructured grid with the field, for ParaView");
    if (nk_button_label(ctx, "VTK mesh")) app_export_data(true);
    {
        char vp[1100];
        snprintf(vp, sizeof vp, "%.*s_view.ini", (int)(strrchr(G.path, '.') && strrchr(G.path, '.') > strrchr(G.path, cv_path_sep()) ? strrchr(G.path, '.') - G.path : (int)strlen(G.path)), G.path);
        tip(ctx, "Camera, step, field, layers, clip and crop to <model>_view.ini, to reproduce this picture later");
        if (nk_button_label(ctx, "save view state")) { snprintf(G.note, sizeof G.note, app_view_save(vp) ? "saved %s" : "could not write %s", vp); G.note_t = cv_now(); }
        if (nk_button_label(ctx, "load view state")) { snprintf(G.note, sizeof G.note, app_view_load(vp) ? "loaded %s" : "no %s", vp); G.note_t = cv_now(); }
    }
    nk_tree_state_pop(ctx);
}

/* The recent files as one left-aligned button each (full path in the tooltip),
   in the empty view: no file open yet, so they are what you most likely want. */
void recent_buttons(struct nk_context* ctx, float row) {
    const char* recent[CV_CFG_RECENT];
    int nr = settings_recent(recent, CV_CFG_RECENT);
    if (!nr || app_busy()) return;
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label_colored(ctx, "Recent files", NK_TEXT_LEFT, P.dim);
    nk_style_push_flags(ctx, &ctx->style.button.text_alignment, NK_TEXT_LEFT);
    for (int i = 0; i < nr; i++) {
        tip(ctx, recent[i]);
        if (nk_button_label(ctx, cv_basename(recent[i]))) app_open(recent[i]);
    }
    nk_style_pop_flags(ctx);
}

void panel_scene(struct nk_context* ctx, float s, float row) {
    /* open */
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 76 * s);
    nk_layout_row_template_end(ctx);
    if (g_focus_open) { nk_edit_focus(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER); g_focus_open = false; }
    tip(ctx, "Path of a .frd / .inp / .fbd / .dat / .stl, then Enter  (Ctrl+L)");
    nk_flags ev = nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, G.open_buf, &G.open_len,
                                 (int)sizeof G.open_buf - 1, nk_filter_default);
    G.open_buf[G.open_len] = 0;
    if (ev & NK_EDIT_COMMITED) app_open(G.open_buf);          /* typed path + Enter */
    if (nk_button_label(ctx, G.dlg_running ? "..." : IC_FOLDER_OPEN "  Open")) app_open_dialog(CV_DLG_MODEL);
    if (G.loaded) {                          /* recent files: a drop-down under the path box (the empty view lists them) */
        const char* recent[CV_CFG_RECENT];
        int nr = settings_recent(recent, CV_CFG_RECENT);
        if (nr) {
            nk_layout_row_dynamic(ctx, row, 1);
            tip(ctx, "Files opened before");
            if (nk_combo_begin_label(ctx, "recent files", nk_vec2(300 * s, (nr + 1) * (row + ctx->style.window.spacing.y) + 8 * s))) {
                nk_layout_row_dynamic(ctx, row, 1);
                for (int i = 0; i < nr; i++)
                    if (nk_combo_item_label(ctx, cv_basename(recent[i]), NK_TEXT_LEFT)) app_open(recent[i]);
                nk_combo_end(ctx);
            }
        }
    }

    if (!G.loaded) {
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label(ctx, app_busy() ? "Loading..." : "Open... (Ctrl+O), drop a .frd/.inp/.fbd", NK_TEXT_LEFT);
        if (!app_busy()) nk_label(ctx, "on the window, or type a path + Enter.", NK_TEXT_LEFT);
        return;
    }

    /* a cgx script that was not evaluated: say what it is, offer to run it */
    if (geo_loaded() && geo_get()->needs_cgx && !geo_evaluated()) {
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label(ctx, "This .fbd is a cgx script.", NK_TEXT_LEFT);
        if (nk_button_label(ctx, "Evaluate with cgx")) app_eval_cgx();
        nk_label_colored(ctx, "Runs its commands in a temporary copy", NK_TEXT_LEFT, P.warn);
        nk_label_colored(ctx, "of its folder. Only for scripts you trust.", NK_TEXT_LEFT, P.warn);
    }

    section_layers(ctx, s, row);
    section_groups(ctx, s, row);
    section_fields(ctx, s, row);
    section_view(ctx, s, row);
    section_export(ctx, s, row);

    uii_hsep(ctx, s);
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label_colored(ctx, "drag: orbit   shift/right/middle: pan", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "hold X/Y/Z + drag: about that axis", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "ctrl drag: box zoom   ctrl right: zoom", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "ctrl shift drag: max in a box", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "alt drag: roll   middle click, C: centre", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "N: normal to face   ctrl Z/Y: view back/fwd", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "ctrl arrows: turn 15 (shift 90)   alt: roll", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "wheel: zoom   click: probe   F: fit", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "space: play   arrows: step", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "+/-: deform scale   R: reset view   1-6: views", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "G: free flight   H: view only", NK_TEXT_LEFT, P.dim);
    nk_label_colored(ctx, "ctrl +/-/0: UI size", NK_TEXT_LEFT, P.dim);
}
