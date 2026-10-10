/* app_settings.c -- what survives a restart: window size, UI zoom and theme,
   the layer, colour, camera and export choices, which panel sections are open,
   the last folder and the recent files. One INI (see cfg.h) beside the
   executable, written in sections with a comment header each. Every key has a
   default, so a missing file or a hand-edited one just falls back; a value out
   of range keeps the default.

   What belongs to one model (camera, mirror, replicate, clip, crop, the chosen
   field, sets, paths, kept lines) is in its <model>.ccxview instead (app_sidecar.c). */
#include <stdarg.h>
#include <math.h>
#include "app_int.h"
#include "web.h"
#include "cfg.h"
#include "ui.h"
#include "quality.h"
#include "app_fail.h"
#include "tbtext.h"

static cv_cfg C;
static bool   loaded;

/* ---- the settings that map one-to-one onto fields of G, in file order -------- */

typedef struct {
    const char* key;       /* the section title for kind '#' */
    char        kind;      /* 'b' bool, 'i' int, 'f' float, 'a' corner anchor (anchor.h), 's' text (hi: its size),
                              't' the title block's text: escaped (tbtext.h), over key, key_2 ... when long, '#' section header */
    void*       p;
    float       lo, hi;    /* accepted range (lo < hi); outside it the default stays */
} setting;

#define SEC(t)            { t, '#', NULL, 0, 0 }
#define B(k)              { #k, 'b', &G.k, 0, 0 }
#define I(k, a, b)        { #k, 'i', &G.k, a, b }
#define F(k, a, b)        { #k, 'f', &G.k, a, b }
#define TREE(name, t)     { "open_" name, 'i', &G.tree[t], 0, 1 }
#define TB(name, k)       { "title_" name, 'b', &G.title_line[k], 0, 0 }
#define TBFREE(i)         { "title_label" #i, 's', G.title_free[i - 1][0], 0, 64 }, { "title_text" #i, 's', G.title_free[i - 1][1], 0, 64 }
#define UNIT(name, q)     { "unit_in_" name, 'i', &G.unit_in[q], -1, 64 }, { "unit_" name, 'i', &G.unit_show[q], -1, 64 }

