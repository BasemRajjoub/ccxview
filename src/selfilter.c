/* selfilter.c -- tests on the entries of a selection; see selfilter.h. */
#include "selfilter.h"
#include <ctype.h>
#include <math.h>

float cv_selfilter_coord(const cv_selfilter* q, const float p[3]) {
    if (q->coord <= CV_SC_Z) return p[q->coord];
    int a = q->axis < 0 || q->axis > 2 ? 2 : q->axis, u = (a + 1) % 3, v = (a + 2) % 3;   /* right-handed: X -> (Y, Z) */
    float du = p[u] - q->at[u], dv = p[v] - q->at[v];
    if (q->coord == CV_SC_R) return sqrtf(du * du + dv * dv);
    if (q->coord == CV_SC_THETA) return atan2f(dv, du) * (float)(180.0 / 3.14159265358979);
    return p[a] - q->at[a];
}

static void centroid(const cv_frd* f, uint32_t e, float c[3]) {
    uint32_t b = f->eoff[e], m = f->eoff[e + 1] - b;
    double s[3] = { 0, 0, 0 };
    for (uint32_t j = 0; j < m; j++) for (int k = 0; k < 3; k++) s[k] += f->xyz[3 * f->conn[b + j] + k];
    for (int k = 0; k < 3; k++) c[k] = m ? (float)(s[k] / m) : 0.f;
}

/* an element's value: its own, else its highest (lowest) node's */
static float elem_value(const cv_frd* f, uint32_t e, const float* nval, const float* eval, bool low) {
    if (eval) return eval[e];
    float r = NAN;
    for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) {
        float v = nval[f->conn[j]];
        if (v == v && (r != r || (low ? v < r : v > r))) r = v;
    }
    return r;
}

static int cmp_desc(const void* a, const void* b) {
    float p = *(const float*)a, q = *(const float*)b;
    return p < q ? 1 : p > q ? -1 : 0;
}

/* the value the top pct % of vals (n, NaN left out) reach, +inf when none */
static float top_threshold(const float* vals, uint32_t n, float pct) {
    float* s = malloc((size_t)CV_MAX(n, 1) * sizeof *s);
    uint32_t m = 0;
    if (!s) return INFINITY;
    for (uint32_t i = 0; i < n; i++) if (vals[i] == vals[i]) s[m++] = vals[i];
    float t = INFINITY;
    if (m && pct > 0) {
        qsort(s, m, sizeof *s, cmp_desc);
        uint32_t k = (uint32_t)ceil((double)m * CV_MIN(pct, 100.f) / 100.0);
        t = s[CV_MIN(CV_MAX(k, 1u), m) - 1];
    }
    free(s);
    return t;
}

static bool pass_value(const cv_selfilter* q, float v, float thr) {
    if (v != v) return false;
    return q->kind == CV_SQ_ABOVE ? v > q->value : q->kind == CV_SQ_BELOW ? v < q->value : v >= thr;
}

static bool is_field(int k) { return k == CV_SQ_ABOVE || k == CV_SQ_BELOW || k == CV_SQ_TOP; }

uint32_t cv_selfilter_elems(const cv_frd* f, const cv_selfilter* q, const float* nval, const float* eval,
                            uint32_t* el, uint32_t ne) {
    if (is_field(q->kind) && !nval && !eval) return 0;
    float* v = is_field(q->kind) ? calloc(CV_MAX(ne, 1), sizeof *v) : NULL;
    if (is_field(q->kind) && !v) return 0;
    for (uint32_t k = 0; v && k < ne; k++) v[k] = elem_value(f, el[k], nval, eval, q->kind == CV_SQ_BELOW);
    float thr = v && q->kind == CV_SQ_TOP ? top_threshold(v, ne, q->value) : 0.f;
    uint32_t m = 0;
    for (uint32_t k = 0; k < ne; k++) {
        uint32_t e = el[k];
        bool ok;
        if (v) ok = pass_value(q, v[k], thr);
        else if (q->kind == CV_SQ_TYPE) ok = f->etype[e] == q->code;
        else if (q->kind == CV_SQ_MAT) ok = f->emat && f->emat[e] == q->code;
        else { float c[3]; centroid(f, e, c); float x = cv_selfilter_coord(q, c); ok = x >= q->lo && x <= q->hi; }
        if (ok) el[m++] = e;
    }
    free(v);
    return m;
}

uint32_t cv_selfilter_nodes(const cv_frd* f, const cv_selfilter* q, const float* nval, uint32_t* nd, uint32_t nn) {
    if (q->kind == CV_SQ_TYPE || q->kind == CV_SQ_MAT) return nn;   /* not a node's property: they all stay */
    if (is_field(q->kind) && !nval) return 0;
    float* v = is_field(q->kind) ? calloc(CV_MAX(nn, 1), sizeof *v) : NULL;
    if (is_field(q->kind) && !v) return 0;
    for (uint32_t k = 0; v && k < nn; k++) v[k] = nval[nd[k]];
    float thr = v && q->kind == CV_SQ_TOP ? top_threshold(v, nn, q->value) : 0.f;
    uint32_t m = 0;
    for (uint32_t k = 0; k < nn; k++) {
        bool ok;
        if (v) ok = pass_value(q, v[k], thr);
        else { float x = cv_selfilter_coord(q, f->xyz + 3 * nd[k]); ok = x >= q->lo && x <= q->hi; }
        if (ok) nd[m++] = nd[k];
    }
    free(v);
    return m;
}

/* ---- "x>10", "10<x<20" ------------------------------------------------------------------ */

static const char* skip(const char* p) { while (isspace((unsigned char)*p)) p++; return p; }

static bool read_num(const char** p, float* v) {
    char* end;
    *v = strtof(skip(*p), &end);
    if (end == skip(*p)) return false;
    *p = end;
    return true;
}

static int read_coord(const char** p) {
    static const struct { const char* name; int c; } names[] = {   /* the longer names first */
        { "theta", CV_SC_THETA }, { "axial", CV_SC_AXIAL }, { "x", CV_SC_X }, { "y", CV_SC_Y }, { "z", CV_SC_Z },
        { "r", CV_SC_R }, { "t", CV_SC_THETA }, { "a", CV_SC_AXIAL },
    };
    const char* s = skip(*p);
    for (size_t i = 0; i < CV_COUNT(names); i++) {
        size_t l = strlen(names[i].name);
        if (!strncmp(s, names[i].name, l)) { *p = s + l; return names[i].c; }
    }
    return -1;
}

bool cv_selfilter_parse(const char* text, cv_selfilter* q) {
    const char* p = text;
    float a;
    int c;
    q->kind = CV_SQ_COORD; q->lo = -INFINITY; q->hi = INFINITY;
    if (read_num(&p, &a)) {                         /* lo < coord < hi */
        p = skip(p);
        if (*p++ != '<') return false;
        if ((c = read_coord(&p)) < 0) return false;
        p = skip(p);
        if (*p++ != '<') return false;
        float b;
        if (!read_num(&p, &b)) return false;
        q->coord = c; q->lo = a; q->hi = b;
        return !*skip(p);
    }
    if ((c = read_coord(&p)) < 0) return false;
    p = skip(p);
    char op = *p++;
    if ((op != '<' && op != '>') || !read_num(&p, &a)) return false;
    q->coord = c;
    if (op == '<') q->hi = a; else q->lo = a;
    return !*skip(p);
}
