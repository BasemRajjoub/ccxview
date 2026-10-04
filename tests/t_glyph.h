/* t_glyph.h -- tensor glyphs (glyph.c): axes, lengths, superquadric shapes.
   Included by test_main.c after its CHECK macros. */
#include "../src/glyph.h"

static float g_dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void test_glyph(void) {
    CHECK_EQ(cv_glyph_style("superquadric"), CV_GLYPH_SUPERQUADRIC);
    CHECK_EQ(cv_glyph_style("cross"), CV_GLYPH_CROSS);
    CHECK_EQ(cv_glyph_style("blob"), -1);
    cv_glyph g;
    {   /* diagonal: the largest magnitude first, sign kept, lengths k |s| */
        float s[6] = { 10, -30, 20, 0, 0, 0 };
        CHECK(cv_glyph_make(s, false, CV_GLYPH_ELLIPSOID, 0.1f, 0, &g));
        CHECK_NEAR(g.val[0], -30, 1e-4); CHECK_NEAR(g.val[1], 20, 1e-4); CHECK_NEAR(g.val[2], 10, 1e-4);
        CHECK_NEAR(fabs(g.axis[0][1]), 1, 1e-5);
        CHECK_NEAR(fabs(g.axis[1][2]), 1, 1e-5);
        CHECK_NEAR(g.len[0], 3, 1e-4); CHECK_NEAR(g.len[1], 2, 1e-4); CHECK_NEAR(g.len[2], 1, 1e-4);
        CHECK(g.alpha == 1 && g.beta == 1 && !g.planar);
        float c[3] = { g.axis[0][1] * g.axis[1][2] - g.axis[0][2] * g.axis[1][1],
                       g.axis[0][2] * g.axis[1][0] - g.axis[0][0] * g.axis[1][2],
                       g.axis[0][0] * g.axis[1][1] - g.axis[0][1] * g.axis[1][0] };
        CHECK_NEAR(g_dot(c, g.axis[2]), 1, 1e-5);           /* right-handed */
    }
    {   /* pure shear XY = t: principal +t, -t along the diagonals, 0 along z */
        float s[6] = { 0, 0, 0, 5, 0, 0 };
        CHECK(cv_glyph_make(s, false, CV_GLYPH_ELLIPSOID, 1, 0.1f, &g));
        CHECK_NEAR(fabs(g.val[0]), 5, 1e-4); CHECK_NEAR(fabs(g.val[1]), 5, 1e-4);
        CHECK_NEAR(fabs(g.axis[0][0]), 0.70710678, 1e-4);
        CHECK_NEAR(g.len[2], 0.5, 1e-4);                     /* the zero axis kept at min_frac */
        CHECK_NEAR(fabs(g.axis[2][2]), 1, 1e-4);
    }
    {   /* .dat shear order: XZ in the fifth slot */
        float s[6] = { 0, 0, 0, 0, 7, 0 };
        CHECK(cv_glyph_make(s, true, CV_GLYPH_ELLIPSOID, 1, 0, &g));
        CHECK_NEAR(fabs(g.axis[0][1]), 0, 1e-5);             /* in the x-z plane */
    }
    {   /* NaN and zero give no glyph */
        float n[6] = { NAN, 0, 0, 0, 0, 0 }, z[6] = { 0 };
        CHECK(!cv_glyph_make(n, false, CV_GLYPH_ELLIPSOID, 1, 0, &g));
        CHECK(!cv_glyph_make(z, false, CV_GLYPH_ELLIPSOID, 1, 0, &g));
    }
    {   /* superquadric shapes: ball, rod (round about x), disc (round about z) */
        float a, b; bool pl;
        float ball[3] = { 2, 2, 2 }, rod[3] = { 3, 0, 0 }, disc[3] = { 3, 3, 0 };
        cv_superquad_shape(ball, CV_GLYPH_GAMMA, &a, &b, &pl);
        CHECK_NEAR(a, 1, 1e-6); CHECK_NEAR(b, 1, 1e-6); CHECK(!pl);
        cv_superquad_shape(rod, CV_GLYPH_GAMMA, &a, &b, &pl);
        CHECK(!pl); CHECK_NEAR(a, 1, 1e-6); CHECK_NEAR(b, 0.05, 1e-6);
        cv_superquad_shape(disc, CV_GLYPH_GAMMA, &a, &b, &pl);
        CHECK(pl); CHECK_NEAR(a, 1, 1e-6); CHECK_NEAR(b, 0.05, 1e-6);
        float s[6] = { 4, 4, 0, 0, 0, 0 };
        CHECK(cv_glyph_make(s, false, CV_GLYPH_SUPERQUADRIC, 1, 0.05f, &g));
        CHECK(g.planar);
    }
    {   /* reference size: a quantile ignores the one singular peak; cap keeps the shape */
        float m[100];
        for (int i = 0; i < 100; i++) m[i] = (float)(99 - i);
        m[7] = 1e6f;
        CHECK_NEAR(cv_glyph_ref(m, 100, 0.98f), 98, 1e-6);   /* 92 went up to the peak */
        CHECK_NEAR(cv_glyph_ref(m, 100, 1.f), 1e6, 1);
        CHECK_EQ(cv_glyph_ref(m, 0, 0.5f), 0);
        float s[6] = { 40, 20, 10, 0, 0, 0 };
        CHECK(cv_glyph_make(s, false, CV_GLYPH_ELLIPSOID, 1, 0, &g));
        cv_glyph_cap(&g, 8);
        CHECK_NEAR(g.len[0], 8, 1e-5); CHECK_NEAR(g.len[1], 4, 1e-5); CHECK_NEAR(g.len[2], 2, 1e-5);
        cv_glyph_cap(&g, 100);
        CHECK_NEAR(g.len[0], 8, 1e-5);
    }
    {   /* unit surface: alpha = beta = 1 is the unit sphere; the poles on the round axis */
        float q[3];
        for (int i = 0; i < 8; i++) {
            cv_superquad_point(1, 1, i & 1, 0.7f * i, 0.4f * i, q);
            CHECK_NEAR(g_dot(q, q), 1, 1e-5);
        }
        cv_superquad_point(0.3f, 0.3f, false, 1.f, 0, q);
        CHECK_NEAR(q[0], 1, 1e-6); CHECK_NEAR(q[1], 0, 1e-6);
        cv_superquad_point(0.3f, 0.3f, true, 1.f, 0, q);
        CHECK_NEAR(q[2], 1, 1e-6);
        /* a small beta squares the profile: at phi = 45 deg the point reaches past the sphere */
        cv_superquad_point(1, 0.1f, false, 0, 0.78539816f, q);
        CHECK(q[0] > 0.9f && q[2] > 0.9f);
    }
}
