/* app_fail.c -- failure criteria as a field (field_src 3).

   The .frd STRESS of the step, at every corner of every element: turned into the
   element's material axes (S = Q G Q^T, the axes cv_elemmap_axes gives at the
   node), converted to MPa, and put to the chosen criterion (failure.h) with the
   strength material assigned to the element's deck material. A composite layer
   is judged as a ply: embedded or outer, its thickness that of the cluster of
   neighbouring layers of the same material and orientation (LaRC in-situ
   strengths). The element shows its worst corner, a node the worst corner around
   it: worst meaning least reserve factor, so a node between two materials shows
   the weaker.

   The strength materials and their assignments live in the settings file:
     fmatN    = name|kind=ud E1=... (cv_fmat_format)
     fassignN = DECKMAT=name        ("*": every element whose deck material has none)

   A deck material with none assigned, and no "*", takes the nearest built-in
   template (fail_auto), so the field shows something from the start. Criterion
   "Auto" picks per material (cv_fc_auto); any other criterion judges the
   materials of its kind, the rest by their own auto criterion. */
#include "app_int.h"
#include "app_fail.h"
#include "units.h"
#include <math.h>

enum { FAIL_MAX = 256, ASSIGN_MAX = 512, NTHREAD = 8 };

typedef struct { char deck[64]; char fmat[48]; } assign;

static struct {
    cv_fmat  m[FAIL_MAX];
    int      n;
    assign   as[ASSIGN_MAX];
    int      nas;
    unsigned gen;                   /* bumped on every edit */
    /* results, per corner (index into frd.conn), for this key */
    cv_fres* r;
    size_t   nr;
    const cv_frd* f;
    int      step, crit;
    unsigned rgen, ugen;
    double   kS, kL;
    char     why[128];              /* materials left out, said again on a cached answer */
    uint32_t* node_at;              /* per node: its worst corner, UINT32_MAX none */
    uint32_t* elem_at;              /* per element */
} F = { .step = -1 };

static const char* const out_names[CV_FO_N] = {
    "exposure", "reserve factor", "failure index", "mode", "fracture angle", "fibre exposure", "matrix exposure",
};
static const char* const out_tips[CV_FO_N] = {
    "1 / reserve factor: the share of the strength used, linear in the load; 1 fails",
    "The factor on the whole stress state that brings it to failure; below 1 fails",
    "The criterion's own index, as its authors define it (quadratic for Hashin,\n"
    "Tsai-Wu and LaRC matrix tension); 1 fails",
    "The governing failure mode, numbered: 1 FT, 2 FC, 3 MT, 4 MC, 5 S12, 6 S23,\n"
    "7-9 Puck IFF A/B/C, 10 FKT, 11 FKC, 12 split, 13 yield, 14 fracture (probe names it)",
    "The fracture plane angle in degrees (Puck, LaRC matrix modes)",
    "The exposure of the fibre modes alone",
    "The exposure of the matrix (inter-fibre) modes alone",
};

const char* fail_out_name(int o) { return o >= 0 && o < CV_FO_N ? out_names[o] : "?"; }
const char* fail_out_tip(int o) { return o >= 0 && o < CV_FO_N ? out_tips[o] : ""; }

/* ---- the materials and their assignments ----------------------------------------- */

int fail_count(void) { return F.n; }
cv_fmat* fail_mat(int i) { return i >= 0 && i < F.n ? &F.m[i] : NULL; }
void fail_changed(void) { F.gen++; }

int fail_find(const char* name) {
    for (int i = 0; name && i < F.n; i++) if (!strcmp(F.m[i].name, name)) return i;
    return -1;
}

int fail_add(const cv_fmat* m) {
    if (F.n >= FAIL_MAX) return -1;
    cv_fmat c = *m;
    for (int k = 2; fail_find(c.name) >= 0; k++) {           /* "IM7/8552 2" */
        char base[40];
        snprintf(base, sizeof base, "%s", m->name);
        snprintf(c.name, sizeof c.name, "%s %d", base, k);
    }
    F.m[F.n] = c;
    F.gen++;
    return F.n++;
}

