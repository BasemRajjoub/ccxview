/* traj.c -- principal stress trajectories, and the bin / occupancy grids they use. */
#include "traj.h"
#include "field.h"
#include <math.h>

/* ---- uniform grids ---------------------------------------------------------- */

/* a grid over lo..hi of cubic cells about `cell` wide, at most maxdim per axis */
static bool grid_fit(const float lo[3], const float hi[3], float cell, int maxdim,
                     float glo[3], float* gcell, int n[3]) {
    float ext = 0;
    for (int k = 0; k < 3; k++) {
        if (!(hi[k] >= lo[k])) return false;              /* also NaN */
        ext = fmaxf(ext, hi[k] - lo[k]);
    }
    if (maxdim < 1) maxdim = 1;
    if (!(cell > 0)) cell = ext > 0 ? ext / (float)maxdim : 1.f;
    cell = fmaxf(cell, ext / (float)maxdim * 1.0001f);    /* grow to fit maxdim */
    if (!(cell > 0)) cell = 1.f;                            /* a single point */
    for (int k = 0; k < 3; k++) {
        glo[k] = lo[k];
        n[k] = CV_MAX(1, CV_MIN(maxdim, (int)ceilf((hi[k] - lo[k]) / cell)));
    }
    *gcell = cell;
    return true;
}

/* the cell of p per axis; false outside (a point on the far face goes to the last cell) */
static bool grid_ijk(const float lo[3], float cell, const int n[3], const float p[3], int ijk[3]) {
    for (int k = 0; k < 3; k++) {
        float f = (p[k] - lo[k]) / cell;
        if (!(f >= 0) || f > (float)n[k]) return false;
        ijk[k] = CV_MIN((int)f, n[k] - 1);
    }
    return true;
}

static size_t grid_idx(const int n[3], int i, int j, int k) {
    return ((size_t)k * (size_t)n[1] + (size_t)j) * (size_t)n[0] + (size_t)i;
}

/* the cell range of a box, clamped to the grid */
static void box_range(const cv_bins* b, const float* lo, const float* hi, int a[3], int z[3]) {
    for (int k = 0; k < 3; k++) {
        a[k] = CV_MAX(0, CV_MIN(b->n[k] - 1, (int)floorf((lo[k] - b->lo[k]) / b->cell)));
        z[k] = CV_MAX(0, CV_MIN(b->n[k] - 1, (int)floorf((hi[k] - b->lo[k]) / b->cell)));
    }
}

bool cv_bins_build(cv_bins* b, const float* lo, const float* hi, uint32_t nbox, float cell, int maxdim) {
    memset(b, 0, sizeof *b);
    float blo[3] = { INFINITY, INFINITY, INFINITY }, bhi[3] = { -INFINITY, -INFINITY, -INFINITY };
    for (uint32_t i = 0; i < nbox; i++)
        for (int k = 0; k < 3; k++) { blo[k] = fminf(blo[k], lo[3 * i + k]); bhi[k] = fmaxf(bhi[k], hi[3 * i + k]); }
    if (!nbox || !grid_fit(blo, bhi, cell, maxdim, b->lo, &b->cell, b->n)) {
        b->n[0] = b->n[1] = b->n[2] = 0;                    /* empty: every lookup misses */
        return nbox == 0;
    }
    size_t nc = (size_t)b->n[0] * b->n[1] * b->n[2];
    b->start = calloc(nc + 1, sizeof *b->start);
    if (!b->start) return false;
    /* counting sort: count per cell, prefix sums, then fill */
    size_t total = 0;
    int a[3], z[3];
    for (uint32_t i = 0; i < nbox; i++) {
        box_range(b, lo + 3 * i, hi + 3 * i, a, z);
        for (int kk = a[2]; kk <= z[2]; kk++) for (int j = a[1]; j <= z[1]; j++) for (int ii = a[0]; ii <= z[0]; ii++)
            b->start[grid_idx(b->n, ii, j, kk) + 1]++;
        total += (size_t)(z[0] - a[0] + 1) * (z[1] - a[1] + 1) * (z[2] - a[2] + 1);
    }
    if (total > UINT32_MAX || !(b->items = malloc((total ? total : 1) * sizeof *b->items))) {
        cv_bins_free(b);
        return false;
    }
    for (size_t c = 0; c < nc; c++) b->start[c + 1] += b->start[c];
    for (uint32_t i = 0; i < nbox; i++) {                  /* start[c] as the cursor: ends at start[c+1] */
        box_range(b, lo + 3 * i, hi + 3 * i, a, z);
        for (int kk = a[2]; kk <= z[2]; kk++) for (int j = a[1]; j <= z[1]; j++) for (int ii = a[0]; ii <= z[0]; ii++)
            b->items[b->start[grid_idx(b->n, ii, j, kk)]++] = i;
    }
    memmove(b->start + 1, b->start, nc * sizeof *b->start);   /* shift the cursors back to the starts */
    b->start[0] = 0;
    return true;
}

