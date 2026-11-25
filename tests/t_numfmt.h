/* t_numfmt.h -- node coordinates written with 2- and 3-digit exponents (issue 42: an S8R deck
   with 1.000000e+02 reportedly showed nothing, 1.000000e+002 did). The .inp *NODE block and the
   .frd -1 records both ways, including 12-wide columns that run together with no space.
   Included by test_main.c. */
#include "../src/frd.h"
#include "../src/inp.h"
#include <string.h>

/* an S8R deck (8 nodes, one shell); `fmt` writes a coordinate as "%.6e" with a 2- or 3-digit exponent */
static void nf_fmt(char* out, size_t n, double v, int digits) {
    char t[64];
    snprintf(t, sizeof t, "%.6e", v);
    char* e = strchr(t, 'e');
    int ex = atoi(e + 1);
    snprintf(out, n, "%.*se%c%0*d", (int)(e - t), t, ex < 0 ? '-' : '+', digits, ex < 0 ? -ex : ex);
}

static void nf_inp(int digits, double scale) {
    static const double c[8][3] = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}, {.5,0,0}, {1,.5,0}, {.5,1,0}, {0,.5,0} };
    char deck[4096]; size_t n = 0;
    n += (size_t)snprintf(deck + n, sizeof deck - n, "*NODE, NSET=NALL\n");
    for (int i = 0; i < 8; i++) {
        char x[40], y[40], z[40];
        nf_fmt(x, sizeof x, c[i][0] * scale, digits); nf_fmt(y, sizeof y, c[i][1] * scale, digits);
        nf_fmt(z, sizeof z, c[i][2] * scale, digits);
        n += (size_t)snprintf(deck + n, sizeof deck - n, "%d, %s, %s, %s\n", i + 1, x, y, z);
    }
    n += (size_t)snprintf(deck + n, sizeof deck - n,
        "*ELEMENT, TYPE=S8R, ELSET=ESH\n1, 1,2,3,4,5,6,7,8\n"
        "*SHELL SECTION, ELSET=ESH, COMPOSITE\n0.1, 3, MAT1, 0.\n0.1, 3, MAT1, 90.\n"
        "*MATERIAL, NAME=MAT1\n*ELASTIC\n210000, 0.3\n");
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, n, NULL, NULL));
    CHECK_EQ(d.mesh.n_nodes, 8);
    CHECK_EQ(d.mesh.n_elems, 1);
    if (d.mesh.n_nodes == 8) {
        CHECK_NEAR(d.mesh.xyz[3 * 2 + 0], (float)scale, scale * 1e-6);       /* node 3 = (1,1,0) * scale */
        CHECK_NEAR(d.mesh.xyz[3 * 2 + 1], (float)scale, scale * 1e-6);
        CHECK_NEAR(d.mesh.xyz[3 * 5 + 1], (float)(0.5 * scale), scale * 1e-6);
    }
    cv_inp_free(&d);
}

/* one frd with two nodes: `a` and `b` are the three 12-column fields of each -1 record, verbatim */
static void nf_frd(const char* a, const char* b, double want_a, double want_b) {
    char s[1024];
    snprintf(s, sizeof s,
        "    1C\n    2C%18u%*s1\n -1%10u%s\n -1%10u%s\n -3\n9999\n", 2u, 73 - 24, "", 1u, a, 2u, b);
    cv_frd f;
    CHECK(cv_frd_parse(&f, s, strlen(s)));
    CHECK_EQ(f.n_nodes, 2);
    if (f.n_nodes == 2) {
        CHECK_NEAR(f.xyz[0], (float)want_a, fabs(want_a) * 1e-5);
        CHECK_NEAR(f.xyz[1], (float)want_a, fabs(want_a) * 1e-5);
        CHECK_NEAR(f.xyz[3], (float)want_b, fabs(want_b) * 1e-5);
    }
    cv_frd_free(&f);
    free(f.msgs.a);
}

static void test_numfmt(void) {
    for (int digits = 2; digits <= 3; digits++)
        for (int k = 0; k < 2; k++) nf_inp(digits, k ? 1e-3 : 100.0);
    /* %12.5E: ' 1.00000E+02' has a space, '-1.00000E+02' fills the column; E+002 forms are 13 wide */
    nf_frd(" 1.00000E+02 1.00000E+02 1.00000E+02", " 2.50000E-01 2.50000E-01 2.50000E-01", 100, 0.25);
    nf_frd("-1.00000E+02-1.00000E+02-1.00000E+02", "-2.50000E-01-2.50000E-01-2.50000E-01", -100, -0.25);
    nf_frd(" 1.0000E+002 1.0000E+002 1.0000E+002", "-2.5000E-001-2.5000E-001-2.5000E-001", 100, -0.25);   /* 3-digit, 12 wide */
    nf_frd(" 1.00000E+02 1.00000E+02 1.00000E+02", " 2.50000E-01 2.50000E-01 2.50000E-01", 100, 0.25);
}
