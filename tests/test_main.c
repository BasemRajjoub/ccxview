/* test_main.c -- headless unit tests for frd / mesh / field. `make test` */
#include "../src/frd.h"
#include "../src/mesh.h"
#include "../src/field.h"
#include "../src/filedlg.h"
#include "../src/dat.h"
#include "../src/gauss.h"
#include "../src/inp.h"
#include "../src/fbd.h"
#include "../src/os.h"
#include "../src/sta.h"
#include "../src/gpu.h"
#include "frd_write.h"
#include <math.h>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; \
    fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { g_fail++; fprintf(stderr, "%s:%d: %s == %s failed (%lld vs %lld)\n", \
    __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_NEAR(a, b, eps) do { g_checks++; double _a = (a), _b = (b); \
    if (!(fabs(_a - _b) <= (eps))) { g_fail++; fprintf(stderr, "%s:%d: %s ~ %s failed (%g vs %g)\n", \
    __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

#include "t_cfg.h"
#include "t_idlist.h"
#include "t_export.h"
#include "t_path.h"
#include "t_video.h"
#include "t_match.h"
#include "t_anchor.h"
#include "t_measure.h"
#include "t_tbtext.h"
#include "t_mesh.h"

/* ---- helpers ---- */

typedef struct { char* p; size_t n; } buf_t;

/* A scratch file the writer fills and slurp reads back. tmpfile() fails on
   Windows without rights to the drive root, so it is a named file under build/. */
static char g_tmp[64];
static FILE* tmp_open(void) {
    static int n;
    snprintf(g_tmp, sizeof g_tmp, "build/t_tmp%d.bin", n++);
    FILE* o = fopen(g_tmp, "w+b");
    if (!o) { fprintf(stderr, "cannot create %s\n", g_tmp); exit(1); }
    return o;
}

static buf_t slurp(FILE* o) {
    buf_t b;
    fflush(o);
    long n = ftell(o);
    rewind(o);
    b.p = malloc((size_t)n + 1);
    b.n = fread(b.p, 1, (size_t)n, o);
    fclose(o);
    remove(g_tmp);
    return b;
}

static const char* kDisp[] = { "D1", "D2", "D3" };
static const char* kStress[] = { "SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX" };

/* Two Hex8 sharing a face along x: 12 nodes (ids 100..111), elements 7 and 8
   with materials 1 and 2, two steps of DISP (+pseudo ALL) and STRESS. */
static buf_t two_hex(int binary) {
    uint32_t id[12];
    double xyz[36];
    for (int i = 0; i < 12; i++) {
        id[i] = 100 + i;
        xyz[3 * i] = i % 3; xyz[3 * i + 1] = (i / 3) % 2; xyz[3 * i + 2] = i / 6;
    }
#define N(ix, iy, iz) (100 + (iz) * 6 + (iy) * 3 + (ix))
    uint32_t conn[16] = {
        N(0,0,0), N(1,0,0), N(1,1,0), N(0,1,0), N(0,0,1), N(1,0,1), N(1,1,1), N(0,1,1),
        N(1,0,0), N(2,0,0), N(2,1,0), N(1,1,0), N(1,0,1), N(2,0,1), N(2,1,1), N(1,1,1),
    };
#undef N
    uint32_t eid[2] = { 7, 8 }, mat[2] = { 1, 2 };
    float disp[36], stress[72];
    for (int i = 0; i < 12; i++) {
        disp[3 * i] = -1.5f * i; disp[3 * i + 1] = -2.25f; disp[3 * i + 2] = 0.001f * i;
        for (int c = 0; c < 6; c++) stress[6 * i + c] = (float)(i * 10 + c) * (c % 2 ? -1.f : 1.f);
    }
    FILE* o = tmp_open();
    fprintf(o, "    1C\n");
    fw_nodes(o, 12, id, xyz, binary);
    fw_elems(o, 2, eid, 1, 8, conn, mat, binary);
    for (int s = 1; s <= 2; s++) {
        fw_step(o, s, s * 0.5, 12, binary);
        fw_field(o, "DISP", 3, kDisp, 1, 12, id, disp, binary);
        fw_step(o, s, s * 0.5, 12, binary);
        fw_field(o, "STRESS", 6, kStress, 0, 12, id, stress, binary);
    }
    fprintf(o, "9999\n");
    return slurp(o);
}

static void dump_msgs(const cv_frd* f) {
    for (size_t i = 0; i < f->msgs.n; i++) fprintf(stderr, "    msg: %s\n", f->msgs.a[i].text);
}

/* ---- tests ---- */

static void test_numbers(void) {
    double v;
    const char* s[] = { "1.00000E+00", " -1.50000E-03", "1.0D+05", "2.0-100", "  42 ", "-0.5", "nan" };
    double want[] = { 1.0, -1.5e-3, 1e5, 2e-100, 42, -0.5, NAN };
    for (int i = 0; i < 7; i++) {
        CHECK(cv_parse_num(s[i], s[i] + strlen(s[i]), &v));
        if (i == 6) CHECK(v != v);
        else CHECK_NEAR(v, want[i], fabs(want[i]) * 1e-12);
    }
    const char* bad[] = { "", "   ", "abc", "1.0E", "--1", "1.2.3" };
    for (int i = 0; i < 6; i++) CHECK(!cv_parse_num(bad[i], bad[i] + strlen(bad[i]), &v));
}

static void check_two_hex(int binary) {
    buf_t b = two_hex(binary);
    cv_frd f;
    CHECK(cv_frd_parse(&f, b.p, b.n));
    CHECK_EQ(f.msgs.n, 0);
    dump_msgs(&f);
    CHECK_EQ(f.n_nodes, 12);
    CHECK_EQ(f.n_elems, 2);
    if (f.n_nodes != 12 || f.n_elems != 2) { cv_frd_free(&f); free(f.msgs.a); free(b.p); return; }
    CHECK_EQ(f.elem_id[1], 8);
    CHECK_EQ(f.emat[1], 2);
    CHECK_EQ(f.n_steps, 2);
    if (f.n_steps == 2 && f.steps[1].nfields == 2) {
        CHECK_NEAR(f.steps[1].time, 1.0, 1e-6);
        CHECK(!f.steps[1].modal);
        const cv_field_desc* d = &f.steps[1].fields[0];
        CHECK(strcmp(d->name, "DISP") == 0);
        CHECK_EQ(d->ncomp, 3);                    /* pseudo ALL dropped */
        CHECK(strcmp(d->comp[2], "D3") == 0);
        CHECK_EQ(d->fmt, binary ? 2 : 1);
        float v[36];
        cv_frd_read_field(&f, d, v, NULL);
        CHECK_NEAR(v[3 * 11], -16.5, 1e-4);       /* adjacent negatives in ASCII */
        CHECK_NEAR(v[3 * 11 + 1], -2.25, 1e-5);
        CHECK_NEAR(v[3 * 11 + 2], 0.011, 1e-7);
        const cv_field_desc* s = &f.steps[1].fields[1];
        CHECK_EQ(s->ncomp, 6);
        float st[72];
        cv_frd_read_field(&f, s, st, NULL);
        CHECK_NEAR(st[6 * 5 + 3], -53, 1e-4);
    } else {
        CHECK(!"steps/fields missing");
    }

    {   /* per-element means stay inside the nodal range, NaN nodes make NaN elements */
        float nodal[12], em[2];
        for (int i = 0; i < 12; i++) nodal[i] = (float)i * 1.5f;
        cv_elem_mean(&f, nodal, em);
        float mn, mx;
        cv_range(nodal, 12, &mn, &mx);
        CHECK(em[0] >= mn && em[0] <= mx && em[1] >= mn && em[1] <= mx);
        CHECK(em[1] > em[0]);                     /* element 8 sits at larger x, larger node ids */
        nodal[3] = NAN;
        cv_elem_mean(&f, nodal, em);
        CHECK(em[0] != em[0] && em[1] == em[1]);  /* node 103 belongs to element 7 only */
    }

    cv_skin sk;
    CHECK(cv_skin_build(&sk, &f, NULL));
    CHECK_EQ(sk.n_tri, 20);                       /* 10 exterior quads */
    CHECK_EQ(sk.n_face, 10);
    CHECK(f.n_elems == 2 && cv_frd_elem_index(&f, 8) == 1 && cv_frd_elem_index(&f, 9) == UINT32_MAX);
    CHECK_EQ(sk.n_edge, 20);
    CHECK_EQ(sk.n_pt, 12);
    cv_skin_free(&sk);

    cv_groups g;
    CHECK(cv_groups_build(&g, &f));
    CHECK_EQ(g.axis[CV_AXIS_MAT].n, 2);
    CHECK_EQ(g.axis[CV_AXIS_TYPE].n, 1);
    CHECK_EQ(g.axis[CV_AXIS_TYPE].value[0], 1);
    g.axis[CV_AXIS_MAT].on[1] = false;            /* hide material 2 */
    uint8_t vis[2];
    cv_groups_mask(&g, f.n_elems, vis);
    CHECK(vis[0] && !vis[1]);
    CHECK(cv_skin_build(&sk, &f, vis));
    CHECK_EQ(sk.n_tri, 12);                       /* a lone hex: 6 quads */
    CHECK_EQ(sk.n_edge, 12);
    CHECK_EQ(sk.n_pt, 8);

    float o[3] = { 0.5f, 0.5f, 5.f }, dir[3] = { 0, 0, -1 };
    cv_pick p = cv_pick_ray(&f, &sk, NULL, 0, o, dir);
    CHECK(p.hit);
    CHECK_EQ(p.elem, 0);
    CHECK_NEAR(p.t, 4.0, 1e-5);
    float o2[3] = { 1.5f, 0.5f, 5.f };            /* element 8 is hidden: miss */
    CHECK(!cv_pick_ray(&f, &sk, NULL, 0, o2, dir).hit);

    cv_skin_free(&sk);

    /* crop box through the shared face: keep only the element at x < 1 */
    uint8_t v2[2] = { 1, 1 };
    float lo[3] = { -1, -1, -1 }, hi[3] = { 1.0f, 2, 2 };
    cv_crop_mask(&f, lo, hi, v2);
    CHECK(v2[0] && !v2[1]);
    CHECK(cv_skin_build(&sk, &f, v2));
    CHECK_EQ(sk.n_tri, 12);                       /* the cut exposes the shared face */
    cv_skin_free(&sk);

    cv_groups_free(&g);
    cv_frd_free(&f);
    free(b.p);
}

static void test_ascii(void)  { check_two_hex(0); }
static void test_binary(void) { check_two_hex(1); }

static void test_truncated(void) {
    buf_t b = two_hex(0);
    for (size_t cut = 0; cut < b.n; cut += 37) {
        cv_frd f;
        CHECK(cv_frd_parse(&f, b.p, cut));
        if (cut > 0 && cut < b.n - 5) CHECK(f.msgs.n > 0);   /* at least the missing 9999 */
        for (int s = 0; s < f.n_steps; s++)
            for (int k = 0; k < f.steps[s].nfields; k++) {
                float* v = malloc(sizeof(float) * CV_MAX(1, f.n_nodes) * f.steps[s].fields[k].ncomp);
                cv_frd_read_field(&f, &f.steps[s].fields[k], v, &f.msgs);
                free(v);
            }
        cv_frd_free(&f);
        free(f.msgs.a);
    }
    free(b.p);
}

/* Random byte damage must never crash or hang, ASCII and binary. */
static void test_fuzz(void) {
    uint32_t rng = 12345;
    for (int bin = 0; bin < 2; bin++) {
        buf_t b = two_hex(bin);
        char* w = malloc(b.n);
        for (int it = 0; it < 3000; it++) {
            memcpy(w, b.p, b.n);
            int flips = 1 + (int)(rng % 8);
            for (int k = 0; k < flips; k++) {
                rng = rng * 1664525u + 1013904223u;
                size_t at = (rng >> 8) % b.n;
                rng = rng * 1664525u + 1013904223u;
                const char pool[] = "0123456789-+. E\n\x00\xff" "ABC";
                w[at] = (rng & 1) ? (char)(rng >> 16) : pool[(rng >> 16) % (sizeof pool - 1)];
            }
            cv_frd f;
            cv_frd_parse(&f, w, b.n);
            for (int s = 0; s < f.n_steps; s++)
                for (int k = 0; k < f.steps[s].nfields; k++) {
                    float* v = malloc(sizeof(float) * CV_MAX(1, f.n_nodes) * f.steps[s].fields[k].ncomp);
                    cv_frd_read_field(&f, &f.steps[s].fields[k], v, &f.msgs);
                    free(v);
                }
            cv_skin sk;
            if (cv_skin_build(&sk, &f, NULL)) cv_skin_free(&sk);
            cv_groups g;
            if (cv_groups_build(&g, &f)) cv_groups_free(&g);
            cv_frd_free(&f);
            free(f.msgs.a);
        }
        free(w);
        free(b.p);
    }
    CHECK(1);
}

static void test_modal_flag(void) {
    const char* s =
        "    2C                     1                                     1\n"
        " -1         1 0.00000E+00 0.00000E+00 0.00000E+00\n -3\n"
        "    1PSTEP                         1           1           1\n"
        "    1PMODE                         1\n"
        "  100CL  101 3521.942762           1                     2    1MODAL      1\n"
        " -4  DISP        4    1\n -5  D1          1    2    1    0\n"
        " -1         1 9.63160E+03\n -3\n"
        "    1PSTEP                         2           1           2\n"
        "  100CL  102 1.00000E+00           1                     0    2           1\n"
        " -4  DISP        4    1\n -5  D1          1    2    1    0\n"
        " -1         1 1.00000E-02\n -3\n9999\n";
    cv_frd f;
    CHECK(cv_frd_parse(&f, s, strlen(s)));
    CHECK_EQ(f.n_steps, 2);
    if (f.n_steps == 2) { CHECK(f.steps[0].modal); CHECK(!f.steps[1].modal); }
    cv_frd_free(&f);
    free(f.msgs.a);
}

/* The 1U records: heading, user, date, program and version; the step's mode
   number and the 100CL analysis type; the solver's date as ISO */
static void test_frd_head(void) {
    const char* s =
        "    1C\n"
        "    1Uplate with a hole, tension\n"
        "    1UUSER              sergio\n"
        "    1UDATE              30.september.2026\n"
        "    1UTIME              23:06:53\n"
        "    1UHOST\n"
        "    1UPGM               CalculiX\n"
        "    1UVERSION           Version 2.22\n"
        "    1UCOMPILETIME       Mon Aug  5 19:15:25 CEST 2024\n"
        "    1UMAT    1STEEL\n"
        "    2C                     1                                     1\n"
        " -1         1 0.00000E+00 0.00000E+00 0.00000E+00\n -3\n"
        "    1PSTEP                         1           1           1\n"
        "  100CL  101 1.000000000           1                     0    1           1\n"
        " -4  DISP        4    1\n -5  D1          1    2    1    0\n"
        " -1         1 1.00000E-02\n -3\n"
        "    1PSTEP                         2           1           2\n"
        "    1PMODE                         3\n"
        "  100CL  102 210.1187427           1                     2    2MODAL      1\n"
        " -4  DISP        4    1\n -5  D1          1    2    1    0\n"
        " -1         1 9.63160E+03\n -3\n9999\n";
    cv_frd f;
    CHECK(cv_frd_parse(&f, s, strlen(s)));
    CHECK(!strcmp(f.head.heading, "plate with a hole, tension"));
    CHECK(!strcmp(f.head.user, "sergio"));
    CHECK(!strcmp(f.head.date, "30.september.2026"));
    CHECK(!strcmp(f.head.time, "23:06:53"));
    CHECK(!strcmp(f.head.host, ""));
    CHECK(!strcmp(f.head.pgm, "CalculiX"));
    CHECK(!strcmp(f.head.version, "Version 2.22"));
    CHECK_EQ(f.n_steps, 2);
    if (f.n_steps == 2) {
        CHECK_EQ(f.steps[0].ictype, 0); CHECK_EQ(f.steps[0].mode, 0);
        CHECK_EQ(f.steps[1].ictype, 2); CHECK_EQ(f.steps[1].mode, 3); CHECK_EQ(f.steps[1].step, 2);
    }
    cv_frd_free(&f);
    free(f.msgs.a);
    char d[16];
    CHECK(cv_frd_date_iso("30.september.2026", d, sizeof d) && !strcmp(d, "2026-09-30"));
    CHECK(cv_frd_date_iso("5.Jan.1999", d, sizeof d) && !strcmp(d, "1999-01-05"));
    CHECK(cv_frd_date_iso("07.11.2024", d, sizeof d) && !strcmp(d, "2024-11-07"));
    CHECK(!cv_frd_date_iso("", d, sizeof d) && !d[0]);
    CHECK(!cv_frd_date_iso("32.may.2020", d, sizeof d));
    CHECK(!cv_frd_date_iso("1.smarch.2020", d, sizeof d));
}

static void test_absurd_header(void) {
    const char* s = "    2C  999999999999                                                1\n"
                    " -1         1 0.00000E+00 0.00000E+00 0.00000E+00\n -3\n9999\n";
    cv_frd f;
    CHECK(cv_frd_parse(&f, s, strlen(s)));
    CHECK_EQ(f.n_nodes, 1);
    CHECK(f.msgs.n >= 1);
    cv_frd_free(&f);
    free(f.msgs.a);
}

/* Element pointing at a node that does not exist is dropped and reported. */
static void test_missing_node(void) {
    FILE* o = tmp_open();
    uint32_t id[4] = { 1, 2, 3, 4 };
    double xyz[12] = { 0,0,0, 1,0,0, 0,1,0, 0,0,1 };
    uint32_t conn[8] = { 1, 2, 3, 4, 1, 2, 3, 99 };
    uint32_t eid[2] = { 1, 2 };
    fw_nodes(o, 4, id, xyz, 0);
    fw_elems(o, 2, eid, 3, 4, conn, NULL, 0);
    fprintf(o, "9999\n");
    buf_t b = slurp(o);
    cv_frd f;
    CHECK(cv_frd_parse(&f, b.p, b.n));
    CHECK_EQ(f.n_elems, 1);
    CHECK_EQ(f.msgs.n, 1);
    cv_skin sk;
    CHECK(cv_skin_build(&sk, &f, NULL));
    CHECK_EQ(sk.n_tri, 4);                        /* a tet: 4 triangles */
    CHECK_EQ(sk.n_edge, 6);
    cv_skin_free(&sk);
    cv_frd_free(&f);
    free(f.msgs.a);
    free(b.p);
}

/* >6 components use -2 continuation lines in ASCII; a value for an unknown node
   is ignored and nodes without values read as NaN. */
static void test_continuation_and_nan(void) {
    FILE* o = tmp_open();
    uint32_t id[3] = { 1, 2, 3 };
    double xyz[9] = { 0,0,0, 1,0,0, 0,1,0 };
    fw_nodes(o, 3, id, xyz, 0);
    const char* comps[8] = { "A", "B", "C", "D", "E", "F", "G", "H" };
    float v[24];
    for (int i = 0; i < 24; i++) v[i] = (float)i - 7.f;
    uint32_t rid[3] = { 1, 2, 77 };                    /* 77 is not a node */
    fw_step(o, 1, 1.0, 3, 0);
    fw_field(o, "WIDE", 8, comps, 0, 3, rid, v, 0);
    fprintf(o, "9999\n");
    buf_t b = slurp(o);
    cv_frd f;
    CHECK(cv_frd_parse(&f, b.p, b.n));
    CHECK_EQ(f.n_steps, 1);
    if (f.n_steps == 1 && f.steps[0].nfields == 1) {
        const cv_field_desc* d = &f.steps[0].fields[0];
        CHECK_EQ(d->ncomp, 8);
        float out[24];
        cv_msgs m = {0};
        cv_frd_read_field(&f, d, out, &m);
        CHECK_NEAR(out[7], 0.f, 1e-6);              /* node 1, comp H = 7-7 */
        CHECK_NEAR(out[8 + 7], 8.f, 1e-6);          /* node 2, comp H */
        CHECK(out[16] != out[16]);                  /* node 3 has no value: NaN */
        CHECK_EQ(m.n, 1);                           /* the unknown id 77 */
        free(m.a);
    } else {
        CHECK(!"field missing");
    }
    cv_frd_free(&f);
    free(f.msgs.a);
    free(b.p);
}

/* FEMaster writes tensors XX YY ZZ YZ ZX XY: read back in the CalculiX order
   XX YY ZZ XY YZ ZX, names and values; a CalculiX tensor stays as it is. */
static void test_shear_order(void) {
    for (int binary = 0; binary < 2; binary++) {
        FILE* o = tmp_open();
        uint32_t id[2] = { 1, 2 };
        double xyz[6] = { 0,0,0, 1,0,0 };
        fw_nodes(o, 2, id, xyz, binary);
        const char* ccx[6] = { "SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX" };
        const char* fem[6] = { "SXX", "SYY", "SZZ", "SYZ", "SZX", "SXY" };
        float v[12] = { 1, 2, 3, 4, 5, 6,  11, 12, 13, 14, 15, 16 };
        fw_step(o, 1, 1.0, 2, binary);
        fw_field(o, "STRESS", 6, ccx, 0, 2, id, v, binary);
        fw_step(o, 1, 1.0, 2, binary);
        fw_field(o, "STRPOS", 6, fem, 0, 2, id, v, binary);
        fprintf(o, "9999\n");
        buf_t b = slurp(o);
        cv_frd f;
        CHECK(cv_frd_parse(&f, b.p, b.n));
        if (f.n_steps == 1 && f.steps[0].nfields == 2) {
            const cv_field_desc* c = &f.steps[0].fields[0];
            const cv_field_desc* m = &f.steps[0].fields[1];
            CHECK(!c->shear_yzx);
            CHECK(m->shear_yzx);
            CHECK(!strcmp(c->comp[3], "SXY") && !strcmp(c->comp[4], "SYZ") && !strcmp(c->comp[5], "SZX"));
            CHECK(!strcmp(m->comp[3], "SXY") && !strcmp(m->comp[4], "SYZ") && !strcmp(m->comp[5], "SZX"));
            float out[12];
            cv_msgs ms = {0};
            cv_frd_read_field(&f, c, out, &ms);
            for (int i = 0; i < 12; i++) CHECK_NEAR(out[i], v[i], 1e-6);     /* CalculiX untouched */
            cv_frd_read_field(&f, m, out, &ms);
            const float want[12] = { 1, 2, 3, 6, 4, 5,  11, 12, 13, 16, 14, 15 };
            for (int i = 0; i < 12; i++) CHECK_NEAR(out[i], want[i], 1e-6);
            free(ms.a);
        } else {
            CHECK(!"fields missing");
        }
        cv_frd_free(&f);
        free(f.msgs.a);
        free(b.p);
    }
}

static void test_field_math(void) {
    float s1[6] = { 100, 0, 0, 0, 0, 0 };
    CHECK_NEAR(cv_von_mises(s1), 100, 1e-4);
    float s2[6] = { 0, 0, 0, 50, 0, 0 };
    CHECK_NEAR(cv_von_mises(s2), 50 * sqrt(3.0), 1e-3);
    float pr[3];
    cv_principal(s2, false, pr);                    /* pure shear: +50, 0, -50 */
    CHECK_NEAR(pr[0], 50, 1e-3); CHECK_NEAR(pr[1], 0, 1e-3); CHECK_NEAR(pr[2], -50, 1e-3);
    float s3[6] = { 10, 20, 30, 0, 0, 0 };
    cv_principal(s3, false, pr);
    CHECK_NEAR(pr[0], 30, 1e-4); CHECK_NEAR(pr[1], 20, 1e-4); CHECK_NEAR(pr[2], 10, 1e-4);
    float s4[6] = { 2, 3, 4, 1, 0.5f, 0.25f }, s5[6] = { 2, 3, 4, 1, 0.25f, 0.5f }, q4[3], q5[3];
    cv_principal(s4, false, q4); cv_principal(s5, true, q5);     /* same tensor, .frd vs .dat order */
    for (int k = 0; k < 3; k++) CHECK_NEAR(q4[k], q5[k], 1e-5);
    CHECK_NEAR(q4[0] + q4[1] + q4[2], 9, 1e-4);                    /* trace */
    float s6[6] = { 7, 7, 7, 0, 0, 0 };
    cv_principal(s6, false, pr);
    CHECK_NEAR(pr[0], 7, 1e-5); CHECK_NEAR(pr[2], 7, 1e-5);
    {   /* cylindrical about Z: at (0,2,5) radial is +y, hoop is -x */
        cv_field_desc dv = { .ncomp = 3 }, dt = { .ncomp = 6 };
        static const char* tn[6] = { "SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX" };
        for (int c = 0; c < 6; c++) snprintf(dt.comp[c], sizeof dt.comp[c], "%s", tn[c]);
        for (int c = 0; c < 3; c++) snprintf(dv.comp[c], sizeof dv.comp[c], "D%d", c + 1);
        float xyz[3] = { 0, 2, 5 }, o[3] = { 0, 0, 0 };
        float vv[3] = { 1, 3, 7 };
        cv_cyl_values(&dv, xyz, 1, 2, o, vv);
        CHECK_NEAR(vv[0], 3, 1e-6); CHECK_NEAR(vv[1], -1, 1e-6); CHECK_NEAR(vv[2], 7, 1e-6);
        float tt[6] = { 10, 20, 30, 4, 5, 6 };            /* SXX SYY SZZ SXY SYZ SZX */
        cv_cyl_values(&dt, xyz, 1, 2, o, tt);
        CHECK_NEAR(tt[0], 20, 1e-5); CHECK_NEAR(tt[1], 10, 1e-5); CHECK_NEAR(tt[2], 30, 1e-5);
        CHECK_NEAR(tt[3], -4, 1e-5);                       /* Srt = -SXY */
        CHECK_NEAR(tt[4], -6, 1e-5);                       /* Sta = -SZX */
        CHECK_NEAR(tt[5], 5, 1e-5);                        /* Sar = SYZ */
        char nm[12];
        cv_cyl_comp_name(&dt, 5, nm); CHECK(!strcmp(nm, "Sar"));
        cv_cyl_comp_name(&dv, 1, nm); CHECK(!strcmp(nm, "Dt"));
    }
    {   /* linearization: s = 3 + 5x is all membrane + bending; s = x^2 on t = 2 */
        float ls[41 * 6];
        for (int i = 0; i < 41; i++) {
            float x = 2.f * i / 40;
            for (int c = 0; c < 6; c++) ls[6 * i + c] = c == 0 ? 3 + 5 * x : c == 1 ? x * x : 0;
        }
        double m[6], b[6];
        CHECK(cv_linearize(ls, 41, 2, m, b));
        CHECK_NEAR(m[0], 8, 1e-5); CHECK_NEAR(b[0], -5, 1e-5);
        CHECK_NEAR(m[1], 4.0 / 3, 1e-5); CHECK_NEAR(b[1], -2, 1e-5);
        double l[6];
        cv_lin_at(m, b, 2, 0, l);   CHECK_NEAR(l[0], 3, 1e-5);           /* m + b at the start */
        cv_lin_at(m, b, 2, 2, l);   CHECK_NEAR(l[0], 13, 1e-5);          /* m - b at the end */
        cv_lin_at(m, b, 2, 1, l);   CHECK_NEAR(l[0], 8, 1e-5);           /* the membrane in the middle */
        CHECK_NEAR(l[1], 4.0 / 3, 1e-5);
        /* ASME bending mask: along x drops XX, XY, ZX; along y drops YY, XY, YZ */
        double bm[6] = { 1, 2, 3, 4, 5, 6 };
        float ex[3] = { 2, 0, 0 }, ey[3] = { 0, 1, 0 }, ed[3] = { 1, 2, -3 }, e0[3] = { 0, 0, 0 };
        cv_bend_mask(bm, ex);
        CHECK_NEAR(bm[0], 0, 1e-9); CHECK_NEAR(bm[1], 2, 1e-9); CHECK_NEAR(bm[2], 3, 1e-9);
        CHECK_NEAR(bm[3], 0, 1e-9); CHECK_NEAR(bm[4], 5, 1e-9); CHECK_NEAR(bm[5], 0, 1e-9);
        double bn[6] = { 1, 2, 3, 4, 5, 6 };
        cv_bend_mask(bn, ey);
        CHECK_NEAR(bn[0], 1, 1e-9); CHECK_NEAR(bn[1], 0, 1e-9); CHECK_NEAR(bn[2], 3, 1e-9);
        CHECK_NEAR(bn[3], 0, 1e-9); CHECK_NEAR(bn[4], 0, 1e-9); CHECK_NEAR(bn[5], 6, 1e-9);
        double bd[6] = { 1, 2, 3, 4, 5, 6 };
        cv_bend_mask(bd, ed);                  /* an oblique line: the tensor times dir vanishes */
        {
            double S[3][3] = { { bd[0], bd[3], bd[5] }, { bd[3], bd[1], bd[4] }, { bd[5], bd[4], bd[2] } };
            for (int a = 0; a < 3; a++) CHECK_NEAR(S[a][0] * ed[0] + S[a][1] * ed[1] + S[a][2] * ed[2], 0, 1e-9);
            double tr = bd[0] + bd[1] + bd[2];  /* what is left is the tensor in the normal plane: trace = total - d.S.d/|d|^2 */
            double dd = 14, Sd = 1 * 1 + 2 * 4 + 3 * 9 + 2 * (4 * 2 + 5 * -6 + 6 * -3);
            CHECK_NEAR(tr, 6 - Sd / dd, 1e-9);
        }
        double bz[6] = { 1, 2, 3, 4, 5, 6 };
        cv_bend_mask(bz, e0);
        CHECK_NEAR(bz[0], 1, 0); CHECK_NEAR(bz[5], 6, 0);
        double sh[6] = { 0, 0, 0, 50, 0, 0 };
        CHECK_NEAR(cv_tresca6(sh, false), 100, 1e-3); CHECK_NEAR(cv_mises6(sh), 86.6025, 1e-3);
    }
    float v[6] = { 3, 4, 0, 1, NAN, 0 }, out[2];
    cv_field_scalar(v, 3, 2, CV_COMP_MAG, out);
    CHECK_NEAR(out[0], 5, 1e-6);
    CHECK(out[1] != out[1]);
    float r[4] = { -2, NAN, 5, 1 }, mn, mx;
    CHECK(cv_range(r, 4, &mn, &mx));
    CHECK_NEAR(mn, -2, 0); CHECK_NEAR(mx, 5, 0);
    cv_center_zero(&mn, &mx);
    CHECK_NEAR(mn, -5, 0);
    float nan1[1] = { NAN };
    CHECK(!cv_range(nan1, 1, &mn, &mx));
    CHECK_NEAR(cv_auto_deform(0.001f, 10.f), 1000.f, 1e-2);
    CHECK_NEAR(cv_auto_deform(5.f, 10.f), 1.f, 0);        /* never shrink */

    /* banding: band centres, monotone, clamped at both ends, identity when smooth */
    CHECK_NEAR(cv_band_center(0.f, 12), 0.5 / 12, 1e-7);
    CHECK_NEAR(cv_band_center(0.999f, 12), 11.5 / 12, 1e-7);
    CHECK_NEAR(cv_band_center(1.f, 12), 11.5 / 12, 1e-7);
    CHECK_NEAR(cv_band_center(0.5f, 2), 0.75, 1e-7);
    CHECK_NEAR(cv_band_center(0.37f, 0), 0.37, 1e-7);
    for (int b = 1; b <= 24; b++) {
        float prev = -1;
        for (int i = 0; i <= 100; i++) { float t = cv_band_center(i / 100.f, b); CHECK(t >= prev && t > 0 && t < 1); prev = t; }
    }

    cv_field_desc d = {0};
    strcpy(d.name, "STRESS");
    d.ncomp = 6;
    cv_scalar_opt opts[16];
    int n = cv_field_options(&d, opts, 16);
    CHECK_EQ(n, 7);
    CHECK_EQ(opts[0].comp, CV_COMP_MISES);
    snprintf(d.name, sizeof d.name, "stresses (elem, integ.pnt.,sxx,...)");    /* a .dat block: von Mises too */
    n = cv_field_options(&d, opts, 16);
    CHECK_EQ(opts[0].comp, CV_COMP_MISES);
}

static void test_portal_wire(void) {
    char p[256];
    CHECK(cv_uri_to_path("file:///home/a%20b/x.frd", p, sizeof p));
    CHECK(strcmp(p, "/home/a b/x.frd") == 0);
    CHECK(cv_uri_to_path("file://host/tmp/y", p, sizeof p));
    CHECK(strcmp(p, "/tmp/y") == 0);
    CHECK(!cv_uri_to_path("http://x/y", p, sizeof p));

    /* OpenFile: the title and the filter chosen first follow what the dialog is for (#23:
       the STL filter second in the list hid every .stl in KDE's dialog) */
    {
        uint8_t body[4096];
        char title[128], filt[128];
        const char* const* globs;
        size_t bn = cv_dbus_openfile_body(body, sizeof body, "x11:1a", "tok", "/home/u/models", CV_DLG_STL);
        CHECK(bn > 0);
        CHECK(cv_dbus_openfile_check(body, bn, title, sizeof title, filt, sizeof filt));
        CHECK(strcmp(title, "Import STL geometry") == 0);
        CHECK(strcmp(filt, "STL geometry (*.stl)") == 0);
        CHECK(strcmp(cv_filedlg_filter(CV_DLG_STL, &globs), "STL geometry (*.stl)") == 0 && !strcmp(globs[0], "*.stl") && !globs[1]);
        bn = cv_dbus_openfile_body(body, sizeof body, "", "tok", NULL, CV_DLG_MODEL);
        CHECK(cv_dbus_openfile_check(body, bn, title, sizeof title, filt, sizeof filt));
        CHECK(strstr(filt, "*.frd") && strstr(filt, "*.stl"));       /* an STL can be opened from Open too */
        cv_filedlg_filter(CV_DLG_MODEL, &globs);
        int k = 0; while (globs[k]) k++;
        CHECK(k == 6 && !strcmp(globs[4], "*.stl") && !strcmp(globs[5], "*.cel"));
        bn = cv_dbus_openfile_body(body, sizeof body, "", "tok", "/x", CV_DLG_COMPARE);
        CHECK(cv_dbus_openfile_check(body, bn, title, sizeof title, filt, sizeof filt));
        CHECK(strstr(filt, "*.frd") && !strstr(filt, "*.inp"));
        for (size_t c = 0; c + 1 < bn; c += 7) CHECK(!cv_dbus_openfile_check(body, c, title, sizeof title, filt, sizeof filt));
        CHECK(cv_dbus_openfile_body(body, 16, "", "tok", NULL, CV_DLG_STL) == 0);   /* too small a buffer */
    }

    uint8_t msg[1024];
    size_t n = cv_dbus_build_response(msg, sizeof msg, 0, "file:///data/m%C3%BCller/r.frd");
    CHECK(n > 0);
    uint32_t code = 9;
    CHECK(cv_dbus_parse_response(msg, n, &code, p, sizeof p));
    CHECK_EQ(code, 0);
    CHECK(strcmp(p, "/data/m\xc3\xbcller/r.frd") == 0);

    n = cv_dbus_build_response(msg, sizeof msg, 1, NULL);           /* cancelled */
    CHECK(cv_dbus_parse_response(msg, n, &code, p, sizeof p));
    CHECK_EQ(code, 1);
    CHECK(p[0] == 0);

    /* every truncation / byte flip of a valid message must be rejected or parsed safely */
    n = cv_dbus_build_response(msg, sizeof msg, 0, "file:///a");
    for (size_t k = 0; k < n; k++) cv_dbus_parse_response(msg, k, &code, p, sizeof p);
    uint8_t bad[1024];
    for (size_t k = 0; k < n; k++)
        for (int v = 0; v < 256; v += 37) {
            memcpy(bad, msg, n); bad[k] = (uint8_t)v;
            cv_dbus_parse_response(bad, n, &code, p, sizeof p);
        }
#if defined(__linux__)
    if (getenv("DBUS_SESSION_BUS_ADDRESS")) {                     /* live, invisible */
        char uniq[128] = "";
        CHECK(cv_dbus_hello(uniq, sizeof uniq));
        CHECK(uniq[0] == ':');
        printf("session bus ok, unique name %s\n", uniq);
    }
#endif
}

static void test_list_dir(void) {
    cv_dirent* e;
    int n = cv_list_dir("tests", &e);
    CHECK(n >= 4);
    bool found = false;
    for (int i = 0; i < n; i++) found |= !strcmp(e[i].name, "test_main.c") && !e[i].dir && e[i].size > 0;
    CHECK(found);
    free(e);
    CHECK_EQ(cv_list_dir("/definitely/not/here", &e), -1);
    char d[1024];
    CHECK(cv_exe_dir(d, sizeof d));
    CHECK(cv_is_dir(d));
    /* only plain web links reach the browser (a good one would open it: not tried here) */
    CHECK(!cv_open_url("file:///etc/passwd"));
    CHECK(!cv_open_url("javascript:alert(1)"));
    CHECK(!cv_open_url("https://example.com/a b"));
    CHECK(!cv_open_url("https://example.com/\"x"));
}

static void test_dat(void) {
    const char* txt =
        "\n                        S T E P       2\n\n"
        "                                INCREMENT     1\n\n"
        " displacements (vx,vy,vz) for set NDISP and time  0.1000000E+01\n\n"
        "        48 -2.841229E-17  9.979220E-04  2.104719E-02\n\n"
        " stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set EALL and time  0.1000000E+01\n\n"
        "         7   1  1.000000E+02 -2.000000E+01  0.000000E+00  5.0E+00  0.0E+00  0.0E+00\n"
        "         7   2  1.100000E+02 -2.100000E+01  0.000000E+00  5.0E+00  0.0E+00  0.0E+00\n"
        "         8   1  garbage\n"
        "\n"
        " equivalent plastic strain (elem, integ.pnt.,pe)for set EPEEQ and time  0.2000000E+01\n\n"
        "         7   1  7.746379E-04\n"
        "         7   2  8.0E-04\n"
        " global coordinates (elem, integ.pnt.,x,y,z) for set EALL and time  0.2000000E+01\n\n"
        "         7   1  1.0 2.0 3.0\n";
    cv_dat d;
    CHECK(cv_dat_parse(&d, txt, strlen(txt)));
    CHECK_EQ(d.n, 3);                                   /* nodal block skipped */
    if (d.n == 3) {
        CHECK(strcmp(d.b[0].name, "stresses") == 0);
        CHECK(strcmp(d.b[0].set, "EALL") == 0);
        CHECK_EQ(d.b[0].ncomp, 6);
        CHECK(strcmp(d.b[0].comp[5], "syz") == 0);
        CHECK_EQ(d.b[0].step, 2);
        CHECK_NEAR(d.b[0].time, 1.0, 1e-6);
        CHECK_EQ(d.b[0].n, 3);                          /* garbage row kept as NaN, reported */
        CHECK_NEAR(d.b[0].vals[6 + 1], -21.0, 1e-4);
        CHECK(d.b[0].vals[12 + 1] != d.b[0].vals[12 + 1]);
        CHECK(strcmp(d.b[1].name, "equivalent plastic strain") == 0);
        CHECK_EQ(d.b[1].ncomp, 1);
        CHECK_EQ(d.b[1].n, 2);                          /* header without a blank line ends it */
        CHECK_EQ(d.b[1].ip[1], 2);
        CHECK(d.b[2].is_coord);
        CHECK_NEAR(d.b[2].vals[2], 3.0, 1e-6);
    }
    CHECK(d.msgs.n >= 1);
    cv_dat_free(&d);
    free(d.msgs.a);

    /* damage never crashes */
    size_t L = strlen(txt);
    char* w = malloc(L);
    uint32_t rng = 7;
    for (int it = 0; it < 2000; it++) {
        memcpy(w, txt, L);
        for (int k = 0; k < 4; k++) { rng = rng * 1664525u + 1013904223u; w[(rng >> 8) % L] = (char)(rng >> 20); }
        cv_dat dd;
        cv_dat_parse(&dd, w, (size_t)((rng >> 4) % (L + 1)));
        cv_dat_free(&dd);
        free(dd.msgs.a);
    }
    free(w);
}

static void test_gauss(void) {
    /* shape functions: N_i(node_j) = delta_ij, and they sum to 1 everywhere */
    const int types[10] = { 1, 4, 3, 6, 2, 5, 9, 10, 7, 8 }, nns[10] = { 8, 20, 4, 10, 6, 15, 4, 8, 3, 6 };
    for (int t = 0; t < 10; t++) {
        double N[20], x[3];
        int bad = 0;
        for (int j = 0; j < nns[t]; j++) {
            CHECK(cv_node_param(types[t], nns[t], j, x));
            CHECK(cv_shape(types[t], nns[t], x, N));
            for (int i = 0; i < nns[t]; i++) if (fabs(N[i] - (i == j)) > 1e-12) bad++;
        }
        CHECK_EQ(bad, 0);
        double y[3] = { 0.21, 0.13, -0.37 }, sum = 0;
        cv_shape(types[t], nns[t], y, N);
        for (int i = 0; i < nns[t]; i++) sum += N[i];
        CHECK_NEAR(sum, 1.0, 1e-12);
    }
    /* CalculiX order: C3D8 point 2 is (+,-,-); C3D20 point 27 is (+,+,+) */
    double x[3];
    CHECK(cv_ip_param(1, 8, 1, x));
    CHECK(x[0] > 0 && x[1] < 0 && x[2] < 0);
    CHECK(cv_ip_param(4, 27, 26, x));
    CHECK(x[0] > 0.7 && x[1] > 0.7 && x[2] > 0.7);
    CHECK(cv_ip_param(4, 27, 13, x));
    CHECK_NEAR(x[0], 0, 0); CHECK_NEAR(x[2], 0, 0);
    CHECK(!cv_ip_param(1, 5, 0, x));                     /* unknown scheme */
    CHECK(cv_ip_param(10, 4, 1, x));                     /* S8R point 2: (+,-) on the face */
    CHECK(x[0] > 0 && x[1] < 0 && x[2] == 0);
    CHECK(cv_ip_param(7, 1, 0, x));                      /* 3-node triangle: centroid */
    CHECK_NEAR(x[0], 1.0 / 3, 1e-12);
    CHECK(cv_ip_param(8, 3, 2, x));
    CHECK_NEAR(x[1], 2.0 / 3, 1e-9);
    CHECK(cv_ip_param(3, 15, 14, x));

    /* .frd order: hex20 13-16 <-> 17-20, wedge15 10-12 <-> 13-15, both involutions */
    CHECK_EQ(cv_frd_node_pos(4, 20, 12), 16);
    CHECK_EQ(cv_frd_node_pos(4, 20, 19), 15);
    CHECK_EQ(cv_frd_node_pos(5, 15, 9), 12);
    CHECK_EQ(cv_frd_node_pos(5, 15, 14), 11);
    CHECK_EQ(cv_frd_node_pos(6, 10, 7), 7);
    for (int i = 0; i < 20; i++) CHECK_EQ(cv_frd_node_pos(4, 20, cv_frd_node_pos(4, 20, i)), i);
}

/* in-memory *INCLUDE files for the .inp tests */
static bool mem_reader(void* user, const char* path, char** data, size_t* size) {
    const char** files = user;
    for (int i = 0; files[i]; i += 2)
        if (strcmp(files[i], path) == 0) {
            *size = strlen(files[i + 1]);
            *data = malloc(*size + 1);
            memcpy(*data, files[i + 1], *size + 1);
            return true;
        }
    return false;
}

/* a rigid body's ROT NODE: its DOFs 1-3 are rotations, so its load is a moment and
   what holds it a held rotation; the REF NODE's stay translations (#17) */
static void test_rot_node(void) {
    const char* deck =
        "*NODE\n1, 0, 0, 0\n2, 1, 0, 0\n3, 0, 1, 0\n4, 0, 0, 1\n40, 0, 0, 0\n41, 0, 0, 0\n"
        "*ELEMENT, TYPE=C3D4, ELSET=E\n1, 1, 2, 3, 4\n"
        "*NSET, NSET=N\n1, 2\n"
        "*Rigid body, Nset=N, Ref node=40, Rot node=41\n"
        "*STEP\n*STATIC\n*BOUNDARY\n41, 1, 1\n40, 1, 3\n*CLOAD\n41, 2, 1.0E7\n40, 3, 5.0\n*END STEP\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), mem_reader, NULL));
    CHECK_EQ(d.nlinks, 1);
    if (d.nlinks == 1) { CHECK_EQ(d.links[0].ref, 40); CHECK_EQ(d.links[0].rot, 41); }
    cv_applied ap;
    CHECK(cv_inp_applied(&d, 0, &ap));
    int mom = 0, force = 0, rot_held = 0, held = 0;
    for (uint32_t i = 0; i < ap.ncloads; i++) {
        if (ap.cloads[i].node == 41) mom += ap.cloads[i].dof == 5;
        if (ap.cloads[i].node == 40) force += ap.cloads[i].dof == 3;
    }
    for (uint32_t i = 0; i < ap.nbcs; i++) {
        if (ap.bcs[i].node == 41) rot_held += ap.bcs[i].dof_lo == 4;
        if (ap.bcs[i].node == 40) held += ap.bcs[i].dof_lo <= 3;
    }
    CHECK_EQ(mom, 1); CHECK_EQ(force, 1); CHECK_EQ(rot_held, 1); CHECK_EQ(held, 3);
    cv_applied_free(&ap);
    cv_inp_free(&d);
}

static void test_inp(void) {
    /* 20-node hex spanning two lines (deck order), a C3D8 in ELSET via param,
       node data continued from an include, sets referencing sets, a surface */
    const char* inc_nodes =
        "13, 1.0, 1.0, 0.0\n14, 1.0, 0.0, 1.0\n15, 1.0, 0.5, 2.0\n";
    const char* deck =
        "*HEADING\nTest deck\n"
        "** a comment\n"
        "*NODE, NSET=NALL\n"
        "1, 0,0,0\n2, 1,0,0\n3, 1,1,0\n4, 0,1,0\n5, 0,0,1\n6, 1,0,1\n7, 1,1,1\n8, 0,1,1\n"
        "9, 0.5,0,0\n10, 1,0.5,0\n11, 0.5,1,0\n12, 0,0.5,0\n"
        "*INCLUDE, INPUT=nodes.inc\n"
        "16, 0,0.5,1\n17, 0,0,0.5\n18, 1,0,0.5\n19, 1,1,0.5\n20, 0,1,0.5\n21, 3,3,3\n"
        "*ELEMENT, TYPE=C3D20R, ELSET=EBIG\n"
        "1, 1,2,3,4,5,6,7,8,9,10,\n11,12,13,14,15,16,17,18,19,20\n"
        "*ELEMENT, TYPE=C3D8, ELSET=ESMALL\n"
        "2, 1,2,3,4,5,6,7,8\n"
        "3, 1,2,3,4,5,6,7,99\n"                  /* missing node: dropped */
        "*ELEMENT, TYPE=U1\n9, 1, 2\n"         /* not drawable: skipped */
        "*ELEMENT, TYPE=SPRINGA, ELSET=ESPR\n20, 1, 21\n"
        "*ELEMENT, TYPE=SPRING1, ELSET=EGND\n21, 21\n"
        "*ELEMENT, TYPE=MASS, ELSET=EM\n22, 21\n"
        "*SPRING, ELSET=EGND\n3\n1000.\n"
        "*ELEMENT, TYPE=DCOUP3D, ELSET=EDC\n30, 21\n"
        "*NSET, NSET=NGEN, GENERATE\n1, 9, 2\n"
        "*ELSET, ELSET=EBOTH\nEBIG, ESMALL\n"
        "*NSET, NSET=NBAD\n1, garbage\n"
        "*SURFACE, NAME=STOP\nEBIG, S2\n2, S1\n"
        "*NODE\n40, 5, 5, 5\n41, 5, 5, 6\n"
        "*RIGID BODY, NSET=NGEN, REF NODE=40, ROT NODE=41\n"
        "*SURFACE, NAME=SCPL\nESMALL, S1\n"
        "*COUPLING,REFNODE=40,SURFACE=SCPL,CONSTRAINTNAME=CPL1\n*KINEMATIC\n1, 3\n"   /* blanks are optional (#21) */
        "*DISTRIBUTINGCOUPLING, ELSET=EDC\n1, 1.\n2, 1.\n3, 1.\n"
        "*EQUATION\n3\n5, 1, 1., 6, 1, -1.,\n7, 2, 0.5\n"
        "*TIE, NAME=T1\nSCPL, STOP\n"
        "*CONTACT PAIR, INTERACTION=I1, TYPE=NODE TO SURFACE\nSTOP, SCPL\n"
        "*MATERIAL, NAME=Steel\n*ELASTIC\n210000, 0.3\n"
        "*SOLID SECTION, ELSET=ESMALL, MATERIAL=STEEL\n"
        "*BOUNDARY\n1, 1, 3\n2, 2\n"
        "*STEP\n*STATIC\n*BOUNDARY\nNGEN, 11, 11, 20.0\n*CLOAD\nNGEN, 2, -5.5\n8, 1, 1.0\n"
        "*DLOAD\nESMALL, P2, 3.0\nEBIG, GRAV, 9810, 0, 0, -1\n*END STEP\n"
        "*STEP\n*CLOAD\n8, 1, 2.0\n*END STEP\n";
    const char* files[] = { "nodes.inc", inc_nodes, NULL };
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), mem_reader, (void*)files));
    CHECK(strcmp(d.heading, "Test deck") == 0);
    CHECK_EQ(d.nsteps, 2);                             /* a *STATIC step, then one without a procedure */
    if (d.nsteps == 2) { CHECK_EQ(d.proc[0], CV_PROC_STATIC); CHECK_EQ(d.proc[1], CV_PROC_NONE); }
    CHECK_EQ(d.mesh.n_nodes, 23);                      /* 12 + 3 included + 6 + 2 ref nodes */
    CHECK_EQ(d.mesh.n_elems, 3);                       /* + the SPRINGA as a Line2 */
    CHECK_EQ(d.ndisc, 4);                              /* + the DCOUP3D */
    if (d.ndisc == 4) {
        CHECK_EQ(d.disc[0].kind, CV_DISC_SPRING); CHECK_EQ(d.disc[0].nn, 2); CHECK_EQ(d.disc[0].n[1], 21);
        CHECK_EQ(d.disc[1].nn, 1); CHECK_EQ(d.disc[1].dof, 3);
        CHECK_EQ(d.disc[2].kind, CV_DISC_MASS);
    }
    CHECK(cv_inp_set(&d, "EGND", true) && cv_inp_set(&d, "EGND", true)->n == 1);
    CHECK_EQ(d.mesh.etype[0], 4);
    uint32_t e1 = cv_frd_elem_index(&d.mesh, 1);
    CHECK(e1 != UINT32_MAX);
    if (e1 != UINT32_MAX) {
        /* deck node 13 (top mid-edge) lands at .frd position 17 (index 16) */
        uint32_t n = d.mesh.conn[d.mesh.eoff[e1] + 16];
        CHECK_EQ(d.mesh.node_id[n], 13);
        n = d.mesh.conn[d.mesh.eoff[e1] + 12];        /* .frd 13 = deck 17 (vertical) */
        CHECK_EQ(d.mesh.node_id[n], 17);
    }
    const cv_set* s = cv_inp_set(&d, "ngen", false);
    CHECK(s && s->n == 5);
    s = cv_inp_set(&d, "EBOTH", true);
    CHECK(s && s->n == 3);                             /* 1, 2 and the dropped 3 */
    s = cv_inp_set(&d, "NALL", false);
    CHECK(s && s->n == 21);                            /* the ref nodes came from a later *NODE without NSET */
    CHECK_EQ(d.nsurfs, 2);
    if (d.nsurfs == 2) { CHECK_EQ(d.surfs[0].n, 2); CHECK_EQ(d.surfs[0].face[0], 1); }
    CHECK_EQ(d.nlinks, 6);
    if (d.nlinks == 6) {
        CHECK_EQ(d.links[0].kind, CV_LINK_RIGID); CHECK_EQ(d.links[0].ref, 40); CHECK_EQ(d.links[0].n, 5);
        CHECK_EQ(d.links[1].kind, CV_LINK_KINEMATIC); CHECK_EQ(d.links[1].ref, 40); CHECK_EQ(d.links[1].surf[0], 1); CHECK(strcmp(d.links[1].name, "CPL1") == 0);
        CHECK_EQ(d.links[2].kind, CV_LINK_DISTRIBUTING); CHECK_EQ(d.links[2].ref, 21); CHECK_EQ(d.links[2].n, 3);
        CHECK_EQ(d.links[3].kind, CV_LINK_EQUATION); CHECK_EQ(d.links[3].n, 3); CHECK_EQ(d.links[3].nodes[2], 7);
        CHECK_EQ(d.links[4].kind, CV_LINK_TIE); CHECK_EQ(d.links[4].surf[0], 1); CHECK_EQ(d.links[4].surf[1], 0);
        CHECK_EQ(d.links[5].kind, CV_LINK_CONTACT); CHECK(strcmp(d.links[5].name, "I1") == 0);
    }
    CHECK_EQ(d.nmats, 1);
    uint32_t e2 = cv_frd_elem_index(&d.mesh, 2);
    CHECK(e1 != UINT32_MAX && e2 != UINT32_MAX && d.mesh.emat[e2] == 1 && d.mesh.emat[e1] == 0);
    CHECK(d.msgs.n >= 3);                              /* bad line, missing node, U1 */
    CHECK_EQ(d.nbcs, 2 + 5);                           /* 1,1-3  2,2  and NGEN (5 nodes) dof 11 */
    if (d.nbcs == 7) { CHECK_EQ(d.bcs[0].dof_lo, 1); CHECK_EQ(d.bcs[0].dof_hi, 3); CHECK_EQ(d.bcs[6].dof_lo, 11); }
    CHECK_EQ(d.ncloads, 5 + 1 + 1);                    /* NGEN dof 2, node 8 dof 1 in both steps */
    CHECK_EQ(d.ndloads, 2);                            /* ESMALL = 2 and (dropped) 3 */
    if (d.ndloads == 2) { CHECK_EQ(d.dloads[0].face, 1); CHECK_NEAR(d.dloads[0].value, 3.0, 0); }
    CHECK_EQ(d.nbody, 1);                              /* GRAV on EBIG */
    for (int st = 0; st < 2; st++) {                   /* the second step gives node 8 a new value */
        cv_applied ap;
        CHECK(cv_inp_applied(&d, st, &ap));
        CHECK_EQ(ap.ncloads, 6);
        float n8 = 0;
        for (uint32_t i = 0; i < ap.ncloads; i++) if (ap.cloads[i].node == 8 && ap.cloads[i].dof == 1) n8 = ap.cloads[i].value;
        CHECK_NEAR(n8, st ? 2.0 : 1.0, 0);
        CHECK_EQ(ap.nbcs, 3 + 1 + 5);                  /* one entry per DOF */
        cv_applied_free(&ap);
    }
    cv_skin sk;
    CHECK(cv_skin_build(&sk, &d.mesh, NULL));
    CHECK_EQ(sk.n_face, 0);                            /* two hexes on the same 8 corners: every face is shared */
    CHECK_EQ(sk.n_edge, 1);                            /* the spring */
    cv_skin_free(&sk);
    cv_inp_free(&d);
    free(d.msgs.a);

    /* random damage never crashes */
    size_t L = strlen(deck);
    char* w = malloc(L);
    uint32_t rng = 99;
    for (int it = 0; it < 3000; it++) {
        memcpy(w, deck, L);
        for (int k = 0; k < 5; k++) { rng = rng * 1664525u + 1013904223u; w[(rng >> 8) % L] = (char)(rng >> 20); }
        cv_inp dd;
        cv_inp_parse(&dd, w, (size_t)((rng >> 4) % (L + 1)), mem_reader, (void*)files);
        cv_skin k2;
        if (cv_skin_build(&k2, &dd.mesh, NULL)) cv_skin_free(&k2);
        cv_inp_free(&dd);
        free(dd.msgs.a);
    }
    free(w);
}

/* each step's procedure, by its keyword, blanks and parameters as decks write them */
static void test_inp_procs(void) {
    const char* deck =
        "*NODE\n1, 0,0,0\n"
        "*STEP\n*HEAT TRANSFER, STEADY STATE\n1., 1.\n*END STEP\n"
        "*STEP, NLGEOM\n*Static\n*END STEP\n"
        "*STEP\n*FREQUENCY, SOLVER=SPOOLES\n6\n*END STEP\n"
        "*STEP, PERTURBATION\n*BUCKLE\n2\n*END STEP\n"
        "*STEP\n*HEAT TRANSFER\n0.1, 1.\n*END STEP\n"
        "*STEP\n*COUPLED TEMPERATURE-DISPLACEMENT\n*END STEP\n"
        "*STEP\n*MODAL DYNAMIC\n*END STEP\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    CHECK_EQ(d.nsteps, 7);
    static const int want[7] = { CV_PROC_HEAT_STEADY, CV_PROC_STATIC, CV_PROC_FREQUENCY, CV_PROC_BUCKLE,
                                 CV_PROC_HEAT, CV_PROC_COUPLED_TD, CV_PROC_MODAL_DYNAMIC };
    for (int i = 0; i < 7 && i < d.nsteps; i++) CHECK_EQ(d.proc[i], want[i]);
    CHECK(!strcmp(cv_inp_proc_name(CV_PROC_BUCKLE), "Buckling"));
    CHECK(!strcmp(cv_inp_proc_name(CV_PROC_NONE), ""));
    CHECK(!strcmp(cv_inp_proc_name(99), ""));
    cv_inp_free(&d);
}

static void test_fbd(void) {
    const char* geo =
        "# written by cgx_2.23\n"
        " PNT p1 0 0 0\n PNT p2 1 0 0\n PNT p3 1 1 0\n PNT p4 0 1 0\n"
        " PNT C 0 0 5\n PNT A 1 0 5\n PNT B 0 1 5\n"
        " PNT s1 0 0 9\n PNT s2 1 1 9\n PNT s3 2 0 9\n"
        " SEQA Q1 pnt s1 s2 s3\n"
        " LINE L1 p1 p2 4\n LINE L2 p2 p3 4\n LINE L3 p3 p4 4\n LINE L4 p4 p1 4\n"
        " LINE ARC A B C 8\n"
        " LINE SPL s1 s3 Q1 8\n"
        " GSUR SQ + BLEND + L1 + L2 + L3 + L4\n"
        " SETA corners p p1 p3\n SETA edges l L1 L2\n SETA face s SQ\n SETA mesh n 7 3 7\n SETA mesh e 2\n SETA empty q x\n"
        " VALU x 3\n MSHP SQ s 4 1 0\n";
    cv_fbd g;
    CHECK(cv_fbd_parse(&g, geo, strlen(geo)));
    CHECK(!g.needs_cgx);
    CHECK_EQ(g.npts, 10);
    CHECK_EQ(g.ncrv, 6);
    CHECK_EQ(g.nsrf, 1);
    CHECK_EQ(g.nsets, 4);                               /* "empty" holds nothing */
    CHECK_EQ(g.sets[3].nnod, 3);                        /* mesh ids as written */
    CHECK_EQ(g.sets[3].nel, 1);
    /* the arc stays on its circle (radius 1 around C) */
    int off_circle = 0;
    for (uint32_t v = g.coff[4]; v < g.coff[5]; v++) {
        const float* q = g.cxyz + 3 * v;
        float r = sqrtf(q[0] * q[0] + q[1] * q[1]);
        if (fabsf(r - 1.f) > 1e-4f || fabsf(q[2] - 5.f) > 1e-6f) off_circle++;
    }
    CHECK_EQ(off_circle, 0);
    CHECK(g.coff[5] - g.coff[4] > 4);
    /* the spline starts and ends on its points */
    const float* s0 = g.cxyz + 3 * g.coff[5];
    const float* s9 = g.cxyz + 3 * (g.coff[6] - 1);
    CHECK_NEAR(s0[0], 0, 1e-6); CHECK_NEAR(s9[0], 2, 1e-6);
    /* the unit square is filled: triangle areas sum to 1 */
    double area = 0;
    for (uint32_t t = 0; t < g.ntri; t++) {
        const float* q = g.txyz + 9 * t;
        double ux = q[3] - q[0], uy = q[4] - q[1], vx = q[6] - q[0], vy = q[7] - q[1];
        area += 0.5 * fabs(ux * vy - uy * vx);
    }
    CHECK_NEAR(area, 1.0, 1e-4);
    cv_fbd_free(&g);
    free(g.msgs.a);

    /* a script with cgx-only commands is recognised as one */
    const char* script = "pnt p1 0 0 0\npnt p2 1 0 0\nline l1 p1 p2 4\nswep all new tra 0 0 1 4\n";
    CHECK(cv_fbd_parse(&g, script, strlen(script)));
    CHECK(g.needs_cgx);
    CHECK(strstr(g.needs_why, "swep") != NULL);
    cv_fbd_free(&g);
    free(g.msgs.a);
    /* references to undefined points are reported, not fatal */
    const char* broken = " PNT a 0 0 0\n LINE L a zz 2\n GSUR S + BLEND + L + L + L\n";
    CHECK(cv_fbd_parse(&g, broken, strlen(broken)));
    CHECK(g.msgs.n >= 1);
    cv_fbd_free(&g);
    free(g.msgs.a);

    size_t L = strlen(geo);
    char* w = malloc(L);
    uint32_t rng = 5;
    for (int it = 0; it < 3000; it++) {
        memcpy(w, geo, L);
        for (int k = 0; k < 5; k++) { rng = rng * 1664525u + 1013904223u; w[(rng >> 8) % L] = (char)(rng >> 20); }
        cv_fbd gg;
        cv_fbd_parse(&gg, w, (size_t)((rng >> 4) % (L + 1)));
        cv_fbd_free(&gg);
        free(gg.msgs.a);
    }
    free(w);
}

static void test_sta(void) {
    const char* sta =
        "SUMMARY OF JOB INFORMATION\n"
        "  STEP      INC     ATT  ITRS     TOT TIME     STEP TIME      INC TIME\n"
        "     1          1     1     2  0.100000E+01  0.100000E+01  0.100000E+01\n"
        "     2          4     1U    4  0.100350E+01  0.350000E-02  0.225000E-02\n"
        "     2          4     2     3  0.100354E+01  0.353516E-02  0.351563E-04\n"
        "garbage line here\n";
    const char* cvg =
        "SUMMARY OF C0NVERGENCE INFORMATION\n"
        "  STEP   INC  ATT   ITER     CONT.   RESID.        CORR.      RESID.      CORR.\n"
        "     1     1     1     1        0  0.7734E+03  0.1000E+03  0.0000E+00  0.0000E+00\n"
        "     1     1     1     2      243  0.3111E-01  0.6636E+00  0.0000E+00  0.0000E+00\n";
    cv_sta t = {0};
    CHECK(cv_sta_parse(&t, sta, strlen(sta)));
    CHECK_EQ(t.ninc, 3);
    if (t.ninc == 3) {
        CHECK(!t.inc[0].cutback && t.inc[1].cutback && !t.inc[2].cutback);
        CHECK_EQ(t.inc[1].iters, 4);
        CHECK_NEAR(t.inc[2].total_time, 1.00354, 1e-5);
    }
    CHECK(cv_cvg_parse(&t, cvg, strlen(cvg)));
    CHECK_EQ(t.nit, 2);
    if (t.nit == 2) { CHECK_EQ(t.it[1].contact_elems, 243); CHECK_NEAR(t.it[1].resid_force, 0.03111, 1e-6); CHECK_NEAR(t.it[0].corr_disp, 100, 1e-3); }
    cv_sta_free(&t);
    for (size_t k = 0; k < strlen(sta); k += 7) { cv_sta t2 = {0}; cv_sta_parse(&t2, sta, k); cv_cvg_parse(&t2, cvg, k % strlen(cvg)); cv_sta_free(&t2); }
}

static void test_gpu_env(void) {
    CHECK(!cv_gpu_is_software_run());
    cv_gpu_software_mode();
    CHECK(cv_gpu_is_software_run());
#if defined(__linux__)
    CHECK(getenv("LIBGL_ALWAYS_SOFTWARE") && strcmp(getenv("LIBGL_ALWAYS_SOFTWARE"), "1") == 0);
    CHECK(getenv("GALLIUM_DRIVER") && strcmp(getenv("GALLIUM_DRIVER"), "llvmpipe") == 0);
#endif
    CHECK(cv_gpu_percent(NULL) < 0);                                 /* nothing initialised: unknown */
}

/* results in local systems: what the deck says CalculiX wrote, turned back */
static void test_localsys(void) {
    const char* deck =
        "*NODE, NSET=NALL\n1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n5,0,0,1\n6,1,0,1\n7,1,1,1\n8,0,1,1\n"
        "*ELEMENT, TYPE=C3D8, ELSET=E1\n1,1,2,3,4,5,6,7,8\n"
        "*NSET, NSET=NT\n2\n"
        "*TRANSFORM, NSET=NT\n0.,1.,0.,-1.,0.,0.\n"
        "*ORIENTATION, NAME=Or1\n0.,1.,0.,-1.,0.,0.\n"
        "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n"
        "*SOLID SECTION, ELSET=E1, MATERIAL=M, ORIENTATION=OR1\n"
        "*STEP\n*STATIC\n*NODE FILE, GLOBAL=NO\nU\n*EL FILE, GLOBAL=NO\nS\n*END STEP\n"
        "*STEP\n*STATIC\n*EL FILE\nS\n*END STEP\n"          /* S global again, U kept */
        "*STEP\n*STATIC\n*END STEP\n";                       /* no cards: step 2's requests */
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    CHECK_EQ(d.nsteps, 3);
    if (d.nsteps == 3) {
        CHECK_EQ(d.outsys[0][CV_OUT_U], 'L'); CHECK_EQ(d.outsys[0][CV_OUT_S], 'L'); CHECK_EQ(d.outsys[0][CV_OUT_E], ' ');
        CHECK_EQ(d.outsys[1][CV_OUT_U], 'L'); CHECK_EQ(d.outsys[1][CV_OUT_S], 'G');
        CHECK_EQ(d.outsys[2][CV_OUT_S], 'G');
    }
    CHECK_EQ(d.norients, 1); CHECK_EQ(d.nelem_ori, 1); CHECK_EQ(d.nnode_tr, 1);
    cv_localsys L;
    CHECK(cv_localsys_init(&L, &d, &d.mesh));
    cv_field_desc st = { .name = "STRESS", .ncomp = 6, .comp = { "SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX" } };
    cv_field_desc ds = { .name = "DISP", .ncomp = 3, .comp = { "D1", "D2", "D3" } };
    float s[8 * 6], u[8 * 3];
    for (int i = 0; i < 8; i++) {
        float l[6] = { 1, 0, 0, 0.5f, 0, 0 };              /* e1 = global y, e2 = -x */
        memcpy(s + 6 * i, l, sizeof l);
        u[3 * i] = 1; u[3 * i + 1] = u[3 * i + 2] = 0;
    }
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &st, s), CV_LOC_TURNED);
    CHECK_NEAR(s[0], 0, 1e-6); CHECK_NEAR(s[1], 1, 1e-6); CHECK_NEAR(s[3], -0.5, 1e-6);
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 2, &st, s), 0);   /* GLOBAL=YES in step 2 */
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &ds, u), CV_LOC_TURNED);
    uint32_t n2 = cv_frd_node_index(&d.mesh, 2), n1 = cv_frd_node_index(&d.mesh, 1);
    CHECK_NEAR(u[3 * n2], 0, 1e-6); CHECK_NEAR(u[3 * n2 + 1], 1, 1e-6);   /* the transformed node */
    CHECK_NEAR(u[3 * n1], 1, 1e-6);                                       /* the others are global */
    cv_localsys_free(&L);

    /* .dat: *EL PRINT names the system after the values; a global record first */
    const char* dat =
        " stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set E1 and time  0.1000000E+01\n\n"
        "         1   1  2.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00\n"
        "         1   2  1.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00 OR1\n\n";
    cv_dat dt;
    CHECK(cv_dat_parse(&dt, dat, strlen(dat)));
    CHECK_EQ(dt.n, 1);
    if (dt.n == 1) {
        cv_dat_block* b = &dt.b[0];
        CHECK_EQ(b->n, 2); CHECK_EQ(b->nsys, 1);
        CHECK(b->sys && b->sys[0] == 0 && b->sys[1] == 1);
        CHECK(b->sysname && strcmp(b->sysname[0], "OR1") == 0);
        CHECK_EQ(cv_localsys_dat(&d, &d.mesh, b), CV_LOC_TURNED);
        CHECK_NEAR(b->vals[0], 2, 1e-6);                                  /* untouched */
        CHECK_NEAR(b->vals[6], 0, 1e-6); CHECK_NEAR(b->vals[7], 1, 1e-6);
        CHECK(b->sys[1] == 0);
        CHECK_EQ(cv_localsys_dat(&d, &d.mesh, b), 0);                     /* once only */
    }
    cv_dat_free(&dt); free(dt.msgs.a);
    cv_inp_free(&d); free(d.msgs.a);

    /* a shell has a system of its own even without *ORIENTATION (gen3dfrom2d.f):
       e3 its normal, e1 the global x on it. In the XZ plane: e3 = -y, e2 = z. */
    const char* sh =
        "*NODE\n1,0,0,0\n2,1,0,0\n3,1,0,1\n4,0,0,1\n"
        "*ELEMENT, TYPE=S4, ELSET=E\n1,1,2,3,4\n"
        "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n*SHELL SECTION, ELSET=E, MATERIAL=M\n0.1\n"
        "*STEP\n*STATIC\n*EL FILE, GLOBAL=NO\nS\n*END STEP\n";
    CHECK(cv_inp_parse(&d, sh, strlen(sh), NULL, NULL));
    CHECK_EQ(d.nshells, 1);
    CHECK(cv_localsys_init(&L, &d, &d.mesh));
    for (int i = 0; i < 4; i++) { float l[6] = { 0, 1, 3, 0, 0, 0 }; memcpy(s + 6 * i, l, sizeof l); }
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &st, s), CV_LOC_TURNED);
    CHECK_NEAR(s[0], 0, 1e-6); CHECK_NEAR(s[1], 3, 1e-6); CHECK_NEAR(s[2], 1, 1e-6);
    cv_localsys_free(&L);
    const char* sdat =
        " stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set E and time  0.1000000E+01\n\n"
        "         1   1  0.000000E+00  1.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00 _shell_0000000001\n\n";
    CHECK(cv_dat_parse(&dt, sdat, strlen(sdat)));
    if (dt.n == 1) {
        CHECK(dt.b[0].nsys == 1 && strcmp(dt.b[0].sysname[0], "_shell_") == 0);
        CHECK_EQ(cv_localsys_dat(&d, &d.mesh, &dt.b[0]), CV_LOC_TURNED);
        CHECK_NEAR(dt.b[0].vals[1], 0, 1e-6); CHECK_NEAR(dt.b[0].vals[2], 1, 1e-6);
    }
    cv_dat_free(&dt); free(dt.msgs.a);
    CHECK(d.shell_off == NULL);
    cv_inp_free(&d); free(d.msgs.a);

    /* *SHELL SECTION, OFFSET=: per element, the last section naming it; none: NULL */
    const char* so =
        "*NODE\n1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n5,2,0,0\n6,2,1,0\n"
        "*ELEMENT, TYPE=S4, ELSET=E\n1,1,2,3,4\n*ELEMENT, TYPE=S4, ELSET=F\n2,2,5,6,3\n"
        "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n*SHELL SECTION, ELSET=E, MATERIAL=M, OFFSET=0.5\n0.1\n"
        "*SHELL SECTION, ELSET=F, MATERIAL=M, OFFSET=-.25\n0.1\n*STEP\n*STATIC\n*END STEP\n";
    CHECK(cv_inp_parse(&d, so, strlen(so), NULL, NULL));
    CHECK(d.shell_off != NULL);
    if (d.shell_off) {
        CHECK_NEAR(d.shell_off[cv_frd_elem_index(&d.mesh, 1)], 0.5, 1e-6);
        CHECK_NEAR(d.shell_off[cv_frd_elem_index(&d.mesh, 2)], -0.25, 1e-6);
    }
    cv_inp_free(&d); free(d.msgs.a);
}

