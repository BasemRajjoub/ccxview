/* sta.c -- .sta / .cvg readers. See sta.h. */
#include "sta.h"
#include "frd.h"      /* cv_parse_num */

typedef struct { const char* b; const char* e; } tok;

static int split(const char* s, const char* e, tok* t, int max) {
    int n = 0;
    while (n < max) {
        while (s < e && (*s == ' ' || *s == '\t' || *s == '\r')) s++;
        if (s >= e) break;
        t[n].b = s;
        while (s < e && *s != ' ' && *s != '\t' && *s != '\r') s++;
        t[n++].e = s;
    }
    return n;
}

/* an integer, optionally followed by a letter flag (the U of an unconverged attempt) */
static bool as_int(tok t, int* v, char* flag) {
    const char* s = t.b;
    if (s >= t.e) return false;
    long x = 0;
    for (; s < t.e && *s >= '0' && *s <= '9'; s++) x = x * 10 + (*s - '0');
    if (s == t.b || x > 1000000000L) return false;
    if (flag) *flag = s < t.e ? *s : 0;
    else if (s < t.e) return false;
    *v = (int)x;
    return true;
}

static bool as_num(tok t, float* v) {
    double d;
    if (!cv_parse_num(t.b, t.e, &d)) return false;
    *v = (float)d;
    return true;
}

static bool each_line(const char* data, size_t size, bool (*fn)(cv_sta*, const tok*, int), cv_sta* s) {
    const char* c = data;
    const char* end = data + size;
    while (c < end) {
        const char* nl = memchr(c, '\n', (size_t)(end - c));
        const char* e = nl ? nl : end;
        tok t[12];
        int n = split(c, e, t, 12);
        c = nl ? nl + 1 : end;
        if (n && !fn(s, t, n)) return false;
    }
    return true;
}

static bool sta_line(cv_sta* s, const tok* t, int n) {
    cv_sta_inc r = {0};
    char flag = 0;
    if (n < 7 || !as_int(t[0], &r.step, NULL) || !as_int(t[1], &r.inc, NULL) || !as_int(t[2], &r.att, &flag) ||
        !as_int(t[3], &r.iters, NULL) || !as_num(t[4], &r.total_time) || !as_num(t[5], &r.step_time) ||
        !as_num(t[6], &r.inc_time))
        return true;                                     /* header or junk: skip */
    r.cutback = flag == 'U' || flag == 'u';
    CV_VEC(cv_sta_inc) v = { s->inc, s->ninc, s->ninc };
    if (!cv_push(v, r)) return false;
    s->inc = v.a; s->ninc = (uint32_t)v.n;
    return true;
}

static bool cvg_line(cv_sta* s, const tok* t, int n) {
    cv_cvg_iter r = {0};
    if (n < 6 || !as_int(t[0], &r.step, NULL) || !as_int(t[1], &r.inc, NULL) || !as_int(t[2], &r.att, NULL) ||
        !as_int(t[3], &r.iter, NULL) || !as_int(t[4], &r.contact_elems, NULL) || !as_num(t[5], &r.resid_force))
        return true;
    if (n > 6) as_num(t[6], &r.corr_disp);
    if (n > 7) as_num(t[7], &r.resid_flux);
    if (n > 8) as_num(t[8], &r.corr_temp);
    CV_VEC(cv_cvg_iter) v = { s->it, s->nit, s->nit };
    if (!cv_push(v, r)) return false;
    s->it = v.a; s->nit = (uint32_t)v.n;
    return true;
}

bool cv_sta_parse(cv_sta* s, const char* data, size_t size) {
    free(s->inc); s->inc = NULL; s->ninc = 0;
    return each_line(data, size, sta_line, s);
}

bool cv_cvg_parse(cv_sta* s, const char* data, size_t size) {
    free(s->it); s->it = NULL; s->nit = 0;
    return each_line(data, size, cvg_line, s);
}

void cv_sta_free(cv_sta* s) {
    free(s->inc); free(s->it);
    memset(s, 0, sizeof *s);
}
