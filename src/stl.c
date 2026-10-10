/* stl.c -- binary and ASCII STL into a triangle list (stl.h). */
#include "stl.h"
#include "frd.h"     /* cv_parse_num */
#include "os.h"
#include <math.h>
#include <stdarg.h>

static bool fail(char* err, size_t n, const char* fmt, ...) {
    if (err && n) { va_list ap; va_start(ap, fmt); vsnprintf(err, n, fmt, ap); va_end(ap); }
    return false;
}

static uint32_t rd_u32(const unsigned char* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static float rd_f32(const unsigned char* p) { uint32_t u = rd_u32(p); float f; memcpy(&f, &u, 4); return f; }

/* the triangles as read: the file's normal, then the three vertices (12 floats each) */
typedef CV_VEC(float) soup;

static bool push12(soup* s, const float* f) {
    if (!cv_reserve(*s, s->n + 12)) return false;
    memcpy(s->a + s->n, f, 12 * sizeof(float));
    s->n += 12;
    return true;
}

/* ---- binary: 80 bytes of header, the count, 50 bytes per triangle -------------- */
static bool read_binary(soup* out, const char* data, size_t size, char* err, size_t errn) {
    if (size < 84) return fail(err, errn, "too short for an STL file (%zu bytes)", size);
    const unsigned char* p = (const unsigned char*)data;
    uint64_t n = rd_u32(p + 80);
    if (84 + 50 * n > size)
        return fail(err, errn, "truncated: the header counts %llu triangles, the file holds %llu", (unsigned long long)n, (unsigned long long)((size - 84) / 50));
    if (n == 0) return fail(err, errn, "no triangles");
    if (!cv_reserve(*out, (size_t)n * 12)) return fail(err, errn, "out of memory for %llu triangles", (unsigned long long)n);
    for (uint64_t i = 0; i < n; i++) {
        const unsigned char* t = p + 84 + 50 * i;
        for (int k = 0; k < 12; k++) out->a[out->n++] = rd_f32(t + 4 * k);
    }
    return true;
}

/* ---- ASCII: solid / facet normal / outer loop / vertex x3 / endloop / endfacet -- */
typedef struct { const char* p; const char* e; unsigned line; } lex;

/* the next word: [*s, *e); false at the end */
static bool word(lex* L, const char** s, const char** e) {
    while (L->p < L->e && (unsigned char)*L->p <= ' ') { if (*L->p == '\n') L->line++; L->p++; }
    if (L->p >= L->e) return false;
    *s = L->p;
    while (L->p < L->e && (unsigned char)*L->p > ' ') L->p++;
    *e = L->p;
    return true;
}
static bool is(const char* s, const char* e, const char* w) { size_t n = strlen(w); return (size_t)(e - s) == n && !memcmp(s, w, n); }
static bool num3(lex* L, float* v) {
    for (int k = 0; k < 3; k++) {
        const char *s, *e;
        double d;
        if (!word(L, &s, &e) || !cv_parse_num(s, e, &d)) return false;
        v[k] = (float)d;
    }
    return true;
}

/* facets: set when the file has any facet at all (else it may be a binary one with "solid" in its header) */
static bool read_ascii(soup* out, const char* data, size_t size, bool* facets, char* err, size_t errn) {
    lex L = { data, data + size, 1 };
    const char *s, *e;
    float f[12] = { 0 }, first[3] = { 0 }, prev[3] = { 0 };
    int nv = -1;                                    /* vertices of the open facet, -1 none open */
    while (word(&L, &s, &e)) {
        if (is(s, e, "facet")) {
            if (nv >= 0) return fail(err, errn, "line %u: a facet inside a facet", L.line);
            nv = 0;
            *facets = true;
            memset(f, 0, sizeof f);
            const char* keep = L.p; unsigned kl = L.line;
            if (word(&L, &s, &e) && is(s, e, "normal")) {
                if (!num3(&L, f)) return fail(err, errn, "line %u: a normal without three numbers", L.line);
            } else { L.p = keep; L.line = kl; }
        } else if (is(s, e, "vertex")) {
            float v[3];
            if (nv < 0) return fail(err, errn, "line %u: a vertex outside a facet", L.line);
            if (!num3(&L, v)) return fail(err, errn, "line %u: a vertex without three numbers", L.line);
            if (nv == 0) memcpy(first, v, sizeof v);
            else if (nv >= 2) {                     /* a polygon: a fan from its first vertex */
                memcpy(f + 3, first, sizeof first); memcpy(f + 6, prev, sizeof prev); memcpy(f + 9, v, sizeof v);
                if (!push12(out, f)) return fail(err, errn, "out of memory at line %u", L.line);
            }
            memcpy(prev, v, sizeof v);
            nv++;
        } else if (is(s, e, "endfacet")) {
            if (nv >= 0 && nv < 3) return fail(err, errn, "line %u: a facet with %d vertices", L.line, nv);
            nv = -1;
        }                                           /* solid, outer loop, endloop, endsolid, names: nothing to keep */
    }
    if (nv >= 0) return fail(err, errn, "truncated: the file ends inside a facet (line %u)", L.line);
    if (!out->n) return fail(err, errn, "no triangles");
    return true;
}

static bool starts_solid(const char* d, size_t n) {
    size_t i = 0;
    while (i < n && (d[i] == ' ' || d[i] == '\t' || d[i] == '\r' || d[i] == '\n')) i++;
    return n - i >= 5 && !memcmp(d + i, "solid", 5);
}

/* ---- from the soup to the mesh ------------------------------------------------ */
static uint32_t hash3(const uint32_t* b) {
    uint32_t h = b[0] * 0x9E3779B1u;
    h ^= b[1] * 0x85EBCA77u; h = (h << 13 | h >> 19) * 5u + 0xE6546B64u;
    h ^= b[2] * 0xC2B2AE3Du; h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15;
    return h;
}

static bool finish(cv_stl* s, const soup* q, bool weld, char* err, size_t errn) {
    size_t n_in = q->n / 12, nt = 0;
    for (size_t t = 0; t < n_in; t++) {
        bool ok = true;
        for (int k = 3; k < 12; k++) ok = ok && isfinite(q->a[12 * t + k]);
        nt += ok;
    }
    s->skipped = (uint32_t)(n_in - nt);
    if (!nt) return fail(err, errn, "no triangle with finite coordinates (%zu read)", n_in);
    if ((uint64_t)nt * 3 > UINT32_MAX - 1) return fail(err, errn, "too many triangles (%zu)", nt);
    s->n_tri = (uint32_t)nt;
    s->tri = malloc(nt * 3 * sizeof *s->tri);
    s->nrm = malloc(nt * 3 * sizeof *s->nrm);
    s->xyz = malloc(nt * 9 * sizeof *s->xyz);
    uint32_t* table = NULL;
    size_t cap = 1;
    if (weld) { while (cap < nt * 6) cap <<= 1; table = malloc(cap * sizeof *table); }
    if (!s->tri || !s->nrm || !s->xyz || (weld && !table)) { free(table); return fail(err, errn, "out of memory for %zu triangles", nt); }
    if (weld) memset(table, 0xff, cap * sizeof *table);
    for (int k = 0; k < 3; k++) { s->lo[k] = INFINITY; s->hi[k] = -INFINITY; }
    uint32_t nv = 0, ti = 0;
    for (size_t t = 0; t < n_in; t++) {
        const float* r = q->a + 12 * t;
        bool ok = true;
        for (int k = 3; k < 12; k++) ok = ok && isfinite(r[k]);
        if (!ok) continue;
        for (int c = 0; c < 3; c++) {
            float p[3] = { r[3 + 3 * c] + 0.f, r[4 + 3 * c] + 0.f, r[5 + 3 * c] + 0.f };   /* -0 is 0 */
            uint32_t at = nv;
            if (weld) {
                uint32_t b[3]; memcpy(b, p, sizeof b);
                size_t h = hash3(b) & (cap - 1);
                while (table[h] != UINT32_MAX && memcmp(s->xyz + 3 * (size_t)table[h], p, sizeof p)) h = (h + 1) & (cap - 1);
                if (table[h] != UINT32_MAX) at = table[h];
                else table[h] = nv;
            }
            if (at == nv) {
                memcpy(s->xyz + 3 * (size_t)nv++, p, sizeof p);
                for (int k = 0; k < 3; k++) { s->lo[k] = fminf(s->lo[k], p[k]); s->hi[k] = fmaxf(s->hi[k], p[k]); }
            }
            s->tri[3 * (size_t)ti + c] = at;
        }
        /* the normal from the vertices; the file's when they are in a line */
        double u[3], v[3], c[3];
        for (int k = 0; k < 3; k++) { u[k] = (double)r[6 + k] - r[3 + k]; v[k] = (double)r[9 + k] - r[3 + k]; }
        c[0] = u[1] * v[2] - u[2] * v[1]; c[1] = u[2] * v[0] - u[0] * v[2]; c[2] = u[0] * v[1] - u[1] * v[0];
        double l = sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        if (!(l > 0)) {
            for (int k = 0; k < 3; k++) c[k] = isfinite(r[k]) ? r[k] : 0;
            l = sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        }
        for (int k = 0; k < 3; k++) s->nrm[3 * (size_t)ti + k] = l > 0 ? (float)(c[k] / l) : 0.f;
        ti++;
    }
    free(table);
    s->n_vert = nv;
    if (nv < nt * 3) { float* x = realloc(s->xyz, (size_t)nv * 3 * sizeof *x); if (x) s->xyz = x; }
    return true;
}

bool cv_stl_parse(cv_stl* s, const char* data, size_t size, bool weld, char* err, size_t errn) {
    memset(s, 0, sizeof *s);
    if (err && errn) err[0] = 0;
    if (!data || !size) return fail(err, errn, "empty file");
    soup q = { 0 };
    bool ok;
    uint64_t n = size >= 84 ? rd_u32((const unsigned char*)data + 80) : 0;
    if (size >= 84 && 84 + 50 * n == size) { s->binary = true; ok = read_binary(&q, data, size, err, errn); }
    else if (starts_solid(data, size)) {
        bool facets = false;
        ok = read_ascii(&q, data, size, &facets, err, errn);
        /* "solid" in a binary header: with bytes after the triangles, or truncated */
        if (!ok && size >= 84 && n > 0 && (84 + 50 * n <= size || !facets)) {
            q.n = 0; s->binary = true;
            ok = read_binary(&q, data, size, err, errn);
        }
    } else { s->binary = true; ok = read_binary(&q, data, size, err, errn); }
    if (ok) ok = finish(s, &q, weld, err, errn);
    cv_free_vec(q);
    if (!ok) { bool b = s->binary; cv_stl_free(s); s->binary = b; }
    return ok;
}

bool cv_stl_read(cv_stl* s, const char* path, bool weld, char* err, size_t errn) {
    memset(s, 0, sizeof *s);
    if (cv_file_size(path) == 0) {
        FILE* f = fopen(path, "rb");
        if (f) fclose(f);
        return fail(err, errn, "%s", f ? "empty file" : "cannot open the file");
    }
    cv_map m;
    if (!cv_map_open(&m, path)) return fail(err, errn, "cannot open the file");
    bool ok = cv_stl_parse(s, m.data, m.size, weld, err, errn);
    cv_map_close(&m);
    return ok;
}

void cv_stl_free(cv_stl* s) {
    free(s->xyz); free(s->tri); free(s->nrm);
    memset(s, 0, sizeof *s);
}

bool cv_stl_to_frd(const cv_stl* s, cv_frd* f, uint32_t* dropped) {
    memset(f, 0, sizeof *f);
    uint32_t nt = 0, nv = s->n_vert;
    for (uint32_t t = 0; t < s->n_tri; t++) {
        const uint32_t* v = s->tri + 3 * (size_t)t;
        nt += v[0] != v[1] && v[1] != v[2] && v[2] != v[0];
    }
    if (dropped) *dropped = s->n_tri - nt;
    f->n_nodes = nv;
    f->n_elems = nt;
    f->node_id = malloc(CV_MAX(nv, 1) * sizeof *f->node_id);
    f->xyz = malloc(CV_MAX(nv, 1) * 3 * sizeof *f->xyz);
    f->elem_id = malloc(CV_MAX(nt, 1) * sizeof *f->elem_id);
    f->etype = malloc(CV_MAX(nt, 1));
    f->emat = calloc(CV_MAX(nt, 1), sizeof *f->emat);
    f->egrp = calloc(CV_MAX(nt, 1), sizeof *f->egrp);
    f->eoff = malloc(((size_t)nt + 1) * sizeof *f->eoff);
    f->conn = malloc(CV_MAX(nt, 1) * 3 * sizeof *f->conn);
    if (!f->node_id || !f->xyz || !f->elem_id || !f->etype || !f->emat || !f->egrp || !f->eoff || !f->conn) goto oom;
    for (uint32_t i = 0; i < nv; i++) f->node_id[i] = i + 1;
    if (nv) memcpy(f->xyz, s->xyz, (size_t)nv * 3 * sizeof *f->xyz);
    uint32_t e = 0;
    for (uint32_t t = 0; t < s->n_tri; t++) {
        const uint32_t* v = s->tri + 3 * (size_t)t;
        if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) continue;
        f->elem_id[e] = e + 1;
        f->etype[e] = 7;                          /* Tri3: corners counter-clockwise, as the STL has them */
        f->eoff[e] = 3 * e;
        memcpy(f->conn + 3 * (size_t)e, v, 3 * sizeof *v);
        e++;
    }
    f->eoff[nt] = 3 * nt;
    if (!cv_frd_build_maps(f, NULL, NULL)) goto oom;
    return true;
oom:
    cv_frd_free(f);
    return false;
}

