/* t_inpname.h -- inp.c: lookups by name (many sets) (#45) */

static void test_inp_many_sets(void) {
    enum { N = 50000 };
    size_t cap = 64 + (size_t)N * 60;
    char* deck = malloc(cap);
    size_t n = (size_t)snprintf(deck, cap, "*NODE\n1,0,0,0\n2,1,0,0\n3,0,1,0\n4,0,0,1\n*ELEMENT, TYPE=C3D4, ELSET=E1\n1,1,2,3,4\n");
    for (int i = 0; i < N; i++)
        n += (size_t)snprintf(deck + n, cap - n, "*NSET, NSET=S%d\n%d\n*ELSET, ELSET=S%d\n1\n", i, 1 + i % 4, i);
    n += (size_t)snprintf(deck + n, cap - n, "*NSET, NSET=s7\n2\n*SURFACE, NAME=Q1\n1, S1\n*SURFACE, NAME=Q1\n1, S2\n");   /* s7 adds to S7; Q1 twice: the first wins */
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, n, NULL, NULL));
    CHECK_EQ(d.nsets, 2 * N + 1);                        /* S0..S49999 as node and element sets, E1 */
    const cv_set* a = cv_inp_set(&d, "s49999", false);
    const cv_set* b = cv_inp_set(&d, "S49999", true);
    CHECK(a && b && a != b && a->n == 1 && a->ids[0] == 1 + 49999 % 4 && !a->is_elem && b->is_elem && b->ids[0] == 1);
    const cv_set* s7 = cv_inp_set(&d, "S7", false);
    CHECK(s7 && s7->n == 2);                             /* its first line (id 4) and the later NSET=s7 (id 2) */
    CHECK(cv_inp_set(&d, "E1", true) && !cv_inp_set(&d, "E1", false) && !cv_inp_set(&d, "S50000", false));
    CHECK_EQ(d.nsurfs, 2);
    cv_inp_free(&d);
    free(d.msgs.a);
    free(deck);
}