static const setting S[] = {
    SEC("Layers"),
    B(show_faces), I(faces_mode, 0, FM_N - 1), B(show_edges), B(edges_field), B(edges_auto), B(show_outline), F(outline_angle, 0.1f, 180), B(mid_faces),
    B(show_nodes), B(nodes_field), F(point_size, 0.5f, 64), B(gp_colored), F(gp_size, 0.5f, 256),
    B(show_bc), B(show_loads), F(bc_scale, 0.01f, 100), F(load_scale, 0.01f, 100),
    B(sym_auto), F(sym_size, 0, 1e30f), F(sym_thick, 0.1f, 10), B(sym_thin),
    B(show_disc), B(show_links), B(show_hl), F(hl_size, 0.5f, 64), B(show_removed), F(model_alpha, 0, 1),
    B(cel_show), B(cel_master), B(cel_links), B(cel_front), I(cel_pick, -1, 100000),
    I(cel_mode, 0, CV_CMODE_ALL), B(cel_true), F(cel_gap_max, 0, 1e30f), F(cel_pen_max, 0, 1e30f), F(cel_tol, 0, 1e30f), F(cel_near, 0, 1e30f), I(tie_nodes, 0, 3), B(contact_key), { "ckey_pos", 'a', &G.ckey_pos, 0, 0 },
    B(vec_colored), F(vec_pct, 0.01f, 100),
    I(tensor_style, 0, CV_GLYPH_N - 1), B(tensor_colored), F(tensor_scale, 0.01f, 100),
    I(traj_which, 0, 2), F(traj_spacing, 0.2f, 50), F(geo_size, 0.5f, 64), B(show_markers), B(show_ghost), { "oor_above", 'i', &G.oor_mode[0], 0, 3 }, { "oor_below", 'i', &G.oor_mode[1], 0, 3 },
    { "oor_above_r", 'f', &G.oor_rgb[0][0], 0, 1 }, { "oor_above_g", 'f', &G.oor_rgb[0][1], 0, 1 }, { "oor_above_b", 'f', &G.oor_rgb[0][2], 0, 1 },
    { "oor_below_r", 'f', &G.oor_rgb[1][0], 0, 1 }, { "oor_below_g", 'f', &G.oor_rgb[1][1], 0, 1 }, { "oor_below_b", 'f', &G.oor_rgb[1][2], 0, 1 },
    SEC("Colours and legend"),
    I(cmap, 0, CV_CMAP_N - 1), I(bands, 0, 64), B(center_zero), B(legend_reverse), B(legend_grey),
    I(legend_fmt, 0, 2), I(legend_decimals, 0, 9), B(hide_legend), B(legend_box),
    SEC("Units: the model's system; each quantity in the file (unit_in_, -1: the system's) and shown in (unit_, -1: as input)"),
    I(units, 0, CV_SYS_N - 1),
    UNIT("length", CV_Q_LEN), UNIT("stress", CV_Q_STRESS), UNIT("force", CV_Q_FORCE), UNIT("temperature", CV_Q_TEMP),
    UNIT("strain", CV_Q_STRAIN), UNIT("velocity", CV_Q_VELO), UNIT("acceleration", CV_Q_ACC),
    UNIT("energy_density", CV_Q_ENERGY_D), UNIT("heat_flux", CV_Q_FLUX), UNIT("power", CV_Q_POWER),
    UNIT("energy", CV_Q_ENERGY), UNIT("mass_flow", CV_Q_MASSFLOW), UNIT("volume", CV_Q_VOLUME), UNIT("mass", CV_Q_MASS),
    UNIT("force_per_width", CV_Q_FORCE_LEN), UNIT("moment_per_width", CV_Q_MOMENT_LEN),
    UNIT("area_per_width", CV_Q_AREA_LEN),
    SEC("Failure field: criterion (0 max stress .. 6 LaRC05, 7 von Mises, 8 Tresca, 9 Mohr), shown (0 exposure, 1 RF, 2 FI, 3 mode, 4 angle, 5 fibre, 6 matrix)"),
    I(fail_crit, 0, CV_FC_N - 1), I(fail_out, 0, CV_FO_N - 1),
    SEC("Reinforcement of concrete shells (the REBAR field), in the model's units: fcd and fyd (MPa with N, mm), the cover to the bars' centroid (mm)"),
    F(rebar_fcd, 1e-12f, 1e30f), F(rebar_fyd, 1e-12f, 1e30f), F(rebar_cover, 1e-12f, 1e30f),
    SEC("Mesh quality field: 0 size, 1 shortest edge, 2 longest edge, 3 aspect ratio, 4 scaled Jacobian, 5 Jacobian ratio, 6 skewness, 7 smallest angle, 8 largest angle, 9 warpage, 10 shape factor"),
    I(mesh_q, 0, CV_MQ_N - 1), F(mesh_warn_pct, 0, 100),
    SEC("Labels on the model: the kinds as bits (2 node id, 4 element id, 8 node value, 16 element value, 32 sets, 64 couplings, 128 loads, 256 supports, 512 materials, 1024 Gauss value, 2048 Gauss id), size px, gap px, colours; measurements' labels (meas_show: 0 undeformed -> deformed, 1 undeformed, 2 deformed)"),
    { "label_kinds", 'i', &G.label_kinds, 0, (1 << CV_LABEL_N) - 1 }, I(minmax_n, 1, 100), F(label_px, 6, 48), F(label_spacing, 0, 400), B(label_sel_only), B(label_probe_only), B(label_front),
    { "label_r", 'f', &G.label_rgb[0], 0, 1 }, { "label_g", 'f', &G.label_rgb[1], 0, 1 }, { "label_b", 'f', &G.label_rgb[2], 0, 1 },
    { "label_box_r", 'f', &G.label_box_rgba[0], 0, 1 }, { "label_box_g", 'f', &G.label_box_rgba[1], 0, 1 },
    { "label_box_b", 'f', &G.label_box_rgba[2], 0, 1 }, { "label_box_a", 'f', &G.label_box_rgba[3], 0, 1 },
    I(meas_show, 0, 2),
    SEC("Mesh quality limits, for every element type (0: the usual one per type)"),
    { "mq_lim_aspect", 'f', &G.mq_lim[CV_MQ_ASPECT], 0, 1000 }, { "mq_lim_sjac", 'f', &G.mq_lim[CV_MQ_SJAC], -1, 1 },
    { "mq_lim_jratio", 'f', &G.mq_lim[CV_MQ_JRATIO], 0, 1 }, { "mq_lim_skew", 'f', &G.mq_lim[CV_MQ_SKEW], 0, 1 },
    { "mq_lim_anglemin", 'f', &G.mq_lim[CV_MQ_ANGLE_MIN], 0, 90 }, { "mq_lim_anglemax", 'f', &G.mq_lim[CV_MQ_ANGLE_MAX], 0, 180 },
    { "mq_lim_warp", 'f', &G.mq_lim[CV_MQ_WARP], 0, 90 }, { "mq_lim_shape", 'f', &G.mq_lim[CV_MQ_SHAPE], 0, 1 },
    SEC("Legend and axes gizmo: view corner (tl tr bl br), gap x, gap y; auto = default place"),
    { "legend_pos", 'a', &G.legend_pos, 0, 0 }, { "gizmo_pos", 'a', &G.gizmo_pos, 0, 0 },
    SEC("Title block: on, its lines (1 shown), the date of the result file (0: today), three free lines, its place, "
        "the date's strftime format, the text edited by hand (1) and that text (\\n between lines, in title_text, title_text_2 ...)"),
    B(title_on), TB("heading", CV_TB_TITLE), TB("file", CV_TB_FILE), TB("solver", CV_TB_SOLVER), TB("analysis", CV_TB_ANALYSIS),
    TB("step", CV_TB_STEP), TB("scale", CV_TB_SCALE), TB("units", CV_TB_UNITS), TB("user", CV_TB_USER), TB("date", CV_TB_DATE),
    B(title_file_date), TBFREE(1), TBFREE(2), TBFREE(3), { "title_pos", 'a', &G.title_pos, 0, 0 },
    { "title_date_fmt", 's', G.title_date_fmt, 0, sizeof G.title_date_fmt }, B(title_hand),
    { "title_text", 't', G.title_text, 0, sizeof G.title_text },
    SEC("Camera"),
    B(up_z), B(orbit_free), B(orbit_cursor), B(zoom_cursor), B(wheel_invert), B(show_pivot),
    { "cam_ortho", 'b', &G.cam.ortho, 0, 0 }, F(fly_speed, 0.005f, 10),
    I(fly_clip, 0, 2), F(fly_clip_depth, 0, 0.2f), B(sel_elems), B(sel_nodes), B(sel_visible), B(sel_mark_max), B(sel_mark_min), B(sel_filters),
    { "sel_open_pick", 'b', &G.sel_open[CV_SELG_PICK], 0, 0 }, { "sel_open_names", 'b', &G.sel_open[CV_SELG_NAMES], 0, 0 },
    { "sel_open_use", 'b', &G.sel_open[CV_SELG_USE], 0, 0 }, { "sel_open_named", 'b', &G.sel_open[CV_SELG_NAMED], 0, 0 },
    SEC("Each model's post-processing (views, sets, paths, lines) kept in <model>.ccxview beside it: 1 on, 0 off"),
    B(sidecar),
    SEC("Display"),
    B(shading), B(hide_axes), B(clip_cap),
    { "bgr", 'f', &G.bg[0], 0, 1 }, { "bgg", 'f', &G.bg[1], 0, 1 }, { "bgb", 'f', &G.bg[2], 0, 1 },
    SEC("Deformation and animation"),
    B(deform_auto), I(anim_mode, 0, 1), F(anim_period, 0.2f, 20), F(fps, 0.1f, 60),
    SEC("Export"),
    I(exp_kind, 0, 1), B(exp_video), I(exp_cycles, 1, 20), I(exp_fps, 1, 60), B(exp_lock_range), B(png_alpha),
    SEC("Panel sections: open (1) or closed (0)"),
    TREE("layers", CV_TREE_LAYERS), TREE("groups", CV_TREE_GROUPS), TREE("fields", CV_TREE_FIELDS),
    TREE("view", CV_TREE_VIEW), TREE("camera", CV_TREE_CAMERA), TREE("colours", CV_TREE_COLOURS),
    TREE("display", CV_TREE_DISPLAY), TREE("mirror", CV_TREE_MIRROR), TREE("replicate", CV_TREE_REPLICATE),
    TREE("clip", CV_TREE_CLIP), TREE("file", CV_TREE_FILE), TREE("export", CV_TREE_EXPORT),
    TREE("symbols", CV_TREE_SYMBOLS), TREE("cyclic", CV_TREE_CYCLIC), TREE("labels", CV_TREE_LABELS), TREE("import", CV_TREE_IMPORT),
};
enum { NS = sizeof S / sizeof S[0] };

