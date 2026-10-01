/* app_field.c -- decoded-field cache, colourings, displacement and the current
   step: everything that turns .frd values into what the renderer draws. */
#include "app_int.h"
#include "gauss.h"
#include "path.h"
#include <math.h>

/* ---- messages -------------------------------------------------------------- */

size_t app_total_msgs(void) {
    return G.frd.msgs.n + G.frd.msgs.dropped + G.msgs.n + G.msgs.dropped;
}

/* ---- decoded field cache ------------------------------------------------------ */

enum { CACHE_BUDGET = 768u << 20 };   /* bytes of decoded values kept around */

void cache_clear(void) {
    for (int i = 0; i < CV_CACHE_N; i++) free(G.cache[i].vals);
    memset(G.cache, 0, sizeof G.cache);
}

const float* cache_get(int step, int field) {
    for (int i = 0; i < CV_CACHE_N; i++)
        if (G.cache[i].vals && G.cache[i].step == step && G.cache[i].field == field) {
            G.cache[i].used = ++G.cache_clock;
            return G.cache[i].vals;
        }
    const cv_field_desc* d = &G.frd.steps[step].fields[field];
    size_t bytes = (size_t)CV_MAX(G.frd.n_nodes, 1) * (size_t)d->ncomp * sizeof(float);
    /* evict least recently used until the new entry fits the budget and a slot */
    for (;;) {
        size_t total = bytes; int used = 0, lru = -1;
        for (int i = 0; i < CV_CACHE_N; i++) {
            if (!G.cache[i].vals) continue;
            total += G.cache[i].bytes; used++;
            if (lru < 0 || G.cache[i].used < G.cache[lru].used) lru = i;
        }
        if ((total <= CACHE_BUDGET && used < CV_CACHE_N) || lru < 0) break;
        free(G.cache[lru].vals);
        memset(&G.cache[lru], 0, sizeof G.cache[lru]);
    }
    int slot = 0;
    while (slot < CV_CACHE_N && G.cache[slot].vals) slot++;
    if (slot == CV_CACHE_N) slot = 0;
    float* v = malloc(bytes);
    if (!v) { cv_msg_add(&G.msgs, 0, false, "out of memory decoding a field"); return NULL; }
    cv_frd_read_field(&G.frd, d, v, &G.msgs);
    deck_localize(step, d, v);
    G.cache[slot] = (cv_cache_entry){ step, field, v, bytes, ++G.cache_clock };
    return v;
}

/* the option label ("von Mises", "D1", ...) of component `comp` of a field */
int app_field_options(const cv_field_desc* d, cv_scalar_opt* out, int max) {
    int n = cv_field_options(d, out, max);
    if (G.csys > 0 && cv_cyl_applies(d))
        for (int i = 0; i < n; i++) if (out[i].comp >= 0) {
            char nm[12];
            cv_cyl_comp_name(d, out[i].comp, nm);
            snprintf(out[i].label, sizeof out[i].label, "%s", nm);
        }
    return n;
}

/* values of a field turned into the chosen cylindrical system: a copy, or NULL
   when the global values stand (no system chosen, or a scalar field) */
static float* to_csys(const cv_field_desc* d, const cv_frd* f, const float* vals) {
    if (G.csys <= 0 || !vals || !cv_cyl_applies(d)) return NULL;
    size_t bytes = (size_t)CV_MAX(f->n_nodes, 1) * (size_t)d->ncomp * sizeof(float);
    float* t = malloc(bytes);
    if (!t) return NULL;
    memcpy(t, vals, bytes);
    cv_cyl_values(d, f->xyz, f->n_nodes, G.csys - 1, G.csys_o, t);
    return t;
}

static const char* opt_label(const cv_field_desc* d, int comp) {
    static cv_scalar_opt opts[CV_MAX_OPTS];
    int n = app_field_options(d, opts, CV_MAX_OPTS);
    for (int i = 0; i < n; i++) if (opts[i].comp == comp) return opts[i].label;
    return "?";
}

/* ---- units: CalculiX has none, the user says which consistent set the model uses ---- */
const char* const cv_units_name[CV_UNITS_N] = {
    "units: none", "units: t mm s N MPa °C", "units: kg m s N Pa °C", "units: in lbf s psi °F",
};

enum { U_NONE, U_LEN, U_STRESS, U_FORCE, U_TEMP, U_ENERGY_D, U_VELO, U_FLUX, U_N };

static int quantity(const char* f, int comp) {
    static const struct { const char* name; int q; } T[] = {
        { "DISP", U_LEN }, { "DISPI", U_LEN }, { "PDISP", U_LEN }, { "MDISP", U_LEN }, { "MAXU", U_LEN },
        { "STRESS", U_STRESS }, { "STRESSI", U_STRESS }, { "PSTRESS", U_STRESS }, { "ZZSTR", U_STRESS },
        { "ZZSTRI", U_STRESS }, { "MAXS", U_STRESS }, { "PRESS", U_STRESS },
        { "FORC", U_FORCE }, { "FORCI", U_FORCE }, { "RF", U_FORCE },
        { "NDTEMP", U_TEMP }, { "NT", U_TEMP }, { "ENER", U_ENERGY_D }, { "VELO", U_VELO },
        { "HFL", U_FLUX },
        { "TOSTRAIN", U_NONE }, { "MESTRAIN", U_NONE }, { "PE", U_NONE }, { "SDV", U_NONE },
        /* .dat phrases */
        { "displacements", U_LEN }, { "stresses", U_STRESS }, { "forces", U_FORCE },
        { "temperatures", U_TEMP }, { "velocities", U_VELO }, { "heat flux", U_FLUX },
        { "internal energy density", U_ENERGY_D },
    };
    if (!strcmp(f, "CONTACT")) return comp >= 0 && comp < 3 ? U_LEN : U_STRESS;   /* COPEN CSLIP1 CSLIP2 | CPRESS CSHEAR1 CSHEAR2 */
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        size_t n = strlen(T[i].name);
        if (!strncmp(f, T[i].name, n) && (f[n] == 0 || f[n] == ' ' || f[n] == '(')) return T[i].q;
    }
    return -1;
}

const char* app_unit(const char* field, int comp) {
    static const char* const U[CV_UNITS_N][U_N] = {
        { 0 },
        { "", "mm", "MPa", "N", "°C", "mJ/mm³", "mm/s", "mW/mm²" },
        { "", "m", "Pa", "N", "°C", "J/m³", "m/s", "W/m²" },
        { "", "in", "psi", "lbf", "°F", "in·lbf/in³", "in/s", "in·lbf/(s·in²)" },
    };
    if (G.units <= 0 || G.units >= CV_UNITS_N || !field) return "";
    int q = quantity(field, comp);
    return q < 0 ? "" : U[G.units][q];
}

/* " [MPa]" appended to a label, nothing without a unit */
static void add_unit(char* lab, size_t cap, const char* field, int comp) {
    const char* u = app_unit(field, comp);
    size_t n = strlen(lab);
    if (u[0] && n < cap) snprintf(lab + n, cap - n, " [%s]", u);
}

int find_field(int step, const char* name) {
    if (step < 0 || step >= G.frd.n_steps) return -1;
    const cv_step* s = &G.frd.steps[step];
    for (int i = 0; i < s->nfields; i++)
        if (strcmp(s->fields[i].name, name) == 0) return i;
    return -1;
}

/* ---- field / displacement refresh ----------------------------------------------- */

/* ---- group colours --------------------------------------------------------------- */

static void palette(int i, float rgb[3]) {
    static const float base[10][3] = {             /* Tableau 10 */
        {0.31f,0.47f,0.65f}, {0.95f,0.56f,0.16f}, {0.88f,0.34f,0.35f}, {0.46f,0.72f,0.70f},
        {0.35f,0.63f,0.31f}, {0.93f,0.79f,0.28f}, {0.69f,0.48f,0.63f}, {1.00f,0.62f,0.66f},
        {0.61f,0.46f,0.38f}, {0.73f,0.69f,0.67f},
    };
    const float* b = base[i % 10];
    int shade = (i / 10) % 3;                      /* beyond 10: lighter, then darker */
    for (int k = 0; k < 3; k++)
        rgb[k] = shade == 0 ? b[k] : shade == 1 ? b[k] + (1 - b[k]) * 0.45f : b[k] * 0.6f;
}

/* Group colours: the skin triangles sorted by group (counting sort), drawn as
   one solid-colour run per group -- no per-triangle lookup in the shader. */
void refresh_tri_colors(void) {
    int axis = G.faces_mode - FM_TYPE;
    if (axis < 0 || axis >= CV_AXIS_N || !G.skin.n_tri || !G.axis_rgb[axis]) {
        cv_render_groups(NULL, 0, NULL, NULL, 0);
        return;
    }
    const cv_axis* ax = &G.groups.axis[axis];
    int ng = ax->n;
    uint32_t* first = calloc((size_t)ng + 1, sizeof(uint32_t));
    uint32_t* fill = malloc(((size_t)ng + 1) * sizeof(uint32_t));
    uint32_t* tri = malloc(G.skin.n_tri * 3 * sizeof(uint32_t));
    if (!first || !fill || !tri) { free(first); free(fill); free(tri); cv_render_groups(NULL, 0, NULL, NULL, 0); return; }
    for (size_t t = 0; t < G.skin.n_tri; t++) first[ax->of_elem[G.skin.tri_elem[t]] + 1]++;
    for (int g = 0; g < ng; g++) first[g + 1] += first[g];
    memcpy(fill, first, ((size_t)ng + 1) * sizeof(uint32_t));
    for (size_t t = 0; t < G.skin.n_tri; t++) {
        uint32_t k = fill[ax->of_elem[G.skin.tri_elem[t]]]++;
        memcpy(tri + 3 * k, G.skin.tri + 3 * t, 3 * sizeof(uint32_t));
    }
    cv_render_groups(tri, G.skin.n_tri, first, G.axis_rgb[axis], ng);
    free(first); free(fill); free(tri);
}

