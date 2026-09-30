/* app_field.c -- decoded-field cache, colourings, displacement and the current
   step: everything that turns .frd values into what the renderer draws. */
#include "app_int.h"
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
static const char* opt_label(const cv_field_desc* d, int comp) {
    static cv_scalar_opt opts[CV_MAX_OPTS];
    int n = cv_field_options(d, opts, CV_MAX_OPTS);
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
    return fi >= 0 && G.frd.steps[G.step].fields[fi].ncomp == 3;
}

void refresh_vectors(void) {
    if (!G.show_vec || !app_field_is_vector() || !G.has_field) { cv_render_aux(CV_AUX_VECLN, NULL, NULL, NULL, 0); return; }
    int fi = find_field(G.step, G.field_name);
    const float* v = cache_get(G.step, fi);
    if (!v) { cv_render_aux(CV_AUX_VECLN, NULL, NULL, NULL, 0); return; }
    const uint32_t* ids = G.skin.n_pt ? G.skin.pt : NULL;
    size_t n = ids ? G.skin.n_pt : G.frd.n_nodes;
    size_t stride = n / 200000 + 1;                     /* huge models: a sample */
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
        const float z[3] = { 0, 0, 0 };
        size_t before = pos.n;
        deck_arrow(&pos, &disp, tip, dir, len, G.disp ? G.disp + 3 * i : z, false);
        float sv = G.scalar ? G.scalar[i] : NAN;
        for (size_t k = before; k < pos.n; k += 3) cv_push(scal, sv);
    }
    cv_render_aux(CV_AUX_VECLN, pos.a, disp.a, scal.a, (uint32_t)(pos.n / 3));
    cv_free_vec(pos); cv_free_vec(disp); cv_free_vec(scal);
}

void app_vectors_changed(void) { refresh_vectors(); }

/* ---- path plot ------------------------------------------------------------------- */

void refresh_path(void) {
    if (!G.path_n || !G.loaded) { cv_render_aux(CV_AUX_PATHLN, NULL, NULL, NULL, 0); return; }
    cv_fvec pos = {0}, disp = {0};
    const float z[3] = { 0, 0, 0 };
    for (uint32_t i = 0; i + 1 < G.path_n; i++) {
        for (int e = 0; e < 2; e++) {
            uint32_t n = G.path_nodes[i + e];
            if (cv_reserve(pos, pos.n + 3) && cv_reserve(disp, disp.n + 3)) {
                memcpy(pos.a + pos.n, G.frd.xyz + 3 * n, 3 * sizeof(float)); pos.n += 3;
                memcpy(disp.a + disp.n, G.disp ? G.disp + 3 * n : z, 3 * sizeof(float)); disp.n += 3;
            }
        }
    }
    cv_render_aux(CV_AUX_PATHLN, pos.a, disp.a, NULL, (uint32_t)(pos.n / 3));
    cv_free_vec(pos); cv_free_vec(disp);
}

void app_path_clear(void) {
    free(G.path_nodes); free(G.path_dist);
    G.path_nodes = NULL; G.path_dist = NULL; G.path_n = 0;
    G.path_a = UINT32_MAX; G.path_arm = false; G.path_open = false;
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

void app_path_end(uint32_t node) {
    uint32_t a = snap_to_edges(G.path_a);
    node = snap_to_edges(node);
    G.path_arm = false;
    if (a == UINT32_MAX || node == a) return;
    if (!cv_path_find(&G.frd, &G.skin, a, node, &G.path_nodes, &G.path_n, &G.path_dist)) {
        cv_msg_add(&G.msgs, 0, false, "no surface path between the two nodes");
        return;
    }
    G.path_open = true;
    refresh_path();
}

bool app_path_csv(const char* path) {
    if (!G.path_n) return false;
    FILE* o = fopen(path, "w");
    if (!o) return false;
    fprintf(o, "distance,id,x,y,z,%s\n", G.has_field ? G.field_label : "value");
    for (uint32_t i = 0; i < G.path_n; i++) {
        uint32_t n = G.path_nodes[i];
        const float* p = G.frd.xyz + 3 * n;
        float v = G.has_field && !G.elem_mode ? G.scalar[n] : NAN;
        fprintf(o, "%.9g,%u,%.9g,%.9g,%.9g,", G.path_dist[i], G.frd.node_id[n], p[0], p[1], p[2]);
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
            cv_field_scalar(vals, d->ncomp, G.frd.n_nodes, G.comp, G.scalar);
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
    refresh_tri_values();
    refresh_gauss();
    refresh_vectors();
    refresh_path();
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

