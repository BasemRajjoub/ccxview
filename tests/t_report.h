/* t_report.h -- the Markdown model summary (report.c). Included by test_main.c. */
#include "../src/report.h"

static char* rep_text(const cv_frd* f, const cv_inp* d, const cv_report_opts* o) {
    char* buf = NULL; size_t n = 0;
    FILE* fp = open_memstream(&buf, &n);
    if (!fp) return NULL;
    bool ok = cv_report_md(fp, f, d, o);
    fclose(fp);
    if (!ok) { free(buf); return NULL; }
    return buf;
}

static void test_report(void) {
    const char* deck =
        "*HEADING\nreport | test\n"
        "*NODE\n1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n5,0,0,1\n6,1,0,1\n7,1,1,1\n8,0,1,1\n9,2,0,0\n"
        "*ELEMENT, TYPE=C3D8, ELSET=BLOCK\n1,1,2,3,4,5,6,7,8\n"
        "*ELEMENT, TYPE=B31, ELSET=BEAM\n2,2,9\n"
        "*NSET, NSET=FIXED\n1,4,5,8\n"
        "*MATERIAL, NAME=STEEL|X\n*ELASTIC\n210000.,0.3\n*PLASTIC\n355.,0.\n"
        "*SOLID SECTION, ELSET=BLOCK, MATERIAL=STEEL|X\n"
        "*AMPLITUDE, NAME=RAMP\n0,0,1,1\n"
        "*STEP\n*STATIC\n1.,1.\n*BOUNDARY\nFIXED,1,3\n*CLOAD\n6,3,-50.\n*DLOAD\n1,P2,3.5\n*END STEP\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    cv_report_opts o = { NULL, "dir/model.inp", CV_SYS_MM_T_S };
    char* t = rep_text(NULL, &d, &o);
    CHECK(t != NULL);
    if (t) {
        CHECK(strstr(t, "| Nodes | 9 |"));
        CHECK(strstr(t, "| Elements | 2 |"));
        CHECK(strstr(t, "| Hex8 | 3D | 1 |"));
        CHECK(strstr(t, "| Line2 | 1D | 1 |"));
        CHECK(strstr(t, "| BLOCK | 1 |"));
        CHECK(strstr(t, "| FIXED | 4 |"));
        CHECK(strstr(t, "STEEL\\|X"));
        CHECK(strstr(t, "E = 210000, nu = 0.3"));
        CHECK(strstr(t, "| 355 |"));
        CHECK(strstr(t, "| solid | BLOCK | STEEL\\|X | 1 |"));
        CHECK(strstr(t, "### Step 1"));
        CHECK(strstr(t, "| 1-3 | 0 (held) |"));
        CHECK(strstr(t, "| 3 | -50 |"));
        CHECK(strstr(t, "| 1 | Static | 1 |"));
        CHECK(strstr(t, "model.inp"));
        CHECK(strstr(t, "not read"));
        free(t);
    }
    CHECK(!rep_text(NULL, NULL, NULL));
    cv_inp_free(&d);
    /* the samples, when they are there: the showcase result with its deck, a result with its deck, a deck alone */
    static const char* S[][2] = { { "samples/showcase/showcase.frd", "samples/showcase/showcase.inp" },
                                  { "samples/element_based_stress/SANDWICH_MASTER.frd", "samples/element_based_stress/SANDWICH_MESH.inp" },
                                  { NULL, "samples/rbe_couplings/rbe_couplings.inp" } };
    for (int k = 0; k < 3; k++) {
        cv_map mf = {0}, mi = {0};
        cv_frd f; cv_inp di;
        bool hf = S[k][0] && cv_map_open(&mf, S[k][0]) && cv_frd_parse(&f, mf.data, mf.size);
        bool hi = S[k][1] && cv_map_open(&mi, S[k][1]) && cv_inp_parse(&di, mi.data, mi.size, NULL, NULL);
        if (hf || hi) {
            char* r = rep_text(hf ? &f : NULL, hi ? &di : NULL, NULL);
            CHECK(r && strstr(r, "# Model summary") && strstr(r, "| Nodes |"));
            if (r) {
                char name[64];
                snprintf(name, sizeof name, "build/report_sample%d.md", k);
                FILE* w = fopen(name, "w");
                if (w) { fputs(r, w); fclose(w); }
            }
            free(r);
        }
        if (hi) cv_inp_free(&di);
        if (hf) cv_frd_free(&f);
        if (mf.data) cv_map_close(&mf);
        if (mi.data) cv_map_close(&mi);
    }
}
