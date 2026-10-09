/* mesh.c -- groups, skin extraction, edges, points, picking. */
#include "mesh.h"
#include <math.h>

/* ---- face tables (CalculiX corner numbering, 0-based) ------------------------ */

typedef struct { int n; int8_t v[4]; } face_t;

static const face_t kHexFaces[6] = {
    {4, {0, 1, 2, 3}}, {4, {4, 7, 6, 5}}, {4, {0, 4, 5, 1}},
    {4, {1, 5, 6, 2}}, {4, {2, 6, 7, 3}}, {4, {3, 7, 4, 0}},
};
static const face_t kWedgeFaces[5] = {
    {3, {0, 1, 2}}, {3, {3, 4, 5}}, {4, {0, 1, 4, 3}}, {4, {1, 2, 5, 4}}, {4, {2, 0, 3, 5}},
};
static const face_t kTetFaces[4] = {
    {3, {0, 1, 2}}, {3, {0, 3, 1}}, {3, {1, 3, 2}}, {3, {2, 3, 0}},
};
static const face_t kTriFace[1]  = { {3, {0, 1, 2}} };
static const face_t kQuadFace[1] = { {4, {0, 1, 2, 3}} };

/* Solid faces take part in the interior/exterior matching; shell faces are
   always exterior. Beams have no faces. */
static int faces_of(int t, const face_t** out, bool* solid) {
    *solid = true;
    switch (t) {
        case 1: case 4:  *out = kHexFaces;   return 6;
        case 2: case 5:  *out = kWedgeFaces; return 5;
        case 3: case 6:  *out = kTetFaces;   return 4;
        case 7: case 8:  *out = kTriFace;    *solid = false; return 1;
        case 9: case 10: *out = kQuadFace;   *solid = false; return 1;
        default: *out = NULL; return 0;
    }
}

int cv_elem_face_corners(const cv_frd* f, uint32_t e, int face, uint32_t out[4]) {
    const face_t* ft; bool solid;
    int nf = faces_of(f->etype[e], &ft, &solid);
    if (!solid && nf > 0) face = 0;                 /* S1/S2/SPOS/SNEG of a shell */
    if (face < 0 || face >= nf) return 0;
    const uint32_t* cn = f->conn + f->eoff[e];
    for (int i = 0; i < ft[face].n; i++) out[i] = cn[ft[face].v[i]];
    return ft[face].n;
}

static bool face_mids(const cv_frd* f, uint32_t e, const face_t* fc, uint32_t m[4]);

int cv_elem_nfaces(const cv_frd* f, uint32_t e) {
    const face_t* ft; bool solid;
    return faces_of(f->etype[e], &ft, &solid);
}

int cv_elem_face_nodes(const cv_frd* f, uint32_t e, int face, uint32_t out[8]) {
    const face_t* ft; bool solid;
    int nf = faces_of(f->etype[e], &ft, &solid);
    if (!solid && nf > 0) face = 0;
    if (face < 0 || face >= nf) return 0;
    int n = cv_elem_face_corners(f, e, face, out);
    uint32_t m[4];
    if (face_mids(f, e, &ft[face], m)) for (int i = 0; i < n; i++) out[n + i] = m[i];
    else return n;
    return 2 * n;
}

/* ---- groups ------------------------------------------------------------------ */

const char* cv_axis_name(int a) {
    static const char* n[] = { "Element type", "Material", "Group" };
    return (a >= 0 && a < CV_AXIS_N) ? n[a] : "?";
}

