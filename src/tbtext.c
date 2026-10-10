/* tbtext.c -- the title block's template: placeholders, dates, the ini escaping; see tbtext.h. */
#include "tbtext.h"
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

/* ---- placeholders --------------------------------------------------------------- */

/* name ("key" or "key:format") -> its value; false when it is no placeholder we know */
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
    if (colon) return false;
    for (int i = 0; i < c->n; i++)
        if (!strcmp(c->kv[i].key, name)) {
            obuf b = { v, vn, 0 };
            for (const char* s = c->kv[i].val ? c->kv[i].val : ""; *s; s++)
                put(&b, (unsigned char)*s < ' ' ? ' ' : *s);     /* one row: no line breaks from a value */
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
