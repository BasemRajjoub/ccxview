/* app_select.c -- box selection, CAD style (app.h): which elements a dragged box
   takes, the field's extremes over them, and their outline in the view. */
#include "app_int.h"
#include <math.h>

static void shown_pos(uint32_t n, float sc, float sc2, float p[3]) {
    for (int k = 0; k < 3; k++)
        p[k] = G.frd.xyz[3 * n + k] + (G.disp ? sc * G.disp[3 * n + k] : 0) + (G.disp2 ? sc2 * G.disp2[3 * n + k] : 0);
}

void app_sel_clear(void) {
    free(G.sel); G.sel = NULL; G.sel_n = 0;
    G.boxq.on = false;
    cv_render_aux(CV_AUX_SELLN, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_SELTRI, NULL, NULL, NULL, 0);
}

/* the corner edges of the selected elements' exterior faces, moving with the shape */
void app_sel_refresh(void) {
    if (!G.sel_n || !G.loaded) { cv_render_aux(CV_AUX_SELLN, NULL, NULL, NULL, 0); cv_render_aux(CV_AUX_SELTRI, NULL, NULL, NULL, 0); return; }
    uint8_t* on = calloc(CV_MAX(G.frd.n_elems, 1), 1);
    if (!on) return;
    for (uint32_t i = 0; i < G.sel_n; i++) on[G.sel[i]] = 1;
    cv_fvec pos = {0}, disp = {0};
    for (size_t k = 0; k < G.skin.n_face; k++) {
        uint32_t e = G.skin.face[k] >> 3, c[4];
        if (e >= G.frd.n_elems || !on[e]) continue;
        int m = cv_elem_face_corners(&G.frd, e, (int)(G.skin.face[k] & 7), c);
        for (int j = 0; j < m; j++) {
            uint32_t ends[2] = { c[j], c[(j + 1) % m] };
            for (int t = 0; t < 2; t++) {
                float d[6];
                app_node_disp6(ends[t], d);
                if (cv_reserve(pos, pos.n + 3)) for (int q = 0; q < 3; q++) pos.a[pos.n++] = G.frd.xyz[3 * ends[t] + q];
                if (cv_reserve(disp, disp.n + 6)) for (int q = 0; q < 6; q++) disp.a[disp.n++] = d[q];
            }
        }
    }
    app_aux_upload(CV_AUX_SELLN, &pos, &disp, NULL);
    /* their skin triangles, coloured by the field as the faces are */
    const float* nv = G.has_field && !G.elem_mode ? G.scalar : NULL;
    const float* ev = G.has_field && G.elem_mode ? G.elem_val : NULL;
    pos.n = disp.n = 0;
    cv_fvec val = {0};
    for (size_t t = 0; t < G.skin.n_tri; t++) {
        uint32_t e = G.skin.tri_elem[t];
        if (e >= G.frd.n_elems || !on[e]) continue;
        for (int j = 0; j < 3; j++) {
            uint32_t i = G.skin.tri[3 * t + j];
            float d[6];
            app_node_disp6(i, d);
            if (cv_reserve(pos, pos.n + 3)) for (int q = 0; q < 3; q++) pos.a[pos.n++] = G.frd.xyz[3 * i + q];
            if (cv_reserve(disp, disp.n + 6)) for (int q = 0; q < 6; q++) disp.a[disp.n++] = d[q];
            if (cv_reserve(val, val.n + 1)) val.a[val.n++] = nv ? nv[i] : ev ? ev[e] : 0.f;
        }
    }
    free(on);
    app_aux_upload(CV_AUX_SELTRI, &pos, &disp, nv || ev ? val.a : NULL);
    cv_free_vec(pos); cv_free_vec(disp); cv_free_vec(val);
}

bool app_box_select(float x0, float y0, float x1, float y1) {
    app_sel_clear();
    if (!G.loaded) return false;
    bool crossing = x1 < x0;                         /* right to left */
    float lx = CV_MIN(x0, x1), hx = CV_MAX(x0, x1), ly = CV_MIN(y0, y1), hy = CV_MAX(y0, y1);
    uint32_t N = G.frd.n_nodes, E = G.frd.n_elems;
    uint8_t* inside = calloc(CV_MAX(N, 1), 1);
    uint32_t* sel = malloc((size_t)CV_MAX(E, 1) * sizeof *sel);
    if (!inside || !sel) { free(inside); free(sel); return false; }
    float mvp[16], mv[16];
    cam_matrices(mvp, mv, NULL);
    float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f, sc2 = G.deform ? G.deform_scale * G.anim_factor2 : 0.f;
    for (uint32_t i = 0; i < N; i++) {
        float p[3], c[4];
        shown_pos(i, sc, sc2, p);
        for (int r = 0; r < 4; r++) c[r] = mvp[r] * p[0] + mvp[4 + r] * p[1] + mvp[8 + r] * p[2] + mvp[12 + r];
        if (c[3] <= 1e-9f) continue;                 /* behind the eye */
        float sx = G.vp_x + (c[0] / c[3] * 0.5f + 0.5f) * G.vp_w, sy = G.vp_y + (0.5f - c[1] / c[3] * 0.5f) * G.vp_h;
        inside[i] = sx >= lx && sx <= hx && sy >= ly && sy <= hy;
    }
    uint32_t n = cv_box_elems(&G.frd, G.vis, inside, crossing, sel);
    if (!n) { free(inside); free(sel); return false; }
    G.sel = sel; G.sel_n = n; G.sel_crossing = crossing;
    app_sel_refresh();
    /* the extremes over the selection: its nodes, or its elements per element */
    bool elem = G.elem_mode;
    const float* val = !G.has_field || G.field_src == 1 ? NULL : elem ? G.elem_val : G.scalar;
    uint32_t imax = UINT32_MAX, imin = UINT32_MAX, cnt = 0;
    if (val) {
        memset(inside, 0, CV_MAX(N, 1));             /* reused: node already counted */
        for (uint32_t k = 0; k < n; k++) {
            uint32_t e = sel[k], b = G.frd.eoff[e], m = elem ? 1 : G.frd.eoff[e + 1] - b;
            for (uint32_t j = 0; j < m; j++) {        /* the element itself, or each of its nodes once */
                uint32_t i = elem ? e : G.frd.conn[b + j];
                if (!elem && inside[i]) continue;
                if (!elem) inside[i] = 1;
                float v = val[i];
                if (v != v) continue;
                cnt++;
                if (imax == UINT32_MAX || v > val[imax]) imax = i;
                if (imin == UINT32_MAX || v < val[imin]) imin = i;
            }
        }
    }
    free(inside);
    if (cnt) {
        app_probe_at(imax, elem);
        G.boxq.on = true; G.boxq.elem = elem; G.boxq.n = cnt; G.boxq.gen = G.field_gen;
        G.boxq.max_at = imax; G.boxq.min_at = imin; G.boxq.vmax = val[imax]; G.boxq.vmin = val[imin];
    }
    return true;
}
