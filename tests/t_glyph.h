/* t_glyph.h -- tensor glyphs (glyph.c): axes, lengths, superquadric shapes.
   Included by test_main.c after its CHECK macros. */
#include "../src/glyph.h"

static float g_dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void test_glyph(void) {
    CHECK_EQ(cv_glyph_style("superquadric"), CV_GLYPH_SUPERQUADRIC);
    CHECK_EQ(cv_glyph_style("cross"), CV_GLYPH_CROSS);
    CHECK_EQ(cv_glyph_style("hwy"), CV_GLYPH_HWY);
    CHECK_EQ(cv_glyph_style("blob"), -1);
    cv_glyph g;
    {   /* diagonal: the largest magnitude first, sign kept, lengths k |s| */
        float s[6] = { 10, -30, 20, 0, 0, 0 };
        CHECK(cv_glyph_make(s, false, CV_GLYPH_ELLIPSOID, 0.1f, 0, &g));
        CHECK_NEAR(g.val[0], -30, 1e-4); CHECK_NEAR(g.val[1], 20, 1e-4); CHECK_NEAR(g.val[2], 10, 1e-4);
        CHECK_NEAR(fabs(g.axis[0][1]), 1, 1e-5);
        CHECK_NEAR(fabs(g.axis[1][2]), 1, 1e-5);
        CHECK_NEAR(g.len[0], 3, 1e-4); CHECK_NEAR(g.len[1], 2, 1e-4); CHECK_NEAR(g.len[2], 1, 1e-4);
        CHECK(g.alpha == 1 && g.beta == 1 && g.cee == 1 && g.shape == CV_GSHAPE_SQ);
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
        CHECK_NEAR(fabs(g.base[2][2]), 1, 1e-5);             /* disc: round about the zero axis z */
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
    {   /* unit surface: alpha = beta = cee = 1 is the unit sphere; the pole on z */
        float q[3];
        for (int i = 0; i < 8; i++) {
            cv_superquad_point(1, 1, 1, 0.7f * i, 0.4f * i, q);
            CHECK_NEAR(g_dot(q, q), 1, 1e-5);
        }
        cv_superquad_point(0.3f, 0.3f, 0.3f, 1.f, 0, q);
        CHECK_NEAR(q[2], 1, 1e-6); CHECK_NEAR(q[0], 0, 1e-6);
        /* a small beta squares the profile: at phi = 45 deg the point reaches past the sphere */
        cv_superquad_point(1, 0.1f, 0.1f, 0, 0.78539816f, q);
        CHECK(q[0] > 0.9f && q[2] > 0.9f);
        /* the hybrid profile: y follows cee, x keeps beta */
        float h[3];
        cv_superquad_point(1, 3, 2, 1.5707963f, 0.78539816f, h);
        cv_superquad_point(1, 2, 2, 1.5707963f, 0.78539816f, q);
        CHECK_NEAR(h[1], 1 - powf(0.70710678f, 3), 1e-4);    /* sin(acos(z^(1/2)))^2 = 1 - z^(1/2)... at z = c^3 */
        CHECK(q[1] > 0);
    }
    {   /* Schultz-Kindlmann (u, v): the corners of the shape square */
        float uv[2], abc[3];
        const float iso[3] = { 1, 1, 1 }, lin[3] = { 2, 0, 0 }, thorn[3] = { 1, 1, -1 }, shear[3] = { 1, 0, -1 }, neg[3] = { -3, -3, -3 };
        cv_sk_uv(iso, uv);   CHECK_NEAR(uv[0], 1, 1e-6); CHECK_NEAR(uv[1], 1, 1e-6);
        cv_sk_uv(lin, uv);   CHECK_NEAR(uv[0], 0.5, 1e-6); CHECK_NEAR(uv[1], 1, 1e-6);
        cv_sk_uv(thorn, uv); CHECK_NEAR(uv[0], 1, 1e-6); CHECK_NEAR(uv[1], 0, 1e-6);
        cv_sk_uv(shear, uv); CHECK_NEAR(uv[0], 0.5, 1e-6); CHECK_NEAR(uv[1], 0.5, 1e-6);
        cv_sk_uv(neg, uv);   CHECK_NEAR(uv[0], 0, 1e-6); CHECK_NEAR(uv[1], 0, 1e-6);
        /* sphere for one sign all equal, the thorn of two equal tensions and a compression */
        cv_sk_uv(iso, uv); cv_sk_abc(uv, 4, abc);
        CHECK_NEAR(abc[0], 1, 1e-6); CHECK_NEAR(abc[1], 1, 1e-6); CHECK_NEAR(abc[2], 1, 1e-6);
        cv_sk_uv(neg, uv); cv_sk_abc(uv, 4, abc); CHECK_NEAR(abc[1], 1, 1e-6);
        cv_sk_uv(thorn, uv); cv_sk_abc(uv, 4, abc);
        CHECK_NEAR(abc[0], 1, 1e-6); CHECK_NEAR(abc[1], 4, 1e-6); CHECK_NEAR(abc[2], 3, 1e-6);
        /* blends stay inside the control values; concave (beta > 2) for mixed signs */
        const float mixed[3] = { 1, 0.3f, -0.6f };
        cv_sk_uv(mixed, uv); cv_sk_abc(uv, 4, abc);
        CHECK(abc[1] > 2 && abc[1] <= 4 && abc[0] >= 0 && abc[0] <= 1);
        /* the round axis: normal to the two values of the same sign */
        const float rod[3] = { 1, 0.1f, 0.05f }, two_neg[3] = { 1, -1, -1 };
        CHECK(cv_sk_axis_first(rod));
        CHECK(!cv_sk_axis_first(thorn));
        CHECK(cv_sk_axis_first(two_neg));
        float s[6] = { 100, 100, -100, 0, 0, 0 };
        CHECK(cv_glyph_make(s, false, CV_GLYPH_SK, 0.01f, 0.05f, &g));
        CHECK_NEAR(fabs(g.base[2][2]), 1, 1e-5);             /* the round axis on z, the compression */
        CHECK_NEAR(g.lam[2], -100, 1e-3);
        CHECK(cv_glyph_signed(CV_GLYPH_SK) && !cv_glyph_signed(CV_GLYPH_ELLIPSOID));
    }
    {   /* Reynolds and HWY: the reach is the largest normal / shear stress */
        float s[6] = { 100, 0, -50, 0, 0, 0 };
        CHECK(cv_glyph_make(s, false, CV_GLYPH_REYNOLDS, 0.01f, 0, &g));
        CHECK_EQ(g.shape, CV_GSHAPE_REYNOLDS);
        CHECK_NEAR(cv_glyph_extent(&g), 1, 1e-5);
        CHECK_NEAR(g.lam[0], 100, 1e-3); CHECK_NEAR(g.lam[2], -50, 1e-3);
        CHECK(cv_glyph_make(s, false, CV_GLYPH_HWY, 0.01f, 0, &g));
        CHECK_NEAR(cv_glyph_extent(&g), 0.75, 1e-5);
        cv_glyph_cap(&g, 0.5f);
        CHECK_NEAR(cv_glyph_extent(&g), 0.5, 1e-5);
    }
}
