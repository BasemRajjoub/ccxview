/* app_field.c -- decoded-field cache, colourings, displacement and the current
   step: everything that turns .frd values into what the renderer draws.
   The rest of the field's views live beside it: app_overlay.c (vector arrows,
   clip caps), app_path.c (path plot, history), app_linearize.c (stress
   linearization). */
#include "app_int.h"
#include "label.h"
#include "calc.h"
#include "gauss.h"
#include "path.h"
#include "app_fail.h"
#include "app_mesh.h"
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
    field_read(step, d, v, &G.msgs);
    deck_localize(step, d, v);
    units_apply(d->name, d->ncomp, v, G.frd.n_nodes);
    G.cache[slot] = (cv_cache_entry){ step, field, v, bytes, ++G.cache_clock };
    return v;
}

void field_read(int step, const cv_field_desc* d, float* out, cv_msgs* msgs) {
    if (shell_field(d)) shell_read(step, d, out, msgs);
    else cv_frd_read_field(&G.frd, d, out, msgs);
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
float* to_csys(const cv_field_desc* d, const cv_frd* f, const float* vals) {
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

/* ---- units: CalculiX has none, the user says which consistent set the model uses
   (G.units, or G.unit_in per quantity) and what each is shown in (G.unit_show). Values
   are converted once, as a field is decoded, so the legend, probe, plots, exports
   and formulas all see the shown units. The shape itself stays in model units. */

/* shown = file * k + off for component comp of field f */
static bool unit_conv(const char* f, int comp, double* k, double* off) {
    int q = cv_field_quantity(f, comp);
    *k = 1; *off = 0;
    return q >= 0 && cv_unit_conv(G.units, cv_sys_temp(G.units), G.unit_in[q], q, G.unit_show[q], k, off);
}

void units_apply(const char* f, int ncomp, float* v, size_t n) {
    double k[CV_MAX_COMP], off[CV_MAX_COMP];
    bool any = false;
    for (int c = 0; c < ncomp && c < CV_MAX_COMP; c++) any |= unit_conv(f, c, &k[c], &off[c]);
    if (!any) return;
    for (size_t i = 0; i < n; i++)
        for (int c = 0; c < ncomp && c < CV_MAX_COMP; c++) v[i * ncomp + c] = (float)(v[i * ncomp + c] * k[c] + off[c]);
}

/* the factor back from shown lengths to the model's: the shape is drawn in those */
float units_len_raw(void) {
    double k, off;
    return cv_unit_conv(G.units, cv_sys_temp(G.units), G.unit_in[CV_Q_LEN], CV_Q_LEN, G.unit_show[CV_Q_LEN], &k, &off) && k > 0
           ? (float)(1 / k) : 1.f;
}

const char* app_unit(const char* field, int comp) {
    int q = field ? cv_field_quantity(field, comp) : -1;
    const cv_unit* u = q >= 0 ? cv_unit_get(q, cv_unit_shown(G.units, cv_sys_temp(G.units), G.unit_in[q], q, G.unit_show[q])) : NULL;
    return u ? u->name : "";
}

void app_units_changed(void) {
    if (!G.loaded) return;
    cache_clear();
    G.hist_key[0] = G.lin_key[0] = 0;
    G.range_lock = false;
    app_set_step(G.step);                  /* decodes again: shape, field, plots */
}

/* the one-line label (probe, plots, exports): "STRESS von Mises [MPa]", and the
   legend's title in lines of its own: the field, the component, the unit. `head` is
   the field as the legend names it (shorter, for .dat blocks); NULL: as `title`. */
static void set_label(const char* title, const char* head, const char* kind, const char* field, int comp) {
    const char* u = field ? app_unit(field, comp) : "";
    snprintf(G.field_label, sizeof G.field_label, "%s%s%s%s%s%s", title, kind[0] ? " " : "", kind,
             u[0] ? " [" : "", u, u[0] ? "]" : "");
    snprintf(G.legend_lines[0], sizeof G.legend_lines[0], "%s", head ? head : title);
    snprintf(G.legend_lines[1], sizeof G.legend_lines[1], "%s", kind);
    snprintf(G.legend_lines[2], sizeof G.legend_lines[2], "%s%s%s", u[0] ? "[" : "", u, u[0] ? "]" : "");
}

int find_field(int step, const char* name) {
    if (step < 0 || step >= G.frd.n_steps) return -1;
    const cv_step* s = &G.frd.steps[step];
    for (int i = 0; i < s->nfields; i++)
        if (strcmp(s->fields[i].name, name) == 0) return i;
    return -1;
}

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
    bool want = G.elem_mode && G.field_src != 1;
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

/* the minmax_n smallest, then the largest, as balls */
void refresh_markers(void) {
    uint32_t cap = 2 * G.ext_n, n = 0;
    float* pos = malloc(CV_MAX(cap, 1) * 3 * sizeof *pos);
    float* disp = malloc(CV_MAX(cap, 1) * 3 * sizeof *disp);
    float* val = malloc(CV_MAX(cap, 1) * sizeof *val);
    const float* v = G.elem_mode ? G.elem_val : G.scalar;
    if (pos && disp && val && G.has_field && v)
        for (uint32_t i = 0; i < cap; i++) {
            uint32_t at = i < G.ext_n ? G.ext_lo[i] : G.ext_hi[i - G.ext_n];
            if (extreme_pos(at, pos + 3 * n, disp + 3 * n)) val[n++] = v[at];
        }
    cv_render_aux(CV_AUX_MARK, n ? pos : NULL, disp, val, n);
    free(pos); free(disp); free(val);
}

/* the minmax_n extremes among the shown values (the range's subset) */
static void refresh_extremes(const float* v, const uint32_t* ids, size_t n) {
    uint32_t k = (uint32_t)CV_MAX(1, CV_MIN(G.minmax_n, 100));
    uint32_t* lo = realloc(G.ext_lo, k * sizeof *lo);
    if (lo) G.ext_lo = lo;
    uint32_t* hi = realloc(G.ext_hi, k * sizeof *hi);
    if (hi) G.ext_hi = hi;
    G.ext_n = 0;
    if (!lo || !hi || G.field_src == 1) return;   /* Gauss point fields: no balls */
    uint32_t* sub = NULL;
    if (G.elem_mode && G.vis) {                     /* hidden elements are out of the range */
        sub = malloc(CV_MAX(n, 1) * sizeof *sub);
        if (!sub) return;
        size_t m = 0;
        for (size_t j = 0; j < n; j++) { uint32_t i = ids ? ids[j] : (uint32_t)j; if (G.vis[i]) sub[m++] = i; }
        ids = sub; n = m;
    }
    G.ext_n = cv_label_extremes(v, ids, (uint32_t)n, k, G.ext_lo, G.ext_hi);
    free(sub);
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
        if (G.elem_mode && G.field_src != 1 && G.vis && !G.vis[i]) continue;
        float x = v[i];
        if (x != x) { G.nan_count++; continue; }
        if (isinf(x)) continue;
        if (x < G.data_min) { G.data_min = x; G.min_at = i; }
        if (x > G.data_max) { G.data_max = x; G.max_at = i; }
    }
    if (G.min_at == UINT32_MAX) { G.data_min = 0; G.data_max = 1; }   /* nothing but NaN */
    refresh_extremes(v, ids, n);
    CV_ASSERT(G.min_at == UINT32_MAX || G.min_at < (G.field_src == 1 ? n : G.elem_mode ? G.frd.n_elems : G.frd.n_nodes));
    CV_ASSERT(G.data_min <= G.data_max);
    refresh_markers();
    if (app_label_on(CV_LABEL_MINMAX)) app_label_changed();
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
        char kind[64], head[64];
        snprintf(kind, sizeof kind, "%s (Gauss)", gp_desc(G.field_name, &d) ? opt_label(&d, G.comp) : "?");
        snprintf(head, sizeof head, "%s", G.field_name);
        char* cut = strstr(head, " (");             /* "stresses (elem, integ.pnt.,sxx,...)": stresses */
        if (cut) *cut = 0;
        set_label(G.field_name, head, kind, G.field_name, G.comp);
        app_refresh_range();
    } else {
        char t[96];
        snprintf(t, sizeof t, "%s (not in this increment)", G.field_name);
        set_label(t, NULL, "", NULL, 0);
    }
    refresh_tri_values();
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

/* ---- field / displacement refresh ----------------------------------------------- */

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
    units_apply(db->name, db->ncomp, vb, G.cmp.n_nodes);
    if (G.csys > 0 && G.comp >= 0 && cv_cyl_applies(db)) cv_cyl_values(db, G.cmp.xyz, G.cmp.n_nodes, G.csys - 1, G.csys_o, vb);
    cv_field_scalar(vb, db->ncomp, G.cmp.n_nodes, G.comp, sb);
    for (uint32_t i = 0; i < G.frd.n_nodes; i++) G.scalar[i] -= sb[i];
    free(vb); free(sb);
    return true;
}

