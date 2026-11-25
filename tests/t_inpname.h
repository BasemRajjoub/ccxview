/* t_inpname.h -- inp.c: lookups by name (many sets) and surfaces named before they are defined (#44, #45) */

static bool inpname_reader(void* user, const char* path, char** data, size_t* size) {
    const char* t = !strcmp(path, "surf.inp") ? "*SURFACE, TYPE=NODE, NAME=Inc\n3, 1.\n4, 1.\n"
                  : !strcmp(path, "sets.inp") ? "*NSET, NSET=INCN\n3, 4\n*ELSET, ELSET=INCE\n1\n" : NULL;
    if (!t) return false;
    *size = strlen(t);
    *data = malloc(*size + 1);
    memcpy(*data, t, *size + 1);
    return true;
}

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

static int inpname_unreadable(const cv_inp* d) {
    for (size_t i = 0; i < d->msgs.n; i++) if (strstr(d->msgs.a[i].text, "unreadable data lines")) return atoi(d->msgs.a[i].text);
    return 0;
}

static void test_inp_surface_order(void) {
    const char* deck =
        "*NODE\n1,0,0,0\n2,1,0,0\n3,0,1,0\n4,0,0,1\n9,5,5,5\n"
        "*ELEMENT, TYPE=C3D4, ELSET=E\n1,1,2,3,4\n"
        "*SURFACE, TYPE=NODE, NAME=Before\n1, 1.\n"
        "*COUPLING, REF NODE=9, SURFACE=before, CONSTRAINT NAME=c1\n*KINEMATIC\n1, 3\n"
        "*COUPLING, REF NODE=9, SURFACE=AFTER, CONSTRAINT NAME=c2\n*KINEMATIC\n1, 3\n"
        "*COUPLING, REF NODE=9, SURFACE=inc, CONSTRAINT NAME=c3\n*KINEMATIC\n1, 3\n"
        "*COUPLING, REF NODE=9, SURFACE=nowhere, CONSTRAINT NAME=c4\n*KINEMATIC\n1, 3\n"
        "*SURFACE, TYPE=NODE, NAME=After\n2, 1.\n"
        "*INCLUDE, INPUT=surf.inp\n"
        "*SURFACE, TYPE=NODE, NAME=M\n3, 1.\n"
        "*TIE, NAME=t1\nafter, m\n"
        "*TIE, NAME=t2\nlate, m\n"
        "*CONTACT PAIR, INTERACTION=I\nm, LATE\n"
        "*SURFACE, TYPE=NODE, NAME=Late\n4, 1.\n"
        "*NSET, NSET=NA\n1\n"
        "*RIGID BODY, NSET=LATENS, REF NODE=9\n"
        "*RIGID BODY, NSET=gone, REF NODE=9\n"
        "*NSET, NSET=LateNS\n2, 3\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), inpname_reader, NULL));
    int idx[3];
    for (int k = 0; k < 3; k++) {
        const char* want[3] = { "BEFORE", "AFTER", "INC" };
        idx[k] = -1;
        for (int i = 0; i < d.nsurfs; i++) if (!strcmp(d.surfs[i].name, want[k])) idx[k] = i;
        CHECK(idx[k] >= 0);
    }
    CHECK_EQ(d.nlinks, 4 + 2 + 1 + 2);
    if (d.nlinks == 9) {
        CHECK_EQ(d.links[0].surf[0], idx[0]);            /* defined before, any case */
        CHECK_EQ(d.links[1].surf[0], idx[1]);            /* defined after */
        CHECK_EQ(d.links[2].surf[0], idx[2]);            /* defined in an included file */
        CHECK_EQ(d.links[3].surf[0], -1);                /* never defined */
        CHECK_EQ(d.links[4].surf[0], idx[1]);            /* *TIE: after, m */
        CHECK(d.links[4].surf[1] >= 0);
        CHECK(d.links[5].surf[0] >= 0 && d.links[5].surf[1] >= 0);  /* t2 and the contact pair: LATE comes after them */
        CHECK(d.links[6].surf[0] >= 0 && d.links[6].surf[1] >= 0);
        CHECK_EQ(d.links[7].n, 2);                       /* rigid body of a set defined below it */
        CHECK_EQ(d.links[8].n, 0);
    }
    CHECK_EQ(inpname_unreadable(&d), 2);                 /* c4 and the rigid body of a set never defined */
    cv_inp_free(&d);
    free(d.msgs.a);
}

