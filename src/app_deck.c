/* app_deck.c -- the input deck beside the results: named element sets as a
   display group, node sets and surfaces as highlights, material names.

   Sets hold raw ids; they are resolved against whatever geometry is shown (the
   deck's own mesh, or the .frd's -- CalculiX keeps element and node numbers). */
#include "app.h"
#include "inp.h"
#include "os.h"
#include <math.h>

static struct {
    cv_inp  d;
    bool    on;
    char    path[1024];
    bool*   set_on;             /* per set: elset = in the display group, nset = highlighted */
    bool*   surf_on;
    bool*   link_on;
    uint8_t* nvis;              /* per shown node: in a visible element (NULL = all) */
    /* position lookup into the shown geometry, built on first need */
    uint64_t* key;              /* sorted grid cell key per entry */
    uint32_t* idx;              /* node index per entry */
    uint32_t  nkey;
    v3        g0;
    float     gh;
    const cv_frd* gfor;         /* geometry the grid was built for */
    /* deck element (index in D.d.mesh) -> shown element, built on first need */
    uint32_t* emap;
    const cv_frd* efor;         /* geometry emap was built for */
    bool      etried;
    /* nodes the deck has but the results do not (ccx drops nodes that only sit in a
       constraint: coupling and rigid body reference nodes), with the displacement
       estimated from the nodes they are tied to, so their glyphs and spiders ride
       along with the shape. Sorted by id. */
    uint32_t* est_id; float* est_d; uint32_t n_est;
    /* the local systems of the results (GLOBAL=NO), built on first need */
    cv_localsys L;
    bool      L_tried;
    int       L_said;           /* CV_LOC_ bits already reported */
} D;

static void grid_free(void) {
    free(D.key); free(D.idx);
    D.key = NULL; D.idx = NULL; D.nkey = 0; D.gfor = NULL;
}

bool deck_loaded(void) { return D.on; }
const cv_inp* deck_get(void) { return D.on ? &D.d : NULL; }
const char* deck_path(void) { return D.path; }
bool* deck_set_flags(void) { return D.set_on; }
bool* deck_surf_flags(void) { return D.surf_on; }
bool* deck_link_flags(void) { return D.link_on; }

void deck_clear(void) {
    grid_free();
    free(D.emap);
    free(D.est_id); free(D.est_d);
    cv_localsys_free(&D.L);
    cv_inp_free(&D.d);
    free(D.d.msgs.a);
    free(D.set_on); free(D.surf_on); free(D.link_on); free(D.nvis);
    memset(&D, 0, sizeof D);
    cv_render_aux(CV_AUX_HLPT, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_HLTRI, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_BCLN, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_LDLN, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_MOMLN, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_HEATLN, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_DISCLN, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_LINKLN, NULL, NULL, NULL, 0);
}

bool deck_has_discrete(void) { return D.on && D.d.ndisc > 0; }

bool deck_has_bc(void) { return D.on && D.d.nbcs > 0; }
bool deck_has_loads(void) { return D.on && (D.d.ncloads || D.d.ndloads || D.d.nbody || D.d.ntemps || D.d.npret); }

/* Take the parsed deck (its mesh already moved out if it became the geometry). */
void deck_set(cv_inp* d, const char* path) {
    deck_clear();
    if (!d) return;
    D.d = *d;
    memset(d, 0, sizeof *d);
    D.on = true;
    snprintf(D.path, sizeof D.path, "%s", path);
    D.set_on = calloc((size_t)CV_MAX(D.d.nsets, 1), sizeof(bool));
    D.surf_on = calloc((size_t)CV_MAX(D.d.nsurfs, 1), sizeof(bool));
    D.link_on = calloc((size_t)CV_MAX(D.d.nlinks, 1), sizeof(bool));
    for (int i = 0; D.link_on && i < D.d.nlinks; i++)           /* spiders on, surface pairs off */
        D.link_on[i] = D.d.links[i].kind != CV_LINK_TIE && D.d.links[i].kind != CV_LINK_CONTACT;
    for (size_t i = 0; i < D.d.msgs.n; i++) cv_msg_add(&G.msgs, D.d.msgs.a[i].where, false, D.d.msgs.a[i].text);
    if (D.d.cyc_n > 1) {                /* *CYCLIC SYMMETRY MODEL: the cyclic view is ready with its sectors and axis */
        const float* a = D.d.cyc_axis;
        float ax[3] = { a[3] - a[0], a[4] - a[1], a[5] - a[2] }, l = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        G.cyc_n = G.cyc_show = D.d.cyc_n;
        for (int k = 0; k < 3 && l > 0; k++)
            if (fabsf(ax[k]) > 0.999f * l) { G.cyc_axis = k; memcpy(G.cyc_o, a, 3 * sizeof(float)); }
    }
}

