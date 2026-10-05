/* inp_localsys.c -- results in local systems: the *TRANSFORM / *ORIENTATION maths
   that turns .frd and .dat values CalculiX wrote in a local system back to global
   (cv_localsys_*), including the system of an expanded or composite shell. */
#include "inp.h"
#include "gauss.h"      /* cv_frd_node_pos, cv_ip_param, cv_shape */
#include <stdint.h>
#include <math.h>
#include "inp_int.h"

/* the system CalculiX gives an expanded shell element (gen3dfrom2d.f): at the point
   (xi, eta) = (0, 0), e3 the normal, e1 the orientation's (or the global) x on the
   shell plane -- its z when x is normal to the shell -- and e2 = e3 x e1 */
static bool shell_axes(const cv_inp* d, uint32_t de, const cv_csys* cs, double Q[3][3]) {
    static const double w3[] = { 1, 0, 0 }, a3[] = { -1, 1, 0 }, b3[] = { -1, 0, 1 };
    static const double w6[] = { 1, 0, 0, 0, 0, 0 }, a6[] = { -3, -1, 0, 4, 0, 0 }, b6[] = { -3, 0, -1, 0, 0, 4 };
    static const double w4[] = { .25, .25, .25, .25 }, a4[] = { -.25, .25, .25, -.25 }, b4[] = { -.25, -.25, .25, .25 };
    static const double w8[] = { -.25, -.25, -.25, -.25, .5, .5, .5, .5 }, a8[] = { 0, 0, 0, 0, 0, .5, 0, -.5 },
                        b8[] = { 0, 0, 0, 0, -.5, 0, .5, 0 };
    const cv_frd* m = &d->mesh;
    uint32_t o = m->eoff[de], nn = m->eoff[de + 1] - o;
    const double *w, *a, *b;
    if (nn == 3) { w = w3; a = a3; b = b3; }
    else if (nn == 6) { w = w6; a = a6; b = b6; }
    else if (nn == 4) { w = w4; a = a4; b = b4; }
    else if (nn == 8) { w = w8; a = a8; b = b8; }
    else return false;
    double p[3] = { 0, 0, 0 }, t1[3] = { 0, 0, 0 }, t2[3] = { 0, 0, 0 }, n[3], A[3][3];
    for (uint32_t k = 0; k < nn; k++) {
        const float* x = m->xyz + 3 * (size_t)m->conn[o + k];
        for (int c = 0; c < 3; c++) { p[c] += w[k] * x[c]; t1[c] += a[k] * x[c]; t2[c] += b[k] * x[c]; }
    }
    n[0] = t1[1] * t2[2] - t1[2] * t2[1];
    n[1] = t1[2] * t2[0] - t1[0] * t2[2];
    n[2] = t1[0] * t2[1] - t1[1] * t2[0];
    double l = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!(l > 0)) return false;
    for (int c = 0; c < 3; c++) n[c] /= l;
    if (cs) {
        const float pf[3] = { (float)p[0], (float)p[1], (float)p[2] };
        cv_csys_axes(cs, pf, A);
    } else {
        memset(A, 0, sizeof A);
        A[0][0] = A[1][1] = A[2][2] = 1;
    }
    double dd = A[0][0] * n[0] + A[0][1] * n[1] + A[0][2] * n[2], e1[3];
    const double* v = A[0];
    if (fabs(dd) > 0.999999999536) { v = A[2]; dd = A[2][0] * n[0] + A[2][1] * n[1] + A[2][2] * n[2]; }
    for (int c = 0; c < 3; c++) e1[c] = v[c] - dd * n[c];
    l = sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
    if (!(l > 0)) return false;
    for (int c = 0; c < 3; c++) { Q[0][c] = e1[c] / l; Q[2][c] = n[c]; }
    Q[1][0] = n[1] * Q[0][2] - n[2] * Q[0][1];
    Q[1][1] = n[2] * Q[0][0] - n[0] * Q[0][2];
    Q[1][2] = n[0] * Q[0][1] - n[1] * Q[0][0];
    return true;
}

#define LAYER_LOST (UINT32_MAX - 1)

void cv_elemmap_free(cv_elemmap* m) {
    free(m->f2d); free(m->comp); free(m->layer); free(m->lay_off); free(m->lay_elem); free(m->disc);
    memset(m, 0, sizeof *m);
}

