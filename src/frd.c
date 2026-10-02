/* frd.c -- CalculiX .frd reader: one index pass + on-demand field decode.

   Record keys (first token of a line):
     2C nodes · 3C elements · 1PSTEP step/inc · 100CL result block header
     -4 field header · -5 component · -1 record · -2 continuation · -3 end · 9999 EOF
   Binary blocks are flagged in fixed header columns (2C/3C col 74, 100CL col 75)
   and follow their header line directly as raw little-endian records. */
#include "frd.h"
#include <math.h>
#include <ctype.h>

/* ---- element types ------------------------------------------------------- */

static const struct { int nodes; const char* name; } kTypes[] = {
    {0, "?"}, {8, "Hex8"}, {6, "Wedge6"}, {4, "Tet4"}, {20, "Hex20"}, {15, "Wedge15"},
    {10, "Tet10"}, {3, "Tri3"}, {6, "Tri6"}, {4, "Quad4"}, {8, "Quad8"}, {2, "Line2"},
    {3, "Line3"},
};

int cv_frd_type_nodes(int t) {
    return (t > 0 && t < (int)CV_COUNT(kTypes)) ? kTypes[t].nodes : 0;
}
const char* cv_frd_type_name(int t) {
    return (t > 0 && t < (int)CV_COUNT(kTypes)) ? kTypes[t].name : "?";
}

/* ---- numbers ------------------------------------------------------------- */

static const double kPow10[] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

static inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool cv_parse_num(const char* s, const char* e, double* out) {
    while (s < e && (*s == ' ' || *s == '\t')) s++;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
    if (s >= e) return false;
    bool neg = false;
    if (*s == '-' || *s == '+') { neg = *s == '-'; s++; }
    if (s < e && ((*s | 32) == 'n' || (*s | 32) == 'i')) {        /* NaN / Inf */
        if (e - s >= 3 && (s[0] | 32) == 'n' && (s[1] | 32) == 'a' && (s[2] | 32) == 'n') {
            *out = NAN; return true;
        }
        if (e - s >= 3 && (s[0] | 32) == 'i' && (s[1] | 32) == 'n' && (s[2] | 32) == 'f') {
            *out = neg ? -INFINITY : INFINITY; return true;
        }
        return false;
    }
    uint64_t mant = 0;
    int digits = 0, exp10 = 0;
    bool any = false;
    for (; s < e && is_digit(*s); s++, any = true) {
        if (digits < 19) { mant = mant * 10 + (uint64_t)(*s - '0'); if (mant) digits++; }
        else exp10++;
    }
    if (s < e && *s == '.') {
        for (s++; s < e && is_digit(*s); s++, any = true) {
            if (digits < 19) { mant = mant * 10 + (uint64_t)(*s - '0'); if (mant) digits++; exp10--; }
        }
    }
    if (!any) return false;
    if (s < e) {
        /* 1.0E+05, 1.0D+05, and the Fortran short form 1.0-100 */
        if ((*s | 32) == 'e' || (*s | 32) == 'd') s++;
        else if (*s != '+' && *s != '-') return false;
        bool eneg = false;
        if (s < e && (*s == '+' || *s == '-')) { eneg = *s == '-'; s++; }
        if (s >= e) return false;
        int ex = 0;
        for (; s < e && is_digit(*s); s++) if (ex < 10000) ex = ex * 10 + (*s - '0');
        if (s != e) return false;
        exp10 += eneg ? -ex : ex;
    }
    double v = (double)mant;
    if (v != 0.0) {
        if (exp10 >= 0 && exp10 <= 22)       v *= kPow10[exp10];
        else if (exp10 < 0 && exp10 >= -22)  v /= kPow10[-exp10];
        else                                 v *= pow(10.0, exp10);
    }
    *out = neg ? -v : v;
    return true;
}

/* ---- line cursor + tokens ------------------------------------------------ */

typedef struct {
    const char* base;
    const char* p;
    const char* end;
    uint64_t    line;
} cursor;

static bool next_line(cursor* c, const char** ls, const char** le) {
    if (c->p >= c->end) return false;
    const char* nl = memchr(c->p, '\n', (size_t)(c->end - c->p));
    *ls = c->p;
    *le = nl ? nl : c->end;
    c->p = nl ? nl + 1 : c->end;
    if (*le > *ls && (*le)[-1] == '\r') (*le)--;
    c->line++;
    return true;
}

static int tokens(const char* s, const char* e, const char** tb, const char** te, int max) {
    int n = 0;
    while (n < max) {
        while (s < e && (*s == ' ' || *s == '\t')) s++;
        if (s >= e) break;
        tb[n] = s;
        while (s < e && *s != ' ' && *s != '\t') s++;
        te[n++] = s;
    }
    return n;
}

static bool tok_is(const char* b, const char* e, const char* lit) {
    size_t n = strlen(lit);
    return (size_t)(e - b) == n && memcmp(b, lit, n) == 0;
}

