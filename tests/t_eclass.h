#include "../src/eclass.h"
/* t_eclass.h -- unit tests for eclass.c (elements by set, by section; the palette). Included by test_main.c. */

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
