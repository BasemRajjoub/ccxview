/* inp.c -- CalculiX / Abaqus .inp reader. See inp.h. The reader is in two files:
     inp.c           element types, text helpers, the keyword parser, cv_inp_parse
     inp_localsys.c  results in local systems (cv_localsys_*, *TRANSFORM / *ORIENTATION)
   inp_int.h holds what they share. */
#include "inp.h"
#include "gauss.h"      /* cv_frd_node_pos */
#include <ctype.h>
#include <stdint.h>
#include <math.h>
#include "inp_int.h"

/* ---- element types ------------------------------------------------------------- */

static const struct { const char* name; int type, nn; } kElem[] = {
    {"C3D8", 1, 8}, {"C3D8R", 1, 8}, {"C3D8I", 1, 8}, {"F3D8", 1, 8},
    {"C3D6", 2, 6}, {"F3D6", 2, 6},
    {"C3D4", 3, 4}, {"F3D4", 3, 4},
    {"C3D20", 4, 20}, {"C3D20R", 4, 20},
    {"C3D15", 5, 15},
    {"C3D10", 6, 10}, {"C3D10T", 6, 10},
    {"S3", 7, 3}, {"CPS3", 7, 3}, {"CPE3", 7, 3}, {"CAX3", 7, 3}, {"M3D3", 7, 3},
    {"S6", 8, 6}, {"CPS6", 8, 6}, {"CPE6", 8, 6}, {"CAX6", 8, 6}, {"M3D6", 8, 6},
    {"S4", 9, 4}, {"S4R", 9, 4}, {"CPS4", 9, 4}, {"CPS4R", 9, 4}, {"CPE4", 9, 4}, {"CPE4R", 9, 4},
    {"CAX4", 9, 4}, {"CAX4R", 9, 4}, {"M3D4", 9, 4}, {"M3D4R", 9, 4},
    {"S8", 10, 8}, {"S8R", 10, 8}, {"CPS8", 10, 8}, {"CPS8R", 10, 8}, {"CPE8", 10, 8}, {"CPE8R", 10, 8},
    {"CAX8", 10, 8}, {"CAX8R", 10, 8}, {"M3D8", 10, 8}, {"M3D8R", 10, 8},
    {"B31", 11, 2}, {"B31R", 11, 2}, {"T3D2", 11, 2}, {"SPRINGA", 11, 2}, {"DASHPOTA", 11, 2},
    {"GAPUNI", 11, 2}, {"T2D2", 11, 2},
    {"B32", 12, 3}, {"B32R", 12, 3}, {"T3D3", 12, 3},
};

/* discrete element types: kind, nodes; -1 = not one */
static int discrete_kind(const char* name, int* nn) {
    static const struct { const char* name; int kind, nn; } k[] = {
        {"SPRINGA", CV_DISC_SPRING, 2}, {"SPRING1", CV_DISC_SPRING, 1}, {"SPRING2", CV_DISC_SPRING, 2},
        {"DASHPOTA", CV_DISC_DASHPOT, 2}, {"DASHPOT1", CV_DISC_DASHPOT, 1}, {"DASHPOT2", CV_DISC_DASHPOT, 2},
        {"MASS", CV_DISC_MASS, 1}, {"GAPUNI", CV_DISC_GAP, 2}, {"DCOUP3D", CV_DISC_DCOUP, 1},
    };
    for (size_t i = 0; i < CV_COUNT(k); i++)
        if (strcmp(k[i].name, name) == 0) { *nn = k[i].nn; return k[i].kind; }
    return -1;
}

int cv_inp_elem_type(const char* name, int* nn) {
    for (size_t i = 0; i < CV_COUNT(kElem); i++)
        if (strcmp(kElem[i].name, name) == 0) { *nn = kElem[i].nn; return kElem[i].type; }
    *nn = 0;
    return 0;
}

/* ---- small text helpers ------------------------------------------------------------- */

void upcase(char* s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }

static void trim_to(char* out, size_t n, const char* s, const char* e) {
    while (s < e && isspace((unsigned char)*s)) s++;
    while (e > s && isspace((unsigned char)e[-1])) e--;
    size_t k = (size_t)(e - s) < n - 1 ? (size_t)(e - s) : n - 1;
    memcpy(out, s, k);
    out[k] = 0;
}

/* split a data line at commas into trimmed fields */
static int fields(const char* s, const char* e, char f[][64], int max) {
    int n = 0;
    while (n < max) {
        const char* c = memchr(s, ',', (size_t)(e - s));
        const char* fe = c ? c : e;
        trim_to(f[n], 64, s, fe);
        n++;
        if (!c) break;
        s = c + 1;
    }
    while (n > 0 && !f[n - 1][0]) n--;             /* trailing comma */
    return n;
}

static bool to_u32(const char* s, uint32_t* v) {
    if (!*s) return false;
    char* end;
    unsigned long long x = strtoull(s, &end, 10);
    while (*end == ' ') end++;
    if (*end || x > 0xFFFFFFFEull) return false;
    *v = (uint32_t)x;
    return true;
}

static bool to_f(const char* s, double* v) { return cv_parse_num(s, s + strlen(s), v); }

/* KEY=VALUE parameters of a keyword line (key upper-cased) */
typedef struct { char key[32]; char val[512]; } param;

static int keyword(const char* s, const char* e, char* kw, size_t kwn, param* p, int max) {
    const char* c = memchr(s, ',', (size_t)(e - s));
    trim_to(kw, kwn, s + 1, c ? c : e);             /* skip the '*' */
    upcase(kw);
    int n = 0;
    while (c && n < max) {
        const char* s2 = c + 1;
        c = memchr(s2, ',', (size_t)(e - s2));
        const char* fe = c ? c : e;
        const char* eq = memchr(s2, '=', (size_t)(fe - s2));
        trim_to(p[n].key, sizeof p[n].key, s2, eq ? eq : fe);
        upcase(p[n].key);
        if (eq) trim_to(p[n].val, sizeof p[n].val, eq + 1, fe); else p[n].val[0] = 0;
        if (p[n].key[0]) n++;
    }
    return n;
}

static const char* pget(const param* p, int n, const char* key) {
    for (int i = 0; i < n; i++) if (strcmp(p[i].key, key) == 0) return p[i].val;
    return NULL;
}

int cmp_idix(const void* a, const void* b) {        /* by id, then index */
    const cv_idix *x = a, *y = b;
    if (x->id != y->id) return x->id < y->id ? -1 : 1;
    return (x->ix > y->ix) - (x->ix < y->ix);
}

int32_t find_idix(const cv_idix* v, uint32_t n, uint32_t id) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t m = lo + (hi - lo) / 2;
        if (v[m].id < id) lo = m + 1; else hi = m;
    }
    return lo < n && v[lo].id == id ? v[lo].ix : -2;
}

/* ---- parser state ---------------------------------------------------------------------- */

typedef struct { char name[64]; bool is_elem; CV_VEC(uint32_t) ids; } vset;
typedef struct { char name[64]; CV_VEC(uint32_t) elem; CV_VEC(uint8_t) face; CV_VEC(uint32_t) nodes; } vsurf;
typedef struct { char elset[64]; char mat[64]; } section;
typedef struct { cv_link l; CV_VEC(uint32_t) nodes; CV_VEC(uint32_t) elems; } vlink;   /* elems: a rigid ELSET, expanded at the end */
typedef struct { char name[64]; cv_csys cs; bool bad; } vorient;
/* a section's orientation, by name; the layers of a composite shell may each name one */
typedef struct { char elset[64]; char ori[64]; bool mixed; uint32_t lay0, nlay; } osect;
typedef struct { char ori[64], mat[64]; float t; } vlayer;
typedef struct { char f[CV_OUT_N]; } outsys;

enum { S_SKIP, S_NODE, S_ELEM, S_NSET, S_ELSET, S_SURF, S_HEADING, S_BOUNDARY, S_CLOAD, S_DLOAD, S_SPRINGDOF,
       S_CFLUX, S_DFLUX, S_FILM, S_RADIATE, S_TEMP, S_PRETENSION, S_MPC, S_CYCLIC,
       S_EQUATION, S_DCOUP, S_TIE, S_CONTACT, S_TRANSFORM, S_ORIENT, S_OUTREQ, S_COMPOSITE,
       S_ELASTIC, S_PLASTIC };