/* a *SURFACE line may name an *NSET / *ELSET defined further down (or in an include) */
static void test_inp_surface_late_sets(void) {
    const char* deck =
        "*NODE\n1,0,0,0\n2,1,0,0\n3,0,1,0\n4,0,0,1\n9,5,5,5\n"
        "*ELEMENT, TYPE=C3D4, ELSET=E\n1,1,2,3,4\n"
        "*SURFACE, TYPE=NODE, NAME=SN\nlatens\n"
        "*SURFACE, TYPE=ELEMENT, NAME=SE\nlatee, S3\n"
        "*SURFACE, TYPE=NODE, NAME=SI\nincn\n"
        "*SURFACE, TYPE=ELEMENT, NAME=SJ\nince, S2\n"
        "*SURFACE, TYPE=ELEMENT, NAME=SX\nnever, S1\n"
        "*COUPLING, REF NODE=9, SURFACE=SN, CONSTRAINT NAME=c1\n*KINEMATIC\n1, 3\n"
        "*SURFACE, TYPE=ELEMENT, NAME=SK\nlatee, S1\n"
        "*SURFACE, TYPE=ELEMENT, NAME=SL\nlatee, S1\n"
        "*STEP\n*DSLOAD\nSK, P, 1.\nSL, P, 1.\n*END STEP\n"
        "*NSET, NSET=LATENS\n2, 3\n"
        "*ELSET, ELSET=LATEE\n1\n"
        "*INCLUDE, INPUT=sets.inp\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), inpname_reader, NULL));
    const char* nm[6] = { "SN", "SE", "SI", "SJ", "SX", "SK" };
    const cv_surface* s[6] = { 0 };
    for (int k = 0; k < 6; k++)
        for (int i = 0; i < d.nsurfs; i++) if (!strcmp(d.surfs[i].name, nm[k])) s[k] = &d.surfs[i];
    CHECK(s[0] && s[0]->nn == 2 && s[0]->nodes[0] == 2 && s[0]->nodes[1] == 3);
    CHECK(s[1] && s[1]->n == 1 && s[1]->elem[0] == 1 && s[1]->face[0] == 2);
    CHECK(s[2] && s[2]->nn == 2);
    CHECK(s[3] && s[3]->n == 1 && s[3]->face[0] == 1);
    CHECK(s[4] && s[4]->n == 0);
    CHECK(s[5] && s[5]->n == 1);
    CHECK_EQ(d.nlinks, 1);
    if (d.nlinks == 1 && s[0]) CHECK(d.links[0].surf[0] == (int)(s[0] - d.surfs));
    CHECK_EQ(d.ndloads, 0);                              /* the sets come after both loads */
    CHECK_EQ(inpname_unreadable(&d), 3);                 /* SX's set, and the two loads */
    cv_inp_free(&d);
    free(d.msgs.a);
}

static void test_inp_dsload_set_order(void) {
    const char* deck =
        "*NODE\n1,0,0,0\n2,1,0,0\n3,0,1,0\n4,0,0,1\n"
        "*ELEMENT, TYPE=C3D4, ELSET=E\n1,1,2,3,4\n"
        "*ELSET, ELSET=EARLY\n1\n"
        "*SURFACE, TYPE=ELEMENT, NAME=SA\nearly, S1\n"
        "*STEP\n*DSLOAD\nSA, P, 2.\n*END STEP\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    CHECK_EQ(d.ndloads, 1);
    CHECK_EQ(inpname_unreadable(&d), 0);
    cv_inp_free(&d);
    free(d.msgs.a);
}