/* .frd of hex8 elements eid[k], each on 8 nodes of its own (ids 10k+1..), the unit
   cube moved by at[k] */
static buf_t own_hexes(uint32_t ne, const uint32_t* eid, const double (*at)[3]) {
    static const double c[8][3] = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}, {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1} };
    uint32_t id[80], conn[80];
    double xyz[240];
    for (uint32_t k = 0; k < ne; k++)
        for (int j = 0; j < 8; j++) {
            uint32_t i = 8 * k + (uint32_t)j;
            id[i] = conn[i] = 10 * k + (uint32_t)j + 1;
            for (int a = 0; a < 3; a++) xyz[3 * i + a] = c[j][a] + at[k][a];
        }
    FILE* o = tmp_open();
    fw_nodes(o, 8 * ne, id, xyz, 0);
    fw_elems(o, ne, eid, 1, 8, conn, NULL, 0);
    fprintf(o, "9999\n");
    return slurp(o);
}

static void fill6(float* v, uint32_t n, const float l[6]) {
    for (uint32_t i = 0; i < n; i++) memcpy(v + 6 * i, l, 6 * sizeof(float));
}

/* the turns themselves, against the matrix product written out */
static void test_csys_math(void) {
    double a = 0.3, b = -1.1, g = 2.0, T[3][3], Q[3][3];
    double cz = cos(a), sz = sin(a), cy = cos(b), sy = sin(b), cx = cos(g), sx = sin(g);
    const double Rz[3][3] = { { cz, -sz, 0 }, { sz, cz, 0 }, { 0, 0, 1 } };
    const double Ry[3][3] = { { cy, 0, sy }, { 0, 1, 0 }, { -sy, 0, cy } };
    const double Rx[3][3] = { { 1, 0, 0 }, { 0, cx, -sx }, { 0, sx, cx } };
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        T[i][j] = 0; for (int k = 0; k < 3; k++) T[i][j] += Rz[i][k] * Ry[k][j];
    }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        Q[i][j] = 0; for (int k = 0; k < 3; k++) Q[i][j] += T[i][k] * Rx[k][j];
    }
    /* local tensor, shears XY YZ ZX; global G = Q^T S Q (rows of Q: local axes) */
    float s[6] = { 3, -1, 2, 0.5f, -0.7f, 1.3f };
    const double S[3][3] = { { 3, 0.5, 1.3 }, { 0.5, -1, -0.7 }, { 1.3, -0.7, 2 } };
    double G[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        G[i][j] = 0;
        for (int k = 0; k < 3; k++) for (int l = 0; l < 3; l++) G[i][j] += Q[k][i] * S[k][l] * Q[l][j];
    }
    cv_ten_to_global(Q, s);
    CHECK_NEAR(s[0], G[0][0], 1e-5); CHECK_NEAR(s[1], G[1][1], 1e-5); CHECK_NEAR(s[2], G[2][2], 1e-5);
    CHECK_NEAR(s[3], G[0][1], 1e-5); CHECK_NEAR(s[4], G[1][2], 1e-5); CHECK_NEAR(s[5], G[2][0], 1e-5);
    float v[3] = { 1, 2, -3 };
    double w[3];
    for (int i = 0; i < 3; i++) w[i] = Q[0][i] * 1 + Q[1][i] * 2 + Q[2][i] * -3;
    cv_vec_to_global(Q, v);
    for (int i = 0; i < 3; i++) CHECK_NEAR(v[i], w[i], 1e-5);

    /* rectangular: a along e1, b made normal to it in the a-b plane */
    cv_csys r = { { 2, 0, 0, 1, 3, 0 }, false };
    float p0[3] = { 5, 5, 5 };
    cv_csys_axes(&r, p0, Q);
    for (int i = 0; i < 9; i++) CHECK_NEAR(Q[i / 3][i % 3], i % 4 == 0, 1e-12);
    /* cylindrical (a, b on the axis): e1 radial, e3 along a->b, e2 = e3 x e1 */
    cv_csys cy2 = { { 0, 0, 0, 0, 0, 2 }, true };
    float p1[3] = { 0, 3, 7 };
    cv_csys_axes(&cy2, p1, Q);
    CHECK_NEAR(Q[0][1], 1, 1e-12); CHECK_NEAR(Q[1][0], -1, 1e-12); CHECK_NEAR(Q[2][2], 1, 1e-12);
    float p2[3] = { 0, 0, 4 };                    /* on the axis: some unit e1 normal to it */
    cv_csys_axes(&cy2, p2, Q);
    double l = 0, dt = 0;
    for (int k = 0; k < 3; k++) { l += Q[0][k] * Q[0][k]; dt += Q[0][k] * Q[2][k]; }
    CHECK_NEAR(l, 1, 1e-12); CHECK_NEAR(dt, 0, 1e-12);
}

