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

/* the surface path, and the straight line between the ends (linearized) */
void refresh_path(void) {
    cv_fvec pos = {0}, disp = {0};
    if (G.loaded && G.path_n && G.path_surface && G.path_nodes)
        for (uint32_t i = 0; i + 1 < G.path_n; i++) { path_vertex(&pos, &disp, G.path_nodes[i]); path_vertex(&pos, &disp, G.path_nodes[i + 1]); }
    if (G.loaded && G.lin_open) { path_vertex(&pos, &disp, G.lin_a); path_vertex(&pos, &disp, G.lin_b); }
    else if (G.loaded && G.path_n && !G.path_surface) { path_vertex(&pos, &disp, G.path_end[0]); path_vertex(&pos, &disp, G.path_end[1]); }
    app_aux_upload(CV_AUX_PATHLN, &pos, &disp, NULL);
    cv_free_vec(pos); cv_free_vec(disp);
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
    FILE* o = fopen(path, "w");
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
    snprintf(key, sizeof key, "%d|%d|%d|%g|%g|%g|%u|%u", G.step, fi, G.csys, G.csys_o[0], G.csys_o[1], G.csys_o[2], G.lin_a, G.lin_b);
    if (!strcmp(key, G.lin_key)) return;
    snprintf(G.lin_key, sizeof G.lin_key, "%s", key);
    free(G.lin_s); G.lin_s = NULL; G.lin_n = 0;
    G.lin_fi = fi;
    const float *A = G.frd.xyz + 3 * (size_t)G.lin_a, *B = G.frd.xyz + 3 * (size_t)G.lin_b;
    G.lin_t = sqrtf((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2]));
    if (fi < 0) return;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
    const float* v = cache_get(G.step, fi);
    if (!v) return;
    float* tv = to_csys(d, &G.frd, v);
    const float* S = tv ? tv : v;
    static uint32_t el[LIN_N];
    static float w[LIN_N * 20];
    static char where[80];
    G.lin_s = malloc(LIN_N * 6 * sizeof(float));
    if (!G.lin_s) { free(tv); return; }
    char wk[80];
    snprintf(wk, sizeof wk, "%p|%u|%u|%u", (void*)G.frd.xyz, G.frd.n_elems, G.lin_a, G.lin_b);
    if (strcmp(wk, where)) { line_locate(A, B, LIN_N, el, w); snprintf(where, sizeof where, "%s", wk); }
    for (int i = 0; i < LIN_N; i++) {
        float* o = G.lin_s + 6 * i;
        if (!line_interp(el[i], w + 20 * i, S, 6, o)) for (int c = 0; c < 6; c++) o[c] = NAN;
    }
    G.lin_n = LIN_N;
    free(tv);
}

void app_lin_open(uint32_t a, uint32_t b) {
    G.lin_open = true;
    G.lin_a = a; G.lin_b = b;
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

bool app_lin_csv(const char* path) {
    if (!G.lin_n || G.lin_fi < 0) return false;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[G.lin_fi];
    double m[6], b[6];
    bool ok = cv_linearize(G.lin_s, G.lin_n, G.lin_t, m, b);
    FILE* o = fopen(path, "w");
    if (!o) return false;
    fprintf(o, "# %s linearized from node %u to node %u, t = %.9g, step %d\n", d->name, G.frd.node_id[G.lin_a],
            G.frd.node_id[G.lin_b], G.lin_t, G.step + 1);
    if (ok) {
        double mb[6], mb2[6];
        for (int c = 0; c < 6; c++) { mb[c] = m[c] + b[c]; mb2[c] = m[c] - b[c]; }
        fprintf(o, "# membrane: von Mises %.9g, Tresca %.9g\n", cv_mises6(m), cv_tresca6(m, false));
        fprintf(o, "# membrane + bending at start: von Mises %.9g, Tresca %.9g\n", cv_mises6(mb), cv_tresca6(mb, false));
        fprintf(o, "# membrane + bending at end: von Mises %.9g, Tresca %.9g\n", cv_mises6(mb2), cv_tresca6(mb2, false));
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
        for (int c = 0; c < 6; c++) { s[c] = v[c]; l[c] = ok ? m[c] + b[c] * (1 - 2 * x / G.lin_t) : NAN; fprintf(o, ",%.9g", v[c]); }
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
    bool had = G.path_n > 0;
    path_free();
    G.path_a = UINT32_MAX; G.path_arm = false; G.path_open = false;
    if (had) app_lin_close();                           /* the linearization belongs to the path */
    refresh_path();
}

void app_path_start(uint32_t node) {
    app_path_clear();
    G.path_a = node;
    G.path_arm = true;
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

/* the path between path_end[0] and [1]: straight, sampled in the elements, or
   over the surface edges. The straight line between the ends is linearized
   either way (the stress classification line is straight). */
void app_path_rebuild(void) {
    path_free();
    uint32_t a = G.path_end[0], b = G.path_end[1];
    if (G.path_surface) {
        a = snap_to_edges(a); b = snap_to_edges(b);
        if (a == b || !cv_path_find(&G.frd, &G.skin, a, b, &G.path_nodes, &G.path_n, &G.path_dist)) {
            path_free();
            cv_msg_add(&G.msgs, 0, false, "no surface path between the two nodes: back to the straight line");
            G.path_surface = false;
            app_path_rebuild();
            return;
        }
        app_lin_open(G.path_end[0], G.path_end[1]);
    } else {
        G.path_dist = malloc(PATH_SN * sizeof(float));
        G.path_el = malloc(PATH_SN * sizeof(uint32_t));
        G.path_w = malloc(PATH_SN * 20 * sizeof(float));
        if (G.path_dist && G.path_el && G.path_w) {
            const float *A = G.frd.xyz + 3 * (size_t)a, *B = G.frd.xyz + 3 * (size_t)b;
            float L = sqrtf((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2]));
            line_locate(A, B, PATH_SN, G.path_el, G.path_w);
            for (int i = 0; i < PATH_SN; i++) G.path_dist[i] = L * i / (PATH_SN - 1);
            G.path_n = PATH_SN;
            app_lin_open(a, b);
        } else path_free();
    }
    refresh_path();
}

void app_path_end(uint32_t node) {
    G.path_arm = false;
    if (G.path_a == UINT32_MAX || node == G.path_a) return;
    G.path_end[0] = G.path_a; G.path_end[1] = node;
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
    FILE* o = fopen(path, "w");
    if (!o) return false;
    fprintf(o, "distance,id,x,y,z,%s\n", G.has_field ? G.field_label : "value");
    const float *A = G.frd.xyz + 3 * (size_t)G.path_end[0], *B = G.frd.xyz + 3 * (size_t)G.path_end[1];
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

