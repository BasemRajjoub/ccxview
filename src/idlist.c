/* idlist.c -- id lists as text; see idlist.h. */
#include "idlist.h"
#include <ctype.h>

char* cv_idlist_format(const uint32_t* ids, size_t n) {
    size_t cap = n * 12 + 1, o = 0;            /* worst case: "4294967295, " per id */
    char* s = malloc(cap);
    if (!s) return NULL;
    s[0] = 0;
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j + 1 < n && ids[j + 1] == ids[j] + 1 && ids[j] != UINT32_MAX) j++;   /* a run */
        const char* sep = o ? ", " : "";
        if (j == i) o += (size_t)snprintf(s + o, cap - o, "%s%u", sep, ids[i]);
        else if (j == i + 1) o += (size_t)snprintf(s + o, cap - o, "%s%u, %u", sep, ids[i], ids[j]);
        else o += (size_t)snprintf(s + o, cap - o, "%s%u-%u", sep, ids[i], ids[j]);
        i = j + 1;
    }
    return s;
}

static bool is_sep(char c) { return c == ',' || c == ';' || isspace((unsigned char)c); }

/* a decimal id at *p, moved past it; false (p unmoved) when there is none or it overflows */
static bool read_id(const char** p, uint32_t* v) {
    const char* q = *p;
    if (!isdigit((unsigned char)*q)) return false;
    uint64_t x = 0;
    while (isdigit((unsigned char)*q)) { x = x * 10 + (uint64_t)(*q - '0'); if (x > UINT32_MAX) return false; q++; }
    *v = (uint32_t)x;
    *p = q;
    return true;
}

static void note_bad(cv_idlist* l, const char* s, const char* e) {
    l->bad++;
    size_t o = strlen(l->bad_text), room = sizeof l->bad_text - 1 - o;
    if (o && room > 2) { memcpy(l->bad_text + o, ", ", 2); o += 2; room -= 2; }
    size_t k = CV_MIN((size_t)(e - s), room);
    memcpy(l->bad_text + o, s, k);
    l->bad_text[o + k] = 0;
}

bool cv_idlist_parse(const char* text, cv_idlist* l) {
    memset(l, 0, sizeof *l);
    CV_VEC(uint32_t) r = {0};                  /* lo, hi pairs */
    uint64_t total = 0;
    const char* p = text ? text : "";
    while (*p) {
        while (*p && is_sep(*p)) p++;
        if (!*p) break;
        const char* t = p;
        uint32_t a, b;
        bool ok = read_id(&p, &a);
        b = a;
        if (ok) {                              /* "a", "a-b", "a - b" */
            const char* q = p;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '-') {
                q++;
                while (*q == ' ' || *q == '\t') q++;
                ok = read_id(&q, &b);
                p = q;
            }
        }
        if (ok && *p && !is_sep(*p)) ok = false;   /* "12abc" */
        if (!ok) {
            while (*p && !is_sep(*p)) p++;
            note_bad(l, t, p);
            continue;
        }
        if (a > b) { uint32_t x = a; a = b; b = x; }
        total += (uint64_t)b - a + 1;
        if (total > CV_IDLIST_MAX || !cv_push(r, a) || !cv_push(r, b)) { cv_free_vec(r); memset(l, 0, sizeof *l); return false; }
    }
    if (total) {
        l->ids = malloc((size_t)total * sizeof *l->ids);
        if (!l->ids) { cv_free_vec(r); memset(l, 0, sizeof *l); return false; }
        size_t n = 0;
        for (size_t i = 0; i < r.n; i += 2)
            for (uint64_t v = r.a[i]; v <= r.a[i + 1]; v++) l->ids[n++] = (uint32_t)v;
        qsort(l->ids, n, sizeof *l->ids, cv_cmp_u32);
        size_t m = 0;
        for (size_t i = 0; i < n; i++) if (!m || l->ids[i] != l->ids[m - 1]) l->ids[m++] = l->ids[i];
        l->n = m;
    }
    cv_free_vec(r);
    return true;
}

void cv_idlist_free(cv_idlist* l) {
    free(l->ids);
    memset(l, 0, sizeof *l);
}
