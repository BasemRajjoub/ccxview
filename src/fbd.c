/* fbd.c -- cgx geometry records -> points, tessellated curves, surface patches. */
#include "fbd.h"
#include "frd.h"      /* cv_parse_num */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include "stb_sprintf.h"

typedef struct { char s[32]; } nm;
typedef struct { nm n; int8_t sign; } snm;

typedef struct { nm name; float x, y, z; } rpnt;
typedef struct { nm name; size_t first, n; } rseq;                 /* items: nms */
typedef struct { nm name, a, b, c; bool has_c; int div; } rline;
typedef struct { nm name; size_t first, n; } rlcmb;               /* items: snms */
typedef struct { nm name; size_t first, n; int8_t sign; } rsurf;  /* items: snms; GSUR and GBOD */
typedef struct { nm set; char type; size_t first, n; } rseta;      /* items: nms */

typedef struct { const char* key; uint32_t idx; } kv;

static int kv_cmp(const void* a, const void* b) { return strcmp(((const kv*)a)->key, ((const kv*)b)->key); }

static uint32_t kv_find(const kv* t, size_t n, const char* key) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t m = (lo + hi) / 2;
        int c = strcmp(t[m].key, key);
        if (c == 0) return t[m].idx;
        if (c < 0) lo = m + 1; else hi = m;
    }
    return UINT32_MAX;
}

static int tokenize(char* s, char** tok, int max) {
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ' || *s == '\t' || *s == '\r') s++;
        if (!*s) break;
        tok[n++] = s;
        while (*s && *s != ' ' && *s != '\t' && *s != '\r') s++;
        if (*s) *s++ = 0;
    }
    return n;
}

static bool num(const char* s, float* v) {
    double d;
    if (!cv_parse_num(s, s + strlen(s), &d)) return false;
    *v = (float)d;
    return true;
}

static void nmset(nm* o, const char* s) { snprintf(o->s, sizeof o->s, "%s", s); }

static bool kw_is(const char* t, const char* k) {
    for (; *t && *k; t++, k++) if (toupper((unsigned char)*t) != *k) return false;
    return !*t && !*k;
}

/* ---- geometry helpers ---------------------------------------------------------- */

typedef struct { float x, y, z; } f3;
typedef CV_VEC(f3) f3vec;