void app_set_faces_mode(int fm) {
    G.faces_mode = fm;
    refresh_tri_colors();
}

void app_group_colors_changed(void) { refresh_tri_colors(); }

void init_group_colors(void) {
    for (int a = 0; a < CV_AXIS_N; a++) {
        free(G.axis_rgb[a]);
        int n = G.groups.axis[a].n;
        G.axis_rgb[a] = malloc((size_t)CV_MAX(n, 1) * 3 * sizeof(float));
        if (!G.axis_rgb[a]) continue;
        for (int i = 0; i < n; i++) palette(i, G.axis_rgb[a] + 3 * i);
    }
}

/* Per-element colouring: every skin triangle expanded to its own three vertices
   carrying the element's value (robust on every driver). Above ELEMTRI_MAX
   triangles that would cost too much memory, so the per-triangle texture path
   (gl_PrimitiveID) takes over. */
#ifdef __EMSCRIPTEN__
enum { ELEMTRI_MAX = 1 << 30 };           /* WebGL2 has no gl_PrimitiveID */
#else
enum { ELEMTRI_MAX = 4000000 };
#endif

void refresh_tri_values(void) {
    bool want = G.elem_mode && G.field_src == 0;
    if (!want || !G.has_field || !G.skin.n_tri) { cv_render_tri_values(NULL, 0); cv_render_aux(CV_AUX_ELEMTRI, NULL, NULL, NULL, 0); return; }
    if (G.skin.n_tri <= ELEMTRI_MAX) {
        size_t nv = G.skin.n_tri * 3;
        float* pos = malloc(nv * 3 * sizeof(float));
        float* disp = G.disp ? malloc(nv * 3 * sizeof(float)) : NULL;
        float* val = malloc(nv * sizeof(float));
        if (pos && val && (disp || !G.disp)) {
            for (size_t t = 0; t < G.skin.n_tri; t++) {
                uint32_t e = G.skin.tri_elem[t];
                CV_ASSERT(e < G.frd.n_elems);
                for (int k = 0; k < 3; k++) {
                    uint32_t n = G.skin.tri[3 * t + k];
                    CV_ASSERT(n < G.frd.n_nodes);
                    memcpy(pos + (3 * t + k) * 3, G.frd.xyz + 3 * n, 3 * sizeof(float));
                    if (disp) memcpy(disp + (3 * t + k) * 3, G.disp + 3 * n, 3 * sizeof(float));
                    val[3 * t + k] = G.elem_val[e];
                }
            }
            cv_render_aux(CV_AUX_ELEMTRI, pos, disp, val, (uint32_t)nv);
            cv_render_tri_values(NULL, 0);
            free(pos); free(disp); free(val);
            return;
        }
        free(pos); free(disp); free(val);      /* out of memory: the texture path below */
    }
    cv_render_aux(CV_AUX_ELEMTRI, NULL, NULL, NULL, 0);
    float* t = realloc(G.tri_val, G.skin.n_tri * sizeof(float));
    if (!t) { cv_render_tri_values(NULL, 0); return; }
    G.tri_val = t;
    for (size_t i = 0; i < G.skin.n_tri; i++) { CV_ASSERT(G.skin.tri_elem[i] < G.frd.n_elems); t[i] = G.elem_val[G.skin.tri_elem[i]]; }
    cv_render_tri_values(t, G.skin.n_tri);
}

/* the two extreme values as balls: at their node, or the element's centre */
static bool extreme_pos(uint32_t at, float p[3], float d[3]) {
    if (at == UINT32_MAX || G.field_src == 1) return false;
    if (!G.elem_mode) {
        memcpy(p, G.frd.xyz + 3 * at, 3 * sizeof(float));
        if (G.disp) memcpy(d, G.disp + 3 * at, 3 * sizeof(float)); else d[0] = d[1] = d[2] = 0;
        return true;
    }
    uint32_t b = G.frd.eoff[at], n = G.frd.eoff[at + 1] - b;
    if (!n) return false;
    p[0] = p[1] = p[2] = d[0] = d[1] = d[2] = 0;
    for (uint32_t j = b; j < b + n; j++)
        for (int k = 0; k < 3; k++) { p[k] += G.frd.xyz[3 * G.frd.conn[j] + k] / n; if (G.disp) d[k] += G.disp[3 * G.frd.conn[j] + k] / n; }
    return true;
}

void refresh_markers(void) {
    float pos[6], disp[6], val[2];
    int n = 0;
    const uint32_t at[2] = { G.min_at, G.max_at };
    for (int i = 0; G.has_field && i < 2; i++)
        if (extreme_pos(at[i], pos + 3 * n, disp + 3 * n)) { val[n] = i ? G.data_max : G.data_min; n++; }
    cv_render_aux(CV_AUX_MARK, n ? pos : NULL, disp, val, (uint32_t)n);
}

/* The legend's range and the min / max markers cover what is drawn: the nodes
   of the visible elements (skin.pt), or the visible elements in per-element
   mode. Hidden groups and the crop box change the range on purpose. Gauss
   point fields range over every point of the .dat block. */
void app_refresh_range(void) {
    if (!G.has_field) return;
    const float* v = G.elem_mode ? G.elem_val : G.scalar;
    size_t n = G.elem_mode ? G.frd.n_elems : G.frd.n_nodes;
    const uint32_t* ids = NULL;             /* the subset actually shown, when not everything */
    if (G.field_src == 1) {
        gp_values(&v, &n);
    } else if (!G.elem_mode && G.skin.n_pt) {
        ids = G.skin.pt; n = G.skin.n_pt;
    }
    G.data_min = INFINITY; G.data_max = -INFINITY;
    G.min_at = G.max_at = UINT32_MAX;
    G.nan_count = 0;
    for (size_t k = 0; k < n; k++) {
        uint32_t i = ids ? ids[k] : (uint32_t)k;
        if (G.elem_mode && G.field_src == 0 && G.vis && !G.vis[i]) continue;
        float x = v[i];
        if (x != x) { G.nan_count++; continue; }
        if (isinf(x)) continue;
        if (x < G.data_min) { G.data_min = x; G.min_at = i; }
        if (x > G.data_max) { G.data_max = x; G.max_at = i; }
    }
    if (G.min_at == UINT32_MAX) { G.data_min = 0; G.data_max = 1; }   /* nothing but NaN */
    CV_ASSERT(G.min_at == UINT32_MAX || G.min_at < (G.field_src == 1 ? n : G.elem_mode ? G.frd.n_elems : G.frd.n_nodes));
    CV_ASSERT(G.data_min <= G.data_max);
    refresh_markers();
    if (!G.range_lock) {
        G.rmin = G.data_min; G.rmax = G.data_max;
        if (G.center_zero) cv_center_zero(&G.rmin, &G.rmax);
    }
}

static void refresh_field_dat(void) {
    G.has_field = gp_refresh();
    cv_render_scalar(NULL, 0);                  /* no nodal values: nodes/edges go plain */
    if (G.has_field) {
        cv_field_desc d;
        const char* lab = gp_desc(G.field_name, &d) ? opt_label(&d, G.comp) : "?";
        snprintf(G.field_label, sizeof G.field_label, "%s %s (Gauss)", G.field_name, lab);
        add_unit(G.field_label, sizeof G.field_label, G.field_name, G.comp);
        app_refresh_range();
    } else {
        snprintf(G.field_label, sizeof G.field_label, "%s (not in this increment)", G.field_name);
    }
    refresh_tri_values();
}

/* ---- vector arrows: a 3-component field drawn at the nodes ------------------------
   Arrow from the (deformed) node along the vector; the longest one is vec_pct %
   of the model diagonal. Coloured by the current scalar, so the legend applies. */

bool app_field_is_vector(void) {
    if (!G.loaded || G.field_src != 0) return false;
    int fi = find_field(G.step, G.field_name);
    if (fi < 0) return false;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
    return d->ncomp == 3 || (G.comp <= CV_COMP_P1 && G.comp >= CV_COMP_P3_XZ && cv_tensor_order(d));
}

/* a principal value as a pair of arrows through the node along its direction:
   pointing out for tension, in for compression (the usual stress-cross picture) */
static void principal_arrows(const float* v, const uint32_t* ids, size_t n, size_t stride, cv_fvec* pos, cv_fvec* disp, cv_fvec* scal) {
    bool xz = G.comp <= CV_COMP_P1_XZ;
    int k = (xz ? CV_COMP_P1_XZ : CV_COMP_P1) - G.comp;
    float peak = 0;
    for (size_t j = 0; j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        float m = fabsf(G.scalar[i]);
        if (m == m && m > peak) peak = m;
    }
    float L = CV_MAX(G.vec_pct, 0.1f) * 0.01f * G.diag;
    for (size_t j = 0; peak > 0 && j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        float val[3], vec[3][3];
        if (!cv_principal_dirs(v + 6 * (size_t)i, xz, val, vec)) continue;
        float h = 0.5f * L * fabsf(val[k]) / peak;
        if (!(h > 0)) continue;
        const float* p = G.frd.xyz + 3 * (size_t)i;
        float d[6];
        app_node_disp6(i, d);
        size_t before = pos->n;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            float dir[3] = { vec[k][0] * sgn, vec[k][1] * sgn, vec[k][2] * sgn };
            if (val[k] >= 0) {
                float tip[3] = { p[0] + dir[0] * h, p[1] + dir[1] * h, p[2] + dir[2] * h };
                deck_arrow(pos, disp, tip, dir, h, d, false);
            } else {                                    /* head at the node, shaft outside */
                float in[3] = { -dir[0], -dir[1], -dir[2] };
                deck_arrow(pos, disp, p, in, h, d, false);
            }
        }
        for (size_t q = before; q < pos->n; q += 3) cv_push(*scal, G.scalar[i]);
    }
}

