/* cpal.c -- the user's colour palettes (cpal.h): the list, sampling, the ini
   line, JSON / XML import and JSON export. */
#include "cpal.h"
#include <ctype.h>
#include <stdarg.h>

static cv_pal P[CV_PAL_MAX];
static int np;
static unsigned gen = 1;

int cv_pal_count(void) { return np; }
cv_pal* cv_pal_at(int i) { return i >= 0 && i < np ? &P[i] : NULL; }
unsigned cv_pal_gen(void) { return gen; }
void cv_pal_touch(void) { gen++; }
void cv_pal_clear(void) { np = 0; gen++; }

int cv_pal_find(const char* name) {
    for (int i = 0; name && i < np; i++) if (!strcmp(P[i].name, name)) return i;
    return -1;
}

/* trimmed, no '|' (the ini line's separator), never empty */
static void clean_name(const char* in, char* out, size_t n) {
    while (*in == ' ' || *in == '\t') in++;
    snprintf(out, n, "%s", *in ? in : "palette");
    for (char* c = out; *c; c++) if (*c == '|' || *c == '\n' || *c == '\r') *c = '/';
    size_t l = strlen(out);
    while (l > 1 && (out[l - 1] == ' ' || out[l - 1] == '\t')) out[--l] = 0;
}

int cv_pal_add(const cv_pal* p) {
    if (!p || p->n < 2) return -1;
    cv_pal c = *p;
    if (c.n > CV_PAL_COLS) c.n = CV_PAL_COLS;
    clean_name(p->name, c.name, sizeof c.name);
    int i = cv_pal_find(c.name);
    if (i < 0) {
        if (np >= CV_PAL_MAX) return -1;
        i = np++;
    }
    P[i] = c;
    gen++;
    return i;
}

void cv_pal_remove(int i) {
    if (i < 0 || i >= np) return;
    memmove(&P[i], &P[i + 1], (size_t)(np - i - 1) * sizeof P[0]);
    np--;
    gen++;
}

void cv_pal_move(int i, int to) {
    if (i < 0 || i >= np || to < 0 || to >= np || i == to) return;
    cv_pal t = P[i];
    if (to < i) memmove(&P[to + 1], &P[to], (size_t)(i - to) * sizeof P[0]);
    else memmove(&P[i], &P[i + 1], (size_t)(to - i) * sizeof P[0]);
    P[to] = t;
    gen++;
}

bool cv_pal_rename(int i, const char* name) {
    if (i < 0 || i >= np || !name) return false;
    const char* s = name;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || strchr(s, '|')) return false;
    char c[CV_PAL_NAME];
    clean_name(s, c, sizeof c);
    int k = cv_pal_find(c);
    if (k >= 0 && k != i) return false;
    snprintf(P[i].name, sizeof P[i].name, "%s", c);
    gen++;
    return true;
}

void cv_pal_unique(const char* base, char* out, size_t n) {
    char b[CV_PAL_NAME];
    clean_name(base ? base : "", b, sizeof b);
    snprintf(out, n, "%s", b);
    for (int k = 2; cv_pal_find(out) >= 0 && k < 1000; k++) snprintf(out, n, "%.40s %d", b, k);
}

void cv_pal_sample(const cv_pal* p, float t, float o[3]) {
    if (!p || p->n < 1) { o[0] = o[1] = o[2] = 0.5f; return; }
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    if (!p->blend || p->n < 2) {
        int k = (int)(t * (float)p->n);
        if (k > p->n - 1) k = p->n - 1;
        for (int c = 0; c < 3; c++) o[c] = (float)p->rgb[k][c] / 255.f;
        return;
    }
    float s = t * (float)(p->n - 1);
    int k = (int)s;
    if (k > p->n - 2) k = p->n - 2;
    float f = s - (float)k;
    for (int c = 0; c < 3; c++) o[c] = ((float)p->rgb[k][c] + ((float)p->rgb[k + 1][c] - (float)p->rgb[k][c]) * f) / 255.f;
}

/* ---- one colour, the ini line ------------------------------------------------------ */

static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }

bool cv_pal_color(const char* s, uint8_t rgb[3]) {
    if (!s) return false;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '#') {
        int v[6];
        for (int i = 0; i < 6; i++) if ((v[i] = hexv((unsigned char)s[1 + i])) < 0) return false;
        for (int c = 0; c < 3; c++) rgb[c] = (uint8_t)(v[2 * c] * 16 + v[2 * c + 1]);
        return true;
    }
    for (int c = 0; c < 3; c++) {
        while (isspace((unsigned char)*s)) s++;
        char* e;
        double v = strtod(s, &e);
        if (e == s) return false;
        rgb[c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5);
        s = e;
        while (isspace((unsigned char)*s)) s++;
        if (c < 2) { if (*s != ',' && *s != ';') return false; s++; }
    }
    return true;
}

