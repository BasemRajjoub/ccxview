/* report_pdf.c -- the model summary report as PDF (report.h): the Markdown that
   cv_report_md writes, laid out on A4 pages. Only what that Markdown holds is
   understood: # ## ### headings, paragraphs, and pipe tables (wrapped cells, the
   header row again on each page a table runs onto). PDFGen draws, in the 14
   standard PDF fonts: text outside their Latin-1 / WinAnsi set shows as '?'. */
#if !defined(_POSIX_C_SOURCE) && !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L     /* strdup, fileno (PDFGen), also where the build sets none (wasm) */
#endif
#if defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1    /* PDFGen writes stream lengths with %zu; msvcrt's printf has no z */
#endif
#include "report.h"
#include <string.h>
#include <stdlib.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4996 4090)
#endif
/* pdfgen's endian test reads `__BYTE_ORDER == __BIG_ENDIAN`, which with neither
   defined (gcc on mingw) is 0 == 0: big endian, and every PNG chunk length is
   read byte-swapped. Say it first where the compiler knows. */
#if !defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define __LITTLE_ENDIAN__
#endif
#endif
#include "pdfgen.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#define PAGE_W PDF_A4_WIDTH
#define PAGE_H PDF_A4_HEIGHT
static const float ML = 48, MR = 48, MT = 54, MB = 54;   /* margins, points */
static const char* F = "Helvetica";
static const char* FB = "Helvetica-Bold";
#define INK    PDF_RGB(0x1f, 0x23, 0x28)
#define HEAD   PDF_RGB(0x1d, 0x4e, 0x89)
#define GREY   PDF_RGB(0x6b, 0x72, 0x7c)
#define RULE   PDF_RGB(0xd0, 0xd5, 0xdd)
#define FILL   PDF_RGB(0xe9, 0xee, 0xf4)

typedef struct {
    struct pdf_doc* pdf;
    float y;                        /* the next line's top, from the page bottom */
} R;

static void page(R* r) { pdf_append_page(r->pdf); r->y = PAGE_H - MT; }
static void room(R* r, float h) { if (r->y - h < MB) page(r); }

/* text the standard fonts can show: each UTF-8 character outside WinAnsi becomes '?' */
static void clean(const char* s, size_t n, char* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && s[i] && o + 4 < cap;) {
        uint32_t cp;
        int k = utf8_to_utf32(s + i, (int)(n - i), &cp);
        bool ok = k > 0 && cp >= 32 && cp != 127 && !(cp >= 0x80 && cp < 0xa0);
        if (ok && cp > 255) {
            ok = false;
            for (size_t j = 0; j < ARRAY_SIZE(win_ansi_encoding); j++) ok |= win_ansi_encoding[j].codepoint == cp;
        }
        if (k <= 0) k = 1;
        if (ok) { memcpy(out + o, s + i, (size_t)k); o += (size_t)k; }
        else out[o++] = cp == '\t' ? ' ' : '?';
        i += (size_t)k;
    }
    out[o] = 0;
}

static float tw(R* r, const char* s, int n, bool bold, float size) {
    char b[1024];
    if (n < 0) n = (int)strlen(s);
    if (n >= (int)sizeof b) n = (int)sizeof b - 1;
    memcpy(b, s, (size_t)n);
    b[n] = 0;
    float w = 0;
    if (pdf_get_font_text_width(r->pdf, bold ? FB : F, b, size, &w) < 0) { pdf_clear_err(r->pdf); w = n * size * 0.6f; }
    return w;
}

/* s cut into lines no wider than w: at spaces, else inside a word too long for a line */
typedef struct { int a, n; } seg;
enum { MAXL = 48 };
static int wrap(R* r, const char* s, bool bold, float size, float w, seg* out) {
    int len = (int)strlen(s), nl = 0, i = 0;
    while (i < len && nl < MAXL) {
        while (s[i] == ' ') i++;
        if (i >= len) break;
        int end = i, k = i;
        while (k < len) {                                   /* whole words while they fit */
            int we = k;
            while (we < len && s[we] != ' ') we++;
            if (tw(r, s + i, we - i, bold, size) > w) break;
            end = we;
            k = we;
            while (k < len && s[k] == ' ') k++;
        }
        if (end == i) {                                     /* the first word alone is too wide */
            int e = i;
            do { e++; while (e < len && (s[e] & 0xc0) == 0x80) e++; } while (e < len && s[e] != ' ' && tw(r, s + i, e - i, bold, size) <= w);
            if (e - i > 1 && tw(r, s + i, e - i, bold, size) > w) { e--; while (e > i + 1 && (s[e] & 0xc0) == 0x80) e--; }
            end = e;
        }
        int n = end - i;
        while (n > 0 && s[i + n - 1] == ' ') n--;
        out[nl++] = (seg){ i, n };
        i = end;
    }
    if (!nl) out[nl++] = (seg){ 0, 0 };
    return nl;
}

