/* app_traj.c -- principal stress trajectories (traj.h) of the tensor field on
   screen: lines along the S1 and / or S3 direction through the shown solid
   elements, evenly spaced by an occupancy grid, seeded at element centres in a
   scrambled order. Each segment is a thin tube (CV_INST_TRAJ1 / TRAJ3) carrying
   the principal value for its colour and the displacement at both ends, so the
   lines follow the deformed shape and the animation. */
#include "app_int.h"
#include "traj.h"
#include <math.h>

#define TRAJ_MAX_LINES  4000
#define TRAJ_MAX_SEEDS  50000
#define TRAJ_MAX_POINTS 400000    /* over both families: bounds the work on huge models */

/* the sampler's state: the field, the element boxes and the last element hit */
typedef struct {
    const float* v;
    bool xz;
    const uint32_t* el;           /* box index -> element */
    const float *lo, *hi;
    const cv_bins* bins;
    uint32_t last;                /* box index, UINT32_MAX none */
} traj_ctx;

static bool in_elem(traj_ctx* c, uint32_t bi, const float p[3], float s[6], float d[6]) {
    const float* lo = c->lo + 3 * (size_t)bi, *hi = c->hi + 3 * (size_t)bi;
    if (p[0] < lo[0] || p[1] < lo[1] || p[2] < lo[2] || p[0] > hi[0] || p[1] > hi[1] || p[2] > hi[2]) return false;
    uint32_t e = c->el[bi];
    double pd[3] = { p[0], p[1], p[2] }, N[20];
    if (!elem_locate(e, pd, N)) return false;
    float w[20];
    for (int k = 0; k < 20; k++) w[k] = (float)N[k];
    if (!line_interp(e, w, c->v, 6, s)) return false;
    if (c->xz) { float t = s[4]; s[4] = s[5]; s[5] = t; }   /* xy xz yz -> XY YZ ZX */
    memset(d, 0, 6 * sizeof(float));
    if (G.disp && !line_interp(e, w, G.disp, 3, d)) memset(d, 0, 3 * sizeof(float));
    if (G.disp2 && !line_interp(e, w, G.disp2, 3, d + 3)) memset(d + 3, 0, 3 * sizeof(float));
    c->last = bi;
    return true;
}

static bool traj_sample(void* user, const float p[3], float s[6], float d[6]) {
    traj_ctx* c = user;
    if (c->last != UINT32_MAX && in_elem(c, c->last, p, s, d)) return true;
    uint32_t n;
    const uint32_t* it = cv_bins_at(c->bins, p, &n);
    for (uint32_t i = 0; i < n; i++)
        if (it[i] != c->last && in_elem(c, it[i], p, s, d)) return true;
    return false;
}

/* a stride coprime to n near the golden section: visits 0..n-1 once each, scattered */
static uint32_t scatter_stride(uint32_t n) {
    if (n < 3) return 1;
    uint32_t s = (uint32_t)(n * 0.618034) | 1u;
    for (;; s++) {
        uint32_t a = n, b = s;
        while (b) { uint32_t t = a % b; a = b; b = t; }
        if (a == 1) return s;
    }
}

