/* inp_steps.c -- what changes from step to step besides the loads. Headless.

   Amplitudes: a load card with AMPLITUDE= is the line's value times the amplitude
   at the time on screen, in step time or, with TIME=TOTAL TIME, in total time (the
   periods of the steps before plus the step time). Without one CalculiX ramps a
   static step's new loads up to their value at the step's end, which is what is
   shown.

   *MODEL CHANGE: elements removed in a step stay out of the model in the steps
   after it until a later card adds them back. */
#include "inp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

double cv_amp_at(const cv_amp* a, double t) {
    if (!a->tabular || !a->n) return NAN;
    if (t <= a->t[0]) return a->v[0];
    for (uint32_t k = 1; k < a->n; k++) {
        if (t > a->t[k]) continue;
        double dt = (double)a->t[k] - a->t[k - 1];
        return dt > 0 ? a->v[k - 1] + (a->v[k] - a->v[k - 1]) * (t - a->t[k - 1]) / dt : a->v[k];
    }
    return a->v[a->n - 1];
}

double cv_inp_step_start(const cv_inp* d, int step) {
    double t = 0;
    for (int s = 0; s < step && s < d->nsteps; s++) t += d->stepinfo ? d->stepinfo[s].period : 1.0;
    return t;
}

double cv_inp_amp_factor(const cv_inp* d, int amp, double ts, double tt) {
    if (amp <= 0 || amp > d->namps) return 1.0;
    const cv_amp* a = &d->amps[amp - 1];
    return cv_amp_at(a, a->total ? tt : ts);
}

/* the value scaled, or left as it is when the amplitude is not evaluated */
static float at(const cv_inp* d, int amp, float v, double ts, double tt) {
    double f = cv_inp_amp_factor(d, amp, ts, tt);
    return f == f ? (float)(v * f) : v;
}

void cv_applied_at(const cv_inp* d, cv_applied* a, double ts, double tt) {
    for (uint32_t i = 0; i < a->nbcs; i++) a->bcs[i].value = at(d, a->bcs[i].amp, a->bcs[i].value, ts, tt);
    for (uint32_t i = 0; i < a->ncloads; i++) a->cloads[i].value = at(d, a->cloads[i].amp, a->cloads[i].value, ts, tt);
    for (uint32_t i = 0; i < a->ndloads; i++) a->dloads[i].value = at(d, a->dloads[i].amp, a->dloads[i].value, ts, tt);
    for (uint32_t i = 0; i < a->nbody; i++) a->body[i].value = at(d, a->body[i].amp, a->body[i].value, ts, tt);
    for (uint32_t i = 0; i < a->ntemps; i++) a->temps[i].value = at(d, a->temps[i].amp, a->temps[i].value, ts, tt);
}

static int u32_cmp(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? -1 : x > y;
}
static int64_t find_u32(const uint32_t* v, uint32_t n, uint32_t id) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t m = lo + (hi - lo) / 2;
        if (v[m] < id) lo = m + 1; else hi = m;
    }
    return lo < n && v[lo] == id ? (int64_t)lo : -1;
}

int64_t cv_inp_removed(const cv_inp* d, int step, uint32_t** ids) {
    *ids = NULL;
    /* every element any card names, then its state card by card */
    size_t n = 0;
    for (uint32_t i = 0; i < d->nmchg; i++) {
        const cv_mchange* m = &d->mchg[i];
        if (m->link >= 0 || m->step > step) continue;
        n += m->set >= 0 && m->set < d->nsets ? d->sets[m->set].n : 1;
    }
    if (!n) return 0;
    uint32_t* all = malloc(n * sizeof *all);
    if (!all) return -1;
    n = 0;
    for (uint32_t i = 0; i < d->nmchg; i++) {
        const cv_mchange* m = &d->mchg[i];
        if (m->link >= 0 || m->step > step) continue;
        if (m->set >= 0 && m->set < d->nsets) { memcpy(all + n, d->sets[m->set].ids, d->sets[m->set].n * sizeof *all); n += d->sets[m->set].n; }
        else if (m->set < 0) all[n++] = m->elem;
    }
    qsort(all, n, sizeof *all, u32_cmp);
    uint32_t u = 0;
    for (size_t k = 0; k < n; k++) if (!u || all[k] != all[u - 1]) all[u++] = all[k];
    uint8_t* out = calloc(CV_MAX(u, 1), 1);
    if (!out) { free(all); return -1; }
    for (uint32_t i = 0; i < d->nmchg; i++) {
        const cv_mchange* m = &d->mchg[i];
        if (m->link >= 0 || m->step > step) continue;
        if (m->set >= 0 && m->set < d->nsets) {
            for (uint32_t j = 0; j < d->sets[m->set].n; j++) {
                int64_t k = find_u32(all, u, d->sets[m->set].ids[j]);
                if (k >= 0) out[k] = !m->add;
            }
        } else if (m->set < 0) {
            int64_t k = find_u32(all, u, m->elem);
            if (k >= 0) out[k] = !m->add;
        }
    }
    uint32_t r = 0;
    for (uint32_t k = 0; k < u; k++) if (out[k]) all[r++] = all[k];
    free(out);
    if (!r) { free(all); return 0; }
    *ids = all;
    return r;
}

bool cv_inp_link_active(const cv_inp* d, int step, int k) {
    bool on = true;
    for (uint32_t i = 0; i < d->nmchg; i++)
        if (d->mchg[i].link == k && d->mchg[i].step <= step) on = d->mchg[i].add;
    return on;
}
