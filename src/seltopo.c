/* seltopo.c -- selection by the mesh's shape; see seltopo.h. */
#include "seltopo.h"
#include "selset.h"
#include <math.h>

static bool shown(const uint8_t* vis, uint32_t e) { return !vis || vis[e]; }

/* ---- layers of neighbours ---------------------------------------------------------- */

uint32_t* cv_sel_grow_elems(const cv_frd* f, const uint8_t* vis, const uint32_t* el, uint32_t ne, uint32_t* n) {
    *n = 0;
    uint8_t* nm = calloc(CV_MAX(f->n_nodes, 1), 1);
    uint8_t* em = cv_sel_mask(el, ne, f->n_elems);
    if (!nm || !em) { free(nm); free(em); return NULL; }
    for (uint32_t k = 0; k < ne; k++)
        if (el[k] < f->n_elems) for (uint32_t j = f->eoff[el[k]]; j < f->eoff[el[k] + 1]; j++) nm[f->conn[j]] = 1;
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (em[e] || !shown(vis, e)) continue;
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) if (nm[f->conn[j]]) { em[e] = 1; break; }
    }
    uint32_t* r = cv_sel_from_mask(em, f->n_elems, n);
    free(nm); free(em);
    return r;
}

uint32_t* cv_sel_shrink_elems(const cv_frd* f, const uint8_t* vis, const uint32_t* el, uint32_t ne, uint32_t* n) {
    *n = 0;
    uint8_t* bad = calloc(CV_MAX(f->n_nodes, 1), 1);    /* nodes of shown elements outside el */
    uint8_t* em = cv_sel_mask(el, ne, f->n_elems);
    if (!bad || !em) { free(bad); free(em); return NULL; }
    for (uint32_t e = 0; e < f->n_elems; e++)
        if (!em[e] && shown(vis, e)) for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) bad[f->conn[j]] = 1;
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (!em[e]) continue;
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) if (bad[f->conn[j]]) { em[e] = 0; break; }
    }
    uint32_t* r = cv_sel_from_mask(em, f->n_elems, n);
    free(bad); free(em);
    return r;
}

uint32_t* cv_sel_grow_nodes(const cv_frd* f, const uint8_t* vis, const uint32_t* nd, uint32_t nn, uint32_t* n) {
    *n = 0;
    uint8_t* in = cv_sel_mask(nd, nn, f->n_nodes);
    uint8_t* out = cv_sel_mask(nd, nn, f->n_nodes);
    if (!in || !out) { free(in); free(out); return NULL; }
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (!shown(vis, e)) continue;
        bool any = false;
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1] && !any; j++) any = in[f->conn[j]];
        if (any) for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) out[f->conn[j]] = 1;
    }
    uint32_t* r = cv_sel_from_mask(out, f->n_nodes, n);
    free(in); free(out);
    return r;
}

uint32_t* cv_sel_shrink_nodes(const cv_frd* f, const uint8_t* vis, const uint32_t* nd, uint32_t nn, uint32_t* n) {
    *n = 0;
    uint8_t* in = cv_sel_mask(nd, nn, f->n_nodes);
    uint8_t* out = cv_sel_mask(nd, nn, f->n_nodes);
    if (!in || !out) { free(in); free(out); return NULL; }
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (!shown(vis, e)) continue;
        bool all = true;
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1] && all; j++) all = in[f->conn[j]];
        if (!all) for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) out[f->conn[j]] = 0;
    }
    uint32_t* r = cv_sel_from_mask(out, f->n_nodes, n);
    free(in); free(out);
    return r;
}

/* ---- the part: a flood through shared nodes ------------------------------------------- */