static bool tok_int(const char* s, const char* e, long long* v) {
    bool neg = false;
    if (s < e && (*s == '-' || *s == '+')) { neg = *s == '-'; s++; }
    if (s >= e) return false;
    long long x = 0;
    for (; s < e; s++) {
        if (!is_digit(*s)) return false;
        if (x < 1000000000000LL) x = x * 10 + (*s - '0');
    }
    *v = neg ? -x : x;
    return true;
}

/* Parse a "-1"/"-2" record. `s` points just past the key token. For "-1" an id is
   read first. Values are fixed 12-wide columns starting right where the id ends
   (right-justified I5/I10 ids both work); if a column does not parse, the rest of
   the line is re-read by whitespace. Returns the number of values, -1 on no id. */
static int rec_values(const char* s, const char* e, bool with_id, uint32_t* id,
                      double* vals, int max) {
    if (with_id) {
        while (s < e && *s == ' ') s++;
        const char* d = s;
        long long x = 0;
        for (; s < e && is_digit(*s); s++) if (x < 10000000000LL) x = x * 10 + (*s - '0');
        if (s == d || x > 0xFFFFFFFELL) return -1;
        *id = (uint32_t)x;
    }
    int n = 0;
    const char* p = s;
    while (n < max && p < e) {
        const char* q = (e - p > 12) ? p + 12 : e;
        const char* t = p;
        while (t < q && *t == ' ') t++;
        if (t == q) { p = q; continue; }            /* blank column */
        double v;
        if (!cv_parse_num(p, q, &v)) goto by_space;
        vals[n++] = v;
        p = q;
    }
    return n;
by_space:
    {
        const char* tb[CV_MAX_COMP]; const char* te[CV_MAX_COMP];
        int k = tokens(p, e, tb, te, max - n);
        for (int i = 0; i < k; i++) {
            double v;
            if (!cv_parse_num(tb[i], te[i], &v)) v = NAN;
            vals[n++] = v;
        }
        return n;
    }
}

/* A header-ish line (100CL, 1PSTEP, 9999, 2C...) starts with a digit after the
   indent; data lines start with '-'. Used to stop scanning a block that forgot its
   "-3" terminator. */
static bool find_text(const char* s, const char* e, const char* lit) {
    size_t n = strlen(lit);
    for (; s + n <= e; s++) if (memcmp(s, lit, n) == 0) return true;
    return false;
}

static bool is_header_line(const char* s, const char* e) {
    while (s < e && *s == ' ') s++;
    return s < e && is_digit(*s);
}

static bool is_end_line(const char* s, const char* e) {
    while (s < e && *s == ' ') s++;
    return e - s >= 2 && s[0] == '-' && s[1] == '3' && (e - s == 2 || s[2] == ' ');
}

/* ---- id map --------------------------------------------------------------- */

static inline uint32_t hash_u32(uint32_t x, uint32_t mask) {
    return (uint32_t)((x * 2654435761u) ^ (x >> 16)) & mask;
}

static uint32_t map_get(const cv_idmap* m, uint32_t id) {
    if (m->flat) return id < m->flat_n ? m->flat[id] : UINT32_MAX;
    if (!m->keys) return UINT32_MAX;
    uint32_t mask = m->hcap - 1;
    for (uint32_t h = hash_u32(id, mask);; h = (h + 1) & mask) {
        if (m->keys[h] == id) return m->vals[h];
        if (m->keys[h] == UINT32_MAX) return UINT32_MAX;
    }
}

uint32_t cv_frd_node_index(const cv_frd* f, uint32_t id) { return map_get(&f->idmap, id); }
uint32_t cv_frd_elem_index(const cv_frd* f, uint32_t id) { return map_get(&f->emap, id); }

/* id -> index map. Returns false on OOM. Duplicate ids: first occurrence wins. */
static bool build_map(cv_idmap* m, const uint32_t* ids, uint32_t n, size_t* dups) {
    uint32_t maxid = 0;
    for (uint32_t i = 0; i < n; i++) maxid = CV_MAX(maxid, ids[i]);
    if (n == 0) return true;
    if ((uint64_t)maxid <= 4ull * n + 1024) {
        m->flat_n = maxid + 1;
        m->flat = malloc((size_t)m->flat_n * sizeof(uint32_t));
        if (!m->flat) return false;
        memset(m->flat, 0xFF, (size_t)m->flat_n * sizeof(uint32_t));
        for (uint32_t i = 0; i < n; i++) {
            if (m->flat[ids[i]] != UINT32_MAX) { (*dups)++; continue; }
            m->flat[ids[i]] = i;
        }
        return true;
    }
    uint32_t cap = 16;
    while (cap < 2u * n) cap <<= 1;
    m->hcap = cap;
    m->keys = malloc((size_t)cap * sizeof(uint32_t));
    m->vals = malloc((size_t)cap * sizeof(uint32_t));
    if (!m->keys || !m->vals) return false;
    memset(m->keys, 0xFF, (size_t)cap * sizeof(uint32_t));
    uint32_t mask = cap - 1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t id = ids[i];
        if (id == UINT32_MAX) continue;
        for (uint32_t h = hash_u32(id, mask);; h = (h + 1) & mask) {
            if (m->keys[h] == id) { (*dups)++; break; }
            if (m->keys[h] == UINT32_MAX) { m->keys[h] = id; m->vals[h] = i; break; }
        }
    }
    return true;
}

