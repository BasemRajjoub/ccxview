/* t_tbtext.h -- unit tests for src/tbtext.h (the title block's template). */
#ifndef CV_T_TBTEXT_H
#define CV_T_TBTEXT_H

#include "../src/tbtext.h"

/* one line filled in: whether it is kept, the text, the label's end */
static bool tb_line(const cv_tb_ctx* c, const char* line, char* out, size_t n, int* split) {
    return cv_tb_line(line, strlen(line), c, out, n, split);
}

static void test_tbtext(void) {
    const cv_tb_kv kv[] = { { "file", "plate.frd" }, { "title", "Bracket: rev B" }, { "user", "" }, { "units", "N, mm" } };
    struct tm now = { 0 }, file = { 0 };
    now.tm_year = 126; now.tm_mon = 9; now.tm_mday = 9; now.tm_hour = 7; now.tm_min = 5; now.tm_wday = 5;
    file.tm_year = 125; file.tm_mon = 0; file.tm_mday = 31; file.tm_hour = 23; file.tm_min = 59;
    cv_tb_ctx c = { kv, 4, NULL, &now, &file };
    char o[256];
    int sp;

    /* a label and a value; the label's ": " is found in the template, not in a value */
    CHECK(tb_line(&c, "Result file: {file}", o, sizeof o, &sp));
    CHECK(!strcmp(o, "Result file: plate.frd")); CHECK_EQ(sp, 11);
    CHECK(tb_line(&c, "{title}", o, sizeof o, &sp));
    CHECK(!strcmp(o, "Bracket: rev B")); CHECK_EQ(sp, -1);
    CHECK(tb_line(&c, "Project Bracket", o, sizeof o, &sp)); CHECK_EQ(sp, -1);
    CHECK(tb_line(&c, "Ratio:1 here: x", o, sizeof o, &sp)); CHECK_EQ(sp, 12);     /* ":" alone is no label */
    CHECK(tb_line(&c, "Line\r", o, sizeof o, &sp)); CHECK(!strcmp(o, "Line"));

    /* empty placeholders drop the line, a literal line stays, a blank one goes */
    CHECK(!tb_line(&c, "User: {user}", o, sizeof o, &sp));
    CHECK(tb_line(&c, "Who: {user} {file}", o, sizeof o, &sp));
    CHECK(!strcmp(o, "Who:  plate.frd"));
    CHECK(tb_line(&c, "Checked: ", o, sizeof o, &sp));                 /* a field to fill in by hand */
    CHECK(!tb_line(&c, "", o, sizeof o, &sp));
    CHECK(!tb_line(&c, "   ", o, sizeof o, &sp));

    /* braces: {{ writes one, unknown and unclosed stay as typed */
    CHECK(tb_line(&c, "a {{file} b", o, sizeof o, &sp)); CHECK(!strcmp(o, "a {file} b"));
    CHECK(tb_line(&c, "x {nope} y", o, sizeof o, &sp)); CHECK(!strcmp(o, "x {nope} y"));
    CHECK(tb_line(&c, "x {file", o, sizeof o, &sp)); CHECK(!strcmp(o, "x {file"));
    CHECK(tb_line(&c, "{a{file}}", o, sizeof o, &sp)); CHECK(!strcmp(o, "{aplate.frd}"));
    CHECK(tb_line(&c, "{}", o, sizeof o, &sp)); CHECK(!strcmp(o, "{}"));
    CHECK(tb_line(&c, "{file:x}", o, sizeof o, &sp)); CHECK(!strcmp(o, "{file:x}"));   /* a format only for dates */
    CHECK(tb_line(&c, "}", o, sizeof o, &sp)); CHECK(!strcmp(o, "}"));

    /* dates: the default format, the chosen one, one of its own, the result file's */
    CHECK(tb_line(&c, "Date: {date}", o, sizeof o, &sp)); CHECK(!strcmp(o, "Date: 2026-10-09"));
    c.date_fmt = cv_tb_date_fmt[1];
    CHECK(tb_line(&c, "{date} {date_file}", o, sizeof o, &sp)); CHECK(!strcmp(o, "09.10.2026 31.01.2025"));
    CHECK(tb_line(&c, "{date:%d/%m/%y %H:%M}", o, sizeof o, &sp)); CHECK(!strcmp(o, "09/10/26 07:05"));
    CHECK(tb_line(&c, "{time_now}", o, sizeof o, &sp)); CHECK(!strcmp(o, "07:05"));
    CHECK(tb_line(&c, "{date:}", o, sizeof o, &sp)); CHECK(!strcmp(o, "09.10.2026"));
    c.file = NULL;
    CHECK(!tb_line(&c, "Solved: {date_file}", o, sizeof o, &sp));
    c.file = &file;

    /* strftime: the presets, no leading zero, the compounds, unknown ones as typed */
    const char* want[CV_TB_DATE_N] = { "2026-10-09", "09.10.2026", "09/10/2026", "10/09/2026", "9 Oct 2026", "October 9, 2026" };
    for (int i = 0; i < CV_TB_DATE_N; i++) {
        cv_tb_strftime(o, sizeof o, cv_tb_date_fmt[i], &now);
        CHECK(!strcmp(o, want[i]));
    }
    cv_tb_strftime(o, sizeof o, "%F %T|%R|%e|%-H|%%|%q|%", &now); CHECK(!strcmp(o, "2026-10-09 07:05:00|07:05| 9|7|%|%q|%"));
    cv_tb_strftime(o, sizeof o, "%Y-%m-%d" CV_TB_TIME_FMT, &now); CHECK(!strcmp(o, "2026-10-09 07:05"));
    CHECK_EQ(cv_tb_strftime(o, 5, "%Y-%m-%d", &now), 4); CHECK(!strcmp(o, "2026"));    /* cut, terminated */

    /* number formats: pictures (zeros pad with zeros, '#' with spaces), printf style */
    static const struct { const char* pat; double x; const char* want; } nf[] = {
        { "000.000", 3.14159, "003.142" }, { "###.000", 3.14159, "  3.142" }, { "%8.3f", 3.14159, "   3.142" },
        { "00", 7, "07" }, { "0.00", 1.5, "1.50" }, { "#", 0, "0" }, { "##0", 42, " 42" }, { "#0.0", 0.24, " 0.2" },
        { "0.0", 0.96, "1.0" }, { "0.00", 9.996, "10.00" }, { "00", 123, "123" }, { "##", 12345, "12345" },     /* rounding, overflow */
        { "##0.0", -5, " -5.0" }, { "00.0", -5, "-05.0" }, { "#0", -12, "-12" },                   /* a minus takes a '#' */
        { "0.0", -0.04, "0.0" }, { "000", -0.4, "000" }, { "+0.0", 2, "+2.0" }, { "+0.0", -2, "-2.0" }, { "+00", 0, "+00" },
        { "%d", 2.6, "3" }, { "%3d", -2.4, " -2" }, { "%03d", 7, "007" }, { "%#4d", 3, "   3" }, { "%d", -0.2, "0" },
        { "%+.2e", 12345, "+1.23e+04" }, { "%06.2f", -3.14159, "-03.14" }, { "%-6.1f", 2, "2.0   " }, { "%g", 0.5, "0.5" },
        { "%.3g", 1234.5, "1.23e+03" }, { "% .1f", 1, " 1.0" },
    };
    for (size_t i = 0; i < sizeof nf / sizeof nf[0]; i++) {
        CHECK(cv_tb_num(nf[i].pat, nf[i].x, o, sizeof o));
        if (strcmp(o, nf[i].want)) printf("  num %s %g: '%s', want '%s'\n", nf[i].pat, nf[i].x, o, nf[i].want);
        CHECK(!strcmp(o, nf[i].want));
    }
    static const char* const bad[] = { "", "%s", "%n", "%x", "%lf", "%5.3", "%123f", "%.123f", "%.3d", "%f%s", "%--5f",
                                       "%.20f", "%*d", "0.", ".0", "0.#", "abc", "00 ", "+", "%", "0%" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(!cv_tb_num(bad[i], 1.0, o, sizeof o));
        CHECK(!o[0]);
    }
    CHECK(cv_tb_num("0.000", 1.0 / 0.0, o, sizeof o)); CHECK(!strcmp(o, "inf"));
    CHECK(cv_tb_num("0000", 1, o, 3)); CHECK(!strcmp(o, "00"));          /* cut, terminated */
    CHECK(cv_tb_num("%40.17f", -1e300, o, sizeof o));                   /* the widest printf there is: no overflow */

    /* a picture that holds every value at one width */
    static const struct { double x[4]; int n, sig; const char* want; } fit[] = {
        { { 0.1, 0.2, 1.0 }, 3, 6, "0.0" }, { { 0.5, 12.25 }, 2, 6, "#0.00" }, { { 1, 2, 10 }, 3, 10, "#0" },
        { { -1.5, 2 }, 2, 6, "#0.0" }, { { 1e-9, 1 }, 2, 6, "%.5e" }, { { 1e12 }, 1, 6, "%.5e" }, { { 0 }, 1, 6, "0" },
        { { 0 }, 0, 6, "0" }, { { 1.0f / 3.0f }, 1, 6, "0.000000" }, { { 123, 7 }, 2, 10, "##0" }, { { 1.5 }, 1, 3, "0.0" },
    };
    for (size_t i = 0; i < sizeof fit / sizeof fit[0]; i++) {
        char pat[32];
        cv_tb_fit(fit[i].x, fit[i].n, fit[i].sig, pat, sizeof pat);
        if (strcmp(pat, fit[i].want)) printf("  fit %zu: '%s', want '%s'\n", i, pat, fit[i].want);
        CHECK(!strcmp(pat, fit[i].want));
    }

    /* in placeholders: the template's format, else the value's own, in place of its first number */
    const cv_tb_kv nkv[] = {
        { "time", "0.5", true, 0.5, "0.000" }, { "scale", "x1.5 (auto)", true, 1.5, NULL },
        { "step", "1, increment 2", true, 1, "##" }, { "file", "plate.frd" }, { "freq", "", true, 0, "0.0" },
        { "v", "f = -2.5 Hz", true, -2.5, NULL }, { "word", "undeformed", true, 1, NULL },
    };
    cv_tb_ctx nc = { nkv, 7, NULL, &now, &file };
    static const char* const nt[][2] = {
        { "{time}", "0.500" }, { "{time:00.0}", "00.5" }, { "{time:%6.2f}", "  0.50" }, { "{time:bad}", "{time:bad}" },
        { "{time:%s}", "{time:%s}" }, { "{scale}", "x1.5 (auto)" }, { "{scale:0.00}", "x1.50 (auto)" },
        { "{step}", " 1, increment 2" }, { "{step:00}", "01, increment 2" }, { "{file:0.00}", "plate.frd" },
        { "{v:0.00}", "f = -2.50 Hz" }, { "{word:0.0}", "undeformed" }, { "{date:%Y}", "2026" },
    };
    for (size_t i = 0; i < sizeof nt / sizeof nt[0]; i++) {
        CHECK(tb_line(&nc, nt[i][0], o, sizeof o, &sp));
        if (strcmp(o, nt[i][1])) printf("  line %s: '%s', want '%s'\n", nt[i][0], o, nt[i][1]);
        CHECK(!strcmp(o, nt[i][1]));
    }
    CHECK(!tb_line(&nc, "Freq: {freq:00.0}", o, sizeof o, &sp));          /* no value: the line still goes */

    /* very long: cut, terminated, the line kept */
    char longl[2000];
    memset(longl, 'x', sizeof longl - 1); longl[sizeof longl - 1] = 0;
    memcpy(longl, "L: {file}", 9);
    CHECK(tb_line(&c, longl, o, sizeof o, &sp));
    CHECK_EQ(strlen(o), sizeof o - 1); CHECK_EQ(sp, 1);
    char tiny[3];
    CHECK(tb_line(&c, "Ab: x", tiny, sizeof tiny, &sp)); CHECK_EQ(sp, -1);       /* the ": " is cut off */

    /* literal text: braces doubled, never half a pair */
    cv_tb_literal("a{b}", o, sizeof o); CHECK(!strcmp(o, "a{{b}"));
    cv_tb_literal("ab{", o, 4); CHECK(!strcmp(o, "ab"));
    CHECK(tb_line(&c, "a{{b}", o, sizeof o, &sp)); CHECK(!strcmp(o, "a{b}"));

    /* the ini: round trip, ends kept, a stray backslash as typed */
    const char* t = " Project: X\nPath: C:\\dir\\new\tend ";
    char e[256], u[256];
    cv_tb_escape(t, e, sizeof e);
    CHECK(!strchr(e, '\n')); CHECK(e[0] == '\\' && e[1] == 's'); CHECK(e[strlen(e) - 1] == 's');
    cv_tb_unescape(e, u, sizeof u); CHECK(!strcmp(u, t));
    cv_tb_unescape("A\\nB\\x\\", u, sizeof u); CHECK(!strcmp(u, "A\nB\\x\\"));
    cv_tb_escape("a\r\nb", e, sizeof e); CHECK(!strcmp(e, "a\\nb"));
    cv_tb_escape("\n\n", e, 4); CHECK(!strcmp(e, "\\n"));                         /* never half an escape */

    /* cutting: no space at either side, no escape split */
    CHECK_EQ(cv_tb_cut("short", 10), 5);
    CHECK_EQ(cv_tb_cut("abc def", 4), 2);             /* "ab" | "c def": "abc " would end with a space */
    CHECK_EQ(cv_tb_cut("ab\\ncd", 3), 2);             /* not between \ and n */
    CHECK_EQ(cv_tb_cut("a\\\\bc", 3), 3);             /* after an escaped backslash: fine */
    CHECK_EQ(cv_tb_cut("      x", 3), 3);             /* only spaces: cut anyway */
    const char* lt = "word word word word word word word word";
    char joined[64] = "";
    for (const char* p = lt; *p;) {
        size_t m = cv_tb_cut(p, 7);
        CHECK(p[0] != ' ' && p[m - 1] != ' ');
        strncat(joined, p, m);
        p += m;
    }
    CHECK(!strcmp(joined, lt));
}

#endif
