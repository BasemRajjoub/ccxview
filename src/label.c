/* label.c -- labels on the model, the headless part (label.h). */
#include "label.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- layout ---------------------------------------------------------------------- */

/* the next codepoint of a UTF-8 string, '?' for a bad byte; advances *s */
static unsigned next_cp(const char** s) {
    const unsigned char* p = (const unsigned char*)*s;
    unsigned c = p[0], n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
    if (n) c &= 0x3F >> n;
    for (unsigned i = 1; i <= n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *s += i; return '?'; }
        c = c << 6 | (p[i] & 0x3F);
    }
    *s += n + 1;
    return c;
}

static const cv_label_glyph* glyph(const cv_label_metrics* m, unsigned cp) {
    if ((int)cp < m->first || (int)cp >= m->first + m->n) cp = '?';
    if ((int)cp < m->first || (int)cp >= m->first + m->n) return NULL;
    return &m->g[cp - m->first];
}

static void quad(cv_fvec* v, const float anchor[9], float x, float y, float w, float h, float u0, float v0, float u1, float v1, float pull) {
    if (!cv_reserve(*v, v->n + CV_LABEL_FLOATS)) return;
    float* o = v->a + v->n;
    memcpy(o, anchor, 9 * sizeof *o);
    o[9] = x; o[10] = y; o[11] = w; o[12] = h; o[13] = u0; o[14] = v0; o[15] = u1; o[16] = v1; o[17] = pull;
    v->n += CV_LABEL_FLOATS;
}

void cv_label_leader(const cv_label_metrics* m, const float anchor[9], float x0, float y0, float x1, float y1, float pull, cv_fvec* boxes) {
    float dx = x1 - x0, dy = y1 - y0, u = m->white_u, v = m->white_v;
    if (dx == 0) { quad(boxes, anchor, x0 - 0.5f, fminf(y0, y1), 1.f, fabsf(dy), u, v, u, v, pull); return; }
    if (dy == 0) { quad(boxes, anchor, fminf(x0, x1), y0 - 0.5f, fabsf(dx), 1.f, u, v, u, v, pull); return; }
    bool steep = fabsf(dy) > fabsf(dx);
    int n = (int)ceilf(steep ? fabsf(dx) : fabsf(dy));          /* a box per pixel across the slant */
    if (n > 4096) n = 4096;
    for (int k = 0; k < n; k++) {
        float t0 = (float)k / n, t1 = (float)(k + 1) / n, tm = 0.5f * (t0 + t1);
        if (steep) {                                              /* a column: the y run of this step */
            float a = y0 + t0 * dy, b = y0 + t1 * dy;
            quad(boxes, anchor, x0 + tm * dx - 0.5f, fminf(a, b), 1.f, fabsf(b - a), u, v, u, v, pull);
        } else {                                                  /* a row: the x run */
            float a = x0 + t0 * dx, b = x0 + t1 * dx;
            quad(boxes, anchor, fminf(a, b), y0 + tm * dy - 0.5f, fabsf(b - a), 1.f, u, v, u, v, pull);
        }
    }
}

void cv_label_nearest(float x, float y, float w, float h, float px, float py, float* qx, float* qy) {
    *qx = px < x ? x : px > x + w ? x + w : px;
    *qy = py < y ? y : py > y + h ? y + h : py;
}

/* ---- moved labels ---------------------------------------------------------------- */

/* the last two space-separated numbers of s[0 .. len), and where the first starts */
static bool two_numbers(const char* s, size_t len, float* a, float* b, size_t* start) {
    size_t e = len;
    float v[2];
    for (int k = 1; k >= 0; k--) {
        while (e > 0 && s[e - 1] == ' ') e--;
        size_t b0 = e;
        while (b0 > 0 && s[b0 - 1] != ' ') b0--;
        if (b0 == e) return false;
        char t[32], *end;
        if (e - b0 >= sizeof t) return false;
        memcpy(t, s + b0, e - b0); t[e - b0] = 0;
        v[k] = strtof(t, &end);
        if (*end || v[k] != v[k] || isinf(v[k])) return false;
        e = b0;
    }
    *a = v[0]; *b = v[1]; *start = e;
    return true;
}

bool cv_label_off_parse(const char* s, cv_label_off* o) {
    memset(o, 0, sizeof *o);
    while (*s == ' ') s++;
    size_t k = strcspn(s, " ");
    if (!k || k >= sizeof o->kind) return false;
    memcpy(o->kind, s, k);
    const char* id = s + k;
    while (*id == ' ') id++;
    size_t len = strlen(id), at;
    while (len && (id[len - 1] == '\n' || id[len - 1] == '\r')) len--;
    if (!two_numbers(id, len, &o->dx, &o->dy, &at)) return false;
    while (at && id[at - 1] == ' ') at--;
    if (!at || at >= sizeof o->id) return false;
    memcpy(o->id, id, at);
    return true;
}

void cv_label_off_format(const cv_label_off* o, char* out, size_t n) {
    snprintf(out, n, "%s %s %g %g", o->kind, o->id, roundf(o->dx * 10) / 10, roundf(o->dy * 10) / 10);
}

