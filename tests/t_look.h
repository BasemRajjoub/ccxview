/* t_look.h -- the Studio look's material catalog and guess (look.c). Included by test_main.c. */
#include "../src/look.h"
#include "../src/units.h"

static int lk_name(const char* n) { return cv_look_guess(n, NULL, CV_SYS_NONE, NULL, 0); }

static int lk_iso(double E, double nu, double rho, int sys) {
    cv_matprop p; memset(&p, 0, sizeof p);
    p.el = CV_EL_ISO; p.c[0] = (float)E; p.c[1] = (float)nu;
    if (rho > 0) { p.rho = (float)rho; p.has |= CV_MP_RHO; }
    return cv_look_guess("", &p, sys, NULL, 0);
}

static void test_look(void) {
    for (int i = 0; i < CV_LOOK_N; i++) {
        const cv_look_mat* m = &cv_look_mats[i];
        CHECK(m->name && *m->name);
        CHECK(m->roughness >= 0.04f && m->roughness <= 1.f);
        CHECK(m->metallic == 0.f || m->metallic == 1.f);
        for (int k = 0; k < 3; k++) CHECK(m->rgb[k] > 0.f && m->rgb[k] <= 1.f);
        CHECK_EQ(cv_look_find(m->name), i);
    }
    CHECK_EQ(cv_look_find("aluminium"), CV_LOOK_ALUMINIUM);
    CHECK_EQ(cv_look_find("Stainless_Steel"), CV_LOOK_STAINLESS);
    CHECK_EQ(cv_look_find("castiron"), CV_LOOK_CAST_IRON);
    CHECK_EQ(cv_look_find("nope"), -1);
    CHECK_EQ(cv_look_find(NULL), -1);

    CHECK_EQ(lk_name("STEEL"), CV_LOOK_STEEL);
    CHECK_EQ(lk_name("S355J2"), CV_LOOK_STEEL);
    CHECK_EQ(lk_name("X5CrNi18-10"), CV_LOOK_STAINLESS);
    CHECK_EQ(lk_name("Stainless_Steel"), CV_LOOK_STAINLESS);
    CHECK_EQ(lk_name("AL6061-T6"), CV_LOOK_ALUMINIUM);
    CHECK_EQ(lk_name("ALU"), CV_LOOK_ALUMINIUM);
    CHECK_EQ(lk_name("Ti6Al4V"), CV_LOOK_TITANIUM);
    CHECK_EQ(lk_name("C30/37"), CV_LOOK_CONCRETE);
    CHECK_EQ(lk_name("GFRP"), CV_LOOK_GFRP);
    CHECK_EQ(lk_name("glass_fibre"), CV_LOOK_GFRP);
    CHECK_EQ(lk_name("GLASS"), CV_LOOK_GLASS);
    CHECK_EQ(lk_name("AL2O3"), CV_LOOK_CERAMIC);
    CHECK_EQ(lk_name("GJL250"), CV_LOOK_CAST_IRON);
    CHECK_EQ(lk_name("CuZn37"), CV_LOOK_BRASS);
    CHECK_EQ(lk_name("CU"), CV_LOOK_COPPER);
    CHECK_EQ(lk_name("AZ31"), CV_LOOK_MAGNESIUM);
    CHECK_EQ(lk_name("T300"), CV_LOOK_CFRP);
    CHECK_EQ(lk_name("PP"), CV_LOOK_PLASTIC);
    CHECK_EQ(lk_name("EPDM70"), CV_LOOK_RUBBER);
    CHECK_EQ(lk_name("Timber"), CV_LOOK_WOOD);
    CHECK_EQ(lk_name("TIE_PART"), CV_LOOK_DEFAULT);
    CHECK_EQ(lk_name("MATERIAL1"), CV_LOOK_DEFAULT);
    CHECK_EQ(lk_name("SPACER"), CV_LOOK_DEFAULT);
    CHECK_EQ(lk_name("CUSHION"), CV_LOOK_DEFAULT);
    CHECK_EQ(lk_name("PART1"), CV_LOOK_DEFAULT);
    CHECK_EQ(lk_name(NULL), CV_LOOK_DEFAULT);

    CHECK_EQ(lk_iso(210000, 0.3, 7.85e-9, CV_SYS_NONE), CV_LOOK_STEEL);
    CHECK_EQ(lk_iso(2.1e11, 0.3, 7850, CV_SYS_NONE), CV_LOOK_STEEL);
    CHECK_EQ(lk_iso(2.1e11, 0.3, 7850, CV_SYS_M_KG_S), CV_LOOK_STEEL);
    CHECK_EQ(lk_iso(70000, 0.33, 2.7e-9, CV_SYS_NONE), CV_LOOK_ALUMINIUM);
    CHECK_EQ(lk_iso(70000, 0.22, 2.5e-9, CV_SYS_NONE), CV_LOOK_GLASS);
    CHECK_EQ(lk_iso(30000, 0.2, 2.4e-9, CV_SYS_NONE), CV_LOOK_CONCRETE);
    CHECK_EQ(lk_iso(210000, 0.3, 0, CV_SYS_NONE), CV_LOOK_STEEL);
    CHECK_EQ(lk_iso(3e7, 0.3, 7.33e-4, CV_SYS_NONE), CV_LOOK_STEEL);
    CHECK_EQ(lk_iso(5, 0.49, 0, CV_SYS_NONE), CV_LOOK_RUBBER);
    CHECK_EQ(lk_iso(1e-3, 0.3, 0, CV_SYS_NONE), CV_LOOK_DEFAULT);
    CHECK_EQ(lk_iso(210000, 0, 0, CV_SYS_NONE), CV_LOOK_STEEL);       /* E alone, in range */
    CHECK_EQ(lk_iso(150000, 0, 0, CV_SYS_NONE), CV_LOOK_DEFAULT);     /* E alone, no match */
    CHECK_EQ(lk_iso(210000, 0.3, 7.85e-9, CV_SYS_CM_G_S), CV_LOOK_DEFAULT);   /* wrong system forced */

    cv_matprop p; memset(&p, 0, sizeof p);
    p.el = CV_EL_ENG; p.c[0] = 135000; p.c[1] = 9000; p.rho = 1.55e-9f; p.has = CV_MP_RHO;
    char why[96];
    CHECK_EQ(cv_look_guess("", &p, CV_SYS_NONE, why, sizeof why), CV_LOOK_CFRP);
    CHECK(why[0]);
    CHECK_EQ(cv_look_guess(NULL, NULL, CV_SYS_NONE, why, sizeof why), CV_LOOK_DEFAULT);
    CHECK(why[0]);
    CHECK_EQ(cv_look_guess("S355", NULL, CV_SYS_NONE, why, sizeof why), CV_LOOK_STEEL);
    CHECK(why[0]);
}