#define CUBE_NODES "1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n5,0,0,1\n6,1,0,1\n7,1,1,1\n8,0,1,1\n"
#define S_LINE(el, ip, name) "         " el "   " ip "  1.000000E+00  0.000000E+00  0.000000E+00" \
    "  0.000000E+00  0.000000E+00  0.000000E+00 " name "\n"
#define S_HEAD(set) " stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set " set \
    " and time  0.1000000E+01\n\n"

/* composite shells: the .frd adds one element per layer, numbered on from the
   largest element (frd.c), each on nodes of its own, each in its layer's system */
static void test_localsys_layers(void) {
    cv_field_desc st = { .name = "STRESS", .ncomp = 6, .comp = { "SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX" } };
    const float sx[6] = { 1, 0, 0, 0, 0, 0 };     /* local: sxx = 1 */
    float s[64 * 6];
    cv_inp d;
    cv_localsys L;
    cv_frd f;
    buf_t b;
    const char* comp =
        "*NODE\n1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n"
        "*ELEMENT, TYPE=S4, ELSET=E\n1,1,2,3,4\n"
        "*ORIENTATION, NAME=ORA\n1.,1.,0.,-1.,1.,0.\n"
        "*ORIENTATION, NAME=ORB\n0.,1.,0.,-1.,0.,0.\n"
        "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n"
        "*SHELL SECTION, ELSET=E, COMPOSITE\n0.05,,M,ORA\n0.05,,M,ORB\n"
        "*STEP\n*STATIC\n*EL FILE, GLOBAL=NO\nS\n*END STEP\n";
    CHECK(cv_inp_parse(&d, comp, strlen(comp), NULL, NULL));
    CHECK_EQ(d.ncomps, 1);
    if (d.ncomps == 1) {
        CHECK_EQ(d.comps[0].nlay, 2);
        CHECK_EQ(d.layer_ori[d.comps[0].lay0], 0); CHECK_EQ(d.layer_ori[d.comps[0].lay0 + 1], 1);
    }
    {
        const uint32_t eid[3] = { 1, 3, 2 };      /* out of order on purpose */
        const double at[3][3] = { { 0, 0, 0 }, { 0, 0, 4 }, { 0, 0, 2 } };
        b = own_hexes(3, eid, at);
        CHECK(cv_frd_parse(&f, b.p, b.n));
        CHECK(cv_localsys_init(&L, &d, &f));
        fill6(s, f.n_nodes, sx);
        CHECK_EQ(cv_localsys_apply(&L, &d, &f, 1, &st, s), CV_LOC_TURNED | CV_LOC_NAN);
        uint32_t n1 = cv_frd_node_index(&f, 1), n2 = cv_frd_node_index(&f, 21), n3 = cv_frd_node_index(&f, 11);
        CHECK(s[6 * n1] != s[6 * n1]);           /* the shell itself: its layers differ */
        /* element 2 = layer 1 (ORA): e1 = (1,1,0)/sqrt2 */
        CHECK_NEAR(s[6 * n2], 0.5, 1e-6); CHECK_NEAR(s[6 * n2 + 1], 0.5, 1e-6); CHECK_NEAR(s[6 * n2 + 3], 0.5, 1e-6);
        /* element 3 = layer 2 (ORB): e1 = y */
        CHECK_NEAR(s[6 * n3], 0, 1e-6); CHECK_NEAR(s[6 * n3 + 1], 1, 1e-6); CHECK_NEAR(s[6 * n3 + 3], 0, 1e-6);
        cv_localsys_free(&L); cv_frd_free(&f); free(f.msgs.a); free(b.p);
    }
    {   /* one element too many: the layers cannot be matched, all NaN */
        const uint32_t eid[4] = { 1, 2, 3, 4 };
        const double at[4][3] = { { 0, 0, 0 }, { 0, 0, 2 }, { 0, 0, 4 }, { 0, 0, 6 } };
        b = own_hexes(4, eid, at);
        CHECK(cv_frd_parse(&f, b.p, b.n));
        CHECK(cv_localsys_init(&L, &d, &f));
        fill6(s, f.n_nodes, sx);
        CHECK_EQ(cv_localsys_apply(&L, &d, &f, 1, &st, s), CV_LOC_NAN);
        int nan = 0;
        for (uint32_t i = 0; i < f.n_nodes; i++) nan += s[6 * i] != s[6 * i];
        CHECK_EQ(nan, 32);
        cv_localsys_free(&L); cv_frd_free(&f); free(f.msgs.a); free(b.p);
    }
    cv_inp_free(&d); free(d.msgs.a);

    /* layers in one orientation: the shell too; a layer naming an orientation that
       does not exist cannot be rebuilt */
    const char* comp2 =
        "*NODE\n1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n"
        "*ELEMENT, TYPE=S4, ELSET=E\n1,1,2,3,4\n"
        "*ELEMENT, TYPE=S4, ELSET=F\n2,1,2,3,4\n"
        "*ORIENTATION, NAME=ORB\n0.,1.,0.,-1.,0.,0.\n"
        "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n"
        "*SHELL SECTION, ELSET=E, COMPOSITE\n0.05,,M,ORB\n0.05,,M,ORB\n"
        "*SHELL SECTION, ELSET=F, COMPOSITE\n0.05,,M,ORB\n0.05,,M,NOSUCH\n"
        "*STEP\n*STATIC\n*EL FILE, GLOBAL=NO\nS\n*END STEP\n";
    CHECK(cv_inp_parse(&d, comp2, strlen(comp2), NULL, NULL));
    CHECK_EQ(d.ncomps, 2);
    {   /* layers: 3, 4 of element 1; 5, 6 of element 2 */
        const uint32_t eid[6] = { 1, 2, 3, 4, 5, 6 };
        const double at[6][3] = { { 0, 0, 0 }, { 0, 0, 2 }, { 0, 0, 4 }, { 0, 0, 6 }, { 0, 0, 8 }, { 0, 0, 10 } };
        b = own_hexes(6, eid, at);
        CHECK(cv_frd_parse(&f, b.p, b.n));
        CHECK(cv_localsys_init(&L, &d, &f));
        fill6(s, f.n_nodes, sx);
        CHECK_EQ(cv_localsys_apply(&L, &d, &f, 1, &st, s), CV_LOC_TURNED | CV_LOC_NAN);
        const uint32_t probe[6] = { 1, 11, 21, 31, 41, 51 };
        const int turned[6] = { 1, 0, 1, 1, 1, 0 };     /* element 2: mixed; layer 6: NOSUCH */
        for (int k = 0; k < 6; k++) {
            uint32_t n = cv_frd_node_index(&f, probe[k]);
            if (turned[k]) { CHECK_NEAR(s[6 * n], 0, 1e-6); CHECK_NEAR(s[6 * n + 1], 1, 1e-6); }
            else CHECK(s[6 * n] != s[6 * n]);
        }
        cv_localsys_free(&L); cv_frd_free(&f); free(f.msgs.a); free(b.p);
    }
    cv_inp_free(&d); free(d.msgs.a);
}