bool cv_frd_build_maps(cv_frd* f, size_t* nd, size_t* ed) {
    size_t a = 0, b = 0;
    bool ok = build_map(&f->idmap, f->node_id, f->n_nodes, &a) && build_map(&f->emap, f->elem_id, f->n_elems, &b);
    if (nd) *nd = a;
    if (ed) *ed = b;
    return ok;
}

/* ---- element matching across two meshes ---------------------------------------- */

static uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/* order-independent signature of an element's node ids and count */
static uint64_t elem_sig(const cv_frd* f, uint32_t e) {
    uint64_t s = mix64(f->eoff[e + 1] - f->eoff[e]), x = 0;
    for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) {
        uint64_t h = mix64(f->node_id[f->conn[j]]);
        s += h; x ^= h * 0x2545F4914F6CDD1Dull;
    }
    return s ^ mix64(x);
}

static bool same_nodes(const cv_frd* a, uint32_t ea, const cv_frd* b, uint32_t eb) {
    uint32_t n = a->eoff[ea + 1] - a->eoff[ea];
    if (n != b->eoff[eb + 1] - b->eoff[eb] || elem_sig(a, ea) != elem_sig(b, eb)) return false;
    for (uint32_t i = a->eoff[ea]; i < a->eoff[ea + 1]; i++) {   /* n <= 20: quadratic is fine */
        uint32_t id = a->node_id[a->conn[i]];
        bool hit = false;
        for (uint32_t j = b->eoff[eb]; j < b->eoff[eb + 1] && !hit; j++) hit = b->node_id[b->conn[j]] == id;
        if (!hit) return false;
    }
    return true;
}

static int cmp_u64pair(const void* a, const void* b) {
    const uint64_t* x = a; const uint64_t* y = b;
    return x[0] != y[0] ? (x[0] > y[0]) - (x[0] < y[0]) : (x[1] > y[1]) - (x[1] < y[1]);
}

uint32_t* cv_frd_match_elems(const cv_frd* from, const cv_frd* to, cv_elem_match* info) {
    cv_elem_match m = {0};
    uint32_t* map = malloc(CV_MAX(from->n_elems, 1) * sizeof(uint32_t));
    uint8_t* used = calloc(CV_MAX(to->n_elems, 1), 1);
    uint64_t* sig = NULL;
    if (!map || !used) goto oom;
    uint32_t left = 0;
    for (uint32_t e = 0; e < from->n_elems; e++) {
        uint32_t t = cv_frd_elem_index(to, from->elem_id[e]);
        map[e] = t != UINT32_MAX && same_nodes(from, e, to, t) ? t : UINT32_MAX;
        if (map[e] != UINT32_MAX) { used[t] = 1; m.by_id++; } else left++;
    }
    if (left) {                                     /* by node list: (signature, index), sorted */
        sig = malloc(CV_MAX(to->n_elems, 1) * 2 * sizeof(uint64_t));
        if (!sig) goto oom;
        for (uint32_t t = 0; t < to->n_elems; t++) { sig[2 * t] = elem_sig(to, t); sig[2 * t + 1] = t; }
        qsort(sig, to->n_elems, 2 * sizeof(uint64_t), cmp_u64pair);
        for (uint32_t e = 0; e < from->n_elems; e++) {
            if (map[e] != UINT32_MAX) continue;
            uint64_t s = elem_sig(from, e);
            uint32_t lo = 0, hi = to->n_elems;
            while (lo < hi) { uint32_t k = (lo + hi) / 2; if (sig[2 * k] < s) lo = k + 1; else hi = k; }
            for (; lo < to->n_elems && sig[2 * lo] == s; lo++) {
                uint32_t t = (uint32_t)sig[2 * lo + 1];
                if (used[t] || !same_nodes(from, e, to, t)) continue;   /* duplicates pair off in order */
                int64_t off = (int64_t)to->elem_id[t] - from->elem_id[e];
                m.shifted = m.by_nodes ? m.shifted && off == m.offset : true;
                m.offset = off;
                map[e] = t; used[t] = 1; m.by_nodes++;
                break;
            }
        }
        bool trust = m.by_id >= m.by_nodes;
        for (uint32_t e = 0; e < from->n_elems; e++) {
            if (map[e] != UINT32_MAX) continue;
            uint32_t t = trust ? cv_frd_elem_index(to, from->elem_id[e]) : UINT32_MAX;
            if (t != UINT32_MAX && !used[t]) { map[e] = t; m.by_id++; } else m.none++;
        }
    }
    if (!m.shifted) m.offset = 0;
    free(used); free(sig);
    if (info) *info = m;
    return map;
oom:
    free(map); free(used); free(sig);
    return NULL;
}

