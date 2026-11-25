/* t_elnod.h -- element-nodal (IRTYPE 3) results: decode, then grouping, sides, averaging.
   Included by test_main.c. */
#ifndef CV_T_ELNOD_H
#define CV_T_ELNOD_H

/* Two hexes stacked on z (elements 10 and 11, nodes 1..12) with a quad shell (element 12) on the
   top face, its normal +z. STRESS holds every slot of the hexes, value sxx = 100 e + position;
   STRPOS 300 + position and STRNEG 50 + position on the shell only. Binary files carry one field
   with a record for every slot (the step's record count is one number). */
static const uint32_t kEnElem[3] = { 10, 11, 12 };

static void en_nodes_elems(FILE* o, int binary) {
    uint32_t id[12];
    double xyz[36];
    for (int i = 0; i < 12; i++) {
        id[i] = 1 + i;
        xyz[3 * i] = i % 2; xyz[3 * i + 1] = (i / 2) % 2; xyz[3 * i + 2] = i / 4;
    }
    /* node ring of a layer, counter-clockwise from above: (0,0) (1,0) (1,1) (0,1) = 4k+1, 4k+2, 4k+4, 4k+3 */
    uint32_t h1[8] = { 1, 2, 4, 3, 5, 6, 8, 7 }, h2[8] = { 5, 6, 8, 7, 9, 10, 12, 11 }, q[4] = { 9, 10, 12, 11 };
    fw_nodes(o, 12, id, xyz, binary);
    uint32_t e1 = 10, e2 = 11, e3 = 12;
    fw_elems(o, 1, &e1, 1, 8, h1, NULL, binary);
    fw_elems(o, 1, &e2, 1, 8, h2, NULL, binary);
    fw_elems(o, 1, &e3, 9, 4, q, NULL, binary);
}

static buf_t en_frd(int binary) {
    FILE* o = tmp_open();
    fprintf(o, "    1CFEMAST\n");
    if (binary) {                                 /* one element block per call is not allowed: write the three as one */
        uint32_t id[12];
        double xyz[36];
        for (int i = 0; i < 12; i++) { id[i] = 1 + i; xyz[3 * i] = i % 2; xyz[3 * i + 1] = (i / 2) % 2; xyz[3 * i + 2] = i / 4; }
        fw_nodes(o, 12, id, xyz, 1);
        fw_header(o, "    3C                 3", 73, '2');
        int32_t h[4] = { 10, 1, 0, 1 }, c[8] = { 1, 2, 4, 3, 5, 6, 8, 7 };
        fwrite(h, 4, 4, o); fwrite(c, 4, 8, o);
        int32_t h2[4] = { 11, 1, 0, 1 }, c2[8] = { 5, 6, 8, 7, 9, 10, 12, 11 };
        fwrite(h2, 4, 4, o); fwrite(c2, 4, 8, o);
        int32_t h3[4] = { 12, 9, 0, 1 }, c3[4] = { 9, 10, 12, 11 };
        fwrite(h3, 4, 4, o); fwrite(c3, 4, 4, o);
        fprintf(o, " -3\n");
        fw_step(o, 1, 1.0, 20, 1);
        static const uint32_t n1[8] = { 1, 2, 4, 3, 5, 6, 8, 7 }, n2[8] = { 5, 6, 8, 7, 9, 10, 12, 11 }, n3[4] = { 9, 10, 12, 11 };
        uint32_t el[20], ps[20], nd[20];
        float v[120];
        for (int k = 0; k < 20; k++) {
            int e = k < 8 ? 0 : k < 16 ? 1 : 2, p = k < 8 ? k : k < 16 ? k - 8 : k - 16;
            el[k] = kEnElem[e]; ps[k] = (uint32_t)p + 1;
            nd[k] = e == 0 ? n1[p] : e == 1 ? n2[p] : n3[p];
            for (int c6 = 0; c6 < 6; c6++) v[6 * k + c6] = 100.f * (float)e + (float)ps[k] + 0.25f * (float)c6;
        }
        fw_elnod(o, "STRESS", 6, kStress, 20, el, nd, v, 1);
        fprintf(o, "9999\n");
        return slurp(o);
    }
    en_nodes_elems(o, 0);
    fw_step(o, 1, 1.0, 20, 0);
    static const uint32_t hn[2][8] = { { 1, 2, 4, 3, 5, 6, 8, 7 }, { 5, 6, 8, 7, 9, 10, 12, 11 } };
    uint32_t el[16], ps[16], nd[16];
    float v[96];
    for (int k = 0; k < 16; k++) {
        int e = k / 8, p = k % 8;
        el[k] = kEnElem[e]; ps[k] = (uint32_t)p + 1; nd[k] = hn[e][p];
        for (int c = 0; c < 6; c++) v[6 * k + c] = 100.f * (e + 1) + (float)ps[k] + 0.25f * (float)c;
    }
    fw_elnod(o, "STRESS", 6, kStress, 16, el, nd, v, 0);
    uint32_t se[4] = { 12, 12, 12, 12 }, sp[4] = { 1, 2, 3, 4 }, sn[4] = { 9, 10, 12, 11 };
    float pos[24], neg[24];
    for (int k = 0; k < 4; k++) for (int c = 0; c < 6; c++) { pos[6 * k + c] = 300.f + (float)sp[k] + 0.25f * (float)c; neg[6 * k + c] = 50.f + (float)sp[k] + 0.25f * (float)c; }
    fw_elnod(o, "STRPOS", 6, kStress, 4, se, sn, pos, 0);
    fw_elnod(o, "STRNEG", 6, kStress, 4, se, sn, neg, 0);
    fprintf(o, "9999\n");
    return slurp(o);
}