bool cv_label_off_parse_arg(const char* s, cv_label_off* o) {
    memset(o, 0, sizeof *o);
    const char* c1 = strchr(s, ':'), *c2 = strrchr(s, ':');
    if (!c1 || c2 == c1 || c1 == s || c2 == c1 + 1 || (size_t)(c1 - s) >= sizeof o->kind || (size_t)(c2 - c1 - 1) >= sizeof o->id) return false;
    char* e;
    o->dx = strtof(c2 + 1, &e);
    if (e == c2 + 1 || *e != ',') return false;
    const char* b = e + 1;
    o->dy = strtof(b, &e);
    if (e == b || *e || o->dx != o->dx || o->dy != o->dy || isinf(o->dx) || isinf(o->dy)) return false;
    memcpy(o->kind, s, (size_t)(c1 - s));
    memcpy(o->id, c1 + 1, (size_t)(c2 - c1 - 1));
    return true;
}

int cv_label_hit(const float* box, uint32_t n, float x, float y) {
    for (uint32_t i = n; i-- > 0;) {
        const float* b = box + 4 * (size_t)i;
        if (x >= b[0] && y >= b[1] && x < b[0] + b[2] && y < b[1] + b[3]) return (int)i;
    }
    return -1;
}

float cv_label_width(const cv_label_metrics* m, const char* text) {
    float w = 0;
    for (const char* s = text; *s;) { const cv_label_glyph* g = glyph(m, next_cp(&s)); if (g) w += g->xadvance; }
    return w;
}

float cv_label_layout(const cv_label_metrics* m, const float anchor[9], const char* text,
                      float dx, float dy, bool box, float pad, float pull, cv_fvec* gly, cv_fvec* boxes) {
    float w = cv_label_width(m, text);
    if (box) quad(boxes, anchor, dx - pad, dy - pad, w + 2 * pad, m->height + 2 * pad, m->white_u, m->white_v, m->white_u, m->white_v, pull);
    float pen = dx;
    for (const char* s = text; *s;) {
        const cv_label_glyph* g = glyph(m, next_cp(&s));
        if (!g) continue;
        if (g->x1 > g->x0 && g->y1 > g->y0)
            quad(gly, anchor, pen + g->x0, dy + g->y0, g->x1 - g->x0, g->y1 - g->y0, g->u0, g->v0, g->u1, g->v1, pull);
        pen += g->xadvance;
    }
    return w;
}

/* ---- thinning ---------------------------------------------------------------------- */

/* radix sort of the points by depth (0 .. 1 as a 32-bit key), two 16-bit passes */
static void sort_by_depth(cv_label_pt* p, uint32_t n, cv_label_pt* tmp) {
    uint32_t* key = malloc(n * sizeof *key);
    if (!key) return;
    static uint32_t cnt[65536];
    for (uint32_t i = 0; i < n; i++) key[i] = (uint32_t)(fminf(fmaxf(p[i].depth, 0.f), 1.f) * 4294967295.0);
    for (int pass = 0; pass < 2; pass++) {
        int sh = pass * 16;
        memset(cnt, 0, sizeof cnt);
        for (uint32_t i = 0; i < n; i++) cnt[key[i] >> sh & 0xFFFF]++;
        uint32_t sum = 0;
        for (int b = 0; b < 65536; b++) { uint32_t c = cnt[b]; cnt[b] = sum; sum += c; }
        for (uint32_t i = 0; i < n; i++) tmp[cnt[key[i] >> sh & 0xFFFF]++] = p[i];
        /* the keys follow the points: recompute (cheap) rather than carry them */
        memcpy(p, tmp, n * sizeof *p);
        for (uint32_t i = 0; i < n; i++) key[i] = (uint32_t)(fminf(fmaxf(p[i].depth, 0.f), 1.f) * 4294967295.0);
    }
    free(key);
}

/* an open-addressing table of the cells that hold a chosen point: cell -> first chosen
   point in it, chained through next[] */
typedef struct { uint64_t* cell; uint32_t* head; uint32_t cap; } grid;

static uint32_t cell_slot(const grid* g, uint64_t key) {
    uint32_t h = (uint32_t)(key * 0x9E3779B97F4A7C15ull >> 33) & (g->cap - 1);
    while (g->cell[h] && g->cell[h] != key) h = (h + 1) & (g->cap - 1);
    return h;
}

