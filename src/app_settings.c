/* app_settings.c -- what survives a restart: window size, UI zoom and theme,
   the layer, colour, camera and export choices, which panel sections are open,
   the last folder and the recent files. One INI (see cfg.h) beside the
   executable, written in sections with a comment header each. Every key has a
   default, so a missing file or a hand-edited one just falls back; a value out
   of range keeps the default.

   What belongs to one model (camera, mirror, replicate, clip, crop, the chosen
   field) is in its view file instead (app_load.c, View > save view). */
#include <stdarg.h>
#include "app_int.h"
#include "web.h"
#include "cfg.h"
#include "ui.h"

static cv_cfg C;
static bool   loaded;

/* ---- the settings that map one-to-one onto fields of G, in file order -------- */

typedef struct {
    const char* key;       /* the section title for kind '#' */
    char        kind;      /* 'b' bool, 'i' int, 'f' float, '#' section header */
    void*       p;
    float       lo, hi;    /* accepted range (lo < hi); outside it the default stays */
} setting;

#define SEC(t)            { t, '#', NULL, 0, 0 }
#define B(k)              { #k, 'b', &G.k, 0, 0 }
#define I(k, a, b)        { #k, 'i', &G.k, a, b }
#define F(k, a, b)        { #k, 'f', &G.k, a, b }
#define TREE(name, t)     { "open_" name, 'i', &G.tree[t], 0, 1 }

static const setting S[] = {
    SEC("Layers"),
    B(show_faces), I(faces_mode, 0, FM_N - 1), B(show_edges), B(edges_field), B(edges_auto), B(show_outline), F(outline_angle, 0.1f, 180),
    B(show_nodes), B(nodes_field), F(point_size, 0.5f, 64), B(gp_colored), F(gp_size, 0.5f, 256),
    B(show_bc), B(show_loads), F(bc_scale, 0.01f, 100), F(load_scale, 0.01f, 100),
    B(show_disc), B(show_links), B(show_hl), F(hl_size, 0.5f, 64),
    B(vec_colored), F(vec_pct, 0.01f, 100), F(geo_size, 0.5f, 64), B(show_markers), B(show_ghost),
    SEC("Colours and legend"),
    I(cmap, 0, CV_CMAP_N - 1), I(bands, 0, 64), B(center_zero), B(legend_reverse), B(legend_grey),
    I(legend_fmt, 0, 2), I(legend_decimals, 0, 9), B(hide_legend), I(units, 0, CV_UNITS_N - 1),
    SEC("Camera"),
    B(up_z), B(orbit_free), B(orbit_cursor), B(zoom_cursor), B(wheel_invert), B(show_pivot),
    { "cam_ortho", 'b', &G.cam.ortho, 0, 0 }, F(fly_speed, 0.005f, 10),
    SEC("Display"),
    B(shading), B(hide_axes), B(clip_cap),
    { "bgr", 'f', &G.bg[0], 0, 1 }, { "bgg", 'f', &G.bg[1], 0, 1 }, { "bgb", 'f', &G.bg[2], 0, 1 },
    SEC("Deformation and animation"),
    B(deform_auto), I(anim_mode, 0, 1), F(anim_period, 0.2f, 20), F(fps, 0.1f, 60),
    SEC("Export"),
    I(exp_kind, 0, 1), B(exp_video), I(exp_cycles, 1, 20), I(exp_fps, 1, 60), B(exp_lock_range),
    SEC("Panel sections: open (1) or closed (0)"),
    TREE("layers", CV_TREE_LAYERS), TREE("groups", CV_TREE_GROUPS), TREE("fields", CV_TREE_FIELDS),
    TREE("view", CV_TREE_VIEW), TREE("camera", CV_TREE_CAMERA), TREE("colours", CV_TREE_COLOURS),
    TREE("display", CV_TREE_DISPLAY), TREE("mirror", CV_TREE_MIRROR), TREE("replicate", CV_TREE_REPLICATE),
    TREE("clip", CV_TREE_CLIP), TREE("file", CV_TREE_FILE), TREE("export", CV_TREE_EXPORT),
    TREE("symbols", CV_TREE_SYMBOLS), TREE("cyclic", CV_TREE_CYCLIC),
};
enum { NS = sizeof S / sizeof S[0] };

#undef SEC
#undef B
#undef I
#undef F
#undef TREE

/* keys written apart from the table */
static const char* const OTHER[] = { "window_w", "window_h", "ui_zoom", "ui_theme", "last_dir" };

static bool in_range(const setting* e, float v) { return !(e->lo < e->hi) || (v >= e->lo && v <= e->hi); }

static bool parse_bool(const char* v) {
    return atoi(v) != 0 || !strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on");
}

/* value text -> the field; false when out of range (the field keeps its value) */
static bool set_from_text(const setting* e, const char* v) {
    switch (e->kind) {
    case 'b': *(bool*)e->p = parse_bool(v); return true;
    case 'i': { int x = atoi(v); if (!in_range(e, (float)x)) return false; *(int*)e->p = x; return true; }
    case 'f': { float x = (float)atof(v); if (x != x || !in_range(e, x)) return false; *(float*)e->p = x; return true; }
    }
    return false;
}

static void get_one(const setting* e) {
    const char* v = cv_cfg_get(&C, e->key, NULL);
    if (v) set_from_text(e, v);
}

static void put_one(const setting* e) {
    switch (e->kind) {
    case 'b': cv_cfg_set_bool(&C, e->key, *(bool*)e->p); break;
    case 'i': cv_cfg_set_int(&C, e->key, *(int*)e->p); break;
    case 'f': cv_cfg_set_float(&C, e->key, *(float*)e->p); break;
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
    ui_set_theme(cv_cfg_get(&C, "ui_theme", NULL));
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
    return false;
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

static bool known_key(const char* key) {
    if (find(key)) return true;
    for (size_t i = 0; i < sizeof OTHER / sizeof OTHER[0]; i++) if (!strcmp(OTHER[i], key)) return true;
    return !strncmp(key, "recent", 6);
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
    }
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
    cv_cfg_set(&C, "ui_theme", ui_get_theme());
    if (win_w > 0 && win_h > 0) { cv_cfg_set_int(&C, "window_w", win_w); cv_cfg_set_int(&C, "window_h", win_h); }
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
    if (!O.shot_path && !O.nopts) settings_save(0, 0);
}

int settings_recent(const char** out, int max) { return loaded ? cv_cfg_recent(&C, out, max) : 0; }
const char* settings_last_dir(void) { return loaded ? cv_cfg_get(&C, "last_dir", "") : ""; }
void settings_free(void) { if (loaded) cv_cfg_free(&C); loaded = false; }
