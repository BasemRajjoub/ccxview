/* t_units.h -- unit systems and conversions (units.c): what a file system's units
   are called, known factors against hand values, metric <-> imperial both ways,
   temperature offsets, and which quantity a field is. Included by test_main.c. */
#include "../src/units.h"
#include <string.h>

/* value v of q in the file units of sys, shown in unit `show` */
static double u_conv(int sys, int temp, int q, const char* show, double v) {
    int i = cv_unit_find(q, show);
    if (i < 0) { fprintf(stderr, "    no unit %s\n", show); return NAN; }
    double k, off;
    cv_unit_conv(sys, temp, -1, q, i, &k, &off);
    return v * k + off;
}

/* the name of q's unit in a file system */
static const char* u_file(int sys, int temp, int q) {
    const cv_unit* u = cv_unit_get(q, cv_sys_unit(sys, temp, q));
    return u ? u->name : "(none)";
}

#define CHECK_REL(a, b, r) CHECK_NEAR((a) / (b), 1.0, r)
#define CHECK_UNIT(sys, q, name) do { g_checks++; const char* _n = u_file(sys, cv_sys_temp(sys), q); \
    if (strcmp(_n, name)) { g_fail++; fprintf(stderr, "%s:%d: %s of %s is %s, not %s\n", __FILE__, __LINE__, \
    cv_quantity_name(q), cv_sys_name(sys), _n, name); } } while (0)