uint32_t* cv_sel_part(const cv_frd* f, const uint8_t* vis, uint32_t e0, uint32_t* n) {
    *n = 0;
    uint32_t N = f->n_nodes, E = f->n_elems;
    if (e0 >= E) return NULL;
    uint32_t* off = calloc((size_t)N + 1, sizeof *off);           /* node -> shown elements */
    uint8_t* em = calloc(CV_MAX(E, 1), 1);
    uint32_t* queue = malloc((size_t)CV_MAX(E, 1) * sizeof *queue);
    uint32_t* list = NULL;
    if (!off || !em || !queue) goto out;
    for (uint32_t e = 0; e < E; e++)
        if (shown(vis, e)) for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) off[f->conn[j] + 1]++;
    for (uint32_t i = 0; i < N; i++) off[i + 1] += off[i];
    list = malloc((size_t)CV_MAX(off[N], 1) * sizeof *list);
    uint32_t* fill = calloc(CV_MAX(N, 1), sizeof *fill);
    if (!list || !fill) { free(fill); goto out; }
    for (uint32_t e = 0; e < E; e++)
        if (shown(vis, e)) for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) { uint32_t i = f->conn[j]; list[off[i] + fill[i]++] = e; }
    free(fill);
    uint32_t qh = 0, qt = 0;
    em[e0] = 1; queue[qt++] = e0;
    while (qh < qt) {
        uint32_t e = queue[qh++];
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) {
            uint32_t i = f->conn[j];
            for (uint32_t k = off[i]; k < off[i + 1]; k++) if (!em[list[k]]) { em[list[k]] = 1; queue[qt++] = list[k]; }
        }
    }
    free(list); free(off); free(queue);
    uint32_t* r = cv_sel_from_mask(em, E, n);
    free(em);
    return r;
out:
    free(off); free(em); free(queue); free(list);
    return NULL;
}

/* ---- the boundary ------------------------------------------------------------------------ */

typedef struct { uint32_t c[4]; uint32_t e; int8_t face, edge; } facet;   /* edge -1: a whole face */

static int cmp_facet(const void* a, const void* b) {
    const facet* p = a; const facet* q = b;
    for (int i = 0; i < 4; i++) if (p->c[i] != q->c[i]) return p->c[i] < q->c[i] ? -1 : 1;
    return 0;
}

static void sort4(uint32_t* c, int m) {
    for (int i = m; i < 4; i++) c[i] = UINT32_MAX;
    for (int i = 1; i < 4; i++) for (int j = i; j > 0 && c[j - 1] > c[j]; j--) { uint32_t t = c[j]; c[j] = c[j - 1]; c[j - 1] = t; }
}

static bool is_shell(int t) { return t >= 7 && t <= 10; }

bool cv_sel_boundary(const cv_frd* f, const uint32_t* el, uint32_t ne,
                     uint32_t** nodes, uint32_t* nn, uint32_t** elems, uint32_t* nel) {
    *nodes = *elems = NULL; *nn = *nel = 0;
    facet* fa = malloc((size_t)CV_MAX(ne, 1) * 6 * sizeof *fa);
    uint8_t* nm = calloc(CV_MAX(f->n_nodes, 1), 1);
    uint8_t* em = calloc(CV_MAX(f->n_elems, 1), 1);
    if (!fa || !nm || !em) { free(fa); free(nm); free(em); return false; }
    size_t m = 0;
    for (uint32_t k = 0; k < ne; k++) {
        uint32_t e = el[k];
        if (e >= f->n_elems) continue;
        int nf = cv_elem_nfaces(f, e);
        if (is_shell(f->etype[e])) {                  /* a shell: its edges */
            uint32_t c[4];
            int nc = cv_elem_face_corners(f, e, 0, c);
            for (int j = 0; j < nc; j++) {
                facet* x = &fa[m++];
                x->c[0] = c[j]; x->c[1] = c[(j + 1) % nc]; sort4(x->c, 2);
                x->e = e; x->face = 0; x->edge = (int8_t)j;
            }
            continue;
        }
        for (int i = 0; i < nf; i++) {
            facet* x = &fa[m++];
            int nc = cv_elem_face_corners(f, e, i, x->c);
            sort4(x->c, nc);
            x->e = e; x->face = (int8_t)i; x->edge = -1;
        }
    }
    qsort(fa, m, sizeof *fa, cmp_facet);
    for (size_t i = 0; i < m;) {
        size_t j = i + 1;
        while (j < m && !cmp_facet(&fa[i], &fa[j])) j++;
        if (j == i + 1) {                             /* nobody else of el has it */
            uint32_t out[8];
            int no = cv_elem_face_nodes(f, fa[i].e, fa[i].face, out);
            if (fa[i].edge < 0) for (int q = 0; q < no; q++) nm[out[q]] = 1;
            else {
                int nc = no > 4 ? no / 2 : no, a = fa[i].edge;
                nm[out[a]] = nm[out[(a + 1) % nc]] = 1;
                if (no > nc) nm[out[nc + a]] = 1;
            }
            em[fa[i].e] = 1;
        }
        i = j;
    }
    free(fa);
    *nodes = cv_sel_from_mask(nm, f->n_nodes, nn);
    *elems = cv_sel_from_mask(em, f->n_elems, nel);
    free(nm); free(em);
    return true;
}