typedef struct {
    cv_inp* d;
    cv_inp_reader rd;
    void* user;
    int depth;
    const char* file;
    uint64_t line;
    int st;
    /* collected */
    CV_VEC(uint32_t) node_id;
    CV_VEC(float) xyz;
    CV_VEC(uint32_t) eid, eoff, conn;
    CV_VEC(uint8_t) etype;
    CV_VEC(vset) sets;
    CV_VEC(vsurf) surfs;
    CV_VEC(section) sects;
    CV_VEC(char*) mats;
    CV_VEC(cv_matprop) mprop;           /* per *MATERIAL, as mats */
    CV_VEC(cv_bc) bcs;
    CV_VEC(cv_cload) cloads;
    CV_VEC(cv_dload) dloads;
    CV_VEC(cv_body) body;
    CV_VEC(cv_ntemp) temps;
    CV_VEC(cv_pretension) pret;
    CV_VEC(cv_discrete) disc;
    CV_VEC(section) sdofs;              /* elset -> "dof" of *SPRING / *DASHPOT (mat holds the digit) */
    CV_VEC(vlink) links;
    CV_VEC(cv_csys) trs;
    CV_VEC(cv_idix) node_tr;            /* in deck order: a later transform overrides */
    CV_VEC(vorient) oris;
    CV_VEC(osect) osects;
    CV_VEC(vlayer) layers;              /* of the composite sections, in order */
    CV_VEC(outsys) steps;
    CV_VEC(uint32_t) shells;            /* ids of S3..S8R elements */
    bool cshell;                        /* the open *ELEMENT block is shells */
    bool nodefile, elfile;              /* this step already had a *NODE FILE / *EL FILE */
    char outsys_c;                      /* 'G' / 'L' of the open output request */
    int  blk_line;                      /* data lines read of the open *ORIENTATION / composite */
    int  el_type, el_n;                 /* the open *ELASTIC: CV_EL_, values read */
    bool tr_cyl;
    int  eq_left;                       /* *EQUATION: terms still to read */
    int  cdisc;                         /* discrete kind of the open *ELEMENT block, -1 none */
    bool oom;
    /* current block */
    int ctype, cnn, cset, nodeset;
    bool generate, surf_node;
    uint32_t cur_id, cur_nodes[27];
    char cur_elset[64];
    int cur_n;
    bool in_elem;
    size_t bad_lines, bad_elems, unknown_types, skipped_keywords;
    char last_unknown[64];
} P;

static void note(P* p, const char* fmt, const char* a) {
    char msg[120], where[48] = "";
    if (p->depth > 0 && p->file) snprintf(where, sizeof where, " (%s)", p->file);
    snprintf(msg, sizeof msg, fmt, a);
    size_t l = strlen(msg);
    snprintf(msg + l, sizeof msg - l, "%s", where);
    cv_msg_add(&p->d->msgs, p->depth == 0 ? p->line : 0, false, msg);
}

static int find_set(P* p, const char* name, bool is_elem) {
    for (size_t i = 0; i < p->sets.n; i++)
        if (p->sets.a[i].is_elem == is_elem && strcmp(p->sets.a[i].name, name) == 0) return (int)i;
    return -1;
}

static int get_set(P* p, const char* raw, bool is_elem) {
    char name[64];
    snprintf(name, sizeof name, "%s", raw);
    upcase(name);
    int i = find_set(p, name, is_elem);
    if (i >= 0) return i;
    vset s;
    memset(&s, 0, sizeof s);
    snprintf(s.name, sizeof s.name, "%s", name);
    s.is_elem = is_elem;
    if (!cv_push(p->sets, s)) { p->oom = true; return -1; }
    return (int)p->sets.n - 1;
}

static void set_add(P* p, int si, uint32_t id) {
    if (si < 0) return;
    if (!cv_push(p->sets.a[si].ids, id)) p->oom = true;
}

/* a node id or the members of a node set, through cb */
typedef void (*node_fn)(P* p, uint32_t node, void* arg);
static bool each_node(P* p, const char* tok, node_fn fn, void* arg) {
    uint32_t v;
    if (to_u32(tok, &v)) { fn(p, v, arg); return true; }
    char nm[64]; snprintf(nm, sizeof nm, "%s", tok); upcase(nm);
    int o = find_set(p, nm, false);
    if (o < 0) return false;
    for (size_t j = 0; j < p->sets.a[o].ids.n; j++) fn(p, p->sets.a[o].ids.a[j], arg);
    return true;
}
static bool each_elem(P* p, const char* tok, node_fn fn, void* arg) {
    uint32_t v;
    if (to_u32(tok, &v)) { fn(p, v, arg); return true; }
    char nm[64]; snprintf(nm, sizeof nm, "%s", tok); upcase(nm);
    int o = find_set(p, nm, true);
    if (o < 0) return false;
    for (size_t j = 0; j < p->sets.a[o].ids.n; j++) fn(p, p->sets.a[o].ids.a[j], arg);
    return true;
}
static int find_surf(P* p, const char* raw) {
    char nm[64]; snprintf(nm, sizeof nm, "%s", raw); upcase(nm);
    for (size_t i = 0; i < p->surfs.n; i++) if (strcmp(p->surfs.a[i].name, nm) == 0) return (int)i;
    return -1;
}

static vlink* new_link(P* p, int kind, const char* name) {
    vlink v;
    memset(&v, 0, sizeof v);
    v.l.kind = (uint8_t)kind;
    v.l.surf[0] = v.l.surf[1] = -1;
    snprintf(v.l.name, sizeof v.l.name, "%s", name ? name : "");
    upcase(v.l.name);
    if (!cv_push(p->links, v)) { p->oom = true; return NULL; }
    return &p->links.a[p->links.n - 1];
}

static void link_add_node(P* p, uint32_t n, void* a) {
    vlink* v = &p->links.a[(size_t)(intptr_t)a];
    if (!cv_push(v->nodes, n)) p->oom = true;
}
static void link_add_elem(P* p, uint32_t e, void* a) {
    vlink* v = &p->links.a[(size_t)(intptr_t)a];
    if (!cv_push(v->elems, e)) p->oom = true;
}

static void finish_element(P* p) {
    if (!p->in_elem) return;
    p->in_elem = false;
    if (p->cur_n != p->cnn) { p->bad_elems++; return; }
    if (p->cdisc >= 0) {
        cv_discrete d = { p->cur_id, { p->cur_nodes[0], p->cnn > 1 ? p->cur_nodes[1] : 0 }, (uint8_t)p->cdisc, (uint8_t)p->cnn, 0 };
        if (!cv_push(p->disc, d)) p->oom = true;
        if (!p->ctype) { set_add(p, p->cset, p->cur_id); return; }   /* one node: not a mesh element */
    }
    if (!cv_push(p->eid, p->cur_id) || !cv_push(p->etype, (uint8_t)p->ctype) ||
        !cv_push(p->eoff, (uint32_t)p->conn.n)) { p->oom = true; return; }
    uint32_t frd_order[27];
    for (int i = 0; i < p->cnn; i++) frd_order[cv_frd_node_pos(p->ctype, p->cnn, i)] = p->cur_nodes[i];
    for (int i = 0; i < p->cnn; i++) if (!cv_push(p->conn, frd_order[i])) { p->oom = true; return; }
    if (p->cshell && !cv_push(p->shells, p->cur_id)) { p->oom = true; return; }
    set_add(p, p->cset, p->cur_id);
}

static void parse_text(P* p, const char* data, size_t size);

static int16_t cur_step(const P* p) { return (int16_t)CV_MIN((int)p->steps.n - 1, 32767); }

/* OP=NEW on a load card: a marker that drops what its kind had in the steps before */
static void op_new(P* p, int st) {
    int16_t s = cur_step(p);
    bool ok = true;
    if (st == S_BOUNDARY) { cv_bc b = { 0, 0, 0, s, 0 }; ok = cv_push(p->bcs, b); }
    else if (st == S_CLOAD || st == S_CFLUX) { cv_cload c = { 0, (uint8_t)(st == S_CLOAD ? 1 : 11), s, 0 }; ok = cv_push(p->cloads, c); }
    else if (st == S_TEMP) { cv_ntemp t = { 0, s, 0 }; ok = cv_push(p->temps, t); }
    else {
        cv_dload d = { 0, 0, (uint8_t)(st == S_DLOAD ? CV_DL_P : st == S_DFLUX ? CV_DL_FLUX : st == S_FILM ? CV_DL_FILM : CV_DL_RAD), s, 0 };
        ok = cv_push(p->dloads, d);
        if (ok && (st == S_DLOAD || st == S_DFLUX)) {
            cv_body b = { (uint8_t)(st == S_DLOAD ? CV_BL_GRAV : CV_BL_HEAT), s, -2, 0, 0, { 0 } };
            ok = cv_push(p->body, b);
        }
    }
    if (!ok) p->oom = true;
}

