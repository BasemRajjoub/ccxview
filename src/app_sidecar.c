/* app_sidecar.c -- what was set up for one model, kept beside it in <model>.ccxview
   (model.frd -> model.ccxview; a deck opened alone: beside the .inp): the view
   (camera, step, field, clip, crop, symmetry, layers, colours), units, groups
   switched off, ticked and hidden sets, hand-hidden elements, the path, the kept
   stress classification lines, the named selections, the history node, the
   comparison run, labels and symbols. Restored when the model opens, written a moment after it changes
   (and before another file opens, and at quit). ccxview never writes the
   solver's own files: a .frd may still be being written. The view state files
   (Export > save view state) are the view part alone, with the same keys.

   How a feature adds its state: two functions,
       static void foo_put(cv_cfg* c)         G -> keys, every key each time
       static void foo_get(const cv_cfg* c)   keys -> G, taking what fits this
                                              model (nodes, sets or fields may be
                                              gone) and ignoring the rest
   and one line in PARTS[] at the end; a plain bool / int / float of G is one
   line in PLAIN[] instead. Nothing else: the auto-save compares the text the
   puts make, the load runs the gets in PARTS[] order. Keys are lower case
   with '_'; a value longer than a line goes through cv_cfg_set_long / _get_long. */
#include "app_int.h"
#include "cfg.h"
#include "idlist.h"
#include "web.h"
#include "log.h"
#include "app_fail.h"
#include "quality.h"
#include <math.h>
#include <strings.h>

/* ---- typed reads that never take a value out of range ----------------------------- */

static int geti(const cv_cfg* c, const char* key, int dflt, int lo, int hi) {
    int v = cv_cfg_get_int(c, key, dflt);
    return v < lo || v > hi ? dflt : v;
}

static float getf(const cv_cfg* c, const char* key, float dflt) {
    float v = cv_cfg_get_float(c, key, dflt);
    return isfinite(v) ? v : dflt;
}

/* is name in a ", " separated list, case-insensitive */
static bool in_names(const char* list, const char* name) {
    size_t n = strlen(name);
    for (const char* p = list; p && *p;) {
        while (*p == ',' || *p == ' ') p++;
        const char* e = p;
        while (*e && *e != ',') e++;
        const char* t = e;
        while (t > p && t[-1] == ' ') t--;
        if ((size_t)(t - p) == n && !strncasecmp(p, name, n)) return true;
        p = e;
    }
    return false;
}

/* ---- the view: camera, step, field, clip, crop, symmetry, layers, colours --------
   The keys of the view state files, so old ones still load. */