uint32_t cv_label_thin(cv_label_pt* pts, uint32_t n, uint32_t n_pin, float dxs, float dys, float vx, float vy, float vw, float vh,
                       uint32_t* out, uint32_t max_out) {
    float spacing = dxs > dys ? dxs : dys;       /* the grid cell: the larger reach */
    /* inside the viewport and the depth range: the rest to the back of the array; the
       pinned stay in front, in their order */
    uint32_t m = 0, mp = 0;
    for (uint32_t i = 0; i < n; i++) {
        const cv_label_pt* p = &pts[i];
        if (p->depth < 0.f || p->depth > 1.f || p->sx < vx || p->sx >= vx + vw || p->sy < vy || p->sy >= vy + vh) continue;
        pts[m++] = *p;
        if (i < n_pin) mp = m;
    }
    if (!m) return 0;
    cv_label_pt* tmp = malloc(m * sizeof *tmp);
    if (tmp) { sort_by_depth(pts + mp, m - mp, tmp); free(tmp); }
    if (spacing <= 0.f) {
        uint32_t k = m < max_out ? m : max_out;
        for (uint32_t i = 0; i < k; i++) out[i] = pts[i].id;
        return k;
    }
    grid g = {0};
    g.cap = 1024;
    while (g.cap < 2 * m) g.cap *= 2;
    g.cell = calloc(g.cap, sizeof *g.cell);
    g.head = malloc(g.cap * sizeof *g.head);
    uint32_t* next = malloc(m * sizeof *next);
    uint32_t taken = 0;
    if (!g.cell || !g.head || !next) { free(g.cell); free(g.head); free(next); return 0; }
    for (uint32_t i = 0; i < m && taken < max_out; i++) {
        const cv_label_pt* p = &pts[i];
        int64_t cx = (int64_t)floorf(p->sx / spacing), cy = (int64_t)floorf(p->sy / spacing);
        bool clash = false;
        for (int64_t ddy = -1; ddy <= 1 && !clash; ddy++)
            for (int64_t ddx = -1; ddx <= 1 && !clash; ddx++) {
                uint64_t key = ((uint64_t)(cx + ddx + 0x80000000ll) << 32 | (uint32_t)(cy + ddy + 0x80000000ll)) | 1;
                uint32_t h = cell_slot(&g, key);
                if (!g.cell[h]) continue;
                for (uint32_t j = g.head[h]; j != UINT32_MAX; j = next[j]) {
                    float dx = pts[j].sx - p->sx, dy = pts[j].sy - p->sy;
                    if (fabsf(dx) < dxs && fabsf(dy) < dys) { clash = true; break; }   /* the boxes would overlap */
                }
            }
        if (clash && i >= mp) continue;              /* a pinned one goes in whatever it overlaps */
        uint64_t key = ((uint64_t)(cx + 0x80000000ll) << 32 | (uint32_t)(cy + 0x80000000ll)) | 1;
        uint32_t h = cell_slot(&g, key);
        if (!g.cell[h]) { g.cell[h] = key; g.head[h] = UINT32_MAX; }
        next[i] = g.head[h]; g.head[h] = i;
        out[taken++] = p->id;
    }
    free(g.cell); free(g.head); free(next);
    return taken;
}

/* ---- the coarse pass ------------------------------------------------------------- */

uint32_t cv_label_coarse(const float* xyz, uint32_t n, float cell, uint32_t* out) {
    if (!(cell > 0.f)) { for (uint32_t i = 0; i < n; i++) out[i] = i; return n; }
    uint32_t cap = 1024;
    while (cap < 2 * n) cap *= 2;
    uint64_t* seen = calloc(cap, sizeof *seen);
    if (!seen) return 0;
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) {
        const float* p = xyz + 3 * i;
        uint64_t cx = (uint64_t)((int64_t)floorf(p[0] / cell) & 0x1FFFFF), cy = (uint64_t)((int64_t)floorf(p[1] / cell) & 0x1FFFFF),
                 cz = (uint64_t)((int64_t)floorf(p[2] / cell) & 0x1FFFFF);
        uint64_t key = (cx << 42 | cy << 21 | cz) | 1ull << 63;
        uint32_t h = (uint32_t)(key * 0x9E3779B97F4A7C15ull >> 33) & (cap - 1);
        while (seen[h] && seen[h] != key) h = (h + 1) & (cap - 1);
        if (seen[h]) continue;
        seen[h] = key;
        out[k++] = i;
    }
    free(seen);
    return k;
}

/* a bounded sorted list: x goes in at its rank when it beats the last (ascending when
   asc, else descending); the usual value fails the first test, so a pass is O(n) */
static void rank_in(float* key, uint32_t* idx, uint32_t* m, uint32_t k, float x, uint32_t i, bool asc) {
    if (*m == k && (asc ? x >= key[k - 1] : x <= key[k - 1])) return;
    uint32_t at = *m < k ? *m : k - 1;
    while (at > 0 && (asc ? x < key[at - 1] : x > key[at - 1])) { key[at] = key[at - 1]; idx[at] = idx[at - 1]; at--; }
    key[at] = x; idx[at] = i;
    if (*m < k) (*m)++;
}

uint32_t cv_label_extremes(const float* v, const uint32_t* ids, uint32_t n, uint32_t k, uint32_t* lo, uint32_t* hi) {
    if (!k) return 0;
    float* kl = malloc(2 * k * sizeof *kl);
    if (!kl) return 0;
    float* kh = kl + k;
    uint32_t ml = 0, mh = 0;
    for (uint32_t j = 0; j < n; j++) {
        uint32_t i = ids ? ids[j] : j;
        float x = v[i];
        if (x != x || isinf(x)) continue;
        rank_in(kl, lo, &ml, k, x, i, true);
        rank_in(kh, hi, &mh, k, x, i, false);
    }
    free(kl);
    return ml;
}
