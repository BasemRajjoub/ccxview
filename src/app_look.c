/* app_look.c -- the Studio look (render.c STUDIO_FS): its settings per frame, the
   skin's smooth normals kept in step with the shape drawn, and how each deck material
   looks when the faces are coloured by material.

   Normals are recomputed when the skin, the nodes, the displacement or its scale
   change; while the scale is being dragged on a big model they wait until it rests
   (the shader falls back to each triangle's own normal where the smooth one is off by
   much, so the picture stays right meanwhile). During an animation they stay those of
   the full scale.

   A material looks like the preset the user chose for its name (G.look_mats, kept in
   the ini across models: "STEEL=Stainless steel;..."), else like the one look.c guesses
   from its name and constants. */
#include <math.h>
#ifdef _WIN32
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif
#include "app_int.h"
#include "look.h"
#include "nrm.h"
#include "gpu.h"
#include "render_bg.h"
#include "render_shadow.h"

typedef struct { const void* tri; size_t n_tri; const float* xyz; uint32_t nn; const float* disp; uint32_t gen; float scale; } nkey;

static struct {
    nkey   done, pend;
    double pend_t;
    bool   have;
    int8_t* buf;  uint32_t buf_n;
    /* the material classes' looks: per class of the material axis */
    float* rgb;  float* mr;  int n;
    bool   dirty;
} L = { .dirty = true };

static bool key_eq(const nkey* a, const nkey* b) { return !memcmp(a, b, sizeof *a); }

static void sync_normals(void) {
    if (!app_eff_look() || !G.look_smooth || !G.skin.n_tri || !G.frd.n_nodes) {
        if (L.have) { cv_render_normals(NULL, 0); L.have = false; memset(&L.done, 0, sizeof L.done); }
        return;
    }
    nkey k;
    memset(&k, 0, sizeof k);                  /* padding too: compared bytewise */
    k.tri = G.skin.tri; k.n_tri = G.skin.n_tri; k.xyz = G.frd.xyz; k.nn = G.frd.n_nodes;
    k.disp = G.deform ? G.disp : NULL; k.gen = G.disp && G.deform ? G.field_gen : 0;
    k.scale = k.disp ? G.deform_scale : 0.f;
    if (L.have && key_eq(&k, &L.done)) return;
    double now = cv_now();
    if (!key_eq(&k, &L.pend)) { L.pend = k; L.pend_t = now; }
    /* a big model: wait for the scale (or the step) to rest a moment */
    if (L.have && k.n_tri > 400000 && now - L.pend_t < 0.2) { app_wake(); return; }
    if (L.buf_n != k.nn) {
        free(L.buf);
        L.buf = malloc((size_t)k.nn * 4);
        L.buf_n = L.buf ? k.nn : 0;
    }
    if (!L.buf || !cv_nrm_skin(k.xyz, k.disp, k.scale, k.nn, G.skin.tri, G.skin.n_tri, L.buf)) {
        cv_render_normals(NULL, 0); L.have = false; return;
    }
    cv_render_normals(L.buf, k.nn);           /* after cv_render_positions, which drops them */
    L.done = k; L.have = true;
}

/* ---- the deck materials' looks ---------------------------------------------------- */

/* the preset the user chose for material `name`, -1 none */
static int chosen(const char* name) {
    if (!name || !*name) return -1;
    size_t ln = strlen(name);
    for (const char* s = G.look_mats; *s; ) {
        const char* e = strchr(s, ';');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        const char* eq = memchr(s, '=', n);
        if (eq && (size_t)(eq - s) == ln && !strncasecmp(s, name, ln)) {
            char p[64];
            snprintf(p, sizeof p, "%.*s", (int)(n - ln - 1), eq + 1);
            return cv_look_find(p);
        }
        s += n + (e ? 1 : 0);
    }
    return -1;
}

