/* app_mesh.c -- mesh quality as a field (field_src 4).

   Every measure of every element, on the shape of the step on screen (the .frd
   coordinates plus its displacements at true scale, G.mesh_deformed), or on the
   undeformed mesh: a nonlinear run distorts its elements as the load grows, so
   the measures follow the step. Mode shapes have no true amplitude: their step
   is measured undeformed. Worked out on first use, again when the step changes.
   Stored in model units; lengths, areas and volumes turn to the shown length unit
   when read. Per element the value itself; per node the worst of its elements.

   CalculiX expands shells, plane stress / strain, axisymmetric and beam elements
   into solids (a shell a thin brick a thickness across) and the .frd holds those:
   measured as they are, every brick is a sliver past the aspect limit and every
   score its worst. With the deck beside the .frd, each such element is measured on
   the element of the .inp it was made from (its type, its nodes; the layers of a
   composite all on their shell), deformed by the displacement of its nodes: of a
   node the .frd holds, its own; else the mean of the nearest nodes of the expanded
   element, as the deck's glyphs move. Without the deck the expanded solids are
   measured, and a mesh that looks expanded (most bricks thin in one or two
   directions) says so. */
#include "app_int.h"
#include "app_mesh.h"
#include "units.h"
#include <math.h>

static struct {
    const cv_frd* f;
    uint32_t n;                         /* elements */
    float*   q;                         /* n * CV_MQ_N */
    uint8_t* t;                         /* per element: the type measured (the deck element's when on it) */
    int      step;                      /* the step whose shape was measured, -1 undeformed */
    unsigned dgen;                      /* deck_gen() then */
    uint32_t on_deck;                   /* elements measured on their .inp element */
    uint32_t thin;                      /* hexes and wedges thin like an expanded shell or beam */
} M;
static unsigned gen = 1;

/* measured on the step's deformed shape: asked for, the step has displacements, not a mode shape */
bool mesh_deformed_now(void) {
    return G.mesh_deformed && G.disp && G.step >= 0 && G.step < G.frd.n_steps && !G.frd.steps[G.step].modal;
}

unsigned mesh_gen(void) {
    return gen;
}

enum { NTHREAD = 8 };

/* d: displacement (model units), NULL undeformed. src: per element its deck element,
   UINT32_MAX measured as the .frd has it; dx: the deck's node positions, moved */
typedef struct { const cv_frd* f; const float* d; const cv_frd* dm; const uint32_t* src; const float* dx; uint32_t e0, e1, thin; } job;

/* a hex or wedge thin like an expanded shell (its 4 or 3 thickness edges short) or
   beam (8 edges short): edges under 0.15 of the longest */
