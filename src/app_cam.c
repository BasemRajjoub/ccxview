/* app_cam.c -- camera, fitting, symmetry copies and ray picking. */
#include "app_int.h"
#include <math.h>

/* ---- camera ----------------------------------------------------------------------- */

/* Yaw and pitch live in a frame whose up is +Y. A Z-up model turns that frame
   so its up is world +Z and its front view (yaw 0) looks along +Y. */
static v3 to_world(v3 f) { return G.up_z ? v3_make(f.x, -f.z, f.y) : f; }
static v3 to_frame(v3 w) { return G.up_z ? v3_make(w.x, w.z, -w.y) : w; }
v3 cam_up_axis(void) { return G.up_z ? v3_make(0, 0, 1) : v3_make(0, 1, 0); }

void cam_basis(const cv_camera* c, v3* eye, v3* fwd, v3* right, v3* up) {
    float cp = cosf(c->pitch), sp = sinf(c->pitch), cy = cosf(c->yaw), sy = sinf(c->yaw);
    v3 dir = to_world(v3_make(cp * sy, sp, cp * cy));  /* target -> eye */
    *eye = v3_add(c->target, v3_scale(dir, c->dist));
    *fwd = v3_scale(dir, -1.f);
    *right = v3_norm(v3_cross(*fwd, cam_up_axis()));
    *up = v3_cross(*right, *fwd);
}

static int cmp_float(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return (x > y) - (x < y);
}

/* Box of the model as drawn (deformed at the current scale), ignoring the most
   extreme 1% per axis: a few runaway nodes -- a diverged increment, a free mass --
   must not shrink the model to a speck. From a sample of at most 20k nodes. */
static bool drawn_box(v3* lo, v3* hi) {
    float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f;
    const uint32_t* ids = G.skin.n_pt ? G.skin.pt : NULL;       /* nodes of visible elements */
    size_t n = ids ? G.skin.n_pt : G.frd.n_nodes;
    if (n == 0) return false;
    size_t stride = n / 20000 + 1, m = 0;
    float* c[3];
    for (int k = 0; k < 3; k++) c[k] = malloc((n / stride + 1) * sizeof(float));
    if (!c[0] || !c[1] || !c[2]) { for (int k = 0; k < 3; k++) free(c[k]); return false; }
    for (size_t j = 0; j < n; j += stride) {
        uint32_t i = ids ? ids[j] : (uint32_t)j;
        float q[3];
        bool ok = true;
        for (int k = 0; k < 3; k++) {
            q[k] = G.frd.xyz[3 * i + k] + (G.disp && sc != 0.f ? G.disp[3 * i + k] * sc : 0.f);
            if (!(q[k] == q[k]) || isinf(q[k])) ok = false;
        }
        if (!ok) continue;
        for (int k = 0; k < 3; k++) c[k][m] = q[k];
        m++;
    }
    bool found = m > 0;
    if (found) {
        size_t cut = m >= 200 ? m / 100 : 0;                     /* 1% each side */
        float L[3], H[3];
        for (int k = 0; k < 3; k++) {
            qsort(c[k], m, sizeof(float), cmp_float);
            L[k] = c[k][cut];
            H[k] = c[k][m - 1 - cut];
        }
        *lo = v3_make(L[0], L[1], L[2]);
        *hi = v3_make(H[0], H[1], H[2]);
    }
    for (int k = 0; k < 3; k++) free(c[k]);
    return found;
}

/* ---- symmetry: mirror copies of the model ---------------------------------------
   A model of one half (quarter, eighth) of a symmetric part is drawn again
   reflected across the chosen planes -- every combination, so X+Y gives four
   copies. The copies are the same buffers drawn with a reflected model matrix:
   nothing is rebuilt, deformation and colours follow for free. */

float app_sym_plane(int k) {
    const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
    return G.sym_at[k] == CV_SYM_MIN ? lo[k] : G.sym_at[k] == CV_SYM_MAX ? hi[k] : 0.f;
}

/* the plane a symmetric model is usually cut on: 0 when the part touches it,
   else the side nearer to 0 */
int sym_auto(int k) {
    const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
    float tol = 1e-3f * CV_MAX(G.diag, 1e-12f);
    if (fabsf(lo[k]) <= tol || fabsf(hi[k]) <= tol) return CV_SYM_ZERO;
    return fabsf(lo[k]) <= fabsf(hi[k]) ? CV_SYM_MIN : CV_SYM_MAX;
}