/* systems that turn inside an element or differ between neighbours */
static void test_localsys_spread(void) {
    cv_field_desc st = { .name = "STRESS", .ncomp = 6, .comp = { "SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX" } };
    const float sx[6] = { 1, 0, 0, 0, 0, 0 };
    float s[16 * 6];
    cv_inp d;
    cv_localsys L;
    const char* tail = "*SOLID SECTION, ELSET=E1, MATERIAL=M, ORIENTATION=OC\n"
                       "*STEP\n*STATIC\n*EL FILE, GLOBAL=NO\nS\n*END STEP\n";
    char deck[2048];

    /* cylindrical, axis z through (0,-10): CalculiX turns at each integration point
       and averages at the nodes, here undone with the node's own system: approximate */
    snprintf(deck, sizeof deck, "*NODE\n" CUBE_NODES "*ELEMENT, TYPE=C3D8, ELSET=E1\n1,1,2,3,4,5,6,7,8\n"
             "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n*ORIENTATION, NAME=OC, SYSTEM=C\n0.,-10.,0.,0.,-10.,1.\n%s", tail);
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    CHECK(cv_localsys_init(&L, &d, &d.mesh));
    fill6(s, 8, sx);
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &st, s), CV_LOC_TURNED | CV_LOC_APPROX);
    {
        uint32_t n = cv_frd_node_index(&d.mesh, 3);         /* (1,1,0): e1 = (1,11,0)/sqrt(122) */
        CHECK_NEAR(s[6 * n], 1.0 / 122, 1e-6); CHECK_NEAR(s[6 * n + 1], 121.0 / 122, 1e-6);
        CHECK_NEAR(s[6 * n + 3], 11.0 / 122, 1e-6); CHECK_NEAR(s[6 * n + 2], 0, 1e-6);
    }
    cv_localsys_free(&L);

    /* the .dat names the system at each integration point: exact there. C3D8 points
       run x fastest at 0.5 -+ 0.5/sqrt3: point 2 is (+,-,-), point 7 (-,+,+) */
    {
        const char* dat = S_HEAD("E1")
            S_LINE("1", "1", "OC") S_LINE("1", "2", "OC") S_LINE("1", "3", "OC") S_LINE("1", "4", "OC")
            S_LINE("1", "5", "OC") S_LINE("1", "6", "OC") S_LINE("1", "7", "OC") S_LINE("1", "8", "OC") "\n";
        cv_dat dt;
        CHECK(cv_dat_parse(&dt, dat, strlen(dat)));
        CHECK(dt.n == 1 && dt.b[0].n == 8);
        if (dt.n == 1 && dt.b[0].n == 8) {
            CHECK_EQ(cv_localsys_dat(&d, &d.mesh, &dt.b[0]), CV_LOC_TURNED);
            double lo = 0.5 - 0.5 / sqrt(3.0), hi = 0.5 + 0.5 / sqrt(3.0);
            const double xy[2][2] = { { hi, lo + 10 }, { lo, hi + 10 } };
            const int ips[2] = { 2, 7 };
            for (int j = 0; j < 2; j++) {
                double r = sqrt(xy[j][0] * xy[j][0] + xy[j][1] * xy[j][1]), ex = xy[j][0] / r, ey = xy[j][1] / r;
                const float* v = dt.b[0].vals + 6 * (ips[j] - 1);
                CHECK_NEAR(v[0], ex * ex, 1e-6); CHECK_NEAR(v[1], ey * ey, 1e-6); CHECK_NEAR(v[3], ex * ey, 1e-6);
            }
        }
        cv_dat_free(&dt); free(dt.msgs.a);
    }
    cv_inp_free(&d); free(d.msgs.a);

    /* the axis through the element: its system turns too far inside it, NaN */
    snprintf(deck, sizeof deck, "*NODE\n" CUBE_NODES "*ELEMENT, TYPE=C3D8, ELSET=E1\n1,1,2,3,4,5,6,7,8\n"
             "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n*ORIENTATION, NAME=OC, SYSTEM=C\n0.,0.,0.,0.,0.,1.\n%s", tail);
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    CHECK(cv_localsys_init(&L, &d, &d.mesh));
    fill6(s, 8, sx);
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &st, s), CV_LOC_NAN);
    cv_localsys_free(&L);
    cv_inp_free(&d); free(d.msgs.a);

    /* two orientations meeting at x = 1, 45 degrees apart: NaN on the shared face only */
    const char* two =
        "*NODE\n" CUBE_NODES "9,2,0,0\n10,2,1,0\n11,2,0,1\n12,2,1,1\n"
        "*ELEMENT, TYPE=C3D8, ELSET=EA\n1,1,2,3,4,5,6,7,8\n"
        "*ELEMENT, TYPE=C3D8, ELSET=EB\n2,2,9,10,3,6,11,12,7\n"
        "*ORIENTATION, NAME=OA\n1.,1.,0.,-1.,1.,0.\n*ORIENTATION, NAME=OB\n0.,1.,0.,-1.,0.,0.\n"
        "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n"
        "*SOLID SECTION, ELSET=EA, MATERIAL=M, ORIENTATION=OA\n*SOLID SECTION, ELSET=EB, MATERIAL=M, ORIENTATION=OB\n"
        "*STEP\n*STATIC\n*EL FILE, GLOBAL=NO\nS\n*END STEP\n";
    CHECK(cv_inp_parse(&d, two, strlen(two), NULL, NULL));
    CHECK(cv_localsys_init(&L, &d, &d.mesh));
    fill6(s, 12, sx);
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &st, s), CV_LOC_TURNED | CV_LOC_NAN);
    for (uint32_t id = 1; id <= 12; id++) {
        uint32_t n = cv_frd_node_index(&d.mesh, id);
        bool shared = id == 2 || id == 3 || id == 6 || id == 7;
        if (shared) CHECK(s[6 * n] != s[6 * n]);
        else if (id <= 8) { CHECK_NEAR(s[6 * n], 0.5, 1e-6); CHECK_NEAR(s[6 * n + 3], 0.5, 1e-6); }
        else { CHECK_NEAR(s[6 * n], 0, 1e-6); CHECK_NEAR(s[6 * n + 1], 1, 1e-6); }
    }
    cv_localsys_free(&L);
    cv_inp_free(&d); free(d.msgs.a);
}

