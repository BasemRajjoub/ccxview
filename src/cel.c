/* cel.c -- CalculiX contact elements (.cel) and warning node sets (.nam) (cel.h). */
#include "cel.h"
#include "inp.h"
#include <ctype.h>
#include <strings.h>

/* a set as read: its name and where it first stood in the file */
typedef struct { char name[64]; int step, inc, att, it; uint32_t n, order; } rset;

typedef struct {
    CV_VEC(cv_celem) el;
    CV_VEC(int)      eset;          /* per element: index into sets */
    CV_VEC(rset)     sets;
    int      cur;                   /* the set of the open *ELEMENT card, -1 none */
    int      nn;                    /* node count of its type: 4, 6, 8; 0 another type */
    bool     in_elem;               /* data lines belong to an *ELEMENT card */
    uint32_t v[9]; int nv;          /* the element being read (id, nodes) */
    bool     vbad;
    uint32_t bad;
    bool     oom;
} P;

/* contactelements_st<s>_in<i>_at<a>_it<t>, any case; false (all 0) otherwise */
static void name_numbers(rset* s) {
    char low[64];
    size_t i = 0;
    for (; s->name[i] && i < sizeof low - 1; i++) low[i] = (char)tolower((unsigned char)s->name[i]);
    low[i] = 0;
    static const char* const tag[4] = { "contactelements_st", "_in", "_at", "_it" };
    int v[4];
    const char* q = low;
    s->step = s->inc = s->att = s->it = 0;
    for (int k = 0; k < 4; k++) {               /* each a number 1 .. 10^9, nothing after the last */
        size_t tl = strlen(tag[k]);
        if (strncmp(q, tag[k], tl)) return;
        q += tl;
        long x = 0;
        int nd = 0;
        for (; *q >= '0' && *q <= '9'; q++, nd++) { if (nd >= 9) return; x = x * 10 + (*q - '0'); }
        if (!nd || x < 1) return;
        v[k] = (int)x;
    }
    if (*q) return;
    s->step = v[0]; s->inc = v[1]; s->att = v[2]; s->it = v[3];
}

/* the set named name (any case, the first spelling kept), made when new; -1 out of memory */
static int set_of(P* p, const char* name) {
    if (p->cur >= 0 && !strcasecmp(p->sets.a[p->cur].name, name)) return p->cur;
    for (size_t k = 0; k < p->sets.n; k++)
        if (!strcasecmp(p->sets.a[k].name, name)) return (int)k;
    rset s;
    memset(&s, 0, sizeof s);
    snprintf(s.name, sizeof s.name, "%s", name);
    name_numbers(&s);
    s.order = (uint32_t)p->sets.n;
    if (!cv_push(p->sets, s)) { p->oom = true; return -1; }
    return (int)p->sets.n - 1;
}

/* corners c[0..4) without repeats, in order, to out; how many */
static int distinct(const uint32_t* c, uint32_t* out) {
    int n = 0;
    for (int i = 0; i < 4; i++) {
        bool seen = false;
        for (int j = 0; j < n; j++) seen |= out[j] == c[i];
        if (!seen) out[n++] = c[i];
    }
    return n;
}

/* the element read (p->v) decoded and kept; bad when its nodes do not fit */
static void finish_elem(P* p) {
    const uint32_t* v = p->v;
    cv_celem e;
    memset(&e, 0, sizeof e);
    e.id = v[0];
    bool ok = !p->vbad && v[0] > 0;
    for (int i = 1; i <= p->nn; i++) ok = ok && v[i] > 0;
    if (ok && p->nn == 4) {
        e.kind = CV_CEL_N2S; e.nm = 3; e.ns = 1;
        e.m[0] = v[1]; e.m[1] = v[2]; e.m[2] = v[3]; e.s[0] = v[4];
    } else if (ok && p->nn == 6) {
        e.kind = CV_CEL_N2S; e.nm = 4; e.ns = 1;
        e.m[0] = v[1]; e.m[1] = v[3]; e.m[2] = v[6]; e.m[3] = v[4]; e.s[0] = v[2];
        ok = v[2] == v[5];
    } else if (ok && p->nn == 8) {
        e.kind = CV_CEL_S2S;
        int nm = distinct(v + 1, e.m), ns = distinct(v + 5, e.s);
        e.nm = (uint8_t)nm; e.ns = (uint8_t)ns;
        ok = nm >= 3 && ns >= 3;
    } else ok = false;
    p->nv = 0; p->vbad = false;
    if (!ok || p->cur < 0) { p->bad++; return; }
    if (!cv_push(p->el, e) || !cv_push(p->eset, p->cur)) { p->oom = true; return; }
    p->sets.a[p->cur].n++;
}

