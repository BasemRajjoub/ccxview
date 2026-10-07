/* inp_loads.c -- what a deck applies in one step. Headless.

   The reader keeps every *BOUNDARY, *CLOAD, *DLOAD, ... line with the step it
   stands in (inp.h). CalculiX carries loads from step to step: a later line for
   the same node and DOF, or the same element face, replaces the earlier one, and a
   card with OP=NEW first drops everything of its kind given in earlier steps.
   Supports given before the first step are part of the model and stay. */
#include "inp.h"
#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t key; uint32_t seq; } ks;

static int ks_cmp(const void* a, const void* b) {
    const ks *x = a, *y = b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}
static int u32_cmp(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? -1 : x > y;
}

/* Of n lines in deck order with keys k[i].key (k[i].seq = i), the last line of
   every key, in deck order: their indices in keep[], the count returned. */
static uint32_t keep_last(ks* k, uint32_t n, uint32_t* keep) {
    uint32_t m = 0;
    qsort(k, n, sizeof *k, ks_cmp);
    for (uint32_t i = 0; i < n; i++)
        if (i + 1 == n || k[i + 1].key != k[i].key) keep[m++] = k[i].seq;
    qsort(keep, m, sizeof *keep, u32_cmp);
    return m;
}

/* which family an OP=NEW marker clears */
static int cload_family(int dof) { return dof == 11 ? 1 : 0; }
static int dload_family(int kind) { return kind == CV_DL_P || kind == CV_DL_EDGE ? 0 : kind; }
static int body_family(int kind) { return kind == CV_BL_HEAT ? 1 : 0; }

#define FOLD(T, src, nsrc, dst, ndst, IS_MARK, KEEPS, KEY)                                             \
    do {                                                                                                \
        uint32_t cap = (nsrc), n = 0;                                                                   \
        T* v = malloc(CV_MAX(cap, 1) * sizeof *v);                                                      \
        ks* k = malloc(CV_MAX(cap, 1) * sizeof *k);                                                     \
        uint32_t* keep = malloc(CV_MAX(cap, 1) * sizeof *keep);                                         \
        if (!v || !k || !keep) { free(v); free(k); free(keep); goto oom; }                              \
        for (uint32_t i = 0; i < (nsrc); i++) {                                                         \
            const T* r = &(src)[i];                                                                     \
            if (r->step > step) continue;                                                               \
            if (IS_MARK) {               /* OP=NEW: what its family had from the steps goes */          \
                uint32_t m = 0;                                                                         \
                for (uint32_t j = 0; j < n; j++) {                                                      \
                    const T* q = &v[j]; (void)q;                                                        \
                    if (KEEPS) v[m++] = v[j];                                                           \
                }                                                                                       \
                n = m;                                                                                  \
                continue;                                                                               \
            }                                                                                           \
            v[n++] = *r;                                                                                \
        }                                                                                               \
        for (uint32_t i = 0; i < n; i++) { const T* r = &v[i]; k[i].key = (KEY); k[i].seq = i; }        \
        uint32_t m = keep_last(k, n, keep);                                                             \
        for (uint32_t i = 0; i < m; i++) v[i] = v[keep[i]];                                             \
        free(k); free(keep);                                                                            \
        (dst) = v; (ndst) = m;                                                                          \
    } while (0)

bool cv_inp_applied(const cv_inp* d, int step, cv_applied* a) {
    memset(a, 0, sizeof *a);
    if (step < 0 || step >= d->nsteps) step = CV_MAX(d->nsteps - 1, 0);

    /* supports, one entry per DOF so that a later line can replace a single DOF */
    uint32_t nb = 0;
    for (uint32_t i = 0; i < d->nbcs; i++) nb += d->bcs[i].node ? (uint32_t)(d->bcs[i].dof_hi - d->bcs[i].dof_lo + 1) : 1;
    cv_bc* one = malloc(CV_MAX(nb, 1) * sizeof *one);
    if (!one) goto oom;
    nb = 0;
    for (uint32_t i = 0; i < d->nbcs; i++) {
        cv_bc b = d->bcs[i];
        if (!b.node) { one[nb++] = b; continue; }
        for (int dof = d->bcs[i].dof_lo; dof <= d->bcs[i].dof_hi; dof++) { b.dof_lo = b.dof_hi = (uint8_t)dof; one[nb++] = b; }
    }
    FOLD(cv_bc, one, nb, a->bcs, a->nbcs, !r->node, q->step < 0, ((uint64_t)r->node << 8) | r->dof_lo);
    free(one); one = NULL;
    FOLD(cv_cload, d->cloads, d->ncloads, a->cloads, a->ncloads, !r->node,
         cload_family(q->dof) != cload_family(r->dof), ((uint64_t)r->node << 8) | r->dof);
    FOLD(cv_dload, d->dloads, d->ndloads, a->dloads, a->ndloads, !r->elem,
         dload_family(q->kind) != dload_family(r->kind), ((uint64_t)r->elem << 16) | ((uint64_t)r->kind << 8) | r->face);
    /* a body load: per kind and element set; BX, BY, BZ apart */
    FOLD(cv_body, d->body, d->nbody, a->body, a->nbody, r->set == -2, body_family(q->kind) != body_family(r->kind),
         ((uint64_t)(r->set >= 0 ? (uint32_t)r->set : r->elem) << 16) | ((uint64_t)(r->set >= 0) << 15) | ((uint64_t)r->kind << 8) |
         (r->kind == CV_BL_FORCE ? (r->v[0] != 0 ? 1u : r->v[1] != 0 ? 2u : 3u) : 0u));
    FOLD(cv_ntemp, d->temps, d->ntemps, a->temps, a->ntemps, !r->node, false, (uint64_t)r->node);
    /* a rigid body's ROT NODE: its DOFs 1-3 turn the body, so a force there is a
       moment and a held DOF a held rotation */
    for (int k = 0; k < d->nlinks; k++) {
        uint32_t rot = d->links[k].kind == CV_LINK_RIGID ? d->links[k].rot : 0;
        if (!rot) continue;
        for (uint32_t i = 0; i < a->nbcs; i++)
            if (a->bcs[i].node == rot && a->bcs[i].dof_lo <= 3) { a->bcs[i].dof_lo += 3; a->bcs[i].dof_hi += 3; }
        for (uint32_t i = 0; i < a->ncloads; i++)
            if (a->cloads[i].node == rot && a->cloads[i].dof <= 3) a->cloads[i].dof += 3;
    }
    return true;
oom:
    free(one);
    cv_applied_free(a);
    return false;
}

void cv_applied_free(cv_applied* a) {
    free(a->bcs); free(a->cloads); free(a->dloads); free(a->body); free(a->temps);
    memset(a, 0, sizeof *a);
}