static void view_put(cv_cfg* c) {
    cv_cfg_set_float(c, "cam_yaw", G.cam.yaw); cv_cfg_set_float(c, "cam_pitch", G.cam.pitch);
    cv_cfg_set_float(c, "cam_dist", G.cam.dist); cv_cfg_set_bool(c, "cam_ortho", G.cam.ortho);
    cv_cfg_set_bool(c, "cam_up_z", G.up_z);           /* yaw and pitch are about this axis */
    cv_cfg_set_bool(c, "cam_free", G.orbit_free);     /* free orbit: the direction and up below rule */
    cv_cfg_set_float(c, "cam_dx", G.cam.fdir.x); cv_cfg_set_float(c, "cam_dy", G.cam.fdir.y); cv_cfg_set_float(c, "cam_dz", G.cam.fdir.z);
    cv_cfg_set_float(c, "cam_ux", G.cam.fup.x); cv_cfg_set_float(c, "cam_uy", G.cam.fup.y); cv_cfg_set_float(c, "cam_uz", G.cam.fup.z);
    cv_cfg_set_float(c, "cam_x", G.cam.target.x); cv_cfg_set_float(c, "cam_y", G.cam.target.y); cv_cfg_set_float(c, "cam_z", G.cam.target.z);
    cv_cfg_set_int(c, "step", G.step + 1);
    cv_cfg_set(c, "field", G.field_src == 0 ? G.field_name : "");
    cv_cfg_set(c, "calc", G.field_src == 2 ? G.calc_expr : "");
    cv_cfg_set(c, "gauss_field", G.field_src == 1 ? G.field_name : "");   /* a .dat field */
    char t[32] = "";
    if (G.field_src == 3) snprintf(t, sizeof t, "%d,%d", G.fail_crit, G.fail_out);
    cv_cfg_set(c, "fail_field", t);                                       /* criterion, output */
    if (G.field_src == 4) snprintf(t, sizeof t, "%d", G.mesh_q); else t[0] = 0;
    cv_cfg_set(c, "mesh_field", t);
    cv_cfg_set_int(c, "comp", G.comp);
    cv_cfg_set_bool(c, "elem_mode", G.elem_mode);
    cv_cfg_set_int(c, "csys", G.csys);
    cv_cfg_set_float(c, "csys_x", G.csys_o[0]); cv_cfg_set_float(c, "csys_y", G.csys_o[1]); cv_cfg_set_float(c, "csys_z", G.csys_o[2]);
    cv_cfg_set_bool(c, "deform", G.deform); cv_cfg_set_float(c, "deform_scale", G.deform_scale);
    cv_cfg_set_bool(c, "deform_auto", G.deform_auto);
    cv_cfg_set_bool(c, "range_lock", G.range_lock);
    if (G.range_lock) { cv_cfg_set_float(c, "rmin", G.rmin); cv_cfg_set_float(c, "rmax", G.rmax); }   /* else they follow the field */
    cv_cfg_set_bool(c, "clip_on", G.clip_on); cv_cfg_set_int(c, "clip_axis", G.clip_axis);
    cv_cfg_set_bool(c, "clip_flip", G.clip_flip); cv_cfg_set_float(c, "clip_pos", G.clip_pos);
    cv_cfg_set_bool(c, "clip_cap", G.clip_cap);
    cv_cfg_set_bool(c, "crop_on", G.crop_on);
    for (int k = 0; k < 3; k++) {
        char key[16];
        snprintf(key, sizeof key, "crop_lo%d", k); cv_cfg_set_float(c, key, G.crop_lo[k]);
        snprintf(key, sizeof key, "crop_hi%d", k); cv_cfg_set_float(c, key, G.crop_hi[k]);
        snprintf(key, sizeof key, "mirror%d", k); cv_cfg_set_bool(c, key, G.sym[k]);
        snprintf(key, sizeof key, "mirror_at%d", k); cv_cfg_set_int(c, key, G.sym_at[k]);
        snprintf(key, sizeof key, "rep%d", k); cv_cfg_set_bool(c, key, G.rep[k]);
        snprintf(key, sizeof key, "rep_n%d", k); cv_cfg_set_int(c, key, G.rep_n[k]);
        snprintf(key, sizeof key, "rep_gap%d", k); cv_cfg_set_float(c, key, G.rep_gap[k]);
        if (k == 0) cv_cfg_set_bool(c, "rep_follow", G.rep_follow);
        snprintf(key, sizeof key, "cyc_o%d", k); cv_cfg_set_float(c, key, G.cyc_o[k]);
    }
    cv_cfg_set_bool(c, "cyc_on", G.cyc_on); cv_cfg_set_int(c, "cyc_n", G.cyc_n);
    cv_cfg_set_int(c, "cyc_show", G.cyc_show); cv_cfg_set_int(c, "cyc_axis", G.cyc_axis);
    const char* layer_keys[] = { "show_faces", "show_edges", "show_nodes", "show_gp", "show_vec", "show_tensor", "show_traj", "show_markers", "show_ghost", "shading" };
    const bool  layer_vals[] = { G.show_faces, G.show_edges, G.show_nodes, G.show_gp, G.show_vec, G.show_tensor, G.show_traj, G.show_markers, G.show_ghost, G.shading };
    for (size_t i = 0; i < CV_COUNT(layer_keys); i++) cv_cfg_set_bool(c, layer_keys[i], layer_vals[i]);
    cv_cfg_set_int(c, "tensor_style", G.tensor_style); cv_cfg_set_int(c, "traj_which", G.traj_which);
    cv_cfg_set_int(c, "faces_mode", G.faces_mode); cv_cfg_set_int(c, "cmap", G.cmap); cv_cfg_set_int(c, "bands", G.bands);
}