/* CalculiX writes each layer of a composite shell as an element of its own, numbered
   on from the largest element number (frd.c): after the deck's elements, in the order
   of the shells, their layers in order */
bool cv_elemmap_init(cv_elemmap* m, const cv_inp* d, const cv_frd* f) {
    memset(m, 0, sizeof *m);
    uint32_t E = CV_MAX(f->n_elems, 1), nl = 0;
    for (uint32_t i = 0; i < d->ncomps; i++) nl += d->comps[i].nlay;
    m->f2d = cv_frd_match_elems(f, &d->mesh, NULL);
    m->comp = malloc(E * sizeof(uint32_t));
    m->layer = calloc(E, sizeof(uint16_t));
    m->lay_off = calloc(d->ncomps + 1, sizeof(uint32_t));
    m->lay_elem = malloc(CV_MAX(nl, 1) * sizeof(uint32_t));
    m->disc = malloc(CV_MAX(d->ndisc, 1) * sizeof(uint32_t));
    cv_idix* x = malloc(E * sizeof(cv_idix));
    if (!m->f2d || !m->comp || !m->layer || !m->lay_off || !m->lay_elem || !m->disc || !x) {
        free(x); cv_elemmap_free(m); return false;
    }
    for (uint32_t i = 0; i < d->ndisc; i++) m->disc[i] = d->disc[i].id;
    if (d->ndisc) qsort(m->disc, d->ndisc, sizeof(uint32_t), cv_cmp_u32);
    for (uint32_t i = 0; i < nl; i++) m->lay_elem[i] = UINT32_MAX;
    for (uint32_t i = 0; i < d->ncomps; i++) m->lay_off[i + 1] = m->lay_off[i] + d->comps[i].nlay;
    uint32_t nx = 0, nshown = 0;
    for (uint32_t e = 0; e < f->n_elems; e++) {
        m->comp[e] = UINT32_MAX;
        if (m->f2d[e] == UINT32_MAX) x[nx++] = (cv_idix){ f->elem_id[e], (int32_t)e };
    }
    for (uint32_t i = 0; i < d->ncomps; i++)
        if (cv_frd_elem_index(&d->mesh, d->comps[i].id) != UINT32_MAX) nshown += d->comps[i].nlay;
    if (nx && nx != nshown) {                              /* not this deck's layers */
        m->lost = d->ncomps > 0;
        for (uint32_t k = 0; d->ncomps && k < nx; k++) m->comp[x[k].ix] = LAYER_LOST;
    } else if (nx) {
        qsort(x, nx, sizeof(cv_idix), cmp_idix);
        uint32_t k = 0;
        for (uint32_t i = 0; i < d->ncomps; i++) {
            if (cv_frd_elem_index(&d->mesh, d->comps[i].id) == UINT32_MAX) continue;
            for (uint32_t l = 0; l < d->comps[i].nlay; l++, k++) {
                m->comp[x[k].ix] = i;
                m->layer[x[k].ix] = (uint16_t)l;
                m->lay_elem[m->lay_off[i] + l] = (uint32_t)x[k].ix;
            }
        }
    }
    free(x);
    return true;
}

int cv_elemmap_mat(const cv_elemmap* m, const cv_inp* d, uint32_t e, float* thick, int* layer) {
    if (thick) *thick = 0;
    if (layer) *layer = -1;
    uint32_t c = m->comp[e];
    if (c == LAYER_LOST) return -1;
    if (c != UINT32_MAX) {
        uint32_t k = d->comps[c].lay0 + m->layer[e];
        if (thick) *thick = d->layer_t[k];
        if (layer) *layer = m->layer[e];
        return d->layer_mat[k];
    }
    uint32_t de = m->f2d[e];
    return de != UINT32_MAX && d->mesh.emat ? (int)d->mesh.emat[de] - 1 : -1;
}

uint32_t cv_elemmap_dat(const cv_elemmap* m, const cv_inp* d, uint32_t id, int ip, int nip, int* lip) {
    uint32_t lo = 0, hi = d->ncomps;
    while (lo < hi) {
        uint32_t k = lo + (hi - lo) / 2;
        if (d->comps[k].id < id) lo = k + 1; else hi = k;
    }
    if (lo >= d->ncomps || d->comps[lo].id != id || !d->comps[lo].nlay) return UINT32_MAX;
    int nlay = (int)d->comps[lo].nlay, per = nip / nlay;
    if (per < 1 || ip < 1 || ip > per * nlay) return UINT32_MAX;
    *lip = (ip - 1) % per + 1;
    return m->lay_elem[m->lay_off[lo] + (uint32_t)((ip - 1) / per)];
}