/* .dat record names: at most 20 characters; a name alike in 20 or unknown falls back
   to the element's section; an element without one is NaN */
static void test_localsys_datnames(void) {
    cv_inp d;
    cv_dat dt;
    const char* names =
        "*NODE\n" CUBE_NODES "9,2,0,0\n10,2,1,0\n11,2,0,1\n12,2,1,1\n"
        "*ELEMENT, TYPE=C3D8, ELSET=EA\n1,1,2,3,4,5,6,7,8\n"
        "*ELEMENT, TYPE=C3D8, ELSET=EB\n2,2,9,10,3,6,11,12,7\n"
        "*ORIENTATION, NAME=Abcdefghijklmnopqrstuv\n0.,1.,0.,-1.,0.,0.\n"
        "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n"
        "*SOLID SECTION, ELSET=EA, MATERIAL=M, ORIENTATION=ABCDEFGHIJKLMNOPQRSTUV\n*SOLID SECTION, ELSET=EB, MATERIAL=M\n"
        "*STEP\n*STATIC\n*EL PRINT, ELSET=EA\nS\n*END STEP\n";
    CHECK(cv_inp_parse(&d, names, strlen(names), NULL, NULL));
    {
        const char* dat = S_HEAD("EA")
            S_LINE("1", "1", "ABCDEFGHIJKLMNOPQRST") S_LINE("1", "2", "NOPE") S_LINE("2", "1", "NOPE") "\n"
            " heat flux (elem, integ.pnt.,qx,qy,qz) for set EA and time  0.1000000E+01\n\n"
            "         1   1  1.000000E+00  0.000000E+00  0.000000E+00 ABCDEFGHIJKLMNOPQRST\n\n";
        CHECK(cv_dat_parse(&dt, dat, strlen(dat)));
        CHECK_EQ(dt.n, 2);
        if (dt.n == 2) {
            CHECK_EQ(dt.b[0].nsys, 2);
            CHECK_EQ(cv_localsys_dat(&d, &d.mesh, &dt.b[0]), CV_LOC_TURNED | CV_LOC_NAN);
            const float* v = dt.b[0].vals;
            CHECK_NEAR(v[0], 0, 1e-6); CHECK_NEAR(v[1], 1, 1e-6);         /* 20 characters of the name */
            CHECK_NEAR(v[6], 0, 1e-6); CHECK_NEAR(v[7], 1, 1e-6);         /* unknown: element 1's */
            CHECK(v[12] != v[12]);                                        /* element 2 has none */
            CHECK_EQ(cv_localsys_dat(&d, &d.mesh, &dt.b[1]), CV_LOC_TURNED);  /* a vector */
            CHECK_NEAR(dt.b[1].vals[0], 0, 1e-6); CHECK_NEAR(dt.b[1].vals[1], 1, 1e-6);
        }
        cv_dat_free(&dt); free(dt.msgs.a);
    }
    cv_inp_free(&d); free(d.msgs.a);

    /* two names alike in their first 20 characters: the element's section decides */
    const char* alike =
        "*NODE\n" CUBE_NODES "*ELEMENT, TYPE=C3D8, ELSET=EA\n1,1,2,3,4,5,6,7,8\n"
        "*ORIENTATION, NAME=ABCDEFGHIJKLMNOPQRSTUV\n0.,1.,0.,-1.,0.,0.\n"
        "*ORIENTATION, NAME=ABCDEFGHIJKLMNOPQRSTXX\n1.,1.,0.,-1.,1.,0.\n"
        "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n"
        "*SOLID SECTION, ELSET=EA, MATERIAL=M, ORIENTATION=ABCDEFGHIJKLMNOPQRSTUV\n"
        "*STEP\n*STATIC\n*EL PRINT, ELSET=EA\nS\n*END STEP\n";
    CHECK(cv_inp_parse(&d, alike, strlen(alike), NULL, NULL));
    {
        const char* dat = S_HEAD("EA") S_LINE("1", "1", "ABCDEFGHIJKLMNOPQRST") "\n";
        CHECK(cv_dat_parse(&dt, dat, strlen(dat)));
        if (dt.n == 1) {
            CHECK_EQ(cv_localsys_dat(&d, &d.mesh, &dt.b[0]), CV_LOC_TURNED);
            CHECK_NEAR(dt.b[0].vals[0], 0, 1e-6); CHECK_NEAR(dt.b[0].vals[1], 1, 1e-6);
        }
        cv_dat_free(&dt); free(dt.msgs.a);
    }
    cv_inp_free(&d); free(d.msgs.a);

    /* a shell with an orientation: "<orientation>_shell_<element>", cut at 20 */
    const char* sh =
        "*NODE\n1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n"
        "*ELEMENT, TYPE=S4, ELSET=E\n1,1,2,3,4\n"
        "*ORIENTATION, NAME=ORB\n0.,1.,0.,-1.,0.,0.\n"
        "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n*SHELL SECTION, ELSET=E, MATERIAL=M, ORIENTATION=ORB\n0.1\n"
        "*STEP\n*STATIC\n*EL FILE, GLOBAL=NO\nS\n*END STEP\n";
    CHECK(cv_inp_parse(&d, sh, strlen(sh), NULL, NULL));
    {
        const char* dat = S_HEAD("E") S_LINE("1", "1", "ORB_shell_0000000001") "\n";
        CHECK(cv_dat_parse(&dt, dat, strlen(dat)));
        if (dt.n == 1) {
            CHECK(dt.b[0].nsys == 1 && strcmp(dt.b[0].sysname[0], "ORB_shell_") == 0);
            CHECK_EQ(cv_localsys_dat(&d, &d.mesh, &dt.b[0]), CV_LOC_TURNED);
            CHECK_NEAR(dt.b[0].vals[0], 0, 1e-6); CHECK_NEAR(dt.b[0].vals[1], 1, 1e-6);
        }
        cv_dat_free(&dt); free(dt.msgs.a);
    }
    {   /* and in the .frd */
        cv_field_desc st = { .name = "STRESS", .ncomp = 6, .comp = { "SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX" } };
        const float sx[6] = { 1, 0, 0, 0, 0, 0 };
        float s[4 * 6];
        cv_localsys L;
        CHECK(cv_localsys_init(&L, &d, &d.mesh));
        fill6(s, 4, sx);
        CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &st, s), CV_LOC_TURNED);
        CHECK_NEAR(s[0], 0, 1e-6); CHECK_NEAR(s[1], 1, 1e-6);
        cv_localsys_free(&L);
    }
    cv_inp_free(&d); free(d.msgs.a);
}

