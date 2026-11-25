/* tbtext.c -- the title block's template: placeholders, dates, number formats, the ini escaping; see tbtext.h. */
#include "tbtext.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* const cv_tb_date_fmt[CV_TB_DATE_N] = {
    "%Y-%m-%d", "%d.%m.%Y", "%d/%m/%Y", "%m/%d/%Y", "%-d %b %Y", "%B %-d, %Y",
};

/* a bounded output: always terminated, the rest cut */
typedef struct { char* s; size_t n, o; } obuf;

static void put(obuf* b, char c) {
    if (b->o + 1 >= b->n) return;
    b->s[b->o++] = c;
    b->s[b->o] = 0;
}
static void puts_n(obuf* b, const char* t, size_t m) { for (size_t i = 0; i < m && t[i]; i++) put(b, t[i]); }

/* ---- dates -------------------------------------------------------------------- */

/* one conversion the C library does the same everywhere (C89) */
static bool plain_conv(char c) { return c && strchr("aAbBcdHIjmMpSUwWxXyYZ", c); }

/* conversion c of t into out; false when it is none we know */
static bool conv(char c, const struct tm* t, char* out, size_t n) {
    out[0] = 0;
    const char* compound = c == 'F' ? "%Y-%m-%d" : c == 'T' ? "%H:%M:%S" : c == 'R' ? "%H:%M" : c == 'D' ? "%m/%d/%y" : NULL;
    if (compound) { cv_tb_strftime(out, n, compound, t); return true; }
    if (c == 'e') {                                     /* the day, space-padded */
        if (!conv('d', t, out, n)) return false;
        if (out[0] == '0') out[0] = ' ';
        return true;
    }
    if (!plain_conv(c)) return false;
    char f[3] = { '%', c, 0 };
    if (!strftime(out, n, f, t)) out[0] = 0;
    return true;
}

size_t cv_tb_strftime(char* out, size_t n, const char* fmt, const struct tm* t) {
    obuf b = { out, n, 0 };
    if (n) out[0] = 0;
    if (!fmt || !t) return 0;
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') { put(&b, *p); continue; }
        if (p[1] == '%') { put(&b, '%'); p++; continue; }
        bool trim = p[1] == '-';
        char c = trim ? p[2] : p[1];
        char tmp[128];
        if (!c || !conv(c, t, tmp, sizeof tmp)) { put(&b, '%'); continue; }    /* the rest as typed */
        const char* v = tmp;
        if (trim) while ((v[0] == '0' || v[0] == ' ') && v[1]) v++;
        puts_n(&b, v, strlen(v));
        p += trim ? 2 : 1;
    }
    return b.o;
}

/* ---- numbers ------------------------------------------------------------------ */

/* a picture: '+'? then '0' / '#' (at least one), then '.' and '0's */
static bool picture(const char* p, bool* plus, int* w, int* z, int* d) {
    *plus = *p == '+';
    if (*plus) p++;
    *w = *z = *d = 0;
    for (; *p == '0' || *p == '#'; p++) { ++*w; *z += *p == '0'; }
    if (!*w || *w > 40) return false;
    if (*p == '.') {
        for (p++; *p == '0'; p++) ++*d;
        if (!*d || *d > 17) return false;
    }
    return !*p;
}

/* up to two digits at *p into *v; false for three */
static bool two_digits(const char** p, int* v) {
    *v = 0;
    for (int k = 0; isdigit((unsigned char)**p); ++*p, k++) {
        if (k == 2) return false;
        *v = *v * 10 + **p - '0';
    }
    return true;
}

/* a printf conversion we take, rebuilt into f from its parts (never the text as
   typed): %[-+ 0#][width][.precision] and f, e, g or d; d becomes f without decimals */
static bool printf_fmt(const char* p, char* f, size_t fn, bool* whole) {
    if (*p++ != '%') return false;
    char flags[6] = "";
    int nf = 0, w = -1, pr = -1;
    while (*p && strchr("-+ 0#", *p)) {
        if (strchr(flags, *p)) return false;
        flags[nf++] = *p++;
        flags[nf] = 0;
    }
    if (isdigit((unsigned char)*p) && !two_digits(&p, &w)) return false;
    if (*p == '.') {
        p++;
        if (!two_digits(&p, &pr) || pr > 17) return false;
    }
    char c = *p ? *p++ : 0;
    if (!c || !strchr("fegd", c) || *p) return false;
    *whole = c == 'd';
    if (*whole) {
        if (pr >= 0) return false;
        char* h = strchr(flags, '#');                  /* "%#.0f" would write "2." */
        if (h) memmove(h, h + 1, strlen(h));
        pr = 0;
        c = 'f';
    }
    char ws[8] = "", ps[8] = "";
    if (w >= 0) snprintf(ws, sizeof ws, "%d", w);
    if (pr >= 0) snprintf(ps, sizeof ps, ".%d", pr);
    snprintf(f, fn, "%%%s%s%s%c", flags, ws, ps, c);
    return true;
}