#undef SEC
#undef B
#undef I
#undef F
#undef TREE
#undef TB
#undef TBFREE
#undef UNIT

/* keys written apart from the table */
static const char* const OTHER[] = { "window_w", "window_h", "ui_zoom", "ui_font", "ui_pixel_font", "ui_theme", "last_dir" };

static bool in_range(const setting* e, float v) { return e->kind == 's' || !(e->lo < e->hi) || (v >= e->lo && v <= e->hi); }

static bool parse_bool(const char* v) {
    return atoi(v) != 0 || !strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on");
}

/* value text -> the field; false when out of range (the field keeps its value) */
static bool set_from_text(const setting* e, const char* v) {
    switch (e->kind) {
    case 'b': *(bool*)e->p = parse_bool(v); return true;
    case 'i': { int x = atoi(v); if (!in_range(e, (float)x)) return false; *(int*)e->p = x; return true; }
    case 'f': { float x = (float)atof(v); if (x != x || !in_range(e, x)) return false; *(float*)e->p = x; return true; }
    case 'a': return cv_anchor_parse(v, (cv_anchor*)e->p, 10);
    case 's': snprintf((char*)e->p, (size_t)e->hi, "%s", v); return true;
    case 't': cv_tb_unescape(v, (char*)e->p, (size_t)e->hi); G.title_hand = true; return true;     /* typed: --opt */
    }
    return false;
}