/* ---- results in local systems ----------------------------------------------------
   With GLOBAL=NO (or shells, *ORIENTATION, *TRANSFORM) CalculiX writes values in
   local systems the .frd does not record; the deck does. Turned back on decode. */

static void loc_say(int bits, const char* field) {
    char msg[200];
    int fresh = bits & ~D.L_said;
    D.L_said |= bits;
    if (fresh & CV_LOC_TURNED) {
        snprintf(msg, sizeof msg, "%s and others written in local systems (GLOBAL=NO) are turned to global with the deck", field);
        cv_msg_add(&G.msgs, 0, false, msg);
    }
    if (fresh & CV_LOC_APPROX) {
        snprintf(msg, sizeof msg, "%s: approximate where the element systems around a node differ (cylindrical orientation, curved shell); request GLOBAL=YES for exact values", field);
        cv_msg_add(&G.msgs, 0, false, msg);
    }
    if (fresh & CV_LOC_NAN) {
        snprintf(msg, sizeof msg, "%s: no value where the element systems around a node differ by more than %.0f degrees or cannot be rebuilt; request GLOBAL=YES", field, CV_LOC_SPAN);
        cv_msg_add(&G.msgs, 0, false, msg);
    }
}

void deck_localize(int step, const cv_field_desc* d, float* vals) {
    if (!D.on || !G.frd.n_steps || step < 0 || step >= G.frd.n_steps) return;
    if (!D.L_tried) {
        D.L_tried = true;
        if (!cv_localsys_init(&D.L, &D.d, &G.frd)) cv_msg_add(&G.msgs, 0, false, "out of memory: results in local systems stay local");
    }
    int r = cv_localsys_apply(&D.L, &D.d, &G.frd, G.frd.steps[step].step, d, vals);
    if (r) loc_say(r, d->name);
}

void deck_localize_dat(cv_dat* dat, const cv_frd* f) {
    if (!D.on) return;
    int r = 0;
    for (int i = 0; i < dat->n; i++) r |= cv_localsys_dat(&D.d, f, &dat->b[i]);
    if (r) loc_say(r, "the .dat output");
}

/* The comparison run's own deck, for its results in local systems: B of A - B is
   turned like A, never with A's deck (the runs may differ in exactly that). */
static struct { cv_inp d; cv_localsys L; bool on, tried; } C;

void deck_compare_close(void) {
    if (C.on) { cv_localsys_free(&C.L); cv_inp_free(&C.d); free(C.d.msgs.a); }
    memset(&C, 0, sizeof C);
}

void deck_compare_open(const char* frd_path) {
    deck_compare_close();
    char p[1024];
    if (!deck_sibling(frd_path, ".inp", p, sizeof p)) return;
    if (deck_read(p, &C.d)) C.on = true;
    else { cv_inp_free(&C.d); free(C.d.msgs.a); memset(&C.d, 0, sizeof C.d); }
}

void deck_compare_localize(const cv_frd* f, int step, const cv_field_desc* d, float* vals) {
    if (!C.on || step < 0 || step >= f->n_steps) return;
    if (!C.tried) {
        C.tried = true;
        if (!cv_localsys_init(&C.L, &C.d, f)) cv_msg_add(&G.msgs, 0, false, "out of memory: results in local systems stay local");
    }
    int r = cv_localsys_apply(&C.L, &C.d, f, f->steps[step].step, d, vals);
    if (r) loc_say(r, d->name);
}

/* Parse a deck; includes resolve against its folder. */
bool deck_read(const char* path, cv_inp* d) {
    cv_map m;
    if (!cv_map_open(&m, path)) return false;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char* sl = strrchr(dir, cv_path_sep());
#ifdef _WIN32
    char* sl2 = strrchr(dir, '/');
    if (sl2 > sl) sl = sl2;
#endif
    if (sl) *sl = 0; else snprintf(dir, sizeof dir, ".");
    bool ok = cv_inp_parse(d, m.data, m.size, deck_file_reader, dir);
    cv_map_close(&m);
    return ok;
}

/* *INCLUDE reader: paths relative to the deck's folder */
bool deck_file_reader(void* user, const char* path, char** data, size_t* size) {
    char full[2048];
    char sep = cv_path_sep();
    if (path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':')) snprintf(full, sizeof full, "%s", path);
    else snprintf(full, sizeof full, "%s%c%s", (const char*)user, sep, path);
    cv_map m;
    if (!cv_map_open(&m, full)) return false;
    *data = malloc(m.size + 1);
    if (*data) { memcpy(*data, m.data, m.size); *size = m.size; }
    cv_map_close(&m);
    return *data != NULL;
}

/* "Material 2" becomes "Material 2 STEEL" when the deck names it
   (CalculiX numbers materials in the order the deck defines them). */
const char* deck_material_name(uint32_t k) {
    if (!D.on || k == 0 || (int)k > D.d.nmats) return NULL;
    return D.d.mats[k - 1];
}