static void text(R* r, const char* s, int n, bool bold, float size, float x, float base, uint32_t col) {
    char b[1024];
    if (n < 0) n = (int)strlen(s);
    if (n >= (int)sizeof b) n = (int)sizeof b - 1;
    memcpy(b, s, (size_t)n);
    b[n] = 0;
    pdf_set_font(r->pdf, bold ? FB : F);
    if (pdf_add_text(r->pdf, NULL, b, size, x, base, col) < 0) pdf_clear_err(r->pdf);
}

/* a block of wrapped text from the current y */
static void para(R* r, const char* s, bool bold, float size, uint32_t col, float after) {
    seg l[MAXL];
    float lh = size * 1.32f, w = PAGE_W - ML - MR;
    int n = wrap(r, s, bold, size, w, l);
    for (int i = 0; i < n; i++) {
        room(r, lh);
        text(r, s + l[i].a, l[i].n, bold, size, ML, r->y - size, col);
        r->y -= lh;
    }
    r->y -= after;
}

/* ---- tables ---- */
enum { MAXC = 16 };
typedef struct { char* c[MAXC]; } row;

/* "| a | b \| c |" -> cells, the \| unescaped; -1 when the line is the |---| rule */
static int cells(const char* line, char** out) {
    const char* p = line + 1;
    bool rule = strchr(p, '-') != NULL;                     /* "| | |" is a row of empty cells, not the rule */
    for (const char* q = p; *q; q++) if (!strchr("|-: \t", *q)) rule = false;
    if (rule) return -1;
    int n = 0;
    while (*p && n < MAXC) {
        const char* e = p;
        while (*e && !(*e == '|' && e[-1] != '\\')) e++;
        if (!*e && n > 0) {                                 /* text after the last pipe: nothing, or a cell without a closing pipe */
            const char* t = p;
            while (*t == ' ' || *t == '\t') t++;
            if (!*t) break;
        }
        char raw[1024], un[1024];
        size_t k = 0;
        for (const char* q = p; q < e && k + 1 < sizeof raw; q++) {
            if (*q == '\\' && q[1] == '|') continue;
            raw[k++] = *q;
        }
        raw[k] = 0;
        char* a = raw;
        while (*a == ' ') a++;
        size_t m = strlen(a);
        while (m && (a[m - 1] == ' ' || a[m - 1] == '\t')) a[--m] = 0;
        clean(a, m, un, sizeof un);
        out[n++] = strdup(un);
        if (!*e) break;
        p = e + 1;
    }
    return n;
}

static const float TS = 8.f, TPX = 4.f, TPY = 3.f;

/* one row across the widths w; hr: the header row (shaded, bold) */
static void table_row(R* r, const row* x, int nc, const float* w, bool hr, bool key) {
    const float lh = TS * 1.3f;
    seg l[MAXC][MAXL];
    int n[MAXC], most = 1;
    float tot = 0;
    for (int c = 0; c < nc; c++) {
        const char* s = x->c[c] ? x->c[c] : "";
        n[c] = wrap(r, s, hr || (key && c == 0), TS, w[c] - 2 * TPX, l[c]);
        if (n[c] > most) most = n[c];
        tot += w[c];
    }
    float h = most * lh + 2 * TPY;
    if (hr) pdf_add_filled_rectangle(r->pdf, NULL, ML, r->y - h, tot, h, 0, FILL, PDF_TRANSPARENT);
    float xx = ML;
    for (int c = 0; c < nc; c++) {
        const char* s = x->c[c] ? x->c[c] : "";
        for (int k = 0; k < n[c]; k++)
            text(r, s + l[c][k].a, l[c][k].n, hr || (key && c == 0), TS, xx + TPX, r->y - TPY - TS * 0.85f - k * lh, INK);
        xx += w[c];
    }
    r->y -= h;
    pdf_add_line(r->pdf, NULL, ML, r->y, ML + tot, r->y, hr ? 0.8f : 0.4f, hr ? GREY : RULE);
}
static float row_h(R* r, const row* x, int nc, const float* w, bool bold0, bool hr) {
    seg l[MAXL];
    int most = 1;
    for (int c = 0; c < nc; c++) {
        int n = wrap(r, x->c[c] ? x->c[c] : "", hr || (bold0 && c == 0), TS, w[c] - 2 * TPX, l);
        if (n > most) most = n;
    }
    return most * TS * 1.3f + 2 * TPY;
}