enum { PIECE = 1000 };     /* a 't' value's piece per key, under the ini's 1024 */

/* a 't' key's pieces joined: key, key_2, key_3 ...; NULL when absent (malloc'd) */
static char* get_pieces(const char* key) {
    const char* v = cv_cfg_get(&C, key, NULL);
    if (!v) return NULL;
    size_t n = strlen(v);
    char* s = malloc(n + 1);
    if (!s) return NULL;
    memcpy(s, v, n + 1);
    for (int k = 2;; k++) {
        char kk[80];
        snprintf(kk, sizeof kk, "%s_%d", key, k);
        const char* p = cv_cfg_get(&C, kk, NULL);
        if (!p) break;
        size_t m = strlen(p);
        char* t = realloc(s, n + m + 1);
        if (!t) break;
        s = t; memcpy(s + n, p, m + 1); n += m;
    }
    return s;
}

static void get_one(const setting* e) {
    if (e->kind == 't') {
        char* v = get_pieces(e->key);
        if (v) cv_tb_unescape(v, (char*)e->p, (size_t)e->hi);
        free(v);
        return;
    }
    const char* v = cv_cfg_get(&C, e->key, NULL);
    if (v) set_from_text(e, v);
}

static void put_one(const setting* e) {
    switch (e->kind) {
    case 'b': cv_cfg_set_bool(&C, e->key, *(bool*)e->p); break;
    case 'i': cv_cfg_set_int(&C, e->key, *(int*)e->p); break;
    case 'f': cv_cfg_set_float(&C, e->key, *(float*)e->p); break;
    case 's': cv_cfg_set(&C, e->key, (const char*)e->p); break;
    case 't': {                     /* escaped, in pieces that the ini neither trims nor cuts */
        size_t cap = 2 * (size_t)e->hi + 8;
        char* t = malloc(cap);
        if (!t) break;
        cv_tb_escape((const char*)e->p, t, cap);
        const char* p = t;
        int k = 1;
        do {
            size_t m = cv_tb_cut(p, PIECE);
            char kk[80], piece[PIECE + 1];
            if (k == 1) snprintf(kk, sizeof kk, "%s", e->key); else snprintf(kk, sizeof kk, "%s_%d", e->key, k);
            snprintf(piece, sizeof piece, "%.*s", (int)m, p);
            cv_cfg_set(&C, kk, piece);
            p += m; k++;
        } while (*p);
        for (;; k++) {              /* the tail of a longer text saved before */
            char kk[80];
            snprintf(kk, sizeof kk, "%s_%d", e->key, k);
            if (!cv_cfg_get(&C, kk, NULL)) break;
            cv_cfg_unset(&C, kk);
        }
        free(t);
        break;
    }
    case 'a': {
        char t[64];
        cv_anchor_format((const cv_anchor*)e->p, t, sizeof t);
        cv_cfg_set(&C, e->key, t);
        break;
    }
    }
}

