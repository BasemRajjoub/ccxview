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

/* material properties, sections, named loads, step options: the parser keeps them and the report shows them */
static void test_report_props(void) {
    const char* deck =
        "*NODE\n1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n5,2,0,0\n6,3,0,0\n7,4,0,0\n8,5,0,0\n"
        "*ELEMENT, TYPE=S4, ELSET=PL\n1,1,2,3,4\n"
        "*ELEMENT, TYPE=S4, ELSET=CO\n2,2,5,5,3\n"
        "*ELEMENT, TYPE=B31, ELSET=BR\n3,5,6\n"
        "*ELEMENT, TYPE=B31, ELSET=BP\n4,6,7\n"
        "*NSET, NSET=NFIX\n1,4\n*SURFACE, NAME=SURF1, TYPE=ELEMENT\nPL,SPOS\n"
        "*MATERIAL, NAME=M1\n*ELASTIC\n200000.,0.3\n*DENSITY\n7.8e-9\n*CONDUCTIVITY\n45.\n*EXPANSION, ZERO=20.\n1.2e-5\n*SPECIFIC HEAT\n4.6e8\n"
        "*MATERIAL, NAME=M2\n*ELASTIC\n70000.,0.33\n*DENSITY\n2.7e-9,20.\n2.6e-9,200.\n"
        "*SHELL SECTION, ELSET=PL, MATERIAL=M1, OFFSET=0.5\n2.5,5\n"
        "*SHELL SECTION, ELSET=CO, COMPOSITE\n0.5,,M1\n0.75,,M2\n"
        "*BEAM SECTION, ELSET=BR, MATERIAL=M1, SECTION=RECT\n10.,20.\n0.,0.,1.\n"
        "*BEAM GENERAL SECTION, ELSET=BP, SECTION=PIPE, MATERIAL=M2\n12.,2.\n"
        "*STEP, NLGEOM\n*STATIC\n0.1,2.,1e-4,0.5\n*BOUNDARY\nNFIX,1,3\n8,2,2,0.5\n*CLOAD\nNFIX,3,-5.\n8,3,-7.\n*DSLOAD\nSURF1,P,3.\n*DLOAD\nPL,P2,1.\n*END STEP\n"
        "*STEP\n*STATIC\n*END STEP\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    CHECK(d.nmats == 2);
    if (d.nmats == 2) {
        CHECK((d.mprop[0].has & CV_MP_RHO) && d.mprop[0].rho > 7.7e-9f && d.mprop[0].rho < 7.9e-9f && !(d.mprop[0].has & CV_MP_RHO_T));
        CHECK((d.mprop[0].has & CV_MP_COND) && d.mprop[0].cond == 45.f);
        CHECK((d.mprop[0].has & CV_MP_EXP) && (d.mprop[0].has & CV_MP_ZERO) && d.mprop[0].alpha0 == 20.f);
        CHECK((d.mprop[0].has & CV_MP_CP) && d.mprop[0].cp > 4.5e8f);
        CHECK((d.mprop[1].has & CV_MP_RHO_T) && d.mprop[1].rho > 2.6e-9f && d.mprop[1].rho < 2.8e-9f);
        CHECK(d.mprop[1].el == CV_EL_ISO && d.mprop[1].c[0] == 70000.f);
    }
    CHECK(d.nsecinfo == 4);
    if (d.nsecinfo == 4) {
        CHECK(!strcmp(d.secinfo[0].kind, "shell") && d.secinfo[0].t == 2.5f && d.secinfo[0].has_off && d.secinfo[0].off == 0.5f);
        CHECK(d.secinfo[1].composite && d.secinfo[1].nlay == 2 && d.secinfo[1].ttot == 1.25f);
        CHECK(!strcmp(d.secinfo[2].kind, "beam") && !strcmp(d.secinfo[2].shape, "RECT") && d.secinfo[2].ndim == 2 &&
              d.secinfo[2].dim[1] == 20.f && d.secinfo[2].has_dir && d.secinfo[2].dir[2] == 1.f);
        CHECK(!strcmp(d.secinfo[3].kind, "beamgeneral") && !strcmp(d.secinfo[3].shape, "PIPE") && d.secinfo[3].ndim == 2 && !d.secinfo[3].has_dir);
    }
    CHECK(d.nsteps == 2 && d.stepinfo && d.stepinfo[0].nlgeom && !d.stepinfo[1].nlgeom);
    if (d.nsteps == 2) CHECK(d.stepinfo[0].inc == 0.1f && d.stepinfo[0].period == 2.f && d.stepinfo[0].tmin == 1e-4f && d.stepinfo[0].tmax == 0.5f);
    CHECK(d.nbcs == 3 && d.ncloads == 3);          /* the expanded data is as before */
    bool bcn = false, cln = false, dln = false, nums = true;
    for (uint32_t i = 0; i < d.nlsrc; i++) {
        const cv_loadsrc* s = &d.lsrc[i];
        bcn |= s->kind == CV_LS_BC && !strcmp(s->name, "NFIX") && s->n == 2;
        cln |= s->kind == CV_LS_CLOAD && !strcmp(s->name, "NFIX") && s->n == 2;
        dln |= s->kind == CV_LS_DLOAD && !strcmp(s->name, "SURF1");
        nums &= s->name[0] < '0' || s->name[0] > '9';
    }
    CHECK(bcn && cln && dln && nums);
    char* t = rep_text(NULL, &d, NULL);
    CHECK(t != NULL);
    if (t) {
        CHECK(strstr(t, "| M1 | 7.8e-09 | 45 | 1.2e-05 | 20 | 4.6e+08 |"));
        CHECK(strstr(t, "(table)"));
        CHECK(strstr(t, "| shell | PL | M1 | - | 2.5 | 0.5 | - | - |"));
        CHECK(strstr(t, "composite: 2 layers, total 1.25"));
        CHECK(strstr(t, "| beam | BR | M1 | RECT | - | - | 10, 20 | 0, 0, 1 |"));
        CHECK(strstr(t, "| beam general | BP | M2 | PIPE | - | - | 12, 2 | - |"));
        CHECK(strstr(t, "| 1 | Static | 2 | yes | 0.1 | 0.0001 | 0.5 |"));
        CHECK(strstr(t, "| 2 | Static | 1 | no |"));
        CHECK(strstr(t, "| 2 | 0.5 | -"));              /* DOF 2 of node 8, by id */
        CHECK(strstr(t, "set NFIX"));
        CHECK(strstr(t, "surface SURF1"));
        CHECK(strstr(t, "| 2 | 0.5 | - | 1 | 8 |"));              /* the node given by id keeps its id */
        CHECK(strstr(t, "set PL"));
        free(t);
    }
    cv_inp_free(&d);
}