/* ---- index pass ------------------------------------------------------------ */

typedef struct { int step; cv_field_desc d; } field_rec;

static void msgf(cv_frd* f, uint64_t where, bool off, const char* fmt, long long a) {
    char buf[120];
    snprintf(buf, sizeof buf, fmt, a);
    cv_msg_add(&f->msgs, where, off, buf);
}

static uint64_t binary_field_bytes(int ncomp, int fmt, uint32_t n_rec) {
    int vsz = fmt == 3 ? 8 : 4;
    uint64_t total = 0;
    for (int c0 = 0; c0 < ncomp; c0 += 6) {
        int n = CV_MIN(ncomp - c0, 6);
        total += (uint64_t)n_rec * (uint64_t)(4 + n * vsz);
    }
    return total;
}

/* FEMaster writes a tensor as XX YY ZZ YZ ZX XY. Name the components in the usual
   .frd order XY YZ ZX instead; cv_frd_read_field moves the values to match. */
static bool comp_ends(const char* c, const char* ab) {
    size_t k = strlen(c);
    return k >= 2 && tolower((unsigned char)c[k - 2]) == ab[0] && tolower((unsigned char)c[k - 1]) == ab[1];
}
static void shear_order(cv_field_desc* d) {
    if (d->ncomp != 6 || !comp_ends(d->comp[0], "xx") || !comp_ends(d->comp[1], "yy") || !comp_ends(d->comp[2], "zz") ||
        !comp_ends(d->comp[3], "yz") || !comp_ends(d->comp[4], "zx") || !comp_ends(d->comp[5], "xy")) return;
    char yz[12], zx[12];
    memcpy(yz, d->comp[3], 12);
    memcpy(zx, d->comp[4], 12);
    memcpy(d->comp[3], d->comp[5], 12);
    memcpy(d->comp[4], yz, 12);
    memcpy(d->comp[5], zx, 12);
    d->shear_yzx = true;
}

#define PUSH(v, x) do { if (!cv_push((v), (x))) goto oom; } while (0)

