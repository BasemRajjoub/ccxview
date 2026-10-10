/* app_contact.c -- contact and ties as CalculiX made them.

   The contact elements of jobname.cel (cel.h): for the increment on screen (the
   last iteration of its last attempt), or any iteration picked: the master faces
   they pair, filled, and for surface to surface the slave faces outlined. Their
   nodes are the model's, so they ride on the deformed shape. What each slave node
   is (its gap, its status) is drawn by the contact display, app_cdisp.c.

   The ties ticked (in the Contact subgroup of Fields, or under Groups > Couplings),
   and the contact pairs ticked when their surfaces are asked for: the master surface
   blue, the slave surface crimson over it (half see-through, so where they overlap
   the two mix), and for a tie its slave nodes: green those CalculiX tied, yellow
   those it could not (they are in jobname_WarnNodeMissTiedContact.nam, 2.22, or
   another jobname_WarnNode*Miss*.nam), drawn in front of everything. */
#include "app.h"
#include "app_int.h"
#include "cel.h"
#include "os.h"
#include <math.h>
#include <ctype.h>
#include <strings.h>

enum { NAM_MAX = 8 };

static struct {
    cv_cel   cel;
    bool     has_cel;
    char     cel_path[1024];
    struct { char name[64], file[256], path[1024]; uint32_t* ids; uint32_t n; bool miss; } nam[NAM_MAX];
    int      nnam;
    char     queue[4][1024];        /* opened while the model loads */
    int      nqueue;
    int      set;                   /* the set drawn, -1 none */
    uint32_t* uniq; uint32_t nuniq; int uset;   /* its distinct elements, for set uset (-1 none) */
    uint32_t count;                 /* its distinct elements */
    int      drawn[CV_KEY_N];
    bool     s2s;                   /* surface to surface elements among those drawn */
} K = { .set = -1, .uset = -1 };

static void sk_free(void);

const cv_cel* app_contact_cel(void) { return K.has_cel ? &K.cel : NULL; }
const char* app_contact_cel_path(void) { return K.has_cel ? K.cel_path : ""; }
int  app_contact_cel_set(void) { return K.set; }
uint32_t app_contact_cel_count(void) { return K.count; }
int  app_contact_nam_count(void) { return K.nnam; }

const char* app_contact_nam(int i, uint32_t* n, bool* miss) {
    if (i < 0 || i >= K.nnam) return NULL;
    if (n) *n = K.nam[i].n;
    if (miss) *miss = K.nam[i].miss;
    return K.nam[i].file;
}

bool app_contact_s2s(void) { return K.s2s; }

const uint32_t* app_contact_cel_uniq(uint32_t* n) {
    *n = K.set >= 0 && K.uset == K.set ? K.nuniq : 0;
    return *n ? K.uniq : NULL;
}

void app_contact_skin(void) { sk_free(); }

bool app_contact_drawn(int what[CV_KEY_N]) {
    bool any = false;
    for (int k = 0; k < CV_KEY_N; k++) { if (what) what[k] = K.drawn[k]; any |= K.drawn[k] > 0; }
    return any;
}

static void clear_aux(void) {
    static const int w[] = { CV_AUX_PMST, CV_AUX_PSLV, CV_AUX_PTIED, CV_AUX_PFREE, CV_AUX_CMST, CV_AUX_CMLN, CV_AUX_CSLV };
    for (size_t i = 0; i < CV_COUNT(w); i++) cv_render_aux(w[i], NULL, NULL, NULL, 0);
    cv_render_inst(CV_INST_CSLN, NULL, 0);
    app_cdisp_clear();
    memset(K.drawn, 0, sizeof K.drawn);
    K.s2s = false;
}

static void free_files(void) {
    cv_cel_free(&K.cel);
    K.has_cel = false;
    K.cel_path[0] = 0;
    for (int i = 0; i < K.nnam; i++) free(K.nam[i].ids);
    K.nnam = 0;
    K.set = -1; K.count = 0;
    free(K.uniq); K.uniq = NULL; K.nuniq = 0; K.uset = -1;
}

void app_contact_clear(void) {
    free_files();
    clear_aux();
    sk_free();
}

/* ---- reading ------------------------------------------------------------------------ */