static f3 f3_of(const float* p) { f3 r = { p[0], p[1], p[2] }; return r; }
static f3 sub(f3 a, f3 b) { f3 r = { a.x - b.x, a.y - b.y, a.z - b.z }; return r; }
static f3 add(f3 a, f3 b) { f3 r = { a.x + b.x, a.y + b.y, a.z + b.z }; return r; }
static f3 mul(f3 a, float s) { f3 r = { a.x * s, a.y * s, a.z * s }; return r; }
static float dot(f3 a, f3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static f3 cross(f3 a, f3 b) { f3 r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; return r; }
static float len(f3 a) { return sqrtf(dot(a, a)); }

/* arc from a to b around centre c (the shorter way, as cgx draws it) */
static void tess_arc(f3vec* o, f3 a, f3 b, f3 c) {
    f3 va = sub(a, c), vb = sub(b, c);
    float ra = len(va), rb = len(vb);
    if (ra <= 0 || rb <= 0) { cv_push(*o, a); cv_push(*o, b); return; }
    f3 ua = mul(va, 1.f / ra);
    f3 n = cross(va, vb);
    float nl = len(n);
    float cosang = dot(va, vb) / (ra * rb);
    cosang = cosang > 1 ? 1 : cosang < -1 ? -1 : cosang;
    float ang = acosf(cosang);
    if (nl <= 1e-12f * ra * rb) { cv_push(*o, a); cv_push(*o, b); return; }  /* degenerate */
    f3 w = mul(cross(mul(n, 1.f / nl), ua), 1.f);         /* in-plane, 90 deg from ua */
    int seg = 2 + (int)(ang / 0.08f);
    for (int i = 0; i <= seg; i++) {
        float t = (float)i / (float)seg, th = ang * t, r = ra + (rb - ra) * t;
        f3 p = add(c, add(mul(ua, r * cosf(th)), mul(w, r * sinf(th))));
        cv_push(*o, p);
    }
}

/* Catmull-Rom through the sequence points */
static void tess_spline(f3vec* o, const f3* q, size_t n) {
    if (n < 2) { if (n) cv_push(*o, q[0]); return; }
    for (size_t i = 0; i + 1 < n; i++) {
        f3 p0 = q[i ? i - 1 : 0], p1 = q[i], p2 = q[i + 1], p3 = q[i + 2 < n ? i + 2 : n - 1];
        int seg = 8;
        for (int k = (i ? 1 : 0); k <= seg; k++) {
            float t = (float)k / seg, t2 = t * t, t3 = t2 * t;
            f3 p = mul(add(add(mul(p1, 2.f), mul(sub(p2, p0), t)),
                       add(mul(add(sub(mul(p0, 2.f), mul(p1, 5.f)), sub(mul(p2, 4.f), p3)), t2),
                           mul(add(sub(mul(p1, 3.f), p0), sub(p3, mul(p2, 3.f))), t3))), 0.5f);
            cv_push(*o, p);
        }
    }
}

/* resample a polyline to m points, evenly by arc length */
static void resample(const f3* p, size_t n, f3* out, int m) {
    float total = 0;
    for (size_t i = 1; i < n; i++) total += len(sub(p[i], p[i - 1]));
    if (n < 2 || total <= 0) { for (int k = 0; k < m; k++) out[k] = n ? p[0] : (f3){ 0, 0, 0 }; return; }
    size_t seg = 1;
    float acc = 0;
    for (int k = 0; k < m; k++) {
        float target = total * (float)k / (float)(m - 1);
        while (seg < n - 1 && acc + len(sub(p[seg], p[seg - 1])) < target) { acc += len(sub(p[seg], p[seg - 1])); seg++; }
        float l = len(sub(p[seg], p[seg - 1]));
        float t = l > 0 ? (target - acc) / l : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        out[k] = add(p[seg - 1], mul(sub(p[seg], p[seg - 1]), t));
    }
}

/* append, growing by doubling (a set can hold a whole mesh) */
static bool push_u32(uint32_t** a, uint32_t* n, uint32_t v) {
    uint32_t k = *n;
    if ((k & (k - 1)) == 0) {                   /* 0, 1, 2, 4, ...: full */
        uint32_t* q = realloc(*a, (size_t)(k ? 2 * k : 1) * sizeof *q);
        if (!q) return false;
        *a = q;
    }
    (*a)[k] = v;
    *n = k + 1;
    return true;
}

/* ---- native mapped mesh ------------------------------------------------------------
   What cgx's `mesh all` does for the simple, common case, so a model builds without
   cgx: lines (BE2/BE3), four-sided surfaces (QU4/QU8/TR3/TR6) and six-sided bodies of
   four-sided faces (HE8/HE20), as ELTY assigns them. Divisions come from the lines
   (cgx's default 4; quadratic elements take two divisions each); opposite edges of a
   patch take the larger count. Points are placed by transfinite interpolation of the
   edge curves -- a Coons patch for surfaces, the 12-edge form for bodies -- and
   coincident nodes of neighbouring patches merge. Written as Abaqus text (*NODE,
   *ELEMENT, *ELSET, *NSET) for the deck reader, like cgx's `send all abq`. */

typedef struct { nm set; char ty[16]; } relty;

typedef struct {
    const rpnt* P;  size_t np; const kv* pk;
    const rline* L; size_t nl; const kv* lk;
    const rlcmb* C; size_t nc; const kv* ck;
    const snm* sitems;
    const float* cx; const uint32_t* coff;      /* the tessellated lines */
} mctx;

/* an edge as a surface or body uses it: oriented polyline, end points, divisions */
typedef struct { uint32_t id, a, b; int div; f3vec poly; } medge;

static int line_div(int d) {
    if (d > 99) d %= 100;                        /* a bias is coded above 99: not used */
    return d > 0 ? d : 4;
}

static void rev_poly(f3vec* v) {
    for (size_t u = 0; u < v->n / 2; u++) { f3 t = v->a[u]; v->a[u] = v->a[v->n - 1 - u]; v->a[v->n - 1 - u] = t; }
}

static bool edge_get(const mctx* m, const snm* e, medge* o) {
    memset(o, 0, sizeof *o);
    uint32_t li = kv_find(m->lk, m->nl, e->n.s);
    if (li != UINT32_MAX) {
        const rline* r = &m->L[li];
        o->id = li;
        o->a = kv_find(m->pk, m->np, r->a.s); o->b = kv_find(m->pk, m->np, r->b.s);
        o->div = line_div(r->div);
        for (uint32_t v = m->coff[li]; v < m->coff[li + 1]; v++) cv_push(o->poly, f3_of(m->cx + 3 * v));
    } else {
        uint32_t ci = kv_find(m->ck, m->nc, e->n.s);
        if (ci == UINT32_MAX) return false;
        const rlcmb* cb = &m->C[ci];
        o->id = (uint32_t)m->nl + ci;
        for (size_t k = 0; k < cb->n; k++) {
            const snm* ce = &m->sitems[cb->first + k];
            uint32_t l2 = kv_find(m->lk, m->nl, ce->n.s);
            if (l2 == UINT32_MAX) { cv_free_vec(o->poly); return false; }
            const rline* r = &m->L[l2];
            uint32_t pa = kv_find(m->pk, m->np, r->a.s), pb = kv_find(m->pk, m->np, r->b.s);
            if (ce->sign < 0) { uint32_t t = pa; pa = pb; pb = t; }
            if (!k) o->a = pa;
            o->b = pb;
            o->div += line_div(r->div);
            f3vec part = {0};
            for (uint32_t v = m->coff[l2]; v < m->coff[l2 + 1]; v++) cv_push(part, f3_of(m->cx + 3 * v));
            if (ce->sign < 0) rev_poly(&part);
            for (size_t u = o->poly.n ? 1 : 0; u < part.n; u++) cv_push(o->poly, part.a[u]);
            cv_free_vec(part);
        }
    }
    if (o->a == UINT32_MAX || o->b == UINT32_MAX || o->poly.n < 2) { cv_free_vec(o->poly); return false; }
    if (e->sign < 0) { uint32_t t = o->a; o->a = o->b; o->b = t; rev_poly(&o->poly); }
    return true;
}

/* node intervals along an edge of `div` divisions for elements of order q */
static int edge_ints(int div, int q) { return q == 2 ? 2 * CV_MAX(1, div / 2) : CV_MAX(1, div); }

/* the four edges of a surface, oriented head to tail */
static bool surf_edges(const mctx* m, const rsurf* s, medge e[4]) {
    if (s->n != 4) return false;
    for (int j = 0; j < 4; j++) {
        if (!edge_get(m, &m->sitems[s->first + j], &e[j])) { for (int k = 0; k < j; k++) cv_free_vec(e[k].poly); return false; }
    }
    /* cgx's signs make a loop; fix any edge still pointing the wrong way */
    if (e[0].b != e[1].a && e[0].b != e[1].b) { uint32_t t = e[0].a; e[0].a = e[0].b; e[0].b = t; rev_poly(&e[0].poly); }
    for (int j = 1; j < 4; j++)
        if (e[j].a != e[j - 1].b) { uint32_t t = e[j].a; e[j].a = e[j].b; e[j].b = t; rev_poly(&e[j].poly); }
    bool ok = true;
    for (int j = 0; j < 4; j++) ok &= e[j].a == e[(j + 3) % 4].b;
    if (!ok) for (int j = 0; j < 4; j++) cv_free_vec(e[j].poly);
    return ok;
}

/* nodes, merged when they coincide: a hash of small cells, neighbours searched */
typedef struct {
    CV_VEC(float) xyz;
    uint32_t* head; uint32_t hmask;
    CV_VEC(uint32_t) next;
    float tol, h;
} mnodes;

static uint32_t cell_hash(int64_t i, int64_t j, int64_t k) {
    return (uint32_t)(((uint64_t)i * 73856093u) ^ ((uint64_t)j * 19349663u) ^ ((uint64_t)k * 83492791u));
}

static bool mn_rehash(mnodes* s, uint32_t size) {
    uint32_t* h = malloc((size_t)size * sizeof *h);
    if (!h) return false;
    memset(h, 0xff, (size_t)size * sizeof *h);
    free(s->head); s->head = h; s->hmask = size - 1;
    for (size_t n = 0; n < s->next.n; n++) {
        const float* p = s->xyz.a + 3 * n;
        uint32_t c = cell_hash((int64_t)floorf(p[0] / s->h), (int64_t)floorf(p[1] / s->h), (int64_t)floorf(p[2] / s->h)) & s->hmask;
        s->next.a[n] = s->head[c]; s->head[c] = (uint32_t)n;
    }
    return true;
}

static uint32_t mn_add(mnodes* s, f3 p) {
    int64_t ci = (int64_t)floorf(p.x / s->h), cj = (int64_t)floorf(p.y / s->h), ck = (int64_t)floorf(p.z / s->h);
    for (int di = -1; di <= 1; di++) for (int dj = -1; dj <= 1; dj++) for (int dk = -1; dk <= 1; dk++) {
        for (uint32_t n = s->head[cell_hash(ci + di, cj + dj, ck + dk) & s->hmask]; n != UINT32_MAX; n = s->next.a[n]) {
            const float* q = s->xyz.a + 3 * n;
            if (fabsf(q[0] - p.x) <= s->tol && fabsf(q[1] - p.y) <= s->tol && fabsf(q[2] - p.z) <= s->tol) return n;
        }
    }
    uint32_t n = (uint32_t)s->next.n;
    if (!cv_push(s->xyz, p.x) || !cv_push(s->xyz, p.y) || !cv_push(s->xyz, p.z) || !cv_push(s->next, UINT32_MAX)) return UINT32_MAX;
    uint32_t c = cell_hash(ci, cj, ck) & s->hmask;
    s->next.a[n] = s->head[c]; s->head[c] = n;
    if (s->next.n > 2 * (size_t)s->hmask + 2 && !mn_rehash(s, 2 * (s->hmask + 1))) return UINT32_MAX;
    return n;
}

static const struct { const char* cgx; const char* abq; int nn, q; char kind; bool r; } kMeshType[] = {
    { "BE2", "B31", 2, 1, 'l', true }, { "BE3", "B32", 3, 2, 'l', true },
    { "TR3", "S3", 3, 1, 's', false }, { "TR6", "S6", 6, 2, 's', false },
    { "QU4", "S4", 4, 1, 's', true },  { "QU8", "S8", 8, 2, 's', true },
    { "HE8", "C3D8", 8, 1, 'b', true }, { "HE20", "C3D20", 20, 2, 'b', true },
};

/* ELTY's type -> table row, *2 + 1 when it asks for reduced integration; -1 unknown */
static int mesh_type(const char* s) {
    for (int i = 0; i < (int)CV_COUNT(kMeshType); i++) {
        size_t n = strlen(kMeshType[i].cgx);
        bool eq = true;
        for (size_t k = 0; k < n && eq; k++) eq = toupper((unsigned char)s[k]) == kMeshType[i].cgx[k];
        if (!eq || isdigit((unsigned char)s[n])) continue;
        return 2 * i + (kMeshType[i].r && toupper((unsigned char)s[n]) == 'R');
    }
    return -1;
}

typedef struct {
    mnodes nodes;
    CV_VEC(uint32_t) conn;                       /* node indices, per element */
    CV_VEC(uint32_t) eoff;                       /* element e: conn[eoff[e] .. eoff[e+1]) */
    CV_VEC(uint8_t) ety;                         /* mesh_type() code */
    bool oom;
} mesh;

static void add_elem(mesh* M, int ty, const uint32_t* nd, const int* perm) {
    int nn = kMeshType[ty / 2].nn;
    for (int k = 0; k < nn; k++) {
        uint32_t v = nd[perm ? perm[k] : k];
        if (v == UINT32_MAX) { M->oom = true; return; }
        M->oom |= !cv_push(M->conn, v);
    }
    M->oom |= !cv_push(M->ety, (uint8_t)ty) || !cv_push(M->eoff, (uint32_t)M->conn.n);
}

static f3* sample(const medge* e, int n, bool reverse) {
    f3* o = malloc((size_t)(n + 1) * sizeof *o);
    if (!o) return NULL;
    resample(e->poly.a, e->poly.n, o, n + 1);
    if (reverse) for (int u = 0; u < (n + 1) / 2; u++) { f3 t = o[u]; o[u] = o[n - u]; o[n - u] = t; }
    return o;
}

static void mesh_line(mesh* M, const mctx* m, uint32_t li, int ty) {
    snm e = { .sign = 1 };
    e.n = m->L[li].name;
    medge g;
    if (!edge_get(m, &e, &g)) return;
    int q = kMeshType[ty / 2].q, n = edge_ints(g.div, q);
    f3* p = sample(&g, n, false);
    cv_free_vec(g.poly);
    if (!p) { M->oom = true; return; }
    uint32_t prev = mn_add(&M->nodes, p[0]);
    for (int i = q; i <= n && !M->oom; i += q) {
        uint32_t nd[3] = { prev, mn_add(&M->nodes, p[i - q + 1]), mn_add(&M->nodes, p[i]) };
        if (q == 2) add_elem(M, ty, nd, NULL);       /* B32: end, middle, end */
        else { nd[1] = nd[2]; add_elem(M, ty, nd, NULL); }
        prev = nd[q == 2 ? 2 : 1];
    }
    free(p);
}

static void mesh_surface(mesh* M, const mctx* m, const rsurf* s, int sign, int ty) {
    medge e[4];
    if (!surf_edges(m, s, e)) return;
    int q = kMeshType[ty / 2].q;
    int nu = CV_MAX(edge_ints(e[0].div, q), edge_ints(e[2].div, q));
    int nv = CV_MAX(edge_ints(e[1].div, q), edge_ints(e[3].div, q));
    f3 *B = sample(&e[0], nu, false), *R = sample(&e[1], nv, false);
    f3 *T = sample(&e[2], nu, true), *Lf = sample(&e[3], nv, true);
    uint32_t* id = malloc((size_t)(nu + 1) * (nv + 1) * sizeof *id);
    if (B && R && T && Lf && id) {
        f3 p00 = B[0], p10 = B[nu], p11 = T[nu], p01 = T[0];
        for (int j = 0; j <= nv; j++) for (int i = 0; i <= nu; i++) {
            uint32_t* d = &id[j * (nu + 1) + i];
            *d = UINT32_MAX;
            if (q == 2 && (i & 1) && (j & 1) && kMeshType[ty / 2].cgx[0] != 'T') continue;   /* TR6 splits at it */
            float u = (float)i / nu, v = (float)j / nv;
            f3 p = add(add(mul(B[i], 1 - v), mul(T[i], v)), add(mul(Lf[j], 1 - u), mul(R[j], u)));
            p = sub(p, add(add(mul(p00, (1 - u) * (1 - v)), mul(p10, u * (1 - v))), add(mul(p11, u * v), mul(p01, (1 - u) * v))));
            if (i == 0) p = Lf[j]; else if (i == nu) p = R[j];     /* edges exactly as sampled */
            if (j == 0) p = B[i]; else if (j == nv) p = T[i];
            *d = mn_add(&M->nodes, p);
        }
#define ID(a, b) id[(b) * (nu + 1) + (a)]
        static const int rq4[4] = { 0, 3, 2, 1 }, rq8[8] = { 0, 3, 2, 1, 7, 6, 5, 4 };
        static const int rt3[3] = { 0, 2, 1 }, rt6[6] = { 0, 2, 1, 5, 4, 3 };
        const char* nm_ = kMeshType[ty / 2].cgx;
        bool tri = nm_[0] == 'T';
        for (int j = 0; j < nv && !M->oom; j += q) for (int i = 0; i < nu && !M->oom; i += q) {
            if (q == 1) {
                uint32_t c[4] = { ID(i, j), ID(i + 1, j), ID(i + 1, j + 1), ID(i, j + 1) };
                if (!tri) add_elem(M, ty, c, sign < 0 ? rq4 : NULL);
                else {
                    uint32_t a[3] = { c[0], c[1], c[2] }, b[3] = { c[0], c[2], c[3] };
                    add_elem(M, ty, a, sign < 0 ? rt3 : NULL); add_elem(M, ty, b, sign < 0 ? rt3 : NULL);
                }
            } else {
                uint32_t c[8] = { ID(i, j), ID(i + 2, j), ID(i + 2, j + 2), ID(i, j + 2),
                                  ID(i + 1, j), ID(i + 2, j + 1), ID(i + 1, j + 2), ID(i, j + 1) };
                if (!tri) add_elem(M, ty, c, sign < 0 ? rq8 : NULL);
                else {
                    uint32_t dm = ID(i + 1, j + 1);      /* on the diagonal */
                    uint32_t a[6] = { c[0], c[1], c[2], c[4], c[5], dm }, b[6] = { c[0], c[2], c[3], dm, c[6], c[7] };
                    add_elem(M, ty, a, sign < 0 ? rt6 : NULL); add_elem(M, ty, b, sign < 0 ? rt6 : NULL);
                }
            }
        }
#undef ID
    } else {
        M->oom = true;
    }
    free(B); free(R); free(T); free(Lf); free(id);
    for (int j = 0; j < 4; j++) cv_free_vec(e[j].poly);
}

/* a body of six four-sided faces -> the 12 edges on a unit cube, then hexahedra */
static bool mesh_body(mesh* M, const mctx* m, const rsurf* S, const kv* sk, size_t ns, const rsurf* b, int ty) {
    if (b->n != 6) return false;
    medge ue[12];                                /* the distinct edges */
    int nue = 0;
    uint32_t f0[4] = { 0 };
    bool ok = true;
    for (size_t f = 0; f < 6 && ok; f++) {
        uint32_t si = kv_find(sk, ns, m->sitems[b->first + f].n.s);
        medge e[4];
        if (si == UINT32_MAX || !surf_edges(m, &S[si], e)) { ok = false; break; }
        for (int j = 0; j < 4; j++) {
            if (!f) f0[j] = e[j].a;
            bool dup = false;
            for (int k = 0; k < nue && !dup; k++) dup = ue[k].id == e[j].id;
            if (dup || nue == 12) { ok &= dup; cv_free_vec(e[j].poly); continue; }
            ue[nue++] = e[j];
        }
    }
    ok &= nue == 12;
    /* cube corners: face 0 is w = 0; each of its corners has one edge leaving it */
    uint32_t K[8];
    for (int i = 0; i < 4; i++) K[i] = f0[i];
    for (int i = 0; i < 4 && ok; i++) {
        int found = 0;
        for (int k = 0; k < nue; k++) {
            uint32_t o = ue[k].a == K[i] ? ue[k].b : ue[k].b == K[i] ? ue[k].a : UINT32_MAX;
            if (o == UINT32_MAX || o == f0[0] || o == f0[1] || o == f0[2] || o == f0[3]) continue;
            K[i + 4] = o; found++;
        }
        ok &= found == 1;
    }
    /* cube edges per axis, ordered (v,w) = (0,0) (1,0) (0,1) (1,1) and alike */
    static const int CE[3][4][2] = { { { 0, 1 }, { 3, 2 }, { 4, 5 }, { 7, 6 } },
                                     { { 0, 3 }, { 1, 2 }, { 4, 7 }, { 5, 6 } },
                                     { { 0, 4 }, { 1, 5 }, { 3, 7 }, { 2, 6 } } };
    int q = kMeshType[ty / 2].q, n[3] = { 0, 0, 0 }, which[3][4];
    bool rev[3][4];
    for (int ax = 0; ax < 3 && ok; ax++) for (int c = 0; c < 4 && ok; c++) {
        uint32_t p = K[CE[ax][c][0]], r = K[CE[ax][c][1]];
        which[ax][c] = -1;
        for (int k = 0; k < nue; k++) {
            if (ue[k].a == p && ue[k].b == r) { which[ax][c] = k; rev[ax][c] = false; }
            else if (ue[k].a == r && ue[k].b == p) { which[ax][c] = k; rev[ax][c] = true; }
        }
        if (which[ax][c] < 0) ok = false;
        else n[ax] = CV_MAX(n[ax], edge_ints(ue[which[ax][c]].div, q));
    }
    f3* E[3][4] = { { 0 } };
    uint32_t* id = NULL;
    if (ok) {
        for (int ax = 0; ax < 3; ax++) for (int c = 0; c < 4; c++)
            if (!(E[ax][c] = sample(&ue[which[ax][c]], n[ax], rev[ax][c]))) M->oom = true;
        id = malloc((size_t)(n[0] + 1) * (n[1] + 1) * (n[2] + 1) * sizeof *id);
        if (!id) M->oom = true;
    }
    if (ok && !M->oom) {
        f3 X[8];
        for (int c = 0; c < 8; c++) X[c] = (f3){ m->P[K[c]].x, m->P[K[c]].y, m->P[K[c]].z };
        int nu = n[0], nv = n[1], nw = n[2];
#define ID(a, b, c) id[((size_t)(c) * (nv + 1) + (b)) * (nu + 1) + (a)]
        for (int k = 0; k <= nw; k++) for (int j = 0; j <= nv; j++) for (int i = 0; i <= nu; i++) {
            ID(i, j, k) = UINT32_MAX;
            if (q == 2 && (i & 1) + (j & 1) + (k & 1) >= 2) continue;
            float u = (float)i / nu, v = (float)j / nv, w = (float)k / nw;
            f3 p = { 0, 0, 0 };
            p = add(p, add(add(mul(E[0][0][i], (1 - v) * (1 - w)), mul(E[0][1][i], v * (1 - w))), add(mul(E[0][2][i], (1 - v) * w), mul(E[0][3][i], v * w))));
            p = add(p, add(add(mul(E[1][0][j], (1 - u) * (1 - w)), mul(E[1][1][j], u * (1 - w))), add(mul(E[1][2][j], (1 - u) * w), mul(E[1][3][j], u * w))));
            p = add(p, add(add(mul(E[2][0][k], (1 - u) * (1 - v)), mul(E[2][1][k], u * (1 - v))), add(mul(E[2][2][k], (1 - u) * v), mul(E[2][3][k], u * v))));
            f3 t = add(add(add(mul(X[0], (1 - u) * (1 - v) * (1 - w)), mul(X[1], u * (1 - v) * (1 - w))),
                           add(mul(X[2], u * v * (1 - w)), mul(X[3], (1 - u) * v * (1 - w)))),
                       add(add(mul(X[4], (1 - u) * (1 - v) * w), mul(X[5], u * (1 - v) * w)),
                           add(mul(X[6], u * v * w), mul(X[7], (1 - u) * v * w))));
            ID(i, j, k) = mn_add(&M->nodes, sub(p, mul(t, 2.f)));
        }
        /* right-handed: (X1-X0) x (X3-X0) . (X4-X0) > 0, else top and bottom swap */
        bool flip = dot(cross(sub(X[1], X[0]), sub(X[3], X[0])), sub(X[4], X[0])) < 0;
        static const int f8[8] = { 4, 5, 6, 7, 0, 1, 2, 3 };
        static const int f20[20] = { 4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11, 16, 17, 18, 19 };
        for (int k = 0; k < nw && !M->oom; k += q) for (int j = 0; j < nv && !M->oom; j += q) for (int i = 0; i < nu && !M->oom; i += q) {
            int s = q;
            uint32_t c[20] = { ID(i, j, k), ID(i + s, j, k), ID(i + s, j + s, k), ID(i, j + s, k),
                               ID(i, j, k + s), ID(i + s, j, k + s), ID(i + s, j + s, k + s), ID(i, j + s, k + s) };
            if (q == 2) {
                uint32_t mids[12] = { ID(i + 1, j, k), ID(i + 2, j + 1, k), ID(i + 1, j + 2, k), ID(i, j + 1, k),
                                      ID(i + 1, j, k + 2), ID(i + 2, j + 1, k + 2), ID(i + 1, j + 2, k + 2), ID(i, j + 1, k + 2),
                                      ID(i, j, k + 1), ID(i + 2, j, k + 1), ID(i + 2, j + 2, k + 1), ID(i, j + 2, k + 1) };
                memcpy(c + 8, mids, sizeof mids);
            }
            add_elem(M, ty, c, flip ? (q == 2 ? f20 : f8) : NULL);
        }
#undef ID
    }
    for (int ax = 0; ax < 3; ax++) for (int c = 0; c < 4; c++) free(E[ax][c]);
    free(id);
    for (int k = 0; k < nue; k++) cv_free_vec(ue[k].poly);
    return ok;
}

typedef CV_VEC(char) strbuf;

static void sb_printf(strbuf* b, bool* oom, const char* fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    char tmp[256];
    int n = stbsp_vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n >= 0 && cv_reserve(*b, b->n + (size_t)n + 1)) {
        if ((size_t)n < sizeof tmp) memcpy(b->a + b->n, tmp, (size_t)n + 1);
        else stbsp_vsnprintf(b->a + b->n, (size_t)n + 1, fmt, ap2);     /* longer than tmp: format again in place */
        b->n += (size_t)n;
    } else if (n >= 0) *oom = true;
    va_end(ap2);
}