/* which request decides which block: the first card of a step clears its kind,
   *NODE OUTPUT / *ELEMENT OUTPUT count as *NODE FILE / *EL FILE; FORCI goes with U,
   MESTRAIN with E; PSTRESS cannot be undone */
static void test_localsys_requests(void) {
    const char* req =
        "*NODE, NSET=NALL\n" CUBE_NODES "*ELEMENT, TYPE=C3D8, ELSET=E1\n1,1,2,3,4,5,6,7,8\n"
        "*NSET, NSET=NT\n2\n*TRANSFORM, NSET=NT\n0.,1.,0.,-1.,0.,0.\n"
        "*ORIENTATION, NAME=OR1\n0.,1.,0.,-1.,0.,0.\n"
        "*MATERIAL, NAME=M\n*ELASTIC\n1., 0.\n*SOLID SECTION, ELSET=E1, MATERIAL=M, ORIENTATION=OR1\n"
        "*STEP\n*STATIC\n*NODE FILE, GLOBAL=NO\nU\n*NODE FILE\nRF\n*EL FILE, GLOBAL=NO\nS\n*END STEP\n"
        "*STEP\n*STATIC\n*NODE OUTPUT\nRF\n*ELEMENT OUTPUT, GLOBAL=NO\nE\n*END STEP\n"
        "*STEP\n*STATIC\n*EL FILE\nS\n*EL FILE, GLOBAL=NO\nE\n*END STEP\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, req, strlen(req), NULL, NULL));
    CHECK_EQ(d.nsteps, 3);
    if (d.nsteps == 3) {
        CHECK_EQ(d.outsys[0][CV_OUT_U], 'L'); CHECK_EQ(d.outsys[0][CV_OUT_RF], 'G');
        CHECK_EQ(d.outsys[0][CV_OUT_S], 'L'); CHECK_EQ(d.outsys[0][CV_OUT_E], ' ');
        CHECK_EQ(d.outsys[1][CV_OUT_U], ' '); CHECK_EQ(d.outsys[1][CV_OUT_RF], 'G');
        CHECK_EQ(d.outsys[1][CV_OUT_S], ' '); CHECK_EQ(d.outsys[1][CV_OUT_E], 'L');
        CHECK_EQ(d.outsys[2][CV_OUT_U], ' '); CHECK_EQ(d.outsys[2][CV_OUT_RF], 'G');
        CHECK_EQ(d.outsys[2][CV_OUT_S], 'G'); CHECK_EQ(d.outsys[2][CV_OUT_E], 'L');
    }
    cv_localsys L;
    CHECK(cv_localsys_init(&L, &d, &d.mesh));
    cv_field_desc st = { .name = "STRESS", .ncomp = 6, .comp = { "SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX" } };
    cv_field_desc fi = { .name = "FORCI", .ncomp = 3, .comp = { "F1", "F2", "F3" } };
    cv_field_desc fo = { .name = "FORC", .ncomp = 3, .comp = { "F1", "F2", "F3" } };
    cv_field_desc me = { .name = "MESTRAIN", .ncomp = 6, .comp = { "MEXX", "MEYY", "MEZZ", "MEXY", "MEYZ", "MEZX" } };
    cv_field_desc te = { .name = "TOSTRAIN", .ncomp = 6, .comp = { "EXX", "EYY", "EZZ", "EXY", "EYZ", "EZX" } };
    cv_field_desc ps = { .name = "PSTRESS", .ncomp = 3, .comp = { "PS1", "PS2", "PS3" } };
    cv_field_desc nt = { .name = "NDTEMP", .ncomp = 1, .comp = { "T" } };
    const float sx[6] = { 1, 0, 0, 0, 0, 0 };
    float u[24], s[48], p[24];
    uint32_t n2 = cv_frd_node_index(&d.mesh, 2);
    for (int i = 0; i < 24; i++) u[i] = i % 3 == 0;
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &fo, u), 0);
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &fi, u), CV_LOC_TURNED);
    CHECK_NEAR(u[3 * n2], 0, 1e-6); CHECK_NEAR(u[3 * n2 + 1], 1, 1e-6);
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &nt, u), 0);
    fill6(s, 8, sx);
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &me, s), 0);             /* E not requested */
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 2, &me, s), CV_LOC_TURNED);
    CHECK_NEAR(s[1], 1, 1e-6);
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 2, &st, s), 0);             /* S cleared in step 2 */
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 3, &st, s), 0);
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 9, &te, s), CV_LOC_TURNED); /* past the last: the last */
    for (int i = 0; i < 24; i++) p[i] = 1;
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 3, &ps, p), 0);
    CHECK_EQ(cv_localsys_apply(&L, &d, &d.mesh, 1, &ps, p), CV_LOC_NAN);
    CHECK(p[0] != p[0] && p[23] != p[23]);
    cv_localsys_free(&L);
    cv_inp_free(&d); free(d.msgs.a);
}