static bool slurp(const char* path, char** data, size_t* size) {
    cv_map m;
    if (!cv_map_open(&m, path)) return false;
    *data = malloc(m.size + 1);
    if (*data) { memcpy(*data, m.data, m.size); (*data)[m.size] = 0; *size = m.size; }
    cv_map_close(&m);
    return *data != NULL;
}

static void say(const char* fmt, const char* a) {
    char m[1200];
    snprintf(m, sizeof m, fmt, a);
    cv_msg_add(&G.msgs, 0, false, m);
}

static bool read_cel(const char* path) {
    char* data = NULL;
    size_t size = 0;
    if (!slurp(path, &data, &size)) { say("%s: cannot be read", path); return false; }
    cv_cel c;
    bool ok = cv_cel_parse(&c, data, size);
    free(data);
    if (!ok) { say("%s: out of memory", path); return false; }
    if (!c.nsets) { say("%s: no contact elements in it", path); cv_cel_free(&c); return false; }
    cv_cel_free(&K.cel);
    K.cel = c;
    K.uset = -1;                                /* its sets' distinct elements: sorted again */
    K.has_cel = true;
    snprintf(K.cel_path, sizeof K.cel_path, "%s", path);
    if (c.bad) {
        char m[1300];
        snprintf(m, sizeof m, "%s: %u lines or contact elements not understood, skipped", cv_basename(path), c.bad);
        cv_msg_add(&G.msgs, 0, false, m);
    }
    return true;
}

static bool read_nam(const char* path) {
    char* data = NULL;
    size_t size = 0;
    if (!slurp(path, &data, &size)) { say("%s: cannot be read", path); return false; }
    uint32_t* ids = NULL, n = 0;
    char name[64] = "";
    bool ok = cv_nam_parse(data, size, &ids, &n, name);
    free(data);
    if (!ok) { say("%s: no node set in it", path); return false; }
    int i = -1;
    for (int k = 0; k < K.nnam; k++) if (!strcmp(K.nam[k].path, path)) i = k;    /* read again: replaced */
    if (i < 0 && K.nnam == NAM_MAX) { free(ids); say("%s: too many warning files, left out", path); return false; }
    if (i < 0) i = K.nnam++;
    else free(K.nam[i].ids);
    snprintf(K.nam[i].name, sizeof K.nam[i].name, "%s", name);
    snprintf(K.nam[i].file, sizeof K.nam[i].file, "%s", cv_basename(path));
    snprintf(K.nam[i].path, sizeof K.nam[i].path, "%s", path);
    K.nam[i].ids = ids; K.nam[i].n = n;
    char up[256];                               /* ..._WarnNodeMissTiedContact.nam: slave nodes not tied */
    snprintf(up, sizeof up, "%s", K.nam[i].file);
    for (char* q = up; *q; q++) *q = (char)toupper((unsigned char)*q);
    K.nam[i].miss = strstr(up, "MISS") != NULL;
    return true;
}

/* the job's name: the model's path without its extension */
static bool job_stem(char* out, size_t n) {
    if (!G.path[0]) return false;
    snprintf(out, n, "%s", G.path);
    char* dot = strrchr(out, '.');
    char* sep = strrchr(out, cv_path_sep());
    if (dot && (!sep || dot > sep)) *dot = 0;
    return true;
}

void app_contact_model(void) {
    static char last[1024];
    if (strcmp(last, G.path)) {                    /* another model: its increment on screen, unless --opt picks */
        bool opt = false;
        for (int i = 0; i < O.nopts; i++) opt |= !strncmp(O.opts[i], "cel_pick=", 9);
        if (!opt || last[0]) G.cel_pick = -1;
        snprintf(last, sizeof last, "%s", G.path);
    }
    free_files();
    char stem[1024], p[1100];
    if (job_stem(stem, sizeof stem) && !G.stl_model) {
        snprintf(p, sizeof p, "%s.cel", stem);
        if (cv_file_size(p) > 0) read_cel(p);
        else { snprintf(p, sizeof p, "%s.CEL", stem); if (cv_file_size(p) > 0) read_cel(p); }
        /* jobname_WarnNode*.nam beside it */
        char dir[1024];
        snprintf(dir, sizeof dir, "%s", stem);
        char* sep = strrchr(dir, cv_path_sep());
        const char* base = sep ? sep + 1 : dir;
        char pre[300];
        snprintf(pre, sizeof pre, "%s_WarnNode", base);
        if (sep == dir) dir[1] = 0;                 /* the model at the root: "/" */
        else if (sep) *sep = 0;
        else snprintf(dir, sizeof dir, ".");
        cv_dirent* e = NULL;
        int ne = cv_list_dir(dir, &e);
        for (int i = 0; i < ne; i++) {
            size_t l = strlen(e[i].name);
            if (e[i].dir || strncmp(e[i].name, pre, strlen(pre)) || l < 4 || strcasecmp(e[i].name + l - 4, ".nam")) continue;
            snprintf(p, sizeof p, "%s%c%s", dir, cv_path_sep(), e[i].name);
            read_nam(p);
        }
        free(e);
    }
    for (int i = 0; i < K.nqueue; i++) app_contact_open(K.queue[i]);
    K.nqueue = 0;
    if (G.cel_pick >= (K.has_cel ? K.cel.nsets : 0)) G.cel_pick = -1;     /* another file: back to the increment on screen */
    app_contact_refresh();
}

