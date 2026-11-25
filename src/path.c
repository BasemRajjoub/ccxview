/* path.c -- Dijkstra over the skin's edge graph, undeformed Euclidean weights. */
#include "path.h"
#include <math.h>

/* ---- tiny binary min-heap of (dist, node), lazy deletion on stale entries -- */

typedef struct { float dist; uint32_t node; } heap_item;
typedef CV_VEC(heap_item) heap_t;

static bool heap_push(heap_t* h, float dist, uint32_t node) {
    heap_item it = { dist, node };
    if (!cv_push(*h, it)) return false;
    size_t i = h->n - 1;
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (h->a[p].dist <= h->a[i].dist) break;
        heap_item t = h->a[p]; h->a[p] = h->a[i]; h->a[i] = t;
        i = p;
    }
    return true;
}

static heap_item heap_pop(heap_t* h) {
    heap_item top = h->a[0];
    h->a[0] = h->a[--h->n];
    size_t i = 0;
    for (;;) {
        size_t l = 2 * i + 1, r = 2 * i + 2, s = i;
        if (l < h->n && h->a[l].dist < h->a[s].dist) s = l;
        if (r < h->n && h->a[r].dist < h->a[s].dist) s = r;
        if (s == i) break;
        heap_item t = h->a[s]; h->a[s] = h->a[i]; h->a[i] = t;
        i = s;
    }
    return top;
}

static inline float edge_len(const cv_frd* f, uint32_t u, uint32_t v) {
    const float* p = f->xyz + 3 * u;
    const float* q = f->xyz + 3 * v;
    float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

bool cv_path_find(const cv_frd* f, const cv_skin* skin, uint32_t a, uint32_t b,
                  uint32_t** out, uint32_t* n, float** dist) {
    *out = NULL; *n = 0; if (dist) *dist = NULL;
    if (a >= f->n_nodes || b >= f->n_nodes) return false;

    if (a == b) {                              /* trivial path, no graph needed */
        uint32_t* o = malloc(sizeof *o);
        if (!o) return false;
        o[0] = a;
        if (dist) {
            float* d = malloc(sizeof *d);
            if (!d) { free(o); return false; }
            d[0] = 0.f;
            *dist = d;
        }
        *out = o; *n = 1;
        return true;
    }

    const uint32_t N = f->n_nodes;
    uint32_t* off = calloc((size_t)N + 1, sizeof *off);
    uint32_t* cur = malloc(((size_t)N + 1) * sizeof *cur);
    uint32_t* adj_to = NULL;
    float*    adj_w  = NULL;
    float*    pdist  = NULL;
    uint32_t* prev   = NULL;
    heap_t    heap   = {0};
    if (!off || !cur) goto oom;

    for (size_t i = 0; i < skin->n_edge; i++) {
        uint32_t u = skin->edge[2 * i], v = skin->edge[2 * i + 1];
        if (u >= N || v >= N) continue;        /* defensive: never trust input blindly */
        off[u + 1]++; off[v + 1]++;
    }
    for (uint32_t i = 0; i < N; i++) off[i + 1] += off[i];
    size_t m = off[N];
    adj_to = malloc(CV_MAX(m, 1) * sizeof *adj_to);
    adj_w  = malloc(CV_MAX(m, 1) * sizeof *adj_w);
    if (!adj_to || !adj_w) goto oom;
    memcpy(cur, off, ((size_t)N + 1) * sizeof *cur);
    for (size_t i = 0; i < skin->n_edge; i++) {
        uint32_t u = skin->edge[2 * i], v = skin->edge[2 * i + 1];
        if (u >= N || v >= N) continue;
        float w = edge_len(f, u, v);
        adj_to[cur[u]] = v; adj_w[cur[u]] = w; cur[u]++;
        adj_to[cur[v]] = u; adj_w[cur[v]] = w; cur[v]++;
    }

    pdist = malloc((size_t)N * sizeof *pdist);
    prev  = malloc((size_t)N * sizeof *prev);
    if (!pdist || !prev) goto oom;
    for (uint32_t i = 0; i < N; i++) { pdist[i] = INFINITY; prev[i] = UINT32_MAX; }
    pdist[a] = 0.f;
    if (!heap_push(&heap, 0.f, a)) goto oom;

    while (heap.n > 0) {
        heap_item cur_it = heap_pop(&heap);
        if (cur_it.dist > pdist[cur_it.node]) continue;     /* stale */
        if (cur_it.node == b) break;
        for (uint32_t e = off[cur_it.node]; e < off[cur_it.node + 1]; e++) {
            uint32_t to = adj_to[e];
            float nd = cur_it.dist + adj_w[e];
            if (nd < pdist[to]) {
                pdist[to] = nd;
                prev[to] = cur_it.node;
                if (!heap_push(&heap, nd, to)) goto oom;
            }
        }
    }

    if (isinf(pdist[b])) goto unreachable;

    /* walk back from b to a to get the length, then fill a..b */
    uint32_t len = 1;
    for (uint32_t v = b; v != a; v = prev[v]) len++;
    uint32_t* o = malloc((size_t)len * sizeof *o);
    if (!o) goto oom;
    uint32_t v = b;
    for (uint32_t i = len; i-- > 0; v = prev[v]) o[i] = v;

    if (dist) {
        float* d = malloc((size_t)len * sizeof *d);
        if (!d) { free(o); goto oom; }
        for (uint32_t i = 0; i < len; i++) d[i] = pdist[o[i]];
        *dist = d;
    }
    *out = o; *n = len;

    free(off); free(cur); free(adj_to); free(adj_w); free(pdist); free(prev); cv_free_vec(heap);
    return true;

unreachable:
    free(off); free(cur); free(adj_to); free(adj_w); free(pdist); free(prev); cv_free_vec(heap);
    return false;

oom:
    free(off); free(cur); free(adj_to); free(adj_w); free(pdist); free(prev); cv_free_vec(heap);
    return false;
}
