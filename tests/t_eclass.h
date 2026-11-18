#include "../src/eclass.h"
/* t_eclass.h -- unit tests for eclass.c (elements by set, by section; the palette). Included by test_main.c. */

static void test_s3r(void) {
    const char* deck =
        "*NODE\n1,0,0,0\n2,1,0,0\n3,1,0,1\n4,0,0,1\n"
        "*ELEMENT, TYPE=S3, ELSET=E\n10,1,2,3\n"
        "*ELEMENT, TYPE=s3r, ELSET=E\n20,1,3,4\n"
        "*MATERIAL, NAME=M\n*ELASTIC\n1.,0.\n"
        "*SHELL SECTION, ELSET=E, MATERIAL=M\n0.1\n"
        "*STEP\n*STATIC\n*EL FILE, GLOBAL=NO\nS\n*END STEP\n";
    cv_inp d;
    bool ok = cv_inp_parse(&d, deck, strlen(deck), NULL, NULL);
    CHECK(ok);
    if (!ok) return;
    const cv_frd* f = &d.mesh;
    const cv_set* set = cv_inp_set(&d, "E", true);
    CHECK_EQ(f->n_elems, 2);
    CHECK_EQ(d.nshells, 2);
    CHECK(set && set->n == 2 && set->ids[0] == 10 && set->ids[1] == 20);
    if (f->n_elems == 2 && d.nshells == 2) {
        CHECK_EQ(d.shells[0], 10);
        CHECK_EQ(d.shells[1], 20);
        CHECK_EQ(f->etype[0], 7);
        CHECK_EQ(f->etype[1], 7);
        CHECK_EQ(f->eoff[1] - f->eoff[0], 3);
        CHECK_EQ(f->eoff[2] - f->eoff[1], 3);
        CHECK_EQ(f->emat[0], f->emat[1]);
        cv_eclass c;
        CHECK(cv_eclass_sections(&c, f, &d));
        CHECK_EQ(c.n, 1);
        if (c.n == 1) {
            CHECK_EQ(c.of_elem[0], c.of_elem[1]);
            CHECK(!strcmp(c.name[0], "shell: E (M)"));
        }
        cv_eclass_free(&c);
        cv_localsys L;
        ok = cv_localsys_init(&L, &d, f);
        CHECK(ok);
        if (ok && f->n_nodes == 4) {
            /* node 4 belongs to the S3R only: it gets the same shell system as the S3 */
            cv_field_desc st = { .name = "STRESS", .ncomp = 6, .comp = { "SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX" } };
            float stress[24] = {0};
            for (int i = 0; i < 4; i++) { stress[6 * i + 1] = 1; stress[6 * i + 2] = 3; }
            CHECK_EQ(cv_localsys_apply(&L, &d, f, 1, &st, stress), CV_LOC_TURNED);
            for (int i = 0; i < 4; i++) {
                CHECK_NEAR(stress[6 * i + 1], 3, 1e-6);
                CHECK_NEAR(stress[6 * i + 2], 1, 1e-6);
            }
            float slots[36] = {0};                     /* element-nodal: per connectivity slot */
            for (int i = 0; i < 6; i++) { slots[6 * i + 1] = 1; slots[6 * i + 2] = 3; }
            CHECK_EQ(cv_localsys_apply_elem(&L, &d, f, 1, &st, slots), CV_LOC_TURNED);
            for (int i = 0; i < 6; i++) {
                CHECK_NEAR(slots[6 * i + 1], 3, 1e-6);
                CHECK_NEAR(slots[6 * i + 2], 1, 1e-6);
            }
        }
        if (ok) cv_localsys_free(&L);
        int32_t sections[2];                           /* S3 and S3R share the section: averaged */
        cv_elem_sections(f, &d, sections);
        CHECK_EQ(sections[0], 0);
        CHECK_EQ(sections[1], 0);
        float values[] = {10, 10, 10, 20, 20, 20};
        CHECK(cv_elem_nodal_average(f, NULL, sections, values, 1));
        CHECK_NEAR(values[0], 15, 1e-6);
        CHECK_NEAR(values[1], 10, 1e-6);
        CHECK_NEAR(values[2], 15, 1e-6);
        CHECK_NEAR(values[3], 15, 1e-6);
        CHECK_NEAR(values[4], 15, 1e-6);
        CHECK_NEAR(values[5], 20, 1e-6);
    }
    cv_inp_free(&d); free(d.msgs.a);
}