int cv_elemmap_axes(const cv_elemmap* m, const cv_inp* d, const cv_frd* f, uint32_t e, const float* x, double Q[3][3]) {
    uint32_t de = m->f2d ? m->f2d[e] : UINT32_MAX, id = de != UINT32_MAX ? d->mesh.elem_id[de] : f->elem_id[e];
    if (d->ndisc && bsearch(&id, m->disc, d->ndisc, sizeof(uint32_t), cv_cmp_u32)) return -2;
    if (m->comp && m->comp[e] != UINT32_MAX) {             /* a layer: the shell's system, its orientation */
        if (m->comp[e] == LAYER_LOST) return -1;
        const cv_layered* c = &d->comps[m->comp[e]];
        int32_t o = d->layer_ori[c->lay0 + m->layer[e]];
        uint32_t se = cv_frd_elem_index(&d->mesh, c->id);
        if (o == -1 || se == UINT32_MAX) return -1;
        return shell_axes(d, se, o >= 0 ? &d->orients[o] : NULL, Q) ? 1 : -1;
    }
    int32_t o = d->nelem_ori ? find_idix(d->elem_ori, d->nelem_ori, id) : -2;
    if (o == -1) return -1;
    const cv_csys* cs = o >= 0 ? &d->orients[o] : NULL;
    if (d->nshells && bsearch(&id, d->shells, d->nshells, sizeof(uint32_t), cv_cmp_u32)) {
        return de != UINT32_MAX && shell_axes(d, de, cs, Q) ? 1 : -1;
    }
    if (!cs) {
        memset(Q, 0, 9 * sizeof(double));
        Q[0][0] = Q[1][1] = Q[2][2] = 1;
        return 0;
    }
    cv_csys_axes(cs, x, Q);
    return cs->cyl ? 2 : 1;
}

static double turn_cos(const double A[3][3], const float* B) {   /* cos of the turn from A to B */
    double t = 0;
    for (int i = 0; i < 9; i++) t += A[i / 3][i % 3] * B[i];
    return (t - 1) / 2;
}