void app_contact_queue(const char* path) {
    if (K.nqueue < 4) snprintf(K.queue[K.nqueue++], sizeof K.queue[0], "%s", path);
    else say("%s: more than four contact or warning files at once, left out", path);
}

bool app_contact_open(const char* path) {
    if (!G.loaded) { say("%s: open the model first (its .frd or .inp)", path); return false; }
    bool ok = cv_ends_with_ci(path, ".nam") ? read_nam(path) : read_cel(path);
    if (ok && cv_ends_with_ci(path, ".cel")) G.cel_show = true;
    if (ok) { G.show_hl = true; app_contact_refresh(); }
    return ok;
}

/* ---- drawing -------------------------------------------------------------------------- */

static void push3(cv_fvec* v, const float* p) {
    if (cv_reserve(*v, v->n + 3)) { v->a[v->n++] = p[0]; v->a[v->n++] = p[1]; v->a[v->n++] = p[2]; }
}
static void push6(cv_fvec* v, const float* d) {
    if (cv_reserve(*v, v->n + 6)) for (int k = 0; k < 6; k++) v->a[v->n++] = d[k];
}

typedef struct { cv_fvec p, d; } vbuf;
static void vb_node(vbuf* b, uint32_t i) {
    float d[6];
    app_node_disp6(i, d);
    push3(&b->p, G.frd.xyz + 3 * (size_t)i);
    push6(&b->d, d);
}
static void vb_up(vbuf* b, int which) { app_aux_upload(which, &b->p, &b->d, NULL); cv_free_vec(b->p); cv_free_vec(b->d); }

/* a face (3 or 4 nodes) as triangles, its outline as lines */
static void face_tri(vbuf* b, const uint32_t* ix, int n) {
    vb_node(b, ix[0]); vb_node(b, ix[1]); vb_node(b, ix[2]);
    if (n == 4) { vb_node(b, ix[0]); vb_node(b, ix[2]); vb_node(b, ix[3]); }
}
static void face_ln(vbuf* b, const uint32_t* ix, int n) {
    for (int j = 0; j < n; j++) { vb_node(b, ix[j]); vb_node(b, ix[(j + 1) % n]); }
}

/* ---- faces drawn as the skin draws them --------------------------------------------
   A surface's face redrawn over the model's must be the very triangles of the skin
   (the same diagonal, the mid-side nodes as the skin takes them): exaggerated, a quad
   bends, and two ways of splitting it cross each other. The skin's triangles by element,
   and its faces by their corners, built once per skin. */
static struct {
    const uint32_t* tri; size_t n_tri, n_face;     /* the skin they were built for */
    uint32_t* off; uint32_t* tris;                 /* per element: its skin triangles (CSR) */
    uint64_t* key; uint32_t* code; uint32_t cap;   /* face corners' hash -> element << 3 | face */
    uint32_t* seen; uint32_t stamp;                /* per slot: drawn in this pass */
} SK;

static void sk_free(void) {
    free(SK.off); free(SK.tris); free(SK.key); free(SK.code); free(SK.seen);
    memset(&SK, 0, sizeof SK);
}

/* a face's corners, sorted, hashed (4th 0xFFFFFFFF for a triangle) */
static uint64_t corner_key(const uint32_t* c, int n) {
    uint32_t s[4] = { c[0], c[1], c[2], n == 4 ? c[3] : UINT32_MAX };
    for (int i = 1; i < 4; i++) for (int j = i; j > 0 && s[j] < s[j - 1]; j--) { uint32_t t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 4; i++) { h ^= s[i]; h *= 1099511628211ull; h ^= h >> 29; }
    return h | 1;                                   /* 0: an empty slot */
}