static void do_keyword(P* p, const char* s, const char* e) {
    char kw[64];
    param prm[16];
    int np = keyword(s, e, kw, sizeof kw, prm, 16);

    /* *INCLUDE is transparent: the included text continues whatever block is open */
    if (strncmp(kw, "INCLUDE", 7) == 0) {
        const char* in = pget(prm, np, "INPUT");
        if (!in || !in[0]) { note(p, "*INCLUDE without INPUT=", ""); return; }
        char path[512];
        snprintf(path, sizeof path, "%s", in);
        if (path[0] == '"') { memmove(path, path + 1, strlen(path)); char* q = strchr(path, '"'); if (q) *q = 0; }
        if (p->depth >= 8) { note(p, "*INCLUDE nested too deep: %s", path); return; }
        char* buf = NULL; size_t n = 0;
        if (!p->rd || !p->rd(p->user, path, &buf, &n)) { note(p, "cannot read *INCLUDE %s", path); return; }
        const char* pf = p->file; uint64_t pl = p->line;
        p->depth++; p->file = path; p->line = 0;
        parse_text(p, buf, n);                     /* data lines may continue our block */
        p->depth--; p->file = pf; p->line = pl;
        free(buf);
        return;
    }
    finish_element(p);
    p->st = S_SKIP;
    if (strcmp(kw, "NODE") == 0) {
        p->st = S_NODE;
        const char* ns = pget(prm, np, "NSET");
        p->nodeset = ns ? get_set(p, ns, false) : -1;
        return;
    }
    if (strcmp(kw, "ELEMENT") == 0) {
        const char* t = pget(prm, np, "TYPE");
        char tn[32] = "";
        if (t) { snprintf(tn, sizeof tn, "%s", t); upcase(tn); }
        p->ctype = cv_inp_elem_type(tn, &p->cnn);
        int dn;
        p->cdisc = discrete_kind(tn, &dn);
        p->cshell = tn[0] == 'S' && p->ctype && p->cdisc < 0;
        if (p->cdisc >= 0 && !p->ctype) p->cnn = dn;       /* one-node spring / mass */
        if (!p->ctype && p->cdisc < 0) {
            p->unknown_types++;
            snprintf(p->last_unknown, sizeof p->last_unknown, "%s", tn[0] ? tn : "(none)");
            return;                                /* data skipped */
        }
        const char* es = pget(prm, np, "ELSET");
        p->cset = es ? get_set(p, es, true) : -1;
        snprintf(p->cur_elset, sizeof p->cur_elset, "%s", es ? es : ""); upcase(p->cur_elset);
        p->st = S_ELEM;
        return;
    }
    if (strcmp(kw, "NSET") == 0 || strcmp(kw, "ELSET") == 0) {
        bool el = kw[0] == 'E';
        const char* nm = pget(prm, np, el ? "ELSET" : "NSET");
        if (!nm || !nm[0]) { note(p, "*%s without a name", kw); return; }
        p->cset = get_set(p, nm, el);
        p->generate = pget(prm, np, "GENERATE") != NULL;
        p->st = el ? S_ELSET : S_NSET;
        return;
    }
    if (strcmp(kw, "SURFACE") == 0) {
        const char* nm = pget(prm, np, "NAME");
        const char* ty = pget(prm, np, "TYPE");
        vsurf sf;
        memset(&sf, 0, sizeof sf);
        snprintf(sf.name, sizeof sf.name, "%s", nm ? nm : "(unnamed)");
        upcase(sf.name);
        if (!cv_push(p->surfs, sf)) { p->oom = true; return; }
        p->surf_node = ty && (ty[0] == 'N' || ty[0] == 'n');
        p->st = S_SURF;
        return;
    }
    if (strstr(kw, "SECTION") && strcmp(kw, "SECTION PRINT") != 0 && strncmp(kw, "PRE-TENSION", 11) != 0) {
        const char* es = pget(prm, np, "ELSET");
        const char* ma = pget(prm, np, "MATERIAL");
        const char* oi = pget(prm, np, "ORIENTATION");
        if (es && ma) {
            section sc;
            snprintf(sc.elset, sizeof sc.elset, "%s", es); upcase(sc.elset);
            snprintf(sc.mat, sizeof sc.mat, "%s", ma); upcase(sc.mat);
            if (!cv_push(p->sects, sc)) p->oom = true;
        }
        bool comp = pget(prm, np, "COMPOSITE") != NULL;
        if (es && ((oi && oi[0]) || comp)) {
            osect o;
            memset(&o, 0, sizeof o);
            snprintf(o.elset, sizeof o.elset, "%s", es); upcase(o.elset);
            if (oi) { snprintf(o.ori, sizeof o.ori, "%s", oi); upcase(o.ori); }
            o.lay0 = (uint32_t)p->layers.n;
            if (!cv_push(p->osects, o)) p->oom = true;
            if (comp) { p->st = S_COMPOSITE; p->blk_line = 0; }   /* thickness, , material, orientation per layer */
        }
        return;
    }
    if (strcmp(kw, "MATERIAL") == 0) {
        const char* nm = pget(prm, np, "NAME");
        if (nm) {
            char* c = malloc(64);
            if (!c) { p->oom = true; return; }
            snprintf(c, 64, "%s", nm); upcase(c);
            cv_matprop mp;
            memset(&mp, 0, sizeof mp);
            if (!cv_push(p->mats, c)) { free(c); p->oom = true; }
            else if (!cv_push(p->mprop, mp)) p->oom = true;
        }
        return;
    }
    if (strcmp(kw, "ELASTIC") == 0 && p->mprop.n && !p->mprop.a[p->mprop.n - 1].el) {
        const char* ty = pget(prm, np, "TYPE");
        char t[32] = "ISO";
        if (ty) { snprintf(t, sizeof t, "%s", ty); upcase(t); }
        p->el_type = !strncmp(t, "ISO", 3) ? CV_EL_ISO : !strncmp(t, "ENGINEERING", 11) ? CV_EL_ENG
                   : !strncmp(t, "ORTHO", 5) ? CV_EL_ORTHO : !strncmp(t, "ANISO", 5) ? CV_EL_ANISO : 0;
        p->el_n = 0;
        if (p->el_type) p->st = S_ELASTIC;     /* the first temperature only */
        return;
    }
    if (strcmp(kw, "PLASTIC") == 0 && p->mprop.n && !(p->mprop.a[p->mprop.n - 1].sy > 0)) {
        p->st = S_PLASTIC;
        return;
    }
    if (strcmp(kw, "HEADING") == 0) { p->st = S_HEADING; return; }
    if (strcmp(kw, "TRANSFORM") == 0) {
        const char* ns = pget(prm, np, "NSET");
        const char* ty = pget(prm, np, "TYPE");
        char nm[64] = "";
        if (ns) { snprintf(nm, sizeof nm, "%s", ns); upcase(nm); }
        p->cset = ns ? find_set(p, nm, false) : -1;
        if (p->cset < 0) { note(p, "*TRANSFORM without a known NSET=%s", ns ? ns : ""); return; }
        p->tr_cyl = ty && toupper((unsigned char)ty[0]) == 'C';
        p->st = S_TRANSFORM;
        return;
    }
    if (strcmp(kw, "ORIENTATION") == 0) {
        const char* nm = pget(prm, np, "NAME");
        const char* sy = pget(prm, np, "SYSTEM");
        vorient o;
        memset(&o, 0, sizeof o);
        snprintf(o.name, sizeof o.name, "%s", nm ? nm : ""); upcase(o.name);
        o.cs.cyl = sy && toupper((unsigned char)sy[0]) == 'C';
        o.bad = true;                              /* until its data line is read */
        if (!cv_push(p->oris, o)) { p->oom = true; return; }
        p->blk_line = 0;
        p->st = S_ORIENT;
        return;
    }
    if (strcmp(kw, "STEP") == 0) {
        outsys o;
        if (p->steps.n) o = p->steps.a[p->steps.n - 1];    /* requests carry over */
        else memset(o.f, ' ', sizeof o.f);
        if (!cv_push(p->steps, o)) p->oom = true;
        p->nodefile = p->elfile = false;
        p->skipped_keywords++;
        return;
    }
    {   /* output requests; CalculiX ignores the blanks in keywords */
        char k2[64]; int j = 0;
        for (const char* c = kw; *c && j < 63; c++) if (*c != ' ') k2[j++] = *c;
        k2[j] = 0;
        int kind = !strcmp(k2, "NODEFILE") || !strcmp(k2, "NODEOUTPUT") ? 1 :
                   !strcmp(k2, "ELFILE") || !strcmp(k2, "ELEMENTOUTPUT") ? 2 :
                   !strcmp(k2, "CONTACTFILE") || !strcmp(k2, "CONTACTOUTPUT") ? 3 : 0;
        if (kind && p->steps.n) {
            char* f = p->steps.a[p->steps.n - 1].f;
            if (kind == 1 && !p->nodefile) {       /* the first card of a step starts a fresh list */
                f[CV_OUT_U] = f[CV_OUT_RF] = f[CV_OUT_V] = f[CV_OUT_VF] = ' ';
                p->nodefile = true;
            }
            if (kind == 2 && !p->elfile) {
                f[CV_OUT_S] = f[CV_OUT_E] = f[CV_OUT_HFL] = ' ';
                p->elfile = true;
            }
            p->outsys_c = 'G';                     /* each card: GLOBAL=YES unless it says NO */
            const char* gl = pget(prm, np, "GLOBAL");
            if (gl && toupper((unsigned char)gl[0]) == 'N') p->outsys_c = 'L';
            p->st = S_OUTREQ;
            return;
        }
    }
    if (strcmp(kw, "RIGID BODY") == 0) {
        const char *rn = pget(prm, np, "REF NODE"), *ns = pget(prm, np, "NSET"), *es = pget(prm, np, "ELSET");
        vlink* v = new_link(p, CV_LINK_RIGID, ns ? ns : es);
        if (!v) return;
        uint32_t r;
        if (rn && to_u32(rn, &r)) v->l.ref = r;
        void* ix = (void*)(intptr_t)(p->links.n - 1);
        if (ns && !each_node(p, ns, link_add_node, ix)) p->bad_lines++;
        if (es && !each_elem(p, es, link_add_elem, ix)) p->bad_lines++;
        return;
    }
    if (strcmp(kw, "COUPLING") == 0) {                 /* *KINEMATIC / *DISTRIBUTING follow: dofs, ignored */
        const char *rn = pget(prm, np, "REF NODE"), *sf = pget(prm, np, "SURFACE"), *cn = pget(prm, np, "CONSTRAINT NAME");
        vlink* v = new_link(p, CV_LINK_KINEMATIC, cn ? cn : sf);
        if (!v) return;
        uint32_t r;
        if (rn && to_u32(rn, &r)) v->l.ref = r;
        if (sf) v->l.surf[0] = find_surf(p, sf);
        if (sf && v->l.surf[0] < 0) p->bad_lines++;
        return;
    }
    if (strcmp(kw, "DISTRIBUTING COUPLING") == 0) {   /* ELSET of one DCOUP3D element: its node is the ref */
        const char* es = pget(prm, np, "ELSET");
        vlink* v = new_link(p, CV_LINK_DISTRIBUTING, es);
        if (!v) return;
        int si = es ? find_set(p, v->l.name, true) : -1;
        for (size_t k = 0; si >= 0 && k < p->disc.n; k++) {
            const cv_discrete* q = &p->disc.a[k];
            if (q->kind != CV_DISC_DCOUP) continue;
            for (size_t j = 0; j < p->sets.a[si].ids.n; j++)
                if (p->sets.a[si].ids.a[j] == q->id) { v->l.ref = q->n[0]; break; }
            if (v->l.ref) break;
        }
        p->st = S_DCOUP;
        return;
    }
    if (strcmp(kw, "EQUATION") == 0) { p->st = S_EQUATION; p->eq_left = 0; return; }
    if (strcmp(kw, "TIE") == 0) {
        vlink* v = new_link(p, CV_LINK_TIE, pget(prm, np, "NAME"));
        if (v) p->st = S_TIE;
        return;
    }
    if (strcmp(kw, "CONTACT PAIR") == 0) {
        vlink* v = new_link(p, CV_LINK_CONTACT, pget(prm, np, "INTERACTION"));
        if (v) p->st = S_CONTACT;
        return;
    }
    if (strcmp(kw, "SPRING") == 0 || strcmp(kw, "DASHPOT") == 0) {   /* first line: the dof of a 1-node one */
        const char* es = pget(prm, np, "ELSET");
        if (es) { snprintf(p->cur_elset, sizeof p->cur_elset, "%s", es); upcase(p->cur_elset); p->st = S_SPRINGDOF; }
        return;
    }
    /* loads and supports: drawn as glyphs. Every line keeps the step it stands in. */
    {
        char kc[64]; int j = 0;                    /* CalculiX ignores the blanks in keywords */
        for (const char* c = kw; *c && j < 63; c++) if (*c != ' ') kc[j++] = *c;
        kc[j] = 0;
        int st = !strcmp(kc, "BOUNDARY") ? S_BOUNDARY : !strcmp(kc, "CLOAD") ? S_CLOAD : !strcmp(kc, "CFLUX") ? S_CFLUX :
                 !strcmp(kc, "DLOAD") || !strcmp(kc, "DSLOAD") ? S_DLOAD : !strcmp(kc, "DFLUX") ? S_DFLUX :
                 !strcmp(kc, "FILM") ? S_FILM : !strcmp(kc, "RADIATE") ? S_RADIATE : !strcmp(kc, "TEMPERATURE") ? S_TEMP : 0;
        if (st) {
            const char* op = pget(prm, np, "OP");
            if (op && toupper((unsigned char)op[0]) == 'N') op_new(p, st);
            p->st = st;
            return;
        }
        if (!strcmp(kc, "PRE-TENSIONSECTION")) {
            const char *sf = pget(prm, np, "SURFACE"), *el = pget(prm, np, "ELEMENT"), *nd = pget(prm, np, "NODE");
            cv_pretension t;
            memset(&t, 0, sizeof t);
            t.surf = sf ? find_surf(p, sf) : -1;
            if ((sf && t.surf < 0) || (!sf && !(el && to_u32(el, &t.elem))) || !nd || !to_u32(nd, &t.ref)) { p->bad_lines++; return; }
            if (!cv_push(p->pret, t)) { p->oom = true; return; }
            p->st = S_PRETENSION;
            return;
        }
        if (!strcmp(kc, "MPC")) { p->st = S_MPC; p->eq_left = 0; return; }
        if (!strcmp(kc, "CYCLICSYMMETRYMODEL")) {
            const char* n = pget(prm, np, "N");
            uint32_t v;
            if (n && to_u32(n, &v) && v > 0 && v < 100000) { p->d->cyc_n = (int)v; p->st = S_CYCLIC; }
            return;
        }
    }
    /* anything else (steps, loads, output, ...) is not part of the mesh: skip */
    p->skipped_keywords++;
}

