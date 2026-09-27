/* app_fbd.c -- cgx geometry (.fbd) beside or instead of a mesh: points, curves
   and surfaces as their own layers, cgx sets as a display group.

   Ticking sets shows only what they hold (none ticked = everything), the same
   rule as the deck's element sets; when cgx also meshed the script, a set's
   elements join the mesh's display group under the same tick. */
#include "app.h"
#include "cgx.h"
#include "fbd.h"
#ifdef _WIN32
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

static struct {
    cv_fbd g;
    bool   on;
    bool   evaluated;       /* came back from cgx */
    bool*  set_on;
} F;

bool geo_loaded(void) { return F.on; }
const cv_fbd* geo_get(void) { return F.on ? &F.g : NULL; }
bool* geo_set_flags(void) { return F.set_on; }
bool geo_evaluated(void) { return F.evaluated; }

/* the export helper set is not one of the model's */
bool geo_set_hidden(const char* name) {
    if (name[0] == '+') return true;            /* cgx's own (+EDGE, ...) */
    size_t k = strlen(CV_CGX_SET);
    for (; *name; name++)
        if (!strncasecmp(name, CV_CGX_SET, k)) return true;
    return false;
}

void geo_clear(void) {
    cv_fbd_free(&F.g);
    free(F.g.msgs.a);
    free(F.set_on);
    memset(&F, 0, sizeof F);
    cv_render_aux(CV_AUX_GEOPT, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_GEOLN, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_GEOTRI, NULL, NULL, NULL, 0);
}

void geo_set(cv_fbd* g, bool evaluated) {
    geo_clear();
    F.g = *g;
    memset(g, 0, sizeof *g);
    F.on = true;
    F.evaluated = evaluated;
    F.set_on = calloc((size_t)CV_MAX(F.g.nsets, 1), sizeof(bool));
    for (size_t i = 0; i < F.g.msgs.n; i++) cv_msg_add(&G.msgs, F.g.msgs.a[i].where, false, F.g.msgs.a[i].text);
    geo_refresh();
}

bool geo_any_on(void) {
    for (int i = 0; F.on && i < F.g.nsets; i++) if (F.set_on[i]) return true;
    return false;
}

/* extend a bounding box by everything the geometry holds; false if it is empty */
bool geo_bounds(v3* lo, v3* hi) {
    if (!F.on) return false;
    const float* arr[3] = { F.g.pxyz, F.g.cxyz, F.g.txyz };
    size_t cnt[3] = { F.g.npts, F.g.coff ? F.g.coff[F.g.ncrv] : 0, (size_t)F.g.ntri * 3 };
    bool any = false;
    for (int k = 0; k < 3; k++)
        for (size_t i = 0; i < cnt[k]; i++) {
            const float* p = arr[k] + 3 * i;
            if (!(p[0] == p[0] && p[1] == p[1] && p[2] == p[2])) continue;
            lo->x = fminf(lo->x, p[0]); hi->x = fmaxf(hi->x, p[0]);
            lo->y = fminf(lo->y, p[1]); hi->y = fmaxf(hi->y, p[1]);
            lo->z = fminf(lo->z, p[2]); hi->z = fmaxf(hi->z, p[2]);
            any = true;
        }
    return any;
}


static void put(cv_fvec* v, const float* p, size_t nfloat) {
    if (cv_reserve(*v, v->n + nfloat)) { memcpy(v->a + v->n, p, nfloat * sizeof(float)); v->n += nfloat; }
}

static void add_curve(cv_fvec* ln, uint32_t c) {
    for (uint32_t j = F.g.coff[c]; j + 1 < F.g.coff[c + 1]; j++) put(ln, F.g.cxyz + 3 * j, 6);
}