static void view_get(const cv_cfg* c) {
    int step = geti(c, "step", G.step + 1, 1, G.frd.n_steps) - 1;
    if (step >= 0 && step < G.frd.n_steps) G.step = step;
    const char* f = cv_cfg_get(c, "field", "");
    if (f[0] && find_field(G.step, f) >= 0) { snprintf(G.field_name, sizeof G.field_name, "%s", f); G.comp = cv_cfg_get_int(c, "comp", G.comp); G.field_src = 0; }
    const char* calc = cv_cfg_get(c, "calc", "");
    if (calc[0]) app_calc_set(calc);
    G.elem_mode = cv_cfg_get_bool(c, "elem_mode", G.elem_mode);
    G.csys = geti(c, "csys", G.csys, 0, 3);
    G.csys_o[0] = getf(c, "csys_x", G.csys_o[0]); G.csys_o[1] = getf(c, "csys_y", G.csys_o[1]); G.csys_o[2] = getf(c, "csys_z", G.csys_o[2]);
    G.deform = cv_cfg_get_bool(c, "deform", G.deform);
    G.deform_scale = getf(c, "deform_scale", G.deform_scale);
    G.deform_auto = cv_cfg_get_bool(c, "deform_auto", false);   /* older views: the scale as saved */
    if (G.deform_auto) G.deform_scale = G.auto_scale;
    G.clip_on = cv_cfg_get_bool(c, "clip_on", G.clip_on); G.clip_axis = geti(c, "clip_axis", G.clip_axis, 0, 2);
    G.clip_flip = cv_cfg_get_bool(c, "clip_flip", G.clip_flip); G.clip_pos = getf(c, "clip_pos", G.clip_pos);
    G.clip_cap = cv_cfg_get_bool(c, "clip_cap", G.clip_cap);
    G.crop_on = cv_cfg_get_bool(c, "crop_on", G.crop_on);
    for (int k = 0; k < 3; k++) {
        char key[16];
        snprintf(key, sizeof key, "crop_lo%d", k); G.crop_lo[k] = getf(c, key, G.crop_lo[k]);
        snprintf(key, sizeof key, "crop_hi%d", k); G.crop_hi[k] = getf(c, key, G.crop_hi[k]);
        snprintf(key, sizeof key, "mirror%d", k); G.sym[k] = cv_cfg_get_bool(c, key, G.sym[k]);
        snprintf(key, sizeof key, "mirror_at%d", k); G.sym_at[k] = geti(c, key, sym_auto(k), 0, CV_SYM_N - 1);
        snprintf(key, sizeof key, "rep%d", k); G.rep[k] = cv_cfg_get_bool(c, key, G.rep[k]);
        snprintf(key, sizeof key, "rep_n%d", k); G.rep_n[k] = CV_MAX(2, CV_MIN(cv_cfg_get_int(c, key, G.rep_n[k]), 100));
        snprintf(key, sizeof key, "rep_gap%d", k); G.rep_gap[k] = getf(c, key, G.rep_gap[k]);
        if (k == 0) G.rep_follow = cv_cfg_get_bool(c, "rep_follow", G.rep_follow);
        snprintf(key, sizeof key, "cyc_o%d", k); G.cyc_o[k] = getf(c, key, G.cyc_o[k]);
    }
    G.cyc_on = cv_cfg_get_bool(c, "cyc_on", G.cyc_on);
    G.cyc_n = CV_MAX(1, CV_MIN(cv_cfg_get_int(c, "cyc_n", G.cyc_n), 720));
    G.cyc_show = CV_MAX(1, CV_MIN(cv_cfg_get_int(c, "cyc_show", G.cyc_show), G.cyc_n));
    G.cyc_axis = geti(c, "cyc_axis", G.cyc_axis, 0, 2);
    G.show_faces = cv_cfg_get_bool(c, "show_faces", G.show_faces); G.show_edges = cv_cfg_get_bool(c, "show_edges", G.show_edges);
    G.show_nodes = cv_cfg_get_bool(c, "show_nodes", G.show_nodes); G.show_gp = cv_cfg_get_bool(c, "show_gp", G.show_gp);
    G.show_vec = cv_cfg_get_bool(c, "show_vec", G.show_vec); G.show_markers = cv_cfg_get_bool(c, "show_markers", G.show_markers);
    G.show_tensor = cv_cfg_get_bool(c, "show_tensor", G.show_tensor);
    G.show_traj = cv_cfg_get_bool(c, "show_traj", G.show_traj);
    G.traj_which = geti(c, "traj_which", G.traj_which, 0, 2);
    G.tensor_style = geti(c, "tensor_style", G.tensor_style, 0, CV_GLYPH_N - 1);
    G.show_ghost = cv_cfg_get_bool(c, "show_ghost", G.show_ghost); G.shading = cv_cfg_get_bool(c, "shading", G.shading);
    G.faces_mode = geti(c, "faces_mode", G.faces_mode, 0, FM_N - 1);
    app_colormap(geti(c, "cmap", G.cmap, 0, CV_CMAP_N - 1));
    G.bands = geti(c, "bands", G.bands, 0, 64);
    app_set_step(G.step);
    const char* gf = cv_cfg_get(c, "gauss_field", "");   /* the fields that are not an .frd one */
    cv_field_desc gd;
    if (gf[0] && gp_desc(gf, &gd)) app_select_src(gf, cv_cfg_get_int(c, "comp", G.comp), 1);
    int crit, out, q;
    if (sscanf(cv_cfg_get(c, "fail_field", ""), "%d,%d", &crit, &out) == 2) app_fail_set(crit, out);
    if (sscanf(cv_cfg_get(c, "mesh_field", ""), "%d", &q) == 1) app_mesh_set(q);
    app_set_faces_mode(G.faces_mode);
    app_groups_changed();
    G.range_lock = cv_cfg_get_bool(c, "range_lock", false);
    if (G.range_lock) { G.rmin = getf(c, "rmin", G.rmin); G.rmax = getf(c, "rmax", G.rmax); }
    G.up_z = cv_cfg_get_bool(c, "cam_up_z", false);   /* older views: Y up */
    G.cam.yaw = getf(c, "cam_yaw", G.cam.yaw); G.cam.pitch = getf(c, "cam_pitch", G.cam.pitch);
    float dist = getf(c, "cam_dist", G.cam.dist);
    if (dist > 0) G.cam.dist = dist;
    G.cam.ortho = cv_cfg_get_bool(c, "cam_ortho", G.cam.ortho);
    G.orbit_free = false;                               /* the angles as saved, then the free orientation over them */
    app_set_orbit_free(true);
    G.orbit_free = cv_cfg_get_bool(c, "cam_free", false);
    if (G.orbit_free) {
        v3 d = v3_make(getf(c, "cam_dx", G.cam.fdir.x), getf(c, "cam_dy", G.cam.fdir.y), getf(c, "cam_dz", G.cam.fdir.z));
        v3 u = v3_make(getf(c, "cam_ux", G.cam.fup.x), getf(c, "cam_uy", G.cam.fup.y), getf(c, "cam_uz", G.cam.fup.z));
        if (v3_dot(d, d) > 0.5f && v3_dot(u, u) > 0.5f) { G.cam.fdir = v3_norm(d); G.cam.fup = v3_norm(u); }
    }
    G.cam.target = v3_make(getf(c, "cam_x", G.cam.target.x), getf(c, "cam_y", G.cam.target.y), getf(c, "cam_z", G.cam.target.z));
    view_bounds();
}

/* ---- the view state file: the view part alone, written into the file as it is ---- */
bool app_view_save(const char* path) {
    if (!G.loaded) return false;
    cv_cfg c;
    cv_cfg_load(&c, path);
    view_put(&c);
    bool ok = cv_cfg_save(&c);
    cv_cfg_free(&c);
    if (ok) CV_EXPORTED(path);
    return ok;
}

