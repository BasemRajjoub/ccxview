/* app_cmds.c -- the feature registry's table: one line per thing the left panel, the top bar or the
   settings offers. Add a control to a panel, add its line here (id: lower case, kept in the ini,
   never renamed; label as the panel shows it; keywords: words a user may type instead; help: the
   tooltip; the location as the panel names its section and group). A value points into G, or has
   get / set callbacks when the panel does more than write it; `act` is what the panel calls after
   the change (the same app_* call). Functions: app_cmds_lib.c; the search: ui_search.c.
   Not here: the field tree's own rows (fields differ per file), the Groups list, material looks per
   material, colour pickers, the free-text title block lines, per-set / per-pair contact rows: the GOTO
   entries open their sections. */
#include "app.h"
#include "app_cmds.h"
#include "app_mesh.h"
#include "render_bg.h"
#include "render.h"
#include "glyph.h"
#include "ui.h"

#define LOC(s, g, t, u)  .section = s, .group = g, .tree = t, .sub = u
#define LAY(g)           LOC("Model", "Layers", CV_TREE_LAYERS, -1)
#define GRP(g)           LOC("Model", "Groups", CV_TREE_GROUPS, -1)
#define IMP              LOC("Model", "Imported geometry", CV_TREE_IMPORT, -1)
#define FLD(g, u)        LOC("Results", g, CV_TREE_FIELDS, u)
#define LBLS             LOC("Results", "Labels", CV_TREE_LABELS, -1)
#define VW(g, u)         LOC("View", g, u, -1)
#define RND(g)           LOC("Render", g, CV_TREE_RENDER, -1)
#define EXP(g)           LOC("Export", g, CV_TREE_EXPORT, -1)
#define FIL              LOC("Export", "File", CV_TREE_EXPORT, -1)
#define SET(g)           LOC("Settings", g, -1, -1)
#define SET3D            LOC("Settings", "3D mouse", CV_TREE_NDOF, -1)
#define BAR              LOC("Top bar", NULL, -1, -1)

/* value entries: ptr into G, or get / set; act after a change */
#define BOOLP(i, l, k, h, ptr, a, loc)            { .id = i, .label = l, .keywords = k, .help = h, .kind = CV_CMD_BOOL, .p = &(ptr), .act = a, loc }
#define BOOLG(i, l, k, h, g, s, loc)              { .id = i, .label = l, .keywords = k, .help = h, .kind = CV_CMD_BOOL, .get = g, .set = s, loc }
#define ENUMP(i, l, k, h, ptr, it, a, loc)        { .id = i, .label = l, .keywords = k, .help = h, .kind = CV_CMD_ENUM, .p = &(ptr), .items = it, .n = (int)CV_COUNT(it), .act = a, loc }
#define ENUMG(i, l, k, h, it, g, s, loc)          { .id = i, .label = l, .keywords = k, .help = h, .kind = CV_CMD_ENUM, .items = it, .n = (int)CV_COUNT(it), .get = g, .set = s, loc }
#define INTP(i, l, k, h, ptr, lo_, hi_, st, a, loc) { .id = i, .label = l, .keywords = k, .help = h, .kind = CV_CMD_INT, .p = &(ptr), .lo = lo_, .hi = hi_, .step = st, .act = a, loc }
#define FLT(i, l, k, h, ptr, lo_, hi_, st, a, loc) { .id = i, .label = l, .keywords = k, .help = h, .kind = CV_CMD_FLOAT, .p = &(ptr), .lo = lo_, .hi = hi_, .step = st, .act = a, loc }
#define ACT(i, l, k, h, fn, loc)                  { .id = i, .label = l, .keywords = k, .help = h, .kind = CV_CMD_ACTION, .act = fn, loc }
#define GO(i, l, k, h, loc)                       { .id = i, .label = l, .keywords = k, .help = h, .kind = CV_CMD_GOTO, loc }

/* ---- after-change calls and setters the panels share ------------------------------------ */
static void a_groups(void) { app_groups_changed(); }
static void a_sym(void) { app_sym_changed(); }
static void a_see(void) { app_see_refresh(); }
static void a_vec(void) { app_vectors_changed(); }
static void a_ten(void) { app_tensors_changed(); }
static void a_traj(void) { app_traj_changed(); }
static void a_label(void) { app_label_changed(); }
static void a_range(void) { app_refresh_range(); }
static void a_symsize(void) { app_symbol_size(); }
static void a_select(void) { app_select(G.field_name, G.comp); }
static int  g_cmap(void) { return G.cmap < CV_CMAP_N ? G.cmap : 0; }   /* the user's palettes: in the legend's own picker (cmap_combo) */
static int  g_faces(void) { return G.faces_mode; }
static void a_contact(void) { app_contact_refresh(); }
static void a_mesh(void) { mesh_changed(); }
static void a_en(void) { app_en_changed(); }
static void a_hl(void) { deck_refresh_highlight(); }
static int  g_rv(void) { return G.render_view; }
static void s_rv(int v) { app_render_view(v != 0); }
static int  g_axes(void) { return !G.hide_axes; }
static void s_axes(int v) { G.hide_axes = !v; }
static int  g_legend(void) { return !G.hide_legend; }
static void s_legend(int v) { G.hide_legend = !v; }
static int  g_flight(void) { return G.flight; }
static void s_flight(int v) { app_set_flight(v != 0); }
static int  g_free(void) { return G.orbit_free; }
static void s_free(int v) { app_set_orbit_free(v != 0); }
static int  g_upz(void) { return G.up_z; }
static void s_upz(int v) { G.up_z = v == 1; app_view(CV_VIEW_ISO); }
static void s_elem(int v) { app_set_elem_mode(v != 0); }
static int  g_elem(void) { return G.elem_mode; }
static void s_faces(int v) { app_set_faces_mode(v); }
static void s_cmap(int v) { app_colormap(v); }
static void s_light(int v) { app_set_lighting(v); }
static void s_anim(int v) { G.anim_on = v != 0; if (v) G.deform = true; }
static int  g_anim(void) { return G.anim_on; }
static int  g_samples(void) { return G.render_samples <= 16 ? 0 : G.render_samples <= 64 ? 1 : 2; }
static void s_samples(int v) { G.render_samples = v == 0 ? 16 : v == 1 ? 64 : 256; }
static int  g_bgmode(void) { return G.bg_mode; }
static void s_bgmode(int v) { G.bg_mode = v; }
static int  g_symbols(void) { return G.show_bc || G.show_loads || G.show_links || G.show_disc; }
static void s_symbols(int v) { G.show_bc = G.show_loads = G.show_links = G.show_disc = v != 0; }
static int  g_gp(void) { return G.show_gp; }
static void s_gp(int v) { G.show_gp = v != 0; app_gauss_changed(); }
static void s_vec(int v) { G.show_vec = v != 0; app_vectors_changed(); }
static int  g_vec(void) { return G.show_vec; }
static void s_ten(int v) { G.show_tensor = v != 0; app_tensors_changed(); }
static int  g_ten(void) { return G.show_tensor; }
static void s_traj(int v) { G.show_traj = v != 0; app_traj_changed(); }
static int  g_traj(void) { return G.show_traj; }
static void s_compare(int v) { if (v) app_open_dialog(CV_DLG_COMPARE); else { app_compare_close(); app_select(G.field_name, G.comp); } }
static int  g_compare(void) { return G.cmp_on; }
static int  g_cclr(void) { return G.legend_reverse; }
static void s_rev(int v) { G.legend_reverse = v != 0; app_colormap(G.cmap); }
static int  g_grey(void) { return G.legend_grey; }
static void s_grey(int v) { G.legend_grey = v != 0; app_colormap(G.cmap); }
static void s_lock(int v) { G.range_lock = v != 0; if (!v) app_refresh_range(); }
static int  g_lock(void) { return G.range_lock; }
static void s_zero(int v) { G.center_zero = v != 0; if (v) G.range_lock = false; app_refresh_range(); }
static int  g_zero(void) { return G.center_zero; }
static void s_sym0(int v) { G.sym[0] = v != 0; app_sym_toggled(0); }
static void s_sym1(int v) { G.sym[1] = v != 0; app_sym_toggled(1); }
static void s_sym2(int v) { G.sym[2] = v != 0; app_sym_toggled(2); }
static int  g_sym0(void) { return G.sym[0]; }
static int  g_sym1(void) { return G.sym[1]; }
static int  g_sym2(void) { return G.sym[2]; }
#define LBL(K) static int g_l_##K(void) { return app_label_on(CV_LABEL_##K); } static void s_l_##K(int v) { app_label_set(CV_LABEL_##K, v != 0); }
LBL(NODE) LBL(ELEM) LBL(VALUE) LBL(EVALUE) LBL(SETS) LBL(LINKS) LBL(LOADS) LBL(SUPPORTS) LBL(MATERIALS) LBL(GPVALUE) LBL(GPID) LBL(MINMAX)
#define LBLE(i, K, l, kw, h) BOOLG(i, l, kw, h, g_l_##K, s_l_##K, LBLS)

