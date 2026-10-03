/* app_overlay.c -- geometry built from the field for the overlay layers: the
   vector arrows at the nodes and the filled caps where the clip plane cuts the
   solid elements. */
#include "app_int.h"
#include "gauss.h"
#include "path.h"
#include "cap.h"
#include <math.h>

/* ---- vector arrows: a 3-component field drawn at the nodes ------------------------
   Arrow from the (deformed) node along the vector; the longest one is vec_pct %
   of the model diagonal. Coloured by the current scalar, so the legend applies. */

bool app_field_is_vector(void) {
    if (!G.loaded || G.field_src != 0) return false;
    int fi = find_field(G.step, G.field_name);
    if (fi < 0) return false;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
    return d->ncomp == 3 || (G.comp <= CV_COMP_P1 && G.comp >= CV_COMP_P3_XZ && cv_tensor_order(d));
}

/* an arrow len long arriving at tip along dir: a tube and a cone, coloured by sv */
static void vec_arrow(cv_fvec* in, const float tip[3], const float dir[3], float len, float sv, const float d[6]) {
    float t[3], b[3];
    for (int k = 0; k < 3; k++) { t[k] = tip[k] - dir[k] * len; b[k] = tip[k] - dir[k] * 0.3f * len; }
    deck_inst(in, t, b, 0.03f * len, 0.03f * len, sv, d);
    deck_inst(in, b, tip, 0.11f * len, 0, sv, d);
}

/* a principal value as a pair of arrows through the node along its direction:
   pointing out for tension, in for compression (the usual stress-cross picture) */
static void principal_arrows(const float* v, const uint32_t* ids, size_t n, size_t stride, cv_fvec* in) {
    bool xz = G.comp <= CV_COMP_P1_XZ;
    int k = (xz ? CV_COMP_P1_XZ : CV_COMP_P1) - G.comp;
    float peak = 0;
    for (size_t j = 0; j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        float m = fabsf(G.scalar[i]);
        if (m == m && m > peak) peak = m;
    }
    float L = CV_MAX(G.vec_pct, 0.1f) * 0.01f * G.diag;
    for (size_t j = 0; peak > 0 && j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        float val[3], vec[3][3];
        if (!cv_principal_dirs(v + 6 * (size_t)i, xz, val, vec)) continue;
        float h = 0.5f * L * fabsf(val[k]) / peak;
        if (!(h > 0)) continue;
        const float* p = G.frd.xyz + 3 * (size_t)i;
        float d[6];
        app_node_disp6(i, d);
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            float dir[3] = { vec[k][0] * sgn, vec[k][1] * sgn, vec[k][2] * sgn };
            if (val[k] >= 0) {
                float tip[3] = { p[0] + dir[0] * h, p[1] + dir[1] * h, p[2] + dir[2] * h };
                vec_arrow(in, tip, dir, h, G.scalar[i], d);
            } else {                                    /* head at the node, shaft outside */
                float back[3] = { -dir[0], -dir[1], -dir[2] };
                vec_arrow(in, p, back, h, G.scalar[i], d);
            }
        }
    }
}

