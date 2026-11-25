/* selstats.c -- the selection's totals per kind (selstats.h). */
#include "selstats.h"
#include "integ.h"
#include "pickinfo.h"
#include <math.h>
#include <string.h>

typedef struct { double w, wv, v; uint32_t c; } acc_t;   /* weights, weighted sum, plain sum, values */

static void start(cv_selstat* o, uint32_t n) {
    memset(o, 0, sizeof *o);
    o->n = n; o->vmin = o->vmax = NAN; o->avg = NAN;
}

static void add(cv_selstat* o, acc_t* a, double w, float v) {
    if (w == w && w > 0) o->size += w; else w = 0;
    if (v != v) return;
    o->nval++;
    if (o->vmin != o->vmin || v < o->vmin) o->vmin = v;
    if (o->vmax != o->vmax || v > o->vmax) o->vmax = v;
    a->w += w; a->wv += w * v; a->v += v; a->c++;
}

static void finish(cv_selstat* o, const acc_t* a) {
    if (a->c) o->avg = a->w > 0 ? a->wv / a->w : a->v / a->c;
}

static float mean(const float* nval, const uint32_t* nd, int n) {
    if (!nval || n <= 0) return NAN;
    double s = 0;
    for (int i = 0; i < n; i++) s += nval[nd[i]];
    return (float)(s / n);
}

void cv_selstat_nodes(const float* nval, const uint32_t* nd, uint32_t n, cv_selstat* o) {
    acc_t a = { 0 };
    start(o, n);
    for (uint32_t k = 0; k < n; k++) add(o, &a, 0, nval ? nval[nd[k]] : NAN);
    finish(o, &a);
}

void cv_selstat_elems(const cv_frd* f, const float* nval, const float* eval, const uint32_t* el, uint32_t n, cv_selstat* o) {
    acc_t a = { 0 };
    start(o, n);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t e = el[k];
        if (e >= f->n_elems) continue;
        uint32_t b = f->eoff[e], m = f->eoff[e + 1] - b;
        double x[20][3], V = NAN;
        if (m <= 20) {
            for (uint32_t i = 0; i < m; i++) for (int q = 0; q < 3; q++) x[i][q] = f->xyz[3 * (size_t)f->conn[b + i] + q];
            V = cv_elem_volume(f->etype[e], (int)m, (const double (*)[3])x);
        }
        add(o, &a, V, eval ? eval[e] : mean(nval, f->conn + b, (int)m));
    }
    finish(o, &a);
}

void cv_selstat_faces(const cv_frd* f, const float* nval, const float* eval, const uint32_t* fc, uint32_t n, cv_selstat* o) {
    acc_t a = { 0 };
    start(o, n);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t e = fc[k] >> 3, r[8];
        if (e >= f->n_elems) continue;
        int m = cv_face_ring(f, e, (int)(fc[k] & 7), r);
        if (!m) continue;
        float p[24];
        for (int i = 0; i < m; i++) for (int q = 0; q < 3; q++) p[3 * i + q] = f->xyz[3 * (size_t)r[i] + q];
        add(o, &a, cv_poly_area(p, m, NULL), eval ? eval[e] : mean(nval, r, m));
    }
    finish(o, &a);
}

void cv_selstat_edges(const cv_frd* f, const float* nval, const uint32_t* ed, uint32_t n, cv_selstat* o) {
    acc_t a = { 0 };
    start(o, n);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = ed[2 * k], j = ed[2 * k + 1];
        if (i >= f->n_nodes || j >= f->n_nodes) continue;
        double l = 0;
        for (int q = 0; q < 3; q++) { double d = (double)f->xyz[3 * (size_t)j + q] - f->xyz[3 * (size_t)i + q]; l += d * d; }
        add(o, &a, sqrt(l), mean(nval, ed + 2 * k, 2));
    }
    finish(o, &a);
}