/* ---- deck element -> shown element ------------------------------------------------
   Matched once per geometry by node lists where the ids disagree. */

uint32_t deck_elem(const cv_frd* f, uint32_t id) {
    if (f == &D.d.mesh || !D.d.mesh.n_elems) return cv_frd_elem_index(f, id);
    if (D.efor != f) {
        free(D.emap);
        cv_elem_match m;
        D.emap = cv_frd_match_elems(&D.d.mesh, f, &m);
        D.efor = f;
        if (D.emap && m.by_nodes && !D.etried) {
            char msg[200], off[40] = "";
            if (m.shifted) snprintf(off, sizeof off, " (offset %lld)", (long long)m.offset);
            snprintf(msg, sizeof msg, "deck element ids differ from the .frd%s: matched by node lists", off);
            if (m.none) snprintf(msg + strlen(msg), sizeof msg - strlen(msg), ", %u not found", m.none);
            cv_msg_add(&G.msgs, 0, false, msg);
        }
        D.etried = true;
    }
    if (!D.emap) return cv_frd_elem_index(f, id);               /* out of memory: trust the ids */
    uint32_t de = cv_frd_elem_index(&D.d.mesh, id);
    return de == UINT32_MAX ? UINT32_MAX : D.emap[de];
}

/* ---- display group: ticked element sets --------------------------------------- */

bool deck_any_elset_on(void) {
    for (int i = 0; D.on && i < D.d.nsets; i++) if (D.d.sets[i].is_elem && D.set_on[i]) return true;
    return false;
}

/* AND vis with "in at least one ticked element set" (no set ticked = no filter). */
void deck_apply_mask(const cv_frd* f, uint8_t* vis) {
    if (!deck_any_elset_on()) return;
    uint8_t* in = calloc(CV_MAX(f->n_elems, 1), 1);
    if (!in) return;
    for (int i = 0; i < D.d.nsets; i++) {
        const cv_set* s = &D.d.sets[i];
        if (!s->is_elem || !D.set_on[i]) continue;
        for (uint32_t j = 0; j < s->n; j++) {
            uint32_t e = deck_elem(f, s->ids[j]);
            if (e != UINT32_MAX) in[e] = 1;
        }
    }
    for (uint32_t e = 0; e < f->n_elems; e++) vis[e] &= in[e];
    free(in);
}

/* ---- deck node -> shown node ------------------------------------------------------
   Same id when the .frd kept it; otherwise the .frd node at the deck node's
   position (CalculiX expands shells into solids with new node numbers but keeps a
   node layer on the mid-surface). */

static uint64_t cell_key(int64_t x, int64_t y, int64_t z) {
    return ((uint64_t)(x & 0x1FFFFF) << 42) | ((uint64_t)(y & 0x1FFFFF) << 21) | (uint64_t)(z & 0x1FFFFF);
}

static int cmp_pair(const void* a, const void* b) {
    const uint64_t* x = a; const uint64_t* y = b;
    return (x[0] > y[0]) - (x[0] < y[0]);
}

static bool grid_build(const cv_frd* f) {
    grid_free();
    uint64_t* pairs = malloc((size_t)CV_MAX(f->n_nodes, 1) * 2 * sizeof(uint64_t));
    D.key = malloc((size_t)CV_MAX(f->n_nodes, 1) * sizeof(uint64_t));
    D.idx = malloc((size_t)CV_MAX(f->n_nodes, 1) * sizeof(uint32_t));
    if (!pairs || !D.key || !D.idx) { free(pairs); grid_free(); return false; }
    D.g0 = G.bmin;
    D.gh = CV_MAX(G.diag, 1e-12f) / 256.f;
    for (uint32_t i = 0; i < f->n_nodes; i++) {
        const float* p = f->xyz + 3 * i;
        pairs[2 * i] = cell_key((int64_t)floorf((p[0] - D.g0.x) / D.gh), (int64_t)floorf((p[1] - D.g0.y) / D.gh),
                                (int64_t)floorf((p[2] - D.g0.z) / D.gh));
        pairs[2 * i + 1] = i;
    }
    qsort(pairs, f->n_nodes, 2 * sizeof(uint64_t), cmp_pair);
    for (uint32_t i = 0; i < f->n_nodes; i++) { D.key[i] = pairs[2 * i]; D.idx[i] = (uint32_t)pairs[2 * i + 1]; }
    D.nkey = f->n_nodes;
    D.gfor = f;
    free(pairs);
    return true;
}