/* Distinct values of raw[0..n) via a small hash, then sorted. */
static bool axis_build(cv_axis* ax, const uint32_t* raw, const uint8_t* raw8, uint32_t n) {
    enum { MAXV = 65535 };
    uint32_t hcap = 256, nv = 0;
    uint32_t* hk = malloc(hcap * sizeof *hk);
    uint32_t* hv = malloc(hcap * sizeof *hv);
    uint32_t* vals = malloc(MAXV * sizeof *vals);
    ax->of_elem = malloc((size_t)CV_MAX(n, 1) * sizeof *ax->of_elem);
    if (!hk || !hv || !vals || !ax->of_elem) goto fail;
    memset(hk, 0xFF, hcap * sizeof *hk);

    uint32_t last_v = UINT32_MAX, last_i = 0;
    for (uint32_t e = 0; e < n; e++) {
        uint32_t v = raw ? raw[e] : raw8[e];
        if (v == last_v) { ax->of_elem[e] = (uint16_t)last_i; continue; }
        uint32_t h = (v * 2654435761u) & (hcap - 1);
        while (hk[h] != UINT32_MAX && hk[h] != v) h = (h + 1) & (hcap - 1);
        if (hk[h] == UINT32_MAX) {
            if (nv == MAXV) { h = UINT32_MAX; }       /* overflow: lump into the last value */
            else {
                hk[h] = v; hv[h] = nv; vals[nv++] = v;
                if (nv * 2 > hcap) {                    /* rehash */
                    uint32_t nc = hcap * 2;
                    uint32_t* k2 = malloc(nc * sizeof *k2);
                    uint32_t* v2 = malloc(nc * sizeof *v2);
                    if (!k2 || !v2) { free(k2); free(v2); goto fail; }
                    memset(k2, 0xFF, nc * sizeof *k2);
                    for (uint32_t i = 0; i < hcap; i++) if (hk[i] != UINT32_MAX) {
                        uint32_t j = (hk[i] * 2654435761u) & (nc - 1);
                        while (k2[j] != UINT32_MAX) j = (j + 1) & (nc - 1);
                        k2[j] = hk[i]; v2[j] = hv[i];
                    }
                    free(hk); free(hv); hk = k2; hv = v2; hcap = nc;
                    h = (v * 2654435761u) & (hcap - 1);
                    while (hk[h] != v) h = (h + 1) & (hcap - 1);
                }
            }
        }
        last_v = v;
        last_i = h == UINT32_MAX ? nv - 1 : hv[h];
        ax->of_elem[e] = (uint16_t)last_i;
    }

    /* sort values, remap indices */
    uint32_t* sorted = malloc((size_t)CV_MAX(nv, 1) * sizeof *sorted);
    uint16_t* remap = malloc((size_t)CV_MAX(nv, 1) * sizeof *remap);
    ax->value = malloc((size_t)CV_MAX(nv, 1) * sizeof *ax->value);
    ax->count = calloc(CV_MAX(nv, 1), sizeof *ax->count);
    ax->on = malloc((size_t)CV_MAX(nv, 1) * sizeof *ax->on);
    if (!sorted || !remap || !ax->value || !ax->count || !ax->on) {
        free(sorted); free(remap); goto fail;
    }
    memcpy(sorted, vals, nv * sizeof *sorted);
    qsort(sorted, nv, sizeof *sorted, cv_cmp_u32);
    for (uint32_t i = 0; i < nv; i++) {
        const uint32_t* p = bsearch(&vals[i], sorted, nv, sizeof *sorted, cv_cmp_u32);
        remap[i] = (uint16_t)(p - sorted);
    }
    for (uint32_t e = 0; e < n; e++) {
        ax->of_elem[e] = remap[ax->of_elem[e]];
        CV_ASSERT(ax->of_elem[e] < nv);
        ax->count[ax->of_elem[e]]++;
    }
    memcpy(ax->value, sorted, nv * sizeof *sorted);
    for (uint32_t i = 0; i < nv; i++) ax->on[i] = true;
    ax->n = (int)nv;
    free(sorted); free(remap); free(hk); free(hv); free(vals);
    return true;
fail:
    free(hk); free(hv); free(vals);
    return false;
}

bool cv_groups_build(cv_groups* g, const cv_frd* f) {
    memset(g, 0, sizeof *g);
    if (axis_build(&g->axis[CV_AXIS_TYPE], NULL, f->etype, f->n_elems) &&
        axis_build(&g->axis[CV_AXIS_MAT], f->emat, NULL, f->n_elems) &&
        axis_build(&g->axis[CV_AXIS_GRP], f->egrp, NULL, f->n_elems))
        return true;
    cv_groups_free(g);
    return false;
}

void cv_groups_free(cv_groups* g) {
    for (int a = 0; a < CV_AXIS_N; a++) {
        free(g->axis[a].value); free(g->axis[a].count);
        free(g->axis[a].on);    free(g->axis[a].of_elem);
    }
    memset(g, 0, sizeof *g);
}