void fail_remove(int i) {
    if (i < 0 || i >= F.n) return;
    for (int k = 0; k < F.nas; k++)                          /* its assignments go with it */
        if (!strcmp(F.as[k].fmat, F.m[i].name)) { F.as[k] = F.as[--F.nas]; k--; }
    memmove(&F.m[i], &F.m[i + 1], (size_t)(F.n - i - 1) * sizeof F.m[0]);
    F.n--;
    F.gen++;
}

bool fail_rename(int i, const char* name) {
    if (i < 0 || i >= F.n || !name[0] || strchr(name, '|') || strchr(name, '=')) return false;
    int k = fail_find(name);
    if (k == i) return true;
    if (k >= 0) return false;
    for (int a = 0; a < F.nas; a++)
        if (!strcmp(F.as[a].fmat, F.m[i].name)) snprintf(F.as[a].fmat, sizeof F.as[a].fmat, "%s", name);
    snprintf(F.m[i].name, sizeof F.m[i].name, "%s", name);
    F.gen++;
    return true;
}

const char* fail_assigned(const char* deck_mat) {
    for (int k = 0; k < F.nas; k++) if (!strcmp(F.as[k].deck, deck_mat)) return F.as[k].fmat;
    return NULL;
}

void fail_assign(const char* deck_mat, const char* fmat) {
    int k = 0;
    while (k < F.nas && strcmp(F.as[k].deck, deck_mat)) k++;
    if (!fmat || !fmat[0]) {
        if (k < F.nas) F.as[k] = F.as[--F.nas];
    } else if (k < F.nas || F.nas < ASSIGN_MAX) {
        if (k == F.nas) F.nas++;
        snprintf(F.as[k].deck, sizeof F.as[k].deck, "%s", deck_mat);
        snprintf(F.as[k].fmat, sizeof F.as[k].fmat, "%s", fmat);
    }
    F.gen++;
}

/* ---- settings ------------------------------------------------------------------------ */

bool fail_cfg_key(const char* key) {
    const char* p = !strncmp(key, "fmat", 4) ? key + 4 : !strncmp(key, "fassign", 7) ? key + 7 : NULL;
    if (!p || !*p) return false;
    while (*p >= '0' && *p <= '9') p++;
    return !*p;
}

void fail_cfg_load(const cv_cfg* c) {
    F.n = F.nas = 0;
    for (int i = 0; i < FAIL_MAX; i++) {
        char k[16];
        snprintf(k, sizeof k, "fmat%d", i);
        const char* v = cv_cfg_get(c, k, NULL);
        if (!v) break;
        const char* bar = strchr(v, '|');
        if (!bar || bar == v) continue;
        cv_fmat m;
        memset(&m, 0, sizeof m);
        snprintf(m.name, sizeof m.name, "%.*s", (int)CV_MIN((size_t)(bar - v), sizeof m.name - 1), v);
        if (cv_fmat_parse(bar + 1, &m) && fail_find(m.name) < 0) F.m[F.n++] = m;
    }
    for (int i = 0; i < ASSIGN_MAX; i++) {
        char k[16];
        snprintf(k, sizeof k, "fassign%d", i);
        const char* v = cv_cfg_get(c, k, NULL);
        if (!v) break;
        const char* eq = strrchr(v, '=');
        if (!eq || eq == v || !eq[1]) continue;
        char deck[64];
        snprintf(deck, sizeof deck, "%.*s", (int)CV_MIN((size_t)(eq - v), sizeof deck - 1), v);
        fail_assign(deck, eq + 1);
    }
    F.gen++;
}

