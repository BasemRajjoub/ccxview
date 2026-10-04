/* t_traj.h -- principal stress trajectories (traj.c): the bin and occupancy
   grids, and lines traced through uniform fields in a box. Included by
   test_main.c after its CHECK macros. */
#include "../src/traj.h"

/* a uniform tensor in the box [0,10]^3, the displacement = the position */
typedef struct { float s[6]; } traj_field;

static bool traj_uniform(void* user, const float p[3], float s[6], float d[6]) {
    for (int k = 0; k < 3; k++) if (!(p[k] >= 0 && p[k] <= 10)) return false;
    memcpy(s, ((const traj_field*)user)->s, 6 * sizeof(float));
    for (int k = 0; k < 3; k++) { d[k] = p[k]; d[3 + k] = 0; }
    return true;
}

static void test_traj(void) {
    {   /* bins: three boxes, each point finds the one(s) holding it */
        float lo[9] = { 0, 0, 0,  5, 0, 0,  0, 5, 5 }, hi[9] = { 1, 1, 1,  6, 1, 1,  10, 10, 10 };
        cv_bins b;
        CHECK(cv_bins_build(&b, lo, hi, 3, 1.f, 128));
        uint32_t n;
        float p0[3] = { 0.5f, 0.5f, 0.5f }, p1[3] = { 5.5f, 0.5f, 0.5f }, p2[3] = { 9.9f, 9.9f, 9.9f };
        const uint32_t* it = cv_bins_at(&b, p0, &n);
        CHECK(n == 1 && it && it[0] == 0);
        it = cv_bins_at(&b, p1, &n);
        CHECK(n == 1 && it && it[0] == 1);
        it = cv_bins_at(&b, p2, &n);
        CHECK(n == 1 && it && it[0] == 2);
        float out[3] = { -1, 0, 0 }, empty[3] = { 3, 3, 0.5f };
        CHECK(cv_bins_at(&b, out, &n) == NULL && n == 0);
        CHECK(cv_bins_at(&b, empty, &n) == NULL && n == 0);
        cv_bins_free(&b);
        CHECK(cv_bins_build(&b, lo, hi, 3, 0.001f, 4));      /* the cell grows to fit 4 per axis */
        CHECK(b.n[0] <= 4 && b.cell >= 2.5f);
        it = cv_bins_at(&b, p1, &n);
        bool has1 = false;
        for (uint32_t i = 0; i < n; i++) has1 |= it[i] == 1;
        CHECK(has1);
        cv_bins_free(&b);
    }
    {   /* occupancy: distinct cells, NULL outside */
        cv_occ o;
        float lo[3] = { 0, 0, 0 }, hi[3] = { 10, 10, 10 };
        CHECK(cv_occ_init(&o, lo, hi, 1.f, 96));
        float a[3] = { 0.5f, 0.5f, 0.5f }, b[3] = { 1.5f, 0.5f, 0.5f }, c[3] = { 0.7f, 0.2f, 0.9f }, out[3] = { 11, 0, 0 };
        uint32_t* ca = cv_occ_cell(&o, a);
        CHECK(ca && *ca == 0);
        CHECK(cv_occ_cell(&o, b) != ca && cv_occ_cell(&o, c) == ca);
        CHECK(cv_occ_cell(&o, out) == NULL);
        cv_occ_free(&o);
    }
    cv_traj_opts opt = { .which = 0, .h = 0.5f, .min_val = 1.f, .max_turn = 0.6f, .max_steps = 1000 };
    float seed[3] = { 5, 5, 5 };
    cv_fvec pts = {0}, disp = {0}, val = {0};
    {   /* uniaxial along x: a straight line in x across the box */
        traj_field fl = { { 100, 0, 0, 0, 0, 0 } };
        size_t n = cv_traj_trace(seed, &opt, traj_uniform, &fl, NULL, 1, &pts, &disp, &val);
        CHECK(n >= 19 && pts.n == 3 * n && disp.n == 6 * n && val.n == n);
        float x0 = pts.a[0], x1 = pts.a[3 * (n - 1)];
        CHECK(fminf(x0, x1) < 0.6f && fmaxf(x0, x1) > 9.4f);
        bool ok = true;
        for (size_t i = 0; i < n; i++) {
            ok &= fabsf(pts.a[3 * i + 1] - 5) < 1e-4f && fabsf(pts.a[3 * i + 2] - 5) < 1e-4f;
            ok &= fabsf(val.a[i] - 100) < 1e-3f && fabsf(disp.a[6 * i] - pts.a[3 * i]) < 1e-5f;
            if (i) ok &= (pts.a[3 * i] - pts.a[3 * i - 3]) * (x1 - x0) > 0;   /* in order, end to end */
        }
        CHECK(ok);
        size_t seeds = 0;
        for (size_t i = 0; i < n; i++) seeds += pts.a[3 * i] == 5;
        CHECK_EQ(seeds, 1);
        pts.n = disp.n = val.n = 0;
    }
    {   /* S3 of (100, 0, -50): along z, value -50 */
        traj_field fl = { { 100, 0, -50, 0, 0, 0 } };
        cv_traj_opts o3 = opt; o3.which = 2;
        size_t n = cv_traj_trace(seed, &o3, traj_uniform, &fl, NULL, 1, &pts, &disp, &val);
        CHECK(n >= 19);
        float z0 = pts.a[2], z1 = pts.a[3 * (n - 1) + 2];
        CHECK(fminf(z0, z1) < 0.6f && fmaxf(z0, z1) > 9.4f);
        CHECK(fabsf(pts.a[0] - 5) < 1e-4f && fabsf(pts.a[1] - 5) < 1e-4f);
        CHECK_NEAR(val.a[0], -50, 1e-3);
        pts.n = disp.n = val.n = 0;
    }
    {   /* pure shear XY: S1 at 45 degrees in the xy-plane */
        traj_field fl = { { 0, 0, 0, 40, 0, 0 } };
        size_t n = cv_traj_trace(seed, &opt, traj_uniform, &fl, NULL, 1, &pts, &disp, &val);
        CHECK(n >= 10);
        float dx = pts.a[3 * (n - 1)] - pts.a[0], dy = pts.a[3 * (n - 1) + 1] - pts.a[1];
        CHECK_NEAR(fabsf(dx), fabsf(dy), 1e-3);
        CHECK(fabsf(dx) > 8);
        CHECK_NEAR(pts.a[2], 5, 1e-4);
        CHECK_NEAR(val.a[0], 40, 1e-3);
        pts.n = disp.n = val.n = 0;
    }
    {   /* occupancy: a second line seeded in the first one's cells does not start */
        traj_field fl = { { 100, 0, 0, 0, 0, 0 } };
        cv_occ o;
        float lo[3] = { 0, 0, 0 }, hi[3] = { 10, 10, 10 };
        CHECK(cv_occ_init(&o, lo, hi, 1.f, 96));
        size_t n1 = cv_traj_trace(seed, &opt, traj_uniform, &fl, &o, 1, &pts, &disp, &val);
        CHECK(n1 >= 19);
        float p[3] = { 5, 5, 5 };
        CHECK(*cv_occ_cell(&o, p) == 1);
        p[0] = 2.2f; CHECK(*cv_occ_cell(&o, p) == 1);
        float s2[3] = { 3.3f, 5.1f, 5.1f };
        size_t before = pts.n;
        CHECK_EQ(cv_traj_trace(s2, &opt, traj_uniform, &fl, &o, 2, &pts, &disp, &val), 0);
        CHECK_EQ(pts.n, before);
        float s3[3] = { 5, 7.5f, 5 };                          /* a free row: a full line of its own */
        CHECK(cv_traj_trace(s3, &opt, traj_uniform, &fl, &o, 2, &pts, &disp, &val) >= 19);
        cv_occ_free(&o);
        pts.n = disp.n = val.n = 0;
    }
    {   /* everything below min_val: nothing, the vectors untouched */
        traj_field fl = { { 0.5f, 0.2f, 0, 0, 0, 0 } };
        float x = 42;
        cv_push(val, x);
        CHECK_EQ(cv_traj_trace(seed, &opt, traj_uniform, &fl, NULL, 1, &pts, &disp, &val), 0);
        CHECK(pts.n == 0 && disp.n == 0 && val.n == 1 && val.a[0] == 42);
    }
    cv_free_vec(pts); cv_free_vec(disp); cv_free_vec(val);
}