static uint32_t nearest_node(const cv_frd* f, const float* q) {
    if (D.gfor != f && !grid_build(f)) return UINT32_MAX;
    int64_t cx = (int64_t)floorf((q[0] - D.g0.x) / D.gh), cy = (int64_t)floorf((q[1] - D.g0.y) / D.gh),
            cz = (int64_t)floorf((q[2] - D.g0.z) / D.gh);
    float tol = 1e-4f * CV_MAX(G.diag, 1e-12f), best = tol * tol;
    uint32_t hit = UINT32_MAX;
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) {
        uint64_t k = cell_key(cx + dx, cy + dy, cz + dz);
        uint32_t lo = 0, hi = D.nkey;                         /* first entry >= k */
        while (lo < hi) { uint32_t m = (lo + hi) / 2; if (D.key[m] < k) lo = m + 1; else hi = m; }
        for (uint32_t j = lo; j < D.nkey && D.key[j] == k; j++) {
            const float* p = f->xyz + 3 * D.idx[j];
            float d2 = (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]);
            if (d2 <= best) { best = d2; hit = D.idx[j]; }
        }
    }
    return hit;
}

static uint32_t shown_node(const cv_frd* f, uint32_t id) {
    uint32_t n = cv_frd_node_index(f, id);
    if (n != UINT32_MAX || f == &D.d.mesh || !D.d.mesh.n_nodes) return n;
    uint32_t dn = cv_frd_node_index(&D.d.mesh, id);
    return dn == UINT32_MAX ? UINT32_MAX : nearest_node(f, D.d.mesh.xyz + 3 * dn);
}

/* ---- highlights: node sets as balls, surfaces as faces --------------------------- */


static void push3(cv_fvec* v, const float* p) {
    if (cv_reserve(*v, v->n + 3)) { v->a[v->n++] = p[0]; v->a[v->n++] = p[1]; v->a[v->n++] = p[2]; }
}

/* A glyph vertex's displacement is 6 wide: DISP, then the harmonic part (-DISPI),
   so glyphs follow a steady-state phase animation too; app_aux_upload splits it. */
static void push6(cv_fvec* v, const float* d) {
    if (cv_reserve(*v, v->n + 6)) for (int k = 0; k < 6; k++) v->a[v->n++] = d[k];
}

void app_node_disp6(uint32_t i, float d[6]) {
    for (int k = 0; k < 3; k++) {
        d[k] = G.disp ? G.disp[3 * (size_t)i + k] : 0.f;
        d[3 + k] = G.disp2 ? G.disp2[3 * (size_t)i + k] : 0.f;
    }
}

void app_aux_upload(int which, const cv_fvec* pos, const cv_fvec* disp6, const float* scal) {
    uint32_t n = (uint32_t)(pos->n / 3);
    float* d = n && disp6->n == 2 * pos->n ? malloc((size_t)n * 6 * sizeof(float)) : NULL;
    if (!d) { cv_render_aux(which, n ? pos->a : NULL, NULL, scal, n); return; }
    float* d2 = d + 3 * (size_t)n;
    bool any2 = false;
    for (size_t i = 0; i < n; i++) for (int k = 0; k < 3; k++) {
        d[3 * i + k] = disp6->a[6 * i + k];
        d2[3 * i + k] = disp6->a[6 * i + 3 + k];
        any2 |= d2[3 * i + k] != 0.f;
    }
    cv_render_aux2(which, pos->a, d, any2 ? d2 : NULL, scal, n);
    free(d);
}

static void push_node(cv_fvec* pos, cv_fvec* disp, uint32_t i) {
    float d[6];
    app_node_disp6(i, d);
    push3(pos, G.frd.xyz + 3 * i);
    push6(disp, d);
}

/* ---- glyphs: supports as cones, loads as arrows -------------------------------
   All in world units, L = bc_scale (supports, springs) or 1.5 load_scale (loads)
   times sym_len, the mesh's symbol size; every vertex carries
   its node's displacement so the glyphs ride along with the deformed shape. */

void deck_seg(cv_fvec* pos, cv_fvec* disp, const float* a, const float* b, const float* d) {
    push3(pos, a); push6(disp, d);
    push3(pos, b); push6(disp, d);
}

/* arrow whose head sits at `tip`, shaft along -dir (unit) of length len */
void deck_arrow(cv_fvec* pos, cv_fvec* disp, const float tip[3], const float dir[3], float len, const float d[6], bool twin) {
    float tail[3] = { tip[0] - dir[0] * len, tip[1] - dir[1] * len, tip[2] - dir[2] * len };
    deck_seg(pos, disp, tail, tip, d);
    /* a perpendicular for the head: the axis least aligned with dir */
    int k = fabsf(dir[0]) <= fabsf(dir[1]) ? (fabsf(dir[0]) <= fabsf(dir[2]) ? 0 : 2) : (fabsf(dir[1]) <= fabsf(dir[2]) ? 1 : 2);
    float up[3] = { 0, 0, 0 }; up[k] = 1;
    float side[3] = { dir[1] * up[2] - dir[2] * up[1], dir[2] * up[0] - dir[0] * up[2], dir[0] * up[1] - dir[1] * up[0] };
    float sl = sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
    if (sl > 0) for (int i = 0; i < 3; i++) side[i] /= sl;
    float hl = len * 0.28f, hw = len * 0.12f;
    for (int h = 0; h < (twin ? 2 : 1); h++) {
        float back = h ? hl * 1.6f : 0.f;
        float base[3] = { tip[0] - dir[0] * (hl + back), tip[1] - dir[1] * (hl + back), tip[2] - dir[2] * (hl + back) };
        float t[3] = { tip[0] - dir[0] * back, tip[1] - dir[1] * back, tip[2] - dir[2] * back };
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            float w[3] = { base[0] + side[0] * hw * sgn, base[1] + side[1] * hw * sgn, base[2] + side[2] * hw * sgn };
            deck_seg(pos, disp, t, w, d);
        }
    }
}