/* ---- skin faces up to the feature edges ---------------------------------------------- */

uint32_t cv_skin_face_of_tri(const cv_frd* f, const cv_skin* s, size_t t) {
    if (t >= s->n_tri) return UINT32_MAX;
    uint32_t e = s->tri_elem[t];
    const uint32_t* v = s->tri + 3 * t;
    for (size_t k = 0; k < s->n_face; k++) {
        if (s->face[k] >> 3 != e) continue;
        uint32_t out[8];
        int no = cv_elem_face_nodes(f, e, (int)(s->face[k] & 7), out), hit = 0;
        for (int a = 0; a < 3; a++) for (int q = 0; q < no; q++) if (out[q] == v[a]) { hit++; break; }
        if (hit == 3) return (uint32_t)k;
    }
    return UINT32_MAX;
}

static void newell(const cv_frd* f, const uint32_t* v, int n, double out[3]) {
    double x = 0, y = 0, z = 0;
    for (int i = 0; i < n; i++) {
        const float* a = f->xyz + 3 * v[i];
        const float* b = f->xyz + 3 * v[(i + 1) % n];
        x += ((double)a[1] - b[1]) * ((double)a[2] + b[2]);
        y += ((double)a[2] - b[2]) * ((double)a[0] + b[0]);
        z += ((double)a[0] - b[0]) * ((double)a[1] + b[1]);
    }
    double l = sqrt(x * x + y * y + z * z);
    if (!(l > 0)) l = 1;
    out[0] = x / l; out[1] = y / l; out[2] = z / l;
}

typedef struct { uint64_t key; uint32_t face; } fedge_ent;

static int cmp_ent(const void* a, const void* b) {
    uint64_t p = ((const fedge_ent*)a)->key, q = ((const fedge_ent*)b)->key;
    return p < q ? -1 : p > q;
}

uint32_t* cv_sel_face_flood(const cv_frd* f, const cv_skin* s, uint32_t k0, float crease_deg, uint32_t* n) {
    *n = 0;
    size_t F = s->n_face;
    if (k0 >= F) return NULL;
    fedge_ent* ent = malloc(CV_MAX(F, 1) * 4 * sizeof *ent);
    uint32_t* first = malloc((CV_MAX(F, 1) + 1) * sizeof *first);   /* face k's edges: ent[first[k] .. first[k+1]) */
    double* nrm = malloc(CV_MAX(F, 1) * 3 * sizeof *nrm);
    uint8_t* seen = calloc(CV_MAX(F, 1), 1);
    uint32_t* queue = malloc(CV_MAX(F, 1) * sizeof *queue);
    uint32_t* r = NULL;
    if (!ent || !first || !nrm || !seen || !queue) goto out;
    size_t m = 0;
    for (size_t k = 0; k < F; k++) {
        uint32_t e = s->face[k] >> 3, c[4];
        int nc = cv_elem_face_corners(f, e, (int)(s->face[k] & 7), c);
        newell(f, c, nc, nrm + 3 * k);
        first[k] = (uint32_t)m;
        for (int j = 0; j < nc; j++) {
            uint64_t a = c[j], b = c[(j + 1) % nc];
            ent[m].key = a < b ? a << 32 | b : b << 32 | a; ent[m].face = (uint32_t)k; m++;
        }
    }
    first[F] = (uint32_t)m;
    /* a sorted copy finds the faces on each edge; ent keeps each face's own edges */
    {
        fedge_ent* srt = malloc(CV_MAX(m, 1) * sizeof *srt);
        if (!srt) goto out;
        memcpy(srt, ent, m * sizeof *srt);
        qsort(srt, m, sizeof *srt, cmp_ent);
        double cmin = cos(crease_deg * 3.14159265358979 / 180.0);
        uint32_t qh = 0, qt = 0;
        seen[k0] = 1; queue[qt++] = k0;
        while (qh < qt) {
            uint32_t k = queue[qh++], ek = s->face[k] >> 3;
            for (uint32_t p = first[k]; p < first[k + 1]; p++) {
                uint64_t kp = ent[p].key;
                fedge_ent want = { kp, 0 };
                fedge_ent* hit = bsearch(&want, srt, m, sizeof *srt, cmp_ent);
                if (!hit) continue;
                size_t lo = (size_t)(hit - srt), hi = lo;
                while (lo > 0 && srt[lo - 1].key == kp) lo--;
                while (hi + 1 < m && srt[hi + 1].key == kp) hi++;
                if (hi - lo != 1) continue;               /* an open edge, or three faces or more */
                uint32_t o = srt[lo].face == k ? srt[hi].face : srt[lo].face, eo = s->face[o] >> 3;
                if (seen[o] || f->etype[eo] != f->etype[ek] || (f->emat && f->emat[eo] != f->emat[ek])) continue;
                double d = nrm[3 * k] * nrm[3 * o] + nrm[3 * k + 1] * nrm[3 * o + 1] + nrm[3 * k + 2] * nrm[3 * o + 2];
                if (is_shell(f->etype[ek])) d = fabs(d);
                if (d < cmin) continue;                   /* a crease */
                seen[o] = 1; queue[qt++] = o;
            }
        }
        free(srt);
    }
    r = cv_sel_from_mask(seen, (uint32_t)F, n);
out:
    free(ent); free(first); free(nrm); free(seen); free(queue);
    return r;
}