bool app_view_load(const char* path) {
    if (!G.loaded) return false;
    cv_cfg c;
    if (!cv_cfg_load(&c, path) || !c.n) { cv_cfg_free(&c); return false; }
    view_get(&c);
    cv_cfg_free(&c);
    return true;
}

/* ---- units: the system and each quantity in the file and shown, by unit name ----- */

static void units_put(cv_cfg* c) {
    cv_cfg_set_int(c, "units", G.units);
    for (int q = 0; q < CV_Q_N; q++) {
        char key[64];
        const cv_unit* u = cv_unit_get(q, G.unit_in[q]);
        snprintf(key, sizeof key, "unit_in_%s", cv_quantity_key(q)); cv_cfg_set(c, key, u ? u->name : "");
        u = cv_unit_get(q, G.unit_show[q]);
        snprintf(key, sizeof key, "unit_%s", cv_quantity_key(q)); cv_cfg_set(c, key, u ? u->name : "");
    }
}

static void units_get(const cv_cfg* c) {
    if (!cv_cfg_get(c, "units", NULL)) return;
    G.units = geti(c, "units", G.units, 0, CV_SYS_N - 1);
    for (int q = 0; q < CV_Q_N; q++) {           /* an unknown name: the system's / as input */
        char key[64];
        snprintf(key, sizeof key, "unit_in_%s", cv_quantity_key(q)); G.unit_in[q] = cv_unit_find(q, cv_cfg_get(c, key, ""));
        snprintf(key, sizeof key, "unit_%s", cv_quantity_key(q)); G.unit_show[q] = cv_unit_find(q, cv_cfg_get(c, key, ""));
    }
    app_units_changed();
}

/* ---- deck sets by name: element sets ticked and hidden, node sets, surfaces ------- */

static void names_put(cv_cfg* c, const char* key, const bool* on, int n, int elem, const cv_inp* d, bool surf) {
    size_t cap = 1, o = 0;
    for (int i = 0; i < n; i++) cap += strlen(surf ? d->surfs[i].name : d->sets[i].name) + 2;
    char* s = malloc(cap);
    if (!s) return;
    s[0] = 0;
    for (int i = 0; i < n; i++) {
        if (!on[i] || (!surf && d->sets[i].is_elem != elem)) continue;
        o += (size_t)snprintf(s + o, cap - o, "%s%s", o ? ", " : "", surf ? d->surfs[i].name : d->sets[i].name);
    }
    cv_cfg_set_long(c, key, s);
    free(s);
}

static void sets_put(cv_cfg* c) {
    const cv_inp* d = deck_get();
    if (!d) return;
    names_put(c, "elsets_on", deck_set_flags(), d->nsets, 1, d, false);
    names_put(c, "elsets_hidden", deck_set_hidden_flags(), d->nsets, 1, d, false);
    names_put(c, "nsets_on", deck_set_flags(), d->nsets, 0, d, false);
    names_put(c, "surfaces_on", deck_surf_flags(), d->nsurfs, 0, d, true);
}

static void names_get(const cv_cfg* c, const char* key, bool* on, int n, int elem, const cv_inp* d, bool surf) {
    char* s = cv_cfg_get_long(c, key);
    if (!s) return;
    for (int i = 0; i < n; i++) {
        if (!surf && d->sets[i].is_elem != elem) continue;
        on[i] = in_names(s, surf ? d->surfs[i].name : d->sets[i].name);
    }
    free(s);
}

static void sets_get(const cv_cfg* c) {
    const cv_inp* d = deck_get();
    if (!d) return;
    names_get(c, "elsets_on", deck_set_flags(), d->nsets, 1, d, false);
    names_get(c, "elsets_hidden", deck_set_hidden_flags(), d->nsets, 1, d, false);
    names_get(c, "nsets_on", deck_set_flags(), d->nsets, 0, d, false);
    names_get(c, "surfaces_on", deck_surf_flags(), d->nsurfs, 0, d, true);
}

/* ---- groups switched off: the values per axis (type code, material, group) ------- */

static const char* const AXIS_KEY[CV_AXIS_N] = { "off_type", "off_material", "off_group" };

static void groups_put(cv_cfg* c) {
    for (int a = 0; a < CV_AXIS_N; a++) {
        const cv_axis* ax = &G.groups.axis[a];
        uint32_t* v = malloc((size_t)CV_MAX(ax->n, 1) * sizeof *v);
        size_t n = 0;
        for (int i = 0; v && i < ax->n; i++) if (!ax->on[i]) v[n++] = ax->value[i];
        char* t = v ? cv_idlist_format(v, n) : NULL;
        if (t) cv_cfg_set_long(c, AXIS_KEY[a], t);
        free(t); free(v);
    }
}

static void groups_get(const cv_cfg* c) {
    for (int a = 0; a < CV_AXIS_N; a++) {
        char* t = cv_cfg_get_long(c, AXIS_KEY[a]);
        cv_idlist l;
        if (!t || !cv_idlist_parse(t, &l)) { free(t); continue; }
        cv_axis* ax = &G.groups.axis[a];
        for (int i = 0; i < ax->n; i++) ax->on[i] = !bsearch(&ax->value[i], l.ids, l.n, sizeof *l.ids, cv_cmp_u32);
        cv_idlist_free(&l);
        free(t);
    }
}