bool cv_localsys_init(cv_localsys* L, const cv_inp* d, const cv_frd* f) {
    memset(L, 0, sizeof *L);
    uint32_t nn = CV_MAX(f->n_nodes, 1);
    L->tr = malloc(nn * sizeof(int32_t));
    L->est = calloc(nn, 1);
    /* deck elements by node list where the solver renumbered them */
    bool need = d->nnode_tr || d->nelem_ori || d->nshells;
    cv_elemmap c = { 0 };
    if (!L->tr || !L->est || (need && !cv_elemmap_init(&c, d, f))) { cv_localsys_free(L); return false; }
    const uint32_t* f2d = c.f2d;
    for (uint32_t i = 0; i < f->n_nodes; i++) L->tr[i] = -1;

    /* transforms by node id; nodes CalculiX made (expanded shells and beams) take the
       transform of the nearest node of the deck element they came from */
    if (d->nnode_tr) {
        for (uint32_t i = 0; i < f->n_nodes; i++) {
            int32_t t = find_idix(d->node_tr, d->nnode_tr, f->node_id[i]);
            if (t >= 0) { L->tr[i] = t; L->any_tr = true; }
        }
        for (uint32_t e = 0; e < f->n_elems; e++) {
            uint32_t de = f2d[e];
            if (de == UINT32_MAX) continue;
            for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) {
                uint32_t n = f->conn[j];
                if (cv_frd_node_index(&d->mesh, f->node_id[n]) != UINT32_MAX) continue;
                const float* x = f->xyz + 3 * (size_t)n;
                double best = INFINITY; uint32_t bn = UINT32_MAX;
                for (uint32_t k = d->mesh.eoff[de]; k < d->mesh.eoff[de + 1]; k++) {
                    const float* y = d->mesh.xyz + 3 * (size_t)d->mesh.conn[k];
                    double dd = 0;
                    for (int c = 0; c < 3; c++) dd += ((double)x[c] - y[c]) * ((double)x[c] - y[c]);
                    if (dd < best) { best = dd; bn = d->mesh.conn[k]; }
                }
                int32_t t = bn == UINT32_MAX ? -2 : find_idix(d->node_tr, d->nnode_tr, d->mesh.node_id[bn]);
                if (t >= 0) { L->tr[n] = t; L->any_tr = true; }
            }
        }
    }
    /* element values: the systems of the elements around each node. Pass 1 takes the
       first one, pass 2 how far the others turn from it and their sum. */
    enum { SEEN = 1, BAD = 2, VARY = 4 };
    if (!d->nelem_ori && !d->nshells) { cv_elemmap_free(&c); return true; }
    L->q = malloc((size_t)nn * 9 * sizeof(float));
    float* sum = calloc((size_t)nn * 9, sizeof(float));
    float* mincos = malloc(nn * sizeof(float));
    if (!L->q || !sum || !mincos) { free(sum); free(mincos); cv_elemmap_free(&c); cv_localsys_free(L); return false; }
    const double cos_lim = cos(CV_LOC_SPAN * 3.14159265358979323846 / 180);
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t e = 0; e < f->n_elems; e++) {
            double Q[3][3], Q0[3][3];
            bool wide = false;
            for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) {
                uint32_t n = f->conn[j];
                uint8_t* st = &L->est[n];
                int k = cv_elemmap_axes(&c, d, f, e, f->xyz + 3 * (size_t)n, Q);
                if (k == -2) break;
                if (k == -1) { *st |= BAD; continue; }
                if (k == 0) { memset(Q, 0, sizeof Q); Q[0][0] = Q[1][1] = Q[2][2] = 1; }
                if (k == 2) {                       /* CalculiX turns at each integration point */
                    *st |= VARY;
                    if (j == f->eoff[e]) memcpy(Q0, Q, sizeof Q);
                    else if (pass == 0 && !wide) {
                        float q0[9];
                        for (int i = 0; i < 9; i++) q0[i] = (float)Q0[i / 3][i % 3];
                        wide = turn_cos(Q, q0) < cos_lim;
                    }
                }
                float* q = L->q + 9 * (size_t)n;
                if (pass == 0) {
                    if (!(*st & SEEN)) {
                        for (int i = 0; i < 9; i++) q[i] = (float)Q[i / 3][i % 3];
                        mincos[n] = 1;
                        *st |= SEEN;
                    }
                    continue;
                }
                double cs = turn_cos(Q, q);
                if (cs < mincos[n]) mincos[n] = (float)cs;
                for (int i = 0; i < 9; i++) sum[9 * (size_t)n + i] += (float)Q[i / 3][i % 3];
            }
            if (wide)
                for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) L->est[f->conn[j]] |= BAD;
        }
    for (uint32_t i = 0; i < f->n_nodes; i++) {
        uint8_t st = L->est[i];
        float* q = L->q + 9 * (size_t)i;
        if (st & BAD || ((st & SEEN) && mincos[i] < cos_lim)) L->est[i] = CV_LOC_NONE;
        else if (!(st & SEEN)) L->est[i] = CV_LOC_GLOBAL;
        else if (mincos[i] > 1 - 1e-8 && !(st & VARY)) {     /* one system */
            bool id = true;
            for (int k = 0; k < 9; k++) id = id && fabsf(q[k] - (k % 4 == 0)) < 1e-6f;
            L->est[i] = id ? CV_LOC_GLOBAL : CV_LOC_EXACT;
        } else {                                    /* their mean, made orthonormal again */
            const float* s = sum + 9 * (size_t)i;
            double r[3][3], l;
            for (int k = 0; k < 9; k++) r[k / 3][k % 3] = s[k];
            l = sqrt(r[0][0] * r[0][0] + r[0][1] * r[0][1] + r[0][2] * r[0][2]);
            for (int k = 0; k < 3; k++) r[0][k] /= l;
            l = r[1][0] * r[0][0] + r[1][1] * r[0][1] + r[1][2] * r[0][2];
            for (int k = 0; k < 3; k++) r[1][k] -= l * r[0][k];
            l = sqrt(r[1][0] * r[1][0] + r[1][1] * r[1][1] + r[1][2] * r[1][2]);
            for (int k = 0; k < 3; k++) r[1][k] /= l;
            r[2][0] = r[0][1] * r[1][2] - r[0][2] * r[1][1];
            r[2][1] = r[0][2] * r[1][0] - r[0][0] * r[1][2];
            r[2][2] = r[0][0] * r[1][1] - r[0][1] * r[1][0];
            for (int k = 0; k < 9; k++) q[k] = (float)r[k / 3][k % 3];
            L->est[i] = CV_LOC_NEAR;
        }
        if (L->est[i] != CV_LOC_GLOBAL) L->any_ori = true;
    }
    free(sum); free(mincos); cv_elemmap_free(&c);
    return true;
}