/* upload the three layers, filtered by the ticked sets */
void geo_refresh(void) {
    cv_fvec pt = {0}, ln = {0}, tr = {0};
    if (F.on) {
        if (!geo_any_on()) {
            put(&pt, F.g.pxyz, (size_t)F.g.npts * 3);
            for (uint32_t c = 0; c < F.g.ncrv; c++) add_curve(&ln, c);
            put(&tr, F.g.txyz, (size_t)F.g.ntri * 9);
        } else {
            /* union of the ticked sets, each entity once */
            uint8_t* seen = calloc((size_t)F.g.npts + F.g.ncrv + F.g.nsrf + 1, 1);
            uint8_t *sp = seen, *sc = seen ? seen + F.g.npts : NULL, *ss = seen ? sc + F.g.ncrv : NULL;
            for (int i = 0; seen && i < F.g.nsets; i++) {
                if (!F.set_on[i]) continue;
                const cv_gset* s = &F.g.sets[i];
                for (uint32_t k = 0; k < s->npts; k++) {
                    uint32_t p = s->pts[k];
                    if (!sp[p]) { sp[p] = 1; put(&pt, F.g.pxyz + 3 * p, 3); }
                }
                for (uint32_t k = 0; k < s->ncrv; k++) {
                    uint32_t c = s->crv[k];
                    if (!sc[c]) { sc[c] = 1; add_curve(&ln, c); }
                }
                for (uint32_t k = 0; k < s->nsrf; k++) {
                    uint32_t f = s->srf[k];
                    if (ss[f]) continue;
                    ss[f] = 1;
                    put(&tr, F.g.txyz + (size_t)F.g.soff[f] * 9, (size_t)(F.g.soff[f + 1] - F.g.soff[f]) * 9);
                }
            }
            free(seen);
        }
    }
    cv_render_aux(CV_AUX_GEOPT, pt.a, NULL, NULL, (uint32_t)(pt.n / 3));
    cv_render_aux(CV_AUX_GEOLN, ln.a, NULL, NULL, (uint32_t)(ln.n / 3));
    cv_render_aux(CV_AUX_GEOTRI, tr.a, NULL, NULL, (uint32_t)(tr.n / 3));
    cv_free_vec(pt); cv_free_vec(ln); cv_free_vec(tr);
}

/* A set's tick also drives the same-named element set of the mesh cgx made. */
void geo_set_toggled(int i) {
    geo_refresh();
    const cv_inp* d = deck_get();
    if (!d || i < 0 || i >= F.g.nsets) return;
    bool changed = false;
    for (int k = 0; k < d->nsets; k++)
        if (d->sets[k].is_elem && !strcmp(d->sets[k].name, F.g.sets[i].name)) {
            deck_set_flags()[k] = F.set_on[i];
            changed = true;
        }
    if (changed) app_groups_changed();
}

void geo_show_all(void) {
    for (int i = 0; F.on && i < F.g.nsets; i++) F.set_on[i] = false;
    geo_refresh();
    const cv_inp* d = deck_get();
    if (d) {
        for (int k = 0; k < d->nsets; k++) deck_set_flags()[k] = false;
        app_groups_changed();
    }
}

/* The mesh's sets from cgx's sets (their node and element ids), sorted and
   unique like the deck reader's. Runs on the loader thread. */
void geo_sets_into_deck(const cv_fbd* g, cv_inp* d) {
    for (int i = 0; i < g->nsets; i++) {
        const cv_gset* s = &g->sets[i];
        if (geo_set_hidden(s->name)) continue;
        for (int e = 0; e < 2; e++) {
            uint32_t n = e ? s->nel : s->nnod;
            const uint32_t* src = e ? s->elems : s->nodes;
            if (!n) continue;
            cv_set* q = realloc(d->sets, (size_t)(d->nsets + 1) * sizeof *q);
            if (!q) return;
            d->sets = q;
            cv_set* t = &d->sets[d->nsets];
            memset(t, 0, sizeof *t);
            t->ids = malloc((size_t)n * sizeof(uint32_t));
            if (!t->ids) return;
            memcpy(t->ids, src, (size_t)n * sizeof(uint32_t));
            qsort(t->ids, n, sizeof(uint32_t), cv_cmp_u32);
            uint32_t w = 0;
            for (uint32_t k = 0; k < n; k++) if (!w || t->ids[w - 1] != t->ids[k]) t->ids[w++] = t->ids[k];
            t->n = w;
            t->is_elem = e == 1;
            snprintf(t->name, sizeof t->name, "%s", s->name);
            d->nsets++;
        }
    }
}