/* box of the model and all its mirror copies */
static void sym_extend(v3* lo, v3* hi) {
    float* l = &lo->x; float* h = &hi->x;
    for (int k = 0; k < 3; k++) {
        if (!G.sym[k]) continue;
        float c = app_sym_plane(k), a = 2 * c - h[k], b = 2 * c - l[k];
        l[k] = fminf(l[k], a); h[k] = fmaxf(h[k], b);
    }
}

/* model matrix of mirror copy `m` (bit k = reflected across the k plane) */
void sym_matrix(int m, float* M) {
    for (int i = 0; i < 16; i++) M[i] = i % 5 == 0 ? 1.f : 0.f;
    for (int k = 0; k < 3; k++)
        if (m >> k & 1) { M[k * 5] = -1.f; M[12 + k] = 2.f * app_sym_plane(k); }
}

void app_sym_changed(void) { app_fit(); }

void app_sym_toggled(int k) {
    if (k < 0 || k > 2) return;
    if (G.sym[k]) G.sym_at[k] = sym_auto(k);
    app_fit();
}

/* View box: the drawn shape united with the undeformed one. It sets the depth
   range, so results that carry a part far from where it started stay visible. */
void view_bounds(void) {
    v3 lo = G.bmin, hi = G.bmax, dl, dh;
    if (drawn_box(&dl, &dh)) {
        lo.x = fminf(lo.x, dl.x); lo.y = fminf(lo.y, dl.y); lo.z = fminf(lo.z, dl.z);
        hi.x = fmaxf(hi.x, dh.x); hi.y = fmaxf(hi.y, dh.y); hi.z = fmaxf(hi.z, dh.z);
    }
    sym_extend(&lo, &hi);
    G.vmin = lo; G.vmax = hi;
    v3 e = v3_sub(hi, lo);
    G.vdiag = sqrtf(v3_dot(e, e));
    if (!(G.vdiag > 0) || isinf(G.vdiag)) G.vdiag = G.diag;
}

void app_fit(void) {
    if (!G.loaded) return;
    view_bounds();
    v3 lo, hi;                                   /* frame the shape as drawn */
    if (!drawn_box(&lo, &hi)) { lo = G.bmin; hi = G.bmax; }
    sym_extend(&lo, &hi);
    G.cam.target = v3_scale(v3_add(lo, hi), 0.5f);
    v3 e = v3_sub(hi, lo);
    float r = 0.5f * sqrtf(v3_dot(e, e));
    if (!(r > 0) || isinf(r)) r = 0.5f * CV_MAX(G.diag, 1e-6f);
    float aspect = G.vp_h > 0 ? (float)G.vp_w / (float)G.vp_h : 1.f;
    float half = G.cam.fovy * 0.5f;
    float fit = aspect < 1.f ? atanf(tanf(half) * aspect) : half;
    G.cam.dist = r / sinf(fit) * 1.05f;
}

bool app_project(v3 p, float* sx, float* sy) {
    float mvp[16], mv[16];
    cam_matrices(mvp, mv, NULL);
    float c[4];
    for (int r = 0; r < 4; r++) c[r] = mvp[r] * p.x + mvp[4 + r] * p.y + mvp[8 + r] * p.z + mvp[12 + r];
    if (c[3] <= 1e-9f) return false;
    *sx = G.vp_x + (c[0] / c[3] * 0.5f + 0.5f) * G.vp_w;
    *sy = G.vp_y + (0.5f - c[1] / c[3] * 0.5f) * G.vp_h;
    return true;
}

void app_goto_node(uint32_t n) {
    if (!G.loaded || n >= G.frd.n_nodes) return;
    float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f;
    const float* p = G.frd.xyz + 3 * n;
    const float* d = G.disp ? G.disp + 3 * n : NULL;
    G.cam.target = v3_make(p[0] + (d ? d[0] * sc : 0), p[1] + (d ? d[1] * sc : 0), p[2] + (d ? d[2] * sc : 0));
}

/* a node or element by its id in the file: probe it and look at it */
bool app_find(uint32_t id, bool element) {
    if (!G.loaded) return false;
    uint32_t e = UINT32_MAX, n = UINT32_MAX;
    if (element) {
        e = cv_frd_elem_index(&G.frd, id);
        if (e == UINT32_MAX) return false;
        n = G.frd.conn[G.frd.eoff[e]];
    } else {
        n = cv_frd_node_index(&G.frd, id);
        if (n == UINT32_MAX) return false;
        for (uint32_t k = 0; k < G.frd.n_elems && e == UINT32_MAX; k++)     /* any element holding it */
            for (uint32_t j = G.frd.eoff[k]; j < G.frd.eoff[k + 1]; j++) if (G.frd.conn[j] == n) { e = k; break; }
    }
    G.probe.hit = true; G.probe.elem = e == UINT32_MAX ? 0 : e; G.probe.node = n; G.probe.t = 0;
    G.probe_on = e != UINT32_MAX;
    G.probe_ip = 0;
    G.probe_value = !G.has_field ? NAN : G.elem_mode ? (e != UINT32_MAX ? G.elem_val[e] : NAN) : G.scalar[n];
    if (element && e != UINT32_MAX) app_fit_element(e); else app_goto_node(n);
    return true;
}