void refresh_vectors(void) {
    if (!G.show_vec || !app_field_is_vector() || !G.has_field) { cv_render_aux(CV_AUX_VECLN, NULL, NULL, NULL, 0); return; }
    int fi = find_field(G.step, G.field_name);
    const float* v = cache_get(G.step, fi);
    if (!v) { cv_render_aux(CV_AUX_VECLN, NULL, NULL, NULL, 0); return; }
    const uint32_t* ids = G.skin.n_pt ? G.skin.pt : NULL;
    size_t n = ids ? G.skin.n_pt : G.frd.n_nodes;
    size_t stride = n / 200000 + 1;                     /* huge models: a sample */
    if (G.frd.steps[G.step].fields[fi].ncomp == 6) {
        cv_fvec pos = {0}, disp = {0}, scal = {0};
        if (G.scalar) principal_arrows(v, ids, n, stride, &pos, &disp, &scal);
        app_aux_upload(CV_AUX_VECLN, &pos, &disp, scal.a);
        cv_free_vec(pos); cv_free_vec(disp); cv_free_vec(scal);
        return;
    }
    float peak = 0;
    for (size_t j = 0; j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        float m = sqrtf(v[3 * i] * v[3 * i] + v[3 * i + 1] * v[3 * i + 1] + v[3 * i + 2] * v[3 * i + 2]);
        if (m == m && m > peak) peak = m;
    }
    cv_fvec pos = {0}, disp = {0}, scal = {0};
    float L = CV_MAX(G.vec_pct, 0.1f) * 0.01f * G.diag;
    for (size_t j = 0; peak > 0 && j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        const float* r = v + 3 * i;
        float m = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        if (!(m > 0) || m != m) continue;
        float dir[3] = { r[0] / m, r[1] / m, r[2] / m }, len = L * m / peak;
        float p[3] = { G.frd.xyz[3 * i], G.frd.xyz[3 * i + 1], G.frd.xyz[3 * i + 2] };
        float tip[3] = { p[0] + dir[0] * len, p[1] + dir[1] * len, p[2] + dir[2] * len };
        float d[6];
        app_node_disp6(i, d);
        size_t before = pos.n;
        deck_arrow(&pos, &disp, tip, dir, len, d, false);
        float sv = G.scalar ? G.scalar[i] : NAN;
        for (size_t k = before; k < pos.n; k += 3) cv_push(scal, sv);
    }
    app_aux_upload(CV_AUX_VECLN, &pos, &disp, scal.a);
    cv_free_vec(pos); cv_free_vec(disp); cv_free_vec(scal);
}

void app_vectors_changed(void) { refresh_vectors(); }

/* ---- path plot ------------------------------------------------------------------- */

static void path_vertex(cv_fvec* pos, cv_fvec* disp, uint32_t n) {
    float d[6];
    app_node_disp6(n, d);
    for (int k = 0; k < 3; k++) cv_push(*pos, G.frd.xyz[3 * (size_t)n + k]);
    for (int k = 0; k < 6; k++) cv_push(*disp, d[k]);
}

/* the displacement (6 wide) at every straight-path sample: interpolated in its
   element, the node's own at a node end, and between the neighbours where the
   sample is outside the solid */
static float* path_disp(void) {
    uint32_t n = G.path_n;
    float* d = calloc((size_t)n * 6, sizeof(float));
    uint8_t* ok = calloc(n, 1);
    if (!d || !ok) { free(d); free(ok); return NULL; }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t node = i == 0 ? G.path_end[0] : i == n - 1 ? G.path_end[1] : UINT32_MAX, e = G.path_el[i];
        if (node < G.frd.n_nodes) { app_node_disp6(node, d + 6 * i); ok[i] = 1; continue; }
        if (e == UINT32_MAX) continue;
        int t = G.frd.etype[e];
        uint32_t b = G.frd.eoff[e], nn = G.frd.eoff[e + 1] - b;
        for (uint32_t j = 0; j < nn; j++) {
            float q[6];
            app_node_disp6(G.frd.conn[b + (uint32_t)cv_frd_node_pos(t, (int)nn, (int)j)], q);
            for (int k = 0; k < 6; k++) d[6 * i + k] += G.path_w[20 * i + j] * q[k];
        }
        ok[i] = 1;
    }
    for (uint32_t i = 0; i < n; i++) {                  /* the gaps: between the known ones */
        if (ok[i]) continue;
        int lo = (int)i - 1, hi = (int)i + 1;
        while (lo >= 0 && !ok[lo]) lo--;
        while (hi < (int)n && !ok[hi]) hi++;
        for (int k = 0; k < 6; k++) {
            float a = lo >= 0 ? d[6 * lo + k] : hi < (int)n ? d[6 * hi + k] : 0, c = hi < (int)n ? d[6 * hi + k] : a;
            float f = lo >= 0 && hi < (int)n ? (float)(i - lo) / (hi - lo) : 0;
            d[6 * i + k] = a + f * (c - a);
        }
    }
    free(ok);
    return d;
}

/* a segment as short pieces, positions and displacements linear along it: drawn
   as lines and, over them, as dots, so the path reads as a thick line */
#define PATH_SUB 6
static void seg_push(cv_fvec* pos, cv_fvec* disp, const float p0[3], const float d0[6], const float p1[3], const float d1[6]) {
    for (int j = 0; j < PATH_SUB; j++)
        for (int e = 0; e < 2; e++) {
            float f = (float)(j + e) / PATH_SUB;
            for (int k = 0; k < 3; k++) cv_push(*pos, p0[k] + f * (p1[k] - p0[k]));
            for (int k = 0; k < 6; k++) cv_push(*disp, d0[k] + f * (d1[k] - d0[k]));
        }
}

/* the path plot's line, the (linearized) straight line, and a ball on every pick */
void refresh_path(void) {
    cv_fvec pos = {0}, disp = {0};
    float ends[2][3], ed6[2][6];
    bool line = G.loaded && G.path_n > 0;
    if (line && G.path_surface && G.path_nodes)
        for (uint32_t i = 0; i + 1 < G.path_n; i++) {
            float d0[6], d1[6];
            uint32_t a = G.path_nodes[i], b = G.path_nodes[i + 1];
            app_node_disp6(a, d0); app_node_disp6(b, d1);
            seg_push(&pos, &disp, G.frd.xyz + 3 * (size_t)a, d0, G.frd.xyz + 3 * (size_t)b, d1);
        }
    if (line) {
        memcpy(ends, G.path_p, sizeof ends);
        if (!G.path_surface && G.path_el) {             /* the sampled line, bending with the model */
            float* d = path_disp();
            for (uint32_t i = 0; d && i + 1 < G.path_n; i++) {
                float f0 = (float)i / (G.path_n - 1), f1 = (float)(i + 1) / (G.path_n - 1), p0[3], p1[3];
                for (int k = 0; k < 3; k++) { p0[k] = ends[0][k] + f0 * (ends[1][k] - ends[0][k]); p1[k] = ends[0][k] + f1 * (ends[1][k] - ends[0][k]); }
                seg_push(&pos, &disp, p0, d + 6 * i, p1, d + 6 * (i + 1));
            }
            if (d) { memcpy(ed6[0], d, sizeof ed6[0]); memcpy(ed6[1], d + 6 * (G.path_n - 1), sizeof ed6[1]); }
            else memset(ed6, 0, sizeof ed6);
            free(d);
        } else {                                        /* surface mode: the chord between the two nodes */
            app_node_disp6(G.path_end[0], ed6[0]); app_node_disp6(G.path_end[1], ed6[1]);
            seg_push(&pos, &disp, ends[0], ed6[0], ends[1], ed6[1]);
        }
    }
    app_aux_upload(CV_AUX_PATHLN, &pos, &disp, NULL);
    pos.n = disp.n = 0;
    if (line && !G.path_surface) {                      /* the line on both sides, to the model's box */
        const float *A = ends[0], *B = ends[1];
        float u[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, L = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z }, t0 = -INFINITY, t1 = INFINITY;
        for (int k = 0; k < 3 && L > 0; k++) {
            float m = 0.05f * G.diag;
            u[k] /= L;
            if (fabsf(u[k]) < 1e-12f) continue;
            float a = (lo[k] - m - A[k]) / u[k], b = (hi[k] + m - A[k]) / u[k];
            t0 = CV_MAX(t0, CV_MIN(a, b)); t1 = CV_MIN(t1, CV_MAX(a, b));
        }
        if (L > 0 && t0 < 0 && t1 > L) {
            float P0[3], P1[3];
            for (int k = 0; k < 3; k++) { P0[k] = A[k] + t0 * u[k]; P1[k] = A[k] + t1 * u[k]; }
            seg_push(&pos, &disp, P0, ed6[0], A, ed6[0]);
            seg_push(&pos, &disp, B, ed6[1], P1, ed6[1]);
        }
    }
    app_aux_upload(CV_AUX_RAYLN, &pos, &disp, NULL);
    pos.n = disp.n = 0;
    if (G.loaded) {
        uint32_t m[2];
        int k = 0;
        if (G.probe_on && G.probe.hit) m[k++] = G.probe.node;
        if (G.path_arm && G.path_a != UINT32_MAX) m[k++] = G.path_a;
        for (int i = 0; i < k; i++) if (m[i] < G.frd.n_nodes) path_vertex(&pos, &disp, m[i]);
        for (int e = 0; line && e < 2; e++) {
            for (int c = 0; c < 3; c++) cv_push(pos, ends[e][c]);
            for (int c = 0; c < 6; c++) cv_push(disp, ed6[e][c]);
        }
    }
    app_aux_upload(CV_AUX_PICKPT, &pos, &disp, NULL);
    cv_free_vec(pos); cv_free_vec(disp);
}

/* the markers follow the picks: redrawn when one of them (or the model or
   step under them) changes */