static const setting* find(const char* key) {
    for (int i = 0; i < NS; i++) if (S[i].kind != '#' && !strcmp(S[i].key, key)) return &S[i];
    return NULL;
}

void settings_load(void) {
    char path[1024];
    if (!cv_cfg_default_path(path, sizeof path)) return;
    loaded = cv_cfg_load(&C, path);
    if (!loaded) return;
    /* glyph sizes as a percent of the model diagonal, before the mesh-based size: dropped */
    cv_cfg_unset(&C, "glyph_pct"); cv_cfg_unset(&C, "bc_pct"); cv_cfg_unset(&C, "load_pct");
    if (cv_cfg_get(&C, "export_open", NULL))             /* before the panel sections were all kept */
        G.tree[CV_TREE_EXPORT] = cv_cfg_get_bool(&C, "export_open", false);
    cv_cfg_unset(&C, "export_open");
    for (int i = 0; i < NS; i++) if (S[i].kind != '#') get_one(&S[i]);
    ui_set_zoom(cv_cfg_get_float(&C, "ui_zoom", 1.f));
    ui_set_font_size(cv_cfg_get_float(&C, "ui_font", ui_get_font_size()));
    ui_set_pixel_font(cv_cfg_get_bool(&C, "ui_pixel_font", false));
    ui_set_theme(cv_cfg_get(&C, "ui_theme", NULL));
    fail_cfg_load(&C);
}

/* the headless --integrate-csv: the unit keys alone (and whether the model's own file
   is read), nothing of the window */
void settings_load_units(void) {
    char path[1024];
    cv_cfg c;
    if (!cv_cfg_default_path(path, sizeof path) || !cv_cfg_load(&c, path)) return;
    cv_cfg keep = C;
    bool was = loaded;
    C = c; loaded = true;
    for (int i = 0; i < NS; i++) if (S[i].kind != '#' && (!strncmp(S[i].key, "unit", 4) || !strcmp(S[i].key, "sidecar"))) get_one(&S[i]);
    cv_cfg_free(&C);
    C = keep; loaded = was;
}