static void add_bc(P* p, uint32_t n, void* a) {
    cv_bc b = *(cv_bc*)a;
    b.node = n;
    if (n && !cv_push(p->bcs, b)) p->oom = true;
}
static void add_cload(P* p, uint32_t n, void* a) {
    cv_cload c = *(cv_cload*)a;
    c.node = n;
    if (n && !cv_push(p->cloads, c)) p->oom = true;
}
static void add_dload(P* p, uint32_t e, void* a) {
    cv_dload d = *(cv_dload*)a;
    d.elem = e;
    if (e && !cv_push(p->dloads, d)) p->oom = true;
}
static void add_temp(P* p, uint32_t n, void* a) {
    cv_ntemp t = *(cv_ntemp*)a;
    t.node = n;
    if (n && !cv_push(p->temps, t)) p->oom = true;
}

/* a load on whole elements: an element set by name, or one element */
static void add_body(P* p, const char* tok, cv_body b) {
    char nm[64]; snprintf(nm, sizeof nm, "%s", tok); upcase(nm);
    b.set = find_set(p, nm, true);
    b.step = cur_step(p);
    if (b.set < 0 && !to_u32(tok, &b.elem)) { p->bad_lines++; return; }
    if (!cv_push(p->body, b)) p->oom = true;
}

/* the 0-based face of a label like P3, S2, F1NU, R4CR; a shell's SPOS / SNEG / P: 0 */
static int label_face(const char* lab) {
    return lab[1] >= '1' && lab[1] <= '6' ? lab[1] - '1' : 0;
}