bool cv_pal_parse(const char* line, cv_pal* p) {
    memset(p->rgb, 0, sizeof p->rgb);
    p->n = 0;
    p->blend = false;
    const char* s = line;
    const char* bar = strchr(s, '|');
    if (bar) {
        while (isspace((unsigned char)*s)) s++;
        p->blend = !strncmp(s, "blend", 5) || !strncmp(s, "smooth", 6);
        s = bar + 1;
    }
    while (*s && p->n < CV_PAL_COLS) {
        while (*s && (isspace((unsigned char)*s) || *s == ';')) s++;
        if (!*s) break;
        char tok[64];
        size_t l = 0;
        while (s[l] && !isspace((unsigned char)s[l]) && s[l] != ';') l++;
        snprintf(tok, sizeof tok, "%.*s", (int)(l < sizeof tok ? l : sizeof tok - 1), s);
        s += l;
        if (cv_pal_color(tok, p->rgb[p->n])) p->n++;
    }
    return p->n >= 2;
}

void cv_pal_format(const cv_pal* p, char* out, size_t n) {
    size_t o = (size_t)snprintf(out, n, "%s|", p->blend ? "blend" : "bands");
    for (int i = 0; i < p->n && o < n; i++)
        o += (size_t)snprintf(out + o, n - o, "%s#%02x%02x%02x", i ? " " : "", p->rgb[i][0], p->rgb[i][1], p->rgb[i][2]);
}

/* ---- import --------------------------------------------------------------------- */

static const char* ws(const char* p) { while (*p && (isspace((unsigned char)*p) || *p == ',')) p++; return p; }

/* a JSON string at p (on its quote) into out; the char after it, NULL if unterminated */
static const char* jstr(const char* p, char* out, size_t n) {
    size_t o = 0;
    for (p++; *p && *p != '"'; p++) {
        char c = *p;
        if (c == '\\' && p[1]) { p++; c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p; }
        if (o + 1 < n) out[o++] = c;
    }
    if (n) out[o] = 0;
    return *p ? p + 1 : NULL;
}

/* past one JSON value of any kind */
static const char* jskip(const char* p) {
    int depth = 0;
    char tmp[2];
    do {
        p = ws(p);
        if (!*p) return p;
        if (*p == '"') { p = jstr(p, tmp, sizeof tmp); if (!p) return ""; }
        else if (*p == '{' || *p == '[') { depth++; p++; }
        else if (*p == '}' || *p == ']') { depth--; p++; }
        else if (*p == ':') p++;
        else while (*p && !strchr(",}] \t\r\n", *p)) p++;
    } while (depth > 0);
    return p;
}

static bool in_list(const char* name, char (*names)[CV_PAL_NAME], int n) {
    for (int i = 0; i < n; i++) if (!strcmp(names[i], name)) return true;
    return false;
}

static int import_json(const char* text, int* bad) {
    enum { NB = 64 };
    static char blended[NB][CV_PAL_NAME];
    int nb = 0, added = 0;
    const char* b = strstr(text, "\"Blended\"");             /* ours: which palettes blend */
    if (b && (b = strchr(b, '[')) != NULL) {
        for (b = ws(b + 1); *b == '"' && nb < NB; b = ws(b)) { b = jstr(b, blended[nb], sizeof blended[0]); nb++; if (!b) break; }
    }
    const char* p = strstr(text, "\"UserDefinedPalettes\"");
    p = p ? strchr(p, '{') : strchr(text, '{');
    if (!p) return 0;
    for (p = ws(p + 1); p && *p && *p != '}'; p = ws(p)) {
        cv_pal pal;
        memset(&pal, 0, sizeof pal);
        if (*p != '"') { p = jskip(p); continue; }
        p = jstr(p, pal.name, sizeof pal.name);
        if (!p) break;
        p = ws(p);
        if (*p != ':') continue;
        p = ws(p + 1);
        if (*p != '[' || !strcmp(pal.name, "Blended")) { p = jskip(p); continue; }
        for (p = ws(p + 1); *p && *p != ']'; p = ws(p)) {
            char c[96] = "";
            if (*p == '"') { p = jstr(p, c, sizeof c); if (!p) return added; }
            else if (*p == '[') {                            /* [r, g, b(, a)] */
                const char* e = strchr(p, ']');
                if (!e) return added;
                snprintf(c, sizeof c, "%.*s", (int)CV_MIN((size_t)(e - p - 1), sizeof c - 1), p + 1);
                p = e + 1;
            } else { p = jskip(p); if (bad) (*bad)++; continue; }
            if (pal.n < CV_PAL_COLS && cv_pal_color(c, pal.rgb[pal.n])) pal.n++;
            else if (bad) (*bad)++;
        }
        if (*p == ']') p++;
        pal.blend = in_list(pal.name, blended, nb);
        if (cv_pal_add(&pal) >= 0) added++;
    }
    return added;
}