const uint32_t* cv_bins_at(const cv_bins* b, const float p[3], uint32_t* n) {
    int c[3];
    *n = 0;
    if (!b->start || !grid_ijk(b->lo, b->cell, b->n, p, c)) return NULL;
    size_t i = grid_idx(b->n, c[0], c[1], c[2]);
    *n = b->start[i + 1] - b->start[i];
    return *n ? b->items + b->start[i] : NULL;
}

void cv_bins_free(cv_bins* b) {
    free(b->start); free(b->items);
    memset(b, 0, sizeof *b);
}

bool cv_occ_init(cv_occ* o, const float lo[3], const float hi[3], float cell, int maxdim) {
    memset(o, 0, sizeof *o);
    if (!grid_fit(lo, hi, cell, maxdim, o->lo, &o->cell, o->n)) return false;
    o->id = calloc((size_t)o->n[0] * o->n[1] * o->n[2], sizeof *o->id);
    return o->id != NULL;
}

uint32_t* cv_occ_cell(cv_occ* o, const float p[3]) {
    int c[3];
    if (!o->id || !grid_ijk(o->lo, o->cell, o->n, p, c)) return NULL;
    return o->id + grid_idx(o->n, c[0], c[1], c[2]);
}

void cv_occ_free(cv_occ* o) {
    free(o->id);
    memset(o, 0, sizeof *o);
}

/* ---- the integrator ------------------------------------------------------------ */

typedef struct {
    const cv_traj_opts* o;
    cv_traj_sample f;
    void* user;
} tracer;

/* the principal direction at p, flipped to agree with `ref` (NULL: as it comes) */
static bool dir_at(const tracer* t, const float p[3], const float* ref, float dir[3], float* val, float d[6]) {
    float s[6], v[3], vec[3][3];
    if (!t->f(t->user, p, s, d) || !cv_principal_dirs(s, false, v, vec)) return false;
    *val = v[t->o->which];
    if (!(fabsf(*val) >= t->o->min_val)) return false;
    float sg = ref && vec[t->o->which][0] * ref[0] + vec[t->o->which][1] * ref[1] + vec[t->o->which][2] * ref[2] < 0 ? -1.f : 1.f;
    for (int k = 0; k < 3; k++) dir[k] = sg * vec[t->o->which][k];
    return true;
}

static bool push_point(cv_fvec* pts, cv_fvec* disp, cv_fvec* val, const float p[3], const float d[6], float v) {
    if (!cv_reserve(*pts, pts->n + 3) || !cv_reserve(*disp, disp->n + 6) || !cv_push(*val, v)) return false;
    memcpy(pts->a + pts->n, p, 3 * sizeof(float)); pts->n += 3;
    memcpy(disp->a + disp->n, d, 6 * sizeof(float)); disp->n += 6;
    return true;
}