/* a half-read element left behind (a keyword or a line without its continuation comma) */
static void drop_partial(P* p) {
    if (p->nv) p->bad++;
    p->nv = 0; p->vbad = false;
}

/* a keyword line [s, e): opens or closes an *ELEMENT card */
static void keyword(P* p, const char* s, const char* e) {
    drop_partial(p);
    char buf[256];                  /* blanks dropped, case kept */
    size_t n = 0;
    for (; s < e && n < sizeof buf - 1; s++)
        if ((unsigned char)*s > ' ') buf[n++] = *s;
    buf[n] = 0;
    char* tok[16];                  /* split at the commas */
    int nt = 0;
    tok[nt++] = buf;
    for (char* c = buf; *c && nt < 16; c++)
        if (*c == ',') { *c = 0; tok[nt++] = c + 1; }
    bool elem = !strcasecmp(tok[0], "*ELEMENT");
    p->in_elem = elem;
    p->cur = -1;
    p->nn = 0;
    if (!elem) return;
    const char* elset = "";
    for (int i = 1; i < nt; i++) {
        char* eq = strchr(tok[i], '=');
        if (!eq) continue;
        *eq = 0;
        const char* val = eq + 1;
        if (!strcasecmp(tok[i], "TYPE"))
            p->nn = !strcasecmp(val, "C3D4") ? 4 : !strcasecmp(val, "C3D6") ? 6 : !strcasecmp(val, "C3D8") ? 8 : 0;
        else if (!strcasecmp(tok[i], "ELSET")) elset = val;
    }
    p->cur = set_of(p, elset);
}

/* one unsigned integer [s, e) with blanks around it; false when not one */
static bool uint_tok(const char* s, const char* e, uint32_t* out) {
    while (s < e && (unsigned char)*s <= ' ') s++;
    while (e > s && (unsigned char)e[-1] <= ' ') e--;
    if (s == e) return false;
    uint64_t v = 0;
    for (; s < e; s++) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10 + (uint64_t)(*s - '0');
        if (v > UINT32_MAX) return false;
    }
    *out = (uint32_t)v;
    return true;
}

/* a data line [s, e) of an *ELEMENT card: id and nodes, maybe going on in the next line */
static void data_line(P* p, const char* s, const char* e) {
    while (e > s && (unsigned char)e[-1] <= ' ') e--;
    bool more = e > s && e[-1] == ',';
    if (more) e--;
    if (!p->nn) { if (!more) p->bad++; return; }   /* a type not read: one element per line ended */
    int need = 1 + p->nn;
    const char* t = s;
    while (t <= e) {
        const char* c = memchr(t, ',', (size_t)(e - t));
        if (!c) c = e;
        uint32_t x = 0;
        if (!uint_tok(t, c, &x)) p->vbad = true;
        if (p->nv < need) p->v[p->nv] = x;
        p->nv++;
        t = c + 1;
    }
    if (p->nv > need) { p->bad++; p->nv = 0; p->vbad = false; return; }
    if (p->nv == need) finish_elem(p);
    else if (!more) drop_partial(p);
}

