/* t_failure.h -- failure criteria (failure.c): every criterion at its strengths,
   published and hand-derived points for Hashin, Puck and LaRC, the reserve factor
   against the index at the scaled state, the material text, and the deck's elastic,
   plastic and layer data (inp.c) with the composite .dat mapping (inp_localsys.c).
   Included by test_main.c after t_units.h (CHECK_REL). */
#include "../src/failure.h"

/* AS4/3501-6 as in Soden, Hinton, Kaddour (1998), with Puck's AS4 slopes */
static cv_fmat f_as4(void) {
    cv_fmat m;
    cv_fmat_defaults(&m, CV_MK_UD);
    m.E1 = 126000; m.E2 = 11000; m.G12 = 6600; m.nu12 = 0.28;
    m.Xt = 1950; m.Xc = 1480; m.Yt = 48; m.Yc = 200; m.S12 = 79;
    return m;
}

static cv_fres f_eval(int c, const cv_fmat* m, double s11, double s22, double s33, double t12, double t23, double t13) {
    const double s[6] = { s11, s22, s33, t12, t23, t13 };
    cv_fres r;
    cv_fail_eval(c, m, NULL, s, &r);
    return r;
}

static void test_failure(void) {
    cv_fmat m = f_as4();
    cv_fres r;

    /* every UD criterion fails at each strength alone */
    for (int c = 0; c < CV_FC_MISES; c++) {
        const double at[5][6] = { { m.Xt }, { -m.Xc }, { 0, m.Yt }, { 0, -m.Yc }, { 0, 0, 0, m.S12 } };
        for (int k = 0; k < 5; k++) {
            cv_fail_eval(c, &m, NULL, at[k], &r);
            g_checks++;
            if (!(fabs(r.rf - 1) < 2e-4)) {
                g_fail++;
                fprintf(stderr, "%s:%d: %s, load %d: rf %g\n", __FILE__, __LINE__, cv_fc_name(c), k, r.rf);
            }
        }
        r = f_eval(c, &m, 0, 0, 0, 0, 0, 0);
        CHECK(r.rf > 1e30 || !(r.fi > 0));
    }
    r = f_eval(CV_FC_MAXSTRESS, &m, 0, 0, 0, 40, 0, 0);
    CHECK_EQ(r.mode, CV_FM_S12);
    r = f_eval(CV_FC_MAXSTRESS, &m, -740, 0, 0, 0, 0, 0);
    CHECK_EQ(r.mode, CV_FM_FC); CHECK_NEAR(r.fi, 0.5, 1e-6);

    /* Tsai-Wu: the biaxial state with the interaction, by hand */
    {
        double F1 = 1 / 1950. - 1 / 1480., F2 = 1 / 48. - 1 / 200., F11 = 1 / (1950. * 1480), F22 = 1 / (48. * 200);
        double F12 = -0.5 * sqrt(F11 * F22), s1 = 500, s2 = 20;
        r = f_eval(CV_FC_TSAIWU, &m, s1, s2, 0, 0, 0, 0);
        CHECK_REL(r.fi, F1 * s1 + F2 * s2 + F11 * s1 * s1 + F22 * s2 * s2 + 2 * F12 * s1 * s2, 1e-6);
    }

    /* Hashin (1980) fig. 6, glass/epoxy off-axis tension: the failure stress per angle
       (eq. 24 fibre, eq. 26 matrix; the switch about 8 degrees, as Hashin says) */
    {
        cv_fmat g;
        cv_fmat_defaults(&g, CV_MK_UD);
        g.Xt = 1241; g.Yt = 28.3; g.S12 = 37.9; g.Xc = 600; g.Yc = 100; g.S23 = 30;
        const double th[4] = { 5, 10, 30, 90 }, sig[4] = { 412.13, 215.69, 69.24, 28.30 };
        const int mode[4] = { CV_FM_FT, CV_FM_MT, CV_FM_MT, CV_FM_MT };
        for (int k = 0; k < 4; k++) {
            double c = cos(th[k] * 3.14159265358979 / 180), s = sin(th[k] * 3.14159265358979 / 180);
            r = f_eval(CV_FC_HASHIN, &g, c * c, s * s, 0, -s * c, 0, 0);
            CHECK_NEAR(r.rf, sig[k], 0.01);
            CHECK_EQ(r.mode, mode[k]);
        }
        /* eq. 19 with fig. 3's Yc, tau_T: the linear term makes F negative, the
           reserve factor comes from the quadratic */
        g.Yc = 240; g.S23 = 55; g.S12 = 69;
        r = f_eval(CV_FC_HASHIN, &g, 0, -100, 0, 40, 0, 0);
        CHECK_NEAR(r.fi, -0.404294, 1e-5);
        CHECK_NEAR(r.rf, 1.820332, 1e-5);
        CHECK_EQ(r.mode, CV_FM_MC);
        r = f_eval(CV_FC_HASHIN, &g, 0, -240, 0, 0, 0, 0);
        CHECK_NEAR(r.fi, 1, 1e-6);
    }

    /* Puck (Knops 2008): Rt 48, Rc 200, R_perp_par 79, p 0.35 / 0.30, p_perp_perp
       coupled (eq. 75): 0.293566, so R_perp_perp^A = 77.305687 and the 2D closed forms
       hold; the 3D search must give them */
    {
        cv_fmat p = m;
        p.p23t = p.p23c = 0.293566;
        const struct { double s2, s3, t21, t23, t31, fe, th; } k[] = {
            { -200, 0, 0, 0, 0, 1.000000, 51.559 },
            { 48, 0, 0, 0, 0, 1.000000, 0 },
            { 0, 0, 79, 0, 0, 1.000000, 0 },
            { 0, 0, 0, 48, 0, 1.000000, 45 },
            { 0, 0, 0, 77.305687, 0, 1.610535, 45 },
            { -77.305687, 0, 99.525321, 0, 0, 1.000000, 0 },
            { 30, 0, 40, 0, 0, 0.838971, 0 },
            { -50, 0, 60, 0, 0, 0.594724, 16.482 },
            { -150, 0, 60, 0, 0, 0.864908, 48.115 },
            { -100, 0, 20, 0, 0, 0.519151, 50.691 },
            { 0, 0, 0, 30, 30, 0.700155, 50.978 },
            { -100, 0, 79 * 1.293566, 0, 0, 1.000000, 28.450 },
        };
        for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
            r = f_eval(CV_FC_PUCK, &p, 0, k[i].s2, k[i].s3, k[i].t21, k[i].t23, k[i].t31);
            g_checks++;
            if (!(fabs(r.fi - k[i].fe) < 2e-5 && fabs(fabs(r.angle) - k[i].th) < 0.01)) {
                g_fail++;
                fprintf(stderr, "%s:%d: Puck case %d: fE %.6f at %.3f, not %.6f at %.3f\n", __FILE__, __LINE__,
                        (int)i, r.fi, r.angle, k[i].fe, k[i].th);
            }
        }
        r = f_eval(CV_FC_PUCK, &p, 0, -100, -100, 0, 0, 0);    /* hydrostatic across the fibres */
        CHECK(!(r.fe_m > 1e-6));
        r = f_eval(CV_FC_PUCK, &p, 0, 48, 0, 0, 0, 0);
        CHECK_EQ(r.mode, CV_FM_IFFA);
        r = f_eval(CV_FC_PUCK, &p, 0, -50, 0, 100, 0, 0);
        CHECK_EQ(r.mode, CV_FM_IFFB);
        r = f_eval(CV_FC_PUCK, &p, 0, -150, 0, 60, 0, 0);
        CHECK_EQ(r.mode, CV_FM_IFFC);
        /* weakening by s1 (s = m = 0.5): c = 0.768344, eta = 0.700675 */
        r = f_eval(CV_FC_PUCK, &p, -1000, -100, 0, 20, 0, 0);
        CHECK_NEAR(r.fe_m, 0.740931, 2e-5);
        CHECK_NEAR(r.fe_f, 1000 / 1480., 1e-6);
        /* the refined fibre fracture (eq. 21) */
        p.E1f = 230000; p.nu12f = 0.2; p.msf = 1.1;
        r = f_eval(CV_FC_PUCK, &p, 1000, 50, 0, 0, 0, 0);
        CHECK_NEAR(r.fe_f, (1000 - (0.28 - 0.2 * 1.1 * 126000 / 230000.) * 50) / 1950, 1e-6);
    }

    /* LaRC03 (NASA/TM-2003-212663) */
    {
        cv_fmat l;
        cv_fmat_defaults(&l, CV_MK_UD);       /* the kink example: Xc 900, SL 80, Yc 200 */
        l.E1 = 100000; l.E2 = 9000; l.G12 = 5000; l.nu12 = 0.3;
        l.Xt = 1500; l.Xc = 900; l.Yt = 50; l.Yc = 200; l.S12 = 80;
        r = f_eval(CV_FC_LARC03, &l, -900, 0, 0, 0, 0, 0);
        CHECK_NEAR(r.fi, 1, 1e-9); CHECK_NEAR(r.rf, 1, 1e-6); CHECK_EQ(r.mode, CV_FM_FKC);
        /* the shear's sign does not matter */
        cv_fres a = f_eval(CV_FC_LARC03, &l, -500, -10, 0, 30, 0, 0), b = f_eval(CV_FC_LARC03, &l, -500, -10, 0, -30, 0, 0);
        CHECK_NEAR(a.fi, b.fi, 1e-9); CHECK_NEAR(a.rf, b.rf, 1e-6);
        /* transverse compression breaks at alpha0 */
        r = f_eval(CV_FC_LARC03, &l, 0, -200, 0, 0, 0, 0);
        CHECK_NEAR(r.fi, 1, 1e-6); CHECK_NEAR(r.angle, 53, 0.01); CHECK_EQ(r.mode, CV_FM_MC);
        /* continuous at s22 = 0 */
        a = f_eval(CV_FC_LARC03, &l, 0, 1e-9, 0, 40, 0, 0); b = f_eval(CV_FC_LARC03, &l, 0, -1e-9, 0, 40, 0, 0);
        CHECK_NEAR(a.fi, 0.25, 1e-6); CHECK_NEAR(b.fi, 0.25, 1e-6);
        /* AS4/3502 (table 3, SL in-situ 95.1): fails at Xc */
        l.E1 = 127600; l.E2 = 11300; l.G12 = 6000; l.nu12 = 0.278; l.Xc = 1045; l.Yc = 244; l.S12 = 95.1;
        r = f_eval(CV_FC_LARC03, &l, -1045, 0, 0, 0, 0, 0);
        CHECK_NEAR(r.fi, 1, 1e-9);
        /* E-glass/LY556 (table 2), thick embedded: YT 57.021, SL 86.267, ST 51.995 */
        l.E1 = 53480; l.E2 = 17700; l.G12 = 5830; l.nu12 = 0.278; l.Yt = 36; l.Yc = 138; l.S12 = 61;
        cv_fply ply = { 0, CV_PLY_EMBEDDED };
        double yt, sl, st;
        cv_larc_insitu(&l, &ply, &yt, &sl, &st);
        CHECK_REL(yt, 57.021, 1e-4); CHECK_REL(sl, 86.267, 1e-4); CHECK_REL(st, 51.995, 1e-4);
        double s[6] = { 0, 57.021 };
        cv_fail_eval(CV_FC_LARC03, &l, &ply, s, &r);
        CHECK_NEAR(r.fi, 1, 1e-4);
        /* thin embedded with the toughness: sqrt(8 GIc / (pi t Lambda22)) */
        ply.t = 0.05; l.GIc = 0.258; l.GIIc = 1.08;
        cv_larc_insitu(&l, &ply, &yt, &sl, &st);
        double nu21 = 0.278 * 17700 / 53480, L22 = 2 * (1 / 17700. - nu21 * nu21 / 53480);
        CHECK_REL(yt, sqrt(8 * 0.258 / (3.14159265358979 * 0.05 * L22)), 1e-9);
        CHECK_REL(sl, sqrt(8 * 5830 * 1.08 / (3.14159265358979 * 0.05)), 1e-9);
        ply.pos = CV_PLY_UD;
        cv_larc_insitu(&l, &ply, &yt, &sl, &st);
        CHECK_EQ(yt, 36); CHECK_EQ(sl, 61);
    }

    /* LaRC05: the strengths, the fracture angle under transverse compression */
    r = f_eval(CV_FC_LARC05, &m, 0, -200, 0, 0, 0, 0);
    CHECK_NEAR(fabs(r.angle - 90), 37, 0.01);                /* 53 or 127: the same plane pair */
    r = f_eval(CV_FC_LARC05, &m, -600, 0, 0, 0, 0, 0);
    CHECK_EQ(r.mode, CV_FM_SPLIT);
    r = f_eval(CV_FC_LARC05, &m, -1400, 0, 0, 0, 0, 0);
    CHECK_EQ(r.mode, CV_FM_FC);
    r = f_eval(CV_FC_LARC05, &m, 0, 0, 0, 0, 30, 0);       /* transverse shear: tension at 45 degrees */
    CHECK_NEAR(fabs(r.angle - 90), 45, 0.01);

    /* every criterion: the index is 1 at the state scaled by its reserve factor, and the
       reserve factor scales inversely with the load */
    {
        uint32_t seed = 12345;
        cv_fmat iso;
        cv_fmat_defaults(&iso, CV_MK_ISO);
        iso.Sy = 250; iso.Sut = 300; iso.Suc = 900;
        int bad = 0;
        for (int it = 0; it < 300; it++) {
            double s[6], t[6];
            for (int i = 0; i < 6; i++) {
                seed = seed * 1664525u + 1013904223u;
                double u = (seed >> 8) / 16777216.0 * 2 - 1;
                s[i] = u * (i == 0 ? 1500 : i < 3 ? 120 : 60);
            }
            for (int c = 0; c < CV_FC_N; c++) {
                const cv_fmat* mm = cv_fc_ud(c) ? &m : &iso;
                cv_fres a, b;
                cv_fail_eval(c, mm, NULL, s, &a);
                if (!(a.rf > 0 && a.rf < 1e6)) continue;
                for (int i = 0; i < 6; i++) t[i] = s[i] * a.rf;
                cv_fail_eval(c, mm, NULL, t, &b);
                for (int i = 0; i < 6; i++) t[i] = s[i] * 2;
                cv_fres d;
                cv_fail_eval(c, mm, NULL, t, &d);
                bool ok = fabs(b.rf - 1) < 1e-4 && fabs(d.rf * 2 / a.rf - 1) < 1e-4 && fabs(b.fi - 1) < 1e-3;
                if (!ok && bad++ < 10)
                    fprintf(stderr, "%s:%d: %s at (%g %g %g %g %g %g): rf %g, then rf %g fi %g, twice rf %g\n",
                            __FILE__, __LINE__, cv_fc_name(c), s[0], s[1], s[2], s[3], s[4], s[5], a.rf, b.rf, b.fi, d.rf);
            }
        }
        CHECK_EQ(bad, 0);
    }

    /* isotropic */
    {
        cv_fmat i;
        cv_fmat_defaults(&i, CV_MK_ISO);
        i.Sy = 250; i.Sut = 100; i.Suc = 300;
        CHECK_NEAR(f_eval(CV_FC_MISES, &i, 250, 0, 0, 0, 0, 0).rf, 1, 1e-6);
        CHECK_NEAR(f_eval(CV_FC_MISES, &i, 0, 0, 0, 100, 0, 0).rf, 250 / (sqrt(3.) * 100), 1e-6);
        CHECK_NEAR(f_eval(CV_FC_TRESCA, &i, 0, 0, 0, 100, 0, 0).rf, 1.25, 1e-6);
        CHECK_NEAR(f_eval(CV_FC_TRESCA, &i, 100, -100, 50, 0, 0, 0).fi, 0.8, 1e-6);
        CHECK_NEAR(f_eval(CV_FC_MOHR, &i, -300, 0, 0, 0, 0, 0).rf, 1, 1e-6);
        CHECK_NEAR(f_eval(CV_FC_MOHR, &i, 0, 0, 0, 30, 0, 0).fi, 30 / 100. + 30 / 300., 1e-6);
        CHECK_NEAR(f_eval(CV_FC_MOHR, &i, 50, 20, 0, 0, 0, 0).fi, 0.5, 1e-6);
        CHECK(!(f_eval(CV_FC_HASHIN, &i, 50, 0, 0, 0, 0, 0).fi == f_eval(CV_FC_HASHIN, &i, 50, 0, 0, 0, 0, 0).fi));
        char why[96];
        CHECK(!cv_fmat_valid(&i, CV_FC_PUCK, why, sizeof why));
        CHECK(strstr(why, "UD") != NULL);
    }

    /* the material as text and back */
    {
        char buf[1024];
        cv_fmat a = m, b;
        a.S23 = 70.5; a.GIc = 0.2;
        snprintf(a.name, sizeof a.name, "AS4");
        cv_fmat_format(&a, buf, sizeof buf);
        memset(&b, 0, sizeof b);
        snprintf(b.name, sizeof b.name, "AS4");
        CHECK(cv_fmat_parse(buf, &b));
        CHECK(memcmp(&a, &b, sizeof a) == 0);
        CHECK(strstr(buf, "Sy=") == NULL);
        CHECK(cv_fmat_parse("kind=iso Sy=355", &b));
        CHECK_EQ(b.kind, CV_MK_ISO); CHECK_EQ(b.Sy, 355); CHECK_EQ(b.Xt, 0);
        CHECK(!cv_fmat_parse("nothing here", &b));
    }

    /* templates: complete for every criterion of their kind */
    {
        const cv_ftemplate* t;
        int n = cv_ftemplates(&t);
        for (int i = 0; i < n; i++)
            for (int c = 0; c < CV_FC_N; c++) {
                if (cv_fc_ud(c) != (t[i].m.kind == CV_MK_UD)) continue;
                char why[96];
                g_checks++;
                if (!cv_fmat_valid(&t[i].m, c, why, sizeof why)) {
                    g_fail++;
                    fprintf(stderr, "%s:%d: template %s: %s\n", __FILE__, __LINE__, t[i].m.name, why);
                }
            }
    }

    /* auto: the criterion per material, the template by name and by elastic data */
    {
        const cv_ftemplate* t;
        cv_ftemplates(&t);
        int s235 = cv_ftemplate_by_name("S235", -1), im7 = cv_ftemplate_by_name("IM7/8552", -1);
        int gjl = cv_ftemplate_by_name("EN-GJL-250", -1), pa = cv_ftemplate_by_name("pa66", -1);
        CHECK(s235 >= 0 && im7 >= 0 && gjl >= 0 && pa >= 0);
        CHECK_EQ(cv_fc_auto(&t[s235].m), CV_FC_MISES);
        CHECK_EQ(cv_fc_auto(&t[gjl].m), CV_FC_MOHR);
        CHECK_EQ(cv_fc_auto(&t[im7].m), CV_FC_LARC05);
        cv_fmat u;
        cv_fmat_defaults(&u, CV_MK_UD);
        CHECK_EQ(cv_fc_auto(&u), -1);
        CHECK(!cv_fmat_valid(&u, CV_FC_AUTO, NULL, 0));
        u.Xt = 2000; u.Xc = 1200; u.Yt = 50; u.Yc = 200; u.S12 = 90;      /* no E: not LaRC */
        CHECK_EQ(cv_fc_auto(&u), CV_FC_PUCK);
        /* auto evaluates as the criterion it picks */
        const double st[6] = { 300, -50, 0, 80, 0, 0 };
        cv_fres a, b;
        cv_fail_eval(CV_FC_AUTO, &t[s235].m, NULL, st, &a);
        cv_fail_eval(CV_FC_MISES, &t[s235].m, NULL, st, &b);
        CHECK_NEAR(a.rf, b.rf, 1e-6);
        CHECK(a.rf > 0 && a.rf < 1);
        /* names: case, punctuation, the particular before the general */
        CHECK_EQ(cv_ftemplate_by_name("Steel_S355-plate", -1), cv_ftemplate_by_name("S355", -1));
        CHECK(!strcmp(t[cv_ftemplate_by_name("STEEL", -1)].m.name, "S235"));
        CHECK(!strcmp(t[cv_ftemplate_by_name("stainless steel", -1)].m.name, "1.4301 (304)"));
        CHECK(!strcmp(t[cv_ftemplate_by_name("as4-peek ply", -1)].m.name, "AS4/PEEK (APC-2)"));
        CHECK(!strcmp(t[cv_ftemplate_by_name("PEEK housing", -1)].m.name, "PEEK"));
        CHECK(!strcmp(t[cv_ftemplate_by_name("pa6", -1)].m.name, "PA66"));
        CHECK(!strcmp(t[cv_ftemplate_by_name("cfrp", -1)].m.name, "IM7/8552"));
        CHECK_EQ(cv_ftemplate_by_name("cfrp", CV_MK_ISO), -1);           /* a quasi-isotropic deck material */
        CHECK_EQ(cv_ftemplate_by_name("MAT1", -1), -1);
        CHECK_EQ(cv_ftemplate_by_name("", -1), -1);
        /* elastic data: metal or plastic by E, a steel by its yield, a ply by E1 and E2 */
        CHECK(!strcmp(t[cv_ftemplate_nearest(CV_MK_ISO, 210000, 0, 0.3, 0)].m.name, "S235"));
        CHECK(!strcmp(t[cv_ftemplate_nearest(CV_MK_ISO, 210000, 0, 0.3, 360)].m.name, "S355"));
        CHECK(!strcmp(t[cv_ftemplate_nearest(CV_MK_ISO, 70000, 0, 0.33, 0)].m.name, "Al 6061-T6"));
        CHECK(!strcmp(t[cv_ftemplate_nearest(CV_MK_ISO, 70000, 0, 0.33, 450)].m.name, "Al 7075-T6"));
        CHECK(!strcmp(t[cv_ftemplate_nearest(CV_MK_ISO, 110000, 0, 0.25, 0)].m.name, "EN-GJL-250"));
        CHECK(!strcmp(t[cv_ftemplate_nearest(CV_MK_ISO, 3600, 0, 0.4, 0)].m.name, "PEEK"));
        CHECK(!strcmp(t[cv_ftemplate_nearest(CV_MK_ISO, 1400, 0, 0.4, 0)].m.name, "PP"));
        CHECK(!strcmp(t[cv_ftemplate_nearest(CV_MK_UD, 45000, 16000, 0.28, 0)].m.name, "E-glass/MY750"));
        CHECK_EQ(t[cv_ftemplate_nearest(CV_MK_UD, 170000, 9000, 0.3, 0)].m.kind, CV_MK_UD);
        CHECK_EQ(cv_ftemplate_nearest(CV_MK_ISO, 0, 0, 0.3, 0), -1);
        CHECK(!strcmp(cv_fc_name(CV_FC_AUTO), "AUTO"));
    }

    /* the deck: elastic constants, the first yield stress, layer thickness and material */
    {
        cv_inp d;
        const char* deck =
            "*NODE\n1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n"
            "*ELEMENT, TYPE=S4, ELSET=E\n1,1,2,3,4\n"
            "*ORIENTATION, NAME=OR\n1.,0.,0.,0.,1.,0.\n"
            "*MATERIAL, NAME=Ply\n*ELASTIC, TYPE=ENGINEERING CONSTANTS\n"
            "135000,10000,10000,0.3,0.3,0.45,5000,5000,\n3800,290.\n"
            "*MATERIAL, NAME=STEEL\n*ELASTIC\n210000,.3,290.\n210000,.3,400.\n*PLASTIC\n250,0\n300,.1\n"
            "*SHELL SECTION, ELSET=E, COMPOSITE\n0.125,,PLY,OR\n0.25,,STEEL,OR\n"
            "*STEP\n*STATIC\n*END STEP\n";
        CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
        CHECK_EQ(d.nmats, 2);
        if (d.nmats == 2 && d.mprop) {
            CHECK_EQ(d.mprop[0].el, CV_EL_ENG);
            CHECK_EQ(d.mprop[0].c[0], 135000); CHECK_EQ(d.mprop[0].c[6], 5000); CHECK_EQ(d.mprop[0].c[8], 3800);
            CHECK_EQ(d.mprop[1].el, CV_EL_ISO);
            CHECK_EQ(d.mprop[1].c[0], 210000); CHECK_NEAR(d.mprop[1].c[1], 0.3, 1e-7);
            CHECK_EQ(d.mprop[1].sy, 250);
            CHECK_EQ(d.mprop[0].sy, 0);
        }
        CHECK_EQ(d.ncomps, 1);
        if (d.ncomps == 1) {
            uint32_t k = d.comps[0].lay0;
            CHECK_NEAR(d.layer_t[k], 0.125, 1e-7); CHECK_NEAR(d.layer_t[k + 1], 0.25, 1e-7);
            CHECK_EQ(d.layer_mat[k], 0); CHECK_EQ(d.layer_mat[k + 1], 1);
        }
        /* the .frd: the shell, its layers numbered on (2, 3) */
        const uint32_t eid[3] = { 1, 2, 3 };
        const double at[3][3] = { { 0, 0, 0 }, { 0, 0, 2 }, { 0, 0, 4 } };
        buf_t b = own_hexes(3, eid, at);
        cv_frd f;
        cv_elemmap em;
        CHECK(cv_frd_parse(&f, b.p, b.n));
        CHECK(cv_elemmap_init(&em, &d, &f));
        int lip = 0;
        uint32_t e3 = cv_frd_elem_index(&f, 3), e2 = cv_frd_elem_index(&f, 2);
        CHECK_EQ(cv_elemmap_dat(&em, &d, 1, 5, 8, &lip), e3); CHECK_EQ(lip, 1);
        CHECK_EQ(cv_elemmap_dat(&em, &d, 1, 4, 8, &lip), e2); CHECK_EQ(lip, 4);
        CHECK_EQ(cv_elemmap_dat(&em, &d, 7, 1, 8, &lip), UINT32_MAX);
        float th;
        int lay;
        CHECK_EQ(cv_elemmap_mat(&em, &d, e3, &th, &lay), 1);
        CHECK_NEAR(th, 0.25, 1e-7); CHECK_EQ(lay, 1);
        cv_elemmap_free(&em); cv_frd_free(&f); free(f.msgs.a); free(b.p);
        cv_inp_free(&d); free(d.msgs.a);
    }
}