static void test_units(void) {
    for (int q = 0; q < CV_Q_N; q++) {                 /* every quantity has a file key, each its own */
        CHECK(cv_quantity_key(q) && cv_quantity_key(q)[0]);
        for (int r = 0; r < q; r++) CHECK(strcmp(cv_quantity_key(q), cv_quantity_key(r)) != 0);
    }
    CHECK(!strcmp(cv_quantity_key(CV_Q_FLUX), "heat_flux"));
    CHECK(!strcmp(cv_quantity_key(CV_Q_N), ""));
    /* every system names every quantity with a listed unit */
    for (int s = 1; s < CV_SYS_N; s++)
        for (int q = 0; q < CV_Q_N; q++) {
            g_checks++;
            if (cv_sys_unit(s, cv_sys_temp(s), q) < 0) {
                g_fail++; fprintf(stderr, "%s:%d: %s has no unit in %s\n", __FILE__, __LINE__, cv_quantity_name(q), cv_sys_name(s));
            }
        }
    /* every preset names real units */
    for (int p = 1; p < CV_SHOW_N; p++)
        for (int q = 0; q < CV_Q_N; q++) CHECK(cv_show_unit(p, q) >= 0);
    for (int q = 0; q < CV_Q_N; q++) CHECK_EQ(cv_show_unit(CV_SHOW_FILE, q), -1);
    /* no unit listed twice by name */
    for (int q = 0; q < CV_Q_N; q++)
        for (int i = 0; i < cv_unit_count(q); i++) CHECK_EQ(cv_unit_find(q, cv_unit_get(q, i)->name), i);

    /* what each system's results are in */
    CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_LEN, "mm");        CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_STRESS, "MPa");
    CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_FORCE, "N");       CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_ENERGY_D, "mJ/mm³");
    CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_FLUX, "mW/mm²");   CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_POWER, "mW");
    CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_ENERGY, "mJ");     CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_MASS, "t");
    CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_VELO, "mm/s");     CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_ACC, "mm/s²");
    CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_MASSFLOW, "t/s");  CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_VOLUME, "mm³");
    CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_TEMP, "°C");       CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_STRAIN, "");
    CHECK_UNIT(CV_SYS_M_KG_S, CV_Q_LEN, "m");         CHECK_UNIT(CV_SYS_M_KG_S, CV_Q_STRESS, "Pa");
    CHECK_UNIT(CV_SYS_M_KG_S, CV_Q_FORCE, "N");       CHECK_UNIT(CV_SYS_M_KG_S, CV_Q_ENERGY_D, "J/m³");
    CHECK_UNIT(CV_SYS_M_KG_S, CV_Q_FLUX, "W/m²");     CHECK_UNIT(CV_SYS_M_KG_S, CV_Q_ENERGY, "J");
    CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_LEN, "in");      CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_STRESS, "psi");
    CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_FORCE, "lbf");   CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_ENERGY_D, "in·lbf/in³");
    CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_FLUX, "in·lbf/(s·in²)"); CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_POWER, "in·lbf/s");
    CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_ENERGY, "in·lbf"); CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_MASS, "lbf·s²/in");
    CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_TEMP, "°F");     CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_VELO, "in/s");
    CHECK_UNIT(CV_SYS_MM_KG_MS, CV_Q_FORCE, "kN");    CHECK_UNIT(CV_SYS_MM_KG_MS, CV_Q_STRESS, "GPa");
    /* per width: a moment per width is named by the system's length, though N·m/m = N·mm/mm */
    CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_FORCE_LEN, "N/mm");      CHECK_UNIT(CV_SYS_MM_T_S, CV_Q_MOMENT_LEN, "N·mm/mm");
    CHECK_UNIT(CV_SYS_M_KG_S, CV_Q_FORCE_LEN, "N/m");       CHECK_UNIT(CV_SYS_M_KG_S, CV_Q_MOMENT_LEN, "N·m/m");
    CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_FORCE_LEN, "lbf/in");  CHECK_UNIT(CV_SYS_IN_LBF_S, CV_Q_MOMENT_LEN, "lbf·in/in");
    CHECK_UNIT(CV_SYS_FT_SLUG_S, CV_Q_MOMENT_LEN, "lbf·ft/ft");
    CHECK_UNIT(CV_SYS_MM_KG_MS, CV_Q_FORCE_LEN, "kN/mm");   CHECK_UNIT(CV_SYS_MM_KG_MS, CV_Q_MOMENT_LEN, "kN·mm/mm");
    CHECK_UNIT(CV_SYS_CM_G_S, CV_Q_MOMENT_LEN, "dyn·cm/cm");
    CHECK_NEAR(u_conv(CV_SYS_MM_T_S, CV_TEMP_C, CV_Q_FORCE_LEN, "kN/m", 50), 50, 1e-12);
    CHECK_NEAR(u_conv(CV_SYS_MM_T_S, CV_TEMP_C, CV_Q_MOMENT_LEN, "kN·m/m", 100), 0.1, 1e-12);
    CHECK_REL(u_conv(CV_SYS_MM_T_S, CV_TEMP_C, CV_Q_FORCE_LEN, "lbf/in", 1), 5.71014715, 1e-8);
    CHECK_UNIT(CV_SYS_MM_KG_MS, CV_Q_ENERGY, "J");    CHECK_UNIT(CV_SYS_MM_KG_MS, CV_Q_VELO, "m/s");
    CHECK_UNIT(CV_SYS_MM_KG_MS, CV_Q_ACC, "mm/ms²");  CHECK_UNIT(CV_SYS_MM_KG_MS, CV_Q_POWER, "kW");
    CHECK_UNIT(CV_SYS_MM_G_MS, CV_Q_FORCE, "N");      CHECK_UNIT(CV_SYS_MM_G_MS, CV_Q_STRESS, "MPa");
    CHECK_UNIT(CV_SYS_CM_G_S, CV_Q_FORCE, "dyn");     CHECK_UNIT(CV_SYS_CM_G_S, CV_Q_STRESS, "dyn/cm²");
    CHECK_UNIT(CV_SYS_CM_G_S, CV_Q_ENERGY, "erg");
    CHECK_UNIT(CV_SYS_FT_SLUG_S, CV_Q_LEN, "ft");     CHECK_UNIT(CV_SYS_FT_SLUG_S, CV_Q_STRESS, "psf");
    CHECK_UNIT(CV_SYS_FT_SLUG_S, CV_Q_FORCE, "lbf");  CHECK_UNIT(CV_SYS_FT_SLUG_S, CV_Q_MASS, "slug");
    CHECK_UNIT(CV_SYS_FT_SLUG_S, CV_Q_ENERGY, "ft·lbf");
    CHECK(cv_sys_unit(CV_SYS_NONE, 0, CV_Q_STRESS) < 0);

    /* metric -> imperial, against published values */
    const int MM = CV_SYS_MM_T_S, SI = CV_SYS_M_KG_S, IN = CV_SYS_IN_LBF_S, FT = CV_SYS_FT_SLUG_S;
    const double r = 1e-9;
    CHECK_REL(u_conv(MM, 0, CV_Q_STRESS, "psi", 1), 145.03773773, r);
    CHECK_REL(u_conv(MM, 0, CV_Q_STRESS, "ksi", 250), 36.259434433, r);
    CHECK_REL(u_conv(MM, 0, CV_Q_STRESS, "Pa", 1), 1e6, r);
    CHECK_REL(u_conv(MM, 0, CV_Q_STRESS, "bar", 1), 10, r);
    CHECK_REL(u_conv(MM, 0, CV_Q_STRESS, "kgf/cm²", 98.0665), 1000, r);
    CHECK_REL(u_conv(MM, 0, CV_Q_LEN, "in", 25.4), 1, r);
    CHECK_REL(u_conv(MM, 0, CV_Q_LEN, "ft", 304.8), 1, r);
    CHECK_REL(u_conv(MM, 0, CV_Q_LEN, "µm", 1), 1000, r);
    CHECK_REL(u_conv(MM, 0, CV_Q_FORCE, "lbf", 4.4482216152605), 1, r);
    CHECK_REL(u_conv(MM, 0, CV_Q_FORCE, "kip", 4448.2216152605), 1, r);
    CHECK_REL(u_conv(MM, 0, CV_Q_FORCE, "kgf", 9.80665), 1, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_VELO, "km/h", 1), 3.6, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_VELO, "mph", 0.44704), 1, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_ACC, "g", 9.80665), 1, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_FLUX, "BTU/(h·ft²)", 3.1545907), 1, 1e-7);
    CHECK_REL(u_conv(SI, 0, CV_Q_POWER, "hp", 745.69987158), 1, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_POWER, "BTU/h", 0.29307107017), 1, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_ENERGY, "kWh", 3.6e6), 1, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_ENERGY, "ft·lbf", 1.3558179483), 1, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_MASS, "lb", 0.45359237), 1, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_MASS, "slug", 14.593902937), 1, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_VOLUME, "l", 1), 1000, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_VOLUME, "gal", 3.785411784e-3), 1, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_VOLUME, "in³", 1.6387064e-5), 1, r);
    CHECK_REL(u_conv(SI, 0, CV_Q_MASSFLOW, "lb/h", 1), 3600 / 0.45359237, r);
    CHECK_REL(u_conv(MM, 0, CV_Q_ENERGY_D, "J/m³", 1), 1e6, r);       /* mJ/mm³ */
    CHECK_REL(u_conv(MM, 0, CV_Q_FLUX, "W/m²", 1), 1e3, r);           /* mW/mm² */
    CHECK_REL(u_conv(MM, 0, CV_Q_MASS, "kg", 1), 1e3, r);             /* t */
    CHECK_REL(u_conv(CV_SYS_MM_KG_MS, 0, CV_Q_STRESS, "MPa", 1), 1e3, r);   /* GPa */
    CHECK_REL(u_conv(CV_SYS_CM_G_S, 0, CV_Q_FORCE, "N", 1e5), 1, r);        /* dyn */

    /* imperial -> metric */
    CHECK_REL(u_conv(IN, CV_TEMP_F, CV_Q_STRESS, "MPa", 1), 6.894757293168e-3, r);
    CHECK_REL(u_conv(IN, CV_TEMP_F, CV_Q_STRESS, "Pa", 1), 6894.757293168, r);
    CHECK_REL(u_conv(IN, CV_TEMP_F, CV_Q_STRESS, "ksi", 36000), 36, r);
    CHECK_REL(u_conv(IN, CV_TEMP_F, CV_Q_LEN, "mm", 1), 25.4, r);
    CHECK_REL(u_conv(IN, CV_TEMP_F, CV_Q_LEN, "ft", 12), 1, r);
    CHECK_REL(u_conv(IN, CV_TEMP_F, CV_Q_FORCE, "N", 1), 4.4482216152605, r);
    CHECK_REL(u_conv(IN, CV_TEMP_F, CV_Q_ENERGY, "J", 1), 0.1129848290276, r);
    CHECK_REL(u_conv(IN, CV_TEMP_F, CV_Q_ENERGY_D, "J/m³", 1), 6894.757293168, r);
    CHECK_REL(u_conv(IN, CV_TEMP_F, CV_Q_MASS, "kg", 1), 175.12683524647, r);
    CHECK_REL(u_conv(IN, CV_TEMP_F, CV_Q_VELO, "m/s", 1), 0.0254, r);
    CHECK_REL(u_conv(FT, CV_TEMP_F, CV_Q_STRESS, "psi", 144), 1, r);
    CHECK_REL(u_conv(FT, CV_TEMP_F, CV_Q_STRESS, "Pa", 1), 47.880258980, r);
    CHECK_REL(u_conv(FT, CV_TEMP_F, CV_Q_MASS, "lb", 1), 32.174048556, r);   /* a slug */
    CHECK_REL(u_conv(FT, CV_TEMP_F, CV_Q_VOLUME, "in³", 1), 1728, r);

    /* temperatures: scale and offset, both ways */
    const int T = CV_Q_TEMP;
    CHECK_NEAR(u_conv(MM, CV_TEMP_C, T, "°F", 100), 212, 1e-9);
    CHECK_NEAR(u_conv(MM, CV_TEMP_C, T, "°F", 0), 32, 1e-9);
    CHECK_NEAR(u_conv(MM, CV_TEMP_C, T, "°F", -40), -40, 1e-9);
    CHECK_NEAR(u_conv(MM, CV_TEMP_C, T, "K", 20), 293.15, 1e-9);
    CHECK_NEAR(u_conv(MM, CV_TEMP_C, T, "°R", 0), 491.67, 1e-9);
    CHECK_NEAR(u_conv(IN, CV_TEMP_F, T, "°C", 212), 100, 1e-9);
    CHECK_NEAR(u_conv(IN, CV_TEMP_F, T, "°C", 32), 0, 1e-9);
    CHECK_NEAR(u_conv(IN, CV_TEMP_F, T, "K", 32), 273.15, 1e-9);
    CHECK_NEAR(u_conv(SI, CV_TEMP_K, T, "°C", 0), -273.15, 1e-9);
    CHECK_NEAR(u_conv(SI, CV_TEMP_K, T, "°F", 0), -459.67, 1e-9);
    CHECK_NEAR(u_conv(SI, CV_TEMP_K, T, "°R", 300), 540, 1e-9);
    CHECK_NEAR(u_conv(SI, CV_TEMP_R, T, "K", 540), 300, 1e-9);
    CHECK_NEAR(u_conv(SI, CV_TEMP_R, T, "°F", 459.67), 0, 1e-9);

    /* strain needs no system */
    CHECK_NEAR(u_conv(CV_SYS_NONE, 0, CV_Q_STRAIN, "µε", 1.5e-3), 1500, 1e-9);
    CHECK_NEAR(u_conv(CV_SYS_NONE, 0, CV_Q_STRAIN, "%", 0.02), 2, 1e-12);
    CHECK_NEAR(u_conv(CV_SYS_NONE, 0, CV_Q_STRAIN, "‰", 0.002), 2, 1e-12);
    /* no system: nothing else converts */
    { double k, off; CHECK(!cv_unit_conv(CV_SYS_NONE, 0, -1, CV_Q_STRESS, 0, &k, &off)); CHECK(k == 1 && off == 0); }
    /* shown as in the file, or the file's own unit picked: no work */
    { double k, off; CHECK(!cv_unit_conv(MM, 0, -1, CV_Q_STRESS, -1, &k, &off));
      CHECK(!cv_unit_conv(MM, 0, -1, CV_Q_STRESS, cv_unit_find(CV_Q_STRESS, "MPa"), &k, &off)); }
    /* the same value under another name still converts by 1 */
    CHECK_NEAR(u_conv(MM, 0, CV_Q_STRESS, "N/mm²", 123), 123, 1e-9);
    CHECK_NEAR(u_conv(MM, 0, CV_Q_ENERGY_D, "MJ/m³", 7), 7, 1e-9);

    /* every system to every other and back, every quantity and temperature scale:
       a value in A shown in B's unit, read as B's file value and shown in A's unit */
    const double vals[] = { 1, -273.15, 1234.5678, 1e-7, -3.5e9 };
    for (int a = 1; a < CV_SYS_N; a++)
        for (int b = 1; b < CV_SYS_N; b++)
            for (int ta = 0; ta < CV_TEMP_N; ta++)
                for (int q = 0; q < CV_Q_N; q++) {
                    int tb = (ta + b) % CV_TEMP_N;
                    int ub = cv_sys_unit(b, tb, q), ua = cv_sys_unit(a, ta, q);
                    double k1, o1, k2, o2;
                    cv_unit_conv(a, ta, -1, q, ub, &k1, &o1);
                    cv_unit_conv(b, tb, -1, q, ua, &k2, &o2);
                    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
                        double back = (vals[i] * k1 + o1) * k2 + o2;
                        g_checks++;
                        if (!(fabs(back - vals[i]) <= 1e-9 * fabs(vals[i]) + 1e-9)) {
                            g_fail++;
                            fprintf(stderr, "%s:%d: %s %g in %s -> %s and back = %.12g\n", __FILE__, __LINE__,
                                    cv_quantity_name(q), vals[i], cv_sys_name(a), cv_sys_name(b), back);
                        }
                    }
                }
    /* every unit of a quantity to every other and back, through the SI system */
    for (int q = 0; q < CV_Q_N; q++)
        for (int i = 0; i < cv_unit_count(q); i++)
            for (int j = 0; j < cv_unit_count(q); j++) {
                const cv_unit *ui = cv_unit_get(q, i), *uj = cv_unit_get(q, j);
                double x = 42.25, y = (x * ui->si + ui->off - uj->off) / uj->si;     /* i -> j */
                double z = (y * uj->si + uj->off - ui->off) / ui->si;                /* j -> i */
                CHECK_NEAR(z, x, 1e-9 * x);
                CHECK(ui->si > 0);
            }

    /* the quantity of a field */
    CHECK_EQ(cv_field_quantity("STRESS", 0), CV_Q_STRESS);
    CHECK_EQ(cv_field_quantity("SHELL", 0), CV_Q_FORCE_LEN);
    CHECK_EQ(cv_field_quantity("SHELL", 3), CV_Q_MOMENT_LEN);
    CHECK_EQ(cv_field_quantity("SHELL", 7), CV_Q_FORCE_LEN);
    CHECK_EQ(cv_field_quantity("SHELL", 8), -1);
    CHECK_EQ(cv_field_quantity("STRESS", -1), CV_Q_STRESS);
    CHECK_EQ(cv_field_quantity("DISP", 2), CV_Q_LEN);
    CHECK_EQ(cv_field_quantity("PDISP", 2), CV_Q_LEN);        /* an amplitude */
    CHECK_EQ(cv_field_quantity("PDISP", 3), -1);              /* a phase: degrees, not a length */
    CHECK_EQ(cv_field_quantity("PSTRESS", 5), CV_Q_STRESS);
    CHECK_EQ(cv_field_quantity("PSTRESS", 6), -1);
    CHECK_EQ(cv_field_quantity("TOSTRAIN", 0), CV_Q_STRAIN);
    CHECK_EQ(cv_field_quantity("PE", 0), CV_Q_STRAIN);
    CHECK_EQ(cv_field_quantity("NDTEMP", 0), CV_Q_TEMP);
    CHECK_EQ(cv_field_quantity("FORC", 1), CV_Q_FORCE);
    CHECK_EQ(cv_field_quantity("ENER", 0), CV_Q_ENERGY_D);
    CHECK_EQ(cv_field_quantity("HFL", 0), CV_Q_FLUX);
    CHECK_EQ(cv_field_quantity("FLUX", 2), CV_Q_FLUX);
    CHECK_EQ(cv_field_quantity("RFL", 0), CV_Q_POWER);
    CHECK_EQ(cv_field_quantity("VELO", 0), CV_Q_VELO);
    CHECK_EQ(cv_field_quantity("CONTACT", 0), CV_Q_LEN);
    CHECK_EQ(cv_field_quantity("CONTACT", 3), CV_Q_STRESS);
    CHECK_EQ(cv_field_quantity("SDV", 0), -1);
    CHECK_EQ(cv_field_quantity("ERROR", 0), -1);
    CHECK_EQ(cv_field_quantity("PEEQ", 0), CV_Q_STRAIN);
    CHECK_EQ(cv_field_quantity("PERROR", 0), -1);           /* a prefix alone is not a match */
    CHECK_EQ(cv_field_quantity("stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz)", 0), CV_Q_STRESS);
    CHECK_EQ(cv_field_quantity("internal energy density (elem, integ.pnt.)", 0), CV_Q_ENERGY_D);
    CHECK_EQ(cv_field_quantity("internal energy (element, integ.pnt.)", 0), CV_Q_ENERGY);
    CHECK_EQ(cv_field_quantity("total force (fx,fy,fz)", 0), CV_Q_FORCE);
    CHECK_EQ(cv_field_quantity(NULL, 0), -1);

    /* conversion of a vector field's values, as the app does it on decode */
    {
        int i = cv_unit_find(CV_Q_LEN, "in");
        double k, off;
        CHECK(cv_unit_conv(MM, 0, -1, CV_Q_LEN, i, &k, &off));
        float d[3] = { 25.4f, -50.8f, 0 };
        for (int c = 0; c < 3; c++) d[c] = (float)(d[c] * k + off);
        CHECK_NEAR(d[0], 1, 1e-6); CHECK_NEAR(d[1], -2, 1e-6); CHECK_NEAR(d[2], 0, 1e-9);
        /* the shape goes back to model units: 1 / k */
        for (int c = 0; c < 3; c++) d[c] = (float)(d[c] / k);
        CHECK_NEAR(d[0], 25.4, 1e-5); CHECK_NEAR(d[1], -50.8, 1e-5);
    }
    /* an input unit set by hand for one quantity: the others keep the system's */
    {
        int psi = cv_unit_find(CV_Q_STRESS, "psi"), mpa = cv_unit_find(CV_Q_STRESS, "MPa");
        int ksi = cv_unit_find(CV_Q_STRESS, "ksi"), kelv = cv_unit_find(CV_Q_TEMP, "K");
        int degc = cv_unit_find(CV_Q_TEMP, "°C"), degf = cv_unit_find(CV_Q_TEMP, "°F");
        int m = cv_unit_find(CV_Q_LEN, "m"), mm = cv_unit_find(CV_Q_LEN, "mm");
        double k, off;
        CHECK_EQ(cv_unit_input(MM, 0, -1, CV_Q_STRESS), mpa);
        CHECK_EQ(cv_unit_input(MM, 0, psi, CV_Q_STRESS), psi);
        CHECK_EQ(cv_unit_input(CV_SYS_NONE, 0, psi, CV_Q_STRESS), psi);      /* needs no system */
        CHECK_EQ(cv_unit_input(CV_SYS_NONE, 0, -1, CV_Q_STRESS), -1);
        CHECK_EQ(cv_unit_input(MM, 0, 999, CV_Q_STRESS), mpa);              /* out of range: the system's */
        /* psi in, MPa shown, and back */
        CHECK(cv_unit_conv(MM, 0, psi, CV_Q_STRESS, mpa, &k, &off));
        CHECK_NEAR(1000 * k + off, 6.894757293168, 1e-9);
        CHECK(cv_unit_conv(MM, 0, mpa, CV_Q_STRESS, psi, &k, &off));
        CHECK_NEAR(6.894757293168 * k + off, 1000, 1e-8);
        CHECK(cv_unit_conv(CV_SYS_NONE, 0, ksi, CV_Q_STRESS, psi, &k, &off));
        CHECK_NEAR(k, 1000, 1e-9); CHECK_NEAR(off, 0, 1e-12);
        /* same unit in and shown: nothing to do; shown -1 names the input unit */
        CHECK(!cv_unit_conv(MM, 0, psi, CV_Q_STRESS, psi, &k, &off));
        CHECK(!cv_unit_conv(MM, 0, psi, CV_Q_STRESS, -1, &k, &off));
        CHECK_EQ(cv_unit_shown(MM, 0, psi, CV_Q_STRESS, -1), psi);
        CHECK_EQ(cv_unit_shown(CV_SYS_NONE, 0, -1, CV_Q_STRESS, psi), -1);
        /* temperatures in K on a °C model, shown in °C and °F, and back */
        CHECK(cv_unit_conv(MM, CV_TEMP_C, kelv, CV_Q_TEMP, degc, &k, &off));
        CHECK_NEAR(293.15 * k + off, 20, 1e-9);
        CHECK(cv_unit_conv(MM, CV_TEMP_C, kelv, CV_Q_TEMP, degf, &k, &off));
        CHECK_NEAR(233.15 * k + off, -40, 1e-9);
        CHECK(cv_unit_conv(MM, CV_TEMP_C, degf, CV_Q_TEMP, kelv, &k, &off));
        CHECK_NEAR(-40 * k + off, 233.15, 1e-9);
        /* lengths in m on an mm model: the length factor alone changes */
        CHECK(cv_unit_conv(MM, 0, m, CV_Q_LEN, mm, &k, &off));
        CHECK_NEAR(k, 1000, 1e-9);
        CHECK(!cv_unit_conv(MM, 0, -1, CV_Q_LEN, mm, &k, &off));
        /* every input unit to every shown unit and back, for every quantity */
        for (int q = 0; q < CV_Q_N; q++)
            for (int a = 0; a < cv_unit_count(q); a++)
                for (int b = 0; b < cv_unit_count(q); b++) {
                    double k1, o1, k2, o2, v = q == CV_Q_TEMP ? 37.5 : -12.25;
                    cv_unit_conv(CV_SYS_NONE, 0, a, q, b, &k1, &o1);
                    cv_unit_conv(CV_SYS_NONE, 0, b, q, a, &k2, &o2);
                    CHECK_NEAR(((v * k1 + o1) * k2 + o2) / v, 1, 1e-12);
                    /* and against the SI values directly */
                    const cv_unit *ua = cv_unit_get(q, a), *ub = cv_unit_get(q, b);
                    CHECK_NEAR((v * k1 + o1) * ub->si + ub->off, v * ua->si + ua->off, 1e-9 * fabs(v * ua->si + ua->off) + 1e-12);
                }
    }
}
