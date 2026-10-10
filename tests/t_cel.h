/* t_cel.h -- unit tests for src/cel.c: contact elements (.cel) and warning node sets (.nam). */
#ifndef CV_T_CEL_H
#define CV_T_CEL_H

#include "../src/cel.h"

static bool cel_str(cv_cel* c, const char* s) { return cv_cel_parse(c, s, strlen(s)); }

static void test_cel(void) {
    cv_cel c;

    /* node to surface: a quadrilateral face (C3D6) and a triangle (C3D4), blanks and case in the keyword */
    CHECK(cel_str(&c,
        "*ELEMENT,TYPE=C3D6,ELSET=contactelements_st1_in1_at1_it1\n"
        "         613 ,         268 ,         376 ,         281 ,         267 ,         376 ,         280\n"
        "* element , type = c3d4 , elset = CONTACTELEMENTS_ST1_IN1_AT1_IT1\n"
        "  614, 10, 11, 12, 99\n"));
    CHECK_EQ(c.n, 2); CHECK_EQ(c.nsets, 1); CHECK_EQ(c.bad, 0);
    if (c.n == 2) {
        const cv_celem* e = &c.elem[0];
        CHECK_EQ(e->id, 613); CHECK_EQ(e->kind, CV_CEL_N2S); CHECK_EQ(e->nm, 4); CHECK_EQ(e->ns, 1);
        CHECK_EQ(e->m[0], 268); CHECK_EQ(e->m[1], 281); CHECK_EQ(e->m[2], 280); CHECK_EQ(e->m[3], 267);
        CHECK_EQ(e->s[0], 376);
        e = &c.elem[1];
        CHECK_EQ(e->kind, CV_CEL_N2S); CHECK_EQ(e->nm, 3); CHECK_EQ(e->ns, 1);
        CHECK_EQ(e->m[0], 10); CHECK_EQ(e->m[2], 12); CHECK_EQ(e->s[0], 99);
    }
    if (c.nsets == 1) {
        CHECK(!strcmp(c.sets[0].name, "contactelements_st1_in1_at1_it1"));
        CHECK_EQ(c.sets[0].step, 1); CHECK_EQ(c.sets[0].it, 1); CHECK_EQ(c.sets[0].n, 2);
    }
    cv_cel_free(&c);
    CHECK(c.elem == NULL && c.n == 0 && c.nsets == 0);

    /* surface to surface: a triangle master (last corner repeated), a quad; repeats per integration point;
       continuation lines; one card with two data lines */
    CHECK(cel_str(&c,
        "** a comment\n"
        "\n"
        "*ELEMENT,TYPE=C3D8,ELSET=contactelements_st1_in2_at1_it1\n"
        " 805, 253, 254, 267, 267, 375, 374, 358, 359\n"
        "*ELEMENT,TYPE=C3D8,ELSET=contactelements_st1_in2_at1_it1\n"
        " 806, 253, 254, 267, 267,\n"
        "      375, 374, 358, 359\n"
        "*ELEMENT,TYPE=C3D8,ELSET=contactelements_st1_in2_at1_it1\n"
        " 807, 1, 2, 3, 4, 5, 6, 7, 8\n"
        " 808, 1, 2, 3, 4, 5, 6, 7, 8\n"
        " 809, 253, 254, 267, 267, 375, 374, 358, 359\n"));
    CHECK_EQ(c.n, 5); CHECK_EQ(c.nsets, 1); CHECK_EQ(c.bad, 0);
    if (c.n == 5) {
        CHECK_EQ(c.elem[0].kind, CV_CEL_S2S); CHECK_EQ(c.elem[0].nm, 3); CHECK_EQ(c.elem[0].ns, 4);
        CHECK_EQ(c.elem[0].m[2], 267); CHECK_EQ(c.elem[0].s[3], 359);
        CHECK_EQ(c.elem[1].id, 806); CHECK_EQ(c.elem[1].s[0], 375);
        CHECK_EQ(c.elem[2].nm, 4); CHECK_EQ(c.elem[2].m[3], 4); CHECK_EQ(c.elem[2].s[0], 5);
    }
    if (c.nsets == 1) {
        uint32_t u[5];
        CHECK_EQ(cv_cel_unique(&c, 0, NULL), 2);
        CHECK_EQ(cv_cel_unique(&c, 0, u), 2);
        CHECK_EQ(u[0], 0); CHECK_EQ(u[1], 2);
    }
    CHECK_EQ(cv_cel_unique(&c, 5, NULL), 0);
    cv_cel_free(&c);

    /* sets: two increments of two iterations, a second attempt of increment 1, written out of order;
       an unnamed set; the same name again further down */
    {
        const char* t =
            "*ELEMENT,TYPE=C3D4,ELSET=other\n 1, 1, 2, 3, 4\n"
            "*ELEMENT,TYPE=C3D4,ELSET=contactelements_st1_in2_at1_it1\n 2, 1, 2, 3, 4\n"
            "*ELEMENT,TYPE=C3D4,ELSET=contactelements_st1_in1_at1_it1\n 3, 1, 2, 3, 4\n"
            "*ELEMENT,TYPE=C3D4,ELSET=contactelements_st1_in1_at1_it2\n 4, 1, 2, 3, 4\n"
            "*ELEMENT,TYPE=C3D4,ELSET=contactelements_st1_in1_at2_it1\n 5, 1, 2, 3, 4\n"
            "*ELEMENT,TYPE=C3D4,ELSET=contactelements_st1_in1_at2_it2\n 6, 1, 2, 3, 4\n"
            "*ELEMENT,TYPE=C3D4,ELSET=contactelements_st1_in2_at1_it2\n 7, 1, 2, 3, 4\n"
            "*ELEMENT,TYPE=C3D4,ELSET=contactelements_st1_in2_at1_it1\n 8, 1, 2, 3, 4\n";
        CHECK(cel_str(&c, t));
        CHECK_EQ(c.n, 8); CHECK_EQ(c.nsets, 7);
        if (c.nsets == 7) {
            CHECK_EQ(c.sets[0].inc, 1); CHECK_EQ(c.sets[0].att, 1); CHECK_EQ(c.sets[0].it, 1);
            CHECK_EQ(c.sets[3].att, 2); CHECK_EQ(c.sets[3].it, 2);
            CHECK_EQ(c.sets[4].inc, 2); CHECK_EQ(c.sets[4].it, 1); CHECK_EQ(c.sets[4].n, 2);
            CHECK(!strcmp(c.sets[6].name, "other")); CHECK_EQ(c.sets[6].step, 0);
            /* each set contiguous, file order within it */
            CHECK_EQ(c.elem[c.sets[4].first].id, 2); CHECK_EQ(c.elem[c.sets[4].first + 1].id, 8);
            CHECK_EQ(c.elem[c.sets[6].first].id, 1);
            CHECK_EQ(cv_cel_find(&c, 1, 1), 3);
            CHECK_EQ(cv_cel_find(&c, 1, 2), 5);
            CHECK_EQ(cv_cel_find(&c, 1, 3), -1);
            CHECK_EQ(cv_cel_find(&c, 2, 1), -1);
            int ends[4] = { -1, -1, -1, -1 };
            CHECK_EQ(cv_cel_ends(&c, NULL, 0), 2);
            CHECK_EQ(cv_cel_ends(&c, ends, 4), 2);
            CHECK_EQ(ends[0], 3); CHECK_EQ(ends[1], 5);
            CHECK_EQ(cv_cel_ends(&c, ends, 1), 1);
        }
        cv_cel_free(&c);
    }

    /* garbage: unknown types, broken lines, a wedge whose slave does not repeat, too many nodes;
       data lines under other keywords are not read */
    CHECK(cel_str(&c,
        "*NODE\n 1, 0, 0, 0\n"
        "*ELEMENT,TYPE=S4,ELSET=contactelements_st1_in1_at1_it1\n 1, 1, 2, 3, 4\n"
        "*ELEMENT,TYPE=C3D6,ELSET=contactelements_st1_in1_at1_it1\n 2, 1, 2, 3, 4, 9, 6\n"
        "*ELEMENT,TYPE=C3D4,ELSET=contactelements_st1_in1_at1_it1\n 3, 1, x, 3, 4\n 4, 1, 2\n 5, 1, 2, 3, 4, 5\n"
        " 6, 1, 2, 3,\n*ELEMENT,TYPE=C3D8\n 7, 1, 1, 1, 1, 2, 3, 4, 5\n"
        "*ELEMENT,TYPE=C3D4,ELSET=contactelements_st1_in1_at1_it1\n 8, 1, 2, 3, 4\n"
        "\x01\xff garbage ,,,\n*\n*,,,=,\n"));
    CHECK_EQ(c.n, 1); CHECK_EQ(c.bad, 8);
    if (c.n == 1) CHECK_EQ(c.elem[0].id, 8);
    cv_cel_free(&c);

    /* empty */
    CHECK(cv_cel_parse(&c, "", 0)); CHECK_EQ(c.n, 0); CHECK_EQ(c.nsets, 0);
    CHECK_EQ(cv_cel_find(&c, 1, 1), -1); CHECK_EQ(cv_cel_ends(&c, NULL, 0), 0);
    cv_cel_free(&c);
    CHECK(cv_cel_parse(&c, NULL, 0)); cv_cel_free(&c);

    /* .nam: one set, then two */
    {
        uint32_t* ids; uint32_t n; char name[64];
        const char* a = " *NSET,NSET=WarnNodeMissTiedContact\n         146\n         145\n";
        CHECK(cv_nam_parse(a, strlen(a), &ids, &n, name));
        CHECK_EQ(n, 2);
        if (n == 2) { CHECK_EQ(ids[0], 145); CHECK_EQ(ids[1], 146); }
        CHECK(!strcmp(name, "WARNNODEMISSTIEDCONTACT"));
        free(ids);
        const char* b = "*NSET,NSET=A\n 7, 3\n*NSET,NSET=B\n 3\n 9\n";
        CHECK(cv_nam_parse(b, strlen(b), &ids, &n, NULL));
        CHECK_EQ(n, 3);
        if (n == 3) { CHECK_EQ(ids[0], 3); CHECK_EQ(ids[1], 7); CHECK_EQ(ids[2], 9); }
        free(ids);
        const char* e = "** nothing\n";
        CHECK(!cv_nam_parse(e, strlen(e), &ids, &n, name));
        CHECK(ids == NULL); CHECK_EQ(n, 0);
    }
}

#endif
