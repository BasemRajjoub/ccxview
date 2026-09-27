/* fbd.c -- cgx geometry records -> points, tessellated curves, surface patches. */
#include "fbd.h"
#include "frd.h"      /* cv_parse_num */
#include <ctype.h>
#include <math.h>

typedef struct { char s[32]; } nm;
typedef struct { nm n; int8_t sign; } snm;

typedef struct { nm name; float x, y, z; } rpnt;
typedef struct { nm name; size_t first, n; } rseq;                 /* items: nms */
typedef struct { nm name, a, b, c; bool has_c; int div; } rline;
typedef struct { nm name; size_t first, n; } rlcmb;               /* items: snms */
typedef struct { nm name; size_t first, n; } rsurf;               /* items: snms */
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

/* ---- parse ------------------------------------------------------------------------ */

bool cv_fbd_parse(cv_fbd* g, const char* data, size_t size) {
    memset(g, 0, sizeof *g);
    CV_VEC(rpnt) P = {0};
    CV_VEC(rseq) Q = {0};
    CV_VEC(rline) L = {0};
    CV_VEC(rlcmb) C = {0};
    CV_VEC(rsurf) S = {0};
    CV_VEC(rseta) T = {0};
    CV_VEC(nm) items = {0};
    CV_VEC(snm) sitems = {0};
    kv *pk = NULL, *qk = NULL, *lk = NULL, *ck = NULL, *sk = NULL;
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
            rsurf r; nmset(&r.name, tok[1]); r.first = sitems.n; r.n = 0;
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
        } else if (kw_is(k, "GBOD") || kw_is(k, "VALU") || kw_is(k, "MSHP") || kw_is(k, "ELTY") ||
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
    MAP(P, pk); MAP(Q, qk); MAP(L, lk); MAP(C, ck); MAP(S, sk);

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
    cv_free_vec(P); cv_free_vec(Q); cv_free_vec(L); cv_free_vec(C); cv_free_vec(S); cv_free_vec(T);
    cv_free_vec(items); cv_free_vec(sitems); cv_free_vec(poly); cv_free_vec(loop);
    free(pk); free(qk); free(lk); free(ck); free(sk);
    return true;

fail:
    cv_free_vec(P); cv_free_vec(Q); cv_free_vec(L); cv_free_vec(C); cv_free_vec(S); cv_free_vec(T);
    cv_free_vec(items); cv_free_vec(sitems); cv_free_vec(poly); cv_free_vec(loop);
    cv_free_vec(cx); cv_free_vec(tx); cv_free_vec(coff); cv_free_vec(soff);
    free(pk); free(qk); free(lk); free(ck); free(sk);
    cv_fbd_free(g);
    cv_msg_add(&g->msgs, 0, false, "out of memory reading the geometry");
    return false;
}

void cv_fbd_free(cv_fbd* g) {
    free(g->pxyz); free(g->pname); free(g->cname); free(g->coff); free(g->cxyz);
    free(g->sname); free(g->soff); free(g->txyz);
    for (int i = 0; i < g->nsets; i++) {
        free(g->sets[i].pts); free(g->sets[i].crv); free(g->sets[i].srf);
        free(g->sets[i].nodes); free(g->sets[i].elems);
    }
    free(g->sets);
    cv_msgs keep = g->msgs;
    memset(g, 0, sizeof *g);
    g->msgs = keep;
}