bool cv_frd_parse(cv_frd* f, const char* data, size_t size) {
    memset(f, 0, sizeof *f);
    f->data = data;
    f->size = size;

    CV_VEC(uint32_t) node_id = {0};
    CV_VEC(float)    xyz = {0};
    CV_VEC(uint32_t) eid = {0}, emat = {0}, egrp = {0}, eoff = {0}, conn = {0};
    CV_VEC(uint8_t)  etype = {0};
    CV_VEC(cv_step)  steps = {0};
    CV_VEC(field_rec) frecs = {0};

    enum { B_NONE, B_NODES, B_ELEMS, B_RESULT } block = B_NONE;
    cursor c = { data, data, data + size, 0 };
    const char *ls, *le;
    const char* tb[16]; const char* te[16];

    int  pend_step = 0, pend_inc = 0;          /* from 1PSTEP, for the next 100CL */
    bool pend_mode = false;                    /* a 1PMODE record since the last 1PSTEP */
    long long cur_numstp = -1;
    double    cur_time = NAN;
    int       res_fmt = 1;
    uint32_t  res_nrec = 0;

    bool in_hdr = false;                        /* between -4 and the field's data */
    field_rec fr;
    int hdr_ncomp = 0, hdr_seen = 0;
    bool saw_end = false;

    PUSH(eoff, 0u);

    while (next_line(&c, &ls, &le)) {
        int nt = tokens(ls, le, tb, te, 8);
        if (nt == 0) continue;
        const char *k = tb[0], *ke = te[0];

        /* An ASCII field ends its header at the first record: index it, skip data. */
        if (in_hdr && res_fmt == 1 && (tok_is(k, ke, "-1") || tok_is(k, ke, "-3"))) {
            fr.d.data_off = (uint64_t)(ls - data);
            fr.d.line = c.line;
            if (fr.d.ncomp == 0) fr.d.ncomp = CV_MIN(CV_MAX(hdr_ncomp, 1), CV_MAX_COMP);
            shear_order(&fr.d);
        PUSH(frecs, fr);
            in_hdr = false;
            if (tok_is(k, ke, "-1")) {
                const char *s2, *e2;
                while (next_line(&c, &s2, &e2)) {
                    if (is_end_line(s2, e2)) break;
                    if (is_header_line(s2, e2)) { c.p = s2; c.line--; break; }
                }
            }
            block = B_NONE;
            continue;
        }

        if (tok_is(k, ke, "-1")) {
            if (block == B_NODES) {
                uint32_t id; double v[3] = {0, 0, 0};
                int n = rec_values(ke, le, true, &id, v, 3);
                if (n < 0) { msgf(f, c.line, false, "malformed node record", 0); continue; }
                if (n < 3) msgf(f, c.line, false, "node has missing coordinates", 0);
                PUSH(node_id, id);
                PUSH(xyz, (float)v[0]); PUSH(xyz, (float)v[1]); PUSH(xyz, (float)v[2]);
            } else if (block == B_ELEMS) {
                long long id, ty, gr = 0, ma = 0;
                if (nt < 3 || !tok_int(tb[1], te[1], &id) || !tok_int(tb[2], te[2], &ty) ||
                    id < 0 || id > 0xFFFFFFFELL) {
                    msgf(f, c.line, false, "malformed element record", 0);
                    continue;
                }
                if (nt > 3) tok_int(tb[3], te[3], &gr);
                if (nt > 4) tok_int(tb[4], te[4], &ma);
                if (eid.n) PUSH(eoff, (uint32_t)conn.n);    /* close the previous one */
                PUSH(eid, (uint32_t)id);
                PUSH(etype, (uint8_t)((ty > 0 && ty < 256) ? ty : 0));
                PUSH(egrp, (uint32_t)(gr < 0 ? 0 : gr));
                PUSH(emat, (uint32_t)(ma < 0 ? 0 : ma));
            }
            continue;
        }
        if (tok_is(k, ke, "-2")) {
            if (block == B_ELEMS && eid.n) {
                const char* nb[32]; const char* ne[32];
                int n = tokens(ke, le, nb, ne, 32);
                for (int i = 0; i < n; i++) {
                    long long v;
                    uint32_t id = (tok_int(nb[i], ne[i], &v) && v >= 0 && v < 0xFFFFFFFFLL)
                                  ? (uint32_t)v : UINT32_MAX;
                    PUSH(conn, id);
                }
            }
            continue;
        }
        if (tok_is(k, ke, "-3")) { block = B_NONE; continue; }

        if (tok_is(k, ke, "-4")) {
            if (in_hdr) {                           /* previous field had no data */
                fr.d.data_off = (uint64_t)(ls - data);
                fr.d.line = c.line;
                if (fr.d.ncomp == 0) fr.d.ncomp = 1;
                fr.d.n_rec = 0;
                shear_order(&fr.d);
                PUSH(frecs, fr);
            }
            memset(&fr, 0, sizeof fr);
            fr.step = (int)steps.n - 1;
            if (fr.step < 0) {                          /* field before any 100CL */
                cv_step s0 = { 0, 0, 0.f, false, 0, NULL };
                PUSH(steps, s0);
                fr.step = 0;
            }
            if (nt > 1) snprintf(fr.d.name, sizeof fr.d.name, "%.*s", (int)(te[1] - tb[1]), tb[1]);
            long long nc = 0;
            if (nt > 2) tok_int(tb[2], te[2], &nc);
            hdr_ncomp = (int)CV_MIN(CV_MAX(nc, 0), 1000);
            hdr_seen = 0;
            fr.d.fmt = res_fmt;
            fr.d.n_rec = res_nrec;
            in_hdr = true;
            block = B_RESULT;
            continue;
        }
        if (tok_is(k, ke, "-5")) {
            if (!in_hdr) continue;
            hdr_seen++;
            /* "ALL" (IEXIST=1, ICNAME ALL) is computed by the reader, never stored. */
            bool pseudo = nt > 1 && tok_is(tb[1], te[1], "ALL");
            if (nt > 6 && te[6] - tb[6] >= 3 && memcmp(te[6] - 3, "ALL", 3) == 0) pseudo = true;
            if (!pseudo && nt > 1 && fr.d.ncomp < CV_MAX_COMP) {
                snprintf(fr.d.comp[fr.d.ncomp], sizeof fr.d.comp[0], "%.*s",
                         (int)(te[1] - tb[1]), tb[1]);
                fr.d.ncomp++;
            }
            if (res_fmt != 1 && hdr_seen >= hdr_ncomp) {
                /* Binary: the records start right after this line. */
                if (fr.d.ncomp == 0) fr.d.ncomp = CV_MIN(CV_MAX(hdr_ncomp, 1), CV_MAX_COMP);
                fr.d.data_off = (uint64_t)(c.p - data);
                uint64_t bytes = binary_field_bytes(fr.d.ncomp, res_fmt, res_nrec);
                if (bytes > (uint64_t)(c.end - c.p)) {
                    msgf(f, fr.d.data_off, true, "binary field data truncated", 0);
                    c.p = c.end;
                } else {
                    c.p += bytes;
                }
                shear_order(&fr.d);
                PUSH(frecs, fr);
                in_hdr = false;
                block = B_NONE;
            }
            continue;
        }

        if (tok_is(k, ke, "2C")) {
            long long cnt = 0;
            if (nt > 1) tok_int(tb[1], te[1], &cnt);
            bool bin = (le - ls) > 73 && ls[73] == '3';
            uint64_t left = (uint64_t)(c.end - c.p);
            uint64_t cap = bin ? left / 28 : left / 12 + 1;
            if (cnt < 0) cnt = 0;
            if ((uint64_t)cnt > cap) {
                msgf(f, c.line, false, "node count in header exceeds file size", 0);
                cnt = (long long)cap;
            }
            if (!cv_reserve(node_id, node_id.n + (size_t)cnt)) goto oom;
            if (!cv_reserve(xyz, xyz.n + 3 * (size_t)cnt)) goto oom;
            if (bin) {
                for (long long i = 0; i < cnt; i++) {
                    int32_t id; double x[3];
                    memcpy(&id, c.p, 4); memcpy(x, c.p + 4, 24);
                    c.p += 28;
                    node_id.a[node_id.n++] = (uint32_t)id;
                    xyz.a[xyz.n++] = (float)x[0];
                    xyz.a[xyz.n++] = (float)x[1];
                    xyz.a[xyz.n++] = (float)x[2];
                }
                block = B_NONE;
            } else {
                block = B_NODES;
            }
            continue;
        }
        if (tok_is(k, ke, "3C")) {
            long long cnt = 0;
            if (nt > 1) tok_int(tb[1], te[1], &cnt);
            bool bin = (le - ls) > 73 && ls[73] == '2';
            uint64_t cap = (uint64_t)(c.end - c.p) / 16 + 1;
            if (cnt < 0) cnt = 0;
            if ((uint64_t)cnt > cap) {
                msgf(f, c.line, false, "element count in header exceeds file size", 0);
                cnt = (long long)cap;
            }
            if (bin) {
                for (long long i = 0; i < cnt; i++) {
                    if (c.end - c.p < 16) {
                        msgf(f, (uint64_t)(c.p - data), true, "binary element data truncated", 0);
                        c.p = c.end;
                        break;
                    }
                    int32_t h[4];
                    memcpy(h, c.p, 16);
                    int nn = cv_frd_type_nodes(h[1]);
                    if (nn == 0) {
                        msgf(f, (uint64_t)(c.p - data), true,
                             "unknown binary element type %lld: rest of element block skipped", h[1]);
                        break;   /* record length unknown: cannot continue in this block */
                    }
                    if (c.end - c.p < 16 + 4 * nn) {
                        msgf(f, (uint64_t)(c.p - data), true, "binary element data truncated", 0);
                        c.p = c.end;
                        break;
                    }
                    c.p += 16;
                    if (eid.n) PUSH(eoff, (uint32_t)conn.n);
                    PUSH(eid, (uint32_t)h[0]);
                    PUSH(etype, (uint8_t)h[1]);
                    PUSH(egrp, (uint32_t)(h[2] < 0 ? 0 : h[2]));
                    PUSH(emat, (uint32_t)(h[3] < 0 ? 0 : h[3]));
                    for (int j = 0; j < nn; j++) {
                        int32_t nid;
                        memcpy(&nid, c.p, 4);
                        c.p += 4;
                        PUSH(conn, (uint32_t)nid);
                    }
                }
                block = B_NONE;
            } else {
                block = B_ELEMS;
            }
            continue;
        }
        if (ke - k >= 2 && k[0] == '1' && k[1] == 'P') {
            if (tok_is(k, ke, "1PMODE")) pend_mode = true;
            if (tok_is(k, ke, "1PSTEP") && nt > 3) {
                pend_mode = false;
                long long a, b;
                if (tok_int(tb[2], te[2], &a)) pend_inc = (int)a;
                if (tok_int(tb[3], te[3], &b)) pend_step = (int)b;
            }
            continue;
        }
        if (tok_is(k, ke, "100CL")) {
            double t = 0; long long nrec = 0, numstp = 0;
            if (nt > 2) cv_parse_num(tb[2], te[2], &t);
            if (nt > 3) tok_int(tb[3], te[3], &nrec);
            if (nt > 5) tok_int(tb[5], te[5], &numstp);
            res_fmt = 1;
            if ((le - ls) > 74) {
                if (ls[74] == '2') res_fmt = 2;
                else if (ls[74] == '3') res_fmt = 3;
            }
            if (nrec < 0) nrec = 0;
            if (res_fmt != 1 && (uint64_t)nrec > (uint64_t)(c.end - c.p) / 8) {
                msgf(f, c.line, false, "result record count exceeds file size", 0);
                nrec = (long long)((uint64_t)(c.end - c.p) / 8);
            }
            res_nrec = (uint32_t)nrec;
            if (steps.n == 0 || numstp != cur_numstp || t != cur_time) {
                /* "MODAL" in the header text (and a 1PMODE record before it) marks a mode shape */
                bool modal = pend_mode || find_text(ls, le, "MODAL");
                cv_step s = { pend_step ? pend_step : (int)numstp, pend_inc, (float)t, modal, 0, NULL };
                PUSH(steps, s);
                cur_numstp = numstp;
                cur_time = t;
            }
            block = B_RESULT;
            continue;
        }
        if (tok_is(k, ke, "9999")) { saw_end = true; break; }
        /* 1C, 1U and anything unknown: ignored. */
    }
    if (in_hdr) {                               /* file ended inside a field header */
        fr.d.data_off = size;
        if (fr.d.ncomp == 0) fr.d.ncomp = 1;
        shear_order(&fr.d);
        PUSH(frecs, fr);
    }
    if (!saw_end && size > 0)
        msgf(f, c.line, false, "file ends without the 9999 terminator (truncated?)", 0);

    /* ---- nodes ---- */
    f->n_nodes = (uint32_t)node_id.n;
    f->node_id = node_id.a;  node_id.a = NULL;
    f->xyz = xyz.a;          xyz.a = NULL;
    size_t dups = 0;
    if (!build_map(&f->idmap, f->node_id, f->n_nodes, &dups)) goto oom;
    if (dups) msgf(f, 0, false, "%lld duplicate node ids (first one kept)", (long long)dups);

    /* ---- elements: map ids to indices, drop what cannot be drawn ---- */
    if (eid.n) PUSH(eoff, (uint32_t)conn.n);
    {
        size_t bad_type = 0, bad_count = 0, bad_node = 0, w = 0, cw = 0;
        for (size_t e = 0; e < eid.n; e++) {
            uint32_t b = eoff.a[e], n = eoff.a[e + 1] - b;
            int want = cv_frd_type_nodes(etype.a[e]);
            if (want == 0) { bad_type++; continue; }
            if ((int)n != want) { bad_count++; continue; }
            bool ok = true;
            for (uint32_t j = 0; j < n; j++) {
                uint32_t ix = cv_frd_node_index(f, conn.a[b + j]);
                if (ix == UINT32_MAX) { ok = false; break; }
                conn.a[cw + j] = ix;      /* cw <= b, so this never overwrites unread ids */
            }
            if (!ok) { bad_node++; continue; }
            eid.a[w] = eid.a[e]; etype.a[w] = etype.a[e];
            egrp.a[w] = egrp.a[e]; emat.a[w] = emat.a[e];
            eoff.a[w] = (uint32_t)cw;
            cw += n;
            w++;
        }
        eoff.a[w] = (uint32_t)cw;
        if (bad_type)  msgf(f, 0, false, "%lld elements of unknown type skipped", (long long)bad_type);
        if (bad_count) msgf(f, 0, false, "%lld elements with a wrong node count skipped", (long long)bad_count);
        if (bad_node)  msgf(f, 0, false, "%lld elements referencing missing nodes skipped", (long long)bad_node);
        f->n_elems = (uint32_t)w;
    }
    for (size_t e = 0; e < (size_t)f->n_elems; e++) {          /* CSR invariants */
        CV_ASSERT(eoff.a[e] <= eoff.a[e + 1]);
        for (uint32_t j = eoff.a[e]; j < eoff.a[e + 1]; j++) CV_ASSERT(conn.a[j] < f->n_nodes);
    }
    f->elem_id = eid.a;  f->etype = etype.a;  f->egrp = egrp.a;  f->emat = emat.a;
    f->eoff = eoff.a;    f->conn = conn.a;
    eid.a = NULL; etype.a = NULL; egrp.a = NULL; emat.a = NULL; eoff.a = NULL; conn.a = NULL;
    {
        size_t edups = 0;
        if (!build_map(&f->emap, f->elem_id, f->n_elems, &edups)) goto oom;
        if (edups) msgf(f, 0, false, "%lld duplicate element ids (first one kept)", (long long)edups);
    }

    /* ---- steps: hand each step its fields ---- */
    f->n_steps = (int)steps.n;
    f->steps = steps.a;  steps.a = NULL;
    for (int s = 0; s < f->n_steps; s++) {
        int n = 0;
        for (size_t i = 0; i < frecs.n; i++) n += frecs.a[i].step == s;
        f->steps[s].fields = n ? malloc((size_t)n * sizeof(cv_field_desc)) : NULL;
        if (n && !f->steps[s].fields) goto oom;
        for (size_t i = 0; i < frecs.n; i++)
            if (frecs.a[i].step == s) f->steps[s].fields[f->steps[s].nfields++] = frecs.a[i].d;
    }
    cv_free_vec(frecs);
    return true;

oom:
    cv_free_vec(node_id); cv_free_vec(xyz); cv_free_vec(eid); cv_free_vec(emat);
    cv_free_vec(egrp); cv_free_vec(eoff); cv_free_vec(conn); cv_free_vec(etype);
    cv_free_vec(frecs);
    for (size_t i = 0; i < steps.n; i++) free(steps.a[i].fields);
    cv_free_vec(steps);
    cv_frd_free(f);
    cv_msg_add(&f->msgs, 0, false, "out of memory while reading the file");
    return false;
}