/* head: rows[0] is the header row, drawn again on each page the table runs onto;
   no header: a list of name | value, the names bold */
static void table(R* r, row* rows, int nr, int nc, bool head) {
    const float avail = PAGE_W - ML - MR;
    float nat[MAXC] = {0}, mn[MAXC] = {0}, w[MAXC];
    for (int i = 0; i < nr; i++) for (int c = 0; c < nc; c++) {
        const char* s = rows[i].c[c] ? rows[i].c[c] : "";
        bool bold = (head && i == 0) || (!head && c == 0);
        float t = tw(r, s, -1, bold, TS) + 2 * TPX;
        if (t > nat[c]) nat[c] = t;
        for (const char* p = s; *p;) {                      /* the longest word */
            while (*p == ' ') p++;
            const char* e = p;
            while (*e && *e != ' ') e++;
            if (e > p) { float ww = tw(r, p, (int)(e - p), bold, TS) + 2 * TPX; if (ww > mn[c]) mn[c] = ww; }
            p = e;
        }
    }
    float sn = 0, sm = 0;
    for (int c = 0; c < nc; c++) { if (mn[c] > avail * 0.4f) mn[c] = avail * 0.4f; if (nat[c] < mn[c]) nat[c] = mn[c]; sn += nat[c]; sm += mn[c]; }
    for (int c = 0; c < nc; c++) {
        if (sn <= avail) w[c] = nat[c] * (sn > avail * 0.55f ? avail / sn : CV_MIN(avail / sn, 1.6f));   /* wide tables span the page, narrow ones get some air */
        else if (sm >= avail) w[c] = mn[c] * avail / sm;
        else w[c] = mn[c] + (nat[c] - mn[c]) * (avail - sm) / (sn - sm);
    }
    for (int i = 0; i < nr; i++) {
        bool hr = head && i == 0;
        float h = row_h(r, &rows[i], nc, w, !head, hr);
        if (hr) h += row_h(r, &rows[1 < nr ? 1 : 0], nc, w, false, false);    /* not a header alone at a page's foot */
        if (r->y - h < MB) {
            page(r);
            if (head && !hr) table_row(r, &rows[0], nc, w, true, false);
        }
        table_row(r, &rows[i], nc, w, hr, !head);
    }
}

/* the table the lines from *i start: rows parsed, drawn, *i past it */
static void table_at(R* r, char** lines, int nl, int* i) {
    int cap = 64, nr = 0, nc = 0;
    row* rows = malloc(cap * sizeof *rows);
    if (!rows) return;
    for (; *i < nl && lines[*i][0] == '|'; (*i)++) {
        row x = {{0}};
        int n = cells(lines[*i], x.c);
        if (n < 0) continue;
        if (nr == cap) { row* g = realloc(rows, (cap *= 2) * sizeof *rows); if (!g) break; rows = g; }
        rows[nr++] = x;
        if (n > nc) nc = n;
    }
    bool head = false;
    for (int c = 0; nr && c < nc; c++) head |= rows[0].c[c] && rows[0].c[c][0];
    if (nr && nc) {
        int skip = head ? 0 : 1;                            /* "| | |" over a two-column list: no header */
        if (nr > skip) { r->y -= 2; table(r, rows + skip, nr - skip, nc, head); r->y -= 10; }
    }
    for (int k = 0; k < nr; k++) for (int c = 0; c < MAXC; c++) free(rows[k].c[c]);
    free(rows);
}

