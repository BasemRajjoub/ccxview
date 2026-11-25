/* base.h -- common includes, growable arrays and the message list. */
#ifndef CV_BASE_H
#define CV_BASE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define CV_MIN(a, b) ((a) < (b) ? (a) : (b))
#define CV_MAX(a, b) ((a) > (b) ? (a) : (b))
#define CV_COUNT(a)  (sizeof(a) / sizeof((a)[0]))

/* ---- growable array -------------------------------------------------------
   A plain struct { T* a; size_t n, cap; }. cv_grow() returns false when the
   allocation fails, so callers can stop loading instead of crashing. */
#define CV_VEC(T) struct { T* a; size_t n, cap; }

static inline bool cv_grow_(void** a, size_t* cap, size_t need, size_t elem) {
    if (need <= *cap) return true;
    size_t c = *cap ? *cap : 16;
    while (c < need) c += c / 2 + 16;
    void* p = realloc(*a, c * elem);
    if (!p) return false;
    *a = p;
    *cap = c;
    return true;
}
#define cv_reserve(v, need) cv_grow_((void**)&(v).a, &(v).cap, (need), sizeof(*(v).a))
#define cv_push(v, x) \
    (cv_reserve((v), (v).n + 1) ? ((v).a[(v).n++] = (x), true) : false)
#define cv_free_vec(v) (free((v).a), (v).a = NULL, (v).n = (v).cap = 0)
typedef CV_VEC(float) cv_fvec;          /* a named one, so functions can take it */

/* ---- message list ---------------------------------------------------------
   Everything the parser or loader wants to tell the user. `where` is a 1-based
   line number for ASCII records or a byte offset for binary ones. */
enum { CV_MSG_MAX = 500 };

typedef struct {
    uint64_t where;
    bool     is_offset;   /* where = byte offset instead of line number */
    char     text[120];
} cv_msg;

typedef struct {
    cv_msg* a;
    size_t  n, cap;
    size_t  dropped;       /* messages not stored because the list was full */
} cv_msgs;

/* Assertions for invariants inside the headless modules: on in test / debug
   builds (-DCV_DEBUG), compiled out otherwise. */
#ifdef CV_DEBUG
#define CV_ASSERT(c) do { if (!(c)) { fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)
#else
#define CV_ASSERT(c) ((void)0)
#endif

extern void (*cv_msg_sink)(const char*);   /* log.c: every message also goes to the log */

static inline void cv_msg_add(cv_msgs* m, uint64_t where, bool is_offset, const char* text) {
    if (cv_msg_sink) cv_msg_sink(text);
    if (m->n >= CV_MSG_MAX || !cv_reserve(*m, m->n + 1)) { m->dropped++; return; }
    cv_msg* e = &m->a[m->n++];
    e->where = where;
    e->is_offset = is_offset;
    snprintf(e->text, sizeof e->text, "%s", text);
}

/* ---- small string / sort helpers shared by several modules ------------------ */

static inline bool cv_ends_with_ci(const char* s, const char* suf) {
    size_t a = strlen(s), b = strlen(suf);
    if (a < b) return false;
    for (size_t i = 0; i < b; i++) if ((s[a - b + i] | 32) != (suf[i] | 32)) return false;
    return true;
}

static inline const char* cv_basename(const char* p) {
    const char* b = p;
    for (const char* q = p; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
    return b;
}

static inline int cv_cmp_u32(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}

#endif