void app_marks_sync(void) {
    uint64_t k[] = { G.loaded, (uintptr_t)G.frd.xyz, (uintptr_t)G.disp, (uint64_t)G.step,
                     G.probe_on && G.probe.hit ? G.probe.node : UINT32_MAX,
                     G.path_arm ? G.path_a : UINT32_MAX,
                     G.path_n ? G.path_gen : UINT64_MAX };
    static uint64_t last[sizeof k / sizeof k[0]];
    if (!memcmp(k, last, sizeof k)) return;
    memcpy(last, k, sizeof k);
    refresh_path();
}

/* ---- history: the field at one node over every step ------------------------------
   Each step's field is decoded into a scratch buffer (not the cache, which holds
   what is on screen); rebuilt only when the field, component, node or system change. */

static void refresh_hist(void) {
    if (!G.hist_open || !G.loaded) return;
    char key[200];
    snprintf(key, sizeof key, "%s|%d|%d|%g|%g|%g|%u|%u|%d|%d|%d", G.field_name, G.comp, G.csys, G.csys_o[0], G.csys_o[1],
             G.csys_o[2], G.hist_node, G.hist_elem, G.elem_mode, G.field_src, G.frd.n_steps);
    if (!strcmp(key, G.hist_key)) return;
    snprintf(G.hist_key, sizeof G.hist_key, "%s", key);
    free(G.hist_t); free(G.hist_v); free(G.hist_step);
    G.hist_t = G.hist_v = NULL; G.hist_step = NULL; G.hist_n = 0;
    if (G.field_src != 0 || G.frd.n_steps == 0 || G.hist_node >= G.frd.n_nodes) return;
    int ns = G.frd.n_steps;
    G.hist_t = malloc((size_t)ns * sizeof(float)); G.hist_v = malloc((size_t)ns * sizeof(float));
    G.hist_step = malloc((size_t)ns * sizeof(int));
    if (!G.hist_t || !G.hist_v || !G.hist_step) return;
    /* the nodes averaged: one, or the element's */
    uint32_t one = G.hist_node, *nodes = &one, nn = 1;
    if (G.elem_mode && G.hist_elem < G.frd.n_elems) {
        nodes = G.frd.conn + G.frd.eoff[G.hist_elem];
        nn = G.frd.eoff[G.hist_elem + 1] - G.frd.eoff[G.hist_elem];
    }
    float* buf = NULL; size_t cap = 0;
    for (int s = 0; s < ns; s++) {
        int fi = find_field(s, G.field_name);
        if (fi < 0) continue;
        const cv_field_desc* d = &G.frd.steps[s].fields[fi];
        if (G.comp >= d->ncomp) continue;
        const float* v = NULL;
        for (int i = 0; i < CV_CACHE_N; i++)            /* already decoded? */
            if (G.cache[i].vals && G.cache[i].step == s && G.cache[i].field == fi) v = G.cache[i].vals;
        if (!v) {
            size_t need = (size_t)CV_MAX(G.frd.n_nodes, 1) * (size_t)d->ncomp;
            if (need > cap) { float* nb = realloc(buf, need * sizeof(float)); if (!nb) break; buf = nb; cap = need; }
            cv_frd_read_field(&G.frd, d, buf, NULL);
            deck_localize(s, d, buf);
            v = buf;
        }
        double sum = 0; int cnt = 0;
        for (uint32_t j = 0; j < nn; j++) {
            uint32_t n = nodes[j];
            float r[CV_MAX_COMP], x;
            memcpy(r, v + (size_t)n * d->ncomp, (size_t)d->ncomp * sizeof(float));
            if (G.csys > 0 && G.comp >= 0 && cv_cyl_applies(d)) cv_cyl_values(d, G.frd.xyz + 3 * (size_t)n, 1, G.csys - 1, G.csys_o, r);
            cv_field_scalar(r, d->ncomp, 1, G.comp, &x);
            if (x == x) { sum += x; cnt++; }
        }
        G.hist_t[G.hist_n] = G.frd.steps[s].time;
        G.hist_v[G.hist_n] = cnt ? (float)(sum / cnt) : NAN;
        G.hist_step[G.hist_n++] = s;
    }
    free(buf);
}

void app_hist_open(uint32_t node, uint32_t elem) {
    G.hist_node = node; G.hist_elem = elem;
    G.hist_open = true;
    G.hist_by_step = false;                     /* time, unless it does not run forward (modal: frequencies) */
    for (int s = 0; s < G.frd.n_steps; s++)
        if (G.frd.steps[s].modal || (s && G.frd.steps[s].time < G.frd.steps[s - 1].time)) G.hist_by_step = true;
    G.hist_key[0] = 0;
    refresh_hist();
}

void app_hist_close(void) {
    free(G.hist_t); free(G.hist_v); free(G.hist_step);
    G.hist_t = G.hist_v = NULL; G.hist_step = NULL; G.hist_n = 0;
    G.hist_open = false; G.hist_key[0] = 0;
}

bool app_hist_csv(const char* path) {
    if (!G.hist_n) return false;
    FILE* o = fopen(path, "wb");
    if (!o) return false;
    fprintf(o, "step,time,%s %s %u\n", G.field_label, G.elem_mode ? "element" : "node",
            G.elem_mode ? G.frd.elem_id[G.hist_elem] : G.frd.node_id[G.hist_node]);
    for (int i = 0; i < G.hist_n; i++) {
        fprintf(o, "%d,%.9g,", G.hist_step[i] + 1, G.hist_t[i]);
        if (G.hist_v[i] == G.hist_v[i]) fprintf(o, "%.9g\n", G.hist_v[i]); else fprintf(o, "nan\n");
    }
    return fclose(o) == 0;
}

/* ---- stress linearization: a tensor along a straight line through the solid ------
   Sampled at LIN_N points by locating each in a solid element (Newton on the shape
   functions) and interpolating the nodal values there. */

#define LIN_N 41

/* the tensor linearized: the field shown when it is an .frd tensor, else STRESS */
static int lin_field(void) {
    int fi = G.field_src == 0 ? find_field(G.step, G.field_name) : -1;
    if (fi >= 0 && cv_tensor_order(&G.frd.steps[G.step].fields[fi]) == 1) return fi;
    fi = find_field(G.step, "STRESS");
    return fi >= 0 && cv_tensor_order(&G.frd.steps[G.step].fields[fi]) == 1 ? fi : -1;
}

/* natural coordinates of p in solid element e; N: its shape functions there */
static bool elem_locate(uint32_t e, const double p[3], double N[20]) {
    int t = G.frd.etype[e];
    uint32_t b = G.frd.eoff[e], nn = G.frd.eoff[e + 1] - b;
    if (t < 1 || t > 6 || nn > 20) return false;
    double X[20][3], xi[3];
    for (uint32_t i = 0; i < nn; i++) {
        const float* q = G.frd.xyz + 3 * (size_t)G.frd.conn[b + (uint32_t)cv_frd_node_pos(t, (int)nn, (int)i)];
        for (int k = 0; k < 3; k++) X[i][k] = q[k];
    }
    bool tet = t == 3 || t == 6, wedge = t == 2 || t == 5;
    xi[0] = xi[1] = tet ? 0.25 : wedge ? 1.0 / 3 : 0; xi[2] = tet ? 0.25 : 0;
    for (int it = 0; it < 30; it++) {
        double f[3], J[3][3];
        for (int c = -1; c < 3; c++) {                  /* x(xi) and its derivatives */
            double y[3] = { xi[0], xi[1], xi[2] }, x[3] = { 0, 0, 0 };
            if (c >= 0) y[c] += 1e-6;
            if (!cv_shape(t, (int)nn, y, N)) return false;
            for (uint32_t i = 0; i < nn; i++) for (int k = 0; k < 3; k++) x[k] += N[i] * X[i][k];
            if (c < 0) for (int k = 0; k < 3; k++) f[k] = x[k] - p[k];
            else for (int k = 0; k < 3; k++) J[k][c] = (x[k] - p[k] - f[k]) / 1e-6;
        }
        double det = J[0][0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1]) - J[0][1] * (J[1][0] * J[2][2] - J[1][2] * J[2][0])
                   + J[0][2] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]);
        if (!(fabs(det) > 1e-300)) return false;
        double d[3];
        for (int c = 0; c < 3; c++) {                   /* Cramer: J d = -f */
            double A[3][3];
            memcpy(A, J, sizeof A);
            for (int k = 0; k < 3; k++) A[k][c] = -f[k];
            d[c] = (A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
                  + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0])) / det;
        }
        for (int k = 0; k < 3; k++) xi[k] = CV_MAX(-3.0, CV_MIN(3.0, xi[k] + d[k]));
        if (fabs(d[0]) + fabs(d[1]) + fabs(d[2]) < 1e-10) break;
    }
    const double e1 = 1e-3;
    bool in = tet   ? xi[0] >= -e1 && xi[1] >= -e1 && xi[2] >= -e1 && xi[0] + xi[1] + xi[2] <= 1 + e1
            : wedge ? xi[0] >= -e1 && xi[1] >= -e1 && xi[0] + xi[1] <= 1 + e1 && fabs(xi[2]) <= 1 + e1
                    : fabs(xi[0]) <= 1 + e1 && fabs(xi[1]) <= 1 + e1 && fabs(xi[2]) <= 1 + e1;
    return in && cv_shape(t, (int)nn, xi, N);
}

/* Where n points evenly on the straight line A..B sit in the solid: the element
   (UINT32_MAX outside) and its 20 shape-function weights. One pass keeps the
   elements whose box the segment actually crosses (slab test), with their boxes;
   each point then tries the last hit, and the Newton inversion only in the
   candidates whose box holds it. */
typedef struct { uint32_t e; float lo[3], hi[3]; } line_cand;

