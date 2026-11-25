/* elemnodal.c -- see elemnodal.h */
#include "elemnodal.h"
#include "field.h"
#include <math.h>

static bool is_elnod(const cv_step* st, int fi, const char* name) {
    return fi >= 0 && fi < st->nfields && st->fields[fi].elnod && !strcmp(st->fields[fi].name, name);
}
static int find(const cv_step* st, const char* name) {
    for (int i = 0; i < st->nfields; i++) if (is_elnod(st, i, name)) return i;
    return -1;
}

int cv_en_entry(const cv_step* st, int fi, cv_en_group* g, bool* folded) {
    cv_en_group z = { CV_EN_ONE, -1, -1, -1 };
    if (g) *g = z;
    if (folded) *folded = false;
    if (!st || fi < 0 || fi >= st->nfields || !st->fields[fi].elnod) return CV_EN_ONE;
    int s = find(st, "STRESS"), p = find(st, "STRPOS"), n = find(st, "STRNEG");
    z.stress = s; z.pos = p; z.neg = n;
    if (s >= 0 && (p >= 0 || n >= 0)) {
        if (fi == s) { z.kind = CV_EN_STRESS; if (g) *g = z; return CV_EN_STRESS; }
        if (fi == p || fi == n) { z.kind = CV_EN_STRESS; if (g) *g = z; if (folded) *folded = true; return CV_EN_ONE; }
    } else if (s < 0 && p >= 0 && n >= 0) {
        if (fi == p) { z.kind = CV_EN_SHELL; if (g) *g = z; return CV_EN_SHELL; }
        if (fi == n) { z.kind = CV_EN_SHELL; if (g) *g = z; if (folded) *folded = true; return CV_EN_ONE; }
    }
    return CV_EN_ONE;
}

const char* cv_en_title(const cv_step* st, int fi) {
    int k = cv_en_entry(st, fi, NULL, NULL);
    return k == CV_EN_STRESS ? "Stress" : k == CV_EN_SHELL ? "Shell stress" : cv_field_title(st->fields[fi].name);
}

bool cv_en_is_shell(int t) { return t >= 7 && t <= 10; }
bool cv_en_is_solid(int t) { return t >= 1 && t <= 6; }

void cv_en_normal(const cv_frd* f, uint32_t e, float n[3]) {
    n[0] = n[1] = n[2] = 0;
    if (e >= f->n_elems || !cv_en_is_shell(f->etype[e])) return;
    const uint32_t* c = f->conn + f->eoff[e];
    int k = f->etype[e] <= 8 ? 3 : 4;                    /* corners: S3 / S6 three, S4 / S8 four */
    if (f->eoff[e + 1] - f->eoff[e] < (uint32_t)k) return;
    double nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < k; i++) {                        /* Newell's sum */
        const float* a = f->xyz + 3 * c[i];
        const float* b = f->xyz + 3 * c[(i + 1) % k];
        nx += ((double)a[1] - b[1]) * ((double)a[2] + b[2]);
        ny += ((double)a[2] - b[2]) * ((double)a[0] + b[0]);
        nz += ((double)a[0] - b[0]) * ((double)a[1] + b[1]);
    }
    double l = sqrt(nx * nx + ny * ny + nz * nz);
    if (!(l > 0)) return;
    n[0] = (float)(nx / l); n[1] = (float)(ny / l); n[2] = (float)(nz / l);
}

float* cv_en_normals(const cv_frd* f) {
    float* n = malloc((size_t)CV_MAX(f->n_elems, 1) * 3 * sizeof *n);
    if (!n) return NULL;
    for (uint32_t e = 0; e < f->n_elems; e++) cv_en_normal(f, e, n + 3 * (size_t)e);
    return n;
}

const float* cv_en_source(int kind, int t, int face, int side, const float* solid, const float* pos, const float* neg) {
    if (kind == CV_EN_ONE) return solid;
    if (cv_en_is_shell(t)) {
        int s = side == CV_EN_TOP ? 0 : side == CV_EN_BOTTOM ? 1 : (face ? 1 : 0);
        return s ? neg : pos;
    }
    return cv_en_is_solid(t) ? solid : NULL;
}

/* may the elements of slots a and b at one node share a mean */
static bool together(const cv_frd* f, const uint32_t* group, const float* nrm, uint32_t ea, uint32_t eb) {
    int ta = f->etype[ea], tb = f->etype[eb];
    bool sha = cv_en_is_shell(ta), shb = cv_en_is_shell(tb);
    if (sha != shb) return false;
    if (!sha && !cv_en_is_solid(ta)) return false;       /* beams and the rest: alone */
    if (!sha && !cv_en_is_solid(tb)) return false;
    if (group && group[ea] != group[eb]) return false;
    if (sha && nrm) {
        const float *a = nrm + 3 * (size_t)ea, *b = nrm + 3 * (size_t)eb;
        float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        if (d < 0.f) return false;
    }
    return true;
}