/* ---- elements hidden by hand, as id ranges ---------------------------------------- */

static void hidden_put(cv_cfg* c) {
    uint32_t n = 0;
    for (uint32_t e = 0; G.hide && e < G.frd.n_elems; e++) n += G.hide[e] != 0;
    uint32_t* id = malloc((size_t)CV_MAX(n, 1) * sizeof *id);
    if (!id) return;
    n = 0;
    for (uint32_t e = 0; G.hide && e < G.frd.n_elems; e++) if (G.hide[e]) id[n++] = G.frd.elem_id[e];
    qsort(id, n, sizeof *id, cv_cmp_u32);
    char* t = cv_idlist_format(id, n);
    if (t) cv_cfg_set_long(c, "hidden_elems", t);
    free(t); free(id);
}

static void hidden_get(const cv_cfg* c) {
    char* t = cv_cfg_get_long(c, "hidden_elems");
    cv_idlist l;
    if (!t || !cv_idlist_parse(t, &l)) { free(t); return; }
    free(G.hide); G.hide = NULL;
    for (size_t i = 0; i < l.n; i++) {
        uint32_t e = cv_frd_elem_index(&G.frd, l.ids[i]);
        if (e == UINT32_MAX) continue;                 /* not in this model (any more) */
        if (!G.hide && !(G.hide = calloc(CV_MAX(G.frd.n_elems, 1), 1))) break;
        G.hide[e] = 1;
    }
    cv_idlist_free(&l);
    free(t);
}

/* ---- the comparison run ------------------------------------------------------------ */

static void compare_put(cv_cfg* c) {
    cv_cfg_set(c, "compare", G.cmp_on ? G.cmp_path : "");
    cv_cfg_set_bool(c, "compare_diff", G.cmp_on && G.diff_mode);
}

static void compare_get(const cv_cfg* c) {
    const char* p = cv_cfg_get(c, "compare", "");
    if (!p[0] || cv_file_size(p) == 0) return;
    char path[1024];
    snprintf(path, sizeof path, "%s", p);          /* app_compare_open clears what p may point into */
    bool lock = G.range_lock;                      /* a locked range stays as the view had it */
    float lo = G.rmin, hi = G.rmax;
    if (!app_compare_open(path)) return;
    G.diff_mode = cv_cfg_get_bool(c, "compare_diff", true);
    app_select(G.field_name, G.comp);
    if (lock) { G.range_lock = true; G.rmin = lo; G.rmax = hi; }
}

/* ---- the path (two nodes, or a node and a direction) and the history node -------- */

static const char* const DIR_WORD[5] = { "", "normal", "x", "y", "z" };

static void path_put(cv_cfg* c) {
    cv_cfg_set_bool(c, "path_lin", G.path_lin);
    bool on = G.path_n && G.path_end[0] < G.frd.n_nodes;
    char t[64] = "";
    if (on && G.path_dir) snprintf(t, sizeof t, "%u, %s", G.frd.node_id[G.path_end[0]], DIR_WORD[CV_MIN(G.path_dir, 4)]);
    else if (on && G.path_end[1] < G.frd.n_nodes) snprintf(t, sizeof t, "%u, %u", G.frd.node_id[G.path_end[0]], G.frd.node_id[G.path_end[1]]);
    cv_cfg_set(c, "path", t);                       /* node, node | normal | x | y | z */
    cv_cfg_set_bool(c, "path_surface", on && G.path_surface);
    cv_cfg_set_bool(c, "path_open", on && G.path_open);
    t[0] = 0;
    if (G.hist_open && G.hist_node < G.frd.n_nodes) snprintf(t, sizeof t, "%u", G.frd.node_id[G.hist_node]);
    cv_cfg_set(c, "history", t);                    /* the node, and its element for per-element fields */
    t[0] = 0;
    if (G.hist_open && G.hist_elem < G.frd.n_elems) snprintf(t, sizeof t, "%u", G.frd.elem_id[G.hist_elem]);
    cv_cfg_set(c, "history_elem", t);
}

/* "id" -> node index; "normal", "x", "y", "z" -> *dir; false when neither fits */
static bool node_or_dir(const char* s, uint32_t* node, int* dir) {
    while (*s == ' ') s++;
    for (int k = 1; k < 5; k++) if (!strncasecmp(s, DIR_WORD[k], strlen(DIR_WORD[k]))) { *dir = k; *node = UINT32_MAX; return true; }
    char* e;
    unsigned long id = strtoul(s, &e, 10);
    *dir = 0;
    *node = e != s ? cv_frd_node_index(&G.frd, (uint32_t)id) : UINT32_MAX;
    return *node != UINT32_MAX;
}

