/* t_contact.h -- unit tests for src/contact.c: projection onto a master face, the
   signed gap, the contact status, the gap's colours, the nearest face. */
#ifndef CV_T_CONTACT_H
#define CV_T_CONTACT_H

#include "../src/contact.h"

static void test_contact(void) {
    cv_cproj o;
    /* a flat quad in z = 0, corners counter-clockwise seen from +z: normal +z */
    const float q[4][3] = { { 0, 0, 0 }, { 2, 0, 0 }, { 2, 2, 0 }, { 0, 2, 0 } };
    const float p1[3] = { 0.5f, 1.5f, 0.3f };
    CHECK(cv_contact_project(p1, q, 4, &o));
    CHECK(o.inside);
    CHECK_NEAR(o.gap, 0.3, 1e-6); CHECK_NEAR(o.n[2], 1.0, 1e-6);
    CHECK_NEAR(o.foot[0], 0.5, 1e-6); CHECK_NEAR(o.foot[1], 1.5, 1e-6); CHECK_NEAR(o.foot[2], 0, 1e-6);
    CHECK_NEAR(o.xi, -0.5, 1e-6); CHECK_NEAR(o.eta, 0.5, 1e-6);
    CHECK_NEAR(o.w[0] + o.w[1] + o.w[2] + o.w[3], 1, 1e-6);
    float fx = 0;                               /* the weights give the foot */
    for (int k = 0; k < 4; k++) fx += o.w[k] * q[k][0];
    CHECK_NEAR(fx, 0.5, 1e-6);
    /* behind the face: negative, as COPEN of a penetration */
    const float p2[3] = { 1, 1, -0.004f };
    CHECK(cv_contact_project(p2, q, 4, &o));
    CHECK_NEAR(o.gap, -0.004, 1e-7); CHECK(o.inside);
    /* the corners the other way round: the normal and the sign turn */
    const float qr[4][3] = { { 0, 0, 0 }, { 0, 2, 0 }, { 2, 2, 0 }, { 2, 0, 0 } };
    CHECK(cv_contact_project(p1, qr, 4, &o));
    CHECK_NEAR(o.gap, -0.3, 1e-6); CHECK_NEAR(o.n[2], -1.0, 1e-6);
    /* outside the face: clamped to its edge, not the centre */
    const float p3[3] = { 3, 1, 0.5f };
    CHECK(cv_contact_project(p3, q, 4, &o));
    CHECK(!o.inside);
    CHECK_NEAR(o.foot[0], 2, 1e-5); CHECK_NEAR(o.foot[1], 1, 1e-5); CHECK_NEAR(o.xi, 1, 1e-6);
    CHECK_NEAR(o.gap, 0.5, 1e-5);
    /* a warped quad: one corner lifted; the foot lies on the bilinear surface, the
       residual along its normal there */
    const float qw[4][3] = { { 0, 0, 0 }, { 2, 0, 0 }, { 2, 2, 0.4f }, { 0, 2, 0 } };
    const float p4[3] = { 1.2f, 1.1f, 0.7f };
    CHECK(cv_contact_project(p4, qw, 4, &o));
    CHECK(o.inside);
    {
        float r[3] = { p4[0] - o.foot[0], p4[1] - o.foot[1], p4[2] - o.foot[2] };
        float len = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        CHECK_NEAR(fabsf(o.gap), len, 1e-5);                  /* straight along the normal */
        float xi = o.xi, eta = o.eta;                         /* the foot on the bilinear surface */
        float z = 0.4f * (1 + xi) * (1 + eta) / 4;
        CHECK_NEAR(o.foot[2], z, 1e-5);
        CHECK(o.gap > 0);
    }
    /* a triangle; inside, then off a corner (clamped onto the nearest edge or corner) */
    const float t[3][3] = { { 0, 0, 1 }, { 1, 0, 1 }, { 0, 1, 1 } };
    const float p5[3] = { 0.2f, 0.3f, 1.25f };
    CHECK(cv_contact_project(p5, t, 3, &o));
    CHECK(o.inside); CHECK_NEAR(o.gap, 0.25, 1e-6);
    CHECK_NEAR(o.w[0], 0.5, 1e-6); CHECK_NEAR(o.w[1], 0.2, 1e-6); CHECK_NEAR(o.w[2], 0.3, 1e-6);
    const float p6[3] = { 2, -1, 0.9f };
    CHECK(cv_contact_project(p6, t, 3, &o));
    CHECK(!o.inside);
    CHECK_NEAR(o.foot[0], 1, 1e-6); CHECK_NEAR(o.foot[1], 0, 1e-6); CHECK_NEAR(o.w[1], 1, 1e-6);
    CHECK_NEAR(o.gap, -0.1, 1e-6);
    const float tz[3][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } };   /* degenerate */
    CHECK(!cv_contact_project(p5, tz, 3, &o));

    /* status */
    cv_cin in = { .gap = NAN, .press = NAN, .shear = NAN, .mu = 0, .tol = 0.01f, .near = 0.2f, .elem = false };
    CHECK_EQ(cv_contact_status(&in), -1);
    in.elem = true;
    CHECK_EQ(cv_contact_status(&in), CV_CST_CLOSED);          /* only the element says so */
    in.gap = 0.15f;
    CHECK_EQ(cv_contact_status(&in), CV_CST_NEAR);            /* the gap beats the element */
    in.gap = 0.35f;
    CHECK_EQ(cv_contact_status(&in), CV_CST_FAR);
    in.gap = 0.005f;
    CHECK_EQ(cv_contact_status(&in), CV_CST_CLOSED);          /* within the tolerance */
    in.press = -3.f;                                          /* CalculiX's open value */
    CHECK_EQ(cv_contact_status(&in), CV_CST_NEAR);
    in.gap = -0.004f; in.press = 3600.f; in.shear = 0;
    CHECK_EQ(cv_contact_status(&in), CV_CST_SLIDE);           /* frictionless */
    in.mu = 0.2f; in.shear = 500.f;
    CHECK_EQ(cv_contact_status(&in), CV_CST_STICK);           /* 500 < 0.2 * 3600 */
    in.shear = 720.f;
    CHECK_EQ(cv_contact_status(&in), CV_CST_SLIDE);           /* at mu p: slipping */
    in.shear = NAN;
    CHECK_EQ(cv_contact_status(&in), CV_CST_CLOSED);
    in.gap = -0.03f;
    CHECK_EQ(cv_contact_status(&in), CV_CST_PEN);
    CHECK(!strcmp(cv_cst_names[CV_CST_STICK], "sticking"));

    /* colours: zero-anchored, ends independent */
    float c[3], d[3];
    cv_contact_gap_rgb(CV_CGAP_CONTOUR, 0, -0.004f, 0.34f, 0, c);
    CHECK(c[0] > 0.9f && c[1] > 0.9f && c[2] > 0.9f);          /* white at 0 */
    cv_contact_gap_rgb(CV_CGAP_CONTOUR, -0.004f, -0.004f, 0.34f, 0, c);
    CHECK(c[0] > 0.6f && c[1] < 0.1f);                        /* the deepest: full red, whatever the open end */
    cv_contact_gap_rgb(CV_CGAP_CONTOUR, 0.34f, -0.004f, 0.34f, 0, c);
    CHECK(c[2] > 0.7f && c[0] < 0.2f);
    cv_contact_gap_rgb(CV_CGAP_CONTOUR, 1.f, -0.004f, 0.34f, 0, d);    /* beyond: the end's */
    CHECK_NEAR(c[2], d[2], 1e-6);
    cv_contact_gap_rgb(CV_CGAP_LINKS, 0.003f, -0.02f, 0.34f, 0.005f, c);
    cv_contact_gap_rgb(CV_CGAP_LINKS, 0, -0.02f, 0.34f, 0.005f, d);
    CHECK(c[1] > 0.7f && c[1] == d[1]);                       /* green within the tolerance */
    float tab[3 * 256];
    cv_contact_gap_table(CV_CGAP_LINKS, -0.02f, 0.34f, 0, tab, 256);
    CHECK(tab[0] > 0.8f && tab[1] < 0.2f);                    /* red first, blue last */
    CHECK(tab[3 * 255 + 2] > 0.9f);

    /* the nearest face: a 3 x 3 patch of unit quads in z = 0, and a triangle above */
    float fc[10 * 12];
    uint8_t nc[10];
    uint32_t nf = 0;
    for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++, nf++) {
        float* f = fc + 12 * nf;
        const float v[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (int k = 0; k < 4; k++) { f[3 * k] = (float)i + v[k][0]; f[3 * k + 1] = (float)j + v[k][1]; f[3 * k + 2] = 0; }
        nc[nf] = 4;
    }
    {
        float* f = fc + 12 * nf;
        const float v[4][3] = { { 0, 0, 5 }, { 1, 0, 5 }, { 0, 1, 5 }, { 0, 1, 5 } };
        memcpy(f, v, sizeof v);
        nc[nf++] = 3;
    }
    cv_cgrid* g = cv_cgrid_build(fc, nc, nf);
    CHECK(g != NULL);
    const float s1[3] = { 2.5f, 1.25f, 0.1f };
    CHECK_EQ(cv_cgrid_nearest(g, s1, 1e30f, &o), 5);          /* row 1, column 2 */
    CHECK_NEAR(o.gap, 0.1, 1e-6);
    const float s2[3] = { 0.2f, 0.2f, 4.5f };
    CHECK_EQ(cv_cgrid_nearest(g, s2, 1e30f, &o), 9);          /* the triangle, its back */
    CHECK_NEAR(o.gap, -0.5, 1e-6);
    const float s3[3] = { 10, 1.5f, 0 };                      /* far off the side: the edge column */
    CHECK_EQ(cv_cgrid_nearest(g, s3, 1e30f, &o), 5);
    CHECK(!o.inside);
    CHECK_EQ(cv_cgrid_nearest(g, s3, 1.f, &o), UINT32_MAX);   /* not within reach */
    cv_cgrid_free(g);
}

#endif
