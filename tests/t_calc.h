/* t_calc.h -- calculated fields (calc.c) on the two-hex file: names, maths, errors.
   Included by test_main.c after its helpers (two_hex). */
#include "../src/calc.h"

typedef struct { const cv_frd* f; float* v; int calls; } calc_ud;

static const float* calc_get(void* p, int step, int field) {
    calc_ud* u = p;
    const cv_field_desc* d = &u->f->steps[step].fields[field];
    free(u->v);
    u->v = malloc(sizeof(float) * CV_MAX(1, u->f->n_nodes) * (size_t)d->ncomp);
    if (u->v) cv_frd_read_field(u->f, d, u->v, NULL);
    u->calls++;
    return u->v;
}

/* expr at node i of step s; NaN when it does not compile or evaluate */
static double calc_at(const cv_frd* f, const char* expr, int s, uint32_t i) {
    char err[128];
    cv_calc* c = cv_calc_compile(f, expr, err, sizeof err);
    if (!c) { fprintf(stderr, "    calc \"%s\": %s\n", expr, err); return NAN; }
    calc_ud u = { f, NULL, 0 };
    float out = 0;
    bool ok = cv_calc_eval(c, f, s, calc_get, &u, &i, 1, &out);
    free(u.v);
    cv_calc_free(c);
    return ok ? out : NAN;
}

/* the message of a formula that must not compile */
static const char* calc_err(const cv_frd* f, const char* expr) {
    static char err[128];
    cv_calc* c = cv_calc_compile(f, expr, err, sizeof err);
    if (c) { cv_calc_free(c); return ""; }
    return err;
}