#include "t_calc.h"
#include "t_units.h"
#include "t_loads.h"
#include "../src/cap.h"
#include "../src/label.h"
#include "t_cap.h"
#include "t_label.h"
#include "t_glyph.h"
#include "../src/traj.h"
#include "t_traj.h"
#include "t_failure.h"
#include "t_quality.h"
#include "t_shell.h"
#include "t_rebar.h"
#include "t_stl.h"
#include "t_cel.h"
#include "t_contact.h"
#include "t_clayer.h"
#include "t_integ.h"
#include "t_selset.h"
#include "t_seltopo.h"
#include "t_selfilter.h"

int main(void) {
    test_gpu_env();
    test_calc();
    test_units();
    test_loads();
    test_steps();
    test_load_cards();
    test_cap();
    test_label();
    test_glyph();
    test_traj();
    test_failure();
    test_quality();
    test_shell();
    test_rebar();
    test_integ();
    test_selset();
    test_seltopo();
    test_selfilter();
    test_video();
    test_cfg();
    test_idlist();
    test_export();
    test_fprintf();
    test_path();
    test_stl();
    test_cel();
    test_contact();
    test_clayer();
    test_anchor();
    test_measure();
    test_tbtext();
    test_feature_edges();
    test_plane_mask();
    test_box_elems();
    test_mid_faces();
    test_sta();
    test_fbd();
    test_inp();
    test_inp_procs();
    test_rot_node();
    test_localsys();
    test_csys_math();
    test_localsys_layers();
    test_localsys_spread();
    test_localsys_datnames();
    test_localsys_requests();
    test_match();
    test_gauss();
    test_dat();
    test_portal_wire();
    test_list_dir();
    test_numbers();
    test_ascii();
    test_binary();
    test_truncated();
    test_fuzz();
    test_absurd_header();
    test_modal_flag();
    test_frd_head();
    test_missing_node();
    test_continuation_and_nan();
    test_shear_order();
    test_field_math();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