void app_fit_element(uint32_t e) {
    if (!G.loaded || e >= G.frd.n_elems) return;
    float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f;
    v3 lo = v3_make(INFINITY, INFINITY, INFINITY), hi = v3_make(-INFINITY, -INFINITY, -INFINITY);
    for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) {
        const float* p = G.frd.xyz + 3 * G.frd.conn[j];
        const float* d = G.disp ? G.disp + 3 * G.frd.conn[j] : NULL;
        v3 q = v3_make(p[0] + (d ? d[0] * sc : 0), p[1] + (d ? d[1] * sc : 0), p[2] + (d ? d[2] * sc : 0));
        lo.x = fminf(lo.x, q.x); lo.y = fminf(lo.y, q.y); lo.z = fminf(lo.z, q.z);
        hi.x = fmaxf(hi.x, q.x); hi.y = fmaxf(hi.y, q.y); hi.z = fmaxf(hi.z, q.z);
    }
    G.cam.target = v3_scale(v3_add(lo, hi), 0.5f);
    v3 ext = v3_sub(hi, lo);
    float r = 0.5f * sqrtf(v3_dot(ext, ext));
    if (r > 0) G.cam.dist = r / sinf(G.cam.fovy * 0.5f) * 3.f;     /* the element fills a third of the view */
}

void app_view(int p) {
    const float d2r = 3.14159265f / 180.f, lim = 89.9f * d2r;
    /* eye direction of each axis view, in world coordinates */
    static const float D[6][3] = { {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1} };
    if (p >= CV_VIEW_PX && p <= CV_VIEW_NZ) {
        const float* w = D[p - CV_VIEW_PX];
        v3 f = to_frame(v3_make(w[0], w[1], w[2]));
        G.cam.pitch = CV_MAX(-lim, CV_MIN(lim, asinf(f.y)));
        G.cam.yaw = fabsf(f.y) > 0.99f ? 0.f : atan2f(f.x, f.z);   /* along the up axis: yaw 0 */
    } else {                                   /* iso: from above, turned about the up axis */
        G.cam.yaw = 35 * d2r; G.cam.pitch = 30 * d2r;
    }
    app_fit();
}

void cam_matrices(float* mvp, float* mv, float* proj_out) {
    v3 eye, fwd, right, up;
    cam_basis(&G.cam, &eye, &fwd, &right, &up);
    float view[16], proj[16];
    m4_look_at(view, eye, fwd, right, up);
    float aspect = G.vp_h > 0 ? (float)G.vp_w / (float)G.vp_h : 1.f;
    float diag = CV_MAX(CV_MAX(G.vdiag, G.diag), 1e-6f);
    /* depth range from the eye's distance to the model (deformed + undeformed box),
       not to the orbit target: in free flight the eye can be far from, or inside, it */
    v3 c = v3_scale(v3_add(G.vmin, G.vmax), 0.5f);
    v3 ec = v3_sub(eye, c);
    float dc = sqrtf(v3_dot(ec, ec));
    if (G.cam.ortho) {
        float hh = G.cam.dist * tanf(G.cam.fovy * 0.5f);
        m4_ortho(proj, hh * aspect, hh, dc - 2 * diag, dc + 2 * diag);
    } else {
        float farp = dc + diag;
        float nearp = CV_MAX(dc - diag, diag * 1e-4f);
        m4_perspective(proj, G.cam.fovy, aspect, nearp, farp);
    }
    memcpy(mv, view, sizeof view);
    if (proj_out) memcpy(proj_out, proj, sizeof proj);
    m4_mul(mvp, proj, view);
}