/* --opt key=value: any settings key, applied on top of the file */
bool settings_apply(const char* kv) {
    const char* eq = strchr(kv, '=');
    if (!eq || eq == kv) return false;
    char key[64];
    snprintf(key, sizeof key, "%.*s", (int)CV_MIN(eq - kv, 63), kv);
    const char* val = eq + 1;
    const setting* e = find(key);
    if (e) { set_from_text(e, val); return true; }
    if (!strcmp(key, "ui_zoom")) { ui_set_zoom((float)atof(val)); return true; }
    if (!strcmp(key, "ui_font")) { ui_set_font_size((float)atof(val)); return true; }
    if (!strcmp(key, "ui_pixel_font")) { ui_set_pixel_font(parse_bool(val)); return true; }
    if (!strcmp(key, "ui_theme")) { ui_set_theme(val); return true; }
    if (!strcmp(key, "export_open")) { G.tree[CV_TREE_EXPORT] = parse_bool(val); return true; }
    /* per-model things that are never saved here (the view file has them), but handy on the command line */
    if (!strcmp(key, "rep_follow")) { G.rep_follow = parse_bool(val); return true; }
    if (!strncmp(key, "rep", 3)) {           /* rep0..2, rep_n0..2, rep_gap0..2: replicate along X, Y, Z */
        size_t n = strlen(key);
        int k = key[n - 1] - '0';
        if (k >= 0 && k < 3 && n >= 4) {
            if (!strncmp(key, "rep_n", 5)) { G.rep_n[k] = CV_MAX(2, CV_MIN(atoi(val), 100)); return true; }
            if (!strncmp(key, "rep_gap", 7)) { G.rep_gap[k] = (float)atof(val); return true; }
            if (n == 4) { G.rep[k] = O.rep[k] = parse_bool(val); return true; }
        }
    }
    if (!strcmp(key, "clip_on")) { G.clip_on = parse_bool(val); return true; }
    if (!strcmp(key, "clip_axis")) { G.clip_axis = atoi(val) % 3; return true; }
    if (!strcmp(key, "clip_flip")) { G.clip_flip = parse_bool(val); return true; }
    if (!strcmp(key, "clip_pos")) { G.clip_pos = (float)atof(val); return true; }
    if (!strcmp(key, "watch")) { G.watch = parse_bool(val); return true; }
    if (!strcmp(key, "elem_mode")) { G.elem_mode = parse_bool(val); return true; }
    if (!strcmp(key, "deform")) { G.deform = parse_bool(val); return true; }
    if (!strcmp(key, "deform_scale")) {       /* a fixed scale, shown */
        float x = (float)atof(val);
        if (!(x >= 0) || isinf(x)) return false;
        G.deform_scale = x; G.deform_auto = false; G.deform = x > 0;
        return true;
    }
    if (!strcmp(key, "contact_mode")) {       /* links,status,gap,solids or the bits: the contact display, shown */
        int m = app_cdisp_parse_mode(val);
        if (m < 0) return false;
        G.cel_mode = m; G.cel_show = true;
        return true;
    }
    if (!strcmp(key, "title_edit")) { G.title_edit = parse_bool(val); return true; }     /* its settings window open */
    return false;
}

/* What a model's load sets afresh (the deformation, from the file and its view; the
   cut and the like, from the view): the --opt keys for them applied again after it,
   so the command line wins as it does for the settings. */