/* sets: named ones by step, increment, attempt, iteration; then the unnamed, all in file order on ties */
static int cmp_set(const void* a, const void* b) {
    const rset* x = a; const rset* y = b;
    bool ux = x->step == 0, uy = y->step == 0;
    if (ux != uy) return ux ? 1 : -1;
    int dx[4] = { x->step, x->inc, x->att, x->it }, dy[4] = { y->step, y->inc, y->att, y->it };
    for (int i = 0; i < 4; i++) if (dx[i] != dy[i]) return dx[i] < dy[i] ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

bool cv_cel_parse(cv_cel* c, const char* data, size_t size) {
    memset(c, 0, sizeof *c);
    P p;
    memset(&p, 0, sizeof p);
    p.cur = -1;
    const char* q = data;
    const char* end = data ? data + size : data;
    while (q && q < end && !p.oom) {
        const char* nl = memchr(q, '\n', (size_t)(end - q));
        const char* le = nl ? nl : end;
        const char* s = q;
        while (s < le && (s[0] == ' ' || s[0] == '\t')) s++;
        if (s < le && s[0] == '*') {
            if (!(s + 1 < le && s[1] == '*')) keyword(&p, s, le);
        } else {
            const char* t = s;
            while (t < le && (unsigned char)*t <= ' ') t++;
            if (t < le && p.in_elem) data_line(&p, s, le);
        }
        q = nl ? nl + 1 : end;
    }
    drop_partial(&p);

    bool ok = !p.oom;
    int ns = (int)p.sets.n;
    if (ok && p.el.n) {
        /* sort the sets, then lay the elements out set by set, file order kept */
        for (int k = 0; k < ns; k++) p.sets.a[k].order = (uint32_t)k;
        qsort(p.sets.a, (size_t)ns, sizeof *p.sets.a, cmp_set);
        int nkeep = 0;
        for (int k = 0; k < ns; k++) nkeep += p.sets.a[k].n > 0;
        c->sets = calloc((size_t)(nkeep ? nkeep : 1), sizeof *c->sets);
        c->elem = malloc(p.el.n * sizeof *c->elem);
        uint32_t* at = malloc((size_t)ns * sizeof *at);
        ok = c->sets && c->elem && at;
        if (ok) {
            uint32_t first = 0;
            int j = 0;
            for (int k = 0; k < ns; k++) {
                const rset* r = &p.sets.a[k];
                if (!r->n) continue;
                cv_celset* o = &c->sets[j];
                snprintf(o->name, sizeof o->name, "%s", r->name);
                o->step = r->step; o->inc = r->inc; o->att = r->att; o->it = r->it;
                o->first = first; o->n = r->n;
                at[r->order] = first;
                j++;
                first += r->n;
            }
            for (size_t i = 0; i < p.el.n; i++) c->elem[at[p.eset.a[i]]++] = p.el.a[i];
            c->n = (uint32_t)p.el.n;
            c->nsets = nkeep;
        }
        free(at);
    }
    c->bad = p.bad;
    cv_free_vec(p.el); cv_free_vec(p.eset); cv_free_vec(p.sets);
    if (!ok) { uint32_t bad = c->bad; cv_cel_free(c); c->bad = bad; }
    return ok;
}

void cv_cel_free(cv_cel* c) {
    if (!c) return;
    free(c->elem);
    free(c->sets);
    memset(c, 0, sizeof *c);
}

int cv_cel_find(const cv_cel* c, int step, int inc) {
    int best = -1;
    if (step <= 0 || inc <= 0) return -1;
    for (int k = 0; k < c->nsets; k++) {
        const cv_celset* s = &c->sets[k];
        if (s->step != step || s->inc != inc) continue;
        if (best < 0 || s->att > c->sets[best].att || (s->att == c->sets[best].att && s->it > c->sets[best].it)) best = k;
    }
    return best;
}

int cv_cel_ends(const cv_cel* c, int* out, int max) {
    int n = 0;
    for (int k = 0; k < c->nsets; ) {
        const cv_celset* s = &c->sets[k];
        if (!s->step) break;                        /* the unnamed come last */
        int j = k, best = k;                        /* sorted: (step, inc) runs together */
        for (; j < c->nsets && c->sets[j].step == s->step && c->sets[j].inc == s->inc; j++)
            if (c->sets[j].att > c->sets[best].att || (c->sets[j].att == c->sets[best].att && c->sets[j].it > c->sets[best].it)) best = j;
        if (out) { if (n >= max) break; out[n] = best; }
        n++;
        k = j;
    }
    return n;
}

/* an element and where it stands, for sorting by its nodes */
typedef struct { cv_celem e; uint32_t ix; } keyed;

static int cmp_keyed(const void* a, const void* b) {
    const keyed* x = a; const keyed* y = b;
    if (x->e.kind != y->e.kind) return x->e.kind < y->e.kind ? -1 : 1;
    if (x->e.nm != y->e.nm) return x->e.nm < y->e.nm ? -1 : 1;
    if (x->e.ns != y->e.ns) return x->e.ns < y->e.ns ? -1 : 1;
    for (int i = 0; i < 4; i++) if (x->e.m[i] != y->e.m[i]) return x->e.m[i] < y->e.m[i] ? -1 : 1;
    for (int i = 0; i < 4; i++) if (x->e.s[i] != y->e.s[i]) return x->e.s[i] < y->e.s[i] ? -1 : 1;
    return x->ix < y->ix ? -1 : x->ix > y->ix;
}
static int cmp_u32(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? -1 : x > y;
}
static bool same_nodes(const cv_celem* a, const cv_celem* b) {
    return a->kind == b->kind && a->nm == b->nm && a->ns == b->ns
        && !memcmp(a->m, b->m, sizeof a->m) && !memcmp(a->s, b->s, sizeof a->s);
}

uint32_t cv_cel_unique(const cv_cel* c, int k, uint32_t* out) {
    if (!c || k < 0 || k >= c->nsets) return 0;
    const cv_celset* s = &c->sets[k];
    if (!s->n) return 0;
    keyed* t = malloc(s->n * sizeof *t);
    if (!t) return 0;
    for (uint32_t i = 0; i < s->n; i++) {
        t[i].e = c->elem[s->first + i];
        t[i].e.id = 0;                              /* repeats differ in their id only */
        t[i].ix = s->first + i;
    }
    qsort(t, s->n, sizeof *t, cmp_keyed);
    uint32_t n = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        if (i && same_nodes(&t[i].e, &t[i - 1].e)) continue;
        if (out) out[n] = t[i].ix;
        n++;
    }
    free(t);
    if (out) qsort(out, n, sizeof *out, cmp_u32);   /* the first of each, in file order */
    return n;
}