static void node_pd(uint32_t i, float p[3], float d[6]) {
    memcpy(p, G.frd.xyz + 3 * i, 3 * sizeof(float));
    app_node_disp6(i, d);
}

/* Where a deck node is drawn: the shown node when the .frd kept it (or one sits
   at its position), else the deck's own coordinates, undisplaced -- a beam's
   axis nodes are not in the expanded .frd mesh at all. */
static bool node_visible(uint32_t n) { return !D.nvis || D.nvis[n]; }

bool deck_node_pd(uint32_t id, float p[3], float d[6]) {
    uint32_t n = shown_node(&G.frd, id);
    if (n != UINT32_MAX) { if (!node_visible(n)) return false; node_pd(n, p, d); return true; }
    uint32_t dn = D.d.mesh.n_nodes ? cv_frd_node_index(&D.d.mesh, id) : UINT32_MAX;
    if (dn == UINT32_MAX) return false;
    memcpy(p, D.d.mesh.xyz + 3 * dn, 3 * sizeof(float));
    for (int k = 0; k < 6; k++) d[k] = 0;
    if (D.n_est) {
        uint32_t* e = bsearch(&id, D.est_id, D.n_est, sizeof(uint32_t), cv_cmp_u32);
        if (e) memcpy(d, D.est_d + 6 * (e - D.est_id), 6 * sizeof(float));
    }
    if (G.crop_on)                                  /* the crop box applies to these too */
        for (int k = 0; k < 3; k++) if (p[k] < G.job.crop_lo[k] || p[k] > G.job.crop_hi[k]) return false;
    return true;
}

/* unit vector least aligned with u, then made perpendicular to it */
static void perp_of(const float u[3], float out[3]) {
    int k = fabsf(u[0]) <= fabsf(u[1]) ? (fabsf(u[0]) <= fabsf(u[2]) ? 0 : 2) : (fabsf(u[1]) <= fabsf(u[2]) ? 1 : 2);
    float e[3] = { 0, 0, 0 }; e[k] = 1;
    float dt = e[0] * u[0] + e[1] * u[1] + e[2] * u[2];
    for (int i = 0; i < 3; i++) out[i] = e[i] - dt * u[i];
    float l = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (l > 0) for (int i = 0; i < 3; i++) out[i] /= l;
}

