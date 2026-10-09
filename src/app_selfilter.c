/* app_selfilter.c -- filters on the selection (app.h): keep what lies in a range of
   x y z (or r theta z about an axis), what the field puts above or below a value
   or in its top N %, an element type, a material, the side facing the camera.
   They look through the selection, or everything shown when nothing is selected
   ("select where"); the mode says what becomes of what passes. The tests
   themselves are headless (selfilter.c). */
#include "app_int.h"
#include "selfilter.h"

/* the shown elements / nodes as a list */
static uint32_t* all_shown(bool nodes, uint32_t* n) {
    *n = 0;
    if (!nodes) {
        uint8_t* m = malloc(CV_MAX(G.frd.n_elems, 1));
        if (!m) return NULL;
        for (uint32_t e = 0; e < G.frd.n_elems; e++) m[e] = !G.vis || G.vis[e];
        uint32_t* r = cv_sel_from_mask(m, G.frd.n_elems, n);
        free(m);
        return r;
    }
    uint8_t* m = cv_sel_shown_nodes(&G.frd, G.vis);
    uint32_t* r = m ? cv_sel_from_mask(m, G.frd.n_nodes, n) : NULL;
    free(m);
    return r;
}

static uint32_t* copy(const uint32_t* a, uint32_t n) {
    uint32_t* r = malloc((size_t)CV_MAX(n, 1) * sizeof *r);
    if (r && n) memcpy(r, a, (size_t)n * sizeof *r);
    return r;
}

/* q, or (q NULL) the facing masks fn / fe */
static bool run(const cv_selfilter* q, const uint8_t* fn, const uint8_t* fe, int mode) {
    if (!G.loaded) return false;
    bool empty = !G.sel_n && !G.seln_n;
    int which = empty ? app_sel_which() : (G.sel_n ? 1 : 0) | (G.seln_n ? 2 : 0);
    if (mode == CV_SEL_ADD) which |= app_sel_which();
    bool all = empty || mode == CV_SEL_ADD;          /* add: what passes anywhere joins */
    uint32_t ne = 0, nn = 0;
    uint32_t* el = !(which & 1) ? NULL : all ? all_shown(false, &ne) : copy(G.sel, ne = G.sel_n);
    uint32_t* nd = !(which & 2) ? NULL : all ? all_shown(true, &nn) : copy(G.seln, nn = G.seln_n);
    const float* ev = G.has_field && (G.elem_mode || G.field_src == 1) ? G.elem_val : NULL;
    const float* nv = G.has_field && !ev ? G.scalar : NULL;
    if (el) {
        if (q) ne = cv_selfilter_elems(&G.frd, q, nv, ev, el, ne);
        else { uint32_t m = 0; for (uint32_t k = 0; k < ne; k++) if (fe[el[k]]) el[m++] = el[k]; ne = m; }
    } else ne = 0;
    if (nd) {
        if (q) nn = cv_selfilter_nodes(&G.frd, q, nv, nd, nn);
        else { uint32_t m = 0; for (uint32_t k = 0; k < nn; k++) if (fn[nd[k]]) nd[m++] = nd[k]; nn = m; }
    } else nn = 0;
    snprintf(G.sel_note, sizeof G.sel_note, "%u elements, %u nodes pass", ne, nn);
    if (q && q->kind >= CV_SQ_ABOVE && q->kind <= CV_SQ_TOP && !nv && !ev) snprintf(G.sel_note, sizeof G.sel_note, "no field shown");
    int m = mode == CV_SEL_ADD || mode == CV_SEL_REMOVE ? mode : CV_SEL_NEW;   /* keep: what passed is the selection */
    bool ok = app_sel_apply(el, ne, nd, nn, m, which);
    free(el); free(nd);
    return ok;
}

bool app_sel_filter(const cv_selfilter* q, int mode) { return run(q, NULL, NULL, mode); }

bool app_sel_filter_facing(int mode) {
    uint8_t* fn = malloc(CV_MAX(G.frd.n_nodes, 1));
    uint8_t* fe = malloc(CV_MAX(G.frd.n_elems, 1));
    bool ok = false;
    if (fn && fe) { app_sel_facing(fn, fe); ok = run(NULL, fn, fe, mode); }
    free(fn); free(fe);
    return ok;
}