static void do_data(P* p, const char* s, const char* e) {
    char f[32][64];
    switch (p->st) {
        case S_BOUNDARY: {                    /* node|set, first dof [, last dof [, value]] */
            int n = fields(s, e, f, 4);
            uint32_t lo, hi;
            if (n < 2 || !to_u32(f[1], &lo) || lo < 1 || lo > 11) { p->bad_lines++; return; }
            hi = lo;
            if (n > 2 && f[2][0] && (!to_u32(f[2], &hi) || hi < lo || hi > 11)) { p->bad_lines++; return; }
            double v = 0;
            if (n > 3 && f[3][0] && !to_f(f[3], &v)) { p->bad_lines++; return; }
            cv_bc b = { 0, (uint8_t)lo, (uint8_t)hi, cur_step(p), (float)v };
            if (!each_node(p, f[0], add_bc, &b)) p->bad_lines++;
            return;
        }
        case S_CLOAD: {                       /* node|set, dof, magnitude */
            int n = fields(s, e, f, 3);
            uint32_t dof; double v;
            if (n < 3 || !to_u32(f[1], &dof) || dof < 1 || dof > 6 || !to_f(f[2], &v)) { p->bad_lines++; return; }
            cv_cload c = { 0, (uint8_t)dof, cur_step(p), (float)v };
            if (!each_node(p, f[0], add_cload, &c)) p->bad_lines++;
            return;
        }
        case S_CFLUX: {                       /* node|set, 11 (or 0), heat */
            int n = fields(s, e, f, 3);
            double v;
            if (n < 3 || !to_f(f[2], &v)) { p->bad_lines++; return; }
            cv_cload c = { 0, 11, cur_step(p), (float)v };
            if (!each_node(p, f[0], add_cload, &c)) p->bad_lines++;
            return;
        }
        case S_TEMP: {                        /* node|set, temperature */
            int n = fields(s, e, f, 2);
            double v;
            if (n < 2 || !to_f(f[1], &v)) { p->bad_lines++; return; }
            cv_ntemp t = { 0, cur_step(p), (float)v };
            if (!each_node(p, f[0], add_temp, &t)) p->bad_lines++;
            return;
        }
        case S_DFLUX: case S_FILM: case S_RADIATE: {   /* elem|set, Sn | Fn | Rn | BF, value (film, radiate: sink, value) */
            int n = fields(s, e, f, 4);
            bool two = p->st != S_DFLUX;
            double v;
            if (n < (two ? 4 : 3) || !to_f(f[two ? 3 : 2], &v)) { p->bad_lines++; return; }
            char lab[16]; snprintf(lab, sizeof lab, "%s", f[1]); upcase(lab);
            if (p->st == S_DFLUX && !strcmp(lab, "BF")) {
                cv_body b = { CV_BL_HEAT, 0, -1, 0, (float)v, { 0 } };
                add_body(p, f[0], b);
                return;
            }
            if (lab[0] != (p->st == S_DFLUX ? 'S' : p->st == S_FILM ? 'F' : 'R')) return;
            cv_dload d = { 0, (uint8_t)label_face(lab), (uint8_t)(p->st == S_DFLUX ? CV_DL_FLUX : p->st == S_FILM ? CV_DL_FILM : CV_DL_RAD),
                           cur_step(p), (float)v };
            if (!each_elem(p, f[0], add_dload, &d)) p->bad_lines++;
            return;
        }
        case S_PRETENSION: {                  /* the direction of the preload */
            int n = fields(s, e, f, 3);
            double v[3] = { 0, 0, 0 };
            cv_pretension* t = &p->pret.a[p->pret.n - 1];
            p->st = S_SKIP;
            for (int k = 0; k < n && k < 3; k++) if (!to_f(f[k], &v[k])) return;
            if (v[0] != 0 || v[1] != 0 || v[2] != 0) { t->has_dir = true; for (int k = 0; k < 3; k++) t->dir[k] = (float)v[k]; }
            return;
        }
        case S_CYCLIC: {                      /* the two points of the axis */
            int n = fields(s, e, f, 6);
            double v;
            p->st = S_SKIP;
            for (int k = 0; k < 6; k++) p->d->cyc_axis[k] = k < n && to_f(f[k], &v) ? (float)v : 0;
            return;
        }
        case S_MPC: {                         /* BEAM | PLANE | STRAIGHT | a user name, nodes ...; lines of nodes continue it */
            int n = fields(s, e, f, 32), k = 0;
            uint32_t v;
            if (n > 0 && !to_u32(f[0], &v)) {
                char nm[64]; snprintf(nm, sizeof nm, "MPC %.50s", f[0]); upcase(nm);
                if (!new_link(p, CV_LINK_EQUATION, nm)) return;
                p->eq_left = -1;              /* an MPC is open */
                k = 1;
            } else if (p->eq_left != -1) { p->bad_lines++; return; }
            vlink* l = &p->links.a[p->links.n - 1];
            for (; k < n; k++) {
                if (!to_u32(f[k], &v) || !v) continue;
                if (!l->l.ref) l->l.ref = v;
                if (!cv_push(l->nodes, v)) { p->oom = true; return; }
            }
            return;
        }
        case S_DLOAD: {                       /* elem|set|surface, label, magnitude, ... */
            int n = fields(s, e, f, 9);
            double v = 0, w[6] = { 0 };
            if (n < 2) { p->bad_lines++; return; }
            char lab[16]; snprintf(lab, sizeof lab, "%s", f[1]); upcase(lab);
            if (strcmp(lab, "NEWTON") != 0 && (n < 3 || !to_f(f[2], &v))) { p->bad_lines++; return; }
            for (int k = 0; k < 6 && 3 + k < n; k++) to_f(f[3 + k], &w[k]);
            cv_body b = { CV_BL_N, 0, -1, 0, (float)v, { (float)w[0], (float)w[1], (float)w[2], (float)w[3], (float)w[4], (float)w[5] } };
            if (!strcmp(lab, "GRAV")) b.kind = CV_BL_GRAV;
            else if (!strcmp(lab, "CENTRIF")) b.kind = CV_BL_CENTRIF;
            else if (!strcmp(lab, "NEWTON")) b.kind = CV_BL_NEWTON;
            else if (lab[0] == 'B' && lab[1] >= 'X' && lab[1] <= 'Z' && !lab[2]) {
                b.kind = CV_BL_FORCE;
                memset(b.v, 0, sizeof b.v);
                b.v[lab[1] - 'X'] = 1;
            }
            if (b.kind != CV_BL_N) { add_body(p, f[0], b); return; }
            cv_dload d = { 0, 0, CV_DL_P, cur_step(p), (float)v };
            if (!strncmp(lab, "EDNOR", 5) && lab[5] >= '1' && lab[5] <= '4') { d.kind = CV_DL_EDGE; d.face = (uint8_t)(lab[5] - '1'); }
            else if (lab[0] == 'P' && lab[1] >= '1' && lab[1] <= '6') d.face = (uint8_t)(lab[1] - '1');
            else if (strcmp(lab, "P") != 0 && strcmp(lab, "PPOS") != 0 && strcmp(lab, "PNEG") != 0) return;   /* a shell's face */
            if (each_elem(p, f[0], add_dload, &d)) return;
            int si = !strcmp(lab, "P") ? find_surf(p, f[0]) : -1;   /* *DSLOAD: SURFACE, P, value: its faces */
            if (si < 0) { p->bad_lines++; return; }
            const vsurf* sf = &p->surfs.a[si];
            for (size_t k = 0; k < sf->elem.n; k++) { d.face = sf->face.a[k]; add_dload(p, sf->elem.a[k], &d); }
            return;
        }
        case S_NODE: {
            int n = fields(s, e, f, 4);
            uint32_t id; double x = 0, y = 0, z = 0;
            if (n < 2 || !to_u32(f[0], &id) || !to_f(f[1], &x) || (n > 2 && !to_f(f[2], &y)) ||
                (n > 3 && !to_f(f[3], &z))) { p->bad_lines++; return; }
            if (!cv_push(p->node_id, id) || !cv_push(p->xyz, (float)x) || !cv_push(p->xyz, (float)y) ||
                !cv_push(p->xyz, (float)z)) { p->oom = true; return; }
            set_add(p, p->nodeset, id);
            return;
        }
        case S_ELEM: {
            int n = fields(s, e, f, 32), k = 0;
            if (!p->in_elem) {
                if (n < 1 || !to_u32(f[0], &p->cur_id)) { p->bad_lines++; return; }
                p->in_elem = true;
                p->cur_n = 0;
                k = 1;
            }
            for (; k < n; k++) {
                uint32_t v;
                if (!to_u32(f[k], &v) || p->cur_n >= p->cnn) { p->bad_lines++; p->in_elem = false; p->bad_elems++; return; }
                p->cur_nodes[p->cur_n++] = v;
            }
            if (p->cur_n == p->cnn) finish_element(p);    /* else the next line continues it */
            return;
        }
        case S_NSET: case S_ELSET: {
            bool el = p->st == S_ELSET;
            int n = fields(s, e, f, 32);
            if (p->generate) {
                uint32_t a, b, st = 1;
                if (n < 2 || !to_u32(f[0], &a) || !to_u32(f[1], &b) || (n > 2 && !to_u32(f[2], &st)) || st == 0 ||
                    b < a || b - a > 50000000u) { p->bad_lines++; return; }
                for (uint64_t v = a; v <= b; v += st) set_add(p, p->cset, (uint32_t)v);
                return;
            }
            for (int k = 0; k < n; k++) {
                uint32_t v;
                if (to_u32(f[k], &v)) { set_add(p, p->cset, v); continue; }
                char nm[64];                                /* a set name: its members */
                snprintf(nm, sizeof nm, "%s", f[k]); upcase(nm);
                int o = find_set(p, nm, el);
                if (o < 0 || o == p->cset) { p->bad_lines++; continue; }
                for (size_t j = 0; j < p->sets.a[o].ids.n; j++) set_add(p, p->cset, p->sets.a[o].ids.a[j]);
            }
            return;
        }
        case S_SURF: {
            vsurf* sf = &p->surfs.a[p->surfs.n - 1];
            int n = fields(s, e, f, 2);
            if (n < 1) return;
            if (p->surf_node) {
                uint32_t v;
                if (to_u32(f[0], &v)) { if (!cv_push(sf->nodes, v)) p->oom = true; return; }
                char nm[64]; snprintf(nm, sizeof nm, "%s", f[0]); upcase(nm);
                int o = find_set(p, nm, false);
                if (o < 0) { p->bad_lines++; return; }
                for (size_t j = 0; j < p->sets.a[o].ids.n; j++) if (!cv_push(sf->nodes, p->sets.a[o].ids.a[j])) p->oom = true;
                return;
            }
            if (n < 2 || (toupper((unsigned char)f[1][0]) != 'S') ) { p->bad_lines++; return; }
            int face = atoi(f[1] + 1);
            if (face < 1 || face > 6) {                      /* SPOS/SNEG of shells: the shell face */
                char up[16]; snprintf(up, sizeof up, "%s", f[1]); upcase(up);
                if (strcmp(up, "SPOS") == 0 || strcmp(up, "SNEG") == 0) face = 1;
                else { p->bad_lines++; return; }
            }
            uint32_t v;
            if (to_u32(f[0], &v)) {
                if (!cv_push(sf->elem, v) || !cv_push(sf->face, (uint8_t)(face - 1))) p->oom = true;
                return;
            }
            char nm[64]; snprintf(nm, sizeof nm, "%s", f[0]); upcase(nm);
            int o = find_set(p, nm, true);
            if (o < 0) { p->bad_lines++; return; }
            for (size_t j = 0; j < p->sets.a[o].ids.n; j++)
                if (!cv_push(sf->elem, p->sets.a[o].ids.a[j]) || !cv_push(sf->face, (uint8_t)(face - 1))) p->oom = true;
            return;
        }
        case S_HEADING:
            if (!p->d->heading[0]) trim_to(p->d->heading, sizeof p->d->heading, s, e);
            return;
        case S_DCOUP: {                       /* node, weight */
            int n = fields(s, e, f, 2);
            if (n < 1 || !each_node(p, f[0], link_add_node, (void*)(intptr_t)(p->links.n - 1))) p->bad_lines++;
            return;
        }
        case S_EQUATION: {                    /* N, then N terms of (node, dof, coef), 4 per line at most */
            int n = fields(s, e, f, 12);
            uint32_t v;
            if (p->eq_left == 0) {
                if (n < 1 || !to_u32(f[0], &v) || v < 1 || v > 1000) { p->bad_lines++; return; }
                p->eq_left = (int)v;
                if (!new_link(p, CV_LINK_EQUATION, "")) return;
                return;
            }
            vlink* l = &p->links.a[p->links.n - 1];
            for (int k = 0; k + 2 < n + 0 && p->eq_left > 0; k += 3) {
                if (!to_u32(f[k], &v)) { p->bad_lines++; p->eq_left = 0; return; }
                if (!l->l.ref) l->l.ref = v;
                if (!cv_push(l->nodes, v)) p->oom = true;
                p->eq_left--;
            }
            return;
        }
        case S_TIE: case S_CONTACT: {         /* slave surface, master surface */
            int n = fields(s, e, f, 2);
            vlink* l = &p->links.a[p->links.n - 1];
            if (n < 2) { p->bad_lines++; return; }
            l->l.surf[0] = find_surf(p, f[0]);
            l->l.surf[1] = find_surf(p, f[1]);
            if (l->l.surf[0] < 0 || l->l.surf[1] < 0) p->bad_lines++;
            p->st = S_SKIP;
            return;
        }
        case S_SPRINGDOF: {                   /* "dof" (SPRING1 / DASHPOT1) or "dof1, dof2"; stiffness lines follow */
            int n = fields(s, e, f, 2);
            uint32_t dof;
            p->st = S_SKIP;
            if (n >= 1 && to_u32(f[0], &dof) && dof >= 1 && dof <= 6) {
                section sc;
                snprintf(sc.elset, sizeof sc.elset, "%s", p->cur_elset);
                snprintf(sc.mat, sizeof sc.mat, "%u", dof);
                if (!cv_push(p->sdofs, sc)) p->oom = true;
            }
            return;
        }
        case S_TRANSFORM: {                   /* a1, a2, a3, b1, b2, b3 */
            int n = fields(s, e, f, 6);
            cv_csys cs;
            memset(&cs, 0, sizeof cs);
            cs.cyl = p->tr_cyl;
            p->st = S_SKIP;
            for (int k = 0; k < n; k++) if (f[k][0] && !to_f(f[k], &cs.a[k])) { p->bad_lines++; return; }
            if (!cv_push(p->trs, cs)) { p->oom = true; return; }
            const vset* ns = &p->sets.a[p->cset];
            for (size_t j = 0; j < ns->ids.n; j++) {
                cv_idix t = { ns->ids.a[j], (int32_t)p->trs.n - 1 };
                if (!cv_push(p->node_tr, t)) { p->oom = true; return; }
            }
            return;
        }
        case S_ORIENT: {                      /* a1..b3 (or a distribution's name), then [axis, angle] */
            vorient* o = &p->oris.a[p->oris.n - 1];
            int n = fields(s, e, f, 6);
            if (p->blk_line++ == 0) {
                for (int k = 0; k < n; k++) if (f[k][0] && !to_f(f[k], &o->cs.a[k])) return;   /* *DISTRIBUTION: stays bad */
                o->bad = false;
                return;
            }
            p->st = S_SKIP;
            uint32_t ax; double ang;
            if (o->bad || o->cs.cyl || n < 2 || !to_u32(f[0], &ax) || ax < 1 || ax > 3 || !to_f(f[1], &ang)) return;
            double Q[3][3], c[3][3];                  /* e1, e2 rotated about axis ax (orientations.f) */
            const float origin[3] = { 0, 0, 0 };
            cv_csys_axes(&o->cs, origin, Q);
            const double* q = Q[ax - 1];
            double t = ang * 3.14159265358979323846 / 180, dc = cos(t), ds = sin(t);
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) c[i][j] = (1 - dc) * q[i] * q[j] + (i == j ? dc : 0);
            c[0][1] -= ds * q[2]; c[0][2] += ds * q[1];
            c[1][0] += ds * q[2]; c[1][2] -= ds * q[0];
            c[2][0] -= ds * q[1]; c[2][1] += ds * q[0];
            for (int i = 0; i < 3; i++) {
                o->cs.a[i] = c[i][0] * Q[0][0] + c[i][1] * Q[0][1] + c[i][2] * Q[0][2];
                o->cs.a[3 + i] = c[i][0] * Q[1][0] + c[i][1] * Q[1][1] + c[i][2] * Q[1][2];
            }
            return;
        }
        case S_COMPOSITE: {                   /* thickness, , material, orientation */
            osect* o = &p->osects.a[p->osects.n - 1];
            int n = fields(s, e, f, 4);
            char nm[64] = "";
            if (n >= 4) { snprintf(nm, sizeof nm, "%s", f[3]); upcase(nm); }
            if (p->blk_line++ == 0) snprintf(o->ori, sizeof o->ori, "%s", nm);
            else if (strcmp(nm, o->ori) != 0) o->mixed = true;
            vlayer l;
            memcpy(l.ori, nm, sizeof l.ori);
            double t = 0;
            l.t = n >= 1 && to_f(f[0], &t) ? (float)t : 0.f;
            l.mat[0] = 0;
            if (n >= 3) { snprintf(l.mat, sizeof l.mat, "%s", f[2]); upcase(l.mat); }
            if (!cv_push(p->layers, l)) p->oom = true;
            else o->nlay++;
            return;
        }
        case S_ELASTIC: {                     /* the constants, then the temperature */
            static const int want[] = { 0, 2, 9, 9, 21 };
            cv_matprop* m = &p->mprop.a[p->mprop.n - 1];
            int w = want[p->el_type], n = fields(s, e, f, 8);
            for (int k = 0; k < n && p->el_n < w; k++) {
                double v = 0;
                if (!to_f(f[k], &v)) { p->st = S_SKIP; return; }
                if (p->el_n < 9) m->c[p->el_n] = (float)v;
                p->el_n++;
            }
            if (p->el_n >= w) { m->el = (uint8_t)p->el_type; p->st = S_SKIP; }
            return;
        }
        case S_PLASTIC: {                     /* stress, plastic strain, temperature */
            double v = 0;
            int n = fields(s, e, f, 3);
            if (n >= 1 && to_f(f[0], &v)) p->mprop.a[p->mprop.n - 1].sy = (float)v;
            p->st = S_SKIP;
            return;
        }
        case S_OUTREQ: {                      /* the variables, any number per line */
            static const char* key[CV_OUT_N] = { "U", "RF", "V", "VF", "S", "E", "HFL" };
            int n = fields(s, e, f, 32);
            char* sys = p->steps.a[p->steps.n - 1].f;
            for (int k = 0; k < n; k++) {
                upcase(f[k]);
                for (int j = 0; j < CV_OUT_N; j++) if (!strcmp(f[k], key[j])) sys[j] = p->outsys_c;
            }
            return;
        }
        default:
            return;
    }
}