/* ---- calculated field: a formula over the .frd fields (calc.h) -------------------- */

static const float* calc_get(void* ud, int step, int field) { return cache_get(step, field); }

/* the formula's values at every node of this step; false with the label saying why not */
static bool refresh_field_calc(void) {
    char t[96];
    snprintf(t, sizeof t, "= %s", G.calc_expr);
    if (!G.calc || !G.scalar || !G.elem_val) { set_label(t, NULL, "", NULL, 0); return false; }
    if (!cv_calc_eval(G.calc, &G.frd, G.step, calc_get, NULL, NULL, 0, G.scalar)) {
        const char* m = cv_calc_missing(G.calc);
        char why[64];
        if (m[0]) snprintf(why, sizeof why, "(%s not in this step)", m);
        else snprintf(why, sizeof why, "(out of memory)");
        set_label(t, NULL, why, NULL, 0);
        return false;
    }
    cv_elem_mean(&G.frd, G.scalar, G.elem_val);
    set_label(t, NULL, "", NULL, 0);
    return true;
}

bool app_calc_set(const char* expr) {
    if (!G.loaded) {                     /* kept for the next file */
        snprintf(G.calc_expr, sizeof G.calc_expr, "%s", expr);
        return false;
    }
    cv_calc* c = cv_calc_compile(&G.frd, expr, G.calc_err, sizeof G.calc_err);
    if (!c) return false;
    cv_calc_free(G.calc);
    G.calc = c;
    if (expr != G.calc_expr) snprintf(G.calc_expr, sizeof G.calc_expr, "%s", expr);
    G.field_src = 2;
    G.field_name[0] = 0;
    G.comp = 0;
    G.range_lock = false;
    refresh_field();
    return true;
}