static void line_locate(const float A[3], const float B[3], int n, uint32_t* el, float* w) {
    float tol = 1e-4f * G.diag, D[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
    CV_VEC(line_cand) cand = {0};
    for (uint32_t e = 0; e < G.frd.n_elems; e++) {
        int t = G.frd.etype[e];
        if (t < 1 || t > 6) continue;
        line_cand c = { e, { INFINITY, INFINITY, INFINITY }, { -INFINITY, -INFINITY, -INFINITY } };
        for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) {
            const float* q = G.frd.xyz + 3 * (size_t)G.frd.conn[j];
            for (int k = 0; k < 3; k++) { c.lo[k] = fminf(c.lo[k], q[k]); c.hi[k] = fmaxf(c.hi[k], q[k]); }
        }
        float t0 = 0.f, t1 = 1.f;
        for (int k = 0; k < 3 && t0 <= t1; k++) {
            c.lo[k] -= tol; c.hi[k] += tol;
            if (fabsf(D[k]) < 1e-30f) { if (A[k] < c.lo[k] || A[k] > c.hi[k]) t0 = 2.f; continue; }
            float a = (c.lo[k] - A[k]) / D[k], b = (c.hi[k] - A[k]) / D[k];
            if (a > b) { float s = a; a = b; b = s; }
            t0 = fmaxf(t0, a); t1 = fminf(t1, b);
        }
        if (t0 <= t1) cv_push(cand, c);
    }
    uint32_t hint = UINT32_MAX;
    for (int i = 0; i < n; i++) {
        double f = n > 1 ? (double)i / (n - 1) : 0, p[3], N[20];
        for (int k = 0; k < 3; k++) p[k] = A[k] + f * D[k];
        el[i] = UINT32_MAX;
        bool ok = hint != UINT32_MAX && elem_locate(hint, p, N);
        for (size_t c = 0; !ok && c < cand.n; c++) {
            const line_cand* q = &cand.a[c];
            if (p[0] < q->lo[0] || p[1] < q->lo[1] || p[2] < q->lo[2] || p[0] > q->hi[0] || p[1] > q->hi[1] || p[2] > q->hi[2]) continue;
            if ((ok = elem_locate(q->e, p, N))) hint = q->e;
        }
        if (!ok) continue;
        el[i] = hint;
        for (int k = 0; k < 20; k++) w[20 * i + k] = (float)N[k];
    }
    cv_free_vec(cand);
}

/* nc values per node interpolated in element e with weights w; false where one is missing */
static bool line_interp(uint32_t e, const float* w, const float* v, int nc, float* out) {
    if (e == UINT32_MAX) return false;
    int t = G.frd.etype[e];
    uint32_t b = G.frd.eoff[e], nn = G.frd.eoff[e + 1] - b;
    double s[6] = { 0 };
    for (uint32_t i = 0; i < nn; i++) {
        const float* q = v + nc * (size_t)G.frd.conn[b + (uint32_t)cv_frd_node_pos(t, (int)nn, (int)i)];
        for (int c = 0; c < nc; c++) s[c] += w[i] * q[c];
    }
    for (int c = 0; c < nc; c++) { if (s[c] != s[c]) return false; out[c] = (float)s[c]; }
    return true;
}

static void refresh_lin(void) {
    if (!G.lin_open || !G.loaded) return;
    int fi = lin_field();
    char key[200];
    const float *A = G.lin_p[0], *B = G.lin_p[1];
    snprintf(key, sizeof key, "%d|%d|%d|%g|%g|%g|%g|%g|%g|%g|%g|%g", G.step, fi, G.csys, G.csys_o[0], G.csys_o[1], G.csys_o[2],
             A[0], A[1], A[2], B[0], B[1], B[2]);
    if (!strcmp(key, G.lin_key)) return;
    snprintf(G.lin_key, sizeof G.lin_key, "%s", key);
    free(G.lin_s); G.lin_s = NULL; G.lin_n = 0;
    G.lin_fi = fi;
    G.lin_t = sqrtf((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2]));
    if (fi < 0) return;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
    const float* v = cache_get(G.step, fi);
    if (!v) return;
    float* tv = to_csys(d, &G.frd, v);
    const float* S = tv ? tv : v;
    static uint32_t el[LIN_N];
    static float w[LIN_N * 20];
    static char where[200];
    G.lin_s = malloc(LIN_N * 6 * sizeof(float));
    if (!G.lin_s) { free(tv); return; }
    char wk[200];
    snprintf(wk, sizeof wk, "%p|%u|%g|%g|%g|%g|%g|%g", (void*)G.frd.xyz, G.frd.n_elems, A[0], A[1], A[2], B[0], B[1], B[2]);
    if (strcmp(wk, where)) { line_locate(A, B, LIN_N, el, w); snprintf(where, sizeof where, "%s", wk); }
    for (int i = 0; i < LIN_N; i++) {
        float* o = G.lin_s + 6 * i;
        uint32_t nd = i == 0 ? G.lin_a : i == LIN_N - 1 ? G.lin_b : UINT32_MAX;   /* an end on a node: its own value */
        if (line_interp(el[i], w + 20 * i, S, 6, o)) continue;
        for (int c = 0; c < 6; c++) o[c] = nd < G.frd.n_nodes ? S[6 * (size_t)nd + c] : NAN;
    }
    G.lin_n = LIN_N;
    free(tv);
}

void app_lin_open(const float A[3], const float B[3], uint32_t na, uint32_t nb) {
    G.lin_open = true;
    memcpy(G.lin_p[0], A, sizeof G.lin_p[0]); memcpy(G.lin_p[1], B, sizeof G.lin_p[1]);
    G.lin_a = na; G.lin_b = nb;
    G.lin_key[0] = 0;
    refresh_lin();
    refresh_path();
}

void app_lin_close(void) {
    free(G.lin_s); G.lin_s = NULL; G.lin_n = 0;
    bool was = G.lin_open;
    G.lin_open = false; G.lin_key[0] = 0;
    if (was) refresh_path();
}

bool app_lin_mb(double m[6], double b[6]) {
    if (!G.lin_n || !cv_linearize(G.lin_s, G.lin_n, G.lin_t, m, b)) return false;
    if (!G.lin_asme) return true;
    float d[3] = { G.lin_p[1][0] - G.lin_p[0][0], G.lin_p[1][1] - G.lin_p[0][1], G.lin_p[1][2] - G.lin_p[0][2] };
    if (G.csys > 0) {                        /* the samples are in cylindrical components: the line's direction too, at its middle */
        float mid[3], Q[3][3], l[3];
        for (int k = 0; k < 3; k++) mid[k] = 0.5f * (G.lin_p[0][k] + G.lin_p[1][k]);
        cv_cyl_basis(mid, G.csys_o, G.csys - 1, Q);
        for (int a = 0; a < 3; a++) l[a] = Q[a][0] * d[0] + Q[a][1] * d[1] + Q[a][2] * d[2];
        memcpy(d, l, sizeof d);
    }
    cv_bend_mask(b, d);
    return true;
}

bool app_lin_csv(const char* path) {
    if (!G.lin_n || G.lin_fi < 0) return false;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[G.lin_fi];
    double m[6], b[6];
    bool ok = app_lin_mb(m, b);
    FILE* o = fopen(path, "wb");
    if (!o) return false;
    fprintf(o, "# %s linearized from (%.9g, %.9g, %.9g) to (%.9g, %.9g, %.9g), t = %.9g, step %d\n", d->name,
            G.lin_p[0][0], G.lin_p[0][1], G.lin_p[0][2], G.lin_p[1][0], G.lin_p[1][1], G.lin_p[1][2], G.lin_t, G.step + 1);
    if (ok) {
        double mb[6], mb2[6];
        for (int c = 0; c < 6; c++) { mb[c] = m[c] + b[c]; mb2[c] = m[c] - b[c]; }
        fprintf(o, "# bending from %s\n", G.lin_asme ? "the components normal to the line only (5-A.4.1.2)" : "all six components");
        fprintf(o, "# membrane: von Mises %.9g, Tresca %.9g\n", cv_mises6(m), cv_tresca6(m, false));
        fprintf(o, "# membrane + bending at start: von Mises %.9g, Tresca %.9g\n", cv_mises6(mb), cv_tresca6(mb, false));
        fprintf(o, "# membrane + bending at end: von Mises %.9g, Tresca %.9g\n", cv_mises6(mb2), cv_tresca6(mb2, false));
        double pk[2] = { -INFINITY, -INFINITY }, tt[2] = { -INFINITY, -INFINITY };   /* the largest anywhere on the line */
        for (int i = 0; i < G.lin_n; i++) {
            double l[6], s[6], p[6];
            cv_lin_at(m, b, G.lin_t, G.lin_t * i / (G.lin_n - 1), l);
            for (int c = 0; c < 6; c++) { s[c] = G.lin_s[6 * i + c]; p[c] = s[c] - l[c]; }
            pk[0] = CV_MAX(pk[0], cv_mises6(p)); pk[1] = CV_MAX(pk[1], cv_tresca6(p, false));
            tt[0] = CV_MAX(tt[0], cv_mises6(s)); tt[1] = CV_MAX(tt[1], cv_tresca6(s, false));
        }
        fprintf(o, "# peak, max on the line: von Mises %.9g, Tresca %.9g\n", pk[0], pk[1]);
        fprintf(o, "# total, max on the line: von Mises %.9g, Tresca %.9g\n", tt[0], tt[1]);
    } else {
        fprintf(o, "# the line leaves the solid: no linearization\n");
    }
    fprintf(o, "x");
    for (int c = 0; c < 6; c++) fprintf(o, ",%s", d->comp[c]);
    for (int c = 0; c < 6; c++) fprintf(o, ",%s_lin", d->comp[c]);
    fprintf(o, ",mises,mises_lin\n");
    for (int i = 0; i < G.lin_n; i++) {
        double x = G.lin_t * i / (G.lin_n - 1), s[6], l[6];
        const float* v = G.lin_s + 6 * i;
        fprintf(o, "%.9g", x);
        if (ok) cv_lin_at(m, b, G.lin_t, x, l); else for (int c = 0; c < 6; c++) l[c] = NAN;
        for (int c = 0; c < 6; c++) { s[c] = v[c]; fprintf(o, ",%.9g", v[c]); }
        for (int c = 0; c < 6; c++) fprintf(o, ",%.9g", l[c]);
        fprintf(o, ",%.9g,%.9g\n", cv_mises6(s), ok ? cv_mises6(l) : NAN);
    }
    return fclose(o) == 0;
}