/* march from p along dir (the seed's direction, signed) until a stop; appends the points after p */
static size_t march(const tracer* t, const float p0[3], const float dir0[3], cv_occ* occ, uint32_t id,
                    cv_fvec* pts, cv_fvec* disp, cv_fvec* val) {
    const cv_traj_opts* o = t->o;
    float p[3] = { p0[0], p0[1], p0[2] }, v1[3] = { dir0[0], dir0[1], dir0[2] };
    float cos_max = cosf(o->max_turn), h = o->h;
    size_t n = 0;
    for (int it = 0; it < o->max_steps; it++) {
        float m[3], v2[3], v3[3], q[3], d[6], sv;
        for (int k = 0; k < 3; k++) m[k] = p[k] + 0.5f * h * v1[k];
        if (!dir_at(t, m, v1, v2, &sv, d)) break;            /* midpoint: the slope of the step */
        for (int k = 0; k < 3; k++) q[k] = p[k] + h * v2[k];
        if (!dir_at(t, q, v2, v3, &sv, d)) break;
        /* a sharp turn: near a degenerate point the direction is noise */
        if (v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2] < cos_max ||
            v1[0] * v3[0] + v1[1] * v3[1] + v1[2] * v3[2] < cos_max) break;
        if (occ) {
            uint32_t* c = cv_occ_cell(occ, q);
            if (!c || (*c && *c != id)) break;                 /* a foreign line's cell (its own is fine) */
            *c = id;
        }
        if (!push_point(pts, disp, val, q, d, sv)) break;
        n++;
        memcpy(p, q, sizeof p); memcpy(v1, v3, sizeof v1);
    }
    return n;
}

size_t cv_traj_trace(const float seed[3], const cv_traj_opts* o, cv_traj_sample f, void* user,
                     cv_occ* occ, uint32_t line_id, cv_fvec* pts, cv_fvec* disp, cv_fvec* val) {
    tracer t = { o, f, user };
    float dir[3], back[3], d0[6], v0;
    if (!dir_at(&t, seed, NULL, dir, &v0, d0)) return 0;
    uint32_t* c0 = occ ? cv_occ_cell(occ, seed) : NULL;
    if (occ && (!c0 || (*c0 && *c0 != line_id))) return 0;
    uint32_t c0_was = c0 ? *c0 : 0;
    if (c0) *c0 = line_id;
    size_t np = pts->n, nd = disp->n, nv = val->n;

    /* backward half, then reversed so the line runs end to end through the seed */
    for (int k = 0; k < 3; k++) back[k] = -dir[k];
    size_t nb = march(&t, seed, back, occ, line_id, pts, disp, val);
    for (size_t i = 0, j = nb - 1; nb && i < j; i++, j--) {
        float tmp[6];
        memcpy(tmp, pts->a + np + 3 * i, 3 * sizeof(float));
        memcpy(pts->a + np + 3 * i, pts->a + np + 3 * j, 3 * sizeof(float));
        memcpy(pts->a + np + 3 * j, tmp, 3 * sizeof(float));
        memcpy(tmp, disp->a + nd + 6 * i, 6 * sizeof(float));
        memcpy(disp->a + nd + 6 * i, disp->a + nd + 6 * j, 6 * sizeof(float));
        memcpy(disp->a + nd + 6 * j, tmp, 6 * sizeof(float));
        float tv = val->a[nv + i]; val->a[nv + i] = val->a[nv + j]; val->a[nv + j] = tv;
    }
    size_t n = nb;
    if (push_point(pts, disp, val, seed, d0, v0)) {
        n++;
        n += march(&t, seed, dir, occ, line_id, pts, disp, val);
    }
    if (n < 2) {                                             /* a dot: leave no trace */
        pts->n = np; disp->n = nd; val->n = nv;
        if (c0) *c0 = c0_was;
        return 0;
    }
    return n;
}