/* ---- failure criteria over the stresses (app_fail.c) ---------------------------------- */

static bool refresh_field_fail(void) {
    char t[64], why[128], kind[64];
    snprintf(t, sizeof t, "Failure %s", cv_fc_title(G.fail_crit));
    bool ok = fail_eval_field(why, sizeof why);
    snprintf(kind, sizeof kind, "%s%s", fail_out_name(G.fail_out), G.fail_out == CV_FO_ANGLE ? " [deg]" : "");
    if (!ok) { set_label(t, NULL, why, NULL, 0); return false; }
    if (why[0]) {                                     /* shown, but some materials are left out */
        char k2[200];
        snprintf(k2, sizeof k2, "%s %s", kind, why);
        set_label(t, NULL, kind, NULL, 0);
        snprintf(G.field_label, sizeof G.field_label, "%s %s", t, k2);
        snprintf(G.legend_lines[2], sizeof G.legend_lines[2], "%s", why);
    } else set_label(t, NULL, kind, NULL, 0);
    return true;
}

void app_fail_set(int crit, int out) {
    G.fail_crit = CV_MAX(0, CV_MIN(crit, CV_FC_N - 1));
    G.fail_out = CV_MAX(0, CV_MIN(out, CV_FO_N - 1));
    G.field_src = 3;
    G.field_name[0] = 0;
    G.comp = 0;
    G.range_lock = false;
    refresh_field();
}

/* ---- mesh quality (app_mesh.c) ------------------------------------------------------- */

static bool refresh_field_mesh(void) {
    char why[64], kind[64];
    const char* u = mesh_unit(G.mesh_q);
    bool ok = mesh_eval_field(why, sizeof why);
    snprintf(kind, sizeof kind, "%s", ok ? cv_mq(G.mesh_q)->name : why);
    set_label("Mesh", NULL, kind, NULL, 0);
    if (ok && u[0]) {
        snprintf(G.field_label, sizeof G.field_label, "Mesh %s [%s]", kind, u);
        snprintf(G.legend_lines[2], sizeof G.legend_lines[2], "[%s]", u);
    }
    return ok;
}