bool cv_nam_parse(const char* data, size_t size, uint32_t** ids, uint32_t* n, char* name) {
    *ids = NULL; *n = 0;
    if (name) name[0] = 0;
    if (!data || !size) return false;
    cv_inp d;
    if (!cv_inp_parse(&d, data, size, NULL, NULL)) { cv_inp_free(&d); free(d.msgs.a); return false; }
    size_t tot = 0;
    bool named = false;
    for (int k = 0; k < d.nsets; k++) {
        if (d.sets[k].is_elem) continue;
        if (name && !named) { snprintf(name, 64, "%s", d.sets[k].name); named = true; }
        tot += d.sets[k].n;
    }
    uint32_t* a = tot ? malloc(tot * sizeof *a) : NULL;
    if (a) {
        size_t m = 0;
        for (int k = 0; k < d.nsets; k++)
            if (!d.sets[k].is_elem && d.sets[k].n) { memcpy(a + m, d.sets[k].ids, d.sets[k].n * sizeof *a); m += d.sets[k].n; }
        qsort(a, tot, sizeof *a, cmp_u32);
        size_t u = 0;
        for (size_t i = 0; i < tot; i++) if (!u || a[i] != a[u - 1]) a[u++] = a[i];
        *ids = a; *n = (uint32_t)u;
    }
    cv_inp_free(&d);
    free(d.msgs.a);
    return a != NULL;
}
