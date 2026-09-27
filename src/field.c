/* field.c -- derived scalars and ranges. */
#include "field.h"
#include <math.h>

static bool is_stress(const char* name) {
    return strncmp(name, "STRESS", 6) == 0 || strncmp(name, "ZZS", 3) == 0;
}

int cv_field_options(const cv_field_desc* d, cv_scalar_opt* out, int max) {
    int n = 0;
    if (d->ncomp == 3 && n < max) {
        snprintf(out[n].label, sizeof out[n].label, "|%s|", d->name);
        out[n++].comp = CV_COMP_MAG;
    }
    if (d->ncomp == 6 && is_stress(d->name) && n < max) {
        snprintf(out[n].label, sizeof out[n].label, "von Mises");
        out[n++].comp = CV_COMP_MISES;
    }
    for (int c = 0; c < d->ncomp && n < max; c++) {
        snprintf(out[n].label, sizeof out[n].label, "%s", d->comp[c][0] ? d->comp[c] : "C?");
        out[n++].comp = c;
    }
    return n;
}

float cv_von_mises(const float s[6]) {
    double a = s[0] - s[1], b = s[1] - s[2], c = s[2] - s[0];
    double sh = (double)s[3] * s[3] + (double)s[4] * s[4] + (double)s[5] * s[5];
    return (float)sqrt(0.5 * (a * a + b * b + c * c) + 3.0 * sh);
}

void cv_field_scalar(const float* v, int nc, uint32_t n, int comp, float* out) {
    for (uint32_t i = 0; i < n; i++) {
        const float* r = v + (size_t)i * nc;
        if (comp >= 0) out[i] = comp < nc ? r[comp] : NAN;
        else if (comp == CV_COMP_MAG) {
            double s = 0;
            for (int c = 0; c < nc; c++) s += (double)r[c] * r[c];
            out[i] = (float)sqrt(s);                 /* NaN propagates */
        } else if (comp == CV_COMP_MISES && nc >= 6) {
            out[i] = cv_von_mises(r);
        } else {
            out[i] = NAN;
        }
    }
}

bool cv_range(const float* v, size_t n, float* mn, float* mx) {
    float lo = INFINITY, hi = -INFINITY;
    for (size_t i = 0; i < n; i++) {
        float x = v[i];
        if (x != x || isinf(x)) continue;
        if (x < lo) lo = x;
        if (x > hi) hi = x;
    }
    if (lo > hi) { *mn = 0; *mx = 1; return false; }
    *mn = lo; *mx = hi;
    return true;
}

void cv_center_zero(float* mn, float* mx) {
    if (*mn < 0 && *mx > 0) {
        float m = CV_MAX(-*mn, *mx);
        *mn = -m; *mx = m;
    }
}

void cv_elem_mean(const cv_frd* f, const float* nodal, float* out) {
    for (uint32_t e = 0; e < f->n_elems; e++) {
        uint32_t b = f->eoff[e], n = f->eoff[e + 1] - b;
        double s = 0;
        for (uint32_t j = 0; j < n; j++) { CV_ASSERT(f->conn[b + j] < f->n_nodes); s += nodal[f->conn[b + j]]; }
        out[e] = n ? (float)(s / n) : NAN;
    }
}

float cv_auto_deform(float peak, float diag) {
    if (!(peak > 0) || !(diag > 0)) return 1.f;
    float s = 0.1f * diag / peak;
    return s > 1.f ? s : 1.f;
}
