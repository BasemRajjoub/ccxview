/* ui_panels.c -- the scene sidebar on the left: the file box and recent files,
   Layers, Groups (with the deck's and cgx's sets), Fields and Export. Its View
   section is in ui_view.c. */
#include <ctype.h>
#include "app.h"
#include "calc.h"
#include "calclib.h"
#include "ui.h"
#include "cfg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"
#include "app_cmds.h"
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

/* Every group row's visibility, the same way: an eye (open: shown), then the name,
   which toggles it too and is dimmed while hidden. true: toggled. */
static bool eye_row(struct nk_context* ctx, const char* lab, bool shown, const char* tip_shown, const char* tip_hidden) {
    tip(ctx, shown ? tip_shown : tip_hidden);
    bool ch = nk_button_label(ctx, shown ? IC_EYE : IC_EYE_OFF);
    struct nk_style_selectable* st = &ctx->style.selectable;
    nk_style_push_style_item(ctx, &st->normal, nk_style_item_hide());   /* plain text, lit under the mouse */
    nk_style_push_color(ctx, &st->text_normal, shown ? P.text : P.dim);
    nk_style_push_color(ctx, &st->text_hover, shown ? P.text : P.dim);
    ch |= nk_select_label(ctx, lab, NK_TEXT_LEFT, nk_false) != 0;
    nk_style_pop_color(ctx); nk_style_pop_color(ctx); nk_style_pop_style_item(ctx);
    return ch;
}

/* a toggle button that reads as on: the accent colour behind it */
static bool lit_toggle(struct nk_context* ctx, const char* lab, bool* on) {
    struct nk_style_selectable* st = &ctx->style.selectable;
    nk_style_push_style_item(ctx, &st->normal_active, nk_style_item_color(P.accent));
    nk_style_push_style_item(ctx, &st->hover_active, nk_style_item_color(P.accent));
    nk_style_push_color(ctx, &st->text_normal_active, nk_rgb(20, 20, 20));
    nk_style_push_color(ctx, &st->text_hover_active, nk_rgb(20, 20, 20));
    nk_bool v = *on;
    bool ch = nk_selectable_label(ctx, lab, NK_TEXT_CENTERED, &v) != 0;
    *on = v;
    nk_style_pop_color(ctx); nk_style_pop_color(ctx);
    nk_style_pop_style_item(ctx); nk_style_pop_style_item(ctx);
    return ch;
}

/* A right click on a set's row (laid out next): integrate over it, select it. kind:
   what the set holds (elements: a volume, faces: a surface, nodes: a sum). */