static void test_elnod_decode(void) {
    for (int bin = 0; bin < 2; bin++) {
        buf_t b = en_frd(bin);
        cv_frd f;
        CHECK(cv_frd_parse(&f, b.p, b.n));
        CHECK_EQ(f.n_elems, 3);
        CHECK_EQ(f.n_steps, 1);
        if (f.n_steps != 1 || f.steps[0].nfields < 1) { CHECK(!"fields"); cv_frd_free(&f); free(f.msgs.a); free(b.p); continue; }
        CHECK_EQ(f.steps[0].nfields, bin ? 1 : 3);
        const cv_field_desc* d = &f.steps[0].fields[0];
        CHECK(d->elnod && !strcmp(d->name, "STRESS") && d->ncomp == 6);
        CHECK_EQ(cv_frd_slots(&f), 20);
        float* v = malloc(20 * 6 * sizeof *v);
        float* nod = malloc(12 * 6 * sizeof *nod);
        cv_msgs ms = {0};
        cv_frd_read_elnod(&f, d, v, &ms);
        /* element 10 (index 0) slot 3 (the element's fourth node): 100 + 4; element 11 slot 0: 200 + 1 */
        if (bin) {
            CHECK_NEAR(v[6 * 3], 4, 1e-4);
            CHECK_NEAR(v[6 * 8 + 1], 101.25, 1e-4);
            CHECK_NEAR(v[6 * 19 + 5], 204 + 1.25, 1e-3);
        } else {
            CHECK_NEAR(v[6 * 3], 104, 1e-3);                     /* the same node (6) in the next element has its own value */
            CHECK_NEAR(v[6 * 8], 201, 1e-3);
            CHECK_NEAR(v[6 * 8 + 5], 202.25, 1e-3);
            CHECK(v[6 * 16] != v[6 * 16]);                       /* the shell: no STRESS there */
            CHECK_EQ(f.n_nodes, 12);
            CHECK(f.conn[3] == f.conn[8 + 0] || f.conn[4] == f.conn[8]);   /* node 5 is shared: slot 4 and slot 8 */
            CHECK(v[6 * 4] != v[6 * 8]);                         /* a jump at the shared node, kept */
            const cv_field_desc* sp = &f.steps[0].fields[1];
            CHECK(sp->elnod && !strcmp(sp->name, "STRPOS"));
            cv_frd_read_elnod(&f, sp, v, &ms);
            CHECK_NEAR(v[6 * 16], 301, 1e-3);
            CHECK_NEAR(v[6 * 19 + 2], 304.5, 1e-3);
            CHECK(v[0] != v[0]);                                 /* the hexes: no STRPOS */
        }
        /* no value per node for such a field: NaN, nothing read past the buffer */
        cv_frd_read_field(&f, d, nod, &ms);
        CHECK(nod[0] != nod[0] && nod[12 * 6 - 1] != nod[12 * 6 - 1]);
        free(v); free(nod); free(ms.a);
        cv_frd_free(&f); free(f.msgs.a); free(b.p);
    }
}