/* actions */
static void x_open(void) { app_open_dialog(CV_DLG_MODEL); }
static void x_reload(void) { app_reload(); }
static void x_fit(void) { app_view_push(); app_fit(); }
static void x_iso(void) { app_view(CV_VIEW_ISO); }
static void x_px(void) { app_view(CV_VIEW_PX); }
static void x_nx(void) { app_view(CV_VIEW_NX); }
static void x_py(void) { app_view(CV_VIEW_PY); }
static void x_ny(void) { app_view(CV_VIEW_NY); }
static void x_pz(void) { app_view(CV_VIEW_PZ); }
static void x_nz(void) { app_view(CV_VIEW_NZ); }
static void x_vback(void) { app_view_undo(-1); }
static void x_vfwd(void) { app_view_undo(+1); }
static void x_true(void) { G.deform_auto = false; G.deform_scale = 1.f; G.deform = true; }
static void x_auto(void) { G.deform_auto = true; G.deform_scale = G.auto_scale; G.deform = true; }
static void x_png(void) { app_export_png(); }
static void x_anim(void) { app_export_animation(); }
static void x_csv(void) { app_export_data(false); }
static void x_vtk(void) { app_export_data(true); }
static void x_report(void) { app_export_report(NULL, false); }
static void x_report_pdf(void) { app_export_report(NULL, true); }
static void x_report_builder(void) { G.show_report = !G.show_report; }
static void x_render(void) { app_render_start(G.render_samples, NULL); }
static void x_mats(void) { app_show_materials(); }
static void x_prev(void) { app_set_step(G.step - 1); }
static void x_next(void) { app_set_step(G.step + 1); }
static void x_play(void) { G.playing = !G.playing; G.last_tick = 0; }
static void x_stl(void) { app_open_dialog(CV_DLG_STL); }
static void x_zin(void) { ui_zoom(+1); }
static void x_zout(void) { ui_zoom(-1); }
static void x_z100(void) { ui_zoom(0); }
static void x_meas(void) { G.show_measure = !G.show_measure; }
static void x_sel(void) { G.show_select = !G.show_select; }
static void x_settings(void) { G.show_settings = !G.show_settings; }
static void x_title(void) { G.title_on = G.title_edit = true; }
static void x_units(void) { G.show_units = !G.show_units; }
static void x_msgs(void) { G.show_msgs = !G.show_msgs; }
static void x_about(void) { G.show_about = !G.show_about; }
static void x_find(void) { G.find_open = true; }
static void x_lset(void) { G.legend_edit = true; }
static void x_mesh(void) { G.show_mesh = !G.show_mesh; }
static void x_fail(void) { G.show_fail = !G.show_fail; }
static void x_cpal(void) { G.show_cpal = !G.show_cpal; }
static void x_formula(void) { G.show_calc_help = !G.show_calc_help; }
static void x_conv(void) { G.show_conv = !G.show_conv; }
static void x_deck(void) { G.show_deck = !G.show_deck; }
static void x_cmore(void) { G.show_contact = !G.show_contact; }
static void x_forget(void) { if (app_sidecar_on()) app_sidecar_forget(); }
static void x_lreset(void) { G.legend_pos.set = G.gizmo_pos.set = false; }
static void x_boxmax(void) { G.box_arm = !G.box_arm; }
static void x_lmoved(void) { app_label_moved_clear(); }
static void x_nopick(void) { app_pick_cancel(); }

static const char* const it_light[]  = { "flat", "headlight", "studio" };
static const char* const it_samples[] = { "16", "64", "256" };
static const char* const it_axis[]   = { "X", "Y", "Z" };
static const char* const it_up[]     = { "Y", "Z" };
static const char* const it_anim[]   = { "0..1", "-1..1" };
static const char* const it_fmt[]    = { "auto", "fixed", "scientific" };
static const char* const it_eye[]    = { "no cut", "cuts", "hides elements" };
static const char* const it_exp[]    = { "deformation cycle", "every step" };
static const char* const it_fam[]    = { "S1 (max)", "S3 (min)", "S1 + S3" };
static const char* const it_enavg[]  = { "not averaged", "average scalars", "average components" };
static const char* const it_enside[] = { "both: top and bottom faces", "top on both faces", "bottom on both faces" };
static const char* const it_csys[]   = { "global x y z", "cylindrical about X", "cylindrical about Y", "cylindrical about Z" };
const char* const cv_faces_mode_names[] = { "field", "by type", "by material", "by group", "plain", "by set", "by section" };