void refresh_traj(void) {
    cv_render_inst(CV_INST_TRAJ1, NULL, 0);
    cv_render_inst(CV_INST_TRAJ3, NULL, 0);
    if (!G.show_traj || !G.has_field || !app_field_is_tensor()) return;
    int fi = find_field(G.step, G.field_name);
    const cv_field_desc* fd = &G.frd.steps[G.step].fields[fi];
    const float* v = cache_get(G.step, fi);
    float ref = app_tensor_ref();
    if (!v || fd->ncomp < 6 || !(ref > 0)) return;

    /* the shown solid elements: boxes, centres, the mean size, the model box */
    CV_VEC(uint32_t) el = {0};
    cv_fvec lo = {0}, hi = {0}, cen = {0};
    float mlo[3] = { INFINITY, INFINITY, INFINITY }, mhi[3] = { -INFINITY, -INFINITY, -INFINITY };
    double size_sum = 0;
    for (uint32_t e = 0; e < G.frd.n_elems; e++) {
        int t = G.frd.etype[e];
        uint32_t a = G.frd.eoff[e], b = G.frd.eoff[e + 1];
        if (t < 1 || t > 6 || b <= a || (G.vis && !G.vis[e])) continue;
        if (!cv_push(el, e) || !cv_reserve(lo, lo.n + 3) || !cv_reserve(hi, hi.n + 3) || !cv_reserve(cen, cen.n + 3)) break;
        float* l = lo.a + lo.n, *h = hi.a + hi.n, *c = cen.a + cen.n;
        for (int k = 0; k < 3; k++) { l[k] = INFINITY; h[k] = -INFINITY; c[k] = 0; }
        for (uint32_t j = a; j < b; j++) {
            const float* q = G.frd.xyz + 3 * (size_t)G.frd.conn[j];
            for (int k = 0; k < 3; k++) { l[k] = fminf(l[k], q[k]); h[k] = fmaxf(h[k], q[k]); c[k] += q[k]; }
        }
        float dx = h[0] - l[0], dy = h[1] - l[1], dz = h[2] - l[2];
        size_sum += sqrtf(dx * dx + dy * dy + dz * dz) * 0.57735f;
        for (int k = 0; k < 3; k++) {
            c[k] /= (float)(b - a);
            l[k] -= 1e-5f * G.diag; h[k] += 1e-5f * G.diag;       /* points on a face still find it */
            mlo[k] = fminf(mlo[k], l[k]); mhi[k] = fmaxf(mhi[k], h[k]);
        }
        lo.n += 3; hi.n += 3; cen.n += 3;
    }
    uint32_t ne = (uint32_t)el.n;
    float h_elem = ne ? (float)(size_sum / ne) : 0;
    cv_bins bins = {0};
    if (!(h_elem > 0) || !cv_bins_build(&bins, lo.a, hi.a, ne, 2 * h_elem, 128)) goto done;

    traj_ctx ctx = { v, cv_tensor_order(fd) == 2, el.a, lo.a, hi.a, &bins, UINT32_MAX };
    cv_traj_opts opt = { .h = 0.3f * h_elem, .min_val = 0.02f * ref, .max_turn = 0.6f };
    opt.max_steps = (int)CV_MIN(20000.f, 3 * G.diag / opt.h);
    float sep = CV_MAX(G.traj_spacing, 0.2f) * h_elem, r = 0.05f * h_elem;
    uint32_t stride = scatter_stride(ne);
    size_t budget = G.traj_which == 2 ? TRAJ_MAX_POINTS / 2 : TRAJ_MAX_POINTS;   /* per family */

    for (int fam = 0; fam < 2; fam++) {
        if (G.traj_which != 2 && G.traj_which != fam) continue;
        opt.which = fam ? 2 : 0;
        cv_occ occ;
        if (!cv_occ_init(&occ, mlo, mhi, sep, 96)) { cv_occ_free(&occ); continue; }
        cv_fvec pts = {0}, disp = {0}, val = {0}, in = {0};
        uint32_t lines = 0, tried = 0;
        size_t left = budget;
        for (uint32_t i = 0, s = 0; i < ne && lines < TRAJ_MAX_LINES && tried < TRAJ_MAX_SEEDS; i++) {
            s = (uint32_t)(((uint64_t)i * stride) % ne);
            const float* p = cen.a + 3 * (size_t)s;
            uint32_t* c = cv_occ_cell(&occ, p);
            if (!c || *c) continue;
            tried++;
            ctx.last = s;                                       /* the seed is in its element (mostly) */
            pts.n = disp.n = val.n = 0;
            size_t n = cv_traj_trace(p, &opt, traj_sample, &ctx, &occ, lines + 1, &pts, &disp, &val);
            if (n < 2) continue;
            lines++;
            for (size_t j = 0; j + 1 < n; j++)
                deck_inst2(&in, pts.a + 3 * j, pts.a + 3 * j + 3, r, r, 0.5f * (val.a[j] + val.a[j + 1]),
                           disp.a + 6 * j, disp.a + 6 * j + 6);
            if (n >= left) break;
            left -= n;
        }
        cv_render_inst(fam ? CV_INST_TRAJ3 : CV_INST_TRAJ1, in.a, (uint32_t)(in.n / CV_INST_FLOATS));
        cv_free_vec(pts); cv_free_vec(disp); cv_free_vec(val); cv_free_vec(in);
        cv_occ_free(&occ);
    }
done:
    cv_bins_free(&bins);
    cv_free_vec(el); cv_free_vec(lo); cv_free_vec(hi); cv_free_vec(cen);
}