/* nodes outside the element, unknown elements, truncation and damage never read or write outside */
static void test_elnod_safe(void) {
    const char* bad =
        "    1C\n"
        "    2C                 4                                     1\n"
        " -1         1 0.00000E+00 0.00000E+00 0.00000E+00\n"
        " -1         2 1.00000E+00 0.00000E+00 0.00000E+00\n"
        " -1         3 0.00000E+00 1.00000E+00 0.00000E+00\n"
        " -1         4 1.00000E+00 1.00000E+00 0.00000E+00\n"
        " -3\n"
        "    3C                 1                                     1\n"
        " -1         5    9    0    1\n"
        " -2         1         2         3         4\n"
        " -3\n"
        "    1PSTEP                   1           1           1\n"
        "  100CL  101 1.00000E+00           8                     0    1\n"
        " -4  STRESS      1    3\n"
        " -5  SXX         1    2    1    0\n"
        " -1         5         1 1.00000E+00\n"
        " -1         5         4 4.00000E+00\n"
        " -1         5         9 5.00000E+00\n"      /* node 9 is not in element 5 */
        " -1         5         0 9.00000E+00\n"
        " -1         6         1 7.00000E+00\n"      /* no such element */
        " -1        99999999999         1 7.00000E+00\n"
        " -1\n"
        " -1         5         2\n"                   /* no value */
        " -3\n"
        "  9999\n";
    cv_frd f;
    CHECK(cv_frd_parse(&f, bad, strlen(bad)));
    CHECK_EQ(f.n_elems, 1);
    if (f.n_steps == 1 && f.steps[0].nfields == 1) {
        float v[4];
        cv_msgs ms = {0};
        cv_frd_read_elnod(&f, &f.steps[0].fields[0], v, &ms);
        CHECK_NEAR(v[0], 1, 1e-6);
        CHECK(v[1] != v[1]);
        CHECK(v[2] != v[2]);
        CHECK_NEAR(v[3], 4, 1e-6);
        CHECK(ms.n > 0);
        free(ms.a);
    } else CHECK(!"field");
    cv_frd_free(&f); free(f.msgs.a);
    /* truncated at every length, ASCII and binary */
    for (int bin = 0; bin < 2; bin++) {
        buf_t b = en_frd(bin);
        for (size_t cut = 0; cut < b.n; cut += 29) {
            cv_frd g;
            CHECK(cv_frd_parse(&g, b.p, cut));
            size_t ns = cv_frd_slots(&g);
            for (int s = 0; s < g.n_steps; s++)
                for (int k = 0; k < g.steps[s].nfields; k++) {
                    float* v = malloc(sizeof(float) * CV_MAX(1, ns) * g.steps[s].fields[k].ncomp);
                    cv_frd_read_elnod(&g, &g.steps[s].fields[k], v, &g.msgs);
                    free(v);
                }
            cv_frd_free(&g); free(g.msgs.a);
        }
        free(b.p);
    }
}

/* ---- grouping, sides, averaging ---------------------------------------------------- */

