/* app_select.c -- box selection, CAD style (app.h): which elements a dragged box
   takes, the field's extremes over them, and their outline in the view. */
#include "app_int.h"
#include "web.h"
#include <math.h>
#include <stdio.h>

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

/* ---- hiding by hand -------------------------------------------------------------- */

static bool hide_alloc(void) {
    if (!G.hide) G.hide = calloc(CV_MAX(G.frd.n_elems, 1), 1);
    return G.hide != NULL;
}

void app_hide_elems(const uint32_t* el, uint32_t n) {
    if (!G.loaded || !n || !hide_alloc()) return;
    for (uint32_t i = 0; i < n; i++) if (el[i] < G.frd.n_elems) G.hide[el[i]] = 1;
    G.probe_on = false;
    app_sel_clear();
    app_groups_changed();
}

void app_isolate_elems(const uint32_t* el, uint32_t n) {
    if (!G.loaded || !n || !hide_alloc()) return;
    memset(G.hide, 1, G.frd.n_elems);
    for (uint32_t i = 0; i < n; i++) if (el[i] < G.frd.n_elems) G.hide[el[i]] = 0;
    G.probe_on = false;
    app_sel_clear();
    app_groups_changed();
}

void app_hide_set(const char* name, bool isolate) {
    const cv_inp* d = deck_get();
    const cv_set* s = d ? cv_inp_set(d, name, true) : NULL;
    if (!s) return;
    uint32_t* el = malloc((size_t)CV_MAX(s->n, 1) * sizeof *el), n = 0;
    if (!el) return;
    for (uint32_t i = 0; i < s->n; i++) {
        uint32_t e = cv_frd_elem_index(&G.frd, s->ids[i]);
        if (e != UINT32_MAX) el[n++] = e;
    }
    if (isolate) app_isolate_elems(el, n); else app_hide_elems(el, n);
    free(el);
}

void app_show_all(void) {
    if (!G.loaded) return;
    free(G.hide); G.hide = NULL;
    for (int a = 0; a < CV_AXIS_N; a++)
        for (int i = 0; i < G.groups.axis[a].n; i++) G.groups.axis[a].on[i] = true;
    app_groups_changed();
}

/* ---- clip here ------------------------------------------------------------------- */

void app_clip_at(const float p[3], const float n[3]) {
    if (!G.loaded) return;
    int k = 0;
    for (int a = 1; a < 3; a++) if (fabsf(n[a]) > fabsf(n[k])) k = a;
    const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
    v3 eye, f, r, u;
    cam_basis(&G.cam, &eye, &f, &r, &u);
    const float e[3] = { eye.x, eye.y, eye.z };
    G.clip_axis = k;
    G.clip_pos = hi[k] > lo[k] ? CV_MIN(CV_MAX((p[k] - lo[k]) / (hi[k] - lo[k]), 0.f), 1.f) : 0.5f;
    G.clip_flip = e[k] < p[k];                       /* the clip drops n . x > d: the eye's side goes */
    G.clip_on = true;
}

/* ---- the selection out ------------------------------------------------------------- */

size_t app_sel_ids(char* out, size_t n) {
    size_t o = 0;
    if (n) out[0] = 0;
    for (uint32_t i = 0; i < G.sel_n && o + 16 < n; i++)
        o += (size_t)snprintf(out + o, n - o, "%u%s", G.frd.elem_id[G.sel[i]],
                              i + 1 == G.sel_n ? "\n" : i % 16 == 15 ? ",\n" : ", ");
    return o;
}

bool app_sel_csv(void) {
    if (!G.loaded || !G.sel_n) return false;
    char base[1024], path[1100];
    snprintf(base, sizeof base, "%s", G.path);
    char* dot = strrchr(base, '.');
    char* sep = strrchr(base, cv_path_sep());
    if (dot && (!sep || dot > sep)) *dot = 0;
    snprintf(path, sizeof path, "%s_selection.csv", base);
    FILE* fp = fopen(path, "w");
    bool ok = fp != NULL;
    if (fp) {
        bool val = G.has_field && G.field_src != 1;
        fprintf(fp, "element,type,material,x,y,z%s\n", !val ? "" : G.elem_mode ? ",value" : ",min,max");
        for (uint32_t k = 0; k < G.sel_n; k++) {
            uint32_t e = G.sel[k], b = G.frd.eoff[e], m = G.frd.eoff[e + 1] - b;
            double c[3] = { 0, 0, 0 };
            float mn = INFINITY, mx = -INFINITY;
            for (uint32_t j = 0; j < m; j++) {
                uint32_t i = G.frd.conn[b + j];
                for (int a = 0; a < 3; a++) c[a] += G.frd.xyz[3 * i + a] / (double)m;
                if (val && !G.elem_mode && G.scalar[i] == G.scalar[i]) { mn = CV_MIN(mn, G.scalar[i]); mx = CV_MAX(mx, G.scalar[i]); }
            }
            fprintf(fp, "%u,%s,%u,%.9g,%.9g,%.9g", G.frd.elem_id[e], cv_frd_type_name(G.frd.etype[e]), G.frd.emat[e], c[0], c[1], c[2]);
            if (val && G.elem_mode) fprintf(fp, ",%.9g", G.elem_val[e]);
            else if (val) fprintf(fp, ",%.9g,%.9g", mn, mx);
            fputc('\n', fp);
        }
        ok = fclose(fp) == 0;
    }
    snprintf(G.note, sizeof G.note, ok ? "saved %s" : "could not write %s", path);
    G.note_t = cv_now();
    cv_msg_add(&G.msgs, 0, false, G.note);
    if (ok) CV_EXPORTED(path);
    return ok;
}

/* ---- the context menu ---------------------------------------------------------------- */

void app_menu_open(float x, float y) {
    if (!G.loaded) return;
    float o[3], d[3];
    G.menu_on = true; G.menu_x = x; G.menu_y = y;
    memset(G.menu_n, 0, sizeof G.menu_n);
    if (!app_pick(x, y, &G.menu_pick, o, d)) return;
    for (int k = 0; k < 3; k++) G.menu_p[k] = o[k] + d[k] * G.menu_pick.t;
    if (G.menu_pick.tri < G.skin.n_tri) {             /* the face's normal, as shown */
        float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f, sc2 = G.deform ? G.deform_scale * G.anim_factor2 : 0.f, q[3][3];
        for (int j = 0; j < 3; j++) shown_pos(G.skin.tri[3 * G.menu_pick.tri + j], sc, sc2, q[j]);
        float a[3], b[3];
        for (int k = 0; k < 3; k++) { a[k] = q[1][k] - q[0][k]; b[k] = q[2][k] - q[0][k]; }
        G.menu_n[0] = a[1] * b[2] - a[2] * b[1]; G.menu_n[1] = a[2] * b[0] - a[0] * b[2]; G.menu_n[2] = a[0] * b[1] - a[1] * b[0];
    }
}