static void parse_text(P* p, const char* data, size_t size) {
    const char* c = data;
    const char* end = data + size;
    while (c < end && !p->oom) {
        const char* nl = memchr(c, '\n', (size_t)(end - c));
        const char* s = c;
        const char* e = nl ? nl : end;
        c = nl ? nl + 1 : end;
        p->line++;
        if (e > s && e[-1] == '\r') e--;
        while (s < e && (*s == ' ' || *s == '\t')) s++;
        if (s == e) continue;
        if (e - s >= 2 && s[0] == '*' && s[1] == '*') continue;       /* comment */
        if (*s == '*') do_keyword(p, s, e);
        else do_data(p, s, e);
    }
}

/* ---- public ------------------------------------------------------------------------------ */

bool cv_inp_parse(cv_inp* d, const char* data, size_t size, cv_inp_reader rd, void* user) {
    memset(d, 0, sizeof *d);
    P p;
    memset(&p, 0, sizeof p);
    p.d = d; p.rd = rd; p.user = user; p.cset = -1; p.nodeset = -1; p.cdisc = -1;
    parse_text(&p, data, size);
    finish_element(&p);
    char msg[160];

    if (p.bad_lines) { snprintf(msg, sizeof msg, "%zu unreadable data lines skipped", p.bad_lines); cv_msg_add(&d->msgs, 0, false, msg); }
    if (p.bad_elems) { snprintf(msg, sizeof msg, "%zu elements with a wrong node count skipped", p.bad_elems); cv_msg_add(&d->msgs, 0, false, msg); }
    if (p.unknown_types) {
        snprintf(msg, sizeof msg, "%zu *ELEMENT blocks of a type that is not drawn (e.g. %s) skipped",
                 p.unknown_types, p.last_unknown);
        cv_msg_add(&d->msgs, 0, false, msg);
    }

    /* one-node springs / dashpots: the dof from *SPRING / *DASHPOT via their elset */
    for (size_t i = 0; i < p.sdofs.n; i++) {
        int si = find_set(&p, p.sdofs.a[i].elset, true);
        if (si < 0) continue;
        for (size_t k = 0; k < p.disc.n; k++) {
            cv_discrete* q = &p.disc.a[k];
            if (q->nn != 1 || q->dof) continue;
            for (size_t j = 0; j < p.sets.a[si].ids.n; j++)
                if (p.sets.a[si].ids.a[j] == q->id) { q->dof = (uint8_t)atoi(p.sdofs.a[i].mat); break; }
        }
    }
    /* mesh */
    cv_frd* f = &d->mesh;
    f->n_nodes = (uint32_t)p.node_id.n;
    f->node_id = p.node_id.a; p.node_id.a = NULL;
    f->xyz = p.xyz.a; p.xyz.a = NULL;
    size_t nd = 0, ed = 0;
    f->n_elems = (uint32_t)p.eid.n;
    f->elem_id = p.eid.a; p.eid.a = NULL;
    if (p.oom || !cv_push(p.eoff, (uint32_t)p.conn.n)) goto oom;
    f->eoff = p.eoff.a; p.eoff.a = NULL;
    f->etype = p.etype.a; p.etype.a = NULL;
    f->conn = p.conn.a; p.conn.a = NULL;
    f->emat = calloc(CV_MAX(f->n_elems, 1), sizeof(uint32_t));
    f->egrp = calloc(CV_MAX(f->n_elems, 1), sizeof(uint32_t));
    if (!f->emat || !f->egrp) goto oom;
    if (!cv_frd_build_maps(f, &nd, NULL)) goto oom;
    if (nd) { snprintf(msg, sizeof msg, "%zu duplicate node ids (first one kept)", nd); cv_msg_add(&d->msgs, 0, false, msg); }

    /* node ids -> indices; drop elements that point at missing nodes */
    {
        size_t w = 0, cw = 0, missing = 0;
        for (uint32_t e = 0; e < f->n_elems; e++) {
            uint32_t b = f->eoff[e], n = f->eoff[e + 1] - b;
            bool ok = true;
            for (uint32_t j = 0; j < n; j++) {
                uint32_t ix = cv_frd_node_index(f, f->conn[b + j]);
                if (ix == UINT32_MAX) { ok = false; break; }
                f->conn[cw + j] = ix;
            }
            if (!ok) { missing++; continue; }
            f->elem_id[w] = f->elem_id[e]; f->etype[w] = f->etype[e];
            f->eoff[w] = (uint32_t)cw;
            cw += n; w++;
        }
        f->eoff[w] = (uint32_t)cw;
        f->n_elems = (uint32_t)w;
        if (missing) { snprintf(msg, sizeof msg, "%zu elements referencing missing nodes skipped", missing); cv_msg_add(&d->msgs, 0, false, msg); }
    }
    free(f->idmap.flat); free(f->idmap.keys); free(f->idmap.vals);   /* rebuilt over the kept elements */
    free(f->emap.flat); free(f->emap.keys); free(f->emap.vals);
    memset(&f->idmap, 0, sizeof f->idmap);
    memset(&f->emap, 0, sizeof f->emap);
    if (!cv_frd_build_maps(f, NULL, &ed)) goto oom;
    if (ed) { snprintf(msg, sizeof msg, "%zu duplicate element ids (first one kept)", ed); cv_msg_add(&d->msgs, 0, false, msg); }

    /* sets: sorted, unique */
    d->sets = calloc(CV_MAX(p.sets.n, 1), sizeof *d->sets);
    if (!d->sets) goto oom;
    for (size_t i = 0; i < p.sets.n; i++) {
        vset* v = &p.sets.a[i];
        if (v->ids.n) qsort(v->ids.a, v->ids.n, sizeof(uint32_t), cv_cmp_u32);
        size_t u = 0;
        for (size_t j = 0; j < v->ids.n; j++) if (!u || v->ids.a[j] != v->ids.a[u - 1]) v->ids.a[u++] = v->ids.a[j];
        cv_set* s = &d->sets[d->nsets++];
        snprintf(s->name, sizeof s->name, "%s", v->name);
        s->is_elem = v->is_elem;
        s->ids = v->ids.a; s->n = (uint32_t)u;
        v->ids.a = NULL;
    }
    /* surfaces */
    d->surfs = calloc(CV_MAX(p.surfs.n, 1), sizeof *d->surfs);
    if (!d->surfs) goto oom;
    for (size_t i = 0; i < p.surfs.n; i++) {
        vsurf* v = &p.surfs.a[i];
        cv_surface* s = &d->surfs[d->nsurfs++];
        snprintf(s->name, sizeof s->name, "%s", v->name);
        s->elem = v->elem.a; s->face = v->face.a; s->n = (uint32_t)v->elem.n;
        s->nodes = v->nodes.a; s->nn = (uint32_t)v->nodes.n;
        v->elem.a = NULL; v->face.a = NULL; v->nodes.a = NULL;
    }
    /* links: a rigid ELSET becomes the nodes of its elements */
    d->links = calloc(CV_MAX(p.links.n, 1), sizeof *d->links);
    if (!d->links) goto oom;
    for (size_t i = 0; i < p.links.n; i++) {
        vlink* v = &p.links.a[i];
        for (size_t k = 0; k < v->elems.n; k++) {
            uint32_t e = cv_frd_elem_index(f, v->elems.a[k]);
            if (e == UINT32_MAX) continue;
            for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++)
                if (!cv_push(v->nodes, f->node_id[f->conn[j]])) goto oom;
        }
        if (v->nodes.n) qsort(v->nodes.a, v->nodes.n, sizeof(uint32_t), cv_cmp_u32);
        size_t u = 0;
        for (size_t j = 0; j < v->nodes.n; j++) if (!u || v->nodes.a[j] != v->nodes.a[u - 1]) v->nodes.a[u++] = v->nodes.a[j];
        cv_link* t = &d->links[d->nlinks++];
        *t = v->l;
        t->nodes = v->nodes.a; t->n = (uint32_t)u;
        v->nodes.a = NULL;
    }

    /* materials: from *MATERIAL order, plus any named only in a section */
    for (size_t i = 0; i < p.sects.n; i++) {
        bool have = false;
        for (size_t k = 0; k < p.mats.n; k++) have |= strcmp(p.mats.a[k], p.sects.a[i].mat) == 0;
        if (!have) {
            char* c = malloc(64);
            cv_matprop mp;
            memset(&mp, 0, sizeof mp);
            if (!c || !cv_push(p.mats, c)) { free(c); goto oom; }
            snprintf(c, 64, "%s", p.sects.a[i].mat);
            if (!cv_push(p.mprop, mp)) goto oom;
        }
    }
    d->disc = p.disc.a;     d->ndisc = (uint32_t)p.disc.n;     p.disc.a = NULL;
    d->bcs = p.bcs.a;       d->nbcs = (uint32_t)p.bcs.n;       p.bcs.a = NULL;
    d->cloads = p.cloads.a; d->ncloads = (uint32_t)p.cloads.n; p.cloads.a = NULL;
    d->dloads = p.dloads.a; d->ndloads = (uint32_t)p.dloads.n; p.dloads.a = NULL;
    d->body = p.body.a;     d->nbody = (uint32_t)p.body.n;     p.body.a = NULL;
    d->temps = p.temps.a;   d->ntemps = (uint32_t)p.temps.n;   p.temps.a = NULL;
    d->pret = p.pret.a;     d->npret = (uint32_t)p.pret.n;     p.pret.a = NULL;
    d->nmats = (int)p.mats.n;
    d->mats = malloc((size_t)CV_MAX(d->nmats, 1) * 64);
    if (!d->mats) goto oom;
    for (int k = 0; k < d->nmats; k++) snprintf(d->mats[k], 64, "%s", p.mats.a[k]);
    d->mprop = p.mprop.a; p.mprop.a = NULL;
    for (size_t i = 0; i < p.sects.n; i++) {
        const cv_set* es = cv_inp_set(d, p.sects.a[i].elset, true);
        int mi = 0;
        for (int k = 0; k < d->nmats; k++) if (strcmp(d->mats[k], p.sects.a[i].mat) == 0) mi = k + 1;
        if (!es) continue;
        for (uint32_t j = 0; j < es->n; j++) {
            uint32_t e = cv_frd_elem_index(f, es->ids[j]);
            if (e != UINT32_MAX) f->emat[e] = (uint32_t)mi;
        }
    }
    /* transforms: per node the last one that names it */
    d->transforms = p.trs.a; d->ntransforms = (int)p.trs.n; p.trs.a = NULL;
    if (p.node_tr.n) qsort(p.node_tr.a, p.node_tr.n, sizeof(cv_idix), cmp_idix);
    {
        size_t u = 0;
        for (size_t j = 0; j < p.node_tr.n; j++) {
            if (u && p.node_tr.a[u - 1].id == p.node_tr.a[j].id) u--;
            p.node_tr.a[u++] = p.node_tr.a[j];
        }
        d->node_tr = p.node_tr.a; d->nnode_tr = (uint32_t)u; p.node_tr.a = NULL;
    }
    /* orientations of the elements, through their sections */
    d->orients = malloc(CV_MAX(p.oris.n, 1) * sizeof *d->orients);
    if (!d->orients) goto oom;
    d->norients = (int)p.oris.n;
    d->orient_names = malloc(CV_MAX(p.oris.n, 1) * sizeof *d->orient_names);
    if (!d->orient_names) goto oom;
    for (size_t i = 0; i < p.oris.n; i++) {
        d->orients[i] = p.oris.a[i].cs;
        memcpy(d->orient_names[i], p.oris.a[i].name, sizeof d->orient_names[0]);
    }
    {
        CV_VEC(cv_idix) eo = { 0 };
        for (size_t i = 0; i < p.osects.n; i++) {
            const osect* o = &p.osects.a[i];
            int32_t ix = -2;                               /* -2: no orientation */
            if (!o->ori[0] && !o->mixed) continue;
            for (size_t k = 0; k < p.oris.n; k++) if (strcmp(p.oris.a[k].name, o->ori) == 0) ix = p.oris.a[k].bad ? -1 : (int32_t)k;
            if (o->mixed) ix = -1;
            const cv_set* es = cv_inp_set(d, o->elset, true);
            for (uint32_t j = 0; es && ix != -2 && j < es->n; j++) {
                cv_idix t = { es->ids[j], ix };
                if (!cv_push(eo, t)) { cv_free_vec(eo); goto oom; }
            }
        }
        if (eo.n) qsort(eo.a, eo.n, sizeof(cv_idix), cmp_idix);
        size_t u = 0;                                      /* a later section wins */
        for (size_t j = 0; j < eo.n; j++) {
            if (u && eo.a[u - 1].id == eo.a[j].id) u--;
            eo.a[u++] = eo.a[j];
        }
        d->elem_ori = eo.a; d->nelem_ori = (uint32_t)u;
    }
    {   /* composite shells: element -> its last composite section */
        CV_VEC(cv_idix) ce = { 0 };
        for (size_t i = 0; i < p.osects.n; i++) {
            const cv_set* es = p.osects.a[i].nlay ? cv_inp_set(d, p.osects.a[i].elset, true) : NULL;
            for (uint32_t j = 0; es && j < es->n; j++) {
                cv_idix t = { es->ids[j], (int32_t)i };
                if (!cv_push(ce, t)) { cv_free_vec(ce); goto oom; }
            }
        }
        if (ce.n) qsort(ce.a, ce.n, sizeof(cv_idix), cmp_idix);
        size_t u = 0;
        for (size_t j = 0; j < ce.n; j++) {
            if (u && ce.a[u - 1].id == ce.a[j].id) u--;
            ce.a[u++] = ce.a[j];
        }
        d->comps = malloc(CV_MAX(u, 1) * sizeof *d->comps);
        d->layer_ori = malloc(CV_MAX(p.layers.n, 1) * sizeof *d->layer_ori);
        d->layer_mat = malloc(CV_MAX(p.layers.n, 1) * sizeof *d->layer_mat);
        d->layer_t = malloc(CV_MAX(p.layers.n, 1) * sizeof *d->layer_t);
        if (!d->comps || !d->layer_ori || !d->layer_mat || !d->layer_t) { cv_free_vec(ce); goto oom; }
        for (size_t j = 0; j < u; j++) {
            const osect* o = &p.osects.a[ce.a[j].ix];
            d->comps[j] = (cv_layered){ ce.a[j].id, o->lay0, o->nlay };
        }
        d->ncomps = (uint32_t)u;
        for (size_t i = 0; i < p.layers.n; i++) {
            int32_t ix = -2;
            for (size_t k = 0; p.layers.a[i].ori[0] && k < p.oris.n; k++)
                if (strcmp(p.oris.a[k].name, p.layers.a[i].ori) == 0) ix = p.oris.a[k].bad ? -1 : (int32_t)k;
            if (p.layers.a[i].ori[0] && ix == -2) ix = -1;     /* names one not defined */
            d->layer_ori[i] = ix;
            d->layer_t[i] = p.layers.a[i].t;
            d->layer_mat[i] = -1;
            for (int k = 0; k < d->nmats; k++) if (strcmp(d->mats[k], p.layers.a[i].mat) == 0) d->layer_mat[i] = k;
        }
        cv_free_vec(ce);
    }
    d->outsys = malloc(CV_MAX(p.steps.n, 1) * sizeof *d->outsys);
    if (!d->outsys) goto oom;
    d->nsteps = (int)p.steps.n;
    for (size_t i = 0; i < p.steps.n; i++) memcpy(d->outsys[i], p.steps.a[i].f, CV_OUT_N);
    cv_free_vec(p.oris); cv_free_vec(p.osects); cv_free_vec(p.layers); cv_free_vec(p.steps);
    if (p.shells.n) qsort(p.shells.a, p.shells.n, sizeof(uint32_t), cv_cmp_u32);
    d->shells = p.shells.a; d->nshells = (uint32_t)p.shells.n; p.shells.a = NULL;

    for (size_t k = 0; k < p.mats.n; k++) free(p.mats.a[k]);
    cv_free_vec(p.mats); cv_free_vec(p.mprop); cv_free_vec(p.sects); cv_free_vec(p.sdofs);
    for (size_t i = 0; i < p.links.n; i++) { cv_free_vec(p.links.a[i].nodes); cv_free_vec(p.links.a[i].elems); }
    cv_free_vec(p.links);
    for (size_t i = 0; i < p.sets.n; i++) cv_free_vec(p.sets.a[i].ids);
    cv_free_vec(p.sets);
    for (size_t i = 0; i < p.surfs.n; i++) { cv_free_vec(p.surfs.a[i].elem); cv_free_vec(p.surfs.a[i].face); cv_free_vec(p.surfs.a[i].nodes); }
    cv_free_vec(p.surfs);
    return true;

