/* datip.c -- one element's integration-point rows (datip.h). */
#include "datip.h"
#include "field.h"
#include <math.h>
#include <string.h>

/* von Mises applies where the field's options offer it: a stress, 6 components */
static bool has_mises(const cv_dat_block* b) {
    if (b->ncomp != 6) return false;
    cv_field_desc d;
    memset(&d, 0, sizeof d);
    snprintf(d.name, sizeof d.name, "%s", b->name);
    d.ncomp = 6;
    for (int c = 0; c < 6; c++) snprintf(d.comp[c], sizeof d.comp[c], "%s", b->comp[c]);
    cv_scalar_opt o[CV_MAX_OPTS];
    int n = cv_field_options(&d, o, CV_MAX_OPTS);
    for (int i = 0; i < n; i++) if (o[i].comp == CV_COMP_MISES) return true;
    return false;
}

void cv_datip_mises(cv_datip* t) {
    for (int r = 0; r < t->n; r++) t->vm[r] = t->mises ? cv_von_mises(t->v[r]) : NAN;
}

int cv_datip_rows(const cv_dat_block* b, uint32_t elem, cv_datip* t) {
    memset(t, 0, sizeof *t);
    if (!b || b->is_coord) return 0;
    t->ncomp = CV_MIN(b->ncomp, CV_DAT_MAX_COMP);
    t->mises = has_mises(b);
    for (uint32_t r = 0; r < b->n && t->n < CV_DATIP_MAX; r++) {
        if (b->elem[r] != elem) continue;
        int k = t->n++;                          /* insertion by point; usually in order already */
        while (k > 0 && t->ip[k - 1] > b->ip[r]) { t->ip[k] = t->ip[k - 1]; memcpy(t->v[k], t->v[k - 1], sizeof t->v[k]); k--; }
        t->ip[k] = b->ip[r];
        memset(t->v[k], 0, sizeof t->v[k]);
        memcpy(t->v[k], b->vals + (size_t)r * b->ncomp, (size_t)t->ncomp * sizeof(float));
    }
    cv_datip_mises(t);
    return t->n;
}

int cv_datip_nearest(const float* p, int n, const float x[3]) {
    int best = -1;
    double bd = INFINITY;
    for (int i = 0; i < n; i++) {
        double d = 0;
        for (int k = 0; k < 3; k++) { double q = (double)p[3 * i + k] - x[k]; d += q * q; }
        if (d < bd) { bd = d; best = i; }                /* NaN never compares less */
    }
    return best;
}