/* the symbol between a and b (displacements da, db interpolated along it) */
static void discrete_symbol(cv_fvec* pos, cv_fvec* disp, int kind, const float a0[3], const float da[6],
                            const float b0[3], const float db[6], float L, int lane) {
    float u[3] = { b0[0] - a0[0], b0[1] - a0[1], b0[2] - a0[2] };
    float len = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (len <= 0) return;
    for (int i = 0; i < 3; i++) u[i] /= len;
    float w[3];
    perp_of(u, w);
    float amp = CV_MIN(0.25f * L, 0.2f * len);
    /* several elements on one node pair (spring + dashpot): side by side */
    float a[3], b[3];
    for (int i = 0; i < 3; i++) { a[i] = a0[i] + w[i] * amp * 2.6f * lane; b[i] = b0[i] + w[i] * amp * 2.6f * lane; }
    /* polyline points: t along a..b, s across; each vertex carries the interpolated displacement */
    float pts[40][2]; int np = 0;
    if (kind == CV_DISC_SPRING) {
        pts[np][0] = 0; pts[np++][1] = 0;
        pts[np][0] = 0.15f; pts[np++][1] = 0;
        const int zig = 7;
        for (int i = 0; i < zig; i++) { pts[np][0] = 0.15f + 0.7f * (i + 0.5f) / zig; pts[np++][1] = (i & 1) ? -1.f : 1.f; }
        pts[np][0] = 0.85f; pts[np++][1] = 0;
        pts[np][0] = 1; pts[np++][1] = 0;
    } else if (kind == CV_DISC_DASHPOT) {                  /* rod into an open cylinder */
        pts[np][0] = 0; pts[np++][1] = 0;
        pts[np][0] = 0.55f; pts[np++][1] = 0;              /* piston rod */
        pts[np][0] = 0.55f; pts[np++][1] = 0.7f;           /* piston plate */
        pts[np][0] = 0.55f; pts[np++][1] = -0.7f;
        pts[np][0] = 0.55f; pts[np++][1] = 0;
        pts[np][0] = 1; pts[np++][1] = 0;
    } else {                                              /* gap: two bars facing each other */
        pts[np][0] = 0; pts[np++][1] = 0;
        pts[np][0] = 0.42f; pts[np++][1] = 0;
        pts[np][0] = 0.42f; pts[np++][1] = 1;  pts[np][0] = 0.42f; pts[np++][1] = -1;
        pts[np][0] = 0.42f; pts[np++][1] = 0;
    }
    float prev[3], prevd[6];
    for (int i = 0; i < np; i++) {
        float t = pts[i][0], q[3], qd[6];
        for (int k = 0; k < 3; k++) q[k] = a[k] + u[k] * len * t + w[k] * amp * pts[i][1];
        for (int k = 0; k < 6; k++) qd[k] = da[k] + (db[k] - da[k]) * t;
        if (i) { push3(pos, prev); push6(disp, prevd); push3(pos, q); push6(disp, qd); }
        memcpy(prev, q, sizeof q); memcpy(prevd, qd, sizeof qd);
    }
    if (kind == CV_DISC_DASHPOT) {                        /* the cylinder around the plate */
        float c0 = 0.35f, c1 = 0.8f, corner[4][2] = { {c0, 1}, {c1, 1}, {c1, -1}, {c0, -1} };
        for (int i = 0; i < 4; i++) {
            if (i == 0) continue;                         /* open end: no bar at c0 */
            float q0[3], q1[3], d0[6], d1[6];
            for (int k = 0; k < 3; k++) {
                q0[k] = a[k] + u[k] * len * corner[i - 1][0] + w[k] * amp * corner[i - 1][1];
                q1[k] = a[k] + u[k] * len * corner[i][0] + w[k] * amp * corner[i][1];
            }
            for (int k = 0; k < 6; k++) {
                d0[k] = da[k] + (db[k] - da[k]) * corner[i - 1][0];
                d1[k] = da[k] + (db[k] - da[k]) * corner[i][0];
            }
            push3(pos, q0); push6(disp, d0); push3(pos, q1); push6(disp, d1);
        }
    }
    if (kind == CV_DISC_GAP) {                            /* the other bar */
        float q0[3], q1[3], d0[6], d1[6];
        for (int k = 0; k < 3; k++) { q0[k] = a[k] + u[k] * len * 0.58f + w[k] * amp; q1[k] = a[k] + u[k] * len * 0.58f - w[k] * amp; }
        for (int k = 0; k < 6; k++) d0[k] = d1[k] = da[k] + (db[k] - da[k]) * 0.58f;
        push3(pos, q0); push6(disp, d0); push3(pos, q1); push6(disp, d1);
        for (int k = 0; k < 3; k++) q0[k] = a[k] + u[k] * len * 0.58f;
        push3(pos, q0); push6(disp, d0); push3(pos, b); push6(disp, db);
    }
}

/* a mass: a small cube of edges around the node */
static void mass_symbol(cv_fvec* pos, cv_fvec* disp, const float p[3], const float d[6], float h) {
    for (int e = 0; e < 12; e++) {
        int axis = e / 4, c = e % 4;
        float q0[3], q1[3];
        for (int k = 0; k < 3; k++) {
            int s0 = (k == (axis + 1) % 3) ? (c & 1 ? 1 : -1) : (k == (axis + 2) % 3) ? (c & 2 ? 1 : -1) : 0;
            q0[k] = p[k] + s0 * h + (k == axis ? -h : 0);
            q1[k] = p[k] + s0 * h + (k == axis ? h : 0);
        }
        push3(pos, q0); push6(disp, d); push3(pos, q1); push6(disp, d);
    }
}

static void refresh_discrete(void) {
    cv_fvec lp = {0}, ld = {0};
    float L = CV_MAX(G.bc_scale, 0.01f) * G.sym_len;
    if (D.on && G.loaded) {
        for (uint32_t i = 0; i < D.d.ndisc; i++) {
            const cv_discrete* q = &D.d.disc[i];
            float a[3], da[6], b[3], db[6];
            if (!deck_node_pd(q->n[0], a, da)) continue;
            if (q->kind == CV_DISC_MASS) { mass_symbol(&lp, &ld, a, da, 0.18f * L); continue; }
            if (q->nn == 2) {
                if (!deck_node_pd(q->n[1], b, db)) continue;
            } else {                                      /* grounded: along its dof, ground bar at the end */
                int k = q->dof ? (q->dof - 1) % 3 : 1;
                memcpy(b, a, sizeof b); memcpy(db, da, sizeof db);
                b[k] -= L;
                for (int m = 0; m < 6; m++) db[m] = 0;            /* the ground stays */
                float w[3] = { 0, 0, 0 }; w[(k + 1) % 3] = 0.3f * L;
                float g0[3], g1[3];
                for (int m = 0; m < 3; m++) { g0[m] = b[m] - w[m]; g1[m] = b[m] + w[m]; }
                push3(&lp, g0); push6(&ld, db); push3(&lp, g1); push6(&ld, db);
            }
            int lane = 0;                                 /* earlier elements on the same pair */
            for (uint32_t j = 0; j < i; j++) {
                const cv_discrete* r = &D.d.disc[j];
                if (r->nn == q->nn && r->kind != CV_DISC_MASS &&
                    ((r->n[0] == q->n[0] && r->n[1] == q->n[1]) || (r->n[0] == q->n[1] && r->n[1] == q->n[0]))) lane++;
            }
            discrete_symbol(&lp, &ld, q->kind, a, da, b, db, L, lane);
        }
    }
    app_aux_upload(CV_AUX_DISCLN, &lp, &ld, NULL);
    cv_free_vec(lp); cv_free_vec(ld);
}