/* the PDF: a table longer than a page, cells too wide, text the standard fonts lack */
static void test_report_pdf(void) {
    size_t cap = 64 * 1024, n = 0;
    char* md = malloc(cap);
    if (!md) return;
    n += snprintf(md + n, cap - n, "# Model summary\n\nIntro line \xe2\x88\x87 \xc3\xa9 \xe2\x82\xac.\n\n| | |\n|---|---|\n| Result file | a \\| b.frd |\n\n## 1. Sets\n\nNode sets\n\n| Set | Nodes |\n|---|---|\n");
    for (int i = 0; i < 160 && n + 200 < cap; i++)
        n += snprintf(md + n, cap - n, "| SET_%d_%s | %d |\n", i, i == 7 ? "AVERYLONGNAMEWITHOUTANYSPACESTHATCANNOTFITINONECOLUMNATALLNOMATTERWHAT_AVERYLONGNAMEWITHOUTANYSPACES" : "x", i);
    n += snprintf(md + n, cap - n, "\n... and 3 more\n\n### Step 1\n\nA paragraph long enough to wrap over more than one line of the page, so the wrapping is used, and once more for good measure, and again.\n");
    const char* path = "build/report_test.pdf";
    CHECK(cv_report_pdf(md, path, "t\xc3\xa9st.frd", NULL));
    FILE* fp = fopen(path, "rb");
    CHECK(fp != NULL);
    if (fp) {
        static char b[1 << 20];
        size_t got = fread(b, 1, sizeof b - 1, fp);
        fclose(fp);
        b[got] = 0;
        CHECK(got > 1000 && !memcmp(b, "%PDF-", 5));
        CHECK(strstr(b, "%%EOF") != NULL);
        const char* c = strstr(b, "/Count ");
        CHECK(c && atoi(c + 7) >= 3);                   /* 160 rows: pages */
        CHECK(!strstr(b, "Length zu") && !strstr(b, "%zu"));
        int heads = 0;
        for (const char* p = b; (p = strstr(p, "(Nodes)")); p++) heads++;
        CHECK(heads >= 3);                              /* the header row on each page the table runs onto */
        CHECK(strstr(b, "a | b.frd") != NULL);          /* \| unescaped */
        CHECK(strstr(b, "SET_159") != NULL);
    }
    CHECK(!cv_report_pdf(md, "build/no/such/dir/x.pdf", NULL, NULL));
    CHECK(!cv_report_pdf(NULL, path, NULL, NULL));
    free(md);
    remove(path);
}