/* which output variable decides the system of a .frd block, and how:
   nodal blocks are local unless 'G' (frdvector), element blocks only when 'L' */
static int block_var(const char* name, bool* nodal, bool* turnable) {
    static const struct { const char* name; int var; bool nodal, turnable; } k[] = {
        { "DISP", CV_OUT_U, true, true }, { "DISPI", CV_OUT_U, true, true },
        { "FORC", CV_OUT_RF, true, true }, { "FORCI", CV_OUT_U, true, true },   /* sic: U's flag */
        { "VELO", CV_OUT_V, true, true }, { "V3DF", CV_OUT_VF, true, true },
        { "STRESS", CV_OUT_S, false, true }, { "STRESSI", CV_OUT_S, false, true },
        { "STRNEG", CV_OUT_S, false, true }, { "STRMID", CV_OUT_S, false, true },
        { "STRPOS", CV_OUT_S, false, true }, { "PSTRESS", CV_OUT_S, false, false },
        { "TOSTRAIN", CV_OUT_E, false, true }, { "TOSTRAII", CV_OUT_E, false, true },
        { "MESTRAIN", CV_OUT_E, false, true }, { "MESTRAII", CV_OUT_E, false, true },   /* sic: E's flag */
        { "FLUX", CV_OUT_HFL, false, true },
    };
    for (size_t i = 0; i < CV_COUNT(k); i++)
        if (strcmp(k[i].name, name) == 0) { *nodal = k[i].nodal; *turnable = k[i].turnable; return k[i].var; }
    return -1;
}

int cv_localsys_apply(const cv_localsys* L, const cv_inp* d, const cv_frd* f, int step,
                      const cv_field_desc* desc, float* vals) {
    if (!L->tr || !d->nsteps || !vals) return 0;
    bool nodal, turnable;
    int var = block_var(desc->name, &nodal, &turnable);
    if (var < 0) return 0;
    const char* sys = d->outsys[CV_MAX(1, CV_MIN(step, d->nsteps)) - 1];
    if (nodal ? sys[var] == 'G' || !L->any_tr : sys[var] != 'L' || !L->any_ori) return 0;
    int nc = desc->ncomp, tens = cv_tensor_order(desc) == 1;
    if (turnable && nc != 3 && !tens) return 0;         /* generalized DOFs: CalculiX writes nothing local */
    int r = 0;
    for (uint32_t i = 0; i < f->n_nodes; i++) {
        float* v = vals + (size_t)i * nc;
        double Q[3][3];
        if (nodal) {
            if (L->tr[i] < 0) continue;
            cv_csys_axes(&d->transforms[L->tr[i]], f->xyz + 3 * (size_t)i, Q);
        } else {
            uint8_t st = L->est[i];
            if (st == CV_LOC_GLOBAL) continue;
            if (st == CV_LOC_NONE || !turnable) {       /* systems too far apart, or unknown */
                for (int c = 0; c < nc; c++) v[c] = NAN;
                r |= CV_LOC_NAN;
                continue;
            }
            for (int k = 0; k < 9; k++) Q[k / 3][k % 3] = L->q[9 * (size_t)i + k];
            if (st == CV_LOC_NEAR) r |= CV_LOC_APPROX;
        }
        if (nc == 3) cv_vec_to_global(Q, v); else cv_ten_to_global(Q, v);
        r |= CV_LOC_TURNED;
    }
    return r;
}

void cv_localsys_free(cv_localsys* L) {
    free(L->tr); free(L->est); free(L->q);
    memset(L, 0, sizeof *L);
}

/* .dat records: CalculiX appends the name of the system it printed in, at most 20
   characters of it; a shell's is "<orientation>_shell_<element>" (gen3dfrom2d.f) */