void cv_groups_mask(const cv_groups* g, uint32_t n, uint8_t* vis) {
    const cv_axis *t = &g->axis[0], *m = &g->axis[1], *r = &g->axis[2];
    for (uint32_t e = 0; e < n; e++)
        vis[e] = t->on[t->of_elem[e]] && m->on[m->of_elem[e]] && r->on[r->of_elem[e]];
}

void cv_crop_mask(const cv_frd* f, const float lo[3], const float hi[3], uint8_t* vis) {
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (!vis[e]) continue;
        uint32_t b = f->eoff[e], n = f->eoff[e + 1] - b;
        float c[3] = { 0, 0, 0 };
        for (uint32_t j = 0; j < n; j++) {
            const float* p = f->xyz + 3 * f->conn[b + j];
            c[0] += p[0]; c[1] += p[1]; c[2] += p[2];
        }
        for (int k = 0; k < 3; k++) {
            float v = n ? c[k] / (float)n : 0.f;
            if (v < lo[k] || v > hi[k]) { vis[e] = 0; break; }
        }
    }
}

void cv_plane_nodes(const cv_frd* f, const float* disp, float f1, const float* disp2, float f2,
                    const float n[3], float d, uint8_t* beyond) {
    for (uint32_t i = 0; i < f->n_nodes; i++) {
        float s = 0;
        for (int k = 0; k < 3; k++) {
            float p = f->xyz[3 * i + k];
            if (disp) p += f1 * disp[3 * i + k];
            if (disp2) p += f2 * disp2[3 * i + k];
            s += n[k] * p;
        }
        beyond[i] = s > d;
    }
}

void cv_node_mask(const cv_frd* f, const uint8_t* beyond, uint8_t* vis) {
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (!vis[e]) continue;
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++)
            if (beyond[f->conn[j]]) { vis[e] = 0; break; }
    }
}

uint32_t cv_box_elems(const cv_frd* f, const uint8_t* vis, const uint8_t* inside, bool crossing, uint32_t* out) {
    uint32_t n = 0;
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (vis && !vis[e]) continue;
        uint32_t b = f->eoff[e], m = f->eoff[e + 1] - b, in = 0;
        for (uint32_t j = 0; j < m; j++) in += inside[f->conn[b + j]] != 0;
        if (m && (crossing ? in > 0 : in == m)) out[n++] = e;
    }
    return n;
}

/* ---- skin -------------------------------------------------------------------- */

typedef struct { uint32_t k[4]; uint32_t code; } fkey;   /* sorted corners, e*8+face */