int app_look_preset(uint32_t mat, bool* user, char* why, int n) {
    const char* name = deck_material_name(mat);
    int c = chosen(name);
    if (user) *user = c >= 0;
    if (c >= 0) { if (why && n > 0) snprintf(why, (size_t)n, "chosen"); return c; }
    const cv_inp* d = deck_get();
    const cv_matprop* p = d && d->mprop && mat >= 1 && (int)mat <= d->nmats ? &d->mprop[mat - 1] : NULL;
    return cv_look_guess(name ? name : "", p, G.units, why, n);
}

void app_look_set(uint32_t mat, int preset) {
    const char* name = deck_material_name(mat);
    if (!name || !*name) return;
    char out[sizeof G.look_mats] = "";
    size_t ln = strlen(name), o = 0;
    for (const char* s = G.look_mats; *s; ) {   /* every other entry kept */
        const char* e = strchr(s, ';');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        bool same = n > ln && s[ln] == '=' && !strncasecmp(s, name, ln);
        if (!same && n && o + n + 2 < sizeof out) { memcpy(out + o, s, n); o += n; out[o++] = ';'; }
        s += n + (e ? 1 : 0);
    }
    if (preset >= 0 && preset < CV_LOOK_N)
        o += (size_t)snprintf(out + o, sizeof out - o, "%s=%s;", name, cv_look_mats[preset].name);
    if (o && o < sizeof out && out[o - 1] == ';') out[o - 1] = 0;
    snprintf(G.look_mats, sizeof G.look_mats, "%s", out);
    app_look_changed();
}

static void build_classes(void) {
    L.dirty = false;
    const cv_axis* ax = &G.groups.axis[CV_AXIS_MAT];
    free(L.rgb); free(L.mr); L.rgb = L.mr = NULL; L.n = 0;
    if (ax->n <= 0) return;
    L.rgb = malloc((size_t)ax->n * 3 * sizeof(float));
    L.mr = malloc((size_t)ax->n * 2 * sizeof(float));
    if (!L.rgb || !L.mr) { free(L.rgb); free(L.mr); L.rgb = L.mr = NULL; return; }
    for (int i = 0; i < ax->n; i++) {
        const cv_look_mat* m = &cv_look_mats[app_look_preset(ax->value[i], NULL, NULL, 0)];
        for (int k = 0; k < 3; k++) L.rgb[3 * i + k] = powf(m->rgb[k], 1.f / 2.2f);   /* the shader takes sRGB */
        L.mr[2 * i] = m->metallic; L.mr[2 * i + 1] = m->roughness;
    }
    L.n = ax->n;
}

bool app_look_materials(void) { return app_eff_look() && G.faces_mode == FM_MAT && G.groups.axis[CV_AXIS_MAT].n > 0; }

const float* app_look_class_rgb(void) {
    if (!app_look_materials()) return NULL;
    if (L.dirty || L.n != G.groups.axis[CV_AXIS_MAT].n) build_classes();
    return L.rgb;
}

/* the group runs are the classes twice (app_field.c group_runs: covering shells after) */
void app_look_runs(int runs) {
    const float* rgb = app_look_class_rgb();
    if (!rgb || runs != 2 * L.n) { cv_render_groups_mr(NULL, 0); return; }
    float* mr = malloc((size_t)runs * 2 * sizeof(float));
    if (!mr) { cv_render_groups_mr(NULL, 0); return; }
    memcpy(mr, L.mr, (size_t)L.n * 2 * sizeof(float));
    memcpy(mr + 2 * L.n, L.mr, (size_t)L.n * 2 * sizeof(float));
    cv_render_groups_mr(mr, runs);
    free(mr);
}

void app_look_changed(void) {
    L.dirty = true;
    app_group_colors_changed();               /* the runs, the legend's swatches, see-through */
    app_wake();
}

int app_lighting(void) { return !G.render_view ? G.plain_shading : !G.shading ? 0 : G.look ? 2 : 1; }

void app_set_lighting(int k) {
    if (!G.render_view) { G.plain_shading = k >= 1; return; }          /* the plain light is its own setting */
    int was = G.look;
    G.shading = k > 0;
    if (k > 0) G.look = k == 2;
    if (G.look != was) app_look_changed();
}