/* "![caption](file)": caption and file cut out of a copy of the line; false: not a picture */
static bool pic_line(char* s, char** cap, char** file) {
    if (s[0] != '!' || s[1] != '[') return false;
    char* e = strstr(s + 2, "](");
    size_t n = strlen(s);
    if (!e || n < 5 || s[n - 1] != ')') return false;
    *e = 0; s[n - 1] = 0;
    *cap = s + 2; *file = e + 2;
    return true;
}

static uint8_t* slurp(const char* path, size_t* n) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    uint8_t* b = NULL;
    long sz = fseek(fp, 0, SEEK_END) == 0 ? ftell(fp) : -1;
    if (sz > 0 && sz < (64L << 20) && fseek(fp, 0, SEEK_SET) == 0 && (b = malloc((size_t)sz)) && fread(b, 1, (size_t)sz, fp) != (size_t)sz) { free(b); b = NULL; }
    fclose(fp);
    *n = b ? (size_t)sz : 0;
    return b;
}

/* a run of picture lines: two to a row (one alone: the full width), each with its
   caption under it, a row moved to the next page when it does not fit */
static void pictures(R* r, char** lines, int nl, int* i, const char* dir) {
    enum { MAXP = 16 };
    struct { char* cap; uint8_t* data; size_t n; float ar; } P[MAXP];
    int np = 0;
    char path[1024], t[512];
    for (; *i < nl && np < MAXP; (*i)++) {
        if (!lines[*i][0]) continue;
        char* cap; char* file;
        if (!pic_line(lines[*i], &cap, &file)) break;
        if (dir && dir[0]) snprintf(path, sizeof path, "%s/%s", dir, file); else snprintf(path, sizeof path, "%s", file);
        P[np].cap = cap; P[np].data = slurp(path, &P[np].n); P[np].ar = 0;
        struct pdf_img_info info;
        char err[128];
        if (P[np].data && pdf_parse_image_header(&info, P[np].data, P[np].n, err, sizeof err) >= 0 && info.width && info.height)
            P[np].ar = (float)info.height / (float)info.width;
        np++;
    }
    const float avail = PAGE_W - ML - MR, gap = 12, capH = 14;
    int per = np == 1 ? 1 : 2;
    float cw = per == 1 ? avail : (avail - gap) / 2;
    for (int k = 0; k < np; k += per) {
        float ih = 0;                                    /* the row's height: its tallest picture */
        for (int c = 0; c < per && k + c < np; c++) {
            float h = P[k + c].ar > 0 ? cw * P[k + c].ar : 20;
            float hmax = (PAGE_H - MT - MB) * (per == 1 ? 0.55f : 0.42f);
            if (h > hmax) h = hmax;
            if (h > ih) ih = h;
        }
        room(r, ih + capH + 6);
        for (int c = 0; c < per && k + c < np; c++) {
            float x = ML + c * (cw + gap), w = cw, h = P[k + c].ar > 0 ? cw * P[k + c].ar : 20;
            if (h > ih) { w = ih / P[k + c].ar; h = ih; }        /* capped: narrower, centred */
            x += (cw - w) / 2;
            clean(P[k + c].cap, strlen(P[k + c].cap), t, sizeof t);
            bool ok = P[k + c].ar > 0 && pdf_add_image_data(r->pdf, NULL, x, r->y - h, w, h, P[k + c].data, P[k + c].n) >= 0;
            if (!ok) {
                pdf_clear_err(r->pdf);
                char m[600];
                snprintf(m, sizeof m, "(picture not available: %s)", t);
                text(r, m, -1, false, 8.5f, ML + c * (cw + gap), r->y - 12, GREY);
            } else pdf_add_rectangle(r->pdf, NULL, x, r->y - h, w, h, 0.4f, RULE);
            float tx = ML + c * (cw + gap) + (cw - tw(r, t, -1, false, 8.5f)) / 2;
            text(r, t, -1, false, 8.5f, tx < ML ? ML : tx, r->y - ih - 10, GREY);
        }
        r->y -= ih + capH + 6;
    }
    for (int k = 0; k < np; k++) free(P[k].data);
}

