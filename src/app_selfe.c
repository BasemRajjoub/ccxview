/* app_selfe.c -- the selected faces (G.self: element << 3 | face, as G.skin.face) and
   edges (G.sele: corner node pairs low, high), beside the elements and nodes of
   app_select.c: combined by the mode, taken by a box or lasso from the nodes it
   holds, toggled by a click, drawn on the model's own skin (the faces' triangles
   toned, their edges and the edges selected as dots), dropped with what is hidden.
   Not in the named selections. */
#include "app_int.h"
#include "selset.h"
#include "seltopo.h"
#include "pickinfo.h"
#include <stdlib.h>

/* what was selected before the box (its mode), and per skin face whether it faced
   the camera as the box was dragged: a changed tick takes the same box again */
static struct { uint32_t *f, *e; uint32_t nf, ne; int mode; uint8_t* front; } base;

static void base_forget(void) {
    free(base.f); free(base.e); free(base.front);
    memset(&base, 0, sizeof base);
}

void app_selfe_clear(void) {
    free(G.self); G.self = NULL; G.self_n = 0;
    free(G.sele); G.sele = NULL; G.sele_n = 0;
    base_forget();
}

static uint32_t* dup32(const uint32_t* a, uint32_t n) {
    uint32_t* r = n ? malloc((size_t)n * sizeof *r) : NULL;
    if (r) memcpy(r, a, (size_t)n * sizeof *r);
    return r;
}

static int cmp64(const void* a, const void* b) {
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return x < y ? -1 : x > y;
}

/* edges as sorted keys low << 32 | high, each once; *n in, then the count */
static uint64_t* edge_keys(const uint32_t* e, uint32_t* n) {
    uint64_t* k = malloc((size_t)CV_MAX(*n, 1) * sizeof *k);
    if (!k) { *n = 0; return NULL; }
    for (uint32_t i = 0; i < *n; i++) {
        uint32_t a = e[2 * i], b = e[2 * i + 1];
        k[i] = (uint64_t)CV_MIN(a, b) << 32 | CV_MAX(a, b);
    }
    qsort(k, *n, sizeof *k, cmp64);
    uint32_t m = 0;
    for (uint32_t i = 0; i < *n; i++) if (!m || k[i] != k[m - 1]) k[m++] = k[i];
    *n = m;
    return k;
}

static bool has_key(const uint64_t* k, uint32_t n, uint64_t v) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) { uint32_t m = (lo + hi) / 2; if (k[m] < v) lo = m + 1; else hi = m; }
    return lo < n && k[lo] == v;
}

/* the edges ed (pairs) with G.sele by mode */
static bool edges_combine(int mode, const uint32_t* ed, uint32_t nb) {
    uint32_t na = G.sele_n;
    uint64_t* a = edge_keys(G.sele, &na);
    uint64_t* b = edge_keys(ed, &nb);
    uint32_t* out = malloc((size_t)CV_MAX(na + nb, 1) * 2 * sizeof *out);
    if (!a || !b || !out) { free(a); free(b); free(out); return false; }
    uint32_t n = 0;
    #define PUT(v) (out[2 * n] = (uint32_t)((v) >> 32), out[2 * n + 1] = (uint32_t)(v), n++)
    if (mode == CV_SEL_NEW) for (uint32_t i = 0; i < nb; i++) PUT(b[i]);
    else if (mode == CV_SEL_ADD) {
        uint32_t i = 0, j = 0;
        while (i < na || j < nb) {
            uint64_t v = j >= nb || (i < na && a[i] <= b[j]) ? a[i] : b[j];
            PUT(v);
            while (i < na && a[i] == v) i++;
            while (j < nb && b[j] == v) j++;
        }
    } else for (uint32_t i = 0; i < na; i++)
        if (has_key(b, nb, a[i]) == (mode == CV_SEL_AND)) PUT(a[i]);
    #undef PUT
    free(a); free(b);
    free(G.sele);
    G.sele = n ? out : NULL; G.sele_n = n;
    if (!n) free(out);
    return true;
}

/* which: 1 faces, 2 edges; by mode, NEW empties the kind not given too */
static bool combine(const uint32_t* fc, uint32_t nf, const uint32_t* ed, uint32_t ne, int mode, int which) {
    bool ok = true;
    if (which & 1) {
        uint32_t *out = NULL, n = 0;
        ok = cv_sel_combine(mode, G.self, G.self_n, fc, nf, G.frd.n_elems * 8u, &out, &n);
        if (ok) { free(G.self); G.self = out; G.self_n = n; }
    } else if (mode == CV_SEL_NEW) { free(G.self); G.self = NULL; G.self_n = 0; }
    if (which & 2) ok = edges_combine(mode, ed, ne) && ok;
    else if (mode == CV_SEL_NEW) { free(G.sele); G.sele = NULL; G.sele_n = 0; }
    return ok;
}