const cv_cmd cv_cmds[] = {
    /* ---- Layers ---- */
    BOOLP("show_faces", "Faces", "surface skin solid shaded", "The model's faces", G.show_faces, a_groups, LAY("Faces and edges")),
    ENUMG("faces_mode", "Colour faces by", "colour by material group type set section field plain", "What colours the faces: the field, the element types, materials, groups, sets, sections, or one plain colour",
          cv_faces_mode_names, g_faces, s_faces, LAY("Faces and edges")),
    FLT("model_alpha", "Opacity", "transparency transparent see-through alpha", "Opacity of the faces: less to see into the model (a contact zone inside an assembly)", G.model_alpha, 0.f, 1.f, 0.05f, a_see, LAY("Faces and edges")),
    BOOLP("mid_faces", "Mid-side nodes", "quadratic second order", "Faces of quadratic elements drawn through their mid-side nodes: colours and shape follow every node", G.mid_faces, a_groups, LAY("Faces and edges")),
    BOOLP("show_edges", "Edges", "mesh lines wireframe element edges", "Edges of the exterior faces", G.show_edges, NULL, LAY("Faces and edges")),
    BOOLP("edges_field", "Edges coloured", "mesh lines paint field", "Paint the field on the edges too (else dark lines)", G.edges_field, NULL, LAY("Faces and edges")),
    BOOLP("show_outline", "Outline", "silhouette feature edges crease border", "Part outline: borders, creases sharper than the angle, material and type changes", G.show_outline, NULL, LAY("Faces and edges")),
    FLT("outline_angle", "Crease angle", "outline feature edge sharp", "Faces meeting at more than this many degrees make a crease", G.outline_angle, 1.f, 180.f, 5.f, a_groups, LAY("Faces and edges")),
    BOOLP("edges_auto", "Hide edges when dense", "fine mesh black zoom", "Hide the edges, nodes and Gauss points while the mesh is denser than ~3 px on screen", G.edges_auto, NULL, LAY("Faces and edges")),
    BOOLP("show_nodes", "Nodes", "points balls vertices", "Nodes of the visible elements, as small balls", G.show_nodes, NULL, LAY("Nodes and points")),
    BOOLP("nodes_field", "Nodes coloured", "points paint field", "Paint the field on the nodes too", G.nodes_field, NULL, LAY("Nodes and points")),
    FLT("point_size", "Node point size", "ball radius", "Size of the node balls", G.point_size, 1.f, 12.f, 1.f, NULL, LAY("Nodes and points")),
    BOOLG("show_gp", "Gauss points", "integration points", "Integration points: real .dat values when a .dat field is selected, else the nodal field interpolated there", g_gp, s_gp, LAY("Nodes and points")),
    BOOLP("gp_colored", "Gauss points coloured", "integration points paint", "Colour the points by the field", G.gp_colored, NULL, LAY("Nodes and points")),
    FLT("gp_size", "Gauss point size", "integration points radius", "Size of the integration points", G.gp_size, 1.f, 30.f, 1.f, NULL, LAY("Nodes and points")),
    BOOLP("gp_on_top", "Gauss points x-ray", "integration points through faces", "Draw the points through the faces (they sit inside the elements)", G.gp_on_top, NULL, LAY("Nodes and points")),
    BOOLP("show_markers", "Min / max balls", "extremes minimum maximum markers", "Balls at the field's minimum and maximum", G.show_markers, NULL, LAY("Field")),
    BOOLP("show_ghost", "Undeformed", "ghost original shape outline", "The undeformed edges in grey behind the deformed shape", G.show_ghost, NULL, LAY("Field")),
    BOOLG("show_symbols", "Symbols", "supports loads couplings springs boundary conditions", "Every symbol layer at once: supports, loads, couplings, springs", g_symbols, s_symbols, LAY("Deck")),
    BOOLP("show_bc", "Supports", "constraints boundary conditions fixed", "Supports of the step on screen: a cone per held DOF", G.show_bc, NULL, LAY("Deck")),
    BOOLP("show_loads", "Loads", "forces pressure gravity arrows", "Loads of the step on screen: forces, pressures, moments, heat, gravity", G.show_loads, NULL, LAY("Deck")),
    BOOLP("show_disc", "Springs, masses", "dashpot discrete gap", "Springs as zigzags, dashpots as pistons, gaps as facing bars, masses as cubes", G.show_disc, NULL, LAY("Deck")),
    BOOLP("show_links", "Couplings", "rigid body equation mpc kinematic spider", "Rigid bodies, couplings and equations as spiders from the reference node", G.show_links, NULL, LAY("Deck")),
    BOOLG("show_vec", "Vectors / directions", "arrows principal direction", "The field's arrows at the nodes, or the principal directions", g_vec, s_vec, LAY("Field")),
    BOOLG("show_tensor", "Tensors", "glyph ellipsoid stress strain", "The tensor as a glyph at each element's centre", g_ten, s_ten, LAY("Field")),
    BOOLG("show_traj", "Trajectories", "stress lines principal load paths", "Principal stress trajectories: curves along S1 or S3 through the solid", g_traj, s_traj, LAY("Field")),
    BOOLP("show_geo_pts", "Geometry points", "cgx fbd", "Points of the geometry", G.show_geo_pts, NULL, LAY("Geometry")),
    BOOLP("show_geo_crv", "Geometry lines", "cgx fbd curves", "Lines of the geometry", G.show_geo_crv, NULL, LAY("Geometry")),
    BOOLP("show_geo_srf", "Geometry surfaces", "cgx fbd", "Surfaces of the geometry", G.show_geo_srf, NULL, LAY("Geometry")),
    FLT("geo_size", "Geometry point size", "cgx fbd", "Size of the geometry points", G.geo_size, 1.f, 20.f, 1.f, NULL, LAY("Geometry")),
    GO("goto_layers", "Layers", "what is drawn show hide", "What is drawn and how it is coloured", LAY(NULL)),
    /* ---- Groups ---- */
    GO("goto_groups", "Groups", "sets materials element types hide isolate only visibility", "Element types, sets, materials, surfaces and couplings: show, hide, isolate, colour", GRP(NULL)),
    GO("goto_materials_list", "Materials", "material list names", "The materials of the deck, to show or hide", GRP(NULL)),
    GO("goto_sets", "Element and node sets", "sets surfaces highlight", "The sets and surfaces of the deck: tick to highlight", GRP(NULL)),
    GO("goto_imported", "Imported geometry", "stl import file overlay", "Imported STL files drawn with the results: show, opacity, scale, colour", IMP),
    ACT("import_stl", "Import STL...", "stl geometry mesh overlay load", "Import geometry that was not analysed (an STL file) to show with the results", x_stl, IMP),
    BOOLP("show_removed", "Removed elements", "model change", "Elements removed by *MODEL CHANGE", G.show_removed, NULL, LAY("Deck")),
    BOOLP("show_hl", "Set highlights", "highlight sets surfaces", "Deck set and surface highlights", G.show_hl, NULL, GRP(NULL)),
    /* ---- Fields ---- */
    GO("goto_fields", "Fields", "result field displacement stress strain temperature component choose", "The result fields of the file, their components, calculated, failure and mesh quality fields", FLD(NULL, -1)),
    BOOLG("elem_mode", "Per element (flat)", "flat element mean unaveraged discontinuous", "One value per element (mean of its nodes) instead of a smooth nodal field", g_elem, s_elem, FLD(NULL, -1)),
    BOOLP("en_on", "Element-nodal results", "irtype 3 femaster stress strpos strneg shell top bottom per element", "Offer and show results written per element and node (FRD IRTYPE 3: FEMaster's STRESS, STRPOS, STRNEG), each element with its own value", G.en_on, a_select, FLD("Fields", -1)),
    ENUMP("en_avg", "Element-nodal averaging", "elem avg average irtype 3 smooth jump discontinuity scalars components", "Average an element-nodal result at the nodes: not at all (the jumps between elements stay), the scalars, or the components first. Only visible elements of one section and material, solids apart from shells, a shell's normals alike; needs the .inp", G.en_avg, it_enavg, a_en, FLD("Fields", -1)),
    ENUMP("en_side", "Shell side", "shell top bottom both pos neg strpos strneg surface", "Which surface of a shell element-nodal stress shows: both (STRPOS on the face its normal leaves, STRNEG on the reverse), or top or bottom on both faces", G.en_side, it_enside, a_en, FLD("Fields", -1)),
    ENUMP("csys", "Results in", "coordinate system cylindrical spherical local global", "The coordinate system the vectors and tensors are shown in", G.csys, it_csys, a_select, FLD(NULL, -1)),
    BOOLP("diff_mode", "Difference to the comparison", "compare minus delta", "Show this field minus the same field of the comparison run", G.diff_mode, a_select, FLD(NULL, -1)),
    BOOLP("vec_colored", "Arrows coloured", "vectors paint", "Colour the arrows by the selected scalar (else white)", G.vec_colored, NULL, FLD("Fields", -1)),
    FLT("vec_pct", "Arrow size %", "vectors length", "Longest arrow, percent of the model's diagonal", G.vec_pct, 0.2f, 50.f, 0.5f, a_vec, FLD("Fields", -1)),
    ENUMP("tensor_style", "Glyph style", "tensor ellipsoid superquadric cross", "How a tensor glyph is drawn", G.tensor_style, cv_glyph_names, a_ten, FLD("Fields", -1)),
    FLT("tensor_scale", "Glyph size", "tensor", "Size of the largest glyph, times the mean element size", G.tensor_scale, 0.1f, 10.f, 0.1f, a_ten, FLD("Fields", -1)),
    BOOLP("tensor_colored", "Glyphs coloured", "tensor trajectories paint", "Colour the glyphs and trajectories", G.tensor_colored, a_ten, FLD("Fields", -1)),
    ENUMP("traj_which", "Trajectory family", "stress lines S1 S3", "Which principal stress trajectories: S1, S3 or both", G.traj_which, it_fam, a_traj, FLD("Fields", -1)),
    FLT("traj_spacing", "Trajectory spacing", "stress lines density", "Distance between trajectories, times the mean element size", G.traj_spacing, 0.5f, 20.f, 0.5f, a_traj, FLD("Fields", -1)),
    BOOLP("nodal_recovery", "Nodal values from integration points", "extrapolate .dat averaging", "Nodal values extrapolated from the integration points of each element", G.nodal_recovery, NULL, FLD("Fields", -1)),
    FLT("avg_pct", "Averaging threshold %", "nodal recovery smooth jump", "A node shared by elements is averaged where their values differ by at most this share of the range", G.avg_pct, 0.f, 100.f, 1.f, NULL, FLD("Fields", -1)),
    GO("goto_failure", "Failure criterion", "tsai wu hashin puck larc strength composite von mises tresca mohr margin reserve factor", "The failure criterion, from the .frd STRESS in each element's material axes", FLD("Failure", -1)),
    ACT("strength_materials", "Strength materials...", "failure allowables ply composite data", "Strength data, and which deck material uses which", x_fail, FLD("Failure", -1)),
    BOOLP("mesh_deformed", "Mesh quality on the deformed shape", "distortion", "Measure the elements as deformed in the step on screen", G.mesh_deformed, a_mesh, FLD("Mesh quality", -1)),
    ACT("mesh_window", "Mesh quality...", "element quality jacobian aspect ratio skewness warpage", "The mesh at a glance: element types, size, and each measure's range", x_mesh, FLD("Mesh quality", -1)),
    ACT("formula_window", "Formula builder...", "calculator calculated field expression custom", "Calculated fields: write a formula over the result fields", x_formula, FLD("Calculated", -1)),
    GO("goto_contact", "Contact", "contact pairs ties gap penetration slave master", "Draw the contact: the slave nodes against their master faces, the ties", FLD(NULL, CV_TREE_CONTACT)),
    BOOLP("cel_show", "Show contact", "contact pairs ties draw", "Draw the contact: slave nodes against their master faces, the ties' surfaces and nodes", G.cel_show, a_contact, FLD("Contact", CV_TREE_CONTACT)),
    ENUMP("cel_draw", "Contact draw", "contact links status gap solids layer", "How the slave nodes are drawn against their master faces", G.cel_draw, cv_cdraw_names, a_contact, FLD("Contact", CV_TREE_CONTACT)),
    ACT("contact_more", "Contact settings...", "contact true scale tolerance near distance key", "Contact settings: true scale, the colours' ends, tolerance and near distance", x_cmore, FLD("Contact", CV_TREE_CONTACT)),
    /* ---- Labels ---- */
    LBLE("label_node", NODE, "Label: node id", "node numbers ids text", "Every node of the surface: its id"),
    LBLE("label_elem", ELEM, "Label: element id", "element numbers ids text", "Every element with a face on the surface: its id, on that face"),
    LBLE("label_value", VALUE, "Label: node value", "values numbers field text", "The shown field at every surface node, in display units"),
    LBLE("label_evalue", EVALUE, "Label: element value", "values numbers field text", "The per-element value on each element's face"),
    LBLE("label_sets", SETS, "Label: set and surface names", "names text", "The sets and surfaces ticked in Groups: their name at their centre"),
    LBLE("label_links", LINKS, "Label: couplings, rigid bodies", "names text", "Couplings and rigid bodies: their name at the reference node"),
    LBLE("label_loads", LOADS, "Label: loads", "force values text", "Loads: kind and value at the symbol, in the deck's units"),
    LBLE("label_supports", SUPPORTS, "Label: supports", "constraints dof text", "Supports: the held degrees of freedom at the node"),
    LBLE("label_materials", MATERIALS, "Label: materials", "names text", "Materials: the name at the centre of each material's surface"),
    LBLE("label_gpvalue", GPVALUE, "Label: Gauss point value", "integration points values text", "The field's value at every Gauss point drawn"),
    LBLE("label_gpid", GPID, "Label: Gauss point id", "integration points numbers text", "Every Gauss point drawn: element id : point number"),
    LBLE("label_minmax", MINMAX, "Label: min / max", "extremes minimum maximum values text", "The field's smallest and largest values at their node or element"),
    INTP("minmax_n", "Min / max count", "extremes how many labels", "How many of the smallest and of the largest values get a ball and a label", G.minmax_n, 1, 100, 1, a_range, LBLS),
    FLT("label_px", "Label size", "text height font", "Text height in pixels", G.label_px, 6.f, 48.f, 1.f, NULL, LBLS),
    FLT("label_spacing", "Label gap", "overlap declutter spacing", "The gap kept between labels, pixels; 0 shows every one, overlapping", G.label_spacing, 0.f, 400.f, 2.f, NULL, LBLS),
    BOOLP("label_front", "Labels always in front", "occlusion hidden through", "Labels in front of everything, never cut by a face", G.label_front, a_label, LBLS),
    FLT("label_box_a", "Label box opacity", "background", "How solid the box behind the text is; 0 none", G.label_box_rgba[3], 0.f, 1.f, 0.05f, NULL, LBLS),
    BOOLP("label_sel_only", "Labels on the selection only", "box selection", "Only the box selection's nodes and elements", G.label_sel_only, a_label, LBLS),
    BOOLP("label_probe_only", "Labels on the probed element only", "probe", "Only the probed element and its nodes", G.label_probe_only, a_label, LBLS),
    ACT("labels_reset", "Reset moved labels", "labels dragged position", "Every label dragged on the model goes back beside its point", x_lmoved, LBLS),
    ACT("measurements", "Measurements...", "distance angle circle radius ruler dimension", "Distances, angles and circles between nodes, as labels on the model", x_meas, VW("Look from", -1)),
    /* ---- View ---- */
    ACT("view_iso", "Look from: Iso", "isometric reset home default view", "The isometric view (R)", x_iso, VW("Look from", -1)),
    ACT("view_px", "Look from: +X", "right side view", "Look from +X (1)", x_px, VW("Look from", -1)),
    ACT("view_nx", "Look from: -X", "left side view", "Look from -X (2)", x_nx, VW("Look from", -1)),
    ACT("view_py", "Look from: +Y", "back top side view", "Look from +Y (3)", x_py, VW("Look from", -1)),
    ACT("view_ny", "Look from: -Y", "front side view", "Look from -Y (4)", x_ny, VW("Look from", -1)),
    ACT("view_pz", "Look from: +Z", "top plan view", "Look from +Z (5)", x_pz, VW("Look from", -1)),
    ACT("view_nz", "Look from: -Z", "bottom view", "Look from -Z (6)", x_nz, VW("Look from", -1)),
    ACT("view_fit", "Fit", "zoom all extents frame centre", "Scale and centre the model in the view (F)", x_fit, VW("Look from", -1)),
    BOOLP("ortho", "Orthographic", "perspective parallel projection", "Parallel projection: no perspective, distances compare directly", G.cam.ortho, NULL, BAR),
    ACT("view_back", "View back", "undo camera history previous", "The view before the last change (Ctrl+Z)", x_vback, VW("Camera", CV_TREE_CAMERA)),
    ACT("view_forward", "View forward", "redo camera history next", "Forward again (Ctrl+Y)", x_vfwd, VW("Camera", CV_TREE_CAMERA)),
    ENUMG("up_axis", "Up axis", "vertical z y turntable", "Dragging turns the model about this world axis, which stays vertical on screen", it_up, g_upz, s_upz, VW("Camera", CV_TREE_CAMERA)),
    BOOLG("orbit_free", "Free rotation", "turntable trackball orbit", "Free: drag turns the model any way round (trackball); off: the up axis stays vertical", g_free, s_free, VW("Camera", CV_TREE_CAMERA)),
    BOOLP("orbit_cursor", "Rotate about the cursor", "orbit pivot mouse", "Drag rotates about the point of the model you grab", G.orbit_cursor, NULL, SET("Mouse")),
    BOOLP("orbit_box", "Rotate about the model's box", "orbit pivot empty space bounding box near", "Grabbing empty space, the drag rotates about where the cursor meets the box of everything drawn, so it stays next to the model", G.orbit_box, NULL, SET("Mouse")),
    BOOLP("zoom_cursor", "Zoom toward the cursor", "wheel mouse", "The wheel zooms toward the point under the cursor", G.zoom_cursor, NULL, SET("Mouse")),
    BOOLP("wheel_invert", "Invert wheel zoom", "scroll reverse", "Wheel up zooms out instead of in", G.wheel_invert, NULL, SET("Mouse")),
    BOOLP("show_pivot", "Show rotation centre", "pivot cross marker", "While navigating, mark the point the view turns or zooms about", G.show_pivot, NULL, SET("Mouse")),
    BOOLG("flight", "Free flight", "walk fly wasd inside navigate", "Walk through the model: WASD, E/Space up, Q down, Shift fast, drag to look", g_flight, s_flight, VW("Camera", CV_TREE_CAMERA)),
    FLT("fly_speed", "Flight speed", "walk fly", "How fast the eye moves", G.fly_speed, 0.005f, 10.f, 0.05f, NULL, VW("Camera", CV_TREE_CAMERA)),
    ENUMP("fly_clip", "Flight: eye", "walk fly cut section", "What lies just in front of the eye: cut away, or whole elements hidden", G.fly_clip, it_eye, NULL, VW("Camera", CV_TREE_CAMERA)),
    GO("goto_colours", "Colours & legend", "colour map legend range", "Colour map, legend, range, bands, number format", VW("Colours & legend", CV_TREE_COLOURS)),
    ENUMG("cmap", "Colour map", "colormap palette rainbow viridis jet gradient", "Colour map for the field", cv_cmap_names, g_cmap, s_cmap, BAR),
    ACT("palettes", "Colour palettes...", "legend colours palette custom user own bands import export json xml other solver",
        "Your own legend colours, to match another solver's plots: made, edited, imported and exported", x_cpal, VW("Colours & legend", CV_TREE_COLOURS)),
    BOOLG("legend", "Show legend", "colour bar scale key", "The colour legend / group key in the view (also in exports)", g_legend, s_legend, VW("Colours & legend", CV_TREE_COLOURS)),
    BOOLG("range_lock", "Lock range", "min max fixed colour scale limits", "Keep min/max fixed across steps and components", g_lock, s_lock, BAR),
    FLT("rmin", "Range min", "colour scale lower limit", "The lowest value of the colour scale", G.rmin, -1e30f, 1e30f, 1.f, NULL, VW("Colours & legend", CV_TREE_COLOURS)),
    FLT("rmax", "Range max", "colour scale upper limit", "The highest value of the colour scale", G.rmax, -1e30f, 1e30f, 1.f, NULL, VW("Colours & legend", CV_TREE_COLOURS)),
    BOOLG("center_zero", "Centre on zero", "symmetric signed range", "A signed field gets a range symmetric about zero", g_zero, s_zero, VW("Colours & legend", CV_TREE_COLOURS)),
    INTP("bands", "Bands", "contour discrete steps levels smooth", "Discrete colour steps (0 = smooth)", G.bands, 0, 64, 1, NULL, BAR),
    BOOLG("legend_reverse", "Reverse colour map", "flip invert colours", "The colour map the other way round", g_cclr, s_rev, VW("Colours & legend", CV_TREE_COLOURS)),
    BOOLG("legend_grey", "Greyscale", "grayscale black and white print", "Greyscale, for printing", g_grey, s_grey, VW("Colours & legend", CV_TREE_COLOURS)),
    ENUMP("legend_fmt", "Legend numbers", "format decimals scientific fixed", "How the legend's numbers are written", G.legend_fmt, it_fmt, NULL, VW("Colours & legend", CV_TREE_COLOURS)),
    INTP("legend_decimals", "Legend decimals", "digits precision", "Digits after the point", G.legend_decimals, 0, 9, 1, NULL, VW("Colours & legend", CV_TREE_COLOURS)),
    BOOLP("legend_box", "White box behind the legend", "background page", "A white box with a border behind the legend, for a picture on a page", G.legend_box, NULL, VW("Colours & legend", CV_TREE_COLOURS)),
    ACT("legend_settings", "Legend settings...", "legend window range format", "The legend's range, number format, band count in a small window", x_lset, VW("Colours & legend", CV_TREE_COLOURS)),
    ACT("legend_reset", "Reset legend and gizmo positions", "move default corner", "Back to the defaults: legend top-right, gizmo bottom-left", x_lreset, VW("Colours & legend", CV_TREE_COLOURS)),
    ACT("units", "Units...", "length stress force input display convert mm mpa", "Input units (what the model was built in) and display units", x_units, VW("Colours & legend", CV_TREE_COLOURS)),
    ACT("goto_min_max", "Go to max in a box", "maximum find extreme", "Drag a box in the view: the probe goes to the field's max inside it", x_boxmax, VW("Colours & legend", CV_TREE_COLOURS)),
    INTP("max_fps", "FPS limit", "frame rate energy battery", "The most frames a second, to save energy. 0: as the display goes", G.max_fps, 0, 1000, 10, NULL, SET("Display")),
    BOOLG("axes", "Axes gizmo", "triad axes corner", "The axes gizmo in the corner (click its tips to look from there)", g_axes, s_axes, SET("Display")),
    BOOLP("title_on", "Title block", "caption header file name date", "A title block in the view: file, solver, analysis, step, date (also in exports)", G.title_on, NULL, SET("Display")),
    ACT("title_edit", "Title block...", "caption header text lines date", "The title block's lines, free text, its date", x_title, SET("Display")),
    GO("goto_background", "Background colour", "backdrop colour white black grey", "The view's background colour, in exports too", SET("Background")),
    GO("goto_theme", "UI theme", "dark light interface colours style", "Colours of the panels and the legend", SET("Interface")),
    GO("goto_font", "UI font and size", "text size zoom scale hidpi pixel font", "Height of the panel text, the pixel font, the whole interface's size", SET("Interface")),
    ACT("settings", "Settings...", "preferences options gear interface display mouse background", "The Settings window: interface, display, background, mouse, 3D mouse, keys", x_settings, SET(NULL)),
    GO("goto_keys", "Keys and mouse", "help shortcuts keyboard hotkeys controls navigation", "The mouse and keys in words", LOC("Settings", "Keys and mouse", CV_TREE_KEYS, -1)),
    BOOLP("ui_glass", "Glass panels", "transparent translucent glass overlay modern see through", "The 3D view drawn behind the panels too, seen through them", G.ui_glass, NULL, SET("Interface")),
    FLT("ui_glass_alpha", "Glass opacity", "transparent translucent glass overlay modern panel background", "How opaque the glass panels are: 1 solid", G.ui_glass_alpha, 0.2f, 1.f, 0.05f, NULL, SET("Interface")),
    FLT("ui_glass_boxes", "Glass fields", "transparent translucent glass boxes edit fields combo checkbox slider property", "With glass panels: how opaque the fields, combos, check boxes and sliders are", G.ui_glass_boxes, 0.2f, 1.f, 0.05f, NULL, SET("Interface")),
    FLT("ui_glass_buttons", "Glass buttons", "transparent translucent glass buttons tabs menu", "With glass panels: how opaque the buttons are (1 solid)", G.ui_glass_buttons, 0.2f, 1.f, 0.05f, NULL, SET("Interface")),
    FLT("ui_glass_headers", "Glass headers", "transparent translucent glass headers sections tree tab strip", "With glass panels: how opaque the section headers and the tab strip are", G.ui_glass_headers, 0.2f, 1.f, 0.05f, NULL, SET("Interface")),
    BOOLP("ui_float", "Floating panels", "floating cards rounded gap corners modern glass panels", "With glass panels: the panes as floating cards, rounded, a gap round each", G.ui_float, NULL, SET("Interface")),
    BOOLP("ui_palette", "Command palette (Ctrl+K in the middle)", "command palette ctrl k ctrl shift p quick open search popup vs code", "Ctrl+K opens a command palette in the middle of the window; off: Ctrl+K goes to the panel's search box", G.ui_palette, NULL, SET("Interface")),
    BOOLP("ui_rail", "Icon rail (panel tabs as icons)", "icon rail activity bar sidebar tabs icons vs code collapse left panel", "The left panel's tabs as icons at the window's left edge; a click on the open one hides the panel (Ctrl+1..5: the tabs); off: a text strip", G.ui_rail, NULL, SET("Interface")),
    BOOLP("ui_modern", "Modern widgets (switches, segments, chips)", "toggle switches modern widgets segmented control chips slider phone ios android checkbox", "Phone-like widgets: switches for on/off, segments for a few choices, chips for filters, sliders with their value inside (Ctrl+click: type it); off: check boxes, lists and plain sliders", G.ui_modern, NULL, SET("Interface")),
    BOOLP("ui_field_picker", "Field picker (one button, search)", "field picker component breadcrumb search top bar combo", "The top bar's field and component as one button: a list of both with search; off: two lists", G.ui_field_picker, NULL, SET("Interface")),
    ACT("ui_bigger", "UI size: larger", "zoom scale interface ctrl plus", "The whole interface a step larger (Ctrl +)", x_zin, SET("Interface")),
    ACT("ui_smaller", "UI size: smaller", "zoom scale interface ctrl minus", "The whole interface a step smaller (Ctrl -)", x_zout, SET("Interface")),
    ACT("ui_100", "UI size: 100 %", "zoom scale interface reset ctrl 0", "The whole interface back at 100 % (Ctrl 0)", x_z100, SET("Interface")),
    BOOLP("sym_auto", "Symbols: auto size", "supports loads size", "Symbols sized from the model: 2.5 % of its diagonal", G.sym_auto, a_symsize, VW("Symbol sizes", CV_TREE_SYMBOLS)),
    FLT("sym_thick", "Symbols: thickness", "supports loads line width", "Thickness of the symbols' lines, times the default", G.sym_thick, 0.2f, 5.f, 0.05f, a_hl, VW("Symbol sizes", CV_TREE_SYMBOLS)),
    BOOLP("sym_thin", "Thin crowded symbols", "supports loads declutter", "Where supports or loads crowd, draw one of a kind per patch of their own size", G.sym_thin, a_hl, VW("Symbol sizes", CV_TREE_SYMBOLS)),
    FLT("bc_scale", "Supports: size", "symbols", "Supports, springs and masses, times the symbol size", G.bc_scale, 0.1f, 10.f, 0.05f, a_hl, VW("Symbol sizes", CV_TREE_SYMBOLS)),
    FLT("load_scale", "Loads: size", "symbols arrows", "Load arrows, times the symbol size", G.load_scale, 0.1f, 10.f, 0.05f, a_hl, VW("Symbol sizes", CV_TREE_SYMBOLS)),
    FLT("hl_size", "Highlighted sets: size", "balls px node set", "Balls of a highlighted node set or surface, pixels", G.hl_size, 2.f, 40.f, 0.5f, NULL, VW("Symbol sizes", CV_TREE_SYMBOLS)),
    BOOLG("mirror_x", "Mirror X", "symmetry reflect half model quarter", "Draw the model again reflected across a plane normal to X", g_sym0, s_sym0, VW("Copies", CV_TREE_MIRROR)),
    BOOLG("mirror_y", "Mirror Y", "symmetry reflect half model quarter", "Draw the model again reflected across a plane normal to Y", g_sym1, s_sym1, VW("Copies", CV_TREE_MIRROR)),
    BOOLG("mirror_z", "Mirror Z", "symmetry reflect half model quarter", "Draw the model again reflected across a plane normal to Z", g_sym2, s_sym2, VW("Copies", CV_TREE_MIRROR)),
    BOOLP("rep_follow", "Replicate: follow deformation", "copies periodic cell", "Each copy sits one deformed cell edge from the next", G.rep_follow, a_sym, VW("Copies", CV_TREE_MIRROR)),
    BOOLP("rep_x", "Replicate along X", "copies array pattern periodic cell", "Draw the model again in a row of copies along X", G.rep[0], a_sym, VW("Copies", CV_TREE_MIRROR)),
    BOOLP("rep_y", "Replicate along Y", "copies array pattern periodic cell", "Draw the model again in a row of copies along Y", G.rep[1], a_sym, VW("Copies", CV_TREE_MIRROR)),
    BOOLP("rep_z", "Replicate along Z", "copies array pattern periodic cell", "Draw the model again in a row of copies along Z", G.rep[2], a_sym, VW("Copies", CV_TREE_MIRROR)),
    BOOLP("cyc_on", "Cyclic copies", "sectors cyclic symmetry rotate turbine", "Draw the sector again turned about an axis, one copy per sector", G.cyc_on, a_sym, VW("Copies", CV_TREE_MIRROR)),
    ENUMP("cyc_axis", "Cyclic axis", "sectors rotate", "The axis the sectors turn about", G.cyc_axis, it_axis, a_sym, VW("Copies", CV_TREE_MIRROR)),
    INTP("cyc_n", "Cyclic sectors", "full circle copies", "Sectors in the full circle: the copy angle is 360 / n", G.cyc_n, 1, 720, 1, a_sym, VW("Copies", CV_TREE_MIRROR)),
    INTP("cyc_show", "Cyclic sectors drawn", "copies", "Sectors drawn, the model included", G.cyc_show, 1, 720, 1, a_sym, VW("Copies", CV_TREE_MIRROR)),
    BOOLP("ndof_on", "3D mouse", "spacemouse 3dconnexion spacenav ndof", "Read a 3D mouse (SpaceMouse); the cap moves the model like a hand holding it", G.ndof_on, NULL, SET3D),
    FLT("ndof_speed", "3D mouse speed", "spacemouse sensitivity", "Sensitivity, 1 = default", G.ndof_speed, 0.05f, 20.f, 0.05f, NULL, SET3D),
    FLT("ndof_dead", "3D mouse dead zone %", "spacemouse", "Cap travel ignored around rest, in %", G.ndof_dead, 0.f, 30.f, 0.5f, NULL, SET3D),
    BOOLP("ndof_zoom_lift", "3D mouse: zoom by push / pull", "spacemouse", "Zoom with the cap pushed down / pulled up; forward / back then pans up / down", G.ndof_zoom_lift, NULL, SET3D),
    BOOLP("ndof_lock_rot", "3D mouse: lock rotation", "spacemouse", "Twisting the cap does not turn the model", G.ndof_lock_rot, NULL, SET3D),
    BOOLP("ndof_lock_pan", "3D mouse: lock pan and zoom", "spacemouse", "Pushing the cap does not pan or zoom", G.ndof_lock_pan, NULL, SET3D),
    BOOLP("ndof_dominant", "3D mouse: dominant axis only", "spacemouse", "Only the axis moved furthest acts at a time", G.ndof_dominant, NULL, SET3D),
    BOOLP("clip_on", "Clip plane", "cut section slice plane", "Cut the drawing at a plane (the crop box removes whole elements instead)", G.clip_on, NULL, VW("Clip & crop", CV_TREE_CLIP)),
    ENUMP("clip_axis", "Clip plane axis", "cut section normal", "The axis the plane is normal to", G.clip_axis, it_axis, NULL, VW("Clip & crop", CV_TREE_CLIP)),
    BOOLP("clip_flip", "Clip: flip", "cut section other side", "Keep the other side instead", G.clip_flip, NULL, VW("Clip & crop", CV_TREE_CLIP)),
    BOOLP("clip_cap", "Clip: fill the cut", "cut section cap solid", "Fill the cut through solid elements, coloured by the field (off: hollow)", G.clip_cap, NULL, VW("Clip & crop", CV_TREE_CLIP)),
    FLT("clip_pos", "Clip plane position", "cut section slider", "Where the plane sits along the axis, as a share of the model's size", G.clip_pos, 0.f, 1.f, 0.002f, NULL, VW("Clip & crop", CV_TREE_CLIP)),
    BOOLP("crop_on", "Crop box", "cut remove elements region box", "Remove elements whose centre lies outside the box, so the cut shows real element faces", G.crop_on, a_groups, VW("Clip & crop", CV_TREE_CLIP)),
    BOOLG("compare_on", "Compare with another file", "difference diff second result", "Open a second .frd of the same mesh and show fields as the difference", g_compare, s_compare, FIL),
    BOOLP("watch", "Watch file", "reload auto running solver monitor live", "Reload when the file changes on disk (a running solver)", G.watch, NULL, FIL),
    ACT("reload", "Reload", "refresh reopen file", "Reopen the file now, keeping camera, step and field", x_reload, FIL),
    ACT("forget", "Forget post-processing", "sidecar reset clean saved views sets", "Forget what was set up for this model (views, sets, paths, kept lines)", x_forget, FIL),
    /* ---- Rendering ---- */
    BOOLG("render_view", "Render view", "studio realistic shading light v", "On: the studio settings. Off: the plain view (flat or headlight light, the background colour) (V)", g_rv, s_rv, RND(NULL)),
    ENUMG("look_light", "Lighting", "flat headlight studio shading light", "Flat: exact colours. Headlight: light from the eye. Studio: physically based light", it_light, app_lighting, s_light, RND("Light and backdrop")),
    ENUMG("look_backdrop", "Backdrop", "background gradient abaqus ansys vignette white", "The view's background: plain, a gradient, a studio vignette, or white", cv_bg_names, g_bgmode, s_bgmode, RND("Light and backdrop")),
    BOOLP("look_floor", "Floor", "ground plane shadow catcher", "A floor under the model, fading out to the backdrop, with a soft shadow", G.look_floor, NULL, RND("Light and backdrop")),
    BOOLP("look_shadow", "Shadows", "shadow light", "The key light's shadows, from a shadow map drawn every frame", G.look_shadow, NULL, RND("Light and backdrop")),
    FLT("look_exposure", "Exposure", "brightness light intensity", "The light's scale, 1 as tuned", G.look_exposure, 0.5f, 2.f, 0.01f, NULL, RND("Light and backdrop")),
    BOOLP("look_smooth", "Smooth normals", "shading facets smooth", "Average the normals across the skin, sharp edges (over 40 degrees) kept", G.look_smooth, NULL, RND("Light and backdrop")),
    BOOLP("look_ao", "Ambient occlusion", "ao creases corners darken ssao", "Darkens creases and corners from the depth", G.look_ao, NULL, RND("Ambient occlusion")),
    FLT("look_ao_strength", "Occlusion strength", "ao creases", "How dark the creases get", G.look_ao_strength, 0.f, 1.f, 0.01f, NULL, RND("Ambient occlusion")),
    FLT("look_ao_radius", "Occlusion radius", "ao creases", "How far a point occludes, as a share of the model's size", G.look_ao_radius, 0.001f, 0.5f, 0.001f, NULL, RND("Ambient occlusion")),
    BOOLP("look_ao_moving", "Occlusion while moving", "ao", "Keep the occlusion while the view moves", G.look_ao_moving, NULL, RND("Ambient occlusion")),
    ENUMG("render_samples", "Render samples", "quality image still anti-aliasing", "Samples per image: more is smoother and slower", it_samples, g_samples, s_samples, RND("Image")),
    ACT("render_image", "Render image", "still picture ray raytrace photo high quality", "A still by accumulating samples, saved as a PNG next to the model", x_render, RND("Image")),
    ACT("show_materials", "Show materials", "colour by material look metal plastic", "Colour the faces by material, lit by the studio: each material takes its look", x_mats, RND("Materials")),
    GO("goto_looks", "Material looks", "metal steel plastic glass rubber roughness", "The look of each deck material", RND("Materials")),
    /* ---- Export ---- */
    ACT("export_png", "Export image", "screenshot picture png save capture", "The 3D view with legend and axes as <model>_stepN.png beside the model (Ctrl+E)", x_png, EXP("Picture")),
    BOOLP("png_alpha", "Transparent PNG background", "alpha cutout picture", "The PNG with a transparent background: only the model, legend and axes on the page", G.png_alpha, NULL, EXP("Picture")),
    ENUMP("exp_kind", "Animation: what loops", "video movie deformation cycle every step", "A deformation cycle, or every increment once", G.exp_kind, it_exp, NULL, EXP("Animation")),
    BOOLP("exp_video", "Animation: one video file", "mp4 gif movie png sequence frames", "One video file (MP4 or GIF) instead of a PNG sequence", G.exp_video, NULL, EXP("Animation")),
    BOOLP("exp_gif", "Animation: GIF", "animated gif mp4 movie", "The video file as an animated GIF instead of MP4", G.exp_gif, NULL, EXP("Animation")),
    INTP("exp_cycles", "Animation cycles", "video loop repeat", "How often the swing repeats in the clip", G.exp_cycles, 1, 20, 1, NULL, EXP("Animation")),
    INTP("exp_fps", "Animation fps", "video frame rate", "Frames per second of the video or GIF", G.exp_fps, 1, 60, 1, NULL, EXP("Animation")),
    BOOLP("exp_lock_range", "Lock colour range while recording", "video legend fixed", "Keep the legend's range fixed while recording", G.exp_lock_range, NULL, EXP("Animation")),
    ACT("export_animation", "Export animation", "video mp4 gif movie record frames", "Export the animation as set above", x_anim, EXP("Animation")),
    ACT("export_csv", "Export CSV nodes", "data table spreadsheet excel values", "id, x, y, z, displacement and the field per node, visible elements only", x_csv, EXP("Data")),
    ACT("export_vtk", "Export VTK mesh", "paraview data mesh", "Legacy VTK unstructured grid with the field, for ParaView", x_vtk, EXP("Data")),
    ACT("export_report", "Export report", "markdown summary model definition document", "The model definition as a Markdown report to <model>_report.md", x_report, EXP("Data")),
    ACT("export_report_pdf", "Export report as PDF", "pdf print summary model definition document", "The model definition as a PDF report to <model>_report.pdf", x_report_pdf, EXP("Data")),
    ACT("report_builder", "Report builder", "report pdf markdown pictures views isometric contents choose document", "Pick the report's parts and pictures of the view (isometric, front, top, right), then create it as PDF or Markdown", x_report_builder, EXP("Data")),
    GO("goto_view_state", "Save / load view state", "camera settings ini restore", "Camera, step, field, layers, clip and crop to <model>_view.ini", EXP("Data")),
    /* ---- Top bar and keys ---- */
    BOOLP("deform", "Deform", "displacement shape displaced", "Draw the shape displaced by DISP x scale", G.deform, NULL, BAR),
    ACT("deform_true", "Deformation 1:1", "true scale displacement", "True scale: the displacement as computed (scale 1)", x_true, BAR),
    ACT("deform_auto", "Deformation auto scale", "automatic scale exaggerate", "Scale so the largest displacement is ~10% of the model", x_auto, BAR),
    FLT("deform_scale", "Deformation scale", "displacement multiplier exaggerate magnify", "Displacement multiplier", G.deform_scale, 0.f, 1e6f, 1.f, NULL, BAR),
    BOOLG("animate", "Animate", "swing oscillate mode shape movie", "Swing the deformation of this increment over time", g_anim, s_anim, BAR),
    ENUMP("anim_mode", "Animation swing", "0..1 -1..1 mode shape", "0..1: grow and relax. -1..1: swing both ways (mode shapes)", G.anim_mode, it_anim, NULL, BAR),
    FLT("anim_period", "Animation seconds per cycle", "speed period", "Seconds per animation cycle", G.anim_period, 0.2f, 20.f, 0.25f, NULL, BAR),
    ACT("step_prev", "Previous step", "increment back time frame", "Step back one increment", x_prev, BAR),
    ACT("step_next", "Next step", "increment forward time frame", "Step forward one increment (arrows)", x_next, BAR),
    ACT("play", "Play / pause", "animate steps increments space", "Step through the increments (Space)", x_play, BAR),
    FLT("play_fps", "Play speed", "fps increments per second", "Increments per second while playing", G.fps, 0.1f, 60.f, 0.1f, NULL, BAR),
    ACT("selection", "Selection...", "select box pick nodes elements add remove intersect", "Select elements and nodes: box, click, by set or id, add / remove / intersect, invert (S)", x_sel, BAR),
    ACT("select_cancel", "Cancel the pick", "escape abort", "Cancel what the next click was going to do (Esc)", x_nopick, BAR),
    ACT("open_file", "Open file...", "load model frd inp fbd dat browse", "Open a .frd / .inp / .fbd / .dat / .stl (Ctrl+O)", x_open, BAR),
    ACT("find", "Find node or element...", "search id locate go to ctrl f", "Find a node or element by its id (Ctrl+F)", x_find, BAR),
    ACT("messages", "Messages", "warnings errors log problems", "The messages and warnings of the load", x_msgs, BAR),
    ACT("convergence", "Convergence...", "solver sta iterations residual", "The solver's convergence history", x_conv, BAR),
    ACT("deck_window", "Deck...", "steps amplitudes input inp model changes", "The deck's steps, amplitudes, model changes", x_deck, BAR),
    ACT("about", "About", "version licence credits", "Who made ccxview, its licence, and the libraries it is built with", x_about, BAR),
    BOOLP("tips", "Tooltips", "help hints hover", "Tooltips over the controls", G.tips, NULL, BAR),
    BOOLP("hide_panels", "View only (hide panels)", "screenshot clean full screen h", "The 3D view alone, for screenshots (H)", G.hide_panels, NULL, BAR),
    BOOLP("probe_tag", "Probe tag", "pick value label tooltip t", "The probe's tag in the view: what and its value (T)", G.probe_tag, NULL, BAR),
    BOOLP("scene_on", "Left panel", "sidebar pane hide show bracket", "The Scene pane on the left ([)", G.scene_on, NULL, BAR),
    BOOLP("insp_on", "Inspector", "right pane hide show bracket", "The Inspector on the right (])", G.insp_on, NULL, BAR),
};
const int cv_cmds_n = (int)CV_COUNT(cv_cmds);
