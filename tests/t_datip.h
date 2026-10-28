#include "../src/datip.h"
/* t_datip.h -- unit tests for datip.c (an element's integration-point rows). Included by test_main.c. */

static void test_datip(void) {
    const char* txt =
        " stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set E and time  0.1000000E+01\n\n"
        "         1   2  1.0E+02  0.0E+00  0.0E+00  0.0E+00  0.0E+00  0.0E+00\n"
        "         2   1  9.0E+00  9.0E+00  9.0E+00  0.0E+00  0.0E+00  0.0E+00\n"
        "         1   1  0.0E+00  0.0E+00  0.0E+00  1.0E+01  0.0E+00  0.0E+00\n"
        "\n"
        " equivalent plastic strain (elem, integ.pnt.,pe)for set E and time  0.1000000E+01\n\n"
        "         1   1  7.0E-04\n";
    cv_dat d;
    CHECK(cv_dat_parse(&d, txt, strlen(txt)));
    CHECK_EQ(d.n, 2);
    if (d.n == 2) {
        cv_datip t;
        CHECK_EQ(cv_datip_rows(&d.b[0], 1, &t), 2);       /* element 1's two points, sorted */
        CHECK_EQ(t.ncomp, 6);
        CHECK(t.mises);
        CHECK_EQ(t.ip[0], 1); CHECK_EQ(t.ip[1], 2);
        CHECK_NEAR(t.v[0][3], 10, 1e-6);
        CHECK_NEAR(t.v[1][0], 100, 1e-6);
        CHECK_NEAR(t.vm[0], 10 * sqrt(3.0), 1e-3);         /* pure shear */
        CHECK_NEAR(t.vm[1], 100, 1e-3);                     /* uniaxial */
        CHECK_EQ(cv_datip_rows(&d.b[0], 2, &t), 1);
        CHECK_NEAR(t.vm[0], 0, 1e-6);                       /* hydrostatic */
        t.v[0][0] = 0; t.v[0][1] = 0; t.v[0][2] = 0; t.v[0][3] = 2;   /* again after a change */
        cv_datip_mises(&t);
        CHECK_NEAR(t.vm[0], 2 * sqrt(3.0), 1e-4);
        CHECK_EQ(cv_datip_rows(&d.b[0], 3, &t), 0);         /* not printed */
        CHECK_EQ(cv_datip_rows(&d.b[1], 1, &t), 1);         /* a scalar: no von Mises */
        CHECK(!t.mises);
        CHECK(t.vm[0] != t.vm[0]);
        CHECK_NEAR(t.v[0][0], 7e-4, 1e-9);
        CHECK_EQ(cv_datip_rows(NULL, 1, &t), 0);
    }
    cv_dat_free(&d); free(d.msgs.a);
    float p[9] = { 0, 0, 0, NAN, NAN, NAN, 1, 1, 1 }, x[3] = { 0.9f, 1, 1 };
    CHECK_EQ(cv_datip_nearest(p, 3, x), 2);
    CHECK_EQ(cv_datip_nearest(p, 0, x), -1);
    float q[3] = { NAN, NAN, NAN };
    CHECK_EQ(cv_datip_nearest(q, 1, x), -1);
}