static int dat_orient(const cv_inp* d, const char* key, bool* shell) {
    char k[24];
    snprintf(k, sizeof k, "%s", key); upcase(k);
    size_t l = strlen(k);
    *shell = l >= 7 && strcmp(k + l - 7, "_SHELL_") == 0;
    if (*shell) { k[l - 7] = 0; if (!k[0]) return -1; }      /* a shell without orientation */
    int found = -2;
    for (int i = 0; i < d->norients; i++) {
        bool eq = *shell ? strcmp(d->orient_names[i], k) == 0 : strncmp(d->orient_names[i], k, 20) == 0;
        if (!eq) continue;
        if (found >= 0) return -3;                  /* two names alike in 20 characters */
        found = i;
    }
    return found;
}

int cv_localsys_dat(const cv_inp* d, const cv_frd* f, cv_dat_block* b) {
    if (!b->sys || (b->ncomp != 6 && b->ncomp != 3)) return 0;
    int32_t* ko = malloc(CV_MAX(b->nsys, 1) * sizeof(int32_t));
    bool* ks = malloc(CV_MAX(b->nsys, 1));
    if (!ko || !ks) { free(ko); free(ks); return 0; }
    for (int k = 0; k < b->nsys; k++) ko[k] = dat_orient(d, b->sysname[k], &ks[k]);
    int r = 0;
    uint32_t run0 = 0;                              /* first record of this element */
    for (uint32_t i = 0; i < b->n; i++) {
        if (i == 0 || b->elem[i] != b->elem[i - 1]) run0 = i;
        if (!b->sys[i]) continue;
        float* v = b->vals + (size_t)i * b->ncomp;
        uint32_t id = b->elem[i];
        int k = b->sys[i] - 1, o = ko[k];
        bool shell = d->nshells && bsearch(&id, d->shells, d->nshells, sizeof(uint32_t), cv_cmp_u32);
        if (o < -1 || (o == -1 && !ks[k])) {        /* name cut short: the element's section */
            o = d->nelem_ori ? find_idix(d->elem_ori, d->nelem_ori, id) : -2;
            if (o == -2) o = -1;
            else if (o == -1) o = -3;
        }
        double Q[3][3];
        bool ok = o >= -1;
        if (ok && shell) {
            uint32_t de = cv_frd_elem_index(&d->mesh, id);
            ok = de != UINT32_MAX && shell_axes(d, de, o >= 0 ? &d->orients[o] : NULL, Q);
        } else if (ok && o >= 0) {
            float x[3] = { 0, 0, 0 };
            if (d->orients[o].cyl) {                /* at the integration point */
                uint32_t e = cv_frd_elem_index(f, id), nip = 1;
                while (run0 + nip < b->n && b->elem[run0 + nip] == id) nip++;
                double xi[3], N[20], s[3] = { 0, 0, 0 };
                int t = e != UINT32_MAX ? f->etype[e] : 0;
                uint32_t nn = e != UINT32_MAX ? f->eoff[e + 1] - f->eoff[e] : 0;
                ok = nn && nn <= 20 && cv_ip_param(t, (int)nip, b->ip[i] - 1, xi) && cv_shape(t, (int)nn, xi, N);
                for (uint32_t j = 0; ok && j < nn; j++) {
                    const float* y = f->xyz + 3 * (size_t)f->conn[f->eoff[e] + (uint32_t)cv_frd_node_pos(t, (int)nn, (int)j)];
                    for (int c = 0; c < 3; c++) s[c] += N[j] * y[c];
                }
                for (int c = 0; c < 3; c++) x[c] = (float)s[c];
            }
            if (ok) cv_csys_axes(&d->orients[o], x, Q);
        } else ok = false;                          /* local, but in a system not in the deck */
        if (!ok) {
            for (int c = 0; c < b->ncomp; c++) v[c] = NAN;
            r |= CV_LOC_NAN;
            continue;
        }
        if (b->ncomp == 3) cv_vec_to_global(Q, v);
        else {                                      /* .dat: xy xz yz */
            float t[6] = { v[0], v[1], v[2], v[3], v[5], v[4] };
            cv_ten_to_global(Q, t);
            v[0] = t[0]; v[1] = t[1]; v[2] = t[2]; v[3] = t[3]; v[4] = t[5]; v[5] = t[4];
        }
        b->sys[i] = 0;
        r |= CV_LOC_TURNED;
    }
    free(ko); free(ks);
    return r;
}

/* the *TRANSFORM of a node id: index into d->transforms, -1 none */
int cv_inp_node_transform(const cv_inp* d, uint32_t node) {
    return d->nnode_tr ? (int)find_idix(d->node_tr, d->nnode_tr, node) : -1;
}