/* ---- a chain of feature edges ------------------------------------------------------------ */

uint32_t* cv_sel_edge_chain(const cv_frd* f, const cv_skin* s, size_t k0, float crease_deg, uint32_t* n) {
    *n = 0;
    size_t M = s->n_fedge;
    uint32_t N = f->n_nodes;
    if (k0 >= M) return NULL;
    const uint32_t* fe = s->fedge;
    uint32_t* off = calloc((size_t)N + 1, sizeof *off);          /* node -> its feature edges */
    uint32_t* lst = malloc(CV_MAX(M, 1) * 2 * sizeof *lst);
    uint32_t* fill = calloc(CV_MAX(N, 1), sizeof *fill);
    uint8_t* used = calloc(CV_MAX(M, 1), 1);
    uint8_t* nm = calloc(CV_MAX(N, 1), 1);
    uint32_t* r = NULL;
    if (!off || !lst || !fill || !used || !nm) goto out;
    for (size_t i = 0; i < 2 * M; i++) off[fe[i] + 1]++;
    for (uint32_t i = 0; i < N; i++) off[i + 1] += off[i];
    for (size_t i = 0; i < 2 * M; i++) lst[off[fe[i]] + fill[fe[i]]++] = (uint32_t)(i / 2);
    double cmin = cos(crease_deg * 3.14159265358979 / 180.0);
    used[k0] = 1;
    for (int side = 0; side < 2; side++) {
        size_t cur = k0;
        uint32_t v = fe[2 * k0 + side], prev = fe[2 * k0 + 1 - side];
        while (off[v + 1] - off[v] == 2) {             /* on through a node two feature edges share */
            uint32_t nx = lst[off[v]] == cur ? lst[off[v] + 1] : lst[off[v]];
            if (used[nx]) break;                           /* round a loop */
            uint32_t w = fe[2 * nx] == v ? fe[2 * nx + 1] : fe[2 * nx];
            const float *a = f->xyz + 3 * prev, *b = f->xyz + 3 * v, *c = f->xyz + 3 * w;
            double d1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, d2[3] = { c[0] - b[0], c[1] - b[1], c[2] - b[2] };
            double l1 = sqrt(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2]), l2 = sqrt(d2[0] * d2[0] + d2[1] * d2[1] + d2[2] * d2[2]);
            if (l1 > 0 && l2 > 0 && (d1[0] * d2[0] + d1[1] * d2[1] + d1[2] * d2[2]) / (l1 * l2) < cmin) break;   /* a corner */
            used[nx] = 1;
            prev = v; v = w; cur = nx;
        }
    }
    for (size_t i = 0; i < M; i++) if (used[i]) nm[fe[2 * i]] = nm[fe[2 * i + 1]] = 1;
    r = cv_sel_from_mask(nm, N, n);
out:
    free(off); free(lst); free(fill); free(used); free(nm);
    return r;
}

bool cv_point_in_poly(float x, float y, const float* xy, int n) {
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}