void fail_cfg_save(cv_cfg* c) {
    char k[16], v[1024], t[960];
    int i;
    for (i = 0; i < F.n; i++) {
        snprintf(k, sizeof k, "fmat%d", i);
        cv_fmat_format(&F.m[i], t, sizeof t);
        snprintf(v, sizeof v, "%s|%s", F.m[i].name, t);
        cv_cfg_set(c, k, v);
    }
    for (;; i++) {                                   /* the ones since deleted */
        snprintf(k, sizeof k, "fmat%d", i);
        if (!cv_cfg_get(c, k, NULL)) break;
        cv_cfg_unset(c, k);
    }
    for (i = 0; i < F.nas; i++) {
        snprintf(k, sizeof k, "fassign%d", i);
        snprintf(v, sizeof v, "%s=%s", F.as[i].deck, F.as[i].fmat);
        cv_cfg_set(c, k, v);
    }
    for (;; i++) {
        snprintf(k, sizeof k, "fassign%d", i);
        if (!cv_cfg_get(c, k, NULL)) break;
        cv_cfg_unset(c, k);
    }
}

/* ---- auto: the nearest template ------------------------------------------------------- */

/* input stress units -> MPa */
static double in_mpa(void) {
    double k = 1, off;
    int u = cv_unit_find(CV_Q_STRESS, "MPa");
    if (u < 0 || !cv_unit_conv(G.units, cv_sys_temp(G.units), G.unit_in[CV_Q_STRESS], CV_Q_STRESS, u, &k, &off)) k = 1;
    return k;
}

bool fail_auto(const cv_inp* d, int k, cv_fmat* m, char* how, size_t n) {
    if (!d || k < 0 || k >= d->nmats) return false;
    const cv_matprop* p = d->mprop ? &d->mprop[k] : NULL;
    double E1 = 0, E2 = 0, G12 = 0, nu = 0, sy = 0, kS = in_mpa();
    int kind = -1;
    if (p && p->el == CV_EL_ISO) {
        E1 = p->c[0]; nu = p->c[1]; kind = CV_MK_ISO;
    } else if (p && p->el == CV_EL_ENG) {
        E1 = p->c[0]; E2 = p->c[1]; nu = p->c[3]; G12 = p->c[6];
    } else if (p && p->el == CV_EL_ORTHO) {
        /* the normal block of the stiffness, inverted: E_i = 1 / S_ii */
        const float* c = p->c;
        double a = c[0], b = c[1], e = c[2], f = c[3], g = c[4], h = c[5];   /* 11 12 22 13 23 33 */
        double det = a * (e * h - g * g) - b * (b * h - g * f) + f * (b * g - e * f);
        if (det != 0) {
            double s11 = (e * h - g * g) / det, s22 = (a * h - f * f) / det, s12 = -(b * h - f * g) / det;
            if (s11 > 0 && s22 > 0) { E1 = 1 / s11; E2 = 1 / s22; nu = -s12 * E1; G12 = c[6]; }
        }
    }
    if (E2 > 0) kind = E1 >= 3 * E2 ? CV_MK_UD : -1;    /* fabric, quasi-isotropic: by name only */
    if (p && p->sy > 0) sy = p->sy;
    E1 *= kS; E2 *= kS; G12 *= kS; sy *= kS;
    if (E1 > 5e6) { E1 *= 1e-6; E2 *= 1e-6; G12 *= 1e-6; sy *= 1e-6; }   /* no units set, deck in Pa */
    const cv_ftemplate* t;
    cv_ftemplates(&t);
    const char* by = "name";
    int i = cv_ftemplate_by_name(d->mats[k], kind);
    if (i < 0 && kind >= 0) { i = cv_ftemplate_nearest(kind, E1, E2, nu, sy); by = "E"; }
    if (i < 0) return false;
    *m = t[i].m;
    if (kind == CV_MK_ISO && m->kind == CV_MK_ISO) {
        m->E1 = E1; m->nu12 = nu;
        if (sy > 0) { m->Sy = sy; if (m->Sut < sy) m->Sut = sy; }
    } else if (kind == CV_MK_UD && m->kind == CV_MK_UD) {
        m->E1 = E1; m->E2 = E2; m->nu12 = nu;
        if (G12 > 0) m->G12 = G12;
    }
    if (how) snprintf(how, n, "%s (%s)", t[i].m.name, by);
    return true;
}