/* pictures: "![caption](file)" lines placed from dir, two to a row; one that cannot be read named in grey */
static void test_report_pdf_pics(void) {
    static const unsigned char png[] = {   /* 4 x 2 RGB */
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,4,0,0,0,2,8,2,0,0,0,240,202,234,52,0,0,0,16,73,68,65,84,120,
        156,99,56,97,100,4,71,12,200,28,0,126,170,9,97,204,65,188,32,0,0,0,0,73,69,78,68,174,66,96,130 };
    FILE* w = fopen("build/report_pic_a.png", "wb");
    CHECK(w != NULL);
    if (!w) return;
    fwrite(png, 1, sizeof png, w);
    fclose(w);
    const char* md = "# Model summary\n\n## 1. Pictures\n\n| | |\n|---|---|\n| Step | 1 |\n\n"
                     "![Isometric](report_pic_a.png)\n\n![Front](report_pic_a.png)\n\n![Top](report_pic_a.png)\n\n![Gone](no_such.png)\n\n"
                     "## 2. Mesh\n\nAfter the pictures.\n";
    const char* path = "build/report_pics.pdf";
    CHECK(cv_report_pdf(md, path, "pics", "build"));
    FILE* fp = fopen(path, "rb");
    CHECK(fp != NULL);
    if (fp) {
        static char b[1 << 18];
        size_t got = fread(b, 1, sizeof b - 1, fp);
        fclose(fp);
        int imgs = 0;
        for (size_t i = 0; i + 15 <= got; i++) imgs += !memcmp(b + i, "/Subtype /Image", 15);
        CHECK(imgs == 3);
        bool gone = false, after = false, cap = false;
        for (size_t i = 0; i + 20 <= got; i++) {
            gone |= !memcmp(b + i, "(picture not availab", 20);
            after |= !memcmp(b + i, "(After the pictures.", 20);
            cap |= !memcmp(b + i, "(Isometric)", 11);
        }
        CHECK(gone && after && cap);
    }
    remove(path);
    remove("build/report_pic_a.png");
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
    /* the builder: parts left out, the rest numbered without gaps; pictures after General */
    cv_report_pic pics[2] = { { "m_iso.png", "Isometric" }, { "m_top.png", "To]p" } };
    cv_report_opts o2 = { NULL, "dir/model.inp", 0, CV_RP_MESH | CV_RP_SETS | CV_RP_QUALITY | CV_RP_MATS, pics, 2, "| | |\n|---|---|\n| Step | 1 |" };
    t = rep_text(NULL, &d, &o2);
    CHECK(t != NULL);
    if (t) {
        CHECK(strstr(t, "## 1. General"));
        CHECK(strstr(t, "## 2. Pictures\n\n| | |\n|---|---|\n| Step | 1 |\n\n![Isometric](m_iso.png)\n\n![Top](m_top.png)\n"));
        CHECK(strstr(t, "## 3. Sections and orientations"));
        CHECK(strstr(t, "## 4. Boundary conditions and loads"));
        CHECK(!strstr(t, "Mesh") && !strstr(t, "Materials") && !strstr(t, "| FIXED | 4 |") && !strstr(t, "E = 210000"));
        CHECK(strstr(t, "Analysis steps"));
        free(t);
    }
    cv_report_opts o3 = { NULL, NULL, 0, CV_RP_GENERAL, NULL, 0, NULL };   /* no General: the steps their own section */
    t = rep_text(NULL, &d, &o3);
    CHECK(t && strstr(t, "## 1. Analysis steps\n\n| Step |") && !strstr(t, "General"));
    free(t);
    CHECK(!strcmp(cv_report_part_name(0), "General") && !cv_report_part_name(CV_RP_N)[0]);
    cv_inp_free(&d);
    /* the samples, when they are there: the showcase result with its deck, a result with its deck, a deck alone */
    static const char* S[][2] = { { "samples/showcase/showcase.frd", "samples/showcase/showcase.inp" },
                                  { "samples/element_based_stress/SANDWICH_MASTER.frd", "samples/element_based_stress/SANDWICH_MESH.inp" },
                                  { NULL, "samples/rbe_couplings/rbe_couplings.inp" },
                                  { NULL, "samples/report_demo/report_demo.inp" } };
    for (int k = 0; k < 4; k++) {
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
    test_report_props();
    test_report_pdf();
    test_report_pdf_pics();
}