void cv_frd_free(cv_frd* f) {
    free(f->node_id); free(f->xyz);
    free(f->idmap.flat); free(f->idmap.keys); free(f->idmap.vals);
    free(f->emap.flat); free(f->emap.keys); free(f->emap.vals);
    free(f->elem_id); free(f->etype); free(f->emat); free(f->egrp);
    free(f->eoff); free(f->conn);
    for (int s = 0; s < f->n_steps; s++) free(f->steps[s].fields);
    free(f->steps);
    cv_msgs keep = f->msgs;       /* messages survive so an OOM can still be reported */
    memset(f, 0, sizeof *f);
    f->msgs = keep;
}

/* ---- field decode ----------------------------------------------------------- */

void cv_frd_read_field(const cv_frd* f, const cv_field_desc* d, float* out, cv_msgs* msgs) {
    const int nc = d->ncomp;
    const size_t total = (size_t)f->n_nodes * (size_t)nc;
    for (size_t i = 0; i < total; i++) out[i] = NAN;
    if (d->data_off >= f->size) return;

    size_t unknown = 0;
    char buf[120];

    if (d->fmt == 1) {
        cursor c = { f->data, f->data + d->data_off, f->data + f->size, d->line - 1 };
        const char *ls, *le;
        uint32_t ix = UINT32_MAX;
        int col = 0;
        double v[CV_MAX_COMP];
        while (next_line(&c, &ls, &le)) {
            const char* s = ls;
            while (s < le && *s == ' ') s++;
            if (le - s < 2 || s[0] != '-') {
                if (is_header_line(ls, le)) break;
                continue;
            }
            if (s[1] == '3') break;
            if (s[1] == '1' && (s + 2 == le || s[2] == ' ')) {
                uint32_t id;
                int n = rec_values(s + 2, le, true, &id, v, nc);
                if (n < 0) {
                    if (msgs) {
                        snprintf(buf, sizeof buf, "%s: malformed record", d->name);
                        cv_msg_add(msgs, c.line, false, buf);
                    }
                    ix = UINT32_MAX;
                    continue;
                }
                ix = cv_frd_node_index(f, id);
                col = 0;
                if (ix == UINT32_MAX) { unknown++; continue; }
                for (int k = 0; k < n; k++) out[(size_t)ix * nc + k] = (float)v[k];
                col = n;
            } else if (s[1] == '2' && (s + 2 == le || s[2] == ' ')) {
                /* continuation of a record with more than 6 components */
                if (ix == UINT32_MAX || col >= nc) continue;
                const char* vs = ls + 13 <= le ? ls + 13 : le;
                uint32_t dummy;
                int n = rec_values(vs, le, false, &dummy, v, nc - col);
                for (int k = 0; k < n; k++) out[(size_t)ix * nc + col + k] = (float)v[k];
                col += n;
            }
        }
    } else {
        const char* p = f->data + d->data_off;
        const char* end = f->data + f->size;
        const int vsz = d->fmt == 3 ? 8 : 4;
        for (int c0 = 0; c0 < nc; c0 += 6) {
            const int n = CV_MIN(nc - c0, 6);
            const size_t rec = 4 + (size_t)n * vsz;
            for (uint32_t r = 0; r < d->n_rec; r++) {
                if ((size_t)(end - p) < rec) {
                    if (msgs) {
                        snprintf(buf, sizeof buf, "%s: binary data truncated", d->name);
                        cv_msg_add(msgs, (uint64_t)(p - f->data), true, buf);
                    }
                    goto done;
                }
                int32_t id;
                memcpy(&id, p, 4);
                uint32_t ix = cv_frd_node_index(f, (uint32_t)id);
                const char* q = p + 4;
                p += rec;
                if (ix == UINT32_MAX) { if (c0 == 0) unknown++; continue; }
                float* o = out + (size_t)ix * nc + c0;
                if (vsz == 4) memcpy(o, q, (size_t)n * 4);
                else for (int k = 0; k < n; k++) { double x; memcpy(&x, q + 8 * k, 8); o[k] = (float)x; }
            }
        }
    }
done:
    if (d->shear_yzx)                           /* stored YZ ZX XY: to XY YZ ZX */
        for (size_t i = 0; i < (size_t)f->n_nodes; i++) {
            float* o = out + 6 * i, yz = o[3], zx = o[4];
            o[3] = o[5]; o[4] = yz; o[5] = zx;
        }
    if (unknown && msgs) {
        snprintf(buf, sizeof buf, "%s: %zu values for nodes not in the node block (ignored)",
                 d->name, unknown);
        cv_msg_add(msgs, d->line, false, buf);
    }
}