bool cv_tb_num(const char* pat, double x, char* out, size_t n) {
    if (n) out[0] = 0;
    if (!pat || !n) return false;
    bool plus, whole;
    int w, z, d;
    char f[24], t[400];
    if (printf_fmt(pat, f, sizeof f, &whole)) {
        if (whole && isfinite(x)) x = floor(x + 0.5);
        if (x == 0) x = 0;                                  /* no "-0" */
        snprintf(t, sizeof t, f, x);
        snprintf(out, n, "%s", t);
        return true;
    }
    if (!picture(pat, &plus, &w, &z, &d)) return false;
    if (!isfinite(x)) { snprintf(out, n, "%s", x != x ? "nan" : x < 0 ? "-inf" : "inf"); return true; }
    snprintf(t, sizeof t, "%.*f", d, fabs(x));
    bool zero = !strpbrk(t, "123456789");
    char sign = x < 0 && !zero ? '-' : plus ? '+' : 0;
    int il = (int)strcspn(t, "."), digits = il > z ? il : z;
    int body = digits + (sign && !plus);                /* a minus takes the place of a '#' */
    obuf b = { out, n, 0 };
    for (int k = body; k < w; k++) put(&b, ' ');
    if (sign) put(&b, sign);
    for (int k = il; k < digits; k++) put(&b, '0');
    puts_n(&b, t, strlen(t));
    return true;
}

/* the decimals x needs when written to sig significant digits; -1: more than 6 */
static int decimals(double x, int sig) {
    char g[64], a[400], b[64];
    snprintf(g, sizeof g, "%.*g", sig, x);
    for (int d = 0; d <= 6; d++) {
        snprintf(a, sizeof a, "%.*f", d, x);
        snprintf(b, sizeof b, "%.*g", sig, strtod(a, NULL));
        if (!strcmp(b, g)) return d;
    }
    return -1;
}

void cv_tb_fit(const double* x, int n, int sig, char* pat, size_t pn) {
    sig = sig < 1 ? 1 : sig > 17 ? 17 : sig;
    int d = 0, il = 1;
    bool neg = false, sci = false;
    for (int i = 0; i < n; i++) {
        if (!isfinite(x[i])) continue;
        int k = decimals(x[i], sig);
        if (k < 0) sci = true;
        else if (k > d) d = k;
        if (x[i] < 0) neg = true;
    }
    char t[400];
    for (int i = 0; i < n && !sci; i++) {
        if (!isfinite(x[i])) continue;
        snprintf(t, sizeof t, "%.*f", d, fabs(x[i]));
        int k = (int)strcspn(t, ".");
        if (k > il) il = k;
    }
    if (il > 9) sci = true;
    if (sci) { snprintf(pat, pn, "%%.%de", sig > 6 ? 5 : sig - 1); return; }
    obuf b = { pat, pn, 0 };
    if (pn) pat[0] = 0;
    for (int k = 0; k < il - 1 + neg; k++) put(&b, '#');
    put(&b, '0');
    if (d) put(&b, '.');
    for (int k = 0; k < d; k++) put(&b, '0');
}

/* ---- placeholders --------------------------------------------------------------- */

/* where the first number in s starts and how long it is (with a sign before it
   when no letter or digit is before that); false when s has none */
static bool first_number(const char* s, size_t* at, size_t* len) {
    size_t i = strcspn(s, "0123456789");
    if (!s[i]) return false;
    size_t b = i, e = i;
    bool point = b && s[b - 1] == '.';
    if (point) b--;
    if (b && (s[b - 1] == '-' || s[b - 1] == '+') && (b == 1 || !isalnum((unsigned char)s[b - 2]))) b--;
    while (isdigit((unsigned char)s[e])) e++;
    if (!point && s[e] == '.' && isdigit((unsigned char)s[e + 1])) { e++; while (isdigit((unsigned char)s[e])) e++; }
    if (s[e] == 'e' || s[e] == 'E') {                  /* an exponent, when digits follow */
        size_t k = e + 1 + (s[e + 1] == '-' || s[e + 1] == '+');
        if (isdigit((unsigned char)s[k])) { e = k; while (isdigit((unsigned char)s[e])) e++; }
    }
    *at = b; *len = e - b;
    return true;
}

/* name ("key" or "key:format") -> its value; false when it is no placeholder we
   know, or its format is none we take (a date's is strftime's, any other a number's) */