static void path_get(const cv_cfg* c) {
    G.path_lin = cv_cfg_get_bool(c, "path_lin", G.path_lin);
    const char* p = cv_cfg_get(c, "path", "");
    const char* comma = strchr(p, ',');
    uint32_t a, b;
    int da, db;
    if (comma && node_or_dir(p, &a, &da) && !da && node_or_dir(comma + 1, &b, &db) && (db || b != a)) {
        G.path_surface = !db && cv_cfg_get_bool(c, "path_surface", false);
        if (db) app_path_ray(a, db);
        else { app_path_start(a); app_path_end(b); }
        G.path_open = G.path_n && cv_cfg_get_bool(c, "path_open", true);
    }
    const char* h = cv_cfg_get(c, "history", ""), *he = cv_cfg_get(c, "history_elem", "");
    uint32_t n = h[0] ? cv_frd_node_index(&G.frd, (uint32_t)strtoul(h, NULL, 10)) : UINT32_MAX;
    uint32_t e = he[0] ? cv_frd_elem_index(&G.frd, (uint32_t)strtoul(he, NULL, 10)) : UINT32_MAX;
    if (n == UINT32_MAX && e != UINT32_MAX && G.frd.eoff[e] < G.frd.eoff[e + 1]) n = G.frd.conn[G.frd.eoff[e]];   /* its first node */
    if (n != UINT32_MAX && (e != UINT32_MAX || !G.elem_mode)) app_hist_open(n, e);
}

/* ---- kept stress classification lines: scl1 = name; ax,ay,az; bx,by,bz; node; node|dir
   The nodes rebuild the line; the points say where it was, for a reader. */

static void scl_put(cv_cfg* c) {
    for (int i = 0; i < G.scl_n; i++) {
        const cv_scl* l = &G.scl[i];
        if (l->a >= G.frd.n_nodes || (!l->dir && l->b >= G.frd.n_nodes)) continue;
        char key[16], nm[32], b[24], v[256];
        snprintf(nm, sizeof nm, "%s", l->name);
        for (char* q = nm; *q; q++) if (*q == ';') *q = ',';
        if (l->dir) snprintf(b, sizeof b, "%s", DIR_WORD[CV_MIN(l->dir, 4)]); else snprintf(b, sizeof b, "%u", G.frd.node_id[l->b]);
        snprintf(v, sizeof v, "%s; %g,%g,%g; %g,%g,%g; %u; %s", nm, l->p[0][0], l->p[0][1], l->p[0][2],
                 l->p[1][0], l->p[1][1], l->p[1][2], G.frd.node_id[l->a], b);
        snprintf(key, sizeof key, "scl%d", i + 1);
        cv_cfg_set(c, key, v);
    }
}

static void scl_get(const cv_cfg* c) {
    app_scl_clear();
    for (int k = 0; k < c->n; k++) {                /* in file order: scl1, scl2 ... */
        const char* key = c->a[k].key;
        if (strncmp(key, "scl", 3) || !key[3] || strspn(key + 3, "0123456789") != strlen(key + 3)) continue;
        char v[1024], *f[5];
        snprintf(v, sizeof v, "%s", c->a[k].val);
        int nf = 0;
        for (char* s = v; nf < 5; nf++) { f[nf] = s; char* e = strchr(s, ';'); if (!e) { nf++; break; } *e = 0; s = e + 1; }
        cv_scl l = { .b = UINT32_MAX };
        int da;
        if (nf < 5 || !node_or_dir(f[3], &l.a, &da) || da || !node_or_dir(f[4], &l.b, &l.dir) || (!l.dir && l.b == l.a)) continue;
        while (*f[0] == ' ') f[0]++;
        snprintf(l.name, sizeof l.name, "%s", f[0]);
        char* e = l.name + strlen(l.name);
        while (e > l.name && e[-1] == ' ') *--e = 0;
        if (sscanf(f[1], "%f,%f,%f", &l.p[0][0], &l.p[0][1], &l.p[0][2]) != 3) memcpy(l.p[0], G.frd.xyz + 3 * (size_t)l.a, sizeof l.p[0]);
        if (sscanf(f[2], "%f,%f,%f", &l.p[1][0], &l.p[1][1], &l.p[1][2]) != 3) memset(l.p[1], 0, sizeof l.p[1]);
        if (!app_scl_add(&l)) break;
    }
}

/* ---- named selections: selection1 = NAME, selection1_elems / _nodes = id ranges ---- */

static void nsel_put(cv_cfg* c) {
    for (int i = 0; i < app_nsel_count(); i++) {
        const cv_namedsel* n = app_nsel_at(i);
        char key[40];
        snprintf(key, sizeof key, "selection%d", i + 1);
        cv_cfg_set(c, key, n->name);
        for (int k = 0; k < 2; k++) {
            char* t = cv_idlist_format(k ? n->nid : n->eid, k ? n->nn : n->ne);
            snprintf(key, sizeof key, "selection%d_%s", i + 1, k ? "nodes" : "elems");
            if (t) cv_cfg_set_long(c, key, t);
            free(t);
        }
    }
}

/* the ids of the list under key that this model has */
static uint32_t* nsel_ids(const cv_cfg* c, const char* key, bool nodes, uint32_t* n) {
    *n = 0;
    char* t = cv_cfg_get_long(c, key);
    cv_idlist l;
    if (!t || !cv_idlist_parse(t, &l)) { free(t); return NULL; }
    free(t);
    uint32_t* r = l.ids;                            /* kept in place: sorted, each once */
    for (size_t i = 0; i < l.n; i++)
        if ((nodes ? cv_frd_node_index(&G.frd, l.ids[i]) : cv_frd_elem_index(&G.frd, l.ids[i])) != UINT32_MAX) r[(*n)++] = l.ids[i];
    return r;
}