static void test_calc(void) {
    buf_t b = two_hex(0);
    cv_frd f;
    CHECK(cv_frd_parse(&f, b.p, b.n));
    if (f.n_nodes != 12 || f.n_steps != 2) { cv_frd_free(&f); free(f.msgs.a); free(b.p); CHECK(0); return; }
    /* node 5: xyz (2,1,0), D (-7.5, -2.25, 0.005), S = 50 -51 52 -53 54 -55 */
    CHECK_NEAR(calc_at(&f, "STRESS_SXX", 0, 5), 50, 1e-5);
    CHECK_NEAR(calc_at(&f, "stress_syy", 0, 5), -51, 1e-5);       /* any case */
    CHECK_NEAR(calc_at(&f, "SXX + SYY", 0, 5), -1, 1e-5);          /* components alone */
    CHECK_NEAR(calc_at(&f, "D1*2", 1, 5), -15, 1e-5);
    CHECK_NEAR(calc_at(&f, "DISP_MAG", 0, 5), sqrt(7.5 * 7.5 + 2.25 * 2.25 + 0.005 * 0.005), 1e-4);
    CHECK_NEAR(calc_at(&f, "DISP", 0, 5), calc_at(&f, "DISP_MAG", 0, 5), 1e-6);
    float s5[6] = { 50, -51, 52, -53, 54, -55 }, p[3];
    cv_principal(s5, false, p);
    CHECK_NEAR(calc_at(&f, "MISES", 0, 5), cv_von_mises(s5), 1e-3);
    CHECK_NEAR(calc_at(&f, "STRESS_MISES", 0, 5), cv_von_mises(s5), 1e-3);
    CHECK_NEAR(calc_at(&f, "S1", 0, 5), p[0], 1e-3);
    CHECK_NEAR(calc_at(&f, "STRESS_P3", 0, 5), p[2], 1e-3);
    CHECK_NEAR(calc_at(&f, "X + 10*Y + 100*Z", 0, 5), 12, 1e-6);
    CHECK_NEAR(calc_at(&f, "X + 10*Y + 100*Z", 0, 11), 2 + 10 + 100, 1e-6);
    CHECK_NEAR(calc_at(&f, "TIME", 1, 0), 1.0, 1e-6);
    /* the maths */
    CHECK_NEAR(calc_at(&f, "-2^2", 0, 0), -4, 1e-9);               /* -(2^2) */
    CHECK_NEAR(calc_at(&f, "2^3^2", 0, 0), 512, 1e-9);             /* 2^(3^2) */
    CHECK_NEAR(calc_at(&f, "1e3 + 2.5E-1", 0, 0), 1000.25, 1e-6);  /* not the name e3 */
    CHECK_NEAR(calc_at(&f, "ln(e) + log10(100)", 0, 0), 3, 1e-6);
    CHECK_NEAR(calc_at(&f, "LOG(E)", 0, 0), 1, 1e-6);              /* natural; upper case too */
    CHECK_NEAR(calc_at(&f, "sqrt(16) * PI / pi", 0, 0), 4, 1e-6);
    CHECK_NEAR(calc_at(&f, "max(SXX, 60) + min(SXX, 60)", 0, 5), 110, 1e-4);
    CHECK_NEAR(calc_at(&f, "clamp(SXX, 0, 20)", 0, 5), 20, 1e-6);
    CHECK_NEAR(calc_at(&f, "sign(SYY)", 0, 5), -1, 1e-6);
    CHECK_NEAR(calc_at(&f, "if(SXX > 40, 1, 2)", 0, 5), 1, 1e-6);
    CHECK_NEAR(calc_at(&f, "if(SXX > 60, 1, 2)", 0, 5), 2, 1e-6);
    CHECK_NEAR(calc_at(&f, "(SXX >= 50) * (SYY < 0) * SXX", 0, 5), 50, 1e-5);
    CHECK_NEAR(calc_at(&f, "SXX == 50 && SYY != 0", 0, 5), 1, 1e-6);
    CHECK_NEAR(calc_at(&f, "abs(SZX) / MISES", 0, 5), 55 / cv_von_mises(s5), 1e-5);
    CHECK_NEAR(calc_at(&f, "10 % 4", 0, 0), 2, 1e-9);
    CHECK(calc_at(&f, "0/0", 0, 0) != calc_at(&f, "0/0", 0, 0));   /* NaN */
    /* errors, with the column in the user's text */
    CHECK(strstr(calc_err(&f, "SXX + FOO"), "unknown name FOO at 7") != NULL);
    CHECK(strstr(calc_err(&f, "SXX +"), "syntax error") != NULL);
    CHECK(strstr(calc_err(&f, "foo(1)"), "unknown function foo") != NULL);
    CHECK(strstr(calc_err(&f, "E1"), "TOSTRAIN") != NULL);          /* no strains in the file */
    CHECK(strstr(calc_err(&f, "DISP_SXX"), "unknown name") != NULL);
    CHECK(strstr(calc_err(&f, "STRESS"), "write STRESS_MISES") != NULL);
    CHECK(strstr(calc_err(&f, ""), "empty") != NULL);
    CHECK(strstr(calc_err(&f, "  "), "empty") != NULL);
    CHECK(*calc_err(&f, "((SXX))") == 0);
    /* every node at once, the getter called once per field and step */
    char err[128];
    cv_calc* c = cv_calc_compile(&f, "STRESS_SXX - 10*X - 30*Y - 60*Z + D2", err, sizeof err);
    CHECK(c != NULL);
    if (c) {
        CHECK(cv_calc_uses_fields(c));
        float out[12];
        calc_ud u = { &f, NULL, 0 };
        CHECK(cv_calc_eval(c, &f, 1, calc_get, &u, NULL, 0, out));
        CHECK_EQ(u.calls, 2);
        for (int i = 0; i < 12; i++) CHECK_NEAR(out[i], -2.25, 1e-4);     /* SXX = 10 i */
        free(u.v);
        cv_calc_free(c);
    }
    c = cv_calc_compile(&f, "X*Y", err, sizeof err);
    CHECK(c && !cv_calc_uses_fields(c));
    cv_calc_free(c);
    /* a field missing in a step: NaN and its name */
    f.steps[0].nfields = 1;                       /* step 1 now has DISP only */
    c = cv_calc_compile(&f, "D1 + SXX", err, sizeof err);
    CHECK(c != NULL);
    if (c) {
        float out[12];
        calc_ud u = { &f, NULL, 0 };
        CHECK(!cv_calc_eval(c, &f, 0, calc_get, &u, NULL, 0, out));
        CHECK(out[3] != out[3]);
        CHECK(strcmp(cv_calc_missing(c), "STRESS_SXX") == 0);
        CHECK(cv_calc_eval(c, &f, 1, calc_get, &u, NULL, 0, out));
        CHECK(cv_calc_missing(c)[0] == 0);
        free(u.v);
        cv_calc_free(c);
    }
    f.steps[0].nfields = 2;
    char names[512];
    cv_calc_names(&f, names, sizeof names);
    CHECK(strstr(names, "DISP: D1 D2 D3 MAG") != NULL);
    CHECK(strstr(names, "STRESS: SXX SYY SZZ SXY SYZ SZX MISES P1 P2 P3") != NULL);
    CHECK_EQ(cv_calc_names(&f, names, 8), 7);     /* cut, terminated */
    CHECK_EQ(strlen(names), 7);
    cv_frd_free(&f);
    free(f.msgs.a);
    free(b.p);
}