void refresh_vectors(void) {
    if (!G.show_vec || !app_field_is_vector() || !G.has_field) { cv_render_inst(CV_INST_VEC, NULL, 0); return; }
    int fi = find_field(G.step, G.field_name);
    const float* v = cache_get(G.step, fi);
    if (!v) { cv_render_inst(CV_INST_VEC, NULL, 0); return; }
    const uint32_t* ids = G.skin.n_pt ? G.skin.pt : NULL;
    size_t n = ids ? G.skin.n_pt : G.frd.n_nodes;
    size_t stride = n / 200000 + 1;                     /* huge models: a sample */
    if (G.frd.steps[G.step].fields[fi].ncomp == 6) {
        cv_fvec in = {0};
        if (G.scalar) principal_arrows(v, ids, n, stride, &in);
        cv_render_inst(CV_INST_VEC, in.a, (uint32_t)(in.n / CV_INST_FLOATS));
        cv_free_vec(in);
        return;
    }
    float peak = 0;
    for (size_t j = 0; j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        float m = sqrtf(v[3 * i] * v[3 * i] + v[3 * i + 1] * v[3 * i + 1] + v[3 * i + 2] * v[3 * i + 2]);
        if (m == m && m > peak) peak = m;
    }
    cv_fvec in = {0};
    float L = CV_MAX(G.vec_pct, 0.1f) * 0.01f * G.diag;
    for (size_t j = 0; peak > 0 && j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        const float* r = v + 3 * i;
        float m = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        if (!(m > 0) || m != m) continue;
        float dir[3] = { r[0] / m, r[1] / m, r[2] / m }, len = L * m / peak;
        float p[3] = { G.frd.xyz[3 * i], G.frd.xyz[3 * i + 1], G.frd.xyz[3 * i + 2] };
        float tip[3] = { p[0] + dir[0] * len, p[1] + dir[1] * len, p[2] + dir[2] * len };
        float d[6];
        app_node_disp6(i, d);
        vec_arrow(&in, tip, dir, len, G.scalar ? G.scalar[i] : NAN, d);
    }
    cv_render_inst(CV_INST_VEC, in.a, (uint32_t)(in.n / CV_INST_FLOATS));
    cv_free_vec(in);
}

void app_vectors_changed(void) { refresh_vectors(); }

/* ---- clip caps: the plane cut through the solid elements, filled (cap.h) -----------
   The projection of the model on the plane's normal is kept while the normal and the
   shape stay the same, so dragging the plane only cuts the elements it crosses. */

void app_clip_caps(bool on, const float n[3], float dd, float f1, float f2) {
    static char key[256], pkey[256];
    static cv_cap_prep prep;
    char k[256];
    on = on && G.clip_cap && G.loaded;
    snprintf(k, sizeof k, "%d|%g|%g|%g|%g|%g|%g|%u|%p|%zu|%d|%d|%d|%d", on, n[0], n[1], n[2], dd, f1, f2, G.field_gen,
             (void*)G.skin.tri, G.skin.n_tri, G.elem_mode, G.has_field, G.field_src, G.mid_faces);
    if (!strcmp(k, key)) return;
    snprintf(key, sizeof key, "%s", k);
    if (!on) {
        cv_cap_prep_free(&prep); pkey[0] = 0;
        cv_render_aux(CV_AUX_CAPTRI, NULL, NULL, NULL, 0);
        return;
    }
    cv_cap_model m = { .f = &G.frd, .vis = G.vis, .disp = G.disp, .disp2 = G.disp2, .f1 = f1, .f2 = f2,
                       .n = { n[0], n[1], n[2] }, .mid = G.mid_faces };
    /* everything but the plane's position and the colours */
    snprintf(k, sizeof k, "%g|%g|%g|%g|%g|%u|%p|%zu|%d", n[0], n[1], n[2], f1, f2, G.field_gen, (void*)G.skin.tri,
             G.skin.n_tri, G.mid_faces);
    if (strcmp(k, pkey) != 0) {
        cv_cap_prep_free(&prep);
        snprintf(pkey, sizeof pkey, "%s", cv_cap_prepare(&prep, &m) ? k : "");
    }
    bool nodal = G.has_field && G.field_src != 1 && !G.elem_mode && G.scalar;
    bool elem = G.has_field && G.field_src != 1 && G.elem_mode && G.elem_val;
    cv_cap_out o = {0};
    cv_cap_cut(&m, &prep, dd, 1e-5f * G.diag, nodal ? G.scalar : NULL, elem ? G.elem_val : NULL, &o);
    cv_render_aux(CV_AUX_CAPTRI, o.pos.a, G.disp ? o.disp.a : NULL, o.val.a, (uint32_t)(o.pos.n / 3));
    cv_cap_out_free(&o);
}