oom:
    cv_free_vec(p.node_id); cv_free_vec(p.xyz); cv_free_vec(p.eid); cv_free_vec(p.eoff);
    cv_free_vec(p.conn); cv_free_vec(p.etype); cv_free_vec(p.sects);
    cv_free_vec(p.trs); cv_free_vec(p.node_tr); cv_free_vec(p.oris); cv_free_vec(p.osects); cv_free_vec(p.layers); cv_free_vec(p.steps);
    cv_free_vec(p.shells);
    cv_free_vec(p.bcs); cv_free_vec(p.cloads); cv_free_vec(p.dloads); cv_free_vec(p.disc); cv_free_vec(p.sdofs);
    cv_free_vec(p.body); cv_free_vec(p.temps); cv_free_vec(p.pret);
    for (size_t i = 0; i < p.links.n; i++) { cv_free_vec(p.links.a[i].nodes); cv_free_vec(p.links.a[i].elems); }
    cv_free_vec(p.links);
    for (size_t k = 0; k < p.mats.n; k++) free(p.mats.a[k]);
    cv_free_vec(p.mats); cv_free_vec(p.mprop);
    for (size_t i = 0; i < p.sets.n; i++) cv_free_vec(p.sets.a[i].ids);
    cv_free_vec(p.sets);
    for (size_t i = 0; i < p.surfs.n; i++) { cv_free_vec(p.surfs.a[i].elem); cv_free_vec(p.surfs.a[i].face); cv_free_vec(p.surfs.a[i].nodes); }
    cv_free_vec(p.surfs);
    cv_inp_free(d);
    cv_msg_add(&d->msgs, 0, false, "out of memory reading the deck");
    return false;
}