static bool thin_solid(int t, const double (*x)[3]) {
    static const int H[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
    static const int W[9][2] = { {0,1},{1,2},{2,0},{3,4},{4,5},{5,3},{0,3},{1,4},{2,5} };
    bool hex = t == 1 || t == 4;
    if (!hex && t != 2 && t != 5) return false;
    const int (*E)[2] = hex ? H : W;
    int ne = hex ? 12 : 9, nshort = 0;
    double l[12], lmax = 0;
    for (int i = 0; i < ne; i++) {
        double s = 0;
        for (int a = 0; a < 3; a++) s += (x[E[i][1]][a] - x[E[i][0]][a]) * (x[E[i][1]][a] - x[E[i][0]][a]);
        l[i] = sqrt(s);
        if (l[i] > lmax) lmax = l[i];
    }
    for (int i = 0; i < ne; i++) nshort += l[i] < 0.15 * lmax;
    return hex ? nshort == 4 || nshort == 8 : nshort == 3;
}

static void worker(void* p) {
    job* J = p;
    const cv_frd* f = J->f;
    double x[32][3], out[CV_MQ_N];
    J->thin = 0;
    for (uint32_t e = J->e0; e < J->e1; e++) {
        float* q = M.q + (size_t)e * CV_MQ_N;
        uint32_t de = J->src ? J->src[e] : UINT32_MAX;
        int t = f->etype[e];
        uint32_t nn;
        if (de != UINT32_MAX) {                      /* the .inp element it was made from */
            const cv_frd* dm = J->dm;
            uint32_t b = dm->eoff[de];
            nn = dm->eoff[de + 1] - b;
            t = dm->etype[de];
            if (nn > 32) nn = 0;
            for (uint32_t i = 0; i < nn; i++)
                for (int a = 0; a < 3; a++) x[i][a] = J->dx[3 * (size_t)dm->conn[b + i] + a];
        } else {
            uint32_t b = f->eoff[e];
            nn = f->eoff[e + 1] - b;
            if (nn > 32) nn = 0;
            for (uint32_t i = 0; i < nn; i++)
                for (int a = 0; a < 3; a++) {
                    size_t k = 3 * (size_t)f->conn[b + i] + a;
                    x[i][a] = f->xyz[k] + (J->d ? J->d[k] : 0.f);
                }
        }
        M.t[e] = (uint8_t)t;
        cv_mq_elem(t, (int)nn, (const double (*)[3])x, out);
        for (int k = 0; k < CV_MQ_N; k++) q[k] = (float)out[k];
        if (de == UINT32_MAX && nn >= 6) J->thin += thin_solid(t, (const double (*)[3])x);
    }
}

typedef char mq_lim_fits[CV_MQ_N <= sizeof G.mq_lim / sizeof G.mq_lim[0] ? 1 : -1];

/* the user's limits into quality.c; true when they changed */
static bool sync_limits(void) {
    static float was[CV_MQ_N];
    bool changed = false;
    for (int q = 0; q < CV_MQ_N; q++)
        if (G.mq_lim[q] != was[q]) { was[q] = G.mq_lim[q]; cv_mq_set_limit(q, G.mq_lim[q]); changed = true; }
    return changed;
}

/* G.mesh_deformed switched: measured again on the shape now asked for, the field too */
void mesh_changed(void) {
    if (G.field_src == 4) app_mesh_set(G.mesh_q);
}

void mesh_limits_check(void) {
    if (!sync_limits() || !M.q) return;
    mesh_clear();                                    /* the scores were made against the old limits */
    if (G.field_src == 4) app_mesh_set(G.mesh_q);
}

/* per .frd element the .inp element CalculiX expanded it from (a shell, plane or
   beam element, measured as itself), UINT32_MAX none; NULL without a deck or none.
   *dx: the deck's node positions, moved by d (NULL: undeformed) */
static uint32_t* deck_src(const cv_frd* f, const float* d, float** dx, uint32_t* n_on) {
    *dx = NULL;
    *n_on = 0;
    const cv_inp* dk = deck_get();
    if (!dk || !dk->mesh.n_elems || f == &dk->mesh) return NULL;
    const cv_elemmap* m = deck_elemmap(f);
    if (!m) return NULL;
    const cv_frd* dm = &dk->mesh;
    uint32_t* src = malloc((size_t)CV_MAX(f->n_elems, 1) * sizeof *src);
    float* x = malloc(3 * (size_t)CV_MAX(dm->n_nodes, 1) * sizeof *x);
    uint8_t* set = calloc(CV_MAX(dm->n_nodes, 1), 1);
    if (!src || !x || !set) { free(src); free(x); free(set); return NULL; }
    memcpy(x, dm->xyz, 3 * (size_t)dm->n_nodes * sizeof *x);
    for (uint32_t e = 0; e < f->n_elems; e++) {
        src[e] = UINT32_MAX;
        if (f->etype[e] < 1 || f->etype[e] > 6) continue;              /* not a solid: as it is */
        uint32_t de = cv_elemmap_deck(m, dk, e);
        if (de == UINT32_MAX || dm->etype[de] < 7 || dm->etype[de] > 12) continue;   /* a solid in the deck too */
        src[e] = de;
        (*n_on)++;
        for (uint32_t j = dm->eoff[de]; d && j < dm->eoff[de + 1]; j++) {
            uint32_t dn = dm->conn[j];
            if (set[dn]) continue;
            set[dn] = 1;
            float* p = x + 3 * (size_t)dn;
            uint32_t fn = cv_frd_node_index(f, dm->node_id[dn]);
            if (fn != UINT32_MAX) { for (int a = 0; a < 3; a++) p[a] += d[3 * (size_t)fn + a]; continue; }
            /* not in the .frd: the mean of the expanded element's nodes nearest to it */
            double best = INFINITY, sum[3] = { 0, 0, 0 };
            for (uint32_t k = f->eoff[e]; k < f->eoff[e + 1]; k++) {
                const float* q = f->xyz + 3 * (size_t)f->conn[k];
                best = fmin(best, (double)(q[0] - p[0]) * (q[0] - p[0]) + (double)(q[1] - p[1]) * (q[1] - p[1]) + (double)(q[2] - p[2]) * (q[2] - p[2]));
            }
            double lim = best * 1.1 + 1e-12 * (double)G.diag * G.diag;
            int cnt = 0;
            for (uint32_t k = f->eoff[e]; k < f->eoff[e + 1]; k++) {
                const float* q = f->xyz + 3 * (size_t)f->conn[k];
                if ((double)(q[0] - p[0]) * (q[0] - p[0]) + (double)(q[1] - p[1]) * (q[1] - p[1]) + (double)(q[2] - p[2]) * (q[2] - p[2]) > lim) continue;
                for (int a = 0; a < 3; a++) sum[a] += d[3 * (size_t)f->conn[k] + a];
                cnt++;
            }
            for (int a = 0; a < 3 && cnt; a++) p[a] += (float)(sum[a] / cnt);
        }
    }
    free(set);
    if (!*n_on) { free(src); free(x); return NULL; }
    *dx = x;
    return src;
}

static bool compute(void) {
    sync_limits();
    const cv_frd* f = &G.frd;
    bool def = mesh_deformed_now();
    int step = def ? G.step : -1;
    if (M.q && M.f == f && M.n == f->n_elems && M.step == step && M.dgen == deck_gen()) return true;
    mesh_clear();
    M.q = malloc((size_t)CV_MAX(f->n_elems, 1) * CV_MQ_N * sizeof(float));
    M.t = malloc(CV_MAX(f->n_elems, 1));
    if (!M.q || !M.t) { mesh_clear(); return false; }
    cv_mq_init();
    float* dx;
    uint32_t on_deck;
    uint32_t* src = deck_src(f, def ? G.disp : NULL, &dx, &on_deck);
    const cv_inp* dk = deck_get();
    job J[NTHREAD];
    cv_thread th[NTHREAD];
    bool started[NTHREAD] = { 0 };
    uint32_t E = f->n_elems, per = (E + NTHREAD - 1) / NTHREAD;
    for (int t = 0; t < NTHREAD; t++) {
        J[t] = (job){ f, def ? G.disp : NULL, src ? &dk->mesh : NULL, src, dx,
                      CV_MIN(E, per * (uint32_t)t), CV_MIN(E, per * (uint32_t)(t + 1)), 0 };
        if (t && J[t].e0 < J[t].e1) started[t] = cv_thread_start(&th[t], worker, &J[t]);
        if (t && !started[t]) worker(&J[t]);
    }
    worker(&J[0]);
    for (int t = 1; t < NTHREAD; t++) if (started[t]) cv_thread_join(&th[t]);
    free(src);
    free(dx);
    M.f = f; M.n = E; M.step = step; M.dgen = deck_gen();
    M.on_deck = on_deck;
    for (int t = 0; t < NTHREAD; t++) M.thin += J[t].thin;
    gen++;
    return true;
}

void mesh_clear(void) {
    free(M.q);
    free(M.t);
    memset(&M, 0, sizeof M);
    gen++;
}

double mesh_len_scale(void) { return 1 / units_len_raw(); }

/* the type element e is measured as: its .inp element's when measured on that */
static int mtype(uint32_t e) {
    return M.t && M.f == &G.frd && e < M.n ? M.t[e] : G.frd.etype[e];
}

/* model -> shown units for measure q of an element of type t */
static double scale(int q, int t) {
    int p = cv_mq(q)->len_pow;
    if (p < 0) p = cv_mq_dim(t);
    if (!p) return 1;
    double k = 1 / units_len_raw();
    return p == 1 ? k : p == 2 ? k * k : k * k * k;
}

double mesh_value(uint32_t e, int q) {
    if (!M.q || M.f != &G.frd || e >= M.n || q < 0 || q >= CV_MQ_N) return NAN;
    double v = M.q[(size_t)e * CV_MQ_N + q];
    return v * scale(q, mtype(e));
}

static const char* len_name(void) {
    const cv_unit* u = cv_unit_get(CV_Q_LEN, cv_unit_shown(G.units, cv_sys_temp(G.units), G.unit_in[CV_Q_LEN], CV_Q_LEN, G.unit_show[CV_Q_LEN]));
    return u ? u->name : "";
}

const char* mesh_unit(int q) {
    static char buf[48];
    const cv_mq_info* in = cv_mq(q);
    if (in->deg) return "deg";
    if (!in->len_pow) return "";
    const char* l = len_name();
    if (!l[0]) return "";
    if (in->len_pow == 1) return l;
    bool d[4] = { 0 };                               /* the element dimensions present */
    for (uint32_t e = 0; e < G.frd.n_elems; e++) d[cv_mq_dim(mtype(e))] = true;
    buf[0] = 0;
    for (int k = 3; k >= 1; k--) {
        if (!d[k]) continue;
        size_t o = strlen(buf);
        snprintf(buf + o, sizeof buf - o, "%s%s%s", o ? " / " : "", l, k == 3 ? "^3" : k == 2 ? "^2" : "");
    }
    return buf;
}

bool mesh_eval_field(char* why, size_t n) {
    why[0] = 0;
    if (!G.loaded || !G.scalar || !G.elem_val) { snprintf(why, n, "(no mesh)"); return false; }
    if (!compute()) { snprintf(why, n, "(out of memory)"); return false; }
    const cv_frd* f = &G.frd;
    int q = CV_MAX(0, CV_MIN(G.mesh_q, CV_MQ_N - 1));
    bool hb = cv_mq(q)->high_bad;
    uint32_t any = 0;
    for (uint32_t i = 0; i < f->n_nodes; i++) G.scalar[i] = NAN;
    for (uint32_t e = 0; e < f->n_elems; e++) {
        float v = (float)mesh_value(e, q);
        G.elem_val[e] = v;
        if (v != v) continue;
        any++;
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) {
            float* s = &G.scalar[f->conn[j]];
            if (*s != *s || (hb ? v > *s : v < *s)) *s = v;
        }
    }
    /* a score or ratio the same to four digits everywhere (a regular mesh, a little
       deformed) is one value, not a rainbow of rounding noise */
    double lo = INFINITY, hi = -INFINITY, sum = 0;
    uint32_t cnt = 0;
    for (uint32_t e = 0; e < f->n_elems && !cv_mq(q)->len_pow; e++) {
        float v = G.elem_val[e];
        if (v != v || isinf(v)) continue;
        lo = fmin(lo, v); hi = fmax(hi, v); sum += v; cnt++;
    }
    if (cnt && hi > lo && hi - lo <= 1e-4 * fmax(fabs(lo), fabs(hi))) {
        float m = (float)(sum / cnt);
        for (uint32_t e = 0; e < f->n_elems; e++) if (isfinite(G.elem_val[e])) G.elem_val[e] = m;
        for (uint32_t i = 0; i < f->n_nodes; i++) if (isfinite(G.scalar[i])) G.scalar[i] = m;
    }
    if (!any) {
        const char* a = cv_mq_applies(q);
        if (a) snprintf(why, n, "(applies to %s)", a);
        else snprintf(why, n, "(no element it applies to)");
        return false;
    }
    return true;
}