static bool lookup(const cv_tb_ctx* c, const char* name, char* v, size_t vn) {
    v[0] = 0;
    const char* colon = strchr(name, ':');
    size_t kl = colon ? (size_t)(colon - name) : strlen(name);
    const char* fmt = colon ? colon + 1 : NULL;
    bool is_date = kl == 4 && !strncmp(name, "date", 4), is_file = kl == 9 && !strncmp(name, "date_file", 9),
         is_now = kl == 8 && !strncmp(name, "time_now", 8);
    if (is_date || is_file || is_now) {
        const struct tm* t = is_file ? c->file : c->now;
        if (!fmt || !fmt[0]) fmt = is_now ? "%H:%M" : c->date_fmt && c->date_fmt[0] ? c->date_fmt : cv_tb_date_fmt[0];
        if (t) cv_tb_strftime(v, vn, fmt, t);
        return true;
    }
    char num[160];
    if (fmt && !cv_tb_num(fmt, 0, num, sizeof num)) return false;
    for (int i = 0; i < c->n; i++) {
        const cv_tb_kv* e = &c->kv[i];
        if (strlen(e->key) != kl || strncmp(e->key, name, kl)) continue;
        const char* s = e->val ? e->val : "";
        const char* pat = fmt ? fmt : e->fmt;
        size_t at = 0, len = 0;
        bool sub = e->num && pat && first_number(s, &at, &len) && cv_tb_num(pat, e->x, num, sizeof num);
        obuf b = { v, vn, 0 };
        for (size_t k = 0; s[k]; k++) {
            if (sub && k == at) { puts_n(&b, num, strlen(num)); k += len - 1; continue; }
            put(&b, (unsigned char)s[k] < ' ' ? ' ' : s[k]);     /* one row: no line breaks from a value */
        }
        return true;
    }
    return false;
}

bool cv_tb_line(const char* line, size_t len, const cv_tb_ctx* c, char* out, size_t n, int* split) {
    obuf b = { out, n, 0 };
    if (n) out[0] = 0;
    *split = -1;
    while (len && (line[len - 1] == '\r' || line[len - 1] == '\n')) len--;
    int ph = 0, full = 0;
    for (size_t i = 0; i < len;) {
        char ch = line[i];
        if (ch == '{' && i + 1 < len && line[i + 1] == '{') { put(&b, '{'); i += 2; continue; }
        if (ch == '{') {
            size_t j = i + 1;
            while (j < len && line[j] != '}' && line[j] != '{') j++;
            char name[96], v[512];
            size_t m = j - i - 1;
            if (j < len && line[j] == '}' && m < sizeof name) {
                memcpy(name, line + i + 1, m); name[m] = 0;
                if (lookup(c, name, v, sizeof v)) {
                    ph++;
                    if (v[0]) full++;
                    puts_n(&b, v, strlen(v));
                    i = j + 1;
                    continue;
                }
            }
            put(&b, '{'); i++;                              /* unknown or unclosed: as typed */
            continue;
        }
        if (ch == ':' && *split < 0 && i + 1 < len && line[i + 1] == ' ') *split = (int)b.o;
        put(&b, (unsigned char)ch < ' ' ? ' ' : ch);
        i++;
    }
    if (*split >= 0 && (b.o <= (size_t)*split || out[*split] != ':')) *split = -1;    /* cut off */
    if (ph && !full) return false;
    for (size_t i = 0; i < b.o; i++) if (out[i] != ' ') return true;
    return false;
}

void cv_tb_literal(const char* in, char* out, size_t n) {
    obuf b = { out, n, 0 };
    if (n) out[0] = 0;
    for (; *in; in++) {
        if (*in == '{') {
            if (b.o + 2 >= n) break;                        /* never half a pair */
            put(&b, '{');
        }
        put(&b, *in);
    }
}

/* ---- the ini ------------------------------------------------------------------- */

void cv_tb_escape(const char* in, char* out, size_t n) {
    obuf b = { out, n, 0 };
    if (n) out[0] = 0;
    size_t len = strlen(in);
    for (size_t i = 0; i < len; i++) {
        char c = in[i];
        const char* e = c == '\n' ? "\\n" : c == '\t' ? "\\t" : c == '\\' ? "\\\\" :
                        c == ' ' && (i == 0 || i + 1 == len) ? "\\s" : NULL;
        if (c == '\r') continue;
        if (e) { if (b.o + 3 > n) break; puts_n(&b, e, 2); }
        else put(&b, c);
    }
}

void cv_tb_unescape(const char* in, char* out, size_t n) {
    obuf b = { out, n, 0 };
    if (n) out[0] = 0;
    for (; *in; in++) {
        if (*in != '\\' || !in[1]) { put(&b, *in); continue; }
        char e = in[1];
        char c = e == 'n' ? '\n' : e == 't' ? '\t' : e == 's' ? ' ' : e == '\\' ? '\\' : 0;
        if (!c) { put(&b, '\\'); continue; }              /* not ours (a Windows path): as typed */
        put(&b, c);
        in++;
    }
}

size_t cv_tb_cut(const char* s, size_t max) {
    size_t len = strlen(s);
    if (len <= max) return len;
    for (size_t m = max; m >= 1; m--) {
        if (s[m - 1] == ' ' || s[m] == ' ') continue;
        size_t bs = 0;
        while (bs < m && s[m - 1 - bs] == '\\') bs++;
        if (bs % 2) continue;                               /* s[m] is escaped */
        return m;
    }
    return max;                                             /* nothing but spaces: cut anyway */
}