void cv_inp_free(cv_inp* d) {
    cv_frd_free(&d->mesh);
    free(d->mesh.msgs.a);
    for (int i = 0; i < d->nsets; i++) free(d->sets[i].ids);
    free(d->sets);
    for (int i = 0; i < d->nsurfs; i++) { free(d->surfs[i].elem); free(d->surfs[i].face); free(d->surfs[i].nodes); }
    free(d->surfs);
    free(d->bcs); free(d->cloads); free(d->dloads); free(d->disc); free(d->body); free(d->temps); free(d->pret);
    for (int i = 0; i < d->nlinks; i++) free(d->links[i].nodes);
    free(d->links);
    free(d->mats);
    free(d->transforms); free(d->node_tr); free(d->orients); free(d->orient_names); free(d->elem_ori); free(d->outsys);
    free(d->shells); free(d->comps); free(d->layer_ori); free(d->layer_mat); free(d->layer_t); free(d->mprop);
    cv_msgs keep = d->msgs;
    memset(d, 0, sizeof *d);
    d->msgs = keep;
}

const cv_set* cv_inp_set(const cv_inp* d, const char* name, bool is_elem) {
    char up[64];
    snprintf(up, sizeof up, "%s", name);
    upcase(up);
    for (int i = 0; i < d->nsets; i++)
        if (d->sets[i].is_elem == is_elem && strcmp(d->sets[i].name, up) == 0) return &d->sets[i];
    return NULL;
}