/* ---- evaluation ------------------------------------------------------------------------ */

void fail_clear(void) {
    free(F.r); free(F.node_at); free(F.elem_at);
    F.r = NULL; F.node_at = F.elem_at = NULL;
    F.nr = 0; F.f = NULL; F.step = -1;
}

typedef struct {
    const cv_frd* f;
    const cv_inp* d;
    const cv_elemmap* map;
    const float* s;                 /* STRESS, 6+ components per node, shown units */
    int nc;
    const cv_fmat* const* mp;       /* per deck material + 1 (0: none): strength material, NULL none */
    const int* cr;                  /* and the criterion it is judged by */
    double kS, kL;
    uint32_t e0, e1;
} job;

/* the ply an element is in: thickness (mm) and position, of the cluster of
   neighbouring layers that share its material and orientation */
static cv_fply ply_of(const job* J, uint32_t e) {
    cv_fply p = { 0, CV_PLY_UD };
    if (!J->map || !J->map->comp || J->map->comp[e] >= J->d->ncomps) return p;
    const cv_layered* c = &J->d->comps[J->map->comp[e]];
    int l = J->map->layer[e], n = (int)c->nlay, lo = l, hi = l;
    const int32_t* mat = J->d->layer_mat + c->lay0;
    const int32_t* ori = J->d->layer_ori + c->lay0;
    while (lo > 0 && mat[lo - 1] == mat[l] && ori[lo - 1] == ori[l]) lo--;
    while (hi < n - 1 && mat[hi + 1] == mat[l] && ori[hi + 1] == ori[l]) hi++;
    if (lo == 0 && hi == n - 1) return p;           /* all one ply: as tested */
    double t = 0;
    for (int k = lo; k <= hi; k++) t += J->d->layer_t[c->lay0 + k];
    p.t = t * J->kL;
    p.pos = lo == 0 || hi == n - 1 ? CV_PLY_OUTER : CV_PLY_EMBEDDED;
    return p;
}