bool app_selfe_apply(const uint32_t* fc, uint32_t nf, const uint32_t* ed, uint32_t ne, int mode, int which) {
    if (!G.loaded) return false;
    base_forget();
    bool ok = combine(fc, nf, ed, ne, mode, which);
    app_sel_refresh();
    return ok && (G.self_n || G.sele_n);
}

/* per element, a bit per skin face of it */
static uint8_t* skin_mask(void) {
    uint8_t* m = calloc(CV_MAX(G.frd.n_elems, 1), 1);
    for (size_t k = 0; m && k < G.skin.n_face; k++) {
        uint32_t e = G.skin.face[k] >> 3;
        if (e < G.frd.n_elems) m[e] |= (uint8_t)(1u << (G.skin.face[k] & 7));
    }
    return m;
}

/* the face of mask m[e] skin triangle t lies on (element << 3 | face), UINT32_MAX none */
static uint32_t tri_face(const uint8_t* m, size_t t) {
    uint32_t e = G.skin.tri_elem[t];
    if (e >= G.frd.n_elems) return UINT32_MAX;
    const uint32_t* v = G.skin.tri + 3 * t;
    for (int f = 0; f < 8; f++) {
        if (!(m[e] >> f & 1)) continue;
        uint32_t nd[8];
        int n = cv_elem_face_nodes(&G.frd, e, f, nd), hit = 0;
        for (int a = 0; a < 3; a++) for (int q = 0; q < n; q++) if (nd[q] == v[a]) { hit++; break; }
        if (hit == 3) return e << 3 | (uint32_t)f;
    }
    return UINT32_MAX;
}

/* per skin face, 1: one of its triangles turns toward the camera (app_sel_facing) */
static uint8_t* faces_front(void) {
    uint8_t* fn = malloc(CV_MAX(G.frd.n_nodes, 1));
    uint8_t* fe = malloc(CV_MAX(G.frd.n_elems, 1));
    uint8_t* ft = malloc(CV_MAX(G.skin.n_tri, 1));
    uint8_t* m = skin_mask();
    uint32_t* code = malloc((size_t)CV_MAX(G.skin.n_face, 1) * sizeof *code);
    uint8_t* out = calloc(CV_MAX(G.skin.n_face, 1), 1);
    if (fn && fe && ft && m && code && out) {
        app_sel_facing(fn, fe, ft);
        uint32_t nf = (uint32_t)G.skin.n_face;
        memcpy(code, G.skin.face, (size_t)nf * sizeof *code);
        for (size_t t = 0; t < G.skin.n_tri; t++) {
            if (!ft[t]) continue;
            uint32_t c = tri_face(m, t);
            for (uint32_t k = 0; c != UINT32_MAX && k < nf; k++) if (code[k] == c) { out[k] = 1; break; }
        }
    } else { free(out); out = NULL; }
    free(fn); free(fe); free(ft); free(m); free(code);
    return out;
}

static bool beam(uint32_t e) { return G.frd.etype[e] == 11 || G.frd.etype[e] == 12; }

/* The faces and edges ticked (G.pick_kinds) from the nodes a box or lasso held: the
   skin faces with every node inside (crossing: any), the skin's element edges (and
   the shown beams') with both ends inside (any); on the side facing the camera when
   asked. fresh: a new box, what was selected before it kept for a reselect. Combined
   with that by the box's mode; how many it took. */