static void set_menu_at(struct nk_context* ctx, float s, float row, int kind, const char* name, bool surf, struct nk_rect b);
static void set_menu(struct nk_context* ctx, float s, float row, int kind, const char* name, bool surf) {
    uii_test_mark(ctx, "#set menu");
    set_menu_at(ctx, s, row, kind, name, surf, nk_widget_bounds(ctx));
}
static void set_menu_at(struct nk_context* ctx, float s, float row, int kind, const char* name, bool surf, struct nk_rect b) {
    static const char* what[CV_IK_N] = { "Integrate over set %s", "Integrate over surface %s", "Sum over set %s" };
    char lab[96];
    snprintf(lab, sizeof lab, what[kind], name);
    if (nk_contextual_begin(ctx, 0, nk_vec2(240 * s, 6.4f * row + 10 * s), b)) {
        uik_cols(ctx, 1);
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

/* The deck's named sets: element sets show and hide like the other groups (the
   eye), and "only" shows the sets marked so and nothing else (several: together;
   none marked: no such filter); a hidden set stays hidden even when marked. Node
   sets and surfaces are highlights, ticked. */
static void panel_deck_sets(struct nk_context* ctx, float s, float row) {
    const cv_inp* d = deck_get();
    if (!d) return;
    bool* on = deck_set_flags();
    bool* son = deck_surf_flags();
    int ne = 0, nn = 0;
    for (int i = 0; i < d->nsets; i++) { if (d->sets[i].is_elem) ne++; else nn++; }
    char lab[96];
    if (d->nsteps || d->namps || d->nsubs) {
        static const char* const sl[1] = { "Steps, amplitudes ..." };
        static const char* const st[1] = { "The deck's steps and their times, the elements *MODEL CHANGE takes out,\n"
                                           "what a submodel's global model drives, the amplitudes and their points" };
        if (uik_buttons(ctx, NULL, 1, sl, st) == 0) G.show_deck = !G.show_deck;
    }
    if (ne && nk_tree_push_id(ctx, NK_TREE_NODE, "Element sets", NK_MAXIMIZED, 40)) {
        bool* hid = deck_set_hidden_flags();
        if (deck_any_elset_on() || deck_any_elset_hidden()) {
            static const char* const sl[1] = { "show every set" };
            static const char* const st[1] = { "Every element set shown again, none marked 'only'" };
            if (uik_buttons(ctx, NULL, 1, sl, st) == 0) {
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
            nk_layout_row_template_push_static(ctx, 44 * s);
            nk_layout_row_template_end(ctx);
            struct nk_rect rb = nk_widget_bounds(ctx);      /* the right click menu: over the whole row */
            rb.w = ctx->current->layout->bounds.w;
            uii_test_mark(ctx, "#set menu");
            if (eye_row(ctx, lab, !hid[i], "Hide this element set, everything else stays",
                        "Hidden: click to show this element set again")) { hid[i] = !hid[i]; app_groups_changed(); }
            tip(ctx, "Show only the sets marked 'only' (several: together); none marked: every set.\n"
                     "A hidden set stays hidden; types and materials hidden above stay hidden too");
            if (lit_toggle(ctx, "only", &on[i])) app_groups_changed();
            set_menu_at(ctx, s, row, CV_IK_VOLUME, d->sets[i].name, false, rb);
        }
        nk_tree_pop(ctx);
    }
    if (nn && nk_tree_push_id(ctx, NK_TREE_NODE, "Node sets", NK_MINIMIZED, 41)) {
        uik_cols(ctx, 1);
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
        uik_cols(ctx, 1);
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
        uik_cols(ctx, 1);
        for (int i = 0; i < d->nsurfs && i < 500; i++) {
            snprintf(lab, sizeof lab, "%s  (%u)", d->surfs[i].name, d->surfs[i].n ? d->surfs[i].n : d->surfs[i].nn);
            set_menu(ctx, s, row, d->surfs[i].n ? CV_IK_SURFACE : CV_IK_NODES, d->surfs[i].name, true);
            if (nk_checkbox_label(ctx, lab, &son[i])) { G.show_hl = true; deck_refresh_highlight(); }
        }
        nk_tree_pop(ctx);
    }
}

/* cgx sets: "only" shows only what the marked ones hold (their geometry, and
   their elements when cgx meshed the script), as the deck's element sets. */
static void panel_geo_sets(struct nk_context* ctx, float s, float row) {
    const cv_fbd* g = geo_get();
    if (!g || !g->nsets) return;
    bool* on = geo_set_flags();
    if (!nk_tree_push_id(ctx, NK_TREE_NODE, "cgx sets", NK_MAXIMIZED, 43)) return;
    if (geo_any_on()) {
        static const char* const sl[1] = { "show every set" };
        if (uik_buttons(ctx, NULL, 1, sl, NULL) == 0) geo_show_all();
    }
    char lab[96];
    for (int i = 0, k = 0; i < g->nsets && k < 500; i++) {
        const cv_gset* st = &g->sets[i];
        if (geo_set_hidden(st->name)) continue;
        k++;
        uint32_t ng = st->npts + st->ncrv + st->nsrf;
        if (st->nel) snprintf(lab, sizeof lab, "%s  (%u el)", st->name, st->nel);
        else if (ng) snprintf(lab, sizeof lab, "%s  (%u)", st->name, ng);
        else snprintf(lab, sizeof lab, "%s  (%u nodes)", st->name, st->nnod);
        uik_wide_row(ctx, 44);
        nk_label(ctx, lab, NK_TEXT_LEFT);
        tip(ctx, "Show only what the sets marked 'only' hold (several: together); none marked: everything");
        if (lit_toggle(ctx, "only", &on[i])) geo_set_toggled(i);
    }
    nk_tree_pop(ctx);
}

/* The field's own layers on and off, mirrored from its options in Fields (arrows,
   tensor glyphs, trajectories): where they were before they moved there */
static void layer_mirrors(struct nk_context* ctx, float row) {
    (void)row;
    bool vec = app_field_is_vector(), ten = app_field_is_tensor();
    if (!vec && !ten) return;
    char help[200];
    snprintf(help, sizeof help, "The field's %s; their settings: Fields > %s > options",
             vec ? (G.comp < CV_COMP_MISES ? "principal directions as arrow pairs" : "arrows at the nodes") : "glyphs", G.field_name);
    if (vec && uik_check(ctx, G.comp < CV_COMP_MISES ? "Directions" : "Vectors", &G.show_vec, help)) app_vectors_changed();
    if (ten) {
        snprintf(help, sizeof help, "The tensor as a glyph at each element's centre; style and size: Fields > %s > options", G.field_name);
        if (uik_check(ctx, "Tensors", &G.show_tensor, help)) app_tensors_changed();
        snprintf(help, sizeof help, "Principal stress trajectories; family and spacing: Fields > %s > options", G.field_name);
        if (uik_check(ctx, "Trajectories", &G.show_traj, help)) app_traj_changed();
    }
}

#define TIP_DECK_BC "Supports of the step on screen: a cone per held DOF (double base for a rotation), along the\nnode's *TRANSFORM axes; a prescribed displacement as an arrow with a bar across its tail,\na prescribed rotation as a turning arrow, a temperature as a cross"
#define TIP_DECK_LOADS "Loads of the step on screen. Yellow: forces, pressures, edge loads (bar across the tail),\ngravity (block arrow), centrifugal (dashed axis, turning arc), bolt preload (ring with arrows).\nMagenta: moments. Red: heat (zigzag), film (zigzag and bar), radiation (rays), given temperature (diamond).\nLength follows the magnitude within each kind"
#define TIP_DECK_DISC "Springs as zigzags, dashpots as pistons, gaps as facing bars, masses as cubes.\nA one-node spring points along its dof to a ground bar."
#define TIP_DECK_LINKS "Rigid bodies, couplings and equations as spiders from the reference node;\ntick single ones under Groups > Couplings"

/* the deck's layers as chips (modern widgets) */
static void deck_chips(struct nk_context* ctx, bool links) {
    const char* l[4]; nk_bool* v[4]; const char* t[4];
    int n = 0;
    if (deck_has_bc() || deck_has_loads()) {
        l[n] = "Supports"; v[n] = &G.show_bc; t[n++] = TIP_DECK_BC;
        l[n] = "Loads"; v[n] = &G.show_loads; t[n++] = TIP_DECK_LOADS;
    }
    if (deck_has_discrete()) { l[n] = "Springs, masses"; v[n] = &G.show_disc; t[n++] = TIP_DECK_DISC; }
    if (links) { l[n] = "Couplings"; v[n] = &G.show_links; t[n++] = TIP_DECK_LINKS; }
    uik_chips(ctx, n, l, v, t);
}

/* ---- Layers: what is drawn and how it is coloured */
void section_layers(struct nk_context* ctx, float s, float row) {
    (void)s;
    if (!uik_section(ctx, "Layers", CV_TREE_LAYERS)) return;
    if (geo_loaded()) {                      /* cgx geometry */
        uik_group(ctx, "Geometry");
        uik_cols(ctx, 3);
        nk_checkbox_label(ctx, "Points", &G.show_geo_pts);
        nk_checkbox_label(ctx, "Lines", &G.show_geo_crv);
        nk_checkbox_label(ctx, "Surfs", &G.show_geo_srf);
        if (G.show_geo_pts) uik_num(ctx, "point size", &G.geo_size, 1.f, 20.f, 1.f, "Size of the geometry points");
    }
    bool links = deck_get() && deck_get()->nlinks;
    if (deck_has_bc() || deck_has_loads() || deck_has_discrete()) {
        uik_group(ctx, "Deck");
        if (uim_on(ctx)) deck_chips(ctx, links);
        else {
        uik_cols(ctx, 2);
        if (deck_has_bc() || deck_has_loads()) {
            tip(ctx, TIP_DECK_BC);
            nk_checkbox_label(ctx, "Supports", &G.show_bc);
            tip(ctx, TIP_DECK_LOADS);
            nk_checkbox_label(ctx, "Loads", &G.show_loads);
        }
        if (deck_has_discrete()) {
            tip(ctx, TIP_DECK_DISC);
            nk_checkbox_label(ctx, "Springs, masses", &G.show_disc);
        }
        if (links) {
            tip(ctx, TIP_DECK_LINKS);
            nk_checkbox_label(ctx, "Couplings", &G.show_links);
        }
        }
    }
    uik_group(ctx, "Field");
    uik_check2(ctx, "Min / max", &G.show_markers, "Balls at the field's minimum and maximum (legend settings: go there)",
               "Undeformed", &G.show_ghost, "The undeformed edges in grey behind the deformed shape");
    uik_group(ctx, "Faces and edges");
    uik_cols(ctx, 2);
    nk_checkbox_label(ctx, "Faces", &G.show_faces);
    {
        const char* const* fm_names = cv_faces_mode_names;
        uii_test_mark(ctx, "#faces mode");
        int fm = G.faces_mode;
        if (uim_seg_fits(ctx, fm_names, FM_N)) uim_segments(ctx, fm_names, FM_N, &fm);
        else fm = nk_combo(ctx, (const char**)fm_names, FM_N, G.faces_mode, (int)row, nk_vec2(150 * s, FM_N * (row + 4 * s) + 16 * s));
        if (fm != G.faces_mode) app_set_faces_mode(fm);
    }
    if (G.show_faces) {                      /* see-through: into an assembly, a contact zone */
        if (uik_float(ctx, "opacity", &G.model_alpha, 0.f, 1.f, 0.05f,
                      "Opacity of the faces: less to see into the model (a contact zone inside an assembly).\n"
                      "Each element set's own: Fields > Contact > more")) app_see_refresh();
        if (uik_check(ctx, "mid-side nodes", &G.mid_faces,
                      "Faces of quadratic elements (C3D20, C3D10, S8, ...) drawn through their mid-side nodes:\ncolours and shape follow every node. Off: corners only, a third of the triangles,\nbut a value at a mid node (bending through one shell layer) does not show")) app_groups_changed();
    }
    uik_check2(ctx, "Edges", &G.show_edges, "Edges of the exterior faces",
               "coloured", &G.edges_field, "Paint the field on the edges too (else dark lines)");
    uik_check(ctx, "Outline", &G.show_outline, "Part outline: borders, creases sharper than the angle, material and type changes.\nStays when the mesh edges hide.");
    if (G.show_outline) {
        float a = G.outline_angle;
        uik_num(ctx, "crease", &a, 1.f, 180.f, 5.f, "Faces meeting at more than this many degrees make a crease");
        if (a != G.outline_angle) { G.outline_angle = a; app_groups_changed(); }
    }
    bool dense = G.edges_auto && G.show_faces && G.edges_dense;
    if ((G.show_edges || G.show_nodes || G.show_gp) && G.show_faces) {
        uik_check(ctx, "hide when dense", &G.edges_auto, "Hide the edges, nodes and Gauss points while the mesh is denser than ~3 px on\nscreen, where they would paint the surface black; zoom in to see them");
        if (dense) { uik_cols(ctx, 1); nk_label_colored(ctx, "hidden: zoom in", NK_TEXT_LEFT, P.warn); }
    }
    uik_group(ctx, "Nodes and points");
    uik_check2(ctx, "Nodes", &G.show_nodes, "Nodes of the visible elements, as small balls",
               "coloured", &G.nodes_field, "Paint the field on the nodes too");
    if (G.show_nodes) uik_num(ctx, "point size", &G.point_size, 1.f, 12.f, 1.f, "Size of the node balls");
    {                                        /* integration points, next to the nodes */
        bool gp = G.show_gp;
        uik_check2(ctx, "Gauss pts", &G.show_gp, "Integration points: real .dat values when a .dat field is selected,\nelse the nodal field interpolated there",
                   "coloured", &G.gp_colored, "Colour the points by the field");
        if ((bool)G.show_gp != gp) app_gauss_changed();
        if (G.show_gp) {
            uik_num(ctx, "size", &G.gp_size, 1.f, 30.f, 1.f, "Size of the integration points");
            uik_check(ctx, "x-ray", &G.gp_on_top, "Draw the points through the faces (they sit inside the elements)");
        }
        layer_mirrors(ctx, row);
    }
    uik_end(ctx);
}

/* ---- Groups: element type / material / group, deck and cgx sets */
void section_groups(struct nk_context* ctx, float s, float row) {
    /* groups */
    if (uik_section(ctx, "Groups", CV_TREE_GROUPS)) {
        {                                        /* Show all, always there; what is hidden said beside it */
            bool any = app_any_hidden();
            uint32_t nh = any ? app_hidden_by_hand() : 0;
            char lab[64];
            if (nh) snprintf(lab, sizeof lab, "%u hidden by hand", nh);
            else snprintf(lab, sizeof lab, any ? "some hidden below" : "everything shown");
            uik_wide_row(ctx, 80);
            nk_label_colored(ctx, lab, NK_TEXT_LEFT, any ? P.warn : P.dim);
            tip(ctx, "Every element shown again: types, materials, groups, element sets ('only' and hidden),\n"
                     "cgx sets, elements hidden by hand (right-click menu, Selection). The crop box, the clip\n"
                     "plane and *MODEL CHANGE keep their own switches");
            if (nk_button_label(ctx, "Show all")) app_show_all();
        }
        for (int a = 0; a < CV_AXIS_N; a++) {
            cv_axis* ax = &G.groups.axis[a];
            if (!nk_tree_push_id(ctx, NK_TREE_NODE, cv_axis_name(a), a == 0 ? NK_MAXIMIZED : NK_MINIMIZED, a))
                continue;
            if (ax->n > 1) {
                static const char* const bl[2] = { "show all", "hide all" };
                int hit = uik_buttons(ctx, NULL, 2, bl, NULL);
                if (hit >= 0) { for (int i = 0; i < ax->n; i++) ax->on[i] = hit == 0; app_groups_changed(); }
            }
            int shown = CV_MIN(ax->n, 500);            /* keep the list usable */
            for (int i = 0; i < shown; i++) {
                char lab[64];
                axis_label(lab, sizeof lab, a, ax->value[i], ax->count[i]);
                nk_layout_row_template_begin(ctx, row);
                nk_layout_row_template_push_static(ctx, row * 1.4f);
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
                        uik_cols(ctx, 3);
                        nf.r = nk_propertyf(ctx, "#R", 0, nf.r, 1, 0.01f, 0.005f);
                        nf.g = nk_propertyf(ctx, "#G", 0, nf.g, 1, 0.01f, 0.005f);
                        nf.b = nk_propertyf(ctx, "#B", 0, nf.b, 1, 0.01f, 0.005f);
                        if (nf.r != rgb[0] || nf.g != rgb[1] || nf.b != rgb[2]) {
                            rgb[0] = nf.r; rgb[1] = nf.g; rgb[2] = nf.b;
                            if (G.faces_mode != FM_TYPE + a) app_set_faces_mode(FM_TYPE + a);
                            else app_group_colors_changed();
                        }
                        uik_cols(ctx, 1);
                        if (nk_button_label(ctx, "done")) {
                            g_pick_axis = g_pick_idx = -1;
                            nk_popup_close(ctx);
                        }
                        nk_popup_end(ctx);
                    } else {
                        g_pick_axis = g_pick_idx = -1;
                    }
                }
                static const char* const shown_tip[CV_AXIS_N] = { "Hide this element type", "Hide this material", "Hide this group" };
                static const char* const hidden_tip[CV_AXIS_N] = { "Hidden: click to show this element type again",
                    "Hidden: click to show this material again", "Hidden: click to show this group again" };
                if (eye_row(ctx, lab, ax->on[i], shown_tip[a], hidden_tip[a])) { ax->on[i] = !ax->on[i]; app_groups_changed(); }
            }
            if (ax->n > shown) {
                uik_note(ctx, "(list truncated)");
            }
            nk_tree_pop(ctx);
        }
        if (geo_loaded()) panel_geo_sets(ctx, s, row);   /* cgx sets drive the mesh's too */
        else panel_deck_sets(ctx, s, row);
        uik_end(ctx);
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

int ui_lib_open = NK_MINIMIZED;                 /* the Saved formulas tree (the interface test opens it) */

static void section_calc(struct nk_context* ctx, float s, float row) {
    if (strcmp(calc_seen, G.calc_expr)) {                 /* set elsewhere: --calc, a view file */
        snprintf(calc_seen, sizeof calc_seen, "%s", G.calc_expr);
        snprintf(calc_buf, sizeof calc_buf, "%s", G.calc_expr);
        calc_len = (int)strlen(calc_buf);
    }
    bool active = G.field_src == 2;
    if (!nk_tree_push_id(ctx, NK_TREE_NODE, "Calculated", active ? NK_MAXIMIZED : NK_MINIMIZED, 200)) return;
    uik_wide_row(ctx, 56);
    tip(ctx, "A formula of the fields, then Enter: STRESS_SXX - STRESS_SYY, sqrt(D1^2 + D2^2), if(MISES > 200, 1, 0)");
    nk_flags ev = nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, calc_buf, &calc_len, (int)sizeof calc_buf - 1, nk_filter_default);
    calc_buf[calc_len] = 0;
    bool go = (ev & NK_EDIT_COMMITED) != 0;
    tip(ctx, "Colour the model by the formula");
    if (nk_button_label(ctx, "Show")) go = true;
    if (go && calc_buf[0]) {
        if (app_pick_calc(calc_buf)) snprintf(calc_seen, sizeof calc_seen, "%s", G.calc_expr);
    } else if (go) {
        G.calc_err[0] = 0;
    }
    if (G.calc_err[0] && strcmp(calc_buf, G.calc_expr)) {
        uik_cols(ctx, 1);
        nk_label_colored(ctx, G.calc_err, NK_TEXT_LEFT, P.warn);
    } else if (strcmp(calc_buf, G.calc_expr) && calc_buf[0]) {
        uik_cols(ctx, 1);
        nk_label_colored(ctx, "Enter or Show to apply", NK_TEXT_LEFT, P.dim);
    }
    static const char* const fl[1] = { "Formula..." };
    static const char* const ft[1] = { "Build a formula from buttons: this file's fields, operators, functions and examples" };
    if (uik_buttons(ctx, NULL, 1, fl, ft) == 0) G.show_calc_help = !G.show_calc_help;
    /* saved formulas: a text file beside the ini (calclib.h); a list for the same reason as below */
    if (nk_tree_state_push(ctx, NK_TREE_NODE, "Saved formulas", (enum nk_collapse_states*)&ui_lib_open)) {
        static char lname[CV_FORMULA_NAME]; static int lname_len, lsel = -1;
        const cv_calclib* lib = app_formulas();
        uik_wide_row(ctx, 56);
        tip(ctx, "A name for the formula above; Save keeps it in the library (the same name is replaced)");
        nk_edit_string(ctx, NK_EDIT_FIELD, lname, &lname_len, (int)sizeof lname - 1, nk_filter_default);
        lname[lname_len] = 0;
        tip(ctx, "Keep the formula in the box above under this name");
        if (nk_button_label(ctx, "Save") && lname[0] && calc_buf[0]) {
            if (!app_formula_save(lname, calc_buf)) cv_msg_add(&G.msgs, 0, false, "formulas: the name or formula is empty or too long");
            lsel = cv_calclib_find(app_formulas(), lname);
        }
        if (!lib->n) uik_note(ctx, "none saved yet");
        else uik_cols(ctx, 1);
        for (int i = 0; i < lib->n; i++) {
            tip(ctx, lib->a[i].expr);
            if (nk_select_label(ctx, lib->a[i].name, NK_TEXT_LEFT, i == lsel)) {
                lsel = i;
                snprintf(lname, sizeof lname, "%s", lib->a[i].name); lname_len = (int)strlen(lname);
                calc_draft_set(lib->a[i].expr);
                app_pick_calc(calc_buf);
                snprintf(calc_seen, sizeof calc_seen, "%s", G.calc_expr);
            }
        }
        static const char* const bl[3] = { "Delete", "Import...", "Export..." };
        static const char* const bt[3] = { "Remove the picked formula from the library",
            "Add the formulas of a text file (name = expression, one per line); names already here are kept",
            "Write the library as <model>_formulas.txt next to the result file" };
#ifdef __EMSCRIPTEN__
        int hit = uik_buttons(ctx, NULL, 1, bl, bt);
#else
        int hit = uik_buttons(ctx, NULL, 3, bl, bt);
        if (hit == 1) app_open_dialog(CV_DLG_FORMULAS);
        if (hit == 2) app_formulas_export();
#endif
        if (hit == 0 && lsel >= 0 && lsel < lib->n) { app_formula_delete(lsel); lsel = -1; }
        nk_tree_pop(ctx);
    }
    /* a list, not a combo: a combo this low in the panel opens past its edge, clipped away */
    if (nk_tree_push_id(ctx, NK_TREE_NODE, "Examples", NK_MINIMIZED, 201)) {
        uik_cols(ctx, 1);
        for (int i = 0; i < calc_example_count; i++) {
            if (!calc_example_ok(i)) continue;
            tip(ctx, calc_examples[i][2]);
            if (nk_button_label(ctx, calc_examples[i][0])) {
                calc_draft_set(calc_examples[i][0]);
                app_pick_calc(calc_buf);
                snprintf(calc_seen, sizeof calc_seen, "%s", G.calc_expr);
            }
        }
        nk_tree_pop(ctx);
    }
    nk_tree_pop(ctx);
}

/* The faces coloured by something else than the field (Layers > Faces, a group's
   swatch): say so above the fields, with one click back. Picking any field does it too. */
static void faces_note(struct nk_context* ctx, float s, float row) {
    if (G.faces_mode == FM_FIELD || !G.show_faces) return;
    char lab[64];
    snprintf(lab, sizeof lab, "faces: by %s", G.faces_mode == FM_PLAIN ? "one plain colour" : app_face_classes_title());
    uik_wide_row(ctx, 90);
    nk_label_colored(ctx, lab, NK_TEXT_LEFT, P.warn);
    tip(ctx, "The faces show the groups' colours, not the field (Layers > Faces).\n"
             "Colour them by the field again; picking any field below does it too");
    if (nk_button_label(ctx, "show field")) app_faces_show_field();
    uik_cols(ctx, 1);
}

/* ---- Fields of the current step, and the .dat ones */
void section_fields(struct nk_context* ctx, float s, float row) {
    /* fields of the current step */
    if (uik_section(ctx, "Fields", CV_TREE_FIELDS)) {
        if (G.frd.n_steps == 0) {
            uik_note(ctx, "No results in this file.");
            section_mesh(ctx, s, row);
        } else {
            const cv_step* st = &G.frd.steps[G.step];
            nk_bool em = G.elem_mode;
            if (uik_check(ctx, "Per element (flat)", &em, "One value per element (mean of its nodes) instead of a smooth nodal field")) app_set_elem_mode(em);
            faces_note(ctx, s, row);
            {   /* components of vectors and tensors in a cylindrical system */
                static const char* cs[] = { "coordinates: global x y z", "cylindrical about X", "cylindrical about Y", "cylindrical about Z" };
                bool ch = uik_combo(ctx, NULL, cs, 4, &G.csys,
                                    "Vector and tensor components in a cylindrical system: r radial, t hoop, a axial (.frd fields; invariants stay)");
                if (G.csys) {
                    float st = G.diag * 0.01f + 1e-30f;
                    static const char* const tp[3] = { "A point on the axis", "A point on the axis", "A point on the axis" };
                    if (uik_nums(ctx, "axis point", 3, G.csys_o, -1e30f, 1e30f, st, tp)) ch = true;
                }
                if (ch) app_select(G.field_name, G.comp);
            }
            if (G.cmp_on) {
                char lab[96];
                snprintf(lab, sizeof lab, "minus %s", cv_basename(G.cmp_path));
                if (uik_check(ctx, lab, &G.diff_mode, "Show this field minus the same field of the comparison run (same step index)")) app_select(G.field_name, G.comp);
            }
            uii_section_contact(ctx, s, row);       /* contact first: pairs, the .cel, the CONTACT block */
            for (int f = 0; f < st->nfields; f++) {
                const cv_field_desc* d = &st->fields[f];
                if (!app_en_listed(st, f)) continue;                 /* STRPOS / STRNEG: inside Stress; element-nodal off */
                bool active = G.field_src == 0 && strcmp(d->name, G.field_name) == 0;
                bool sf = !strcmp(d->name, "SHELL"), rb = !strcmp(d->name, "REBAR");   /* worked out from STRESS with the deck (app_shell.c) */
                if (sf) tip(ctx, "Shell section forces per width from the stresses through the thickness, in the shell's axes:\n"
                                 "N membrane, M bending (positive: the +normal side pulls; about the OFFSET surface), Q transverse shear (rough)");
                if (rb) tip(ctx, "Reinforcement of a concrete shell from its section forces: the steel per width each face needs\n"
                                 "in x and y (sandwich model, Wood-Armer), and where the concrete is crushed");
                int* fs = uii_fnode_state(d->name, active);
                if (!strcmp(G.fields_open, d->name)) { *fs = NK_MAXIMIZED; uii_scroll_here(ctx); }
                if (!nk_tree_state_push(ctx, NK_TREE_NODE, sf ? "SHELL (shell forces)" : rb ? "REBAR (reinforcement)" : app_field_title(st, f),
                                        (enum nk_collapse_states*)fs))
                    continue;
                cv_scalar_opt opts[CV_MAX_OPTS];
                int n = app_field_options(d, opts, CV_MAX_OPTS);
                uik_cols(ctx, 1);
                for (int i = 0; i < n; i++) {
                    bool sel = active && G.comp == opts[i].comp;
                    char mk[96];
                    snprintf(mk, sizeof mk, "#comp %s %s", d->name, opts[i].label);
                    uii_test_mark(ctx, mk);
                    if (sf || !strcmp(d->name, "SHR")) {                  /* what Nxx, Mxx, Qx mean */
                        const char* l = opts[i].label;
                        static const char* const full[3] = { "Membrane force per width, ", "Bending moment per width, ", "Transverse shear force per width, " };
                        char ax[4] = { (char)tolower((unsigned char)l[1]), l[1] ? (char)tolower((unsigned char)l[2]) : 0, 0, 0 };
                        char t[96];
                        int kind = tolower((unsigned char)l[0]) == 'n' ? 0 : tolower((unsigned char)l[0]) == 'm' ? 1 : tolower((unsigned char)l[0]) == 'q' ? 2 : -1;
                        if (kind >= 0) {
                            snprintf(t, sizeof t, "%s%s (%s)", full[kind], ax, kind == 0 ? "in-plane" : kind == 1 ? "about the surface" : "through the thickness");
                            tip(ctx, t);
                        }
                    }
                    if (nk_option_label(ctx, opts[i].label, sel) != sel) app_pick_field(d->name, opts[i].comp, 0);   /* the shown one again: back on the faces */
                }
                uii_field_options(ctx, s, row, d, active);
                if (!strcmp(G.fields_open, d->name)) G.fields_open[0] = 0;
                nk_tree_state_pop(ctx);
            }
            uik_wide_row(ctx, 100);
            nk_spacing(ctx, 1);
            tip(ctx, "The field integrated over the shown elements at every step: volume integral and average\n"
                     "(the homogenised <S>, <E> of an RVE); in its window a set, a surface or node sums instead");
            if (nk_button_label(ctx, "integrate...")) {
                if (GI.open) app_integ_close(); else app_integ_open(CV_IK_VOLUME, "shown");
            }
            section_calc(ctx, s, row);
            section_failure(ctx, s, row);
            section_mesh(ctx, s, row);
        }
        /* integration-point fields from the .dat, at this increment */
        if (gp_loaded()) {
            const char* names[32];
            int nf = gp_fields(names, 32);
            if (nf == 0) uik_note(ctx, "(.dat: nothing at this increment)");
            for (int f = 0; f < nf; f++) {
                cv_field_desc d;
                if (!gp_desc(names[f], &d)) continue;
                bool active = G.field_src == 1 && strcmp(names[f], G.field_name) == 0;
                char title[64];
                snprintf(title, sizeof title, "%s (.dat)", names[f]);
                if (!nk_tree_state_push(ctx, NK_TREE_NODE, title, (enum nk_collapse_states*)uii_fnode_state(title, active)))
                    continue;
                cv_scalar_opt opts[CV_MAX_OPTS];
                int n = cv_field_options(&d, opts, CV_MAX_OPTS);
                uik_cols(ctx, 1);
                for (int i = 0; i < n; i++) {
                    bool sel = active && G.comp == opts[i].comp;
                    if (nk_option_label(ctx, opts[i].label, sel) != sel) app_pick_field(names[f], opts[i].comp, 1);
                }
                uii_gauss_options(ctx, s, row, names[f]);
                nk_tree_state_pop(ctx);
            }
        }
        uik_end(ctx);
    }
}

/* the animation's kind and form as segments (modern widgets) */
static void anim_segments(struct nk_context* ctx) {
    static const char* const kn[2] = { "deformation cycle", "every step" }, *const kt[2] = {
        "One full swing of the deformation of this step (Animate)",
        "Every increment once, each held as long as the play speed (fps in the time bar) shows it" };
    static const char* const fn[3] = { "MP4", "GIF", "PNG files" }, *const ft[3] = {
        "H.264 video: small files, plays everywhere",
        "Animated GIF, 256 fixed colours: bigger than MP4 for most models, but pastes into slides and chats as a picture",
        "One PNG per frame; for 'every step' one per increment" };
    uik_cols(ctx, 1);
    uim_segments_t(ctx, kn, 2, &G.exp_kind, kt);
    int f = !G.exp_video ? 2 : G.exp_gif ? 1 : 0;
    uik_cols(ctx, 1);
    if (uim_segments_t(ctx, fn, 3, &f, ft)) { G.exp_video = f < 2; G.exp_gif = f == 1; }
}

/* ---- the Export tab: pictures, animations, data and the view state ------------------- */
void section_export(struct nk_context* ctx, float s, float row) {
    (void)s; (void)row;
    uik_group(ctx, "Picture");
    static const char* const il[2] = { "Export picture...", "Save (Ctrl+E)" };
    static const char* const it[2] = { "The Export picture window: quick or rendered, the look, what is on it (legend, axes, title ...), the background",
                                       "The PNG of the view at once with the Export picture's settings, then the Save dialog" };
    int pk = uik_buttons(ctx, NULL, 2, il, it);
    if (pk == 0) app_picture_window(false);
    else if (pk == 1) app_export_png();

    /* animation: what loops, in which form, how long */
    uik_group(ctx, "Animation");
    if (uim_on(ctx)) anim_segments(ctx);
    else {
    uik_cols(ctx, 2);
    tip(ctx, "One full swing of the deformation of this step (Animate)");
    if (nk_option_label(ctx, "deformation cycle", G.exp_kind == 0)) G.exp_kind = 0;
    tip(ctx, "Every increment once, each held as long as the play speed (fps in the time bar) shows it");
    if (nk_option_label(ctx, "every step", G.exp_kind == 1)) G.exp_kind = 1;
    uik_cols(ctx, 3);
    tip(ctx, "H.264 video: small files, plays everywhere");
    if (nk_option_label(ctx, "MP4", G.exp_video && !G.exp_gif)) { G.exp_video = true; G.exp_gif = false; }
    tip(ctx, "Animated GIF, 256 fixed colours: bigger than MP4 for most models, but pastes into slides and chats as a picture");
    if (nk_option_label(ctx, "GIF", G.exp_video && G.exp_gif)) { G.exp_video = true; G.exp_gif = true; }
    tip(ctx, "One PNG per frame; for 'every step' one per increment");
    if (nk_option_label(ctx, "PNG files", !G.exp_video)) G.exp_video = false;
    }
    if (G.exp_kind == 0) uik_int(ctx, "cycles", &G.exp_cycles, 1, 20, 1, "How often the swing repeats in the clip");
    uik_int(ctx, "fps", &G.exp_fps, 1, 60, 1, "Frames per second of the video or GIF (and per cycle of a PNG sequence)");
    uik_check(ctx, "lock the colour range while recording", &G.exp_lock_range,
              "Keep the legend's range fixed while recording, so colours mean the same in every frame");
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
        uik_cols(ctx, 1);
        if (G.seq_left > 0) nk_label_colored(ctx, "exporting... (stop: status bar)", NK_TEXT_CENTERED, P.warn);
        else if (nk_button_label(ctx, lab)) app_export_animation();
    }

    /* data and the view state */
    uik_group(ctx, "Data");
    static const char* const dl[2] = { "CSV nodes", "VTK mesh" };
    static const char* const dt[2] = { "id, x, y, z, displacement and the field per node, visible elements only",
                                       "Legacy VTK unstructured grid with the field, for ParaView" };
    int hit = uik_buttons(ctx, NULL, 2, dl, dt);
    if (hit >= 0) app_export_data(hit == 1);
    static const char* const rl[3] = { "Report .md", "Report .pdf", "Builder..." };
    static const char* const rt[3] = { "The model definition as a Markdown report (mesh, materials, sections, loads, constraints, steps) to <model>_report.md",
                                       "The same report as a PDF, to <model>_report.pdf",
                                       "The Report builder: pick its parts and pictures of the view (isometric, front, top, right)" };
    hit = uik_buttons(ctx, NULL, 3, rl, rt);
    if (hit == 2) G.show_report = !G.show_report;
    else if (hit >= 0) app_export_report(NULL, hit == 1);
    {
        char vp[1100];
        snprintf(vp, sizeof vp, "%.*s_view.ini", (int)(strrchr(G.path, '.') && strrchr(G.path, '.') > strrchr(G.path, cv_path_sep()) ? strrchr(G.path, '.') - G.path : (int)strlen(G.path)), G.path);
        static const char* const vl[2] = { "save view state", "load view state" };
        static const char* const vt[2] = { "Camera, step, field, layers, clip and crop to <model>_view.ini, to reproduce this picture later", NULL };
        hit = uik_buttons(ctx, NULL, 2, vl, vt);
        if (hit == 0) { snprintf(G.note, sizeof G.note, app_view_save(vp) ? "saved %s" : "could not write %s", vp); G.note_t = cv_now(); }
        if (hit == 1) { snprintf(G.note, sizeof G.note, app_view_load(vp) ? "loaded %s" : "no %s", vp); G.note_t = cv_now(); }
    }
}

/* The recent files as one left-aligned button each (full path in the tooltip),
   in the empty view: no file open yet, so they are what you most likely want. */
void recent_buttons(struct nk_context* ctx, float row) {
    const char* recent[CV_CFG_RECENT];
    int nr = settings_recent(recent, CV_CFG_RECENT);
    if (!nr || app_busy()) return;
    uik_cols(ctx, 1);
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
    if (G.loaded) nk_layout_row_template_push_static(ctx, 24 * s);    /* hide the pane */
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 76 * s);
    nk_layout_row_template_end(ctx);
    if (G.loaded) uii_pane_button(ctx, 0);
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
            uik_cols(ctx, 1);
            tip(ctx, "Files opened before");
            if (nk_combo_begin_label(ctx, "recent files", nk_vec2(300 * s, (nr + 1) * (row + ctx->style.window.spacing.y) + 8 * s))) {
                uik_cols(ctx, 1);
                for (int i = 0; i < nr; i++)
                    if (nk_combo_item_label(ctx, cv_basename(recent[i]), NK_TEXT_LEFT)) app_open(recent[i]);
                nk_combo_end(ctx);
            }
        }
    }

    if (!G.loaded) {
        uik_cols(ctx, 1);
        nk_label(ctx, app_busy() ? "Loading..." : "Open... (Ctrl+O), drop a .frd/.inp/.fbd", NK_TEXT_LEFT);
        if (!app_busy()) nk_label(ctx, "on the window, or type a path + Enter.", NK_TEXT_LEFT);
        return;
    }

    /* a cgx script that was not evaluated: say what it is, offer to run it */
    if (geo_loaded() && geo_get()->needs_cgx && !geo_evaluated()) {
        uik_cols(ctx, 1);
        nk_label(ctx, "This .fbd is a cgx script.", NK_TEXT_LEFT);
        if (nk_button_label(ctx, "Evaluate with cgx")) app_eval_cgx();
        nk_label_colored(ctx, "Runs its commands in a temporary copy", NK_TEXT_LEFT, P.warn);
        nk_label_colored(ctx, "of its folder. Only for scripts you trust.", NK_TEXT_LEFT, P.warn);
    }

    if (search_panel(ctx, s, row)) return;     /* the search results stand in for the sections */
    uii_scene_body(ctx, s, row);
}