static void wr_u32(unsigned char* p, uint32_t u) { p[0] = (unsigned char)u; p[1] = (unsigned char)(u >> 8); p[2] = (unsigned char)(u >> 16); p[3] = (unsigned char)(u >> 24); }
static void wr_f32(unsigned char* p, float f) { uint32_t u; memcpy(&u, &f, 4); wr_u32(p, u); }

void cv_stl_order_far(const float* lo, const float* hi, int n, const float eye[3], int* order) {
    double d[64];
    for (int i = 0; i < n; i++) {
        order[i] = i;
        double s = 0;
        for (int k = 0; k < 3; k++) { double c = 0.5 * ((double)lo[3 * i + k] + hi[3 * i + k]) - eye[k]; s += c * c; }
        if (i < 64) d[i] = s;
    }
    for (int i = 1; i < n && n <= 64; i++)            /* insertion: a handful of layers, stable */
        for (int j = i; j > 0 && d[order[j]] > d[order[j - 1]]; j--) { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
}

bool cv_stl_write(const char* path, const float* tri9, uint32_t n) {
    FILE* o = fopen(path, "wb");
    if (!o) return false;
    unsigned char h[84] = { 0 };
    memcpy(h, "binary STL written by ccxview", 29);
    wr_u32(h + 80, n);
    bool ok = fwrite(h, 1, 84, o) == 84;
    for (uint32_t t = 0; t < n && ok; t++) {
        const float* v = tri9 + 9 * (size_t)t;
        float u[3], w[3], c[3];
        for (int k = 0; k < 3; k++) { u[k] = v[3 + k] - v[k]; w[k] = v[6 + k] - v[k]; }
        c[0] = u[1] * w[2] - u[2] * w[1]; c[1] = u[2] * w[0] - u[0] * w[2]; c[2] = u[0] * w[1] - u[1] * w[0];
        float l = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        unsigned char r[50] = { 0 };
        for (int k = 0; k < 3; k++) wr_f32(r + 4 * k, l > 0 ? c[k] / l : 0.f);
        for (int k = 0; k < 9; k++) wr_f32(r + 12 + 4 * k, v[k]);
        ok = fwrite(r, 1, 50, o) == 50;
    }
    return fclose(o) == 0 && ok;
}

/* ---- feature edges ------------------------------------------------------------- */
typedef struct { uint64_t key; uint32_t tri; } edge_ref;

static int edge_cmp(const void* a, const void* b) {
    uint64_t x = ((const edge_ref*)a)->key, y = ((const edge_ref*)b)->key;
    return (x > y) - (x < y);
}

bool cv_stl_edges(const cv_stl* s, float deg, uint32_t** out, uint32_t* n) {
    *out = NULL; *n = 0;
    size_t m = (size_t)s->n_tri * 3;
    if (!m) return true;
    edge_ref* e = malloc(m * sizeof *e);
    uint32_t* o = malloc(m * 2 * sizeof *o);
    if (!e || !o) { free(e); free(o); return false; }
    for (uint32_t t = 0; t < s->n_tri; t++)
        for (int c = 0; c < 3; c++) {
            uint64_t a = s->tri[3 * (size_t)t + c], b = s->tri[3 * (size_t)t + (c + 1) % 3];
            e[3 * (size_t)t + c] = (edge_ref){ a < b ? a << 32 | b : b << 32 | a, t };
        }
    qsort(e, m, sizeof *e, edge_cmp);
    float lim = cosf(deg * 3.14159265f / 180.f);
    size_t k = 0;
    for (size_t i = 0; i < m;) {
        size_t j = i + 1;
        while (j < m && e[j].key == e[i].key) j++;
        bool keep = j - i != 2;                  /* open, or more than two faces meet there */
        if (!keep) {
            const float* p = s->nrm + 3 * (size_t)e[i].tri, *q = s->nrm + 3 * (size_t)e[i + 1].tri;
            keep = p[0] * q[0] + p[1] * q[1] + p[2] * q[2] < lim;
        }
        if (keep && e[i].key >> 32 != (e[i].key & 0xffffffffu)) {
            o[2 * k] = (uint32_t)(e[i].key >> 32); o[2 * k + 1] = (uint32_t)e[i].key; k++;
        }
        i = j;
    }
    free(e);
    *out = o; *n = (uint32_t)k;
    return true;
}