static void nsel_get(const cv_cfg* c) {
    app_nsel_clear();
    for (int i = 1; i < 1000; i++) {
        char key[40];
        snprintf(key, sizeof key, "selection%d", i);
        const char* name = cv_cfg_get(c, key, NULL);
        if (!name) break;
        char nm[64];
        snprintf(nm, sizeof nm, "%s", name);
        uint32_t ne, nn;
        snprintf(key, sizeof key, "selection%d_elems", i);
        uint32_t* e = nsel_ids(c, key, false, &ne);
        snprintf(key, sizeof key, "selection%d_nodes", i);
        uint32_t* v = nsel_ids(c, key, true, &nn);
        if (ne || nn) app_nsel_add_ids(nm, e, ne, v, nn);
        free(e); free(v);
    }
}

/* ---- plain values of G: symbols, overlays, legend look, labels, linearization ---- */

typedef struct { const char* key; char kind; void* p; float lo, hi; } plain;
#define PB(k)        { #k, 'b', &G.k, 0, 0 }
#define PI(k, a, b)  { #k, 'i', &G.k, a, b }
#define PF(k, a, b)  { #k, 'f', &G.k, a, b }
static const plain PLAIN[] = {
    PB(show_bc), PB(show_loads), PB(show_disc), PB(show_links), PB(show_hl), PF(hl_size, 0.5f, 64),
    PF(bc_scale, 0.01f, 100), PF(load_scale, 0.01f, 100), PB(sym_auto), PF(sym_size, 0, 1e30f), PF(sym_thick, 0.1f, 10), PB(sym_thin),
    PB(show_outline), PB(edges_field), PB(nodes_field), PF(point_size, 0.5f, 64), PB(gp_colored), PF(gp_size, 0.5f, 256),
    PB(vec_colored), PF(vec_pct, 0.01f, 100), PB(tensor_colored), PF(tensor_scale, 0.01f, 100), PF(traj_spacing, 0.2f, 50),
    PB(center_zero), PB(legend_reverse), PB(legend_grey), PI(legend_fmt, 0, 2), PI(legend_decimals, 0, 9),
    { "oor_above", 'i', &G.oor_mode[0], 0, 3 }, { "oor_below", 'i', &G.oor_mode[1], 0, 3 },
    { "label_kinds", 'i', &G.label_kinds, 0, (1 << CV_LABEL_N) - 1 }, PF(label_px, 6, 48), PF(label_spacing, 0, 400),
    PB(label_sel_only), PB(label_probe_only), PB(label_front), PI(minmax_n, 1, 100),
    { "label_r", 'f', &G.label_rgb[0], 0, 1 }, { "label_g", 'f', &G.label_rgb[1], 0, 1 }, { "label_b", 'f', &G.label_rgb[2], 0, 1 },
    { "label_box_r", 'f', &G.label_box_rgba[0], 0, 1 }, { "label_box_g", 'f', &G.label_box_rgba[1], 0, 1 },
    { "label_box_b", 'f', &G.label_box_rgba[2], 0, 1 }, { "label_box_a", 'f', &G.label_box_rgba[3], 0, 1 },
    PB(lin_asme), PI(lin_q, 0, 8),
};
#undef PB
#undef PI
#undef PF

static void plain_put(cv_cfg* c) {
    for (size_t i = 0; i < CV_COUNT(PLAIN); i++) {
        const plain* e = &PLAIN[i];
        if (e->kind == 'b') cv_cfg_set_bool(c, e->key, *(bool*)e->p);
        else if (e->kind == 'i') cv_cfg_set_int(c, e->key, *(int*)e->p);
        else cv_cfg_set_float(c, e->key, *(float*)e->p);
    }
}

static void plain_get(const cv_cfg* c) {
    for (size_t i = 0; i < CV_COUNT(PLAIN); i++) {
        const plain* e = &PLAIN[i];
        if (e->kind == 'b') *(bool*)e->p = cv_cfg_get_bool(c, e->key, *(bool*)e->p);
        else if (e->kind == 'i') *(int*)e->p = geti(c, e->key, *(int*)e->p, (int)e->lo, (int)e->hi);
        else { float v = getf(c, e->key, *(float*)e->p); if (v >= e->lo && v <= e->hi) *(float*)e->p = v; }
    }
}

/* ---- the parts, in the order they are restored ------------------------------------
   kept: a reload (Reload, Watch file) keeps it in G, so it is not read again then;
   the rest a reload starts afresh (sets, groups, paths ...) and is restored. Sets,
   groups and hidden elements come before the view, whose skin rebuild takes them;
   units before it, as a unit change unlocks the range. */
typedef struct { const char* name; void (*put)(cv_cfg*); void (*get)(const cv_cfg*); bool kept; } part;
static const part PARTS[] = {
    { "sets",    sets_put,    sets_get,    false },
    { "groups",  groups_put,  groups_get,  false },
    { "hidden",  hidden_put,  hidden_get,  false },
    { "plain",   plain_put,   plain_get,   true  },
    { "units",   units_put,   units_get,   true  },
    { "view",    view_put,    view_get,    true  },
    { "compare", compare_put, compare_get, false },
    { "paths",   path_put,    path_get,    false },
    { "scl",     scl_put,     scl_get,     true  },
    { "selections", nsel_put, nsel_get,    false },
};

/* ---- the file ----------------------------------------------------------------------- */