/* ids, 16 to a line as Abaqus wants them */
static void sb_ids(strbuf* b, bool* oom, const uint32_t* v, size_t n) {
    for (size_t i = 0; i < n; i++) sb_printf(b, oom, "%u%s", v[i], i + 1 == n ? "\n" : (i % 16 == 15) ? ",\n" : ", ");
}

static int cmp_u32(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? -1 : x > y;
}

/* bodies: B / bk; sets: T with its items; elty: the ELTY records in file order */
static void native_mesh(cv_fbd* g, const mctx* m, const rsurf* S, size_t ns, const kv* sk,
                        const rsurf* Bd, size_t nb, const kv* bk,
                        const rseta* T, size_t nt, const nm* items, const relty* Y, size_t ny) {
    if (!ny) return;
    /* the element type of every line, surface and body: the last ELTY that names it */
    int* lty = malloc(CV_MAX(m->nl, 1) * sizeof(int));
    int* sty = malloc(CV_MAX(ns, 1) * sizeof(int));
    int* bty = malloc(CV_MAX(nb, 1) * sizeof(int));
    uint32_t *lr = calloc(CV_MAX(m->nl, 1) + 1, sizeof(uint32_t)), *sr = calloc(CV_MAX(ns, 1) + 1, sizeof(uint32_t)),
             *br = calloc(CV_MAX(nb, 1) + 1, sizeof(uint32_t));     /* first element of each (then one past) */
    mesh M; memset(&M, 0, sizeof M);
    strbuf out = {0};
    bool oom = !lty || !sty || !bty || !lr || !sr || !br;
    if (oom) goto done;
    for (size_t i = 0; i < m->nl; i++) lty[i] = -1;
    for (size_t i = 0; i < ns; i++) sty[i] = -1;
    for (size_t i = 0; i < nb; i++) bty[i] = -1;
    size_t unknown = 0;
    for (size_t y = 0; y < ny; y++) {
        int ty = mesh_type(Y[y].ty);
        if (ty < 0) { unknown++; continue; }
        char kind = kMeshType[ty / 2].kind;
        bool named = false;
        for (size_t t = 0; t < nt; t++) {
            if (strcmp(T[t].set.s, Y[y].set.s) || T[t].type != kind) { named |= !strcmp(T[t].set.s, Y[y].set.s); continue; }
            named = true;
            for (size_t j = 0; j < T[t].n; j++) {
                const char* x = items[T[t].first + j].s;
                uint32_t k = kind == 'l' ? kv_find(m->lk, m->nl, x) : kind == 's' ? kv_find(sk, ns, x) : kv_find(bk, nb, x);
                if (k == UINT32_MAX) continue;
                (kind == 'l' ? lty : kind == 's' ? sty : bty)[k] = ty;
            }
        }
        if (!named && !strcmp(Y[y].set.s, "all")) {                 /* cgx's set of everything */
            size_t n = kind == 'l' ? m->nl : kind == 's' ? ns : nb;
            for (size_t k = 0; k < n; k++) (kind == 'l' ? lty : kind == 's' ? sty : bty)[k] = ty;
        }
    }

    float lo[3] = { INFINITY, INFINITY, INFINITY }, hi[3] = { -INFINITY, -INFINITY, -INFINITY };
    for (size_t i = 0; i < m->np; i++) {
        const float p[3] = { m->P[i].x, m->P[i].y, m->P[i].z };
        for (int k = 0; k < 3; k++) { lo[k] = fminf(lo[k], p[k]); hi[k] = fmaxf(hi[k], p[k]); }
    }
    float size = 0;
    for (int k = 0; k < 3 && m->np; k++) size = fmaxf(size, fmaxf(hi[k] - lo[k], fmaxf(fabsf(lo[k]), fabsf(hi[k]))));
    M.nodes.tol = 1e-5f * (size > 0 ? size : 1.f);
    M.nodes.h = 4 * M.nodes.tol;
    if (!mn_rehash(&M.nodes, 1u << 12) || !cv_push(M.eoff, 0)) { oom = true; goto done; }

    size_t skipped = 0;
    for (size_t i = 0; i < nb && !M.oom; i++) {
        br[i] = (uint32_t)M.ety.n;
        if (bty[i] >= 0 && !mesh_body(&M, m, S, sk, ns, &Bd[i], bty[i])) skipped++;
    }
    br[nb] = (uint32_t)M.ety.n;
    for (size_t i = 0; i < ns && !M.oom; i++) {
        sr[i] = (uint32_t)M.ety.n;
        if (sty[i] >= 0) {
            size_t e0 = M.ety.n;
            mesh_surface(&M, m, &S[i], S[i].sign, sty[i]);     /* GSUR's sign orients the normal */
            if (M.ety.n == e0) skipped++;
        }
    }
    sr[ns] = (uint32_t)M.ety.n;
    for (size_t i = 0; i < m->nl && !M.oom; i++) {
        lr[i] = (uint32_t)M.ety.n;
        if (lty[i] >= 0) mesh_line(&M, m, (uint32_t)i, lty[i]);
    }
    lr[m->nl] = (uint32_t)M.ety.n;
    if (M.oom) { oom = true; goto done; }
    char msg[160];
    if (unknown) { snprintf(msg, sizeof msg, "%zu element types (ELTY) the built-in mesher does not make (it makes BE2/3, TR3/6, QU4/8, HE8/20)", unknown); cv_msg_add(&g->msgs, 0, false, msg); }
    if (skipped) { snprintf(msg, sizeof msg, "%zu surfaces or bodies not meshed: the built-in mesher needs 4-sided surfaces and 6-sided bodies (cgx meshes the rest)", skipped); cv_msg_add(&g->msgs, 0, false, msg); }
    if (!M.ety.n) goto done;

    /* ---- Abaqus text ---- */
    size_t nn = M.nodes.next.n, ne = M.ety.n;
    sb_printf(&out, &oom, "** meshed by ccxview from the cgx geometry\n*NODE, NSET=Nall\n");
    for (size_t i = 0; i < nn; i++) {
        const float* p = M.nodes.xyz.a + 3 * i;
        sb_printf(&out, &oom, "%zu, %.9g, %.9g, %.9g\n", i + 1, p[0], p[1], p[2]);
    }
    for (int t = 0; t < 2 * (int)CV_COUNT(kMeshType); t++) {
        bool any = false;
        for (size_t e = 0; e < ne && !any; e++) any = M.ety.a[e] == t;
        if (!any) continue;
        sb_printf(&out, &oom, "*ELEMENT, TYPE=%s%s, ELSET=Eall\n", kMeshType[t / 2].abq, t & 1 ? "R" : "");
        for (size_t e = 0; e < ne; e++) {
            if (M.ety.a[e] != t) continue;
            uint32_t v[21];
            int k = 0;
            v[k++] = (uint32_t)e + 1;
            for (uint32_t c = M.eoff.a[e]; c < M.eoff.a[e + 1]; c++) v[k++] = M.conn.a[c] + 1;
            sb_ids(&out, &oom, v, (size_t)k);
        }
    }
    /* the named sets, as elements and nodes of what they hold */
    for (size_t t = 0; t < nt; t++) {
        bool first = true;
        for (size_t u = 0; u < t && first; u++) first = strcmp(T[u].set.s, T[t].set.s) != 0;
        if (!first) continue;
        CV_VEC(uint32_t) es = {0}, nsv = {0};
        for (size_t u = t; u < nt; u++) {
            if (strcmp(T[u].set.s, T[t].set.s)) continue;
            char kind = T[u].type;
            if (kind != 'l' && kind != 's' && kind != 'b') continue;
            for (size_t j = 0; j < T[u].n; j++) {
                const char* x = items[T[u].first + j].s;
                uint32_t k = kind == 'l' ? kv_find(m->lk, m->nl, x) : kind == 's' ? kv_find(sk, ns, x) : kv_find(bk, nb, x);
                if (k == UINT32_MAX) continue;
                const uint32_t* r = kind == 'l' ? lr : kind == 's' ? sr : br;
                for (uint32_t e = r[k]; e < r[k + 1]; e++) {
                    oom |= !cv_push(es, e + 1);
                    for (uint32_t c = M.eoff.a[e]; c < M.eoff.a[e + 1]; c++) oom |= !cv_push(nsv, M.conn.a[c] + 1);
                }
            }
        }
        if (es.n) {
            qsort(nsv.a, nsv.n, sizeof *nsv.a, cmp_u32);
            size_t w = 0;
            for (size_t i = 0; i < nsv.n; i++) if (!w || nsv.a[w - 1] != nsv.a[i]) nsv.a[w++] = nsv.a[i];
            sb_printf(&out, &oom, "*ELSET, ELSET=%s\n", T[t].set.s);
            sb_ids(&out, &oom, es.a, es.n);
            sb_printf(&out, &oom, "*NSET, NSET=%s\n", T[t].set.s);
            sb_ids(&out, &oom, nsv.a, w);
        }
        cv_free_vec(es); cv_free_vec(nsv);
    }
    if (!oom) {
        g->msh = out.a; g->msh_n = out.n; out.a = NULL;
        g->mesh_nodes = (uint32_t)nn; g->mesh_elems = (uint32_t)ne;
        snprintf(msg, sizeof msg, "meshed without cgx: %zu nodes, %zu elements", nn, ne);
        cv_msg_add(&g->msgs, 0, false, msg);
    }
done:
    if (oom) cv_msg_add(&g->msgs, 0, false, "out of memory meshing the geometry");
    free(lty); free(sty); free(bty); free(lr); free(sr); free(br);
    cv_free_vec(M.nodes.xyz); cv_free_vec(M.nodes.next); free(M.nodes.head);
    cv_free_vec(M.conn); cv_free_vec(M.eoff); cv_free_vec(M.ety);
    cv_free_vec(out);
}

