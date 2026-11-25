/* t_calclib.h -- the formula library: parsing, merging, round trip. Included by test_main.c. */
#include "../src/calclib.h"

static void test_calclib(void) {
    cv_calclib L = {0};
    int dup, bad, fb;
    const char* txt = "# my formulas\n\nvm = sqrt(STRESS_SXX^2 + STRESS_SYY^2)\n  SAFE =  if(MISES > 200, 1, 0)  \r\n"
                      "nonsense line\n= 5\nempty =\nvm = 1\ncmp = A == B\n";
    CHECK_EQ(cv_calclib_merge(&L, txt, &dup, &bad, &fb), 3);
    CHECK_EQ(dup, 1); CHECK_EQ(bad, 3); CHECK_EQ(fb, 5);
    CHECK_EQ(L.n, 3);
    CHECK(!strcmp(L.a[0].name, "vm") && !strcmp(L.a[0].expr, "sqrt(STRESS_SXX^2 + STRESS_SYY^2)"));
    CHECK(!strcmp(L.a[1].name, "SAFE") && !strcmp(L.a[1].expr, "if(MISES > 200, 1, 0)"));
    CHECK(!strcmp(L.a[2].expr, "A == B"));                     /* only the first = splits */
    CHECK_EQ(cv_calclib_find(&L, "safe"), 1);
    CHECK_EQ(cv_calclib_find(&L, "none"), -1);

    CHECK(cv_calclib_set(&L, "vm", "MISES"));                  /* replaces */
    CHECK_EQ(L.n, 3); CHECK(!strcmp(L.a[0].expr, "MISES"));
    CHECK(!cv_calclib_set(&L, "", "x")); CHECK(!cv_calclib_set(&L, "a=b", "x")); CHECK(!cv_calclib_set(&L, "n", "  "));
    CHECK(cv_calclib_set(&L, "new", "1+1")); CHECK_EQ(L.n, 4);

    char* t = cv_calclib_text(&L);
    CHECK(t != NULL);
    cv_calclib M = {0};
    CHECK_EQ(cv_calclib_merge(&M, t, &dup, &bad, &fb), 4);     /* round trip */
    CHECK_EQ(bad, 0); CHECK_EQ(dup, 0);
    CHECK(!strcmp(M.a[3].name, "new") && !strcmp(M.a[0].expr, "MISES"));
    free(t);

    CHECK_EQ(cv_calclib_merge(&M, "vm = 7\nextra = 2\n", &dup, &bad, &fb), 1);   /* merge skips duplicates */
    CHECK_EQ(dup, 1); CHECK_EQ(M.n, 5); CHECK(!strcmp(M.a[0].expr, "MISES"));
    cv_calclib_remove(&M, 0); CHECK_EQ(M.n, 4); CHECK(!strcmp(M.a[0].name, "SAFE"));
    cv_calclib_remove(&M, 9); CHECK_EQ(M.n, 4);

    char big[400];
    memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = 0;
    char line[500];
    snprintf(line, sizeof line, "long = %s\n", big);
    cv_calclib N = {0};
    CHECK_EQ(cv_calclib_merge(&N, line, &dup, &bad, &fb), 0); CHECK_EQ(bad, 1);   /* too long */

    const char* path = "build/t_calclib.txt";
    CHECK(cv_calclib_save(&L, path));
    cv_calclib P = {0};
    CHECK(cv_calclib_load(&P, path, &bad)); CHECK_EQ(P.n, 4); CHECK_EQ(bad, 0);
    remove(path);
    cv_calclib Q = {0};
    CHECK(cv_calclib_load(&Q, "build/does_not_exist.txt", &bad)); CHECK_EQ(Q.n, 0);
    cv_calclib_free(&L); cv_calclib_free(&M); cv_calclib_free(&N); cv_calclib_free(&P); cv_calclib_free(&Q);
}