/* ---- clip caps: the plane cut through the solid elements, filled ---------------------
   Each solid is split into tetrahedra over its corners; a tet crossing the plane
   gives one or two triangles. Vertices carry the undeformed position (with the
   DISPI part baked in), DISP and the value, interpolated along the cut edges, so
   the shader's deformation puts them exactly on the plane; a hair inside it, so the
   clip does not discard them. */

typedef struct { cv_fvec pos, disp, val; float n[3], eps; bool has_disp; } cap_out;

static void cap_vertex(cap_out* o, const float X[][3], const float U[][3], const float* S, int i, int j, float t) {
    for (int k = 0; k < 3; k++) cv_push(o->pos, X[i][k] + t * (X[j][k] - X[i][k]) - o->eps * o->n[k]);
    if (o->has_disp) for (int k = 0; k < 3; k++) cv_push(o->disp, U[i][k] + t * (U[j][k] - U[i][k]));
    cv_push(o->val, S[i] + t * (S[j] - S[i]));
}

static void cap_tet(cap_out* o, const int v[4], const float* sd, const float X[][3], const float U[][3], const float* S) {
    int in[4], out[4], ni = 0, no = 0;
    for (int k = 0; k < 4; k++) { if (sd[v[k]] > 0) out[no++] = v[k]; else in[ni++] = v[k]; }
    if (!ni || !no) return;
    #define CUT(a, b) cap_vertex(o, X, U, S, a, b, sd[a] / (sd[a] - sd[b]))
    if (no == 1) { CUT(out[0], in[0]); CUT(out[0], in[1]); CUT(out[0], in[2]); }
    else if (ni == 1) { CUT(in[0], out[0]); CUT(in[0], out[1]); CUT(in[0], out[2]); }
    else {                                              /* a quad: (a,c) (a,d) (b,d) (b,c) */
        CUT(out[0], in[0]); CUT(out[0], in[1]); CUT(out[1], in[1]);
        CUT(out[0], in[0]); CUT(out[1], in[1]); CUT(out[1], in[0]);
    }
    #undef CUT
}

void app_clip_caps(bool on, const float n[3], float dd, float f1, float f2) {
    static char key[256];
    char k[256];
    on = on && G.clip_cap && G.loaded && G.frd.n_elems <= 4000000;
    snprintf(k, sizeof k, "%d|%g|%g|%g|%g|%g|%g|%u|%p|%zu|%d|%d|%d", on, n[0], n[1], n[2], dd, f1, f2, G.field_gen,
             (void*)G.skin.tri, G.skin.n_tri, G.elem_mode, G.has_field, G.field_src);
    if (!strcmp(k, key)) return;
    snprintf(key, sizeof key, "%s", k);
    cap_out o = { .n = { n[0], n[1], n[2] }, .eps = 1e-5f * G.diag, .has_disp = G.disp != NULL };
    static const int hex[6][4] = { { 0, 1, 2, 6 }, { 0, 2, 3, 6 }, { 0, 3, 7, 6 }, { 0, 7, 4, 6 }, { 0, 4, 5, 6 }, { 0, 5, 1, 6 } };
    static const int wedge[3][4] = { { 0, 1, 2, 3 }, { 1, 2, 3, 4 }, { 2, 3, 4, 5 } };
    static const int tet[1][4] = { { 0, 1, 2, 3 } };
    bool nodal = G.has_field && G.field_src == 0 && !G.elem_mode && G.scalar;
    bool elem = G.has_field && G.field_src == 0 && G.elem_mode && G.elem_val;
    for (uint32_t e = 0; on && e < G.frd.n_elems; e++) {
        if (G.vis && !G.vis[e]) continue;
        int t = G.frd.etype[e], nc = t == 1 || t == 4 ? 8 : t == 2 || t == 5 ? 6 : t == 3 || t == 6 ? 4 : 0;
        uint32_t b = G.frd.eoff[e];
        if (!nc || G.frd.eoff[e + 1] - b < (uint32_t)nc) continue;
        float X[8][3], U[8][3], S[8], sd[8];
        int pos = 0, neg = 0;
        for (int i = 0; i < nc; i++) {
            uint32_t nd = G.frd.conn[b + i];
            const float* x = G.frd.xyz + 3 * (size_t)nd;
            float p = 0;
            for (int c = 0; c < 3; c++) {
                X[i][c] = x[c] + (G.disp2 ? f2 * G.disp2[3 * (size_t)nd + c] : 0.f);
                U[i][c] = G.disp ? G.disp[3 * (size_t)nd + c] : 0.f;
                p += n[c] * (X[i][c] + f1 * U[i][c]);
            }
            sd[i] = p - dd;
            S[i] = nodal ? G.scalar[nd] : elem ? G.elem_val[e] : 0.f;
            if (sd[i] > 0) pos++; else neg++;
        }
        if (!pos || !neg) continue;
        const int (*tt)[4] = nc == 8 ? hex : nc == 6 ? wedge : tet;
        int nt = nc == 8 ? 6 : nc == 6 ? 3 : 1;
        for (int q = 0; q < nt; q++) cap_tet(&o, tt[q], sd, X, U, S);
    }
    cv_render_aux(CV_AUX_CAPTRI, o.pos.a, o.has_disp ? o.disp.a : NULL, o.val.a, (uint32_t)(o.pos.n / 3));
    cv_free_vec(o.pos); cv_free_vec(o.disp); cv_free_vec(o.val);
}

static void path_free(void) {
    free(G.path_nodes); free(G.path_dist); free(G.path_el); free(G.path_w);
    G.path_nodes = NULL; G.path_dist = NULL; G.path_el = NULL; G.path_w = NULL; G.path_n = 0;
}

void app_path_clear(void) {
    path_free();
    app_lin_close();
    G.path_a = UINT32_MAX; G.path_arm = false; G.path_open = false;
    refresh_path();
}

void app_path_start(uint32_t node) {
    app_path_clear();
    G.path_a = node;
    G.path_arm = true;
}

void app_pick_cancel(void) {
    if (G.path_arm) { G.path_arm = false; G.path_a = UINT32_MAX; }
}

/* the nearest node that has skin edges: a mid-edge node of a quadratic
   element (the usual click target) is not part of the edge graph */
static uint32_t snap_to_edges(uint32_t n) {
    if (n >= G.frd.n_nodes) return n;
    uint8_t* has = calloc(G.frd.n_nodes, 1);
    if (!has) return n;
    for (size_t i = 0; i < 2 * G.skin.n_edge; i++) has[G.skin.edge[i]] = 1;
    uint32_t best = n;
    if (!has[n]) {
        const float* p = G.frd.xyz + 3 * n;
        float bd = INFINITY;
        for (size_t i = 0; i < G.skin.n_pt; i++) {
            uint32_t m = G.skin.pt[i];
            if (!has[m]) continue;
            const float* q = G.frd.xyz + 3 * m;
            float d = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]);
            if (d < bd) { bd = d; best = m; }
        }
    }
    free(has);
    return best;
}

#define PATH_SN 121                                 /* samples on a straight path */

/* a skin triangle's normal times twice its area (undeformed) */
static void tri_normal(size_t t, double s[3]) {
    const uint32_t* v = G.skin.tri + 3 * t;
    const float *a = G.frd.xyz + 3 * (size_t)v[0], *b = G.frd.xyz + 3 * (size_t)v[1], *c = G.frd.xyz + 3 * (size_t)v[2];
    double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, w[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    s[0] = u[1] * w[2] - u[2] * w[1]; s[1] = u[2] * w[0] - u[0] * w[2]; s[2] = u[0] * w[1] - u[1] * w[0];
}

/* the surface normals at a skin node, one per face through it (the sign does not
   matter: path_ray tries both ways): the area-weighted mean of the skin
   triangles around the node (the nearest corner node for a mid-edge one) that
   lie on one smooth surface, so a faceted curve gives its true normal. With a
   clicked triangle, the one surface it is on: the triangles within 30 degrees
   of it. Without, the triangles grouped by direction, so a node on an edge (a
   model cut at a symmetry plane, say) gives each face's normal and not the
   bisector. */
#define NRM_MAX 6
static int node_normals(uint32_t n, uint32_t tri, float d[NRM_MAX][3]) {
    double s[3], l;
    if (tri < G.skin.n_tri) {
        double c[3], cl;
        tri_normal(tri, c);
        if ((cl = sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2])) > 0) {
            for (int pass = 0; pass < 2; pass++, n = snap_to_edges(n)) {
                double m[3] = { 0, 0, 0 };
                for (size_t t = 0; t < G.skin.n_tri; t++) {
                    const uint32_t* v = G.skin.tri + 3 * t;
                    if (v[0] != n && v[1] != n && v[2] != n) continue;
                    tri_normal(t, s);
                    l = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
                    if (l > 0 && (s[0] * c[0] + s[1] * c[1] + s[2] * c[2]) / (l * cl) > 0.866)
                        for (int k = 0; k < 3; k++) m[k] += s[k];
                }
                if ((l = sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2])) > 0) { for (int k = 0; k < 3; k++) d[0][k] = (float)(m[k] / l); return 1; }
            }
            for (int k = 0; k < 3; k++) d[0][k] = (float)(c[k] / cl);   /* the node is not on it: the face alone */
            return 1;
        }
    }
    for (int pass = 0; pass < 2; pass++, n = snap_to_edges(n)) {
        double g[NRM_MAX][3] = { { 0 } };
        int ng = 0;
        for (size_t t = 0; t < G.skin.n_tri; t++) {
            const uint32_t* v = G.skin.tri + 3 * t;
            if (v[0] != n && v[1] != n && v[2] != n) continue;
            tri_normal(t, s);
            if (!((l = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2])) > 0)) continue;
            int j = 0;
            for (; j < ng; j++) {                           /* the group within ~37 degrees */
                double gl = sqrt(g[j][0] * g[j][0] + g[j][1] * g[j][1] + g[j][2] * g[j][2]);
                if ((s[0] * g[j][0] + s[1] * g[j][1] + s[2] * g[j][2]) / (l * gl) > 0.8) break;
            }
            if (j == ng) { if (ng == NRM_MAX) continue; ng++; }
            for (int k = 0; k < 3; k++) g[j][k] += s[k];
        }
        for (int j = 0; j < ng; j++) {
            l = sqrt(g[j][0] * g[j][0] + g[j][1] * g[j][1] + g[j][2] * g[j][2]);
            for (int k = 0; k < 3; k++) d[j][k] = (float)(g[j][k] / l);
        }
        if (ng) return ng;
    }
    return 0;
}