static void test_elnod_groups(void) {
    cv_field_desc fd[3];
    memset(fd, 0, sizeof fd);
    cv_step st = { .nfields = 3, .fields = fd };
    bool folded;
    cv_en_group g;
    snprintf(fd[0].name, sizeof fd[0].name, "STRESS");
    snprintf(fd[1].name, sizeof fd[1].name, "STRPOS");
    snprintf(fd[2].name, sizeof fd[2].name, "STRNEG");
    for (int i = 0; i < 3; i++) { fd[i].elnod = true; fd[i].ncomp = 6; }
    /* STRESS + STRPOS + STRNEG: one "Stress", the other two folded into it */
    CHECK_EQ(cv_en_entry(&st, 0, &g, &folded), CV_EN_STRESS);
    CHECK(!folded && g.stress == 0 && g.pos == 1 && g.neg == 2);
    CHECK(!strcmp(cv_en_title(&st, 0), "Stress"));
    CHECK_EQ(cv_en_entry(&st, 1, &g, &folded), CV_EN_ONE); CHECK(folded && g.kind == CV_EN_STRESS && g.stress == 0);
    CHECK_EQ(cv_en_entry(&st, 2, NULL, &folded), CV_EN_ONE); CHECK(folded);
    /* STRESS with STRPOS only: still one Stress */
    st.nfields = 2;
    CHECK_EQ(cv_en_entry(&st, 0, &g, &folded), CV_EN_STRESS);
    CHECK(g.pos == 1 && g.neg == -1);
    /* STRESS alone is just STRESS */
    st.nfields = 1;
    CHECK_EQ(cv_en_entry(&st, 0, &g, &folded), CV_EN_ONE);
    CHECK(!folded && !strcmp(cv_en_title(&st, 0), "STRESS"));
    /* STRPOS + STRNEG without STRESS: "Shell stress" */
    fd[0] = fd[1]; fd[1] = fd[2];
    st.nfields = 2;
    CHECK_EQ(cv_en_entry(&st, 0, &g, &folded), CV_EN_SHELL);
    CHECK(g.stress == -1 && g.pos == 0 && g.neg == 1);
    CHECK(!strcmp(cv_en_title(&st, 0), "Shell stress"));
    CHECK_EQ(cv_en_entry(&st, 1, NULL, &folded), CV_EN_ONE); CHECK(folded);
    /* STRPOS alone, or fields that are not element-nodal, are not grouped */
    st.nfields = 1;
    CHECK_EQ(cv_en_entry(&st, 0, NULL, &folded), CV_EN_ONE); CHECK(!folded);
    st.nfields = 2; fd[0].elnod = fd[1].elnod = false;
    CHECK_EQ(cv_en_entry(&st, 0, NULL, &folded), CV_EN_ONE); CHECK(!folded);
}

typedef struct { cv_frd f; buf_t b; cv_nr_topo t; float *s, *p, *n; } en_fix;

static bool en_load(en_fix* x) {
    memset(x, 0, sizeof *x);
    x->b = en_frd(0);
    if (!cv_frd_parse(&x->f, x->b.p, x->b.n) || x->f.n_steps != 1 || x->f.steps[0].nfields != 3) return false;
    size_t ns = cv_frd_slots(&x->f);
    x->s = malloc(ns * 6 * sizeof(float)); x->p = malloc(ns * 6 * sizeof(float)); x->n = malloc(ns * 6 * sizeof(float));
    cv_frd_read_elnod(&x->f, &x->f.steps[0].fields[0], x->s, NULL);
    cv_frd_read_elnod(&x->f, &x->f.steps[0].fields[1], x->p, NULL);
    cv_frd_read_elnod(&x->f, &x->f.steps[0].fields[2], x->n, NULL);
    return cv_nr_topo_build(&x->t, x->f.n_nodes, x->f.n_elems, x->f.eoff, x->f.conn);
}
static void en_free(en_fix* x) {
    cv_nr_topo_free(&x->t); free(x->s); free(x->p); free(x->n);
    cv_frd_free(&x->f); free(x->f.msgs.a); free(x->b.p);
}