void settings_apply_model(void) {
    /* exact keys, and prefixes (rep0 .. rep_gap2, the contact display's cel_*) */
    static const char* const keys[] = { "deform", "deform_scale", "deform_auto", "clip_on", "clip_axis", "clip_flip",
                                        "clip_pos", "elem_mode", "contact_mode" };
    static const char* const pre[] = { "rep", "cel_" };
    for (int i = 0; i < O.nopts; i++) {
        const char* o = O.opts[i];
        const char* eq = strchr(o, '=');
        if (!eq) continue;
        size_t n = (size_t)(eq - o);
        bool hit = false;
        for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) hit |= strlen(keys[k]) == n && !strncmp(o, keys[k], n);
        for (size_t k = 0; k < sizeof pre / sizeof pre[0]; k++) hit |= !strncmp(o, pre[k], strlen(pre[k]));
        if (hit) settings_apply(o);
    }
}

/* the window size is needed before there is a window: read it on its own */
void settings_window_size(int* w, int* h) {
    char path[1024];
    cv_cfg c;
    if (!cv_cfg_default_path(path, sizeof path) || !cv_cfg_load(&c, path)) return;
    int cw = cv_cfg_get_int(&c, "window_w", *w), ch = cv_cfg_get_int(&c, "window_h", *h);
    if (cw >= 400 && cw <= 16384 && ch >= 300 && ch <= 16384) { *w = cw; *h = ch; }
    cv_cfg_free(&c);
}

/* Another ccxview may have opened files since we loaded the file: take its
   recent list from disk, then ours on top, so neither instance loses the other's. */
static void merge_disk_recent(void) {
    cv_cfg d;
    if (!cv_cfg_load(&d, C.path)) return;
    const char* p[CV_CFG_RECENT];
    char mine[CV_CFG_RECENT][1024];
    int n = cv_cfg_recent(&C, p, CV_CFG_RECENT);
    for (int i = 0; i < n; i++) snprintf(mine[i], sizeof mine[i], "%s", p[i]);
    int m = cv_cfg_recent(&d, p, CV_CFG_RECENT);
    for (int i = m - 1; i >= 0; i--) cv_cfg_add_recent(&C, p[i]);    /* oldest first: each goes to the front */
    for (int i = n - 1; i >= 0; i--) cv_cfg_add_recent(&C, mine[i]);
    cv_cfg_free(&d);
}

/* growing text buffer for the file layout */
typedef struct { char* s; size_t n, cap; } sbuf;

static void sb_add(sbuf* b, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[1200];
    int k = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    if ((size_t)k >= sizeof tmp) k = sizeof tmp - 1;
    if (b->n + (size_t)k + 1 > b->cap) {
        size_t cap = CV_MAX(b->cap * 2, b->n + (size_t)k + 1024);
        char* p = realloc(b->s, cap);
        if (!p) return;
        b->s = p; b->cap = cap;
    }
    memcpy(b->s + b->n, tmp, (size_t)k + 1);
    b->n += (size_t)k;
}

/* key_2, key_3 ... of a 't' setting: its owner, else NULL */
static const setting* piece_of(const char* key) {
    for (int i = 0; i < NS; i++) {
        size_t n = strlen(S[i].key);
        if (S[i].kind == 't' && !strncmp(key, S[i].key, n) && key[n] == '_' && atoi(key + n + 1) >= 2) return &S[i];
    }
    return NULL;
}

static bool known_key(const char* key) {
    if (find(key) || piece_of(key)) return true;
    for (size_t i = 0; i < sizeof OTHER / sizeof OTHER[0]; i++) if (!strcmp(OTHER[i], key)) return true;
    return !strncmp(key, "recent", 6) || fail_cfg_key(key);
}

/* a key's line, if it has a value to fill in */
static void sb_key(sbuf* b, const char* key) {
    if (cv_cfg_get(&C, key, NULL)) sb_add(b, "%s = _\n", key);
}

/* The file in sections, the same order every time; a key this version does
   not know (a newer ccxview's, a typo) is kept at the end. Values are filled
   in by cv_cfg_save; "_" is only a placeholder. */