static void worker(void* arg) {
    const job* J = arg;
    const cv_frd* f = J->f;
    for (uint32_t e = J->e0; e < J->e1; e++) {
        uint32_t b = f->eoff[e], nn = f->eoff[e + 1] - b;
        cv_fres* r = F.r + b;
        int k = -1, ax = 0;
        float thick = 0;
        if (J->map) k = cv_elemmap_mat(J->map, J->d, e, &thick, NULL);
        const cv_fmat* m = J->mp[k + 1];
        int crit = J->cr[k + 1];
        bool ok = m && crit >= 0;
        double Q[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        if (ok && J->map && m->kind == CV_MK_UD && nn) {
            ax = cv_elemmap_axes(J->map, J->d, f, e, f->xyz + 3 * (size_t)f->conn[b], Q);
            ok = ax >= 0;
        }
        cv_fply ply = ok && m->kind == CV_MK_UD ? ply_of(J, e) : (cv_fply){ 0, CV_PLY_UD };
        for (uint32_t j = 0; j < nn; j++) {
            r[j] = (cv_fres){ NAN, NAN, NAN, NAN, NAN, CV_FM_NONE };
            if (!ok) continue;
            uint32_t n = f->conn[b + j];
            const float* g = J->s + (size_t)n * J->nc;
            double G6[6], s[6];
            bool bad = false;
            for (int i = 0; i < 6; i++) { G6[i] = g[i] * J->kS; bad |= g[i] != g[i]; }
            if (bad) continue;
            if (ax == 2 && j) cv_elemmap_axes(J->map, J->d, f, e, f->xyz + 3 * (size_t)n, Q);
            if (m->kind == CV_MK_UD) {
                /* frd order xx yy zz xy yz zx -> the full tensor, then Q T Q^T */
                const double T[3][3] = { { G6[0], G6[3], G6[5] }, { G6[3], G6[1], G6[4] }, { G6[5], G6[4], G6[2] } };
                double QT[3][3], L[3][3];
                for (int a = 0; a < 3; a++)
                    for (int c = 0; c < 3; c++) QT[a][c] = Q[a][0] * T[0][c] + Q[a][1] * T[1][c] + Q[a][2] * T[2][c];
                for (int a = 0; a < 3; a++)
                    for (int c = 0; c < 3; c++) L[a][c] = QT[a][0] * Q[c][0] + QT[a][1] * Q[c][1] + QT[a][2] * Q[c][2];
                s[0] = L[0][0]; s[1] = L[1][1]; s[2] = L[2][2]; s[3] = L[0][1]; s[4] = L[1][2]; s[5] = L[0][2];
            } else memcpy(s, G6, sizeof s);
            cv_fail_eval(crit, m, &ply, s, &r[j]);
        }
    }
}

/* shown stress -> MPa, model length -> mm */
static double to_unit(int q, const char* name) {
    double k1 = 1, k2 = 1, off;
    int u = cv_unit_find(q, name);
    if (!cv_unit_conv(G.units, cv_sys_temp(G.units), G.unit_in[q], q, G.unit_show[q], &k1, &off)) k1 = 1;
    if (u < 0 || !cv_unit_conv(G.units, cv_sys_temp(G.units), G.unit_in[q], q, u, &k2, &off)) k2 = 1;
    return k2 / k1;
}

static bool evaluate(char* why, size_t wn) {
    const cv_frd* f = &G.frd;
    int fi = find_field(G.step, "STRESS");
    if (fi < 0 || G.frd.steps[G.step].fields[fi].ncomp < 6) { snprintf(why, wn, "(no STRESS in this step)"); return false; }
    double kS = to_unit(CV_Q_STRESS, "MPa"), kL = 1, off;
    {   /* the shape stays in model units: model length -> mm */
        int u = cv_unit_find(CV_Q_LEN, "mm");
        if (u < 0 || !cv_unit_conv(G.units, cv_sys_temp(G.units), G.unit_in[CV_Q_LEN], CV_Q_LEN, u, &kL, &off)) kL = 1;
    }
    unsigned ugen = (unsigned)G.units * 7919u + (unsigned)G.unit_show[CV_Q_STRESS] * 131u + (unsigned)G.unit_in[CV_Q_STRESS];
    if (F.r && F.f == f && F.step == G.step && F.crit == G.fail_crit && F.rgen == F.gen && F.kS == kS && F.kL == kL && F.ugen == ugen) {
        snprintf(why, wn, "%s", F.why);
        return true;
    }
    const float* sv = cache_get(G.step, fi);
    if (!sv) { snprintf(why, wn, "(out of memory)"); return false; }
    size_t nc = f->eoff[f->n_elems];
    if (nc != F.nr || F.f != f) {
        free(F.r); free(F.node_at); free(F.elem_at);
        F.r = malloc(CV_MAX(nc, 1) * sizeof(cv_fres));
        F.node_at = malloc(CV_MAX(f->n_nodes, 1) * sizeof(uint32_t));
        F.elem_at = malloc(CV_MAX(f->n_elems, 1) * sizeof(uint32_t));
        F.nr = nc;
        if (!F.r || !F.node_at || !F.elem_at) { fail_clear(); snprintf(why, wn, "(out of memory)"); return false; }
    }
    /* which strength material each deck material uses: its own, or "*", or the
       nearest template; and by which criterion */
    const cv_inp* d = deck_get();
    const cv_elemmap* map = d ? deck_elemmap(f) : NULL;
    int nm = map ? d->nmats : 0;
    cv_fmat* am = malloc((size_t)(nm + 1) * sizeof(cv_fmat));
    const cv_fmat** mp = malloc((size_t)(nm + 1) * sizeof(cv_fmat*));
    int* cr = malloc((size_t)(nm + 1) * sizeof(int));
    if (!am || !mp || !cr) { free(am); free(mp); free(cr); snprintf(why, wn, "(out of memory)"); return false; }
    int any = fail_find(fail_assigned("*"));
    char autos[96] = "", subst[96] = "", miss[96] = "";
    for (int k = -1; k < nm; k++) {
        int i = k < 0 ? any : fail_find(fail_assigned(d->mats[k]));
        if (k >= 0 && i < 0) i = any;
        const cv_fmat* m = i >= 0 ? &F.m[i] : NULL;
        char how[64] = "";
        if (!m && k >= 0 && fail_auto(d, k, &am[k + 1], how, sizeof how)) m = &am[k + 1];
        if (!m && !map) {                        /* no deck: one material for all, assumed */
            const cv_ftemplate* t;
            cv_ftemplates(&t);
            am[0] = t[cv_ftemplate_by_name("S235", CV_MK_ISO)].m;
            m = &am[0];
            snprintf(how, sizeof how, "S235 assumed, no deck");
        }
        if (how[0]) {
            size_t o = strlen(autos);
            snprintf(autos + o, sizeof autos - o, "%s%s", o ? ", " : "", how);
        }
        int c = G.fail_crit;
        if (m && (c == CV_FC_AUTO || cv_fc_ud(c) != (m->kind == CV_MK_UD))) {
            c = cv_fc_auto(m);
            if (G.fail_crit != CV_FC_AUTO && c >= 0 && !strstr(subst, cv_fc_title(c))) {
                size_t o = strlen(subst);
                snprintf(subst + o, sizeof subst - o, "%s%s", o ? ", " : "", cv_fc_title(c));
            }
        }
        char w[96];
        if (m && (c < 0 || !cv_fmat_valid(m, c, w, sizeof w))) {
            if (c < 0) snprintf(w, sizeof w, "%s lacks strengths", m->name);
            if (!miss[0]) snprintf(miss, sizeof miss, "%s", w);
            m = NULL;
        }
        mp[k + 1] = m;
        cr[k + 1] = c;
    }
    int fit = 0;
    for (int k = -1; k < nm; k++) fit += mp[k + 1] != NULL;
    /* said: what is left out, else the templates taken, else the criteria used besides */
    why[0] = 0;
    if (miss[0]) snprintf(why, wn, "(%s)", miss);
    else if (autos[0]) snprintf(why, wn, "(auto: %s%s%s)", autos, subst[0] ? "; by " : "", subst);
    else if (subst[0]) snprintf(why, wn, "(others by %s)", subst);
    if (!fit) {
        if (!why[0]) snprintf(why, wn, "(no strength material: Strength materials...)");
        free(am); free(mp); free(cr);
        return false;
    }
    snprintf(F.why, sizeof F.why, "%s", why);
    F.f = f; F.step = G.step; F.crit = G.fail_crit; F.rgen = F.gen; F.kS = kS; F.kL = kL; F.ugen = ugen;
    job J[NTHREAD];
    cv_thread th[NTHREAD];
    bool started[NTHREAD] = { 0 };
    uint32_t E = f->n_elems, per = (E + NTHREAD - 1) / NTHREAD;
    for (int t = 0; t < NTHREAD; t++) {
        J[t] = (job){ f, d, map, sv, f->steps[G.step].fields[fi].ncomp, mp, cr, kS, kL,
                      CV_MIN(E, per * (uint32_t)t), CV_MIN(E, per * (uint32_t)(t + 1)) };
        if (t && J[t].e0 < J[t].e1) started[t] = cv_thread_start(&th[t], worker, &J[t]);
        if (t && !started[t]) worker(&J[t]);
    }
    worker(&J[0]);
    for (int t = 1; t < NTHREAD; t++) if (started[t]) cv_thread_join(&th[t]);
    free(am); free(mp); free(cr);
    /* the worst corner of each element and around each node */
    for (uint32_t i = 0; i < f->n_nodes; i++) F.node_at[i] = UINT32_MAX;
    for (uint32_t e = 0; e < E; e++) {
        F.elem_at[e] = UINT32_MAX;
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) {
            float rf = F.r[j].rf;
            if (rf != rf) continue;
            if (F.elem_at[e] == UINT32_MAX || rf < F.r[F.elem_at[e]].rf) F.elem_at[e] = j;
            uint32_t n = f->conn[j];
            if (F.node_at[n] == UINT32_MAX || rf < F.r[F.node_at[n]].rf) F.node_at[n] = j;
        }
    }
    return true;
}

