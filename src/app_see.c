/* app_see.c -- see-through faces: the model's opacity (View > Layers) times each
   element set's (Groups > element set > right click), so what lies inside an
   assembly -- a contact zone, a tied face, the elements in contact -- can be
   seen through the parts around it. The skin's triangles go to the renderer in
   runs of one opacity (and, coloured by group, one group); the opaque runs are
   the faces as usual, the others a glass shell drawn over everything. */
#include "app.h"
#include "app_int.h"
#include <math.h>

enum { LEVELS = 64 };      /* opacities kept apart: 1/64 steps */

/* skin triangle t turns clockwise seen from outside its solid element (CalculiX's face
   order is not the same way round for every type): to be turned for the culling. A
   shell's has no outside: as it is. */
static bool inward(size_t t) {
    const cv_frd* f = &G.frd;
    uint32_t e = G.skin.tri_elem[t];
    if (f->etype[e] < 1 || f->etype[e] > 6) return false;
    double c[3] = { 0, 0, 0 };
    uint32_t b = f->eoff[e], n = f->eoff[e + 1] - b;
    for (uint32_t j = 0; j < n; j++) for (int k = 0; k < 3; k++) c[k] += f->xyz[3 * (size_t)f->conn[b + j] + k] / n;
    const float* p0 = f->xyz + 3 * (size_t)G.skin.tri[3 * t];
    const float* p1 = f->xyz + 3 * (size_t)G.skin.tri[3 * t + 1];
    const float* p2 = f->xyz + 3 * (size_t)G.skin.tri[3 * t + 2];
    double u[3], v[3], w[3];
    for (int k = 0; k < 3; k++) { u[k] = p1[k] - p0[k]; v[k] = p2[k] - p0[k]; w[k] = p0[k] - c[k]; }
    double nx = u[1] * v[2] - u[2] * v[1], ny = u[2] * v[0] - u[0] * v[2], nz = u[0] * v[1] - u[1] * v[0];
    return nx * w[0] + ny * w[1] + nz * w[2] < 0;
}

/* per skin triangle: turned inward (1), worked out once per skin (app_see_skin drops it) */
static uint8_t* g_in;
static size_t g_in_n;
void app_see_skin(void) { free(g_in); g_in = NULL; g_in_n = 0; }

static bool inward_cached(size_t t) {
    if (!g_in || g_in_n != G.skin.n_tri) {
        free(g_in);
        g_in_n = 0;
        if (!(g_in = malloc(CV_MAX(G.skin.n_tri, 1)))) return inward(t);
        for (size_t i = 0; i < G.skin.n_tri; i++) g_in[i] = inward(i);
        g_in_n = G.skin.n_tri;
    }
    return g_in[t];
}

void app_see_refresh(void) {
    const cv_frd* f = &G.frd;
    float* ea = NULL;
    uint32_t* key = NULL;
    uint32_t *cnt = NULL, *ids = NULL;
    cv_see_run* runs = NULL;
    if (!G.loaded || !G.skin.n_tri || !f->n_elems) goto clear;
    float ma = fminf(fmaxf(G.model_alpha, 0.f), 1.f);
    const cv_inp* d = deck_get();
    const float* sa = deck_set_alpha();
    bool any = ma < 1.f;
    for (int i = 0; d && sa && i < d->nsets; i++) if (d->sets[i].is_elem && sa[i] < 1.f) any = true;
    if (!any) goto clear;
    /* per element: the model's opacity times the least of its sets' */
    ea = malloc(f->n_elems * sizeof(float));
    if (!ea) goto clear;
    for (uint32_t e = 0; e < f->n_elems; e++) ea[e] = 1.f;
    for (int i = 0; d && sa && i < d->nsets; i++) {
        const cv_set* s = &d->sets[i];
        if (!s->is_elem || !(sa[i] < 1.f)) continue;
        for (uint32_t j = 0; j < s->n; j++) {
            uint32_t e = deck_elem(f, s->ids[j]);
            if (e != UINT32_MAX) ea[e] = fminf(ea[e], fmaxf(sa[i], 0.f));
        }
    }
    /* runs: by group (faces coloured by group: each its colour), then by opacity */
    int axis = G.faces_mode - FM_TYPE;
    bool grp = axis >= 0 && axis < CV_AXIS_N && G.axis_rgb[axis] && G.groups.axis[axis].of_elem;
    uint32_t ng = grp ? (uint32_t)CV_MAX(G.groups.axis[axis].n, 1) : 1, nk = ng * (LEVELS + 1);
    key = malloc(G.skin.n_tri * sizeof(uint32_t));
    cnt = calloc((size_t)nk + 1, sizeof(uint32_t));
    ids = malloc(G.skin.n_tri * sizeof(uint32_t));
    if (!key || !cnt || !ids) goto clear;
    for (size_t t = 0; t < G.skin.n_tri; t++) {
        uint32_t e = G.skin.tri_elem[t];
        uint32_t q = (uint32_t)lroundf(ma * ea[e] * LEVELS);
        uint32_t g = grp ? G.groups.axis[axis].of_elem[e] : 0;
        key[t] = g * (LEVELS + 1) + CV_MIN(q, (uint32_t)LEVELS);
        cnt[key[t] + 1]++;
    }
    int nruns = 0;
    for (uint32_t k = 0; k < nk; k++) { if (cnt[k + 1]) nruns++; cnt[k + 1] += cnt[k]; }
    runs = malloc((size_t)CV_MAX(nruns, 1) * sizeof *runs);
    if (!runs) goto clear;
    nruns = 0;
    for (uint32_t k = 0; k < nk; k++) {
        if (cnt[k + 1] == cnt[k]) continue;
        cv_see_run r = { cnt[k], cnt[k + 1] - cnt[k], (float)(k % (LEVELS + 1)) / LEVELS, { 0, 0, 0 }, grp };
        if (grp) memcpy(r.rgb, G.axis_rgb[axis] + 3 * (k / (LEVELS + 1)), sizeof r.rgb);
        runs[nruns++] = r;
    }
    for (size_t t = 0; t < G.skin.n_tri; t++) ids[cnt[key[t]]++] = (uint32_t)t | (inward_cached(t) ? CV_SEE_FLIP : 0);
    cv_render_see(ids, (uint32_t)G.skin.n_tri, runs, nruns);
    free(ea); free(key); free(cnt); free(ids); free(runs);
    return;
clear:
    cv_render_see(NULL, 0, NULL, 0);
    free(ea); free(key); free(cnt); free(ids); free(runs);
}

bool app_see_set(const char* name, float alpha) {
    const cv_inp* d = deck_get();
    float* sa = deck_set_alpha();
    bool found = false;
    for (int i = 0; d && sa && i < d->nsets; i++)
        if (d->sets[i].is_elem && !strcasecmp(d->sets[i].name, name)) { sa[i] = fminf(fmaxf(alpha, 0.f), 1.f); found = true; }
    if (found) app_see_refresh();
    return found;
}
