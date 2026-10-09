/* selset.c -- the selection's set algebra; see selset.h. Everything goes through
   a mask per index: a pass over the model, never a sort. */
#include "selset.h"

uint8_t* cv_sel_mask(const uint32_t* a, uint32_t n, uint32_t total) {
    uint8_t* m = calloc(CV_MAX(total, 1), 1);
    if (!m) return NULL;
    for (uint32_t i = 0; i < n; i++) if (a[i] < total) m[a[i]] = 1;
    return m;
}

uint32_t* cv_sel_from_mask(const uint8_t* m, uint32_t total, uint32_t* n) {
    uint32_t c = 0;
    for (uint32_t i = 0; i < total; i++) c += m[i] != 0;
    *n = 0;
    if (!c) return NULL;
    uint32_t* out = malloc((size_t)c * sizeof *out);
    if (!out) return NULL;
    for (uint32_t i = 0; i < total; i++) if (m[i]) out[(*n)++] = i;
    return out;
}

bool cv_sel_combine(int mode, const uint32_t* a, uint32_t na, const uint32_t* b, uint32_t nb,
                    uint32_t total, uint32_t** out, uint32_t* n) {
    uint8_t* m = calloc(CV_MAX(total, 1), 1);
    if (!m) return false;
    if (mode != CV_SEL_NEW) for (uint32_t i = 0; i < na; i++) if (a[i] < total) m[a[i]] = 1;
    for (uint32_t i = 0; i < nb; i++) {
        if (b[i] >= total) continue;
        if (mode == CV_SEL_REMOVE) m[b[i]] = 0;
        else if (mode == CV_SEL_AND) m[b[i]] |= m[b[i]] ? 2 : 0;   /* 3: in both */
        else m[b[i]] = 1;
    }
    if (mode == CV_SEL_AND) for (uint32_t i = 0; i < total; i++) m[i] = m[i] == 3;
    uint32_t c = 0;
    for (uint32_t i = 0; i < total; i++) c += m[i] != 0;
    uint32_t* r = c ? malloc((size_t)c * sizeof *r) : NULL;
    if (c && !r) { free(m); return false; }
    c = 0;
    for (uint32_t i = 0; i < total; i++) if (m[i]) r[c++] = i;
    free(m);
    *out = r; *n = c;
    return true;
}

uint8_t* cv_sel_shown_nodes(const cv_frd* f, const uint8_t* vis) {
    uint8_t* m = calloc(CV_MAX(f->n_nodes, 1), 1);
    if (!m) return NULL;
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (vis && !vis[e]) continue;
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) m[f->conn[j]] = 1;
    }
    return m;
}

uint32_t* cv_sel_invert_elems(const cv_frd* f, const uint8_t* vis, const uint32_t* el, uint32_t ne, uint32_t* n) {
    *n = 0;
    uint8_t* m = cv_sel_mask(el, ne, f->n_elems);
    if (!m) return NULL;
    for (uint32_t e = 0; e < f->n_elems; e++) m[e] = !m[e] && (!vis || vis[e]);
    uint32_t* r = cv_sel_from_mask(m, f->n_elems, n);
    free(m);
    return r;
}

uint32_t* cv_sel_invert_nodes(const cv_frd* f, const uint8_t* vis, const uint32_t* nd, uint32_t nn, uint32_t* n) {
    *n = 0;
    uint8_t* m = cv_sel_shown_nodes(f, vis);
    if (!m) return NULL;
    for (uint32_t i = 0; i < nn; i++) if (nd[i] < f->n_nodes) m[nd[i]] = 0;
    uint32_t* r = cv_sel_from_mask(m, f->n_nodes, n);
    free(m);
    return r;
}

uint32_t* cv_sel_elem_nodes(const cv_frd* f, const uint32_t* el, uint32_t ne, uint32_t* n) {
    *n = 0;
    uint8_t* m = calloc(CV_MAX(f->n_nodes, 1), 1);
    if (!m) return NULL;
    for (uint32_t k = 0; k < ne; k++) {
        if (el[k] >= f->n_elems) continue;
        for (uint32_t j = f->eoff[el[k]]; j < f->eoff[el[k] + 1]; j++) m[f->conn[j]] = 1;
    }
    uint32_t* r = cv_sel_from_mask(m, f->n_nodes, n);
    free(m);
    return r;
}

uint32_t* cv_sel_node_elems(const cv_frd* f, const uint8_t* vis, const uint32_t* nd, uint32_t nn, bool any, uint32_t* n) {
    *n = 0;
    uint8_t* m = cv_sel_mask(nd, nn, f->n_nodes);
    uint8_t* em = calloc(CV_MAX(f->n_elems, 1), 1);
    if (!m || !em) { free(m); free(em); return NULL; }
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if ((vis && !vis[e]) || f->eoff[e + 1] == f->eoff[e]) continue;
        uint32_t in = 0, all = f->eoff[e + 1] - f->eoff[e];
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) in += m[f->conn[j]];
        em[e] = any ? in > 0 : in == all;
    }
    uint32_t* r = cv_sel_from_mask(em, f->n_elems, n);
    free(m); free(em);
    return r;
}