void app_show_materials(void) {
    app_set_faces_mode(FM_MAT);
    G.shading = true; G.look = 1;
    app_render_view(true);
    app_look_changed();
}

/* ---- the render view switch: the effective values the frame uses ------------------ */

bool app_eff_shade(void) { return G.render_view ? G.shading : G.plain_shading; }
bool app_eff_look(void) { return G.render_view && G.shading && G.look; }
int  app_eff_bg_mode(void) { return G.render_view ? G.bg_mode : CV_BG_PLAIN; }

void app_render_view(bool on) {
    if (G.render_view == on) return;
    G.render_view = on; G.render_view_forced = false;
    snprintf(G.note, sizeof G.note, on ? "render view on" : "render view off");
    G.note_t = cv_now();
    app_look_changed();
}

/* ---- per frame -------------------------------------------------------------------- */

void app_look_frame(cv_draw* d) {
    sync_normals();
    d->look = app_eff_look();
    d->exposure = G.look_exposure;
    cv_bg_colors(app_eff_bg_mode(), G.bg, d->bg_top, d->bg_bot, NULL);
    d->face_mr[0] = 0.f; d->face_mr[1] = d->faces_color == CV_COLOR_SOLID ? 0.5f : 0.4f;   /* satin; a field glossier */
    bool moving = G.nav_live || G.flight || G.playing;
    bool soft = cv_gpu_is_software();
    d->ao = d->look && G.look_ao && G.show_faces && !cv_render_see_on() &&
            (!moving || (G.look_ao_moving && !soft));
    d->ao_radius = G.look_ao_radius;
    d->ao_strength = G.look_ao_strength;
    /* the floor: under the box of the shape as drawn, a hair lower so a part resting on it does not cut it */
    memcpy(d->bg_plain, G.bg, sizeof d->bg_plain); d->bg_mode = app_eff_bg_mode();
    d->floor_on = d->look && G.look_floor && G.show_faces && !(G.png_alpha && G.export_req) && d->diag > 0.f;
    d->floor_hold = G.playing && soft;
    d->shadow = d->look && G.look_shadow && G.show_faces && !cv_render_see_on() && !(moving && soft);
    d->shadow_size = soft ? CV_SHADOW_SOFT : CV_SHADOW_SIZE;
    d->light_jitter[0] = d->light_jitter[1] = 0.f;
    const float lo[3] = { G.vmin.x, G.vmin.y, G.vmin.z }, hi[3] = { G.vmax.x, G.vmax.y, G.vmax.z };
    float bl[3] = { 1e30f, 1e30f, 1e30f }, bh[3] = { -1e30f, -1e30f, -1e30f };   /* the model and its copies */
    for (int m = 0; m < app_copies(); m++) {
        float M[16];
        app_copy_matrix(m, M);
        for (int c = 0; c < 8; c++) {
            float p[3] = { c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2] };
            for (int k = 0; k < 3; k++) {
                float q = M[k] * p[0] + M[4 + k] * p[1] + M[8 + k] * p[2] + M[12 + k];
                bl[k] = CV_MIN(bl[k], q); bh[k] = CV_MAX(bh[k], q);
            }
        }
    }
    float e = 0.f;
    for (int k = 0; k < 3; k++) { d->sph_c[k] = 0.5f * (bl[k] + bh[k]); e += (bh[k] - bl[k]) * (bh[k] - bl[k]); }
    d->sph_r = 0.5f * sqrtf(e);
    d->floor_up = G.up_z ? 2 : 1;
    for (int k = 0; k < 3; k++) d->floor_c[k] = 0.5f * (lo[k] + hi[k]);
    d->floor_level = lo[d->floor_up] - 0.005f * d->diag;
    d->floor_half = 2.f * d->diag;
    d->floor_hmax = hi[d->floor_up] - d->floor_level;
}

void app_look_clear(void) {
    free(L.buf); L.buf = NULL; L.buf_n = 0;
    L.have = false; memset(&L.done, 0, sizeof L.done); memset(&L.pend, 0, sizeof L.pend);
    L.dirty = true;
}
