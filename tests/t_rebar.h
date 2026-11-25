/* t_rebar.h -- reinforcement of concrete shells (rebar.c): the sandwich model
   against hand calculations. A 200 mm slab, cover 30 (z = 140, layers 60 thick),
   fcd 20 MPa, fyd 435 MPa; forces in N/mm, moments in N mm/mm. Included by
   test_main.c. */
#include "../src/rebar.h"

/* the point's areas for Nxx Nyy Nxy Mxx Myy Mxy */
static bool rb_at(double nxx, double nyy, double nxy, double mxx, double myy, double mxy, float out[CV_RB_N]) {
    const cv_rebar_par p = { 20, 435, 30 };
    const double sf[6] = { nxx, nyy, nxy, mxx, myy, mxy };
    return cv_rebar_point(sf, 200, &p, out);
}

static void test_rebar(void) {
    float o[CV_RB_N];
    double fx, fy, fc;
    CHECK(!strcmp(cv_rebar_comp(CV_RB_XT), "As_x_top") && !strcmp(cv_rebar_comp(CV_RB_CRUSH), "Crushing"));

    /* one layer: the four cases of Annex F.1 */
    cv_rebar_layer(100, 50, 30, &fx, &fy, &fc);
    CHECK_NEAR(fx, 130, 1e-9); CHECK_NEAR(fy, 80, 1e-9); CHECK_NEAR(fc, 60, 1e-9);
    cv_rebar_layer(-500, 300, -200, &fx, &fy, &fc);           /* x compressed past the shear */
    CHECK_NEAR(fx, 0, 1e-9); CHECK_NEAR(fy, 380, 1e-9); CHECK_NEAR(fc, 580, 1e-9);
    cv_rebar_layer(300, -500, 200, &fx, &fy, &fc);            /* y the same */
    CHECK_NEAR(fx, 380, 1e-9); CHECK_NEAR(fy, 0, 1e-9); CHECK_NEAR(fc, 580, 1e-9);
    cv_rebar_layer(-500, -100, 200, &fx, &fy, &fc);           /* both: the principal compression */
    CHECK_NEAR(fx, 0, 1e-9); CHECK_NEAR(fy, 0, 1e-9); CHECK_NEAR(fc, 300 + sqrt(80000.0), 1e-9);

    /* pure bending: the tension face M / (z fyd), the other none */
    CHECK(rb_at(0, 0, 0, 100000, 0, 0, o));
    CHECK_NEAR(o[CV_RB_XT], 100000 / (140 * 435.0), 1e-5);
    CHECK_NEAR(o[CV_RB_XB], 0, 1e-9); CHECK_NEAR(o[CV_RB_YT], 0, 1e-9); CHECK_NEAR(o[CV_RB_YB], 0, 1e-9);
    CHECK_NEAR(o[CV_RB_MAX], o[CV_RB_XT], 1e-9); CHECK_NEAR(o[CV_RB_SUM], o[CV_RB_XT], 1e-6);
    CHECK_NEAR(o[CV_RB_CONC], 100000 / 140.0 / 60 / 20, 1e-5); CHECK_EQ(o[CV_RB_CRUSH], 0);
    CHECK(rb_at(0, 0, 0, 0, -100000, 0, o));                 /* the other way: the bottom, along y */
    CHECK_NEAR(o[CV_RB_YB], 100000 / (140 * 435.0), 1e-5); CHECK_NEAR(o[CV_RB_YT] + o[CV_RB_XT] + o[CV_RB_XB], 0, 1e-9);

    /* pure tension: half to each face */
    CHECK(rb_at(1000, 0, 0, 0, 0, 0, o));
    CHECK_NEAR(o[CV_RB_XT], 500 / 435.0, 1e-5); CHECK_NEAR(o[CV_RB_XB], 500 / 435.0, 1e-5);
    CHECK_NEAR(o[CV_RB_YT] + o[CV_RB_YB], 0, 1e-9);

    /* twisting: Wood-Armer, |Mxy| / (z fyd) both ways on both faces */
    CHECK(rb_at(0, 0, 0, 0, 0, -50000, o));
    for (int k = 0; k < 4; k++) CHECK_NEAR(o[k], 50000 / (140 * 435.0), 1e-5);
    CHECK_NEAR(o[CV_RB_CONC], 2 * 50000 / 140.0 / 60 / 20, 1e-5);

    /* combined: Mxx 70000, Mxy 28000, Nyy -200. Top: nx 500, ny -100, nxy 200 -> 700, 100;
       bottom: nx -500, ny -100, nxy -200, compression both ways */
    CHECK(rb_at(0, -200, 0, 70000, 0, 28000, o));
    CHECK_NEAR(o[CV_RB_XT], 700 / 435.0, 1e-5); CHECK_NEAR(o[CV_RB_YT], 100 / 435.0, 1e-5);
    CHECK_NEAR(o[CV_RB_XB], 0, 1e-9); CHECK_NEAR(o[CV_RB_YB], 0, 1e-9);
    CHECK_NEAR(o[CV_RB_CONC], (300 + sqrt(80000.0)) / 60 / 20, 1e-5);
    /* Mxx -70000, Mxy 28000, Nyy 600. Top: nx -500, ny 300, nxy 200 -> 0, 380;
       bottom: nx 500, ny 300, nxy -200 -> 700, 500 */
    CHECK(rb_at(0, 600, 0, -70000, 0, 28000, o));
    CHECK_NEAR(o[CV_RB_XT], 0, 1e-9); CHECK_NEAR(o[CV_RB_YT], 380 / 435.0, 1e-5);
    CHECK_NEAR(o[CV_RB_XB], 700 / 435.0, 1e-5); CHECK_NEAR(o[CV_RB_YB], 500 / 435.0, 1e-5);
    CHECK_NEAR(o[CV_RB_MAX], 700 / 435.0, 1e-5); CHECK_NEAR(o[CV_RB_SUM], 1580 / 435.0, 1e-5);
    CHECK_NEAR(o[CV_RB_CONC], 580 / 60.0 / 20, 1e-5);
    /* membrane shear with tension: Nxx 400, Nxy 300 -> each layer 200 + 150 and 0 + 150 */
    CHECK(rb_at(400, 0, 300, 0, 0, 0, o));
    CHECK_NEAR(o[CV_RB_XT], 350 / 435.0, 1e-5); CHECK_NEAR(o[CV_RB_YB], 150 / 435.0, 1e-5);

    /* compression: no steel; the concrete takes a little, crushes under a lot */
    CHECK(rb_at(-1000, -1000, 0, 0, 0, 0, o));
    for (int k = 0; k <= CV_RB_SUM; k++) CHECK_NEAR(o[k], 0, 1e-9);
    CHECK_NEAR(o[CV_RB_CONC], 500 / 60.0 / 20, 1e-5); CHECK_EQ(o[CV_RB_CRUSH], 0);
    CHECK(rb_at(-5000, 0, 0, 0, 0, 0, o));
    CHECK(o[CV_RB_CONC] > 2 && o[CV_RB_CRUSH] == 1);
    CHECK_NEAR(o[CV_RB_MAX], 0, 1e-9);

    /* no design: the cover past the middle, a parameter missing, a value missing */
    {
        const double sf[6] = { 1, 2, 3, 4, 5, 6 };
        cv_rebar_par p = { 20, 435, 100 };
        CHECK(!cv_rebar_point(sf, 200, &p, o) && o[0] != o[0] && o[CV_RB_CRUSH] != o[CV_RB_CRUSH]);
        p.cover = 30; p.fyd = 0;
        CHECK(!cv_rebar_point(sf, 200, &p, o));
        CHECK(!rb_at(NAN, 0, 0, 0, 0, 0, o) && o[CV_RB_XT] != o[CV_RB_XT]);
    }
    /* the field: per node, from shell.h's components, NaN where there is no shell */
    {
        float sf[2 * CV_SF_N], h[2] = { 200, 200 }, out[2 * CV_RB_N];
        for (int c = 0; c < 2 * CV_SF_N; c++) sf[c] = 0;
        sf[CV_SF_MXX] = 100000; sf[CV_SF_QX] = 7;            /* the shears play no part */
        sf[CV_SF_N + CV_SF_NXX] = NAN;
        const cv_rebar_par p = { 20, 435, 30 };
        cv_rebar_field(sf, h, 2, &p, out);
        CHECK_NEAR(out[CV_RB_XT], 100000 / (140 * 435.0), 1e-5);
        CHECK(out[CV_RB_N + CV_RB_XT] != out[CV_RB_N + CV_RB_XT]);
    }
}
