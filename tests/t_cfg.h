#include "../src/cfg.h"
/* t_cfg.h -- unit tests for cfg.c (settings file). Included by test_main.c. */

static void write_file(const char* path, const char* data, size_t n) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, n, f);
    fclose(f);
}

static void test_cfg(void) {
    const char* dir = "build";
    char path[1024];
    snprintf(path, sizeof path, "%s/t_cfg.ini", dir);

    /* ---- parse: comments, blanks, dup keys (last wins), unknown key kept ---- */
    const char* text =
        "# a comment\n"
        "\n"
        "width = 800\n"
        "; another style of comment\n"
        "name = first\n"
        "name = second\n"
        "unknownkey = keepme\n"
        "ratio = 1.5\n"
        "flag = yes\n";
    write_file(path, text, strlen(text));

    cv_cfg c;
    CHECK(cv_cfg_load(&c, path));
    CHECK(strcmp(c.path, path) == 0);
    CHECK(strcmp(cv_cfg_get(&c, "width", ""), "800") == 0);
    CHECK(strcmp(cv_cfg_get(&c, "name", ""), "second") == 0);   /* last wins */
    CHECK(strcmp(cv_cfg_get(&c, "unknownkey", ""), "keepme") == 0);
    CHECK(strcmp(cv_cfg_get(&c, "missing", "dflt"), "dflt") == 0);

    /* ---- typed getters ---- */
    CHECK_EQ(cv_cfg_get_int(&c, "width", -1), 800);
    CHECK_EQ(cv_cfg_get_int(&c, "name", -1), -1);              /* not a number: default */
    CHECK_EQ(cv_cfg_get_int(&c, "missing", 42), 42);
    CHECK_NEAR(cv_cfg_get_float(&c, "ratio", -1), 1.5, 1e-6);
    CHECK_NEAR(cv_cfg_get_float(&c, "name", -1), -1, 0);       /* bad value: default */
    CHECK_NEAR(cv_cfg_get_float(&c, "missing", 3.25f), 3.25, 1e-6);
    CHECK(cv_cfg_get_bool(&c, "flag", false) == true);

    /* ---- bool spellings ---- */
    const char* truthy[] = { "1", "true", "TRUE", "yes", "YES", "on", "On" };
    const char* falsy[]  = { "0", "false", "FALSE", "no", "NO", "off", "Off" };
    for (int i = 0; i < 7; i++) { cv_cfg_set(&c, "b", truthy[i]); CHECK(cv_cfg_get_bool(&c, "b", false) == true); }
    for (int i = 0; i < 7; i++) { cv_cfg_set(&c, "b", falsy[i]);  CHECK(cv_cfg_get_bool(&c, "b", true) == false); }
    cv_cfg_set(&c, "b", "sideways");
    CHECK(cv_cfg_get_bool(&c, "b", true) == true);              /* garbage: default */
    CHECK(cv_cfg_get_bool(&c, "b", false) == false);

    /* ---- typed setters round-trip ---- */
    cv_cfg_set_int(&c, "width", 1024);
    cv_cfg_set_float(&c, "ratio", 2.5f);
    cv_cfg_set_bool(&c, "flag", false);
    CHECK_EQ(cv_cfg_get_int(&c, "width", 0), 1024);
    CHECK_NEAR(cv_cfg_get_float(&c, "ratio", 0), 2.5, 1e-6);
    CHECK(cv_cfg_get_bool(&c, "flag", true) == false);

    /* a brand-new key, appended on save */
    cv_cfg_set(&c, "brandnew", "hello world");

    /* ---- save/load round trip: comment and unknown key survive ---- */
    CHECK(cv_cfg_save(&c));
    cv_cfg_free(&c);

    cv_cfg c2;
    CHECK(cv_cfg_load(&c2, path));
    CHECK(strcmp(cv_cfg_get(&c2, "unknownkey", ""), "keepme") == 0);
    CHECK_EQ(cv_cfg_get_int(&c2, "width", 0), 1024);
    CHECK(strcmp(cv_cfg_get(&c2, "brandnew", ""), "hello world") == 0);
    CHECK(c2.raw != NULL);
    CHECK(strstr(c2.raw, "# a comment") != NULL);
    CHECK(strstr(c2.raw, "; another style of comment") != NULL);
    cv_cfg_free(&c2);

    /* ---- missing file: empty config, true, path set ---- */
    char missing[1024];
    snprintf(missing, sizeof missing, "%s/does-not-exist.ini", dir);
    remove(missing);                                  /* a previous run saved it */
    cv_cfg c3;
    CHECK(cv_cfg_load(&c3, missing));
    CHECK(strcmp(c3.path, missing) == 0);
    CHECK_EQ(c3.n, 0);
    CHECK(c3.raw == NULL);
    CHECK(strcmp(cv_cfg_get(&c3, "anything", "d"), "d") == 0);
    /* cv_cfg_save on a fresh path should create the folder and the file */
    cv_cfg_set(&c3, "onlykey", "v");
    CHECK(cv_cfg_save(&c3));
    cv_cfg_free(&c3);
    cv_cfg c3b;
    CHECK(cv_cfg_load(&c3b, missing));
    CHECK(strcmp(cv_cfg_get(&c3b, "onlykey", ""), "v") == 0);
    cv_cfg_free(&c3b);

    /* ---- recent files: ordering, dedupe, limit ---- */
    cv_cfg r;
    CHECK(cv_cfg_load(&r, missing));       /* reuse: empty config, no file needed */
    cv_cfg_free(&r);
    memset(&r, 0, sizeof r);
    snprintf(r.path, sizeof r.path, "%s/recent.ini", dir);

    cv_cfg_add_recent(&r, "/a/one.frd");
    cv_cfg_add_recent(&r, "/a/two.frd");
    cv_cfg_add_recent(&r, "/a/three.frd");
    const char* out[CV_CFG_RECENT + 2];
    int n = cv_cfg_recent(&r, out, CV_CFG_RECENT);
    CHECK_EQ(n, 3);
    CHECK(strcmp(out[0], "/a/three.frd") == 0);
    CHECK(strcmp(out[1], "/a/two.frd") == 0);
    CHECK(strcmp(out[2], "/a/one.frd") == 0);

    cv_cfg_add_recent(&r, "/a/one.frd");   /* re-adding moves it to front, no duplicate */
    n = cv_cfg_recent(&r, out, CV_CFG_RECENT);
    CHECK_EQ(n, 3);
    CHECK(strcmp(out[0], "/a/one.frd") == 0);
    CHECK(strcmp(out[1], "/a/three.frd") == 0);
    CHECK(strcmp(out[2], "/a/two.frd") == 0);

    for (int i = 0; i < CV_CFG_RECENT + 5; i++) {
        char p[64];
        snprintf(p, sizeof p, "/many/%d.frd", i);
        cv_cfg_add_recent(&r, p);
    }
    n = cv_cfg_recent(&r, out, CV_CFG_RECENT);
    CHECK_EQ(n, CV_CFG_RECENT);                          /* capped at CV_CFG_RECENT */
    char last[64];
    snprintf(last, sizeof last, "/many/%d.frd", CV_CFG_RECENT + 4);
    CHECK(strcmp(out[0], last) == 0);

    /* a caller-supplied max smaller than the stored list */
    n = cv_cfg_recent(&r, out, 3);
    CHECK_EQ(n, 3);
    cv_cfg_free(&r);

    /* ---- truncation safety: parse every prefix of a realistic file ---- */
    const char* sample =
        "# ccxview settings\n"
        "width = 1920\n"
        "height = 1080\n"
        "; trailing comment\n"
        "recent0 = /very/long/path/to/some/file/that/goes/on/and/on/for/a/while.frd\n"
        "flag=true\n"
        "empty=\n"
        "no_newline_at_end = tail";
    size_t sn = strlen(sample);
    for (size_t k = 0; k <= sn; k++) {
        write_file(path, sample, k);
        cv_cfg t;
        CHECK(cv_cfg_load(&t, path));
        /* every getter must be safe to call regardless of how the file was cut */
        cv_cfg_get_int(&t, "width", 0);
        cv_cfg_get_float(&t, "height", 0);
        cv_cfg_get_bool(&t, "flag", false);
        cv_cfg_set(&t, "extra", "x");
        cv_cfg_save(&t);
        cv_cfg_free(&t);
    }

    /* an oversized value line must be truncated, not overrun */
    char big[4096];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = 0;
    char oversized[4200];
    snprintf(oversized, sizeof oversized, "hugekey = %s\n", big);
    write_file(path, oversized, strlen(oversized));
    cv_cfg tb;
    CHECK(cv_cfg_load(&tb, path));
    const char* v = cv_cfg_get(&tb, "hugekey", NULL);
    CHECK(v != NULL);
    CHECK(strlen(v) < sizeof(((cv_cfg_item*)0)->val));
    cv_cfg_free(&tb);

    /* an oversized key must also be truncated safely */
    char bigkey[300];
    memset(bigkey, 'k', sizeof bigkey - 1);
    bigkey[sizeof bigkey - 1] = 0;
    char oversized_key[400];
    snprintf(oversized_key, sizeof oversized_key, "%s = v\n", bigkey);
    write_file(path, oversized_key, strlen(oversized_key));
    cv_cfg tk;
    CHECK(cv_cfg_load(&tk, path));
    CHECK_EQ(tk.n, 1);
    if (tk.n == 1) CHECK(strlen(tk.a[0].key) < sizeof tk.a[0].key);
    cv_cfg_free(&tk);

    /* ---- unset and a new layout: sections in a fixed order, the rest at the end ---- */
    write_file(path, "# old\nb = 2\nold = x\na = 1\nextra = e\n", 36);
    cv_cfg tl;
    CHECK(cv_cfg_load(&tl, path));
    cv_cfg_unset(&tl, "old");
    cv_cfg_unset(&tl, "missing");
    CHECK_EQ(tl.n, 3);
    cv_cfg_set_layout(&tl, "# head\na = _\n\nb = _\n");
    CHECK(cv_cfg_save(&tl));
    cv_cfg_free(&tl);
    {
        FILE* f = fopen(path, "rb");
        char got[128] = { 0 };
        if (f) { fread(got, 1, sizeof got - 1, f); fclose(f); }
        CHECK(strcmp(got, "# head\na = 1\n\nb = 2\nextra = e\n") == 0);
    }

    /* ---- garbage with binary bytes must never crash ---- */
    unsigned char garbage[300];
    uint32_t rng = 0xC0FFEE;
    for (int it = 0; it < 500; it++) {
        for (size_t i = 0; i < sizeof garbage; i++) {
            rng = rng * 1664525u + 1013904223u;
            garbage[i] = (unsigned char)(rng >> 16);
        }
        write_file(path, (const char*)garbage, sizeof garbage);
        cv_cfg g;
        CHECK(cv_cfg_load(&g, path));
        cv_cfg_get(&g, "x", "d");
        cv_cfg_get_int(&g, "x", 0);
        cv_cfg_get_float(&g, "x", 0);
        cv_cfg_get_bool(&g, "x", false);
        cv_cfg_set(&g, "after", "ok");
        cv_cfg_save(&g);
        cv_cfg_free(&g);
    }

    /* the keys as text, in order, the layout ignored */
    {
        cv_cfg t = {0};
        char* e = cv_cfg_text(&t);
        CHECK(e && !strcmp(e, ""));
        free(e);
        cv_cfg_set(&t, "b", "2"); cv_cfg_set(&t, "a", "one two"); cv_cfg_set_layout(&t, "# ignored\n");
        e = cv_cfg_text(&t);
        CHECK(e && !strcmp(e, "b = 2\na = one two\n"));
        free(e);
        cv_cfg_free(&t);
    }

    /* long values: split under the line length, through a file and back */
    {
        char* big = malloc(20000);
        size_t o = 0;
        for (int i = 0; i < 2000; i++) o += (size_t)snprintf(big + o, 20000 - o, "%s%d", i ? ", " : "", i * 3);
        cv_cfg t = {0};
        snprintf(t.path, sizeof t.path, "build/t_cfg_long.ini");
        cv_cfg_set_long(&t, "ids", big);
        cv_cfg_set_long(&t, "short", "1, 2");
        cv_cfg_set_long(&t, "empty", "");
        for (int i = 0; i < t.n; i++) CHECK(strlen(t.a[i].val) < sizeof t.a[i].val - 1);
        CHECK(t.n > 3);                                        /* ids took several keys */
        CHECK(cv_cfg_save(&t));
        cv_cfg_free(&t);
        CHECK(cv_cfg_load(&t, "build/t_cfg_long.ini"));
        char* back = cv_cfg_get_long(&t, "ids");
        CHECK(back && !strcmp(back, big));
        free(back);
        back = cv_cfg_get_long(&t, "short");
        CHECK(back && !strcmp(back, "1, 2"));
        free(back);
        back = cv_cfg_get_long(&t, "empty");
        CHECK(back && !strcmp(back, ""));
        free(back);
        CHECK(cv_cfg_get_long(&t, "absent") == NULL);
        cv_cfg_free(&t);
        remove("build/t_cfg_long.ini");
        free(big);
    }

    /* default path resolves to something non-empty when HOME/XDG/APPDATA exist */
    char dp[1024];
    bool got = cv_cfg_default_path(dp, sizeof dp);
    CHECK(got == false || (got && dp[0] != 0 && strstr(dp, "ccxview") != NULL));
}