bool mesh_stats(mesh_stat st[CV_MQ_N], mesh_info* info) {
    memset(st, 0, CV_MQ_N * sizeof *st);
    memset(info, 0, sizeof *info);
    if (!G.loaded || !compute()) return false;
    const cv_frd* f = &G.frd;
    double k = 1 / units_len_raw();
    info->nodes = f->n_nodes;
    info->elems = f->n_elems;
    info->on_deck = M.on_deck;
    info->thin = M.thin;
    /* nodes in use, the box, distinct materials and groups */
    uint8_t* used = calloc(CV_MAX(f->n_nodes, 1), 1);
    for (int a = 0; a < 3; a++) { info->box[a] = INFINITY; info->box[a + 3] = -INFINITY; }
    uint32_t mmax = 0, gmax = 0;
    for (uint32_t e = 0; e < f->n_elems; e++) {
        int t = mtype(e);
        if (t > 0 && t < 16) info->per_type[t]++;
        if (f->emat[e] > mmax) mmax = f->emat[e];
        if (f->egrp[e] > gmax) gmax = f->egrp[e];
        for (uint32_t j = f->eoff[e]; used && j < f->eoff[e + 1]; j++) used[f->conn[j]] = 1;
        float s = M.q[(size_t)e * CV_MQ_N + CV_MQ_SIZE];
        double sz = s * scale(CV_MQ_SIZE, t);
        if (s == s) switch (cv_mq_dim(t)) {
            case 3: info->volume += sz; break;
            case 2: info->area += sz; break;
            case 1: info->length += sz; break;
        }
        bool poor = false;
        for (int q = 0; q < CV_MQ_N; q++) poor |= cv_mq_poor(q, t, M.q[(size_t)e * CV_MQ_N + q]);
        info->poor_any += poor;
    }
    for (uint32_t i = 0; used && i < f->n_nodes; i++) {
        if (!used[i]) continue;
        info->used_nodes++;
        for (int a = 0; a < 3; a++) {
            double x = f->xyz[3 * (size_t)i + a] * k;
            if (x < info->box[a]) info->box[a] = x;
            if (x > info->box[a + 3]) info->box[a + 3] = x;
        }
    }
    free(used);
    uint8_t* seen = calloc((size_t)CV_MAX(mmax, gmax) + 1, 1);
    if (seen) {
        for (uint32_t e = 0; e < f->n_elems; e++) if (!seen[f->emat[e]]) { seen[f->emat[e]] = 1; info->mats++; }
        memset(seen, 0, (size_t)CV_MAX(mmax, gmax) + 1);
        for (uint32_t e = 0; e < f->n_elems; e++) if (!seen[f->egrp[e]]) { seen[f->egrp[e]] = 1; info->groups++; }
        free(seen);
    }
    /* each measure: range, mean, worst, poor count, then a histogram over the range */
    for (int q = 0; q < CV_MQ_N; q++) {
        mesh_stat* m = &st[q];
        bool hb = cv_mq(q)->high_bad;
        double sum = 0;
        m->min = INFINITY; m->max = -INFINITY; m->worst = UINT32_MAX;
        for (uint32_t e = 0; e < f->n_elems; e++) {
            float raw = M.q[(size_t)e * CV_MQ_N + q];
            if (raw != raw) continue;
            double v = raw * scale(q, mtype(e));
            m->n++;
            if (isfinite(v)) sum += v;
            if (v < m->min) { m->min = v; if (!hb) m->worst = e; }
            if (v > m->max) { m->max = v; if (hb) m->worst = e; }
            m->poor += cv_mq_poor(q, mtype(e), raw);
        }
        if (!m->n) { m->min = m->max = m->mean = NAN; continue; }
        m->mean = sum / m->n;
        double lo = m->min, w = m->max - m->min;
        for (uint32_t e = 0; e < f->n_elems && isfinite(w); e++) {
            float raw = M.q[(size_t)e * CV_MQ_N + q];
            if (raw != raw) continue;
            double v = raw * scale(q, mtype(e));
            int b = w > 0 ? (int)((v - lo) / w * 10) : 0;
            m->hist[CV_MAX(0, CV_MIN(b, 9))]++;
        }
    }
    return true;
}