uint32_t app_selfe_box(const uint8_t* inside, bool crossing, int mode, bool fresh) {
    if (fresh) {
        base_forget();
        base.mode = mode;
        if (mode != CV_SEL_NEW) {
            base.f = dup32(G.self, G.self_n); base.nf = base.f ? G.self_n : 0;
            base.e = dup32(G.sele, 2 * G.sele_n); base.ne = base.e ? G.sele_n : 0;
        }
    }
    if (G.sel_visible && !base.front) base.front = faces_front();
    const uint8_t* front = G.sel_visible ? base.front : NULL;
    bool wf = app_pk(CV_PK_FACE), we = app_pk(CV_PK_EDGE);
    size_t NF = G.skin.n_face;
    uint32_t* fc = malloc((size_t)CV_MAX(NF, 1) * sizeof *fc);
    uint32_t* ed = malloc((size_t)CV_MAX(NF * 4 + G.frd.n_elems, 1) * 2 * sizeof *ed);
    uint32_t nf = 0, ne = 0;
    if (!fc || !ed) { free(fc); free(ed); return 0; }
    for (size_t k = 0; k < NF && (wf || we); k++) {
        if (front && !front[k]) continue;
        uint32_t e = G.skin.face[k] >> 3, nd[8], c[4];
        int f = (int)(G.skin.face[k] & 7);
        if (wf) {
            int n = cv_elem_face_nodes(&G.frd, e, f, nd), in = 0;
            for (int i = 0; i < n; i++) in += inside[nd[i]] != 0;
            if (n && (crossing ? in > 0 : in == n)) fc[nf++] = G.skin.face[k];
        }
        if (we) {
            int m = cv_elem_face_corners(&G.frd, e, f, c);
            for (int j = 0; j < m && m > 1; j++) {
                uint32_t a = c[j], b = c[(j + 1) % m];
                if (crossing ? inside[a] || inside[b] : inside[a] && inside[b]) { ed[2 * ne] = a; ed[2 * ne + 1] = b; ne++; }
            }
        }
    }
    for (uint32_t e = 0; we && e < G.frd.n_elems; e++) {   /* beams: no face, their ends */
        if (!beam(e) || (G.vis && !G.vis[e])) continue;
        uint32_t q[4][3];
        int n = cv_elem_edges(&G.frd, e, q, 4);
        for (int i = 0; i < n && ne < NF * 4 + G.frd.n_elems; i++) {
            uint32_t a = q[i][0], b = q[i][1];
            if (crossing ? inside[a] || inside[b] : inside[a] && inside[b]) { ed[2 * ne] = a; ed[2 * ne + 1] = b; ne++; }
        }
    }
    uint32_t nf0 = nf, ne0 = ne;
    {   /* what was there before the box, then the box by its mode */
        free(G.self); G.self = dup32(base.f, base.nf); G.self_n = G.self ? base.nf : 0;
        free(G.sele); G.sele = dup32(base.e, 2 * base.ne); G.sele_n = G.sele ? base.ne : 0;
        combine(fc, nf, ed, ne, base.mode, (wf ? 1 : 0) | (we ? 2 : 0));
    }
    free(fc); free(ed);
    return nf0 + ne0;
}

/* one node's vertex: undeformed, its displacement for the shader */
static void push_pt(cv_fvec* pos, cv_fvec* disp, uint32_t i) {
    float d[6];
    app_node_disp6(i, d);
    if (cv_reserve(*pos, pos->n + 3)) for (int q = 0; q < 3; q++) pos->a[pos->n++] = G.frd.xyz[3 * i + q];
    if (cv_reserve(*disp, disp->n + 6)) for (int q = 0; q < 6; q++) disp->a[disp->n++] = d[q];
}

static void push_line(cv_fvec* lp, cv_fvec* ld, uint32_t a, uint32_t b) { push_pt(lp, ld, a); push_pt(lp, ld, b); }

/* The selected faces' skin triangles (tp, td, values tv) and edges, the selected edges
   (as vertex pairs lp, ld): on the mesh's own skin, through the mid-side nodes when the
   faces are drawn so (G.mid_faces) */