static void face_key(const cv_frd* f, uint32_t code, fkey* out) {
    uint32_t e = code >> 3, fi = code & 7;
    const face_t* ft; bool solid;
    faces_of(f->etype[e], &ft, &solid);
    const uint32_t* cn = f->conn + f->eoff[e];
    const face_t* fc = &ft[fi];
    uint32_t k[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    for (int i = 0; i < fc->n; i++) k[i] = cn[fc->v[i]];
    /* sort 4 */
    for (int i = 1; i < 4; i++) {
        uint32_t x = k[i]; int j = i - 1;
        while (j >= 0 && k[j] > x) { k[j + 1] = k[j]; j--; }
        k[j + 1] = x;
    }
    memcpy(out->k, k, sizeof k);
    out->code = code;
}

static inline uint32_t face_min(const cv_frd* f, uint32_t e, const face_t* fc) {
    const uint32_t* cn = f->conn + f->eoff[e];
    uint32_t m = cn[fc->v[0]];
    for (int i = 1; i < fc->n; i++) m = CV_MIN(m, cn[fc->v[i]]);
    return m;
}

typedef struct {
    CV_VEC(uint32_t) tri;
    CV_VEC(uint32_t) tri_elem;
    CV_VEC(uint64_t) edges;      /* collected, deduplicated at the end */
    CV_VEC(uint32_t) faces;      /* element << 3 | local face */
} emit_t;

/* The mid-side node of a quadratic element between its corners a and b (local,
   0-based, in the .frd order of the connectivity: for a hex and a wedge the mid nodes
   of the bottom face, then of the upright edges, then of the top face), -1 for a
   linear element. */
static int mid_of(int type, int a, int b) {
    static const int8_t hex[12][3] = { {0,1,8}, {1,2,9}, {2,3,10}, {3,0,11}, {0,4,12}, {1,5,13}, {2,6,14}, {3,7,15},
                                       {4,5,16}, {5,6,17}, {6,7,18}, {7,4,19} };
    static const int8_t wed[9][3] = { {0,1,6}, {1,2,7}, {2,0,8}, {0,3,9}, {1,4,10}, {2,5,11}, {3,4,12}, {4,5,13}, {5,3,14} };
    static const int8_t tet[6][3] = { {0,1,4}, {1,2,5}, {2,0,6}, {0,3,7}, {1,3,8}, {2,3,9} };
    static const int8_t tri[3][3] = { {0,1,3}, {1,2,4}, {2,0,5} };
    static const int8_t qua[4][3] = { {0,1,4}, {1,2,5}, {2,3,6}, {3,0,7} };
    const int8_t (*t)[3]; int n;
    switch (type) {
        case 4:  t = hex; n = 12; break;
        case 5:  t = wed; n = 9; break;
        case 6:  t = tet; n = 6; break;
        case 8:  t = tri; n = 3; break;
        case 10: t = qua; n = 4; break;
        default: return -1;
    }
    for (int i = 0; i < n; i++) if ((t[i][0] == a && t[i][1] == b) || (t[i][0] == b && t[i][1] == a)) return t[i][2];
    return -1;
}

/* the mid-side nodes of a face of element e (dense indices), false for a linear element */
static bool face_mids(const cv_frd* f, uint32_t e, const face_t* fc, uint32_t m[4]) {
    const uint32_t* cn = f->conn + f->eoff[e];
    uint32_t nn = f->eoff[e + 1] - f->eoff[e];
    for (int i = 0; i < fc->n; i++) {
        int k = mid_of(f->etype[e], fc->v[i], fc->v[(i + 1) % fc->n]);
        if (k < 0 || (uint32_t)k >= nn) return false;
        m[i] = cn[k];
    }
    return true;
}

static void put_tri(emit_t* o, uint32_t e, uint32_t a, uint32_t b, uint32_t c) {
    o->tri.a[o->tri.n++] = a; o->tri.a[o->tri.n++] = b; o->tri.a[o->tri.n++] = c;
    o->tri_elem.a[o->tri_elem.n++] = e;
}
static void put_edge(emit_t* o, uint32_t a, uint32_t b) {
    if (a > b) { uint32_t t = a; a = b; b = t; }
    o->edges.a[o->edges.n++] = ((uint64_t)a << 32) | b;
}

/* A face as triangles and its sides as edges. With mid (and a quadratic element)
   through its mid-side nodes: a corner triangle at every corner and the middle,
   so the colours and the shape follow every node of the face; else corners only. */
static bool emit_face(emit_t* o, const cv_frd* f, uint32_t e, const face_t* fc, int local, bool mid) {
    if (!cv_push(o->faces, (e << 3) | (uint32_t)local)) return false;
    const uint32_t* cn = f->conn + f->eoff[e];
    uint32_t v[4], m[4];
    int n = fc->n;
    for (int i = 0; i < n; i++) v[i] = cn[fc->v[i]];
    if (!cv_reserve(o->tri, o->tri.n + 18) || !cv_reserve(o->tri_elem, o->tri_elem.n + 6) ||
        !cv_reserve(o->edges, o->edges.n + 8))
        return false;
    if (mid && face_mids(f, e, fc, m)) {
        for (int i = 0; i < n; i++) {
            put_tri(o, e, v[i], m[i], m[(i + n - 1) % n]);
            put_edge(o, v[i], m[i]); put_edge(o, m[i], v[(i + 1) % n]);
        }
        put_tri(o, e, m[0], m[1], m[2]);
        if (n == 4) put_tri(o, e, m[0], m[2], m[3]);
        return true;
    }
    put_tri(o, e, v[0], v[1], v[2]);
    if (n == 4) put_tri(o, e, v[0], v[2], v[3]);
    for (int i = 0; i < n; i++) put_edge(o, v[i], v[(i + 1) % n]);
    return true;
}

static int cmp_u64(const void* a, const void* b) {
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

/* LSD radix sort on 64-bit keys, 16 bits per pass (4 passes), falls back to
   qsort if the scratch buffer cannot be allocated. */
static void sort_u64(uint64_t* a, size_t n) {
    if (n < 2) return;
    uint64_t* tmp = n > 4096 ? malloc(n * sizeof *tmp) : NULL;
    uint32_t* cnt = tmp ? malloc(65536 * sizeof *cnt) : NULL;
    if (!tmp || !cnt) { free(tmp); free(cnt); qsort(a, n, sizeof *a, cmp_u64); return; }
    uint64_t *src = a, *dst = tmp;
    for (int pass = 0; pass < 4; pass++) {
        int sh = pass * 16;
        memset(cnt, 0, 65536 * sizeof *cnt);
        for (size_t i = 0; i < n; i++) cnt[(src[i] >> sh) & 0xFFFF]++;
        uint32_t sum = 0;
        for (int i = 0; i < 65536; i++) { uint32_t c = cnt[i]; cnt[i] = sum; sum += c; }
        for (size_t i = 0; i < n; i++) dst[cnt[(src[i] >> sh) & 0xFFFF]++] = src[i];
        uint64_t* t = src; src = dst; dst = t;
    }
    /* 4 passes: result is back in `a` */
    free(tmp); free(cnt);
}

/* ---- feature edges ------------------------------------------------------------ */

/* Unit normal of a face polygon by Newell's sum, which a slightly warped quad
   does not upset; zero for a collapsed face. */
static void face_normal(const cv_frd* f, const uint32_t* v, int n, float out[3]) {
    double nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < n; i++) {
        const float* a = f->xyz + 3 * v[i];
        const float* b = f->xyz + 3 * v[(i + 1) % n];
        nx += ((double)a[1] - b[1]) * ((double)a[2] + b[2]);
        ny += ((double)a[2] - b[2]) * ((double)a[0] + b[0]);
        nz += ((double)a[0] - b[0]) * ((double)a[1] + b[1]);
    }
    double l = sqrt(nx * nx + ny * ny + nz * nz);
    if (!(l > 0)) { out[0] = out[1] = out[2] = 0; return; }
    out[0] = (float)(nx / l); out[1] = (float)(ny / l); out[2] = (float)(nz / l);
}

/* corners of skin face `code` and whether it is a shell face */
static int skin_face(const cv_frd* f, uint32_t code, uint32_t v[4], bool* shell) {
    const face_t* ft; bool solid;
    faces_of(f->etype[code >> 3], &ft, &solid);
    *shell = !solid;
    const face_t* fc = &ft[code & 7];
    const uint32_t* cn = f->conn + f->eoff[code >> 3];
    for (int i = 0; i < fc->n; i++) v[i] = cn[fc->v[i]];
    return fc->n;
}

typedef struct {
    uint64_t key;        /* a << 32 | b with a < b; 0 = empty (a == b is never stored) */
    uint32_t face;       /* index into the skin's face list of its first face; UINT32_MAX: a beam */
    uint32_t mid;        /* its mid-side node when the faces are drawn through them, else UINT32_MAX */
    uint8_t  count;      /* faces on it, saturating at 3 */
    uint8_t  feat;
} fe_slot;

/* The two faces' normals are compared only when the second one arrives, so a
   slot keeps the first face's index, not its normal: 16 bytes per edge. */
static bool skin_features(cv_skin* s, const cv_frd* f, const uint8_t* vis, float crease_deg, bool mid) {
    size_t cap = 16;
    while (cap < 2 * s->n_edge + 2) cap *= 2;
    fe_slot* h = calloc(cap, sizeof *h);
    if (!h) return false;
    const float cos_t = cosf(crease_deg * 3.14159265f / 180.f);
    size_t nfeat = 0;

    for (size_t fi = 0; fi < s->n_face; fi++) {
        uint32_t code = s->face[fi], e = code >> 3, v[4];
        bool shell;
        int n = skin_face(f, code, v, &shell);
        float nm[3];
        bool have_nm = false;
        uint32_t mids[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
        if (mid) {
            const face_t* ft; bool solid;
            faces_of(f->etype[e], &ft, &solid);
            if (!face_mids(f, e, &ft[code & 7], mids)) mids[0] = mids[1] = mids[2] = mids[3] = UINT32_MAX;
        }
        for (int i = 0; i < n; i++) {
            uint32_t a = v[i], b = v[(i + 1) % n];
            if (a == b) continue;
            if (a > b) { uint32_t t = a; a = b; b = t; }
            uint64_t key = ((uint64_t)a << 32) | b;
            size_t j = (size_t)((key * 0x9E3779B97F4A7C15ull) >> 20) & (cap - 1);
            while (h[j].key && h[j].key != key) j = (j + 1) & (cap - 1);
            fe_slot* sl = &h[j];
            if (!sl->key) { sl->key = key; sl->face = (uint32_t)fi; sl->count = 1; sl->mid = mids[i]; continue; }
            if (sl->count >= 2 || sl->face == UINT32_MAX) { sl->count = 3; sl->feat = 1; continue; }
            sl->count = 2;
            uint32_t code0 = s->face[sl->face], e0 = code0 >> 3, v0[4];
            bool shell0;
            int n0 = skin_face(f, code0, v0, &shell0);
            if (f->etype[e0] != f->etype[e] || (f->emat && f->emat[e0] != f->emat[e])) { sl->feat = 1; continue; }
            if (!have_nm) { face_normal(f, v, n, nm); have_nm = true; }
            float n0m[3];
            face_normal(f, v0, n0, n0m);
            float d = nm[0] * n0m[0] + nm[1] * n0m[1] + nm[2] * n0m[2];
            if (shell || shell0) d = fabsf(d);
            bool degenerate = (nm[0] == 0 && nm[1] == 0 && nm[2] == 0) || (n0m[0] == 0 && n0m[1] == 0 && n0m[2] == 0);
            if (!degenerate && d < cos_t) sl->feat = 1;
        }
    }
    for (uint32_t e = 0; e < f->n_elems; e++) {        /* a beam is its own outline */
        if (vis && !vis[e]) continue;
        int t = f->etype[e];
        if (t != 11 && t != 12) continue;
        const uint32_t* cn = f->conn + f->eoff[e];
        for (int i = 0; i < (t == 11 ? 1 : 2); i++) {
            uint32_t a = cn[i], b = cn[i + 1];
            if (a == b) continue;
            if (a > b) { uint32_t tt = a; a = b; b = tt; }
            uint64_t key = ((uint64_t)a << 32) | b;
            size_t j = (size_t)((key * 0x9E3779B97F4A7C15ull) >> 20) & (cap - 1);
            while (h[j].key && h[j].key != key) j = (j + 1) & (cap - 1);
            if (!h[j].key) { h[j].key = key; h[j].face = UINT32_MAX; h[j].count = 1; h[j].mid = UINT32_MAX; }
            h[j].feat = 1;
        }
    }
    for (size_t j = 0; j < cap; j++)                /* an edge with a mid node: two pieces */
        if (h[j].key && (h[j].feat || h[j].count == 1)) nfeat += h[j].mid != UINT32_MAX ? 2 : 1;
    s->fedge = malloc(CV_MAX(nfeat, 1) * 2 * sizeof *s->fedge);
    if (!s->fedge) { free(h); return false; }
    size_t k = 0;
    for (size_t j = 0; j < cap; j++)
        if (h[j].key && (h[j].feat || h[j].count == 1)) {
            uint32_t a = (uint32_t)(h[j].key >> 32), b = (uint32_t)h[j].key;
            if (h[j].mid != UINT32_MAX) { s->fedge[k++] = a; s->fedge[k++] = h[j].mid; a = h[j].mid; }
            s->fedge[k++] = a;
            s->fedge[k++] = b;
        }
    s->n_fedge = nfeat;
    free(h);
    return true;
}

bool cv_skin_build(cv_skin* s, const cv_frd* f, const uint8_t* vis) {
    return cv_skin_build_opt(s, f, vis, CV_CREASE_DEG, true);
}
bool cv_skin_build_crease(cv_skin* s, const cv_frd* f, const uint8_t* vis, float crease_deg) {
    return cv_skin_build_opt(s, f, vis, crease_deg, true);
}

bool cv_skin_build_opt(cv_skin* s, const cv_frd* f, const uint8_t* vis, float crease_deg, bool mid) {
    memset(s, 0, sizeof *s);
    const uint32_t N = f->n_nodes, E = f->n_elems;
    emit_t o = {0};
    uint32_t* off = calloc((size_t)N + 1, sizeof *off);
    uint32_t* faces = NULL;
    uint8_t*  used = calloc(CV_MAX(N, 1), 1);
    if (!off || !used) goto oom;

    /* 1. count solid faces per smallest corner node; shells go straight out */
    for (uint32_t e = 0; e < E; e++) {
        if (vis && !vis[e]) continue;
        const face_t* ft; bool solid;
        int nf = faces_of(f->etype[e], &ft, &solid);
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) used[f->conn[j]] = 1;
        if (!solid) {
            for (int i = 0; i < nf; i++) if (!emit_face(&o, f, e, &ft[i], i, mid)) goto oom;
            continue;
        }
        for (int i = 0; i < nf; i++) off[face_min(f, e, &ft[i]) + 1]++;
    }
    for (uint32_t i = 0; i < N; i++) off[i + 1] += off[i];
    size_t nfaces = off[N];

    /* 2. bucket the face codes */
    faces = malloc(CV_MAX(nfaces, 1) * sizeof *faces);
    if (!faces) goto oom;
    {
        uint32_t* fill = malloc(((size_t)N + 1) * sizeof *fill);
        if (!fill) goto oom;
        memcpy(fill, off, ((size_t)N + 1) * sizeof *fill);
        for (uint32_t e = 0; e < E; e++) {
            if (vis && !vis[e]) continue;
            const face_t* ft; bool solid;
            int nf = faces_of(f->etype[e], &ft, &solid);
            if (!solid) continue;
            for (int i = 0; i < nf; i++) faces[fill[face_min(f, e, &ft[i])]++] = (e << 3) | (uint32_t)i;
        }
        free(fill);
    }

    /* 3. match faces inside each bucket; unmatched ones are exterior */
    {
        CV_VEC(fkey) keys = {0};
        CV_VEC(uint8_t) matched = {0};
        for (uint32_t nd = 0; nd < N; nd++) {
            uint32_t b = off[nd], n = off[nd + 1] - b;
            if (n == 0) continue;
            if (!cv_reserve(keys, n) || !cv_reserve(matched, n)) {
                cv_free_vec(keys); cv_free_vec(matched); goto oom;
            }
            for (uint32_t i = 0; i < n; i++) { face_key(f, faces[b + i], &keys.a[i]); matched.a[i] = 0; }
            for (uint32_t i = 0; i < n; i++) {
                if (matched.a[i]) continue;
                for (uint32_t j = i + 1; j < n; j++) {
                    if (!matched.a[j] && memcmp(keys.a[i].k, keys.a[j].k, sizeof keys.a[i].k) == 0) {
                        matched.a[i] = matched.a[j] = 1;
                        break;
                    }
                }
                if (!matched.a[i]) {
                    uint32_t code = keys.a[i].code, e = code >> 3;
                    const face_t* ft; bool solid;
                    faces_of(f->etype[e], &ft, &solid);
                    if (!emit_face(&o, f, e, &ft[code & 7], (int)(code & 7), mid)) {
                        cv_free_vec(keys); cv_free_vec(matched); goto oom;
                    }
                }
            }
        }
        cv_free_vec(keys);
        cv_free_vec(matched);
    }

    /* 4. beams contribute edges only */
    for (uint32_t e = 0; e < E; e++) {
        if (vis && !vis[e]) continue;
        int t = f->etype[e];
        if (t != 11 && t != 12) continue;
        const uint32_t* cn = f->conn + f->eoff[e];
        int segs = t == 11 ? 1 : 2;
        for (int i = 0; i < segs; i++) {
            uint32_t a = cn[i], bb = cn[i + 1];
            if (a > bb) { uint32_t tt = a; a = bb; bb = tt; }
            if (!cv_push(o.edges, ((uint64_t)a << 32) | bb)) goto oom;
        }
    }

    /* 5. unique edges */
    sort_u64(o.edges.a, o.edges.n);
    size_t ne = 0;
    for (size_t i = 0; i < o.edges.n; i++)
        if (i == 0 || o.edges.a[i] != o.edges.a[i - 1]) o.edges.a[ne++] = o.edges.a[i];
    s->edge = malloc(CV_MAX(ne, 1) * 2 * sizeof *s->edge);
    if (!s->edge) goto oom;
    for (size_t i = 0; i < ne; i++) {
        s->edge[2 * i] = (uint32_t)(o.edges.a[i] >> 32);
        s->edge[2 * i + 1] = (uint32_t)o.edges.a[i];
    }
    s->n_edge = ne;

    /* 6. points: nodes of visible elements -- or every node when the model has
       no elements at all (a point cloud, a deck of springs written as nodes) */
    if (E == 0) for (uint32_t i = 0; i < N; i++) used[i] = 1;
    size_t np = 0;
    for (uint32_t i = 0; i < N; i++) np += used[i];
    s->pt = malloc(CV_MAX(np, 1) * sizeof *s->pt);
    if (!s->pt) goto oom;
    for (uint32_t i = 0, k = 0; i < N; i++) if (used[i]) s->pt[k++] = i;
    s->n_pt = np;

    for (size_t i = 0; i < o.tri.n; i++) CV_ASSERT(o.tri.a[i] < N);
    s->tri = o.tri.a;           s->n_tri = o.tri.n / 3;
    s->tri_elem = o.tri_elem.a;
    s->face = o.faces.a;        s->n_face = o.faces.n;
    cv_free_vec(o.edges);
    free(off); free(faces); free(used);
    if (!skin_features(s, f, vis, crease_deg, mid)) { cv_skin_free(s); return false; }
    return true;

oom:
    cv_free_vec(o.tri); cv_free_vec(o.tri_elem); cv_free_vec(o.edges); cv_free_vec(o.faces);
    free(off); free(faces); free(used);
    cv_skin_free(s);
    return false;
}

void cv_skin_free(cv_skin* s) {
    free(s->tri); free(s->tri_elem); free(s->edge); free(s->pt); free(s->face); free(s->fedge);
    memset(s, 0, sizeof *s);
}

/* ---- picking ------------------------------------------------------------------ */

static inline void node_pos(const cv_frd* f, const float* disp, float sc, uint32_t i, float p[3]) {
    for (int k = 0; k < 3; k++) p[k] = f->xyz[3 * i + k] + (disp ? disp[3 * i + k] * sc : 0.f);
}

cv_pick cv_pick_ray(const cv_frd* f, const cv_skin* s, const float* disp, float sc,
                    const float o[3], const float d[3]) {
    cv_pick r = { false, 0, 0, INFINITY, UINT32_MAX };
    for (size_t t = 0; t < s->n_tri; t++) {
        float a[3], b[3], c[3];
        node_pos(f, disp, sc, s->tri[3 * t], a);
        node_pos(f, disp, sc, s->tri[3 * t + 1], b);
        node_pos(f, disp, sc, s->tri[3 * t + 2], c);
        /* Moller-Trumbore */
        float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        float p[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
        float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
        if (fabsf(det) < 1e-30f) continue;
        float inv = 1.f / det;
        float tv[3] = { o[0] - a[0], o[1] - a[1], o[2] - a[2] };
        float u = (tv[0] * p[0] + tv[1] * p[1] + tv[2] * p[2]) * inv;
        if (u < 0.f || u > 1.f) continue;
        float q[3] = { tv[1] * e1[2] - tv[2] * e1[1], tv[2] * e1[0] - tv[0] * e1[2], tv[0] * e1[1] - tv[1] * e1[0] };
        float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
        if (v < 0.f || u + v > 1.f) continue;
        float tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
        if (tt > 0.f && tt < r.t) { r.t = tt; r.hit = true; r.elem = s->tri_elem[t]; r.tri = (uint32_t)t; }
    }
    if (!r.hit) return r;
    float h[3] = { o[0] + d[0] * r.t, o[1] + d[1] * r.t, o[2] + d[2] * r.t };
    float best = INFINITY;
    for (uint32_t j = f->eoff[r.elem]; j < f->eoff[r.elem + 1]; j++) {
        float p[3];
        node_pos(f, disp, sc, f->conn[j], p);
        float dd = (p[0] - h[0]) * (p[0] - h[0]) + (p[1] - h[1]) * (p[1] - h[1]) + (p[2] - h[2]) * (p[2] - h[2]);
        if (dd < best) { best = dd; r.node = f->conn[j]; }
    }
    return r;
}
