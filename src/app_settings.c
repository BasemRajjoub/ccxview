/* app_settings.c -- what survives a restart: window size, UI zoom, the layer
   and colour choices, the legend's look, the last folder and the recent files.
   One flat INI (see cfg.h); every key has a default, so a missing file or a
   hand-edited one just falls back. */
#include "app_int.h"
#include "web.h"
#include "cfg.h"
#include "ui.h"

static cv_cfg C;
static bool   loaded;

/* the settings that map one-to-one onto fields of G */
#define BOOLS(X) \
    X(show_faces) X(show_edges) X(show_nodes) X(edges_field) X(nodes_field) X(shading) X(center_zero) \
    X(deform_auto) X(hide_legend) X(hide_axes) X(gp_colored) X(vec_colored) X(legend_reverse) X(legend_grey) \
    X(show_markers) X(show_ghost) X(exp_video) X(exp_lock_range)
#define INTS(X)   X(faces_mode) X(cmap) X(bands) X(anim_mode) X(legend_fmt) X(legend_decimals) X(exp_kind) X(exp_cycles) X(exp_fps)
#define FLOATS(X) X(anim_period) X(fps) X(point_size) X(gp_size) X(glyph_pct) X(vec_pct) X(geo_size) X(hl_size)

void settings_load(void) {
    char path[1024];
    if (!cv_cfg_default_path(path, sizeof path)) return;
    loaded = cv_cfg_load(&C, path);
    if (!loaded) return;
#define RB(k) G.k = cv_cfg_get_bool(&C, #k, G.k);
#define RI(k) G.k = cv_cfg_get_int(&C, #k, G.k);
#define RF(k) G.k = cv_cfg_get_float(&C, #k, G.k);
    BOOLS(RB) INTS(RI) FLOATS(RF)
#undef RB
#undef RI
#undef RF
    for (int k = 0; k < 3; k++) {
        char key[8]; snprintf(key, sizeof key, "bg%c", "rgb"[k]);
        G.bg[k] = cv_cfg_get_float(&C, key, G.bg[k]);
    }
    ui_set_zoom(cv_cfg_get_float(&C, "ui_zoom", 1.f));
    ui_set_theme(cv_cfg_get(&C, "ui_theme", NULL));
    /* sanity: a hand-edited file must not leave the viewer unusable */
    if (G.faces_mode < 0 || G.faces_mode >= FM_N) G.faces_mode = FM_FIELD;
    if (G.cmap < 0 || G.cmap >= CV_CMAP_N) G.cmap = CV_CMAP_FAST;
    if (G.bands < 0 || G.bands > 64) G.bands = 12;
    if (!(G.fps > 0) || G.fps > 60) G.fps = 8;
    if (!(G.anim_period > 0)) G.anim_period = 2;
    if (G.legend_fmt < 0 || G.legend_fmt > 2) G.legend_fmt = 0;
    if (G.legend_decimals < 0 || G.legend_decimals > 9) G.legend_decimals = 3;
    if (G.exp_cycles < 1 || G.exp_cycles > 20) G.exp_cycles = 1;
    if (G.exp_fps < 1 || G.exp_fps > 60) G.exp_fps = 30;
    G.exp_kind = G.exp_kind ? 1 : 0;
}

/* --opt key=value: any settings key, applied on top of the file */
bool settings_apply(const char* kv) {
    const char* eq = strchr(kv, '=');
    if (!eq || eq == kv) return false;
    char key[64];
    snprintf(key, sizeof key, "%.*s", (int)CV_MIN(eq - kv, 63), kv);
    const char* val = eq + 1;
    bool known = false;
#define AB(k) if (!strcmp(key, #k)) { G.k = atoi(val) != 0 || !strcmp(val, "true") || !strcmp(val, "yes") || !strcmp(val, "on"); known = true; }
#define AI(k) if (!strcmp(key, #k)) { G.k = atoi(val); known = true; }
#define AF(k) if (!strcmp(key, #k)) { G.k = (float)atof(val); known = true; }
    BOOLS(AB) INTS(AI) FLOATS(AF)
#undef AB
#undef AI
#undef AF
    if (!strcmp(key, "ui_zoom")) { ui_set_zoom((float)atof(val)); known = true; }
    /* per-session things that are never saved, but handy on the command line */
    if (!strcmp(key, "clip_on")) { G.clip_on = atoi(val) != 0; known = true; }
    if (!strcmp(key, "clip_axis")) { G.clip_axis = atoi(val) % 3; known = true; }
    if (!strcmp(key, "clip_flip")) { G.clip_flip = atoi(val) != 0; known = true; }
    if (!strcmp(key, "clip_pos")) { G.clip_pos = (float)atof(val); known = true; }
    if (!strcmp(key, "watch")) { G.watch = atoi(val) != 0; known = true; }
    if (!strcmp(key, "cam_ortho")) { G.cam.ortho = atoi(val) != 0; known = true; }
    if (!strcmp(key, "elem_mode")) { G.elem_mode = atoi(val) != 0; known = true; }
    if (!strcmp(key, "deform")) { G.deform = atoi(val) != 0; known = true; }
    if (!strcmp(key, "export_open")) { G.exp_open = atoi(val) != 0; known = true; }
    return known;
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

void settings_save(int win_w, int win_h) {
    if (!loaded) return;
#define WB(k) cv_cfg_set_bool(&C, #k, G.k);
#define WI(k) cv_cfg_set_int(&C, #k, G.k);
#define WF(k) cv_cfg_set_float(&C, #k, G.k);
    BOOLS(WB) INTS(WI) FLOATS(WF)
#undef WB
#undef WI
#undef WF
    for (int k = 0; k < 3; k++) {
        char key[8]; snprintf(key, sizeof key, "bg%c", "rgb"[k]);
        cv_cfg_set_float(&C, key, G.bg[k]);
    }
    cv_cfg_set_float(&C, "ui_zoom", ui_get_zoom());
    cv_cfg_set(&C, "ui_theme", ui_get_theme());
    if (win_w > 0 && win_h > 0) { cv_cfg_set_int(&C, "window_w", win_w); cv_cfg_set_int(&C, "window_h", win_h); }
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
}

int settings_recent(const char** out, int max) { return loaded ? cv_cfg_recent(&C, out, max) : 0; }
const char* settings_last_dir(void) { return loaded ? cv_cfg_get(&C, "last_dir", "") : ""; }
void settings_free(void) { if (loaded) cv_cfg_free(&C); loaded = false; }