void cv_en_average(const cv_frd* f, const cv_nr_topo* t, const uint8_t* vis, const uint32_t* group,
                   const float* nrm, const float* in, int nc, float* out) {
    const size_t ns = t->n_slots;
    for (size_t j = 0; j < ns; j++)
        for (int c = 0; c < nc; c++)
            out[j * nc + c] = (!vis || vis[t->elem[j]]) ? in[j * nc + c] : NAN;
    enum { CAP = 256 };
    for (uint32_t n = 0; n < t->n_nodes; n++) {
        uint32_t a = t->start[n], b = t->start[n + 1];
        if (b - a < 2) continue;
        uint32_t m = b - a > CAP ? CAP : b - a;
        int cl[CAP], rep[CAP], ncl = 0;
        for (uint32_t i = 0; i < m; i++) {
            uint32_t si = t->slot[a + i], ei = t->elem[si];
            cl[i] = -1;
            if (vis && !vis[ei]) continue;
            for (int k = 0; k < ncl && cl[i] < 0; k++)
                if (together(f, group, nrm, t->elem[t->slot[a + (uint32_t)rep[k]]], ei)) cl[i] = k;
            if (cl[i] < 0) { rep[ncl] = (int)i; cl[i] = ncl++; }
        }
        for (int k = 0; k < ncl; k++) {
            int cnt = 0;
            for (uint32_t i = 0; i < m; i++) cnt += cl[i] == k;
            if (cnt < 2) continue;
            for (int c = 0; c < nc; c++) {
                double sum = 0;
                int fin = 0;
                for (uint32_t i = 0; i < m; i++) {
                    if (cl[i] != k) continue;
                    float x = in[(size_t)t->slot[a + i] * nc + c];
                    if (x == x && !isinf(x)) { sum += x; fin++; }
                }
                if (!fin) continue;
                float mean = (float)(sum / fin);
                for (uint32_t i = 0; i < m; i++) {
                    if (cl[i] != k) continue;
                    float* o = &out[(size_t)t->slot[a + i] * nc + c];
                    if (*o == *o && !isinf(*o)) *o = mean;      /* what is missing stays missing */
                }
            }
        }
    }
}

bool cv_en_scalars(const cv_frd* f, const cv_nr_topo* t, const cv_en_src* s, int nc, int comp, int side, int avg,
                   const uint8_t* vis, const uint32_t* group, const float* nrm, float* front, float* back) {
    const size_t ns = cv_frd_slots(f);
    bool shells = false;
    for (uint32_t e = 0; e < f->n_elems; e++) shells |= cv_en_is_shell(f->etype[e]);
    float* buf = malloc(CV_MAX(ns, 1) * (size_t)nc * sizeof *buf);
    float* avgd = avg == CV_EN_AVG_NONE ? NULL : malloc(CV_MAX(ns, 1) * (size_t)nc * sizeof *avgd);
    if (!buf || (avg != CV_EN_AVG_NONE && !avgd)) { free(buf); free(avgd); return false; }
    for (int face = 0; face < 2; face++) {
        float* dst = face ? back : front;
        if (face && !shells) { memcpy(back, front, ns * sizeof *back); break; }
        for (uint32_t e = 0; e < f->n_elems; e++) {
            const float* src = cv_en_source(s->kind, f->etype[e], face, side, s->solid, s->pos, s->neg);
            size_t a = f->eoff[e], z = f->eoff[e + 1];
            for (size_t j = a; j < z; j++)
                for (int c = 0; c < nc; c++) buf[j * nc + c] = src ? src[j * nc + c] : NAN;
        }
        const float* use = buf;
        if (avg == CV_EN_AVG_TENSOR) { cv_en_average(f, t, vis, group, nrm, buf, nc, avgd); use = avgd; }
        cv_field_scalar(use, nc, (uint32_t)ns, comp, dst);
        if (avg == CV_EN_AVG_SCALAR) {
            cv_en_average(f, t, vis, group, nrm, dst, 1, avgd);
            memcpy(dst, avgd, ns * sizeof *dst);
        } else if (avg == CV_EN_AVG_NONE && vis) {
            for (size_t j = 0; j < ns; j++) if (!vis[t->elem[j]]) dst[j] = NAN;
        }
    }
    free(buf); free(avgd);
    return true;
}