static float out_of(const cv_fres* r, int o) {
    switch (o) {
    case CV_FO_EXPOSURE: return r->rf > 0 ? 1 / r->rf : r->rf == 0 ? INFINITY : NAN;
    case CV_FO_RF:       return r->rf;
    case CV_FO_FI:       return r->fi;
    case CV_FO_MODE:     return r->rf == r->rf ? (float)r->mode : NAN;
    case CV_FO_ANGLE:    return r->angle > 90 ? r->angle - 180 : r->angle;   /* a plane: -90..90 */
    case CV_FO_FIBRE:    return r->fe_f;
    case CV_FO_MATRIX:   return r->fe_m;
    }
    return NAN;
}

/* the outputs that are maxima of their own (indices, exposures) take the largest
   over the corners; the rest are those of the least reserve factor */
static bool out_max(int o) { return o == CV_FO_EXPOSURE || o == CV_FO_FI || o == CV_FO_FIBRE || o == CV_FO_MATRIX; }

bool fail_eval_field(char* why, size_t n) {
    if (!G.loaded || !G.scalar || !G.elem_val || G.frd.n_steps == 0) { snprintf(why, n, "(no results)"); return false; }
    if (!evaluate(why, n)) return false;
    const cv_frd* f = &G.frd;
    int o = G.fail_out;
    for (uint32_t i = 0; i < f->n_nodes; i++) G.scalar[i] = F.node_at[i] != UINT32_MAX ? out_of(&F.r[F.node_at[i]], o) : NAN;
    for (uint32_t e = 0; e < f->n_elems; e++) G.elem_val[e] = F.elem_at[e] != UINT32_MAX ? out_of(&F.r[F.elem_at[e]], o) : NAN;
    if (out_max(o)) {
        for (uint32_t e = 0; e < f->n_elems; e++)
            for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) {
                float v = out_of(&F.r[j], o);
                if (!(v == v)) continue;
                uint32_t nd = f->conn[j];
                if (!(G.scalar[nd] >= v)) G.scalar[nd] = v;
                if (!(G.elem_val[e] >= v)) G.elem_val[e] = v;
            }
    }
    return true;
}

bool fail_probe_text(uint32_t node, uint32_t elem, char* out, size_t n) {
    if (G.field_src != 3 || !F.r || F.f != &G.frd) return false;
    uint32_t j = G.elem_mode ? (elem < G.frd.n_elems ? F.elem_at[elem] : UINT32_MAX)
                             : (node < G.frd.n_nodes ? F.node_at[node] : UINT32_MAX);
    if (j == UINT32_MAX) { snprintf(out, n, "no strength data"); return true; }
    const cv_fres* r = &F.r[j];
    char ang[24] = "";
    if (r->angle == r->angle) snprintf(ang, sizeof ang, " at %.0f deg", r->angle > 90 ? r->angle - 180 : r->angle);
    snprintf(out, n, "%s%s  RF %.3g  FI %.3g", cv_fm_name(r->mode), ang, r->rf, r->fi);
    return true;
}