/* nodes (shown indices) of a deck surface: its node list, or the corners of its faces */
typedef CV_VEC(uint32_t) u32vec;

static void surface_nodes(const cv_frd* f, const cv_surface* s, u32vec* out) {
    for (uint32_t j = 0; j < s->nn; j++) {
        uint32_t n = shown_node(f, s->nodes[j]);
        if (n != UINT32_MAX) cv_push(*out, n);
    }
    for (uint32_t j = 0; j < s->n; j++) {
        uint32_t e = deck_elem(f, s->elem[j]);
        uint32_t c[4];
        int k = e == UINT32_MAX ? 0 : cv_elem_face_corners(f, e, s->face[j], c);
        for (int i = 0; i < k; i++) cv_push(*out, c[i]);
    }
}

/* Displacement of the deck-only nodes: the mean over the nodes each one is tied to
   through its links (a rigid body's or coupling's driven set, an equation's other
   terms). An estimate, no rotation; enough for the glyphs to follow the shape. */
static void estimate_absent(void) {
    free(D.est_id); free(D.est_d);
    D.est_id = NULL; D.est_d = NULL; D.n_est = 0;
    if (!D.on || !G.loaded || !D.d.mesh.n_nodes || !G.disp) return;
    for (int i = 0; i < D.d.nlinks; i++) {
        const cv_link* l = &D.d.links[i];
        if (!l->ref || cv_frd_node_index(&G.frd, l->ref) != UINT32_MAX) continue;
        u32vec tgt = {0};
        for (uint32_t j = 0; j < l->n; j++) {
            uint32_t n = shown_node(&G.frd, l->nodes[j]);
            if (n != UINT32_MAX) cv_push(tgt, n);
        }
        if (l->surf[0] >= 0 && l->surf[0] < D.d.nsurfs) surface_nodes(&G.frd, &D.d.surfs[l->surf[0]], &tgt);
        if (tgt.n) {
            float m[6] = {0}, d[6];
            for (size_t j = 0; j < tgt.n; j++) {
                app_node_disp6(tgt.a[j], d);
                for (int k = 0; k < 6; k++) m[k] += d[k] / (float)tgt.n;
            }
            uint32_t* id = realloc(D.est_id, (D.n_est + 1) * sizeof(uint32_t));
            float* dd = realloc(D.est_d, (D.n_est + 1) * 6 * sizeof(float));
            if (id) D.est_id = id;
            if (dd) D.est_d = dd;
            if (id && dd) { D.est_id[D.n_est] = l->ref; memcpy(D.est_d + 6 * D.n_est, m, sizeof m); D.n_est++; }
        }
        cv_free_vec(tgt);
    }
    /* sort by id for the lookup; a node driven by two links keeps the first */
    for (uint32_t i = 1; i < D.n_est; i++)
        for (uint32_t j = i; j > 0 && D.est_id[j] < D.est_id[j - 1]; j--) {
            uint32_t t = D.est_id[j]; D.est_id[j] = D.est_id[j - 1]; D.est_id[j - 1] = t;
            float td[6]; memcpy(td, D.est_d + 6 * j, sizeof td);
            memcpy(D.est_d + 6 * j, D.est_d + 6 * (j - 1), sizeof td); memcpy(D.est_d + 6 * (j - 1), td, sizeof td);
        }
}