bool cv_report_pdf(const char* md, const char* path, const char* name, const char* dir) {
    if (!md || !path) return false;
    struct pdf_info info = {0};
    snprintf(info.creator, sizeof info.creator, "ccxview");
    snprintf(info.producer, sizeof info.producer, "ccxview (PDFGen)");
    snprintf(info.title, sizeof info.title, "Model summary%s%s", name && *name ? ": " : "", name ? name : "");
    R r = { pdf_create(PAGE_W, PAGE_H, &info), 0 };
    if (!r.pdf) return false;
    page(&r);

    /* the text as lines */
    char* buf = strdup(md);
    int nl = 0, cap = 256;
    char** lines = malloc(cap * sizeof *lines);
    if (!buf || !lines) { free(buf); free(lines); pdf_destroy(r.pdf); return false; }
    for (char* p = buf; p && *p;) {
        char* e = strchr(p, '\n');
        if (e) *e = 0;
        size_t m = strlen(p);
        if (m && p[m - 1] == '\r') p[m - 1] = 0;
        if (nl == cap) { char** g = realloc(lines, (cap *= 2) * sizeof *lines); if (!g) break; lines = g; }
        lines[nl++] = p;
        p = e ? e + 1 : NULL;
    }

    char t[2048];
    for (int i = 0; i < nl;) {
        const char* s = lines[i];
        if (!s[0]) { i++; continue; }
        if (s[0] == '|') { table_at(&r, lines, nl, &i); continue; }
        if (s[0] == '!' && s[1] == '[') { int i0 = i; pictures(&r, lines, nl, &i, dir); if (i > i0) continue; }
        int h = 0;
        while (s[h] == '#') h++;
        if (h && s[h] == ' ') {
            clean(s + h + 1, strlen(s + h + 1), t, sizeof t);
            if (h == 1) para(&r, t, true, 18, INK, 6);
            else {
                float sz = h == 2 ? 13 : 10.5f;
                if (r.y < PAGE_H - MT - 1) r.y -= h == 2 ? 10 : 4;
                room(&r, 90);                               /* a heading is not left alone at the foot of a page */
                para(&r, t, true, sz, HEAD, h == 2 ? 3 : 2);
                if (h == 2) { pdf_add_line(r.pdf, NULL, ML, r.y + 1, PAGE_W - MR, r.y + 1, 0.6f, RULE); r.y -= 6; }
            }
            i++;
            continue;
        }
        /* a paragraph: the lines up to a blank one, a table or a heading */
        int j = i;
        size_t o = 0;
        t[0] = 0;
        for (; j < nl && lines[j][0] && lines[j][0] != '|' && lines[j][0] != '#'; j++) {
            if (o && o + 1 < sizeof t) t[o++] = ' ';
            clean(lines[j], strlen(lines[j]), t + o, sizeof t - o);
            o = strlen(t);
        }
        int k = j;
        while (k < nl && !lines[k][0]) k++;
        bool caption = k < nl && lines[k][0] == '|';        /* the line over a table names it */
        if (caption) room(&r, 70);
        para(&r, t, caption, caption ? 9.5f : 9, INK, caption ? 3 : 7);
        i = j;
    }

    /* the foot of each page */
    int pages = 0;
    for (int p = 1; pdf_get_page(r.pdf, p); p++) pages++;
    char foot[256], num[48];
    clean(name ? name : "", name ? strlen(name) : 0, t, 200);
    snprintf(foot, sizeof foot, "%s%sccxview model summary", t, t[0] ? " \xc2\xb7 " : "");
    pdf_set_font(r.pdf, F);
    for (int p = 1; p <= pages; p++) {
        struct pdf_object* pg = pdf_get_page(r.pdf, p);
        snprintf(num, sizeof num, "%d / %d", p, pages);
        pdf_add_text(r.pdf, pg, foot, 7.5f, ML, MB - 26, GREY);
        pdf_add_text(r.pdf, pg, num, 7.5f, PAGE_W - MR - tw(&r, num, -1, false, 7.5f), MB - 26, GREY);
    }

    bool ok = pdf_save(r.pdf, path) >= 0;
    pdf_destroy(r.pdf);
    free(lines);
    free(buf);
    return ok;
}