/* <UserDefinedPalettes> <name value="r,g,b,a,label"/> ... </UserDefinedPalettes>: the
   colours of one palette under one tag, in order */
static int import_xml(const char* text, int* bad) {
    const char* p = strstr(text, "<UserDefinedPalettes");
    if (!p || !(p = strchr(p, '>')) || p[-1] == '/') return 0;
    const char* end = strstr(p, "</UserDefinedPalettes");
    if (!end) end = p + strlen(p);
    int added = 0;
    cv_pal pal;
    memset(&pal, 0, sizeof pal);
    for (p++; p < end && (p = strchr(p, '<')) != NULL && p < end; ) {
        p++;
        const char* gt = strchr(p, '>');
        if (!gt || gt > end) break;
        if (*p == '/' || *p == '!' || *p == '?') { p = gt + 1; continue; }
        size_t l = 0;
        while (p + l < gt && !isspace((unsigned char)p[l]) && p[l] != '/') l++;
        char tag[CV_PAL_NAME];
        snprintf(tag, sizeof tag, "%.*s", (int)CV_MIN(l, sizeof tag - 1), p);
        const char* v = strstr(p, "value=");
        char c[96] = "";
        if (v && v < gt && (v[6] == '"' || v[6] == '\'')) {
            const char* q = strchr(v + 7, v[6]);
            if (q && q < gt) snprintf(c, sizeof c, "%.*s", (int)CV_MIN((size_t)(q - v - 7), sizeof c - 1), v + 7);
        }
        p = gt + 1;
        if (!c[0]) continue;
        if (pal.n && strcmp(pal.name, tag)) {
            if (cv_pal_add(&pal) >= 0) added++;
            memset(&pal, 0, sizeof pal);
        }
        snprintf(pal.name, sizeof pal.name, "%s", tag);
        if (pal.n < CV_PAL_COLS && cv_pal_color(c, pal.rgb[pal.n])) pal.n++;
        else if (bad) (*bad)++;
    }
    if (pal.n && cv_pal_add(&pal) >= 0) added++;
    return added;
}

int cv_pal_import(const char* text, int* bad) {
    if (bad) *bad = 0;
    if (!text) return 0;
    const char* x = strstr(text, "<UserDefinedPalettes");
    const char* j = strchr(text, '{');
    if (x && (!j || x < j)) return import_xml(text, bad);
    return import_json(text, bad);
}

/* ---- export --------------------------------------------------------------------- */

typedef struct { char* s; size_t n, cap; } buf;
static void add(buf* b, const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    int k = vsnprintf(NULL, 0, fmt, a);
    va_end(a);
    if (k < 0) return;
    if (b->n + (size_t)k + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 1024;
        while (c < b->n + (size_t)k + 1) c *= 2;
        char* s = realloc(b->s, c);
        if (!s) return;
        b->s = s;
        b->cap = c;
    }
    va_start(a, fmt);
    vsnprintf(b->s + b->n, b->cap - b->n, fmt, a);
    va_end(a);
    b->n += (size_t)k;
}
static void add_jstr(buf* b, const char* s) {
    add(b, "\"");
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') add(b, "\\%c", *s);
        else if ((unsigned char)*s < 32) add(b, " ");
        else add(b, "%c", *s);
    }
    add(b, "\"");
}

char* cv_pal_export_json(int only) {
    buf b = { 0 };
    add(&b, "{\n  \"UserDefinedPalettes\": {");
    bool first = true, any_blend = false;
    for (int i = 0; i < np; i++) {
        if (only >= 0 && i != only) continue;
        add(&b, "%s\n    ", first ? "" : ",");
        first = false;
        add_jstr(&b, P[i].name);
        add(&b, ": [");
        for (int k = 0; k < P[i].n; k++)
            add(&b, "%s\n      \"%d,%d,%d,255\"", k ? "," : "", P[i].rgb[k][0], P[i].rgb[k][1], P[i].rgb[k][2]);
        add(&b, "\n    ]");
        any_blend |= P[i].blend;
    }
    add(&b, "\n  }");
    if (any_blend) {                     /* ours; other readers skip it */
        add(&b, ",\n  \"Blended\": [");
        first = true;
        for (int i = 0; i < np; i++) {
            if ((only >= 0 && i != only) || !P[i].blend) continue;
            add(&b, "%s", first ? "" : ", ");
            first = false;
            add_jstr(&b, P[i].name);
        }
        add(&b, "]");
    }
    add(&b, "\n}\n");
    return b.s;
}