void app_selfe_draw(cv_fvec* lp, cv_fvec* ld, cv_fvec* tp, cv_fvec* td, cv_fvec* tv) {
    if (!G.loaded || (!G.self_n && !G.sele_n)) return;
    uint32_t E = G.frd.n_elems;
    const float* nv = G.has_field && !G.elem_mode ? G.scalar : NULL;
    const float* ev = G.has_field && G.elem_mode ? G.elem_val : NULL;
    if (G.self_n) {
        uint8_t* m = calloc(CV_MAX(E, 1), 1);
        if (!m) return;
        for (uint32_t k = 0; k < G.self_n; k++) {
            uint32_t e = G.self[k] >> 3, c[8];
            int f = (int)(G.self[k] & 7);
            if (e >= E) continue;
            m[e] |= (uint8_t)(1u << f);
            int n = G.mid_faces ? cv_face_ring(&G.frd, e, f, c) : cv_elem_face_corners(&G.frd, e, f, c);
            for (int j = 0; j < n; j++) push_line(lp, ld, c[j], c[(j + 1) % n]);
        }
        for (size_t t = 0; t < G.skin.n_tri; t++) {
            uint32_t e = G.skin.tri_elem[t];
            if (e >= E || !m[e] || tri_face(m, t) == UINT32_MAX) continue;
            for (int j = 0; j < 3; j++) {
                uint32_t i = G.skin.tri[3 * t + j];
                push_pt(tp, td, i);
                if (cv_reserve(*tv, tv->n + 1)) tv->a[tv->n++] = nv ? nv[i] : ev ? ev[e] : 0.f;
            }
        }
        free(m);
    }
    if (!G.sele_n) return;
    uint32_t n = G.sele_n;
    uint64_t* k = edge_keys(G.sele, &n);
    uint8_t* done = calloc(CV_MAX(n, 1), 1);
    if (k && done && G.mid_faces)                      /* through the mid node: from the skin faces' rings */
        for (size_t q = 0; q < G.skin.n_face; q++) {
            uint32_t r[8];
            int nr = cv_face_ring(&G.frd, G.skin.face[q] >> 3, (int)(G.skin.face[q] & 7), r);
            if (nr != 6 && nr != 8) continue;
            for (int j = 0; j < nr; j += 2) {
                uint32_t a = r[j], mid = r[j + 1], b = r[(j + 2) % nr];
                uint64_t v = (uint64_t)CV_MIN(a, b) << 32 | CV_MAX(a, b);
                uint32_t lo = 0, hi = n;
                while (lo < hi) { uint32_t md = (lo + hi) / 2; if (k[md] < v) lo = md + 1; else hi = md; }
                if (lo >= n || k[lo] != v || done[lo]) continue;
                done[lo] = 1;
                push_line(lp, ld, a, mid); push_line(lp, ld, mid, b);
            }
        }
    for (uint32_t i = 0; k && done && i < n; i++)
        if (!done[i]) push_line(lp, ld, (uint32_t)(k[i] >> 32), (uint32_t)k[i]);
    free(k); free(done);
}

/* the faces of hidden elements, the edges with an end not shown (sn: per node) out */
bool app_selfe_drop_hidden(const uint8_t* sn) {
    uint32_t a = 0, b = 0, f0 = G.self_n, e0 = G.sele_n;
    for (uint32_t k = 0; k < G.self_n; k++) if (!G.vis || G.vis[G.self[k] >> 3]) G.self[a++] = G.self[k];
    for (uint32_t k = 0; k < G.sele_n; k++)
        if (sn[G.sele[2 * k]] && sn[G.sele[2 * k + 1]]) { G.sele[2 * b] = G.sele[2 * k]; G.sele[2 * b + 1] = G.sele[2 * k + 1]; b++; }
    G.self_n = a; G.sele_n = b;
    return a != f0 || b != e0;
}

static bool in32(const uint32_t* a, uint32_t n, uint32_t v) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) { uint32_t m = (lo + hi) / 2; if (a[m] < v) lo = m + 1; else hi = m; }
    return lo < n && a[lo] == v;
}

static bool edge_in(uint32_t a, uint32_t b) {
    uint32_t lo = CV_MIN(a, b), hi = CV_MAX(a, b);
    for (uint32_t k = 0; k < G.sele_n; k++) if (G.sele[2 * k] == lo && G.sele[2 * k + 1] == hi) return true;
    return false;
}

/* The face (element << 3 | face, UINT32_MAX none) and edge (corners, [0] UINT32_MAX
   none) of a click, the kinds ticked, by mode; NEW: each in if it is out, out if in */
bool app_selfe_take(uint32_t face, const uint32_t edge[2], int mode) {
    bool any = false;
    if (app_pk(CV_PK_FACE) && face != UINT32_MAX) {
        int m = mode == CV_SEL_NEW ? (in32(G.self, G.self_n, face) ? CV_SEL_REMOVE : CV_SEL_ADD) : mode;
        combine(&face, 1, NULL, 0, m, 1);
        any = true;
    }
    if (app_pk(CV_PK_EDGE) && edge[0] != UINT32_MAX) {
        int m = mode == CV_SEL_NEW ? (edge_in(edge[0], edge[1]) ? CV_SEL_REMOVE : CV_SEL_ADD) : mode;
        combine(NULL, 0, edge, 1, m, 2);
        any = true;
    }
    if (any) { base_forget(); app_sel_refresh(); }
    return any;
}