static void test_elnod_sides(void) {
    en_fix x;
    if (!en_load(&x)) { CHECK(!"fixture"); return; }
    float fr[20], bk[20];
    cv_en_src src = { CV_EN_STRESS, x.s, x.p, x.n };
    float* nrm = cv_en_normals(&x.f);
    CHECK(nrm && nrm[3 * 2 + 2] > 0.99f);                       /* the quad (nodes 9 10 12 11) looks up */
    CHECK(cv_en_scalars(&x.f, &x.t, &src, 6, 0, CV_EN_BOTH, CV_EN_AVG_NONE, NULL, NULL, nrm, fr, bk));
    CHECK_NEAR(fr[3], 104, 1e-3);  CHECK_NEAR(bk[3], 104, 1e-3);           /* a solid: the same on both faces */
    CHECK_NEAR(fr[16], 301, 1e-3); CHECK_NEAR(bk[16], 51, 1e-3);           /* the shell: STRPOS front, STRNEG back */
    CHECK_NEAR(fr[19], 304, 1e-3); CHECK_NEAR(bk[19], 54, 1e-3);
    CHECK(cv_en_scalars(&x.f, &x.t, &src, 6, 0, CV_EN_TOP, CV_EN_AVG_NONE, NULL, NULL, nrm, fr, bk));
    CHECK_NEAR(fr[17], 302, 1e-3); CHECK_NEAR(bk[17], 302, 1e-3);
    CHECK(cv_en_scalars(&x.f, &x.t, &src, 6, 0, CV_EN_BOTTOM, CV_EN_AVG_NONE, NULL, NULL, nrm, fr, bk));
    CHECK_NEAR(fr[17], 52, 1e-3); CHECK_NEAR(bk[17], 52, 1e-3);
    CHECK_NEAR(fr[3], 104, 1e-3);
    /* a surface the file does not have stays missing: no STRNEG, the back of the shell has nothing */
    src.neg = NULL;
    CHECK(cv_en_scalars(&x.f, &x.t, &src, 6, 0, CV_EN_BOTH, CV_EN_AVG_NONE, NULL, NULL, nrm, fr, bk));
    CHECK_NEAR(fr[18], 303, 1e-3);
    CHECK(bk[18] != bk[18]);
    src.neg = x.n; src.solid = NULL;                            /* "Shell stress": the solids have nothing */
    src.kind = CV_EN_SHELL;
    CHECK(cv_en_scalars(&x.f, &x.t, &src, 6, 0, CV_EN_BOTH, CV_EN_AVG_NONE, NULL, NULL, nrm, fr, bk));
    CHECK(fr[3] != fr[3] && !(fr[16] != fr[16]));
    /* hidden elements have no value */
    uint8_t vis[3] = { 1, 0, 1 };
    src.kind = CV_EN_STRESS; src.solid = x.s;
    CHECK(cv_en_scalars(&x.f, &x.t, &src, 6, 0, CV_EN_BOTH, CV_EN_AVG_NONE, vis, NULL, nrm, fr, bk));
    CHECK(fr[8] != fr[8] && !(fr[3] != fr[3]));
    free(nrm);
    en_free(&x);
}