/* ---- parse ------------------------------------------------------------------------ */

bool cv_fbd_parse(cv_fbd* g, const char* data, size_t size) {
    memset(g, 0, sizeof *g);
    CV_VEC(rpnt) P = {0};
    CV_VEC(rseq) Q = {0};
    CV_VEC(rline) L = {0};
    CV_VEC(rlcmb) C = {0};
    CV_VEC(rsurf) S = {0};
    CV_VEC(rseta) T = {0};
    CV_VEC(rsurf) B = {0};
    CV_VEC(relty) Y = {0};
    CV_VEC(nm) items = {0};
    CV_VEC(snm) sitems = {0};
    kv *pk = NULL, *qk = NULL, *lk = NULL, *ck = NULL, *sk = NULL, *bk = NULL;
    f3vec poly = {0}, loop = {0};
    CV_VEC(float) cx = {0}, tx = {0};
    CV_VEC(uint32_t) coff = {0}, soff = {0};
    bool oom = false;
    char line[4096];
    char* tok[512];
    size_t bad = 0;

    const char* c = data;
    const char* end = data + size;
    uint64_t ln = 0;
    while (c < end && !oom) {
        const char* nl = memchr(c, '\n', (size_t)(end - c));
        size_t n = (size_t)((nl ? nl : end) - c);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, c, n);
        line[n] = 0;
        c = nl ? nl + 1 : end;
        ln++;
        int nt = tokenize(line, tok, 512);
        if (nt == 0 || tok[0][0] == '#') continue;
        const char* k = tok[0];
        if (kw_is(k, "PNT")) {
            rpnt r;
            if (nt < 5 || !strcmp(tok[1], "!") || !num(tok[2], &r.x) || !num(tok[3], &r.y) || !num(tok[4], &r.z)) goto script;
            nmset(&r.name, tok[1]);
            oom |= !cv_push(P, r);
        } else if (kw_is(k, "SEQA")) {
            if (nt < 4) { bad++; continue; }
            rseq r; nmset(&r.name, tok[1]); r.first = items.n; r.n = 0;
            for (int i = 3; i < nt; i++) { nm x; nmset(&x, tok[i]); oom |= !cv_push(items, x); r.n++; }
            oom |= !cv_push(Q, r);
        } else if (kw_is(k, "LINE")) {
            if (nt < 4) goto script;
            rline r; memset(&r, 0, sizeof r);
            nmset(&r.name, tok[1]); nmset(&r.a, tok[2]); nmset(&r.b, tok[3]);
            if (nt >= 6) { r.has_c = true; nmset(&r.c, tok[4]); r.div = atoi(tok[5]); }
            else if (nt == 5) r.div = atoi(tok[4]);
            oom |= !cv_push(L, r);
        } else if (kw_is(k, "LCMB")) {
            if (nt < 2) { bad++; continue; }
            rlcmb r; nmset(&r.name, tok[1]); r.first = sitems.n; r.n = 0;
            for (int i = 2; i + 1 < nt; i += 2) {
                snm x; x.sign = tok[i][0] == '-' ? -1 : 1; nmset(&x.n, tok[i + 1]);
                oom |= !cv_push(sitems, x); r.n++;
            }
            oom |= !cv_push(C, r);
        } else if (kw_is(k, "GSUR")) {
            /* GSUR name sign BLEND|NURS sign edge sign edge ... */
            if (nt < 6) { bad++; continue; }
            rsurf r; nmset(&r.name, tok[1]); r.first = sitems.n; r.n = 0; r.sign = tok[2][0] == '-' ? -1 : 1;
            for (int i = 4; i + 1 < nt; i += 2) {
                snm x; x.sign = tok[i][0] == '-' ? -1 : 1; nmset(&x.n, tok[i + 1]);
                oom |= !cv_push(sitems, x); r.n++;
            }
            oom |= !cv_push(S, r);
        } else if (kw_is(k, "SETA")) {
            if (nt < 4) { bad++; continue; }
            rseta r; nmset(&r.set, tok[1]); r.type = (char)tolower((unsigned char)tok[2][0]);
            r.first = items.n; r.n = 0;
            for (int i = 3; i < nt; i++) { nm x; nmset(&x, tok[i]); oom |= !cv_push(items, x); r.n++; }
            oom |= !cv_push(T, r);
        } else if (kw_is(k, "GBOD")) {
            /* GBOD name NORM sign surf sign surf ... */
            if (nt < 5) { bad++; continue; }
            rsurf r; nmset(&r.name, tok[1]); r.first = sitems.n; r.n = 0; r.sign = 1;
            for (int i = 3; i + 1 < nt; i += 2) {
                snm x; x.sign = tok[i][0] == '-' ? -1 : 1; nmset(&x.n, tok[i + 1]);
                oom |= !cv_push(sitems, x); r.n++;
            }
            oom |= !cv_push(B, r);
        } else if (kw_is(k, "ELTY")) {
            if (nt < 3) continue;                 /* ELTY set: the type cleared */
            relty r; nmset(&r.set, tok[1]); snprintf(r.ty, sizeof r.ty, "%s", tok[2]);
            oom |= !cv_push(Y, r);
        } else if (kw_is(k, "VALU") || kw_is(k, "MSHP") ||
                   kw_is(k, "ASGN") || kw_is(k, "NURS") || kw_is(k, "NURL")) {
            continue;                             /* no geometry of their own to draw */
        } else {
            goto script;
        }
        continue;
    script:
        if (!g->needs_cgx) {
            g->needs_cgx = true;
            snprintf(g->needs_why, sizeof g->needs_why, "line %llu: %.60s", (unsigned long long)ln, k);
        }
    }
    if (oom) goto fail;

    /* ---- name maps ---- */