/* from p along d to where the line first leaves the solid: the nearest skin
   triangle it crosses beyond p (not the faces at p itself), if the stretch up to
   it is solid (its middle is in an element). Exact, so a wall thin against the
   model is not missed. */
static bool ray_exit(const float p[3], const float d[3], float out[3], float* len, size_t* hit) {
    double best = INFINITY, eps = 1e-4 * G.diag;
    for (size_t t = 0; t < G.skin.n_tri; t++) {         /* Moller-Trumbore */
        const uint32_t* v = G.skin.tri + 3 * t;
        const float *a = G.frd.xyz + 3 * (size_t)v[0], *b = G.frd.xyz + 3 * (size_t)v[1], *c = G.frd.xyz + 3 * (size_t)v[2];
        double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        double h[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
        double det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
        if (fabs(det) < 1e-12 * (e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2])) continue;   /* along the face */
        double f = 1 / det, sv[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
        double u = f * (sv[0] * h[0] + sv[1] * h[1] + sv[2] * h[2]);
        if (u < -1e-6 || u > 1 + 1e-6) continue;
        double q[3] = { sv[1] * e1[2] - sv[2] * e1[1], sv[2] * e1[0] - sv[0] * e1[2], sv[0] * e1[1] - sv[1] * e1[0] };
        double w = f * (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]);
        if (w < -1e-6 || u + w > 1 + 1e-6) continue;
        double tt = f * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]);
        if (tt > eps && tt < best) { best = tt; *hit = t; }
    }
    if (!(best < INFINITY)) return false;
    float B[3], el_w[3 * 20];
    uint32_t el[3];
    for (int k = 0; k < 3; k++) B[k] = p[k] + (float)best * d[k];
    line_locate(p, B, 3, el, el_w);                     /* the middle: solid, or the way out */
    if (el[1] == UINT32_MAX) return false;
    memcpy(out, B, sizeof B);
    *len = (float)best;
    return true;
}

/* the far end of a ray from node n: along the normal or an axis, whichever way
   goes into the solid. With several faces at the node (an edge), the normal
   that goes straight through a wall, coming out of a face parallel to the one
   it went in by, the shortest of those; else the shortest. */
static bool path_ray(uint32_t n, int dir, float out[3]) {
    float d[NRM_MAX][3] = { { 0 } };
    int nd = 1;
    if (dir == 1) { if (!(nd = node_normals(n, G.path_tri, d))) return false; }
    else d[0][CV_MIN(dir - 2, 2)] = 1;
    const float* p = G.frd.xyz + 3 * (size_t)n;
    float best = INFINITY;
    bool best_wall = false;
    for (int j = 0; j < nd; j++) {
        float q[2][3], len[2] = { 0, 0 };
        size_t hit[2] = { 0, 0 };
        for (int s = 0; s < 2; s++) {                   /* both ways: the one into the solid */
            if (s) for (int k = 0; k < 3; k++) d[j][k] = -d[j][k];
            if (!ray_exit(p, d[j], q[s], &len[s], &hit[s])) len[s] = 0;
        }
        int s = len[1] > len[0];
        if (!(len[s] > 0)) continue;                    /* not into the solid */
        double hn[3];
        tri_normal(hit[s], hn);
        double hl = sqrt(hn[0] * hn[0] + hn[1] * hn[1] + hn[2] * hn[2]);
        bool wall = dir != 1 || (hl > 0 && fabs(hn[0] * d[j][0] + hn[1] * d[j][1] + hn[2] * d[j][2]) / hl > 0.9);
        if (best_wall && !wall) continue;
        if (wall == best_wall && len[s] >= best) continue;
        best = len[s]; best_wall = wall;
        float back = 1e-5f * G.diag / len[s];           /* a hair back in: the last sample is found inside */
        for (int k = 0; k < 3; k++) out[k] = q[s][k] - back * (q[s][k] - p[k]);
    }
    return best < INFINITY;
}

/* the path between path_end[0] and [1]: straight, sampled in the elements, or
   over the surface edges. The straight line between the two is linearized
   either way (a stress classification line is straight). */
void app_path_rebuild(void) {
    path_free();
    G.path_gen++;
    uint32_t a = G.path_end[0], b = G.path_end[1];
    memcpy(G.path_p[0], G.frd.xyz + 3 * (size_t)a, sizeof G.path_p[0]);
    if (G.path_dir) {
        static const char* dn[] = { "", "the normal", "X", "Y", "Z" };
        G.path_surface = false;
        G.path_end[1] = b = UINT32_MAX;
        if (!path_ray(a, G.path_dir, G.path_p[1])) {
            char m[120];
            snprintf(m, sizeof m, "no solid along %s from node %u", dn[CV_MIN(G.path_dir, 4)], G.frd.node_id[a]);
            cv_msg_add(&G.msgs, 0, false, m);
            app_lin_close();
            refresh_path();
            return;
        }
    } else memcpy(G.path_p[1], G.frd.xyz + 3 * (size_t)b, sizeof G.path_p[1]);
    if (G.path_surface) {
        a = snap_to_edges(a); b = snap_to_edges(b);
        if (a == b || !cv_path_find(&G.frd, &G.skin, a, b, &G.path_nodes, &G.path_n, &G.path_dist)) {
            path_free();
            cv_msg_add(&G.msgs, 0, false, "no surface path between the two nodes: back to the straight line");
            G.path_surface = false;
            app_path_rebuild();
            return;
        }
        app_lin_open(G.path_p[0], G.path_p[1], G.path_end[0], G.path_end[1]);
    } else {
        G.path_dist = malloc(PATH_SN * sizeof(float));
        G.path_el = malloc(PATH_SN * sizeof(uint32_t));
        G.path_w = malloc(PATH_SN * 20 * sizeof(float));
        if (G.path_dist && G.path_el && G.path_w) {
            const float *A = G.path_p[0], *B = G.path_p[1];
            float L = sqrtf((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2]));
            line_locate(A, B, PATH_SN, G.path_el, G.path_w);
            for (int i = 0; i < PATH_SN; i++) G.path_dist[i] = L * i / (PATH_SN - 1);
            G.path_n = PATH_SN;
            app_lin_open(A, B, a, b);
        } else path_free();
    }
    refresh_path();
}

void app_path_end(uint32_t node) {
    G.path_arm = false;
    if (G.path_a == UINT32_MAX || node == G.path_a) return;
    G.path_end[0] = G.path_a; G.path_end[1] = G.path_to = node;
    G.path_tri = G.probe_on && G.probe.hit && G.probe.node == G.path_a ? G.probe.tri : UINT32_MAX;
    G.path_dir = 0;
    app_path_rebuild();
    G.path_open = G.path_n > 0;
}

void app_path_ray(uint32_t node, int dir) {
    uint32_t tri = G.probe_on && G.probe.hit && G.probe.node == node ? G.probe.tri : UINT32_MAX;
    app_path_clear();
    if (node >= G.frd.n_nodes) return;
    G.path_tri = tri;
    G.path_dir = CV_MAX(1, CV_MIN(dir, 4));
    G.path_end[0] = node;
    app_path_rebuild();
    G.path_open = G.path_n > 0;
}

float app_path_value(uint32_t i) {
    if (!G.scalar || i >= G.path_n) return NAN;
    if (G.path_surface) return G.path_nodes ? G.scalar[G.path_nodes[i]] : NAN;
    float v;
    return G.path_el && line_interp(G.path_el[i], G.path_w + 20 * i, G.scalar, 1, &v) ? v : NAN;
}

bool app_path_csv(const char* path) {
    if (!G.path_n) return false;
    FILE* o = fopen(path, "wb");
    if (!o) return false;
    fprintf(o, "distance,id,x,y,z,%s\n", G.has_field ? G.field_label : "value");
    const float *A = G.path_p[0], *B = G.path_p[1];
    for (uint32_t i = 0; i < G.path_n; i++) {
        float p[3], f = G.path_n > 1 ? (float)i / (G.path_n - 1) : 0;
        uint32_t id = 0;
        if (G.path_surface) { memcpy(p, G.frd.xyz + 3 * (size_t)G.path_nodes[i], sizeof p); id = G.frd.node_id[G.path_nodes[i]]; }
        else for (int k = 0; k < 3; k++) p[k] = A[k] + f * (B[k] - A[k]);
        float v = G.has_field && !G.elem_mode ? app_path_value(i) : NAN;
        if (id) fprintf(o, "%.9g,%u,%.9g,%.9g,%.9g,", G.path_dist[i], id, p[0], p[1], p[2]);
        else fprintf(o, "%.9g,,%.9g,%.9g,%.9g,", G.path_dist[i], p[0], p[1], p[2]);
        if (v == v) fprintf(o, "%.9g\n", v); else fprintf(o, "nan\n");
    }
    return fclose(o) == 0;
}

/* ---- comparison file ------------------------------------------------------------- */