static void test_elnod_average(void) {
    en_fix x;
    if (!en_load(&x)) { CHECK(!"fixture"); return; }
    float fr[20], bk[20];
    cv_en_src src = { CV_EN_STRESS, x.s, x.p, x.n };
    float* nrm = cv_en_normals(&x.f);
    /* node 5 is slot 4 of element 10 (105) and slot 0 of element 11 (201): two materials, apart */
    uint32_t grp[3] = { 1, 2, 3 };
    CHECK(cv_en_scalars(&x.f, &x.t, &src, 6, 0, CV_EN_BOTH, CV_EN_AVG_SCALAR, NULL, grp, nrm, fr, bk));
    CHECK_NEAR(fr[4], 105, 1e-3); CHECK_NEAR(fr[8], 201, 1e-3);
    /* one section and material: they meet in the mean */
    uint32_t same[3] = { 1, 1, 1 };
    CHECK(cv_en_scalars(&x.f, &x.t, &src, 6, 0, CV_EN_BOTH, CV_EN_AVG_SCALAR, NULL, same, nrm, fr, bk));
    CHECK_NEAR(fr[4], 153, 1e-3); CHECK_NEAR(fr[8], 153, 1e-3);
    CHECK_NEAR(fr[3], 104, 1e-3);                                /* a node of one element only */
    /* the shell shares nodes 9..12 with element 11 but is not averaged with a solid, even in one group */
    CHECK_NEAR(fr[12], 205, 1e-3);                               /* element 11, position 5 = node 9 */
    CHECK_NEAR(fr[16], 301, 1e-3); CHECK_NEAR(bk[16], 51, 1e-3);
    /* a hidden element does not take part: node 5 keeps element 10's own value */
    uint8_t vis[3] = { 1, 0, 1 };
    CHECK(cv_en_scalars(&x.f, &x.t, &src, 6, 0, CV_EN_BOTH, CV_EN_AVG_SCALAR, vis, same, nrm, fr, bk));
    CHECK_NEAR(fr[4], 105, 1e-3);
    CHECK(fr[8] != fr[8]);
    /* the components first, then von Mises, is not the mean of the von Mises values */
    float mt[2], ms[2];
    CHECK(cv_en_scalars(&x.f, &x.t, &src, 6, CV_COMP_MISES, CV_EN_BOTH, CV_EN_AVG_TENSOR, NULL, same, nrm, fr, bk));
    mt[0] = fr[4]; mt[1] = fr[8];
    CHECK(cv_en_scalars(&x.f, &x.t, &src, 6, CV_COMP_MISES, CV_EN_BOTH, CV_EN_AVG_SCALAR, NULL, same, nrm, fr, bk));
    ms[0] = fr[4]; ms[1] = fr[8];
    CHECK_NEAR(mt[0], mt[1], 1e-3); CHECK_NEAR(ms[0], ms[1], 1e-3);
    float t6[6];
    for (int c = 0; c < 6; c++) { t6[c] = (x.s[6 * 4 + c] + x.s[6 * 8 + c]) / 2; }
    CHECK_NEAR(mt[0], cv_von_mises(t6), 1e-2);
    CHECK_NEAR(ms[0], (cv_von_mises(x.s + 6 * 4) + cv_von_mises(x.s + 6 * 8)) / 2, 1e-2);
    free(nrm);
    en_free(&x);

    /* shells meeting at a node: the same way round average, opposite normals do not */
    float xyz[18] = { 0,0,0, 1,0,0, 1,1,0, 0,1,0, 2,0,0, 2,1,0 };
    uint32_t eoff[3] = { 0, 4, 8 };
    uint8_t ty[2] = { 9, 9 };
    for (int flip = 0; flip < 2; flip++) {
        uint32_t conn[8] = { 0, 1, 2, 3, 1, 4, 5, 2 };
        if (flip) { conn[4] = 2; conn[5] = 5; conn[6] = 4; conn[7] = 1; }
        cv_frd f;
        memset(&f, 0, sizeof f);
        f.n_nodes = 6; f.xyz = xyz; f.n_elems = 2; f.etype = ty; f.eoff = eoff; f.conn = conn;
        cv_nr_topo t;
        CHECK(cv_nr_topo_build(&t, 6, 2, eoff, conn));
        float v[8] = { 10, 10, 10, 10, 30, 30, 30, 30 }, o[8];
        float* n = cv_en_normals(&f);
        CHECK(n && n[2] > 0.99f && (flip ? n[5] < -0.99f : n[5] > 0.99f));
        cv_en_average(&f, &t, NULL, NULL, n, v, 1, o);
        if (flip) { CHECK_NEAR(o[1], 10, 1e-6); CHECK_NEAR(o[7], 30, 1e-6); }     /* node 1: opposite normals, each its own */
        else      { CHECK_NEAR(o[1], 20, 1e-6); CHECK_NEAR(o[4], 20, 1e-6); CHECK_NEAR(o[0], 10, 1e-6); }
        free(n);
        cv_nr_topo_free(&t);
    }
}

/* a shell lying on a solid's face is picked before the solid, from either side of it */
static void test_elnod_pick(void) {
    en_fix x;
    if (!en_load(&x)) { CHECK(!"fixture"); return; }
    cv_skin sk;
    CHECK(cv_skin_build(&sk, &x.f, NULL));
    const float o[3] = { 0.5f, 0.5f, 5.f }, dn[3] = { 0.f, 0.f, -1.f };
    cv_pick p = cv_pick_ray(&x.f, &sk, NULL, 0.f, o, dn);
    CHECK(p.hit && p.elem == 2);
    const float o2[3] = { 0.5f, 0.5f, -3.f }, up[3] = { 0.f, 0.f, 1.f };
    p = cv_pick_ray(&x.f, &sk, NULL, 0.f, o2, up);
    CHECK(p.hit && p.elem == 0);                                  /* from below: the solid's bottom face */
    cv_skin_free(&sk);
    en_free(&x);
}

#endif