/* spiders: reference node to every driven node (equation: first term to the others) */
static void refresh_links(void) {
    cv_fvec lp = {0}, ld = {0};
    if (D.on && G.loaded) {
        for (int i = 0; i < D.d.nlinks; i++) {
            const cv_link* l = &D.d.links[i];
            if (!D.link_on[i] || l->kind == CV_LINK_TIE || l->kind == CV_LINK_CONTACT) continue;
            float r[3], rd[6];
            bool have_ref = l->ref && deck_node_pd(l->ref, r, rd);
            u32vec tgt = {0};
            for (uint32_t j = 0; j < l->n; j++) {
                uint32_t n = shown_node(&G.frd, l->nodes[j]);
                if (n != UINT32_MAX && node_visible(n)) cv_push(tgt, n);
            }
            if (l->surf[0] >= 0 && l->surf[0] < D.d.nsurfs) surface_nodes(&G.frd, &D.d.surfs[l->surf[0]], &tgt);
            for (size_t j = 0; j < tgt.n; j++) {
                float p[3], d[6];
                node_pd(tgt.a[j], p, d);
                if (l->kind == CV_LINK_EQUATION && j == 0 && !have_ref) { memcpy(r, p, sizeof r); memcpy(rd, d, sizeof rd); have_ref = true; continue; }
                if (!have_ref) continue;
                if (l->kind == CV_LINK_EQUATION && G.frd.node_id[tgt.a[j]] == l->ref) continue;
                deck_seg(&lp, &ld, r, p, rd);
                /* the far end carries its own displacement: patch the last vertex */
                memcpy(ld.a + ld.n - 6, d, 6 * sizeof(float));
            }
            cv_free_vec(tgt);
        }
    }
    app_aux_upload(CV_AUX_LINKLN, &lp, &ld, NULL);
    cv_free_vec(lp); cv_free_vec(ld);
}

/* a surface is highlighted when ticked itself or through a ticked tie / contact pair */
static bool surf_shown(int si) {
    if (D.surf_on[si]) return true;
    for (int i = 0; i < D.d.nlinks; i++) {
        const cv_link* l = &D.d.links[i];
        if (D.link_on[i] && (l->kind == CV_LINK_TIE || l->kind == CV_LINK_CONTACT) && (l->surf[0] == si || l->surf[1] == si)) return true;
    }
    return false;
}

void deck_refresh_highlight(void) {
    /* glyphs only at nodes of visible elements (crop box, unticked groups) */
    free(D.nvis); D.nvis = NULL;
    if (G.loaded && G.vis && G.frd.n_nodes) {
        D.nvis = calloc(G.frd.n_nodes, 1);
        for (size_t i = 0; D.nvis && i < G.skin.n_pt; i++) D.nvis[G.skin.pt[i]] = 1;
    }
    estimate_absent();
    loads_refresh();
    refresh_discrete();
    refresh_links();
    cv_fvec pp = {0}, pd = {0}, tp = {0}, td = {0};
    const cv_frd* f = &G.frd;
    if (D.on && G.loaded) {
        for (int i = 0; i < D.d.nsets; i++) {
            const cv_set* s = &D.d.sets[i];
            if (s->is_elem || !D.set_on[i]) continue;
            for (uint32_t j = 0; j < s->n; j++) {
                uint32_t n = shown_node(f, s->ids[j]);
                if (n != UINT32_MAX) push_node(&pp, &pd, n);
            }
        }
        for (int i = 0; i < D.d.nsurfs; i++) {
            const cv_surface* s = &D.d.surfs[i];
            if (!surf_shown(i)) continue;
            for (uint32_t j = 0; j < s->nn; j++) {           /* node surface: as balls */
                uint32_t n = shown_node(f, s->nodes[j]);
                if (n != UINT32_MAX) push_node(&pp, &pd, n);
            }
            for (uint32_t j = 0; j < s->n; j++) {            /* element faces: as triangles */
                uint32_t e = deck_elem(f, s->elem[j]);
                uint32_t c[4];
                int k = e == UINT32_MAX ? 0 : cv_elem_face_corners(f, e, s->face[j], c);
                if (k < 3) continue;
                push_node(&tp, &td, c[0]); push_node(&tp, &td, c[1]); push_node(&tp, &td, c[2]);
                if (k == 4) { push_node(&tp, &td, c[0]); push_node(&tp, &td, c[2]); push_node(&tp, &td, c[3]); }
            }
        }
    }
    app_aux_upload(CV_AUX_HLPT, &pp, &pd, NULL);
    app_aux_upload(CV_AUX_HLTRI, &tp, &td, NULL);
    cv_free_vec(pp); cv_free_vec(pd); cv_free_vec(tp); cv_free_vec(td);
}

/* sibling file with another extension, lower or upper case: model.frd -> model.inp */
bool deck_sibling(const char* path, const char* ext, char* out, size_t n) {
    size_t l = strlen(path);
    const char* dot = strrchr(path, '.');
    if (!dot || l + 8 > n) return false;
    size_t b = (size_t)(dot - path);
    memcpy(out, path, b);
    for (int up = 0; up < 2; up++) {
        for (size_t i = 0; ext[i] && b + i + 1 < n; i++) {
            char c = ext[i];
            out[b + i] = up && c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
            out[b + i + 1] = 0;
        }
        if (strcmp(out, path) != 0 && cv_file_size(out) > 0) return true;
    }
    return false;
}
