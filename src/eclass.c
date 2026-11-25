/* eclass.c -- element classes and categorical colours (eclass.h). */
#include "eclass.h"
#include <math.h>
#include <string.h>

enum { MAXC = 65000 };

const float cv_palette_none[3] = { 0.62f, 0.62f, 0.62f };

void cv_palette(int i, float rgb[3]) {
    static const float base[12][3] = {
        {0.12f,0.47f,0.71f}, {1.00f,0.50f,0.05f}, {0.17f,0.63f,0.17f}, {0.84f,0.15f,0.16f},
        {0.58f,0.40f,0.74f}, {0.09f,0.75f,0.81f}, {0.89f,0.47f,0.76f}, {0.74f,0.74f,0.13f},
        {0.55f,0.34f,0.29f}, {1.00f,0.85f,0.20f}, {0.00f,0.45f,0.42f}, {0.55f,0.07f,0.30f},
    };
    if (i >= 0 && i < 12) { memcpy(rgb, base[i], 3 * sizeof(float)); return; }
    double h = fmod(0.07 + (i - 12) * 0.38196601125, 1.0) * 6;   /* golden angle, as a turn */
    double s = 0.70, v = (i & 1) ? 0.72 : 0.92, f = h - floor(h);
    double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    double r[6][3] = { { v, t, p }, { q, v, p }, { p, v, t }, { p, q, v }, { t, p, v }, { v, p, q } };
    int k = (int)h % 6;
    for (int j = 0; j < 3; j++) rgb[j] = (float)r[k][j];
}

void cv_eclass_free(cv_eclass* c) {
    free(c->of_elem); free(c->name);
    memset(c, 0, sizeof *c);
}

/* cls[e]: an index into what was given, -1 none; to classes in that order, those used */
static bool compact(cv_eclass* c, const cv_frd* f, int32_t* cls, int ngiven, const char* (*nm)(const void*, int, char*), const void* u,
                    const char* none_name) {
    int32_t* map = malloc((size_t)CV_MAX(ngiven, 1) * sizeof *map);
    c->of_elem = malloc((size_t)CV_MAX(f->n_elems, 1) * sizeof *c->of_elem);
    if (!map || !c->of_elem) { free(map); return false; }
    for (int i = 0; i < ngiven; i++) map[i] = -1;
    for (uint32_t e = 0; e < f->n_elems; e++) if (cls[e] >= 0) map[cls[e]] = 0;
    int n = 0;
    for (int i = 0; i < ngiven; i++) if (map[i] == 0 && n < MAXC) map[i] = n++; else map[i] = -1;
    for (uint32_t e = 0; e < f->n_elems; e++) c->none |= cls[e] < 0 || map[cls[e]] < 0;
    c->n = n + c->none;
    c->name = calloc((size_t)CV_MAX(c->n, 1), sizeof *c->name);
    if (!c->name) { free(map); return false; }
    char t[96];
    for (int i = 0; i < ngiven; i++) if (map[i] >= 0) snprintf(c->name[map[i]], sizeof c->name[0], "%s", nm(u, i, t));
    if (c->none) snprintf(c->name[n], sizeof c->name[0], "%s", none_name);
    for (uint32_t e = 0; e < f->n_elems; e++) c->of_elem[e] = (uint16_t)(cls[e] >= 0 && map[cls[e]] >= 0 ? map[cls[e]] : n);
    free(map);
    return true;
}

static const char* set_name(const void* u, int i, char* t) { (void)t; return ((const cv_set*)u)[i].name; }

bool cv_eclass_sets(cv_eclass* c, const cv_frd* f, const cv_set* sets, int nsets) {
    memset(c, 0, sizeof *c);
    int32_t* cls = malloc((size_t)CV_MAX(f->n_elems, 1) * sizeof *cls);
    uint32_t* best = malloc((size_t)CV_MAX(f->n_elems, 1) * sizeof *best);
    if (!cls || !best) { free(cls); free(best); return false; }
    for (uint32_t e = 0; e < f->n_elems; e++) { cls[e] = -1; best[e] = UINT32_MAX; }
    for (int i = 0; i < nsets; i++) {                 /* the smallest; of equal ones the first */
        if (!sets[i].is_elem) continue;
        for (uint32_t j = 0; j < sets[i].n; j++) {
            uint32_t e = cv_frd_elem_index(f, sets[i].ids[j]);
            if (e != UINT32_MAX && sets[i].n < best[e]) { best[e] = sets[i].n; cls[e] = i; }
        }
    }
    free(best);
    bool ok = compact(c, f, cls, nsets, set_name, sets, "(no set)");
    free(cls);
    if (!ok) cv_eclass_free(c);
    return ok;
}

static const char* sect_name(const void* u, int i, char* t) {
    const cv_section* s = &((const cv_inp*)u)->sects[i];
    snprintf(t, 96, "%s: %s (%s)", s->kind, s->elset, s->mat);
    return t;
}

bool cv_eclass_sections(cv_eclass* c, const cv_frd* f, const cv_inp* d) {
    memset(c, 0, sizeof *c);
    int32_t* cls = malloc((size_t)CV_MAX(f->n_elems, 1) * sizeof *cls);
    if (!cls) return false;
    for (uint32_t e = 0; e < f->n_elems; e++) cls[e] = -1;
    for (int i = 0; i < d->nsects; i++) {             /* a later section wins */
        const cv_set* s = cv_inp_set(d, d->sects[i].elset, true);
        for (uint32_t j = 0; s && j < s->n; j++) {
            uint32_t e = cv_frd_elem_index(f, s->ids[j]);
            if (e != UINT32_MAX) cls[e] = i;
        }
    }
    bool ok = compact(c, f, cls, d->nsects, sect_name, d, "(no section)");
    free(cls);
    if (!ok) cv_eclass_free(c);
    return ok;
}