void app_compare_close(void) {
    if (G.cmp_on) { cv_frd_free(&G.cmp); free(G.cmp.msgs.a); memset(&G.cmp.msgs, 0, sizeof G.cmp.msgs); cv_map_close(&G.cmp_map); }
    deck_compare_close();
    G.cmp_on = false; G.diff_mode = false; G.cmp_path[0] = 0;
}

bool app_compare_open(const char* path) {
    app_compare_close();
    if (!G.loaded) return false;
    if (!cv_map_open(&G.cmp_map, path)) { cv_msg_add(&G.msgs, 0, false, "compare: cannot open the file"); return false; }
    if (!cv_frd_parse(&G.cmp, G.cmp_map.data, G.cmp_map.size)) { cv_map_close(&G.cmp_map); cv_msg_add(&G.msgs, 0, false, "compare: out of memory"); return false; }
    bool same = G.cmp.n_nodes == G.frd.n_nodes &&
                memcmp(G.cmp.node_id, G.frd.node_id, (size_t)G.frd.n_nodes * sizeof(uint32_t)) == 0;
    if (!same) {
        cv_frd_free(&G.cmp); free(G.cmp.msgs.a); memset(&G.cmp.msgs, 0, sizeof G.cmp.msgs); cv_map_close(&G.cmp_map);
        cv_msg_add(&G.msgs, 0, false, "compare: the other file has different nodes; both runs need the same mesh");
        return false;
    }
    snprintf(G.cmp_path, sizeof G.cmp_path, "%s", path);
    deck_compare_open(path);
    G.cmp_on = true;
    G.diff_mode = true;
    refresh_field();
    return true;
}

/* Gauss points: real values for a .dat field, interpolated nodal values otherwise */
void refresh_gauss(void) {
    if (G.field_src == 1) gp_refresh_geometry();
    else gp_build_nodal();
}

void app_gauss_changed(void) { refresh_gauss(); }

/* A - B: the same field and component of the comparison file at the same step
   index, subtracted node by node. Decoded on demand, not cached: a comparison
   is occasional. */
static bool subtract_compare(const cv_field_desc* d) {
    if (!G.diff_mode || !G.cmp_on || G.cmp.n_steps == 0) return false;
    int st = CV_MIN(G.step, G.cmp.n_steps - 1), bi = -1;
    for (int i = 0; i < G.cmp.steps[st].nfields; i++) if (!strcmp(G.cmp.steps[st].fields[i].name, d->name)) bi = i;
    if (bi < 0) return false;
    const cv_field_desc* db = &G.cmp.steps[st].fields[bi];
    float* vb = malloc((size_t)CV_MAX(G.cmp.n_nodes, 1) * (size_t)db->ncomp * sizeof(float));
    float* sb = malloc((size_t)CV_MAX(G.cmp.n_nodes, 1) * sizeof(float));
    if (!vb || !sb) { free(vb); free(sb); return false; }
    cv_frd_read_field(&G.cmp, db, vb, NULL);
    deck_compare_localize(&G.cmp, st, db, vb);
    if (G.csys > 0 && G.comp >= 0 && cv_cyl_applies(db)) cv_cyl_values(db, G.cmp.xyz, G.cmp.n_nodes, G.csys - 1, G.csys_o, vb);
    cv_field_scalar(vb, db->ncomp, G.cmp.n_nodes, G.comp, sb);
    for (uint32_t i = 0; i < G.frd.n_nodes; i++) G.scalar[i] -= sb[i];
    free(vb); free(sb);
    return true;
}

void refresh_field(void) {
    if (G.field_src == 1) { refresh_field_dat(); return; }
    int fi = find_field(G.step, G.field_name);
    G.has_field = false;
    if (fi >= 0) {
        const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
        const float* vals = cache_get(G.step, fi);
        if (vals && G.scalar && G.elem_val) {
            CV_ASSERT(d->ncomp >= 1 && d->ncomp <= CV_MAX_COMP);
            CV_ASSERT(G.comp < d->ncomp);
            float* tv = G.comp >= 0 ? to_csys(d, &G.frd, vals) : NULL;   /* invariants need no turning */
            cv_field_scalar(tv ? tv : vals, d->ncomp, G.frd.n_nodes, G.comp, G.scalar);
            free(tv);
            bool diff = subtract_compare(d);
            cv_elem_mean(&G.frd, G.scalar, G.elem_val);
            G.has_field = true;
            snprintf(G.field_label, sizeof G.field_label, "%s%s %s", diff ? "A-B " : "", d->name, opt_label(d, G.comp));
            add_unit(G.field_label, sizeof G.field_label, d->name, G.comp);
        }
    }
    if (!G.has_field) {
        if (G.field_name[0]) snprintf(G.field_label, sizeof G.field_label, "%s (not in this step)", G.field_name);
        else G.field_label[0] = 0;
        cv_render_scalar(NULL, 0);
        refresh_markers();
    } else {
        cv_render_scalar(G.scalar, G.frd.n_nodes);
        app_refresh_range();
    }
    G.field_gen++;
    refresh_tri_values();
    refresh_gauss();
    refresh_vectors();
    refresh_lin();
    refresh_path();
    refresh_hist();
}

/* The imaginary part of a steady-state response: G.disp2 = -DISPI, so the shape at
   phase wt is DISP cos wt + disp2 sin wt. */
static void refresh_disp2(void) {
    int fi = find_field(G.step, "DISPI");
    const float* v = fi >= 0 ? cache_get(G.step, fi) : NULL;
    G.harmonic = false;
    if (!v || !G.disp || G.frd.steps[G.step].fields[fi].ncomp < 3) {
        free(G.disp2); G.disp2 = NULL;
        cv_render_displacement2(NULL, 0);
        return;
    }
    int nc = G.frd.steps[G.step].fields[fi].ncomp;
    if (!G.disp2) G.disp2 = malloc((size_t)G.frd.n_nodes * 3 * sizeof(float));
    if (!G.disp2) { cv_render_displacement2(NULL, 0); return; }
    for (uint32_t i = 0; i < G.frd.n_nodes; i++)
        for (int k = 0; k < 3; k++) {
            float x = v[(size_t)i * nc + k];
            G.disp2[3 * i + k] = (x == x && !isinf(x)) ? -x : 0.f;
        }
    cv_render_displacement2(G.disp2, G.frd.n_nodes);
    G.harmonic = true;
}

static void refresh_disp(void) {
    G.field_gen++;
    int fi = find_field(G.step, "DISP");
    const float* v = fi >= 0 ? cache_get(G.step, fi) : NULL;
    if (!v || G.frd.steps[G.step].fields[fi].ncomp < 3) {
        free(G.disp); G.disp = NULL;
        cv_render_displacement(NULL, 0);
        refresh_disp2();
        app_rep_refresh();
        return;
    }
    int nc = G.frd.steps[G.step].fields[fi].ncomp;
    if (!G.disp) G.disp = malloc((size_t)G.frd.n_nodes * 3 * sizeof(float));
    if (!G.disp) { cv_render_displacement(NULL, 0); return; }
    for (uint32_t i = 0; i < G.frd.n_nodes; i++)
        for (int k = 0; k < 3; k++) {
            float x = v[(size_t)i * nc + k];
            G.disp[3 * i + k] = (x == x && !isinf(x)) ? x : 0.f;   /* no data = no motion */
        }
    cv_render_displacement(G.disp, G.frd.n_nodes);
    refresh_disp2();
    app_rep_refresh();
    /* A mode shape's amplitude is arbitrary (CalculiX mass-normalises it), so in
       auto mode each modal increment is scaled on its own to ~10% of the model;
       every other increment keeps the one true-scale factor. */
    if (G.deform_auto) {
        if (G.frd.steps[G.step].modal) {
            float peak = 0;
            for (uint32_t i = 0; i < G.frd.n_nodes; i++) {
                const float* d = G.disp + 3 * i;
                float m = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (m > peak) peak = m;
            }
            G.deform_scale = peak > 0 ? 0.1f * G.diag / peak : 1.f;
        } else {
            G.deform_scale = G.auto_scale;
        }
    }
}


void app_set_step(int s) {
    if (!G.loaded || G.frd.n_steps == 0) return;
    if (s < 0) s = 0;
    if (s >= G.frd.n_steps) s = G.frd.n_steps - 1;
    G.step = s;
    refresh_disp();
    deck_refresh_highlight();             /* highlights move with the shape */
    view_bounds();                       /* depth range follows the moving shape */
    refresh_field();
    G.probe_on = false;
}

void app_select_src(const char* field, int comp, int src) {
    G.field_src = src;
    if (src == 1) G.show_gp = true;             /* a .dat field only lives on the points */
    app_select(field, comp);
}

void app_select(const char* field, int comp) {
    snprintf(G.field_name, sizeof G.field_name, "%s", field);
    G.comp = comp;
    G.range_lock = false;
    refresh_field();
}

void app_set_elem_mode(bool on) {
    G.elem_mode = on;
    refresh_field();
}

void app_colormap(int cm) {
    G.cmap = cm;
    cv_render_colormap(cm, G.legend_reverse, G.legend_grey);
}

void app_cmap_rgb(float t, float rgb[3]) {
    cv_colormap_rgb(G.cmap, G.legend_reverse ? 1.f - t : t, rgb);
    if (G.legend_grey) rgb[0] = rgb[1] = rgb[2] = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}

void app_legend_fmt(char* out, size_t n, double v) {
    if (v != v) { snprintf(out, n, "-"); return; }
    if (G.legend_fmt == 1) { snprintf(out, n, "%.*f", G.legend_decimals, v); return; }
    if (G.legend_fmt == 2) { snprintf(out, n, "%.*e", G.legend_decimals, v); return; }
    double a = fabs(v);
    if (a != 0 && (a < 1e-3 || a >= 1e5)) snprintf(out, n, "%.3e", v);
    else snprintf(out, n, "%.4g", v);
}