/* Ray through window pixel (px, py). */
static void cam_ray(float px, float py, float o[3], float d[3]) {
    v3 eye, fwd, right, up;
    cam_basis(&G.cam, &eye, &fwd, &right, &up);
    float nx = ((px - G.vp_x) / (float)G.vp_w) * 2.f - 1.f;
    float ny = 1.f - ((py - G.vp_y) / (float)G.vp_h) * 2.f;
    float aspect = (float)G.vp_w / (float)CV_MAX(G.vp_h, 1);
    float th = tanf(G.cam.fovy * 0.5f);
    v3 org = eye, dir;
    if (G.cam.ortho) {
        float hh = G.cam.dist * th;
        org = v3_add(eye, v3_add(v3_scale(right, nx * hh * aspect), v3_scale(up, ny * hh)));
        org = v3_sub(org, v3_scale(fwd, 4 * G.diag));
        dir = fwd;
    } else {
        dir = v3_norm(v3_add(fwd, v3_add(v3_scale(right, nx * th * aspect), v3_scale(up, ny * th))));
    }
    o[0] = org.x; o[1] = org.y; o[2] = org.z;
    d[0] = dir.x; d[1] = dir.y; d[2] = dir.z;
}

/* The point under window pixel (px, py): the model surface (mirror copies too)
   when the ray hits it (*on_model), else the point at the orbit target's depth. */
bool app_cursor_point(float px, float py, v3* out, bool* on_model) {
    *on_model = false;
    if (!G.loaded) return false;
    float o[3], d[3];
    cam_ray(px, py, o, d);
    float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f;
    int en = (G.sym[0] ? 1 : 0) | (G.sym[1] ? 2 : 0) | (G.sym[2] ? 4 : 0);
    float best = INFINITY;
    for (int m = 0; m < 8; m++) {
        if ((m & en) != m) continue;
        float mo[3], md[3];
        for (int k = 0; k < 3; k++) {       /* a reflection keeps distances: t compares directly */
            bool r = m >> k & 1;
            mo[k] = r ? 2.f * app_sym_plane(k) - o[k] : o[k];
            md[k] = r ? -d[k] : d[k];
        }
        cv_pick p = cv_pick_ray(&G.frd, &G.skin, G.disp, sc, mo, md);
        if (p.hit && p.t < best) best = p.t;
    }
    v3 org = v3_make(o[0], o[1], o[2]), dir = v3_make(d[0], d[1], d[2]);
    if (best < INFINITY) { *out = v3_add(org, v3_scale(dir, best)); *on_model = true; return true; }
    v3 eye, fwd, right, up;
    cam_basis(&G.cam, &eye, &fwd, &right, &up);
    float dn = v3_dot(dir, fwd);
    if (!(dn > 1e-6f)) return false;
    *out = v3_add(org, v3_scale(dir, v3_dot(v3_sub(G.cam.target, org), fwd) / dn));
    return true;
}

void do_pick(float px, float py) {
    if (!G.loaded) return;
    float o[3], d[3];
    cam_ray(px, py, o, d);
    float sc = G.deform ? G.deform_scale * G.anim_factor : 0.f;
    G.probe = cv_pick_ray(&G.frd, &G.skin, G.disp, sc, o, d);
    /* mirror copies: the reflected ray through the model itself (a reflection is
       its own inverse and keeps distances, so t compares directly) */
    int en = (G.sym[0] ? 1 : 0) | (G.sym[1] ? 2 : 0) | (G.sym[2] ? 4 : 0);
    for (int m = 1; en && m < 8; m++) {
        if ((m & en) != m) continue;
        float mo[3], md[3];
        for (int k = 0; k < 3; k++) {
            bool r = m >> k & 1;
            mo[k] = r ? 2.f * app_sym_plane(k) - o[k] : o[k];
            md[k] = r ? -d[k] : d[k];
        }
        cv_pick p = cv_pick_ray(&G.frd, &G.skin, G.disp, sc, mo, md);
        if (p.hit && (!G.probe.hit || p.t < G.probe.t)) {
            G.probe = p;
            memcpy(o, mo, sizeof o); memcpy(d, md, sizeof d);   /* gp_probe works in the model's frame */
        }
    }
    G.probe_on = G.probe.hit;
    G.probe_ip = 0;
    if (G.probe.hit && G.path_arm) app_path_end(G.probe.node);
    if (G.probe.hit) {
        G.probe_value = !G.has_field ? NAN
                      : (G.elem_mode || G.field_src == 1) ? G.elem_val[G.probe.elem] : G.scalar[G.probe.node];
        if (G.field_src == 1 && G.has_field) {    /* the integration point nearest the click */
            float h[3] = { o[0] + d[0] * G.probe.t, o[1] + d[1] * G.probe.t, o[2] + d[2] * G.probe.t };
            int ip; float v;
            if (gp_probe(G.probe.elem, h, sc, &ip, &v)) { G.probe_ip = ip; G.probe_value = v; }
        }
    }
}