static bool sk_build(void) {
    const cv_skin* k = &G.skin;
    if (SK.tri == k->tri && SK.n_tri == k->n_tri && SK.n_face == k->n_face && SK.off) return true;
    sk_free();
    uint32_t ne = G.frd.n_elems;
    SK.off = calloc((size_t)ne + 1, sizeof(uint32_t));
    SK.tris = malloc(CV_MAX(k->n_tri, 1) * sizeof(uint32_t));
    SK.cap = 16;
    while (SK.cap < 2 * k->n_face + 16) SK.cap *= 2;
    SK.key = calloc(SK.cap, sizeof(uint64_t));
    SK.code = malloc(SK.cap * sizeof(uint32_t));
    SK.seen = calloc(SK.cap, sizeof(uint32_t));
    if (!SK.off || !SK.tris || !SK.key || !SK.code || !SK.seen) { sk_free(); return false; }
    for (size_t t = 0; t < k->n_tri; t++) SK.off[k->tri_elem[t] + 1]++;
    for (uint32_t e = 0; e < ne; e++) SK.off[e + 1] += SK.off[e];
    uint32_t* fill = malloc(((size_t)ne + 1) * sizeof(uint32_t));
    if (!fill) { sk_free(); return false; }
    memcpy(fill, SK.off, ((size_t)ne + 1) * sizeof(uint32_t));
    for (size_t t = 0; t < k->n_tri; t++) SK.tris[fill[k->tri_elem[t]]++] = (uint32_t)t;
    free(fill);
    for (size_t i = 0; i < k->n_face; i++) {
        uint32_t c[4], e = k->face[i] >> 3, f = k->face[i] & 7;
        int n = cv_elem_face_corners(&G.frd, e, (int)f, c);
        if (n < 3) continue;
        uint64_t h = corner_key(c, n);
        uint32_t s = (uint32_t)(h & (SK.cap - 1));
        while (SK.key[s] && SK.key[s] != h) s = (s + 1) & (SK.cap - 1);
        SK.key[s] = h; SK.code[s] = k->face[i];
    }
    SK.tri = k->tri; SK.n_tri = k->n_tri; SK.n_face = k->n_face;
    return true;
}

/* the skin face with these corners (shown indices): its slot (SK.code: element << 3 |
   face), UINT32_MAX none */
static uint32_t sk_slot(const uint32_t* c, int n) {
    if (!SK.key) return UINT32_MAX;
    uint64_t h = corner_key(c, n);
    for (uint32_t s = (uint32_t)(h & (SK.cap - 1)); SK.key[s]; s = (s + 1) & (SK.cap - 1))
        if (SK.key[s] == h) return s;
    return UINT32_MAX;
}

/* face f of element e as the skin's triangles; its corners split in two when the skin
   does not have it (hidden, or inside) */
static void face_skin(vbuf* b, uint32_t e, int f) {
    uint32_t fn[8], c[4];
    int nn = cv_elem_face_nodes(&G.frd, e, f, fn), drawn = 0;
    for (uint32_t j = SK.off ? SK.off[e] : 0; SK.off && j < SK.off[e + 1]; j++) {
        const uint32_t* t = G.skin.tri + 3 * (size_t)SK.tris[j];
        int in = 0;
        for (int q = 0; q < 3; q++) for (int i = 0; i < nn; i++) if (t[q] == fn[i]) { in++; break; }
        if (in < 3) continue;
        vb_node(b, t[0]); vb_node(b, t[1]); vb_node(b, t[2]);
        drawn++;
    }
    int k = drawn ? 0 : cv_elem_face_corners(&G.frd, e, f, c);
    if (k >= 3) face_tri(b, c, k);
}

/* node ids -> indices; false when one is not in the model */
static bool to_ix(const uint32_t* id, int n, uint32_t* ix) {
    for (int j = 0; j < n; j++) if ((ix[j] = cv_frd_node_index(&G.frd, id[j])) == UINT32_MAX) return false;
    return true;
}