void app_mesh_set(int q) {
    G.mesh_q = CV_MAX(0, CV_MIN(q, CV_MQ_N - 1));
    G.field_src = 4;
    G.field_name[0] = 0;
    G.comp = 0;
    G.range_lock = false;
    G.elem_mode = true;                              /* a property of the element: flat */
    refresh_field();
}

int app_field_comps(uint32_t node, char names[][12], float* vals, int max) {
    if (!G.loaded || G.field_src != 0 || node >= G.frd.n_nodes) return 0;
    int fi = find_field(G.step, G.field_name);
    const float* v = fi >= 0 ? cache_get(G.step, fi) : NULL;
    if (!v) return 0;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
    int n = CV_MIN(d->ncomp, max);
    for (int c = 0; c < n; c++) { memcpy(names[c], d->comp[c], 12); vals[c] = v[(size_t)node * d->ncomp + c]; }
    return n;
}

void refresh_field(void) {
    if (G.field_src == 1) { refresh_field_dat(); return; }
    int fi = G.field_src >= 2 ? -1 : find_field(G.step, G.field_name);
    G.has_field = (G.field_src == 2 && refresh_field_calc()) || (G.field_src == 3 && refresh_field_fail())
               || (G.field_src == 4 && refresh_field_mesh());
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
            char t[64];
            snprintf(t, sizeof t, "%s%s", diff ? "A-B " : "", d->name);
            set_label(t, NULL, opt_label(d, G.comp), d->name, G.comp);
        }
    }
    if (!G.has_field) {
        if (G.field_src >= 2) {}          /* the label says why */
        else if (G.field_name[0]) {
            char t[96];
            snprintf(t, sizeof t, "%s (not in this step)", G.field_name);
            set_label(t, NULL, "", NULL, 0);
        } else set_label("", NULL, "", NULL, 0);
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
    refresh_tensors();
    refresh_traj();
    refresh_lin();
    refresh_path();
    refresh_hist();
    refresh_integ();
    app_sel_field_changed();               /* the extremes over the selection, in this field */
    app_label_changed();                  /* values and the field they show */
}

/* The imaginary part of a steady-state response: G.disp2 = -DISPI, so the shape at
   phase wt is DISP cos wt + disp2 sin wt. */
static void refresh_disp2(void) {
    int fi = find_field(G.step, "DISPI");
    const float* v = fi >= 0 ? cache_get(G.step, fi) : NULL;
    G.harmonic = false;
    const float raw = units_len_raw();
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
            G.disp2[3 * i + k] = (x == x && !isinf(x)) ? -x * raw : 0.f;
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
    const float raw = units_len_raw();   /* the shape in model units, whatever DISP is shown in */
    if (!G.disp) G.disp = malloc((size_t)G.frd.n_nodes * 3 * sizeof(float));
    if (!G.disp) { cv_render_displacement(NULL, 0); return; }
    for (uint32_t i = 0; i < G.frd.n_nodes; i++)
        for (int k = 0; k < 3; k++) {
            float x = v[(size_t)i * nc + k];
            G.disp[3 * i + k] = (x == x && !isinf(x)) ? x * raw : 0.f;   /* no data = no motion */
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
    if (G.field_src >= 2 && field[0]) G.field_src = 0;   /* a named field is a .frd one; "" keeps the formula */
    char f[sizeof G.field_name];                        /* field may be G.field_name itself */
    snprintf(f, sizeof f, "%s", field);
    memcpy(G.field_name, f, sizeof f);
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
    if (G.legend_fmt == 1) {                 /* fixed decimals, unless the range is too small to show any: scientific then */
        double big = CV_MAX(fabs(G.rmin), fabs(G.rmax));
        if (big > 0 && big < pow(10.0, 1 - G.legend_decimals)) snprintf(out, n, "%.*e", G.legend_decimals, v);
        else snprintf(out, n, "%.*f", G.legend_decimals, v);
        return;
    }
    if (G.legend_fmt == 2) { snprintf(out, n, "%.*e", G.legend_decimals, v); return; }
    double a = fabs(v);
    if (a != 0 && (a < 1e-3 || a >= 1e5)) snprintf(out, n, "%.3e", v);
    else snprintf(out, n, "%.4g", v);
}