#define MAP(vec, keys) do { keys = malloc(CV_MAX(vec.n, 1) * sizeof *keys); if (!keys) goto fail; \
        for (size_t i = 0; i < vec.n; i++) { keys[i].key = vec.a[i].name.s; keys[i].idx = (uint32_t)i; } \
        qsort(keys, vec.n, sizeof *keys, kv_cmp); } while (0)
    MAP(P, pk); MAP(Q, qk); MAP(L, lk); MAP(C, ck); MAP(S, sk); MAP(B, bk);

    /* ---- points ---- */
    g->npts = (uint32_t)P.n;
    g->pxyz = malloc(CV_MAX(P.n, 1) * 3 * sizeof(float));
    g->pname = malloc(CV_MAX(P.n, 1) * 32);
    if (!g->pxyz || !g->pname) goto fail;
    for (size_t i = 0; i < P.n; i++) {
        g->pxyz[3 * i] = P.a[i].x; g->pxyz[3 * i + 1] = P.a[i].y; g->pxyz[3 * i + 2] = P.a[i].z;
        memcpy(g->pname[i], P.a[i].name.s, 32);
    }

    /* ---- curves ---- */
    g->ncrv = (uint32_t)L.n;
    g->cname = malloc(CV_MAX(L.n, 1) * 32);
    if (!g->cname) goto fail;
    size_t missing = 0;
    for (size_t i = 0; i < L.n; i++) {
        const rline* r = &L.a[i];
        memcpy(g->cname[i], r->name.s, 32);
        if (!cv_push(coff, (uint32_t)(cx.n / 3))) goto fail;
        uint32_t a = kv_find(pk, P.n, r->a.s), b = kv_find(pk, P.n, r->b.s);
        if (a == UINT32_MAX || b == UINT32_MAX) { missing++; continue; }
        f3 pa = f3_of(g->pxyz + 3 * a), pb = f3_of(g->pxyz + 3 * b);
        poly.n = 0;
        uint32_t pc = r->has_c ? kv_find(pk, P.n, r->c.s) : UINT32_MAX;
        uint32_t sq = r->has_c && pc == UINT32_MAX ? kv_find(qk, Q.n, r->c.s) : UINT32_MAX;
        if (pc != UINT32_MAX) {
            tess_arc(&poly, pa, pb, f3_of(g->pxyz + 3 * pc));
        } else if (sq != UINT32_MAX) {
            const rseq* q = &Q.a[sq];
            f3vec pts = {0};
            cv_push(pts, pa);
            for (size_t j = 0; j < q->n; j++) {
                uint32_t pi = kv_find(pk, P.n, items.a[q->first + j].s);
                if (pi == UINT32_MAX || pi == a || pi == b) continue;
                cv_push(pts, f3_of(g->pxyz + 3 * pi));
            }
            cv_push(pts, pb);
            tess_spline(&poly, pts.a, pts.n);
            cv_free_vec(pts);
        } else {
            if (r->has_c) missing++;
            cv_push(poly, pa); cv_push(poly, pb);
        }
        for (size_t j = 0; j < poly.n; j++) {
            if (!cv_push(cx, poly.a[j].x) || !cv_push(cx, poly.a[j].y) || !cv_push(cx, poly.a[j].z)) goto fail;
        }
    }
    if (!cv_push(coff, (uint32_t)(cx.n / 3))) goto fail;

    /* ---- surfaces: close the edge loop, then a Coons patch (4 edges) or a fan ---- */
    g->nsrf = (uint32_t)S.n;
    g->sname = malloc(CV_MAX(S.n, 1) * 32);
    if (!g->sname) goto fail;
    size_t unfilled = 0;
    for (size_t si = 0; si < S.n; si++) {
        const rsurf* r = &S.a[si];
        memcpy(g->sname[si], r->name.s, 32);
        if (!cv_push(soff, (uint32_t)(tx.n / 9))) goto fail;
        /* each edge as its own oriented polyline */
        f3vec edge[8] = {{0}};
        int ne = 0;
        bool ok = true;
        for (size_t j = 0; j < r->n && ne < 8; j++) {
            const snm* e = &sitems.a[r->first + j];
            uint32_t li = kv_find(lk, L.n, e->n.s);
            f3vec* ev = &edge[ne++];
            if (li != UINT32_MAX) {
                for (uint32_t v = coff.a[li]; v < coff.a[li + 1]; v++) cv_push(*ev, f3_of(cx.a + 3 * v));
            } else {
                uint32_t ci = kv_find(ck, C.n, e->n.s);
                if (ci == UINT32_MAX) { ok = false; break; }
                const rlcmb* cb = &C.a[ci];
                for (size_t m = 0; m < cb->n; m++) {
                    const snm* ce = &sitems.a[cb->first + m];
                    uint32_t l2 = kv_find(lk, L.n, ce->n.s);
                    if (l2 == UINT32_MAX) { ok = false; break; }
                    f3vec part = {0};
                    for (uint32_t v = coff.a[l2]; v < coff.a[l2 + 1]; v++) cv_push(part, f3_of(cx.a + 3 * v));
                    if (ce->sign < 0) for (size_t u = 0; u < part.n / 2; u++) { f3 t = part.a[u]; part.a[u] = part.a[part.n - 1 - u]; part.a[part.n - 1 - u] = t; }
                    for (size_t u = ev->n ? 1 : 0; u < part.n; u++) cv_push(*ev, part.a[u]);
                    cv_free_vec(part);
                }
            }
            if (ev->n < 2) { ok = false; break; }
            if (e->sign < 0) for (size_t u = 0; u < ev->n / 2; u++) { f3 t = ev->a[u]; ev->a[u] = ev->a[ev->n - 1 - u]; ev->a[ev->n - 1 - u] = t; }
        }
        /* cgx's signs make a loop; fix any edge still pointing the wrong way */
        for (int j = 1; ok && j < ne; j++) {
            f3 end0 = edge[j - 1].a[edge[j - 1].n - 1];
            if (len(sub(edge[j].a[0], end0)) > len(sub(edge[j].a[edge[j].n - 1], end0))) {
                f3vec* ev = &edge[j];
                for (size_t u = 0; u < ev->n / 2; u++) { f3 t = ev->a[u]; ev->a[u] = ev->a[ev->n - 1 - u]; ev->a[ev->n - 1 - u] = t; }
            }
        }
        if (ok && ne == 4) {
            enum { M = 13 };
            f3 e0[M], e1[M], e2[M], e3[M], grid[M][M];
            resample(edge[0].a, edge[0].n, e0, M);      /* bottom  P00 -> P10 */
            resample(edge[1].a, edge[1].n, e1, M);      /* right   P10 -> P11 */
            resample(edge[2].a, edge[2].n, e2, M);      /* top     P11 -> P01 */
            resample(edge[3].a, edge[3].n, e3, M);      /* left    P01 -> P00 */
            f3 p00 = e0[0], p10 = e0[M - 1], p11 = e2[0], p01 = e2[M - 1];
            for (int i = 0; i < M; i++) for (int j = 0; j < M; j++) {
                float u = (float)i / (M - 1), v = (float)j / (M - 1);
                f3 bot = e0[i], top = e2[M - 1 - i], lft = e3[M - 1 - j], rgt = e1[j];
                f3 ruled = add(add(mul(bot, 1 - v), mul(top, v)), add(mul(lft, 1 - u), mul(rgt, u)));
                f3 bil = add(add(mul(p00, (1 - u) * (1 - v)), mul(p10, u * (1 - v))), add(mul(p11, u * v), mul(p01, (1 - u) * v)));
                grid[i][j] = sub(ruled, bil);
            }
            for (int i = 0; i + 1 < M; i++) for (int j = 0; j + 1 < M; j++) {
                const f3* q[6] = { &grid[i][j], &grid[i + 1][j], &grid[i + 1][j + 1], &grid[i][j], &grid[i + 1][j + 1], &grid[i][j + 1] };
                for (int k = 0; k < 6; k++) { cv_push(tx, q[k]->x); cv_push(tx, q[k]->y); cv_push(tx, q[k]->z); }
            }
        } else if (ok && ne >= 3) {
            loop.n = 0;
            for (int j = 0; j < ne; j++) for (size_t u = 0; u + 1 < edge[j].n; u++) cv_push(loop, edge[j].a[u]);
            f3 cen = { 0, 0, 0 };
            for (size_t u = 0; u < loop.n; u++) cen = add(cen, loop.a[u]);
            cen = mul(cen, 1.f / (float)CV_MAX(loop.n, 1));
            for (size_t u = 0; u < loop.n; u++) {
                const f3* q[3] = { &cen, &loop.a[u], &loop.a[(u + 1) % loop.n] };
                for (int k = 0; k < 3; k++) { cv_push(tx, q[k]->x); cv_push(tx, q[k]->y); cv_push(tx, q[k]->z); }
            }
        } else {
            unfilled++;
        }
        for (int j = 0; j < ne; j++) cv_free_vec(edge[j]);
    }
    if (!cv_push(soff, (uint32_t)(tx.n / 9))) goto fail;

    /* ---- sets ---- */
    {
        CV_VEC(cv_gset) sets = {0};
        for (size_t i = 0; i < T.n; i++) {
            const rseta* r = &T.a[i];
            int si = -1;
            for (size_t k = 0; k < sets.n; k++) if (!strcmp(sets.a[k].name, r->set.s)) { si = (int)k; break; }
            if (si < 0) {
                cv_gset z; memset(&z, 0, sizeof z);
                snprintf(z.name, sizeof z.name, "%s", r->set.s);
                if (!cv_push(sets, z)) goto fail;
                si = (int)sets.n - 1;
            }
            cv_gset* gs = &sets.a[si];
            for (size_t j = 0; j < r->n; j++) {
                const char* nmx = items.a[r->first + j].s;
                uint32_t idx;
                bool ok = true;
                if (r->type == 'p' && (idx = kv_find(pk, P.n, nmx)) != UINT32_MAX) ok = push_u32(&gs->pts, &gs->npts, idx);
                else if (r->type == 'l' && (idx = kv_find(lk, L.n, nmx)) != UINT32_MAX) ok = push_u32(&gs->crv, &gs->ncrv, idx);
                else if (r->type == 's' && (idx = kv_find(sk, S.n, nmx)) != UINT32_MAX) ok = push_u32(&gs->srf, &gs->nsrf, idx);
                else if (r->type == 'n' || r->type == 'e') {           /* mesh ids */
                    char* e;
                    unsigned long v = strtoul(nmx, &e, 10);
                    if (e != nmx && *e == 0 && v > 0 && v < UINT32_MAX)
                        ok = r->type == 'n' ? push_u32(&gs->nodes, &gs->nnod, (uint32_t)v)
                                            : push_u32(&gs->elems, &gs->nel, (uint32_t)v);
                }
                if (!ok) goto fail;
            }
        }
        if (!g->needs_cgx) {
            mctx mc = { P.a, P.n, pk, L.a, L.n, lk, C.a, C.n, ck, sitems.a, cx.a, coff.a };
            native_mesh(g, &mc, S.a, S.n, sk, B.a, B.n, bk, T.a, T.n, items.a, Y.a, Y.n);
        }
        /* keep only sets that hold geometry */
        size_t w = 0;
        for (size_t k = 0; k < sets.n; k++) {
            cv_gset* s = &sets.a[k];
            if (s->npts || s->ncrv || s->nsrf || s->nnod || s->nel) sets.a[w++] = *s;
            else { free(s->pts); free(s->crv); free(s->srf); free(s->nodes); free(s->elems); }
        }
        g->sets = sets.a; g->nsets = (int)w;
    }

    char msg[120];
    if (bad) { snprintf(msg, sizeof msg, "%zu unreadable geometry records skipped", bad); cv_msg_add(&g->msgs, 0, false, msg); }
    if (missing) { snprintf(msg, sizeof msg, "%zu lines refer to points or curves that are not defined", missing); cv_msg_add(&g->msgs, 0, false, msg); }
    if (unfilled) { snprintf(msg, sizeof msg, "%zu surfaces could not be filled (edges missing)", unfilled); cv_msg_add(&g->msgs, 0, false, msg); }

    g->coff = coff.a; coff.a = NULL;
    g->cxyz = cx.a; cx.a = NULL;
    g->soff = soff.a; soff.a = NULL;
    g->txyz = tx.a; tx.a = NULL;
    g->ntri = g->soff ? g->soff[g->nsrf] : 0;
    cv_free_vec(P); cv_free_vec(Q); cv_free_vec(L); cv_free_vec(C); cv_free_vec(S); cv_free_vec(T); cv_free_vec(B); cv_free_vec(Y);
    cv_free_vec(items); cv_free_vec(sitems); cv_free_vec(poly); cv_free_vec(loop);
    free(pk); free(qk); free(lk); free(ck); free(sk); free(bk);
    return true;

fail:
    cv_free_vec(P); cv_free_vec(Q); cv_free_vec(L); cv_free_vec(C); cv_free_vec(S); cv_free_vec(T); cv_free_vec(B); cv_free_vec(Y);
    cv_free_vec(items); cv_free_vec(sitems); cv_free_vec(poly); cv_free_vec(loop);
    cv_free_vec(cx); cv_free_vec(tx); cv_free_vec(coff); cv_free_vec(soff);
    free(pk); free(qk); free(lk); free(ck); free(sk); free(bk);
    cv_fbd_free(g);
    cv_msg_add(&g->msgs, 0, false, "out of memory reading the geometry");
    return false;
}

void cv_fbd_free(cv_fbd* g) {
    free(g->pxyz); free(g->pname); free(g->cname); free(g->coff); free(g->cxyz);
    free(g->sname); free(g->soff); free(g->txyz); free(g->msh);
    for (int i = 0; i < g->nsets; i++) {
        free(g->sets[i].pts); free(g->sets[i].crv); free(g->sets[i].srf);
        free(g->sets[i].nodes); free(g->sets[i].elems);
    }
    free(g->sets);
    cv_msgs keep = g->msgs;
    memset(g, 0, sizeof *g);
    g->msgs = keep;
}