/* the .cel set to draw: picked, else the increment on screen, else the last increment */
static int pick_set(void) {
    const cv_cel* c = &K.cel;
    if (!K.has_cel || !c->nsets) return -1;
    if (G.cel_pick >= 0 && G.cel_pick < c->nsets) return G.cel_pick;
    if (G.frd.n_steps > 0 && G.step >= 0 && G.step < G.frd.n_steps) {
        const cv_step* st = &G.frd.steps[G.step];
        int k = cv_cel_find(c, st->step, st->inc);
        if (k >= 0) return k;
        /* an increment without contact elements (not in contact yet, or a step without
           contact): none, whenever the file's sets can be matched by step at all */
        for (int i = 0; i < c->nsets; i++) if (c->sets[i].step) return -1;
    }
    int n = cv_cel_ends(c, NULL, 0), k = c->nsets - 1;
    int* all = n > 0 ? malloc((size_t)n * sizeof(int)) : NULL;
    if (all && cv_cel_ends(c, all, n) == n) k = all[n - 1];
    free(all);
    return k;
}

static int bsearch_u32(const uint32_t* a, uint32_t n, uint32_t v) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) { uint32_t m = lo + (hi - lo) / 2; if (a[m] < v) lo = m + 1; else hi = m; }
    return lo < n && a[lo] == v;
}

static bool untied(uint32_t id) {
    for (int i = 0; i < K.nnam; i++) if (K.nam[i].miss && bsearch_u32(K.nam[i].ids, K.nam[i].n, id)) return true;
    return false;
}

/* the nodes of a deck surface (element faces with their mid-side nodes, or a node
   surface), as shown indices, each once; *n their count */
static uint32_t* surf_nodes(const cv_inp* dk, int si, uint32_t* n) {
    *n = 0;
    if (si < 0 || si >= dk->nsurfs) return NULL;
    const cv_surface* s = &dk->surfs[si];
    CV_VEC(uint32_t) v = {0};
    for (uint32_t j = 0; j < s->nn; j++) {
        uint32_t ix = cv_frd_node_index(&G.frd, s->nodes[j]);
        if (ix != UINT32_MAX) cv_push(v, ix);
    }
    for (uint32_t j = 0; j < s->n; j++) {
        uint32_t e = deck_elem(&G.frd, s->elem[j]), c[8];
        int k = e == UINT32_MAX ? 0 : cv_elem_face_nodes(&G.frd, e, s->face[j], c);
        for (int i = 0; i < k; i++) cv_push(v, c[i]);
    }
    if (v.n) qsort(v.a, v.n, sizeof(uint32_t), cv_cmp_u32);
    size_t u = 0;
    for (size_t i = 0; i < v.n; i++) if (!u || v.a[i] != v.a[u - 1]) v.a[u++] = v.a[i];
    *n = (uint32_t)u;
    return v.a;
}

uint32_t* app_contact_surf_nodes(int si, uint32_t* n) {
    const cv_inp* dk = deck_get();
    *n = 0;
    return dk && G.loaded ? surf_nodes(dk, si, n) : NULL;
}

void app_contact_pair_nodes(int k, uint32_t* slave, uint32_t* nfree) {
    *slave = *nfree = 0;
    const cv_inp* dk = deck_get();
    if (!dk || k < 0 || k >= dk->nlinks || !G.loaded) return;
    uint32_t n;
    uint32_t* ix = surf_nodes(dk, dk->links[k].surf[0], &n);
    *slave = n;
    for (uint32_t j = 0; j < n; j++) if (untied(G.frd.node_id[ix[j]])) (*nfree)++;
    free(ix);
}

/* the faces of a surface, as triangles */
static void surf_faces(vbuf* b, const cv_inp* dk, int si, const uint8_t* shown) {
    if (si < 0 || si >= dk->nsurfs) return;
    const cv_surface* s = &dk->surfs[si];
    for (uint32_t j = 0; j < s->n; j++) {
        uint32_t e = deck_elem(&G.frd, s->elem[j]), c[4];
        int k = e == UINT32_MAX ? 0 : cv_elem_face_corners(&G.frd, e, s->face[j], c);
        if (k < 3 || (shown && !(shown[c[0]] && shown[c[1]] && shown[c[2]]))) continue;
        face_skin(b, e, s->face[j]);
    }
}