static struct {
    bool  armed;            /* a model is open and its file may be written */
    char  path[1100];
    char* last;             /* the text last written (or read): what "changed" is measured against */
    char* pending;          /* a change seen at the last tick, written when it holds still */
    double t;
    bool  said;             /* "could not write" logged for this file */
} SC;

bool app_sidecar_on(void) {
#ifdef __EMSCRIPTEN__
    return false;           /* the browser has no folder beside the model */
#else
    return G.sidecar && !O.no_sidecar;
#endif
}

bool app_sidecar_path(char* out, size_t n) {
    if (n) out[0] = 0;
    if (!G.path[0]) return false;
#ifdef __EMSCRIPTEN__
    return false;
#else
    const char* base = cv_basename(G.path);
    const char* dot = strrchr(base, '.');
    size_t k = dot && dot > base ? (size_t)(dot - G.path) : strlen(G.path);
    int w = snprintf(out, n, "%.*s.ccxview", (int)k, G.path);
    return w > 0 && (size_t)w < n;
#endif
}

/* every part's keys as text, a "# part" line before each: the file's body and the
   change detector */
static char* serialise(void) {
    char* all = NULL;
    size_t n = 0;
    for (size_t i = 0; i < CV_COUNT(PARTS); i++) {
        cv_cfg c = {0};
        PARTS[i].put(&c);
        char* t = cv_cfg_text(&c);
        cv_cfg_free(&c);
        size_t m = t ? strlen(t) : 0, h = strlen(PARTS[i].name) + 4;
        char* r = t ? realloc(all, n + h + m + 1) : NULL;
        if (!r) { free(t); free(all); return NULL; }
        all = r;
        n += (size_t)snprintf(all + n, h + 1, "\n# %s\n", PARTS[i].name);
        memcpy(all + n, t, m + 1);
        n += m;
        free(t);
    }
    return all;
}

static bool write_text(const char* text) {
    FILE* f = fopen(SC.path, "wb");
    if (!f) {
        if (!SC.said) cv_logf("cannot write %s", SC.path);
        SC.said = true;
        return false;
    }
    fprintf(f, "# ccxview: what was set up for %s (views, sets, paths, lines ...).\n"
               "# Written by ccxview when it changes; delete it to start afresh.\n", cv_basename(G.path));
    fputs(text, f);
    return fclose(f) == 0;
}

bool app_sidecar_save(void) {
    if (!SC.armed || !G.loaded) return false;
    char* t = serialise();
    if (!t) return false;
    bool ok = write_text(t);
    free(SC.last); SC.last = t;
    free(SC.pending); SC.pending = NULL;
    return ok;
}

void app_sidecar_flush(void) {
    if (!SC.armed || !G.loaded) return;
    char* t = serialise();
    if (!t) return;
    if (SC.last && !strcmp(t, SC.last)) { free(t); return; }
    write_text(t);
    free(SC.last); SC.last = t;
    free(SC.pending); SC.pending = NULL;
}

/* Twice a second: the state as text against the last written. A change is written
   once it has held still for one look (a camera being dragged is not written on
   every frame); never while a file loads or the skin is rebuilt. */
void app_sidecar_tick(void) {
    if (!SC.armed || !G.loaded || app_busy()) return;
    double now = cv_now();
    if (now - SC.t < 0.5) return;
    SC.t = now;
    char* t = serialise();
    if (!t) return;
    if (SC.last && !strcmp(t, SC.last)) { free(t); free(SC.pending); SC.pending = NULL; return; }
    if (SC.pending && !strcmp(t, SC.pending)) {
        write_text(t);
        free(SC.last); SC.last = t;
        free(SC.pending); SC.pending = NULL;
        return;
    }
    free(SC.pending); SC.pending = t;
}

void app_sidecar_load(bool reload) {
    if (!reload) {                          /* another model: nothing of the last one's carries over */
        SC.armed = false;
        free(SC.last); SC.last = NULL;
        free(SC.pending); SC.pending = NULL;
        SC.said = false;
        app_scl_clear();
    }
    if (!app_sidecar_on() || !app_sidecar_path(SC.path, sizeof SC.path)) return;
    cv_cfg c = {0};
    if (cv_file_size(SC.path) > 0 && cv_cfg_load(&c, SC.path) && c.n) {
        for (size_t i = 0; i < CV_COUNT(PARTS); i++)
            if (!(reload && PARTS[i].kept)) PARTS[i].get(&c);
        if (!app_busy()) app_groups_changed();     /* the view starts it; a reload does not read the view */
        app_symbol_size();                          /* also the highlights of the sets */
        app_label_changed();
        if (!reload) { snprintf(G.note, sizeof G.note, "restored %s", cv_basename(SC.path)); G.note_t = cv_now(); }
    }
    cv_cfg_free(&c);
    SC.armed = true;
    if (!reload || !SC.last) { free(SC.last); SC.last = serialise(); }
}

void app_sidecar_forget(void) {
    if (!G.loaded) return;
    char p[1100], m[1024];
    bool have = app_sidecar_path(p, sizeof p);
    SC.armed = false;                       /* the reopen must not write it back */
    bool gone = have && remove(p) == 0;
    app_scl_clear();
    snprintf(G.note, sizeof G.note, gone ? "forgot %s: the model opened afresh" : "nothing kept for this model (%s): opened afresh",
             have ? cv_basename(p) : "no file");
    G.note_t = cv_now();
    snprintf(m, sizeof m, "%s", G.path);
    app_open(m);
}
