/* app_mesh.c -- mesh quality as a field (field_src 4).

   Every measure of every element, from the undeformed .frd coordinates, worked out
   on first use and kept for the file: a mesh does not change from step to step.
   Stored in model units; lengths, areas and volumes turn to the shown length unit
   when read. Per element the value itself; per node the worst of its elements. */
#include "app_int.h"
#include "app_mesh.h"
#include "units.h"
#include <math.h>

static struct {
    const cv_frd* f;
    uint32_t n;                         /* elements */
    float*   q;                         /* n * CV_MQ_N */
} M;
static unsigned gen = 1;

unsigned mesh_gen(void) {
    return gen;
}

enum { NTHREAD = 8 };

typedef struct { const cv_frd* f; uint32_t e0, e1; } job;

static void worker(void* p) {
    const job* J = p;
    const cv_frd* f = J->f;
    double x[32][3], out[CV_MQ_N];
    for (uint32_t e = J->e0; e < J->e1; e++) {
        uint32_t b = f->eoff[e], nn = f->eoff[e + 1] - b;
        float* q = M.q + (size_t)e * CV_MQ_N;
        if (nn > 32) nn = 0;
        for (uint32_t i = 0; i < nn; i++)
            for (int a = 0; a < 3; a++) x[i][a] = f->xyz[3 * (size_t)f->conn[b + i] + a];
        cv_mq_elem(f->etype[e], (int)nn, (const double (*)[3])x, out);
        for (int k = 0; k < CV_MQ_N; k++) q[k] = (float)out[k];
    }
}

static bool compute(void) {
    const cv_frd* f = &G.frd;
    if (M.q && M.f == f && M.n == f->n_elems) return true;
    mesh_clear();
    M.q = malloc((size_t)CV_MAX(f->n_elems, 1) * CV_MQ_N * sizeof(float));
    if (!M.q) return false;
    cv_mq_init();
    job J[NTHREAD];
    cv_thread th[NTHREAD];
    bool started[NTHREAD] = { 0 };
    uint32_t E = f->n_elems, per = (E + NTHREAD - 1) / NTHREAD;
    for (int t = 0; t < NTHREAD; t++) {
        J[t] = (job){ f, CV_MIN(E, per * (uint32_t)t), CV_MIN(E, per * (uint32_t)(t + 1)) };
        if (t && J[t].e0 < J[t].e1) started[t] = cv_thread_start(&th[t], worker, &J[t]);
        if (t && !started[t]) worker(&J[t]);
    }
    worker(&J[0]);
    for (int t = 1; t < NTHREAD; t++) if (started[t]) cv_thread_join(&th[t]);
    M.f = f; M.n = E;
    gen++;
    return true;
}

void mesh_clear(void) {
    free(M.q);
    memset(&M, 0, sizeof M);
    gen++;
}

double mesh_len_scale(void) { return 1 / units_len_raw(); }

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
    return v * scale(q, G.frd.etype[e]);
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
    for (uint32_t e = 0; e < G.frd.n_elems; e++) d[cv_mq_dim(G.frd.etype[e])] = true;
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
    if (!any) { snprintf(why, n, "(no element it applies to)"); return false; }
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
    /* nodes in use, the box, distinct materials and groups */
    uint8_t* used = calloc(CV_MAX(f->n_nodes, 1), 1);
    for (int a = 0; a < 3; a++) { info->box[a] = INFINITY; info->box[a + 3] = -INFINITY; }
    uint32_t mmax = 0, gmax = 0;
    for (uint32_t e = 0; e < f->n_elems; e++) {
        int t = f->etype[e];
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
            double v = raw * scale(q, f->etype[e]);
            m->n++;
            if (isfinite(v)) sum += v;
            if (v < m->min) { m->min = v; if (!hb) m->worst = e; }
            if (v > m->max) { m->max = v; if (hb) m->worst = e; }
            m->poor += cv_mq_poor(q, f->etype[e], raw);
        }
        if (!m->n) { m->min = m->max = m->mean = NAN; continue; }
        m->mean = sum / m->n;
        double lo = m->min, w = m->max - m->min;
        for (uint32_t e = 0; e < f->n_elems && isfinite(w); e++) {
            float raw = M.q[(size_t)e * CV_MQ_N + q];
            if (raw != raw) continue;
            double v = raw * scale(q, f->etype[e]);
            int b = w > 0 ? (int)((v - lo) / w * 10) : 0;
            m->hist[CV_MAX(0, CV_MIN(b, 9))]++;
        }
    }
    return true;
}

bool mesh_probe_text(uint32_t e, char* out, size_t n) {
    if (G.field_src != 4 || !M.q || M.f != &G.frd || e >= M.n) return false;
    int t = G.frd.etype[e];
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