bool mesh_looks_expanded(void) {
    return M.q && M.f == &G.frd && !M.on_deck && M.n && M.thin * 2 > M.n;
}

const char* mesh_note(void) {
    if (!M.q || M.f != &G.frd) return "";
    if (M.on_deck) return "on the .inp elements";
    if (mesh_looks_expanded()) return "expanded shells? open the .inp";
    return "";
}

bool mesh_probe_text(uint32_t e, char* out, size_t n) {
    if (G.field_src != 4 || !M.q || M.f != &G.frd || e >= M.n) return false;
    int t = mtype(e);
    static const int show[] = { CV_MQ_ASPECT, CV_MQ_SJAC, CV_MQ_JRATIO, CV_MQ_SKEW, CV_MQ_WARP };
    static const char* abbr[] = { "AR", "SJ", "JR", "skew", "warp" };
    size_t o = 0;
    out[0] = 0;
    double v[CV_MQ_N];
    for (int q = 0; q < CV_MQ_N; q++) v[q] = M.q[(size_t)e * CV_MQ_N + q];
    int g = cv_mq_governing(t, v);
    if (g >= 0) o += snprintf(out, n, "%sQ %.2f (%s)", cv_mq_poor(CV_MQ_CCX, t, v[CV_MQ_CCX]) ? "!" : "", v[CV_MQ_CCX], cv_mq(g)->name);
    for (size_t i = 0; i < CV_COUNT(show) && o < n; i++) {
        double v = mesh_value(e, show[i]);
        if (v != v) continue;
        o += snprintf(out + o, n - o, "%s%s%s %.3g", o ? "  " : "", cv_mq_poor(show[i], t, M.q[(size_t)e * CV_MQ_N + show[i]]) ? "!" : "",
                      abbr[i], v);
    }
    if (!out[0]) snprintf(out, n, "no quality measure applies");
    return true;
}
