#include "../src/idlist.h"
/* t_idlist.h -- unit tests for idlist.c (id lists as text). Included by test_main.c. */

static bool idl_is(const cv_idlist* l, const uint32_t* want, size_t n) {
    if (l->n != n) return false;
    for (size_t i = 0; i < n; i++) if (l->ids[i] != want[i]) return false;
    return true;
}

static void test_idlist(void) {
    /* ---- format: runs as ranges, pairs as two ids, singles ---- */
    uint32_t a[] = { 1, 2, 3, 4, 5, 205, 300, 301, 302, 400, 401, 4294967295u };
    char* s = cv_idlist_format(a, CV_COUNT(a));
    CHECK(s && !strcmp(s, "1-5, 205, 300-302, 400, 401, 4294967295"));
    free(s);
    s = cv_idlist_format(NULL, 0);
    CHECK(s && !strcmp(s, ""));
    free(s);
    uint32_t one[] = { 7 };
    s = cv_idlist_format(one, 1);
    CHECK(s && !strcmp(s, "7"));
    free(s);
    uint32_t top[] = { 4294967294u, 4294967295u };          /* no wrap past the largest id */
    s = cv_idlist_format(top, 2);
    CHECK(s && !strcmp(s, "4294967294, 4294967295"));
    free(s);

    /* ---- parse: what format writes comes back ---- */
    cv_idlist l;
    CHECK(cv_idlist_parse("1-5, 205, 300-302, 400, 401, 4294967295", &l));
    CHECK(idl_is(&l, a, CV_COUNT(a)));
    CHECK_EQ(l.bad, 0);
    cv_idlist_free(&l);

    /* separators of every kind, a range backwards and with spaces, duplicates once, sorted */
    CHECK(cv_idlist_parse("  12;3\n\t10 - 8 ,, 3 9", &l));
    uint32_t w1[] = { 3, 8, 9, 10, 12 };
    CHECK(idl_is(&l, w1, CV_COUNT(w1)));
    CHECK_EQ(l.bad, 0);
    cv_idlist_free(&l);

    /* garbage is skipped and reported, the rest kept */
    CHECK(cv_idlist_parse("4, abc, 5x, 6-, -7, 99999999999, 8", &l));
    uint32_t w2[] = { 4, 8 };
    CHECK(idl_is(&l, w2, CV_COUNT(w2)));
    CHECK_EQ(l.bad, 5);
    CHECK(!strncmp(l.bad_text, "abc, 5x, 6-, -7", 15));
    cv_idlist_free(&l);

    /* nothing, NULL, only separators */
    CHECK(cv_idlist_parse("", &l)); CHECK_EQ(l.n, 0); CHECK(l.ids == NULL); cv_idlist_free(&l);
    CHECK(cv_idlist_parse(NULL, &l)); CHECK_EQ(l.n, 0); cv_idlist_free(&l);
    CHECK(cv_idlist_parse(" ,; \n", &l)); CHECK_EQ(l.n, 0); CHECK_EQ(l.bad, 0); cv_idlist_free(&l);

    /* a range too big to expand is refused, not allocated */
    CHECK(!cv_idlist_parse("1-4000000000", &l));
    CHECK_EQ(l.n, 0);
    cv_idlist_free(&l);

    /* many bad tokens: the text is cut short, never overflows */
    char many[2000] = "";
    for (int i = 0; i < 200; i++) strcat(many, "bad ");
    CHECK(cv_idlist_parse(many, &l));
    CHECK_EQ(l.bad, 200);
    CHECK(strlen(l.bad_text) < sizeof l.bad_text);
    cv_idlist_free(&l);

    /* round trip of a large shuffled set */
    enum { N = 5000 };
    uint32_t* ids = malloc(N * sizeof *ids);
    for (uint32_t i = 0; i < N; i++) ids[i] = (i * 7919u) % 20011u + (i % 3 ? 0 : 100000u);
    qsort(ids, N, sizeof *ids, cv_cmp_u32);
    size_t m = 0;
    for (size_t i = 0; i < N; i++) if (!m || ids[i] != ids[m - 1]) ids[m++] = ids[i];
    s = cv_idlist_format(ids, m);
    CHECK(s != NULL);
    CHECK(cv_idlist_parse(s, &l));
    CHECK(idl_is(&l, ids, m));
    cv_idlist_free(&l);
    free(s);
    free(ids);
}
