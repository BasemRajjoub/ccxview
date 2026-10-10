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