void app_contact_refresh(void) {
    clear_aux();
    K.set = -1; K.count = 0;
    if (!G.loaded) { sk_free(); return; }
    sk_build();
    /* nodes of the shown elements (groups, crop, hidden sets): NULL all */
    uint8_t* shown = NULL;
    if (G.vis && G.frd.n_nodes && (shown = calloc(G.frd.n_nodes, 1)))
        for (size_t i = 0; i < G.skin.n_pt; i++) shown[G.skin.pt[i]] = 1;

    /* ties ticked; contact pairs ticked, their surfaces when asked for */
    const cv_inp* dk = deck_get();
    const bool* lon = deck_link_flags();
    vbuf pm = {0}, ps = {0}, pt = {0}, pf = {0};
    for (int i = 0; G.cel_show && dk && lon && i < dk->nlinks; i++) {
        const cv_link* l = &dk->links[i];
        if (!lon[i] || (l->kind != CV_LINK_TIE && (l->kind != CV_LINK_CONTACT || !G.cel_surfs)) || !cv_inp_link_active(dk, deck_step(), i)) continue;
        size_t m0 = pm.p.n, s0 = ps.p.n;
        surf_faces(&pm, dk, l->surf[1], shown);
        surf_faces(&ps, dk, l->surf[0], shown);
        K.drawn[CV_KEY_MASTER] += pm.p.n > m0;
        K.drawn[CV_KEY_SLAVE] += ps.p.n > s0;
        if (l->kind != CV_LINK_TIE || G.tie_nodes == 3) continue;
        uint32_t n;
        uint32_t* ix = surf_nodes(dk, l->surf[0], &n);
        for (uint32_t j = 0; j < n; j++) {
            if (shown && !shown[ix[j]]) continue;
            bool f = untied(G.frd.node_id[ix[j]]);
            if (f ? G.tie_nodes == 1 : G.tie_nodes == 2) continue;
            vb_node(f ? &pf : &pt, ix[j]);
            K.drawn[f ? CV_KEY_FREE : CV_KEY_TIED]++;
        }
        free(ix);
    }
    vb_up(&pm, CV_AUX_PMST); vb_up(&ps, CV_AUX_PSLV); vb_up(&pt, CV_AUX_PTIED); vb_up(&pf, CV_AUX_PFREE);

    /* the contact elements of the set on screen */
    int k = pick_set();
    K.set = k;
    if (k >= 0) {
        const cv_cel* c = &K.cel;
        if (K.uset != k) {                          /* sorted once per set: playback only moves them */
            free(K.uniq);
            K.uniq = malloc(((size_t)c->sets[k].n + 1) * sizeof(uint32_t));
            K.nuniq = K.uniq ? cv_cel_unique(c, k, K.uniq) : 0;
            K.uset = K.uniq ? k : -1;
        }
        const uint32_t* u = K.uniq;
        uint32_t nu = K.nuniq;
        vbuf cm = {0}, cl = {0}, csl = {0};
        if (++SK.stamp == 0) { if (SK.seen) memset(SK.seen, 0, SK.cap * sizeof(uint32_t)); SK.stamp = 1; }
        for (uint32_t j = 0; j < nu; j++) {
            const cv_celem* e = &c->elem[u[j]];
            uint32_t mi[4], si[4];
            if (!to_ix(e->m, e->nm, mi) || !to_ix(e->s, e->ns, si)) continue;    /* not this model's nodes */
            K.count++;
            if (!G.cel_show || (shown && !shown[si[0]])) continue;
            if (G.cel_master) {
                uint32_t sl = sk_slot(mi, e->nm);       /* each master face once */
                if (sl == UINT32_MAX || SK.seen[sl] != SK.stamp) {
                    if (sl != UINT32_MAX) { face_skin(&cm, SK.code[sl] >> 3, (int)(SK.code[sl] & 7)); SK.seen[sl] = SK.stamp; }
                    else face_tri(&cm, mi, e->nm);
                    face_ln(&cl, mi, e->nm);
                    K.drawn[CV_KEY_MASTER]++;
                }
            }
            if (e->kind == CV_CEL_S2S) {
                face_ln(&csl, si, e->ns);
                K.drawn[CV_KEY_CSLAVE]++; K.s2s = true;
            }
        }
        vb_up(&cm, CV_AUX_CMST); vb_up(&cl, CV_AUX_CMLN);
        float r = 1e-6f * G.diag;                    /* hair lines, widened to a couple of pixels */
        deck_lines_inst(CV_INST_CSLN, &csl.p, &csl.d, r);
        cv_free_vec(csl.p); cv_free_vec(csl.d);
    }
    app_cdisp_refresh();
    free(shown);
}