static void write_layout(void) {
    sbuf b = { 0 };
    sb_add(&b, "# ccxview settings, written on exit. Values may be edited by hand;\n"
               "# any key also works for one run on the command line: --opt key=value\n");
    sb_add(&b, "\n# ---- Window\n");
    for (int i = 0; i < 4; i++) sb_key(&b, OTHER[i]);
    for (int i = 0; i < NS; i++) {
        if (S[i].kind == '#') sb_add(&b, "\n# ---- %s\n", S[i].key);
        else sb_key(&b, S[i].key);
        for (int k = 2; S[i].kind == 't'; k++) {
            char kk[80];
            snprintf(kk, sizeof kk, "%s_%d", S[i].key, k);
            if (!cv_cfg_get(&C, kk, NULL)) break;
            sb_key(&b, kk);
        }
    }
    sb_add(&b, "\n# ---- Strength materials (fmatN = name|kind=ud key=value ..., MPa, N/mm, deg)\n"
               "#      and which deck material uses which (fassignN = DECKMAT=name, * for any)\n");
    for (int i = 0; i < C.n; i++) if (!strncmp(C.a[i].key, "fmat", 4) && fail_cfg_key(C.a[i].key)) sb_add(&b, "%s = _\n", C.a[i].key);
    for (int i = 0; i < C.n; i++) if (!strncmp(C.a[i].key, "fassign", 7) && fail_cfg_key(C.a[i].key)) sb_add(&b, "%s = _\n", C.a[i].key);
    sb_add(&b, "\n# ---- Files, the most recent first\n");
    sb_key(&b, "last_dir");
    for (int i = 0; i < CV_CFG_RECENT; i++) { char k[16]; snprintf(k, sizeof k, "recent%d", i); sb_key(&b, k); }
    bool other = false;
    for (int i = 0; i < C.n; i++) {
        if (known_key(C.a[i].key)) continue;
        if (!other) sb_add(&b, "\n# ---- Not used by this version\n");
        other = true;
        sb_add(&b, "%s = _\n", C.a[i].key);
    }
    if (b.s) cv_cfg_set_layout(&C, b.s);
    free(b.s);
}

/* win_w / win_h <= 0: keep the stored window size */
void settings_save(int win_w, int win_h) {
    if (!loaded) return;
    merge_disk_recent();
    for (int i = 0; i < NS; i++) if (S[i].kind != '#') put_one(&S[i]);
    cv_cfg_set_float(&C, "ui_zoom", ui_get_zoom());
    cv_cfg_set_float(&C, "ui_font", ui_get_font_size());
    cv_cfg_set_bool(&C, "ui_pixel_font", ui_get_pixel_font());
    cv_cfg_set(&C, "ui_theme", ui_get_theme());
    if (win_w > 0 && win_h > 0) { cv_cfg_set_int(&C, "window_w", win_w); cv_cfg_set_int(&C, "window_h", win_h); }
    fail_cfg_save(&C);
    write_layout();
    if (cv_cfg_save(&C)) CV_SETTINGS_SAVED(C.path);
}

void settings_add_recent(const char* path) {
    if (!loaded || !path || !path[0]) return;
    char abs[1024];
    if (!cv_abs_path(path, abs, sizeof abs)) snprintf(abs, sizeof abs, "%s", path);
    cv_cfg_add_recent(&C, abs);
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", abs);
    char* s = strrchr(dir, cv_path_sep());
    if (s) { *s = 0; cv_cfg_set(&C, "last_dir", dir); }
    /* written now, not only on a clean exit: closing the console, Ctrl+C or a
       crash would otherwise lose it. Scripted runs leave the file alone. */
    if (!O.shot_path && !O.nopts && !O.ui_test) settings_save(0, 0);
}

int settings_recent(const char** out, int max) { return loaded ? cv_cfg_recent(&C, out, max) : 0; }
const char* settings_last_dir(void) { return loaded ? cv_cfg_get(&C, "last_dir", "") : ""; }
void settings_free(void) { if (loaded) cv_cfg_free(&C); loaded = false; }
