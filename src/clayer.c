/* clayer.c -- the contact interface layer's solids (clayer.h). */
#include "clayer.h"
#include <math.h>

float cv_clay_top(const cv_lpt* s, const cv_lpt* f, const float n[3], float gap, int place, float minth, cv_lpt* top) {
    *top = *s;
    if (gap != gap || !f) return 0.f;
    /* from the slave towards the top: against the normal when open, along it when the
       corner lies behind the master face */
    float dir = gap < 0 ? 1.f : -1.f, ag = fabsf(gap), more = minth > ag ? minth - ag : 0.f;
    if (place == CV_CLAY_TRUE) {
        for (int i = 0; i < 3; i++) top->p[i] = s->p[i] + dir * (ag + more) * n[i];
        return gap;
    }
    *top = *f;
    for (int i = 0; i < 3; i++) top->p[i] += dir * more * n[i];
    return gap;
}

static void mid(const cv_lpt* a, const cv_lpt* b, cv_lpt* o) {
    for (int i = 0; i < 3; i++) o->p[i] = 0.5f * (a->p[i] + b->p[i]);
    for (int i = 0; i < 6; i++) o->d[i] = 0.5f * (a->d[i] + b->d[i]);
}

static void centre(const cv_lpt* const* c, int n, cv_lpt* o) {
    memset(o, 0, sizeof *o);
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < 3; i++) o->p[i] += c[k]->p[i] / (float)n;
        for (int i = 0; i < 6; i++) o->d[i] += c[k]->d[i] / (float)n;
    }
}

typedef struct { cv_lpt* tri; float* val; int nt; } sink;

static void put(sink* s, const cv_lpt* a, const cv_lpt* b, const cv_lpt* c, float va, float vb, float vc) {
    if (s->nt >= CV_CLAY_MAXTRI) return;
    cv_lpt* t = s->tri + 3 * s->nt;
    float* v = s->val + 3 * s->nt;
    t[0] = *a; t[1] = *b; t[2] = *c;
    v[0] = va; v[1] = vb; v[2] = vc;
    s->nt++;
}

/* a face of the prism, corners c[0..n) turning counter-clockwise seen from outside */
static void face(sink* s, const cv_lpt* const* c, const float* v, int n, bool patches) {
    if (!patches) {
        for (int k = 1; k + 1 < n; k++) put(s, c[0], c[k], c[k + 1], v[0], v[k], v[k + 1]);
        return;
    }
    cv_lpt m;
    centre(c, n, &m);
    for (int k = 0; k < n; k++) {
        cv_lpt a, b;                                /* the middles of the edges to the next and from the last */
        mid(c[k], c[(k + 1) % n], &a);
        mid(c[k], c[(k + n - 1) % n], &b);
        put(s, c[k], &a, &m, v[k], v[k], v[k]);
        put(s, c[k], &m, &b, v[k], v[k], v[k]);
    }
}

int cv_clay_prism(int n, const cv_lpt* b, const cv_lpt* t, const float* v, bool patches, cv_lpt* tri, float* val) {
    if (n != 3 && n != 4) return 0;
    sink s = { tri, val, 0 };
    const cv_lpt* c[4];
    float w[4];
    /* the bottom, against the slave: the face's corners the other way round */
    for (int k = 0; k < n; k++) { c[k] = &b[n - 1 - k]; w[k] = v[n - 1 - k]; }
    face(&s, c, w, n, patches);
    /* the top, towards the master: in the face's order */
    for (int k = 0; k < n; k++) { c[k] = &t[k]; w[k] = v[k]; }
    face(&s, c, w, n, patches);
    /* the sides: along the face's edge, then up */
    for (int k = 0; k < n; k++) {
        int j = (k + 1) % n;
        const cv_lpt* q[4] = { &b[k], &b[j], &t[j], &t[k] };
        float u[4] = { v[k], v[j], v[j], v[k] };
        face(&s, q, u, 4, patches);
    }
    return s.nt;
}

int cv_clay_edges(int n, const cv_lpt* b, const cv_lpt* t, cv_lpt* seg) {
    if (n != 3 && n != 4) return 0;
    int m = 0;
    for (int k = 0; k < n; k++) {
        int j = (k + 1) % n;
        seg[2 * m] = b[k]; seg[2 * m + 1] = b[j]; m++;
        seg[2 * m] = t[k]; seg[2 * m + 1] = t[j]; m++;
        seg[2 * m] = b[k]; seg[2 * m + 1] = t[k]; m++;
    }
    return m;
}