static void test_eclass(void) {
    const char* deck =
        "*NODE\n1, 0,0,0\n2, 1,0,0\n3, 1,1,0\n4, 0,1,0\n5, 0,0,1\n6, 1,0,1\n7, 1,1,1\n8, 0,1,1\n"
        "*ELEMENT, TYPE=C3D8, ELSET=EALL\n1, 1,2,3,4,5,6,7,8\n2, 1,2,3,4,5,6,7,8\n3, 1,2,3,4,5,6,7,8\n"
        "*ELEMENT, TYPE=C3D8\n4, 1,2,3,4,5,6,7,8\n"
        "*ELSET, ELSET=EPART\n1, 2\n"
        "*ELSET, ELSET=EONE\n2\n"
        "*ELSET, ELSET=ETWIN\n2\n"                   /* as small as EONE, later: EONE keeps it */
        "*NSET, NSET=NN\n1\n"
        "*MATERIAL, NAME=STEEL\n*ELASTIC\n210000, 0.3\n"
        "*SOLID SECTION, ELSET=EALL, MATERIAL=STEEL\n"
        "*SHELL SECTION, ELSET=EONE, MATERIAL=STEEL\n1.\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    CHECK_EQ(d.nsects, 2);
    if (d.nsects == 2) {
        CHECK(!strcmp(d.sects[0].kind, "solid") && !strcmp(d.sects[0].elset, "EALL") && !strcmp(d.sects[0].mat, "STEEL"));
        CHECK(!strcmp(d.sects[1].kind, "shell"));
    }
    const cv_frd* f = &d.mesh;
    CHECK_EQ(f->n_elems, 4);
    int32_t sections[4];
    cv_elem_sections(f, &d, sections);
    CHECK_EQ(sections[cv_frd_elem_index(f, 1)], 0);
    CHECK_EQ(sections[cv_frd_elem_index(f, 2)], 1);
    CHECK_EQ(sections[cv_frd_elem_index(f, 3)], 0);
    CHECK_EQ(sections[cv_frd_elem_index(f, 4)], -1);
    float values[32];
    for (uint32_t e = 0; e < f->n_elems; e++)
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) values[j] = (float)(10 * (e + 1));
    CHECK(cv_elem_nodal_average(f, NULL, sections, values, 1));
    CHECK_NEAR(values[f->eoff[cv_frd_elem_index(f, 1)]], 20, 1e-6);
    CHECK_NEAR(values[f->eoff[cv_frd_elem_index(f, 3)]], 20, 1e-6);
    CHECK_NEAR(values[f->eoff[cv_frd_elem_index(f, 4)]], 40, 1e-6);
    cv_elem_sections(f, NULL, sections);
    for (int i = 0; i < 4; i++) CHECK_EQ(sections[i], -1);
    cv_eclass c;
    CHECK(cv_eclass_sets(&c, f, d.sets, d.nsets));
    CHECK_EQ(c.n, 4);                                   /* EALL, EPART, EONE, (no set); ETWIN unused */
    CHECK(c.none);
    if (c.n == 4 && f->n_elems == 4) {
        CHECK(!strcmp(c.name[c.of_elem[cv_frd_elem_index(f, 1)]], "EPART"));
        CHECK(!strcmp(c.name[c.of_elem[cv_frd_elem_index(f, 2)]], "EONE"));
        CHECK(!strcmp(c.name[c.of_elem[cv_frd_elem_index(f, 3)]], "EALL"));
        CHECK(!strcmp(c.name[c.of_elem[cv_frd_elem_index(f, 4)]], "(no set)"));
        CHECK_EQ(c.of_elem[cv_frd_elem_index(f, 4)], 3);   /* none last */
        CHECK(!strcmp(c.name[0], "EALL"));                /* deck order */
    }
    cv_eclass_free(&c);
    CHECK(cv_eclass_sections(&c, f, &d));
    CHECK_EQ(c.n, 3);
    if (c.n == 3 && f->n_elems == 4) {
        CHECK(!strcmp(c.name[c.of_elem[cv_frd_elem_index(f, 1)]], "solid: EALL (STEEL)"));
        CHECK(!strcmp(c.name[c.of_elem[cv_frd_elem_index(f, 2)]], "shell: EONE (STEEL)"));   /* the later wins */
        CHECK(!strcmp(c.name[c.of_elem[cv_frd_elem_index(f, 4)]], "(no section)"));
    }
    cv_eclass_free(&c);
    CHECK(cv_eclass_sets(&c, f, NULL, 0));               /* no sets: all in none */
    CHECK_EQ(c.n, 1);
    cv_eclass_free(&c);
    cv_inp_free(&d); free(d.msgs.a);

    /* the palette: the first 12 and the next ones all far apart, none grey */
    float p[40][3];
    for (int i = 0; i < 40; i++) cv_palette(i, p[i]);
    int close = 0;
    for (int i = 0; i < 12; i++)
        for (int j = i + 1; j < 12; j++) {
            float d2 = 0;
            for (int k = 0; k < 3; k++) d2 += (p[i][k] - p[j][k]) * (p[i][k] - p[j][k]);
            close += d2 < 0.06f;
        }
    CHECK_EQ(close, 0);
    for (int i = 0; i < 40; i++) {
        float mx = fmaxf(p[i][0], fmaxf(p[i][1], p[i][2])), mn = fminf(p[i][0], fminf(p[i][1], p[i][2]));
        CHECK(mx - mn > 0.25f && mx <= 1.f && mn >= 0.f);   /* coloured, not grey */
        CHECK(p[i][0] != p[(i + 1) % 40][0] || p[i][1] != p[(i + 1) % 40][1]);
    }
}
