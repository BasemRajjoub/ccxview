/* app_path.c -- the field sampled along something: the path plot (its line and
   pick markers, picking its ends, a ray into the solid, the samples, CSV) and
   the history plot (the field at one node over every step). */
#include "app_int.h"
#include "calc.h"
#include "gauss.h"
#include "path.h"
#include "export.h"     /* cv_fprintf */
#include <math.h>

/* ---- path plot ------------------------------------------------------------------- */

static void path_vertex(cv_fvec* pos, cv_fvec* disp, uint32_t n) {
    float d[6];
    app_node_disp6(n, d);
    for (int k = 0; k < 3; k++) cv_push(*pos, G.frd.xyz[3 * (size_t)n + k]);
    for (int k = 0; k < 6; k++) cv_push(*disp, d[k]);
}

/* the displacement (6 wide) at every straight-path sample: interpolated in its
   element, the node's own at a node end, and between the neighbours where the
   sample is outside the solid */
static float* path_disp(void) {
    uint32_t n = G.path_n;
    float* d = calloc((size_t)n * 6, sizeof(float));
    uint8_t* ok = calloc(n, 1);
    if (!d || !ok) { free(d); free(ok); return NULL; }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t node = i == 0 ? G.path_end[0] : i == n - 1 ? G.path_end[1] : UINT32_MAX, e = G.path_el[i];
        if (node < G.frd.n_nodes) { app_node_disp6(node, d + 6 * i); ok[i] = 1; continue; }
        if (e == UINT32_MAX) continue;
        int t = G.frd.etype[e];
        uint32_t b = G.frd.eoff[e], nn = G.frd.eoff[e + 1] - b;
        for (uint32_t j = 0; j < nn; j++) {
            float q[6];
            app_node_disp6(G.frd.conn[b + (uint32_t)cv_frd_node_pos(t, (int)nn, (int)j)], q);
            for (int k = 0; k < 6; k++) d[6 * i + k] += G.path_w[20 * i + j] * q[k];
        }
        ok[i] = 1;
    }
    for (uint32_t i = 0; i < n; i++) {                  /* the gaps: between the known ones */
        if (ok[i]) continue;
        int lo = (int)i - 1, hi = (int)i + 1;
        while (lo >= 0 && !ok[lo]) lo--;
        while (hi < (int)n && !ok[hi]) hi++;
        for (int k = 0; k < 6; k++) {
            float a = lo >= 0 ? d[6 * lo + k] : hi < (int)n ? d[6 * hi + k] : 0, c = hi < (int)n ? d[6 * hi + k] : a;
            float f = lo >= 0 && hi < (int)n ? (float)(i - lo) / (hi - lo) : 0;
            d[6 * i + k] = a + f * (c - a);
        }
    }
    free(ok);
    return d;
}

/* a segment as short pieces, positions and displacements linear along it: drawn
   as lines and, over them, as dots, so the path reads as a thick line */
#define PATH_SUB 6
static void seg_push(cv_fvec* pos, cv_fvec* disp, const float p0[3], const float d0[6], const float p1[3], const float d1[6]) {
    for (int j = 0; j < PATH_SUB; j++)
        for (int e = 0; e < 2; e++) {
            float f = (float)(j + e) / PATH_SUB;
            for (int k = 0; k < 3; k++) cv_push(*pos, p0[k] + f * (p1[k] - p0[k]));
            for (int k = 0; k < 6; k++) cv_push(*disp, d0[k] + f * (d1[k] - d0[k]));
        }
}

/* the path plot's line, the (linearized) straight line, and a ball on every pick */
void refresh_path(void) {
    cv_fvec pos = {0}, disp = {0};
    float ends[2][3], ed6[2][6];
    bool line = G.loaded && G.path_n > 0;
    if (line && G.path_surface && G.path_nodes)
        for (uint32_t i = 0; i + 1 < G.path_n; i++) {
            float d0[6], d1[6];
            uint32_t a = G.path_nodes[i], b = G.path_nodes[i + 1];
            app_node_disp6(a, d0); app_node_disp6(b, d1);
            seg_push(&pos, &disp, G.frd.xyz + 3 * (size_t)a, d0, G.frd.xyz + 3 * (size_t)b, d1);
        }
    if (line) {
        memcpy(ends, G.path_p, sizeof ends);
        if (!G.path_surface && G.path_el) {             /* the sampled line, bending with the model */
            float* d = path_disp();
            for (uint32_t i = 0; d && i + 1 < G.path_n; i++) {
                float f0 = (float)i / (G.path_n - 1), f1 = (float)(i + 1) / (G.path_n - 1), p0[3], p1[3];
                for (int k = 0; k < 3; k++) { p0[k] = ends[0][k] + f0 * (ends[1][k] - ends[0][k]); p1[k] = ends[0][k] + f1 * (ends[1][k] - ends[0][k]); }
                seg_push(&pos, &disp, p0, d + 6 * i, p1, d + 6 * (i + 1));
            }
            if (d) { memcpy(ed6[0], d, sizeof ed6[0]); memcpy(ed6[1], d + 6 * (G.path_n - 1), sizeof ed6[1]); }
            else memset(ed6, 0, sizeof ed6);
            free(d);
        } else {                                        /* surface mode: the chord between the two nodes */
            app_node_disp6(G.path_end[0], ed6[0]); app_node_disp6(G.path_end[1], ed6[1]);
            seg_push(&pos, &disp, ends[0], ed6[0], ends[1], ed6[1]);
        }
    }
    app_aux_upload(CV_AUX_PATHLN, &pos, &disp, NULL);
    pos.n = disp.n = 0;
    if (line && !G.path_surface) {                      /* the line on both sides, to the model's box */
        const float *A = ends[0], *B = ends[1];
        float u[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, L = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z }, t0 = -INFINITY, t1 = INFINITY;
        for (int k = 0; k < 3 && L > 0; k++) {
            float m = 0.05f * G.diag;
            u[k] /= L;
            if (fabsf(u[k]) < 1e-12f) continue;
            float a = (lo[k] - m - A[k]) / u[k], b = (hi[k] + m - A[k]) / u[k];
            t0 = CV_MAX(t0, CV_MIN(a, b)); t1 = CV_MIN(t1, CV_MAX(a, b));
        }
        if (L > 0 && t0 < 0 && t1 > L) {
            float P0[3], P1[3];
            for (int k = 0; k < 3; k++) { P0[k] = A[k] + t0 * u[k]; P1[k] = A[k] + t1 * u[k]; }
            seg_push(&pos, &disp, P0, ed6[0], A, ed6[0]);
            seg_push(&pos, &disp, B, ed6[1], P1, ed6[1]);
        }
    }
    app_aux_upload(CV_AUX_RAYLN, &pos, &disp, NULL);
    pos.n = disp.n = 0;
    if (G.loaded) {
        uint32_t m[3];
        int k = 0;
        if (G.hist_open) m[k++] = G.hist_node;       /* stays while its history plot is open, whatever the step */
        if (G.probe_on && G.probe.hit) m[k++] = G.probe.node;
        if (G.path_arm && G.path_a != UINT32_MAX) m[k++] = G.path_a;
        for (int i = 0; i < k; i++) if (m[i] < G.frd.n_nodes) path_vertex(&pos, &disp, m[i]);
        for (int e = 0; line && e < 2; e++) {
            for (int c = 0; c < 3; c++) cv_push(pos, ends[e][c]);
            for (int c = 0; c < 6; c++) cv_push(disp, ed6[e][c]);
        }
    }
    app_aux_upload(CV_AUX_PICKPT, &pos, &disp, NULL);
    cv_free_vec(pos); cv_free_vec(disp);
}

/* the markers follow the picks: redrawn when one of them (or the model or
   step under them) changes */
void app_marks_sync(void) {
    uint64_t k[] = { G.loaded, (uintptr_t)G.frd.xyz, (uintptr_t)G.disp, (uint64_t)G.step,
                     G.probe_on && G.probe.hit ? G.probe.node : UINT32_MAX,
                     G.path_arm ? G.path_a : UINT32_MAX,
                     G.hist_open ? G.hist_node : UINT32_MAX,
                     G.path_n ? G.path_gen : UINT64_MAX };
    static uint64_t last[sizeof k / sizeof k[0]];
    if (!memcmp(k, last, sizeof k)) return;
    memcpy(last, k, sizeof k);
    refresh_path();
}

/* ---- path picking: the two ends, a ray into the solid, the samples ---------------- */

static void path_free(void) {
    free(G.path_nodes); free(G.path_dist); free(G.path_el); free(G.path_w);
    G.path_nodes = NULL; G.path_dist = NULL; G.path_el = NULL; G.path_w = NULL; G.path_n = 0;
}

void app_path_clear(void) {
    path_free();
    app_lin_close();
    G.path_a = UINT32_MAX; G.path_arm = false; G.path_open = false;
    refresh_path();
}

void app_path_start(uint32_t node) {
    app_path_clear();
    app_measure_cancel();                           /* one thing at a time waits for a click */
    G.path_a = node;
    G.path_arm = true;
}

void app_pick_cancel(void) {
    if (G.path_arm) { G.path_arm = false; G.path_a = UINT32_MAX; }
    app_measure_cancel();
    G.box_arm = false;
    if (G.menu_on) { G.menu_on = false; return; }   /* Esc closes the menu first, the selection stays */
    if (G.sel_tool != CV_ST_NONE) { G.sel_tool = CV_ST_NONE; return; }   /* then a selection tool, the selection stays */
    app_sel_clear();
}

/* the nearest node that has skin edges: a mid-edge node of a quadratic
   element (the usual click target) is not part of the edge graph */
static uint32_t snap_to_edges(uint32_t n) {
    if (n >= G.frd.n_nodes) return n;
    uint8_t* has = calloc(G.frd.n_nodes, 1);
    if (!has) return n;
    for (size_t i = 0; i < 2 * G.skin.n_edge; i++) has[G.skin.edge[i]] = 1;
    uint32_t best = n;
    if (!has[n]) {
        const float* p = G.frd.xyz + 3 * n;
        float bd = INFINITY;
        for (size_t i = 0; i < G.skin.n_pt; i++) {
            uint32_t m = G.skin.pt[i];
            if (!has[m]) continue;
            const float* q = G.frd.xyz + 3 * m;
            float d = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]);
            if (d < bd) { bd = d; best = m; }
        }
    }
    free(has);
    return best;
}

#define PATH_SN 121                                 /* samples on a straight path */

/* a skin triangle's normal times twice its area (undeformed) */
static void tri_normal(size_t t, double s[3]) {
    const uint32_t* v = G.skin.tri + 3 * t;
    const float *a = G.frd.xyz + 3 * (size_t)v[0], *b = G.frd.xyz + 3 * (size_t)v[1], *c = G.frd.xyz + 3 * (size_t)v[2];
    double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, w[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    s[0] = u[1] * w[2] - u[2] * w[1]; s[1] = u[2] * w[0] - u[0] * w[2]; s[2] = u[0] * w[1] - u[1] * w[0];
}

/* the surface normals at a skin node, one per face through it (the sign does not
   matter: path_ray tries both ways): the area-weighted mean of the skin
   triangles around the node (the nearest corner node for a mid-edge one) that
   lie on one smooth surface, so a faceted curve gives its true normal. With a
   clicked triangle, the one surface it is on: the triangles within 30 degrees
   of it. Without, the triangles grouped by direction, so a node on an edge (a
   model cut at a symmetry plane, say) gives each face's normal and not the
   bisector. */
#define NRM_MAX 6
static int node_normals(uint32_t n, uint32_t tri, float d[NRM_MAX][3]) {
    double s[3], l;
    if (tri < G.skin.n_tri) {
        double c[3], cl;
        tri_normal(tri, c);
        if ((cl = sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2])) > 0) {
            for (int pass = 0; pass < 2; pass++, n = snap_to_edges(n)) {
                double m[3] = { 0, 0, 0 };
                for (size_t t = 0; t < G.skin.n_tri; t++) {
                    const uint32_t* v = G.skin.tri + 3 * t;
                    if (v[0] != n && v[1] != n && v[2] != n) continue;
                    tri_normal(t, s);
                    l = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
                    if (l > 0 && (s[0] * c[0] + s[1] * c[1] + s[2] * c[2]) / (l * cl) > 0.866)
                        for (int k = 0; k < 3; k++) m[k] += s[k];
                }
                if ((l = sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2])) > 0) { for (int k = 0; k < 3; k++) d[0][k] = (float)(m[k] / l); return 1; }
            }
            for (int k = 0; k < 3; k++) d[0][k] = (float)(c[k] / cl);   /* the node is not on it: the face alone */
            return 1;
        }
    }
    for (int pass = 0; pass < 2; pass++, n = snap_to_edges(n)) {
        double g[NRM_MAX][3] = { { 0 } };
        int ng = 0;
        for (size_t t = 0; t < G.skin.n_tri; t++) {
            const uint32_t* v = G.skin.tri + 3 * t;
            if (v[0] != n && v[1] != n && v[2] != n) continue;
            tri_normal(t, s);
            if (!((l = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2])) > 0)) continue;
            int j = 0;
            for (; j < ng; j++) {                           /* the group within ~37 degrees */
                double gl = sqrt(g[j][0] * g[j][0] + g[j][1] * g[j][1] + g[j][2] * g[j][2]);
                if ((s[0] * g[j][0] + s[1] * g[j][1] + s[2] * g[j][2]) / (l * gl) > 0.8) break;
            }
            if (j == ng) { if (ng == NRM_MAX) continue; ng++; }
            for (int k = 0; k < 3; k++) g[j][k] += s[k];
        }
        for (int j = 0; j < ng; j++) {
            l = sqrt(g[j][0] * g[j][0] + g[j][1] * g[j][1] + g[j][2] * g[j][2]);
            for (int k = 0; k < 3; k++) d[j][k] = (float)(g[j][k] / l);
        }
        if (ng) return ng;
    }
    return 0;
}

/* from p along d to where the line first leaves the solid: the nearest skin
   triangle it crosses beyond p (not the faces at p itself), if the stretch up to
   it is solid (its middle is in an element). Exact, so a wall thin against the
   model is not missed. */
static bool ray_exit(const float p[3], const float d[3], float out[3], float* len, size_t* hit) {
    double best = INFINITY, eps = 1e-4 * G.diag;
    for (size_t t = 0; t < G.skin.n_tri; t++) {         /* Moller-Trumbore */
        const uint32_t* v = G.skin.tri + 3 * t;
        const float *a = G.frd.xyz + 3 * (size_t)v[0], *b = G.frd.xyz + 3 * (size_t)v[1], *c = G.frd.xyz + 3 * (size_t)v[2];
        double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        double h[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
        double det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
        if (fabs(det) < 1e-12 * (e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2])) continue;   /* along the face */
        double f = 1 / det, sv[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
        double u = f * (sv[0] * h[0] + sv[1] * h[1] + sv[2] * h[2]);
        if (u < -1e-6 || u > 1 + 1e-6) continue;
        double q[3] = { sv[1] * e1[2] - sv[2] * e1[1], sv[2] * e1[0] - sv[0] * e1[2], sv[0] * e1[1] - sv[1] * e1[0] };
        double w = f * (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]);
        if (w < -1e-6 || u + w > 1 + 1e-6) continue;
        double tt = f * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]);
        if (tt > eps && tt < best) { best = tt; *hit = t; }
    }
    if (!(best < INFINITY)) return false;
    float B[3], el_w[3 * 20];
    uint32_t el[3];
    for (int k = 0; k < 3; k++) B[k] = p[k] + (float)best * d[k];
    line_locate(p, B, 3, el, el_w);                     /* the middle: solid, or the way out */
    if (el[1] == UINT32_MAX) return false;
    memcpy(out, B, sizeof B);
    *len = (float)best;
    return true;
}

/* the far end of a ray from node n: along the normal or an axis, whichever way
   goes into the solid. With several faces at the node (an edge), the normal
   that goes straight through a wall, coming out of a face parallel to the one
   it went in by, the shortest of those; else the shortest. */
static bool path_ray(uint32_t n, int dir, float out[3]) {
    float d[NRM_MAX][3] = { { 0 } };
    int nd = 1;
    if (dir == 1) { if (!(nd = node_normals(n, G.path_tri, d))) return false; }
    else d[0][CV_MIN(dir - 2, 2)] = 1;
    const float* p = G.frd.xyz + 3 * (size_t)n;
    float best = INFINITY;
    bool best_wall = false;
    for (int j = 0; j < nd; j++) {
        float q[2][3], len[2] = { 0, 0 };
        size_t hit[2] = { 0, 0 };
        for (int s = 0; s < 2; s++) {                   /* both ways: the one into the solid */
            if (s) for (int k = 0; k < 3; k++) d[j][k] = -d[j][k];
            if (!ray_exit(p, d[j], q[s], &len[s], &hit[s])) len[s] = 0;
        }
        int s = len[1] > len[0];
        if (!(len[s] > 0)) continue;                    /* not into the solid */
        double hn[3];
        tri_normal(hit[s], hn);
        double hl = sqrt(hn[0] * hn[0] + hn[1] * hn[1] + hn[2] * hn[2]);
        bool wall = dir != 1 || (hl > 0 && fabs(hn[0] * d[j][0] + hn[1] * d[j][1] + hn[2] * d[j][2]) / hl > 0.9);
        if (best_wall && !wall) continue;
        if (wall == best_wall && len[s] >= best) continue;
        best = len[s]; best_wall = wall;
        float back = 1e-5f * G.diag / len[s];           /* a hair back in: the last sample is found inside */
        for (int k = 0; k < 3; k++) out[k] = q[s][k] - back * (q[s][k] - p[k]);
    }
    return best < INFINITY;
}

/* the path between path_end[0] and [1]: straight, sampled in the elements, or
   over the surface edges. The straight line between the two is linearized
   either way (a stress classification line is straight). */
void app_path_rebuild(void) {
    path_free();
    G.path_gen++;
    uint32_t a = G.path_end[0], b = G.path_end[1];
    memcpy(G.path_p[0], G.frd.xyz + 3 * (size_t)a, sizeof G.path_p[0]);
    if (G.path_dir) {
        static const char* dn[] = { "", "the normal", "X", "Y", "Z" };
        G.path_surface = false;
        G.path_end[1] = b = UINT32_MAX;
        if (!path_ray(a, G.path_dir, G.path_p[1])) {
            char m[120];
            snprintf(m, sizeof m, "no solid along %s from node %u", dn[CV_MIN(G.path_dir, 4)], G.frd.node_id[a]);
            cv_msg_add(&G.msgs, 0, false, m);
            app_lin_close();
            refresh_path();
            return;
        }
    } else memcpy(G.path_p[1], G.frd.xyz + 3 * (size_t)b, sizeof G.path_p[1]);
    if (G.path_surface) {
        a = snap_to_edges(a); b = snap_to_edges(b);
        if (a == b || !cv_path_find(&G.frd, &G.skin, a, b, &G.path_nodes, &G.path_n, &G.path_dist)) {
            path_free();
            cv_msg_add(&G.msgs, 0, false, "no surface path between the two nodes: back to the straight line");
            G.path_surface = false;
            app_path_rebuild();
            return;
        }
        app_lin_open(G.path_p[0], G.path_p[1], G.path_end[0], G.path_end[1]);
    } else {
        G.path_dist = malloc(PATH_SN * sizeof(float));
        G.path_el = malloc(PATH_SN * sizeof(uint32_t));
        G.path_w = malloc(PATH_SN * 20 * sizeof(float));
        if (G.path_dist && G.path_el && G.path_w) {
            const float *A = G.path_p[0], *B = G.path_p[1];
            float L = sqrtf((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2]));
            line_locate(A, B, PATH_SN, G.path_el, G.path_w);
            for (int i = 0; i < PATH_SN; i++) G.path_dist[i] = L * i / (PATH_SN - 1);
            G.path_n = PATH_SN;
            app_lin_open(A, B, a, b);
        } else path_free();
    }
    refresh_path();
}

void app_path_end(uint32_t node) {
    G.path_arm = false;
    if (G.path_a == UINT32_MAX || node == G.path_a) return;
    G.path_end[0] = G.path_a; G.path_end[1] = G.path_to = node;
    G.path_tri = G.probe_on && G.probe.hit && G.probe.node == G.path_a ? G.probe.tri : UINT32_MAX;
    G.path_dir = 0;
    app_path_rebuild();
    G.path_open = G.path_n > 0;
}

void app_path_ray(uint32_t node, int dir) {
    uint32_t tri = G.probe_on && G.probe.hit && G.probe.node == node ? G.probe.tri : UINT32_MAX;
    app_path_clear();
    if (node >= G.frd.n_nodes) return;
    G.path_tri = tri;
    G.path_dir = CV_MAX(1, CV_MIN(dir, 4));
    G.path_end[0] = node;
    app_path_rebuild();
    G.path_open = G.path_n > 0;
}

float app_path_value(uint32_t i) {
    if (!G.scalar || i >= G.path_n) return NAN;
    if (G.path_surface) return G.path_nodes ? G.scalar[G.path_nodes[i]] : NAN;
    float v;
    return G.path_el && line_interp(G.path_el[i], G.path_w + 20 * i, G.scalar, 1, &v) ? v : NAN;
}

bool app_path_csv(const char* path) {
    if (!G.path_n) return false;
    FILE* o = fopen(path, "wb");
    if (!o) return false;
    cv_fprintf(o, "distance,id,x,y,z,%s\n", G.has_field ? G.field_label : "value");
    const float *A = G.path_p[0], *B = G.path_p[1];
    for (uint32_t i = 0; i < G.path_n; i++) {
        float p[3], f = G.path_n > 1 ? (float)i / (G.path_n - 1) : 0;
        uint32_t id = 0;
        if (G.path_surface) { memcpy(p, G.frd.xyz + 3 * (size_t)G.path_nodes[i], sizeof p); id = G.frd.node_id[G.path_nodes[i]]; }
        else for (int k = 0; k < 3; k++) p[k] = A[k] + f * (B[k] - A[k]);
        float v = G.has_field && !G.elem_mode ? app_path_value(i) : NAN;
        if (id) cv_fprintf(o, "%.9g,%u,%.9g,%.9g,%.9g,", G.path_dist[i], id, p[0], p[1], p[2]);
        else cv_fprintf(o, "%.9g,,%.9g,%.9g,%.9g,", G.path_dist[i], p[0], p[1], p[2]);
        if (v == v) cv_fprintf(o, "%.9g\n", v); else cv_fprintf(o, "nan\n");
    }
    return fclose(o) == 0;
}

/* ---- history: the field at one node over every step ------------------------------
   Each step's field is decoded into a scratch buffer (not the cache, which holds
   what is on screen); rebuilt only when the field, component, node or system change. */

/* a step's field for the history (and the integrals): from the cache when there,
   else decoded into a scratch buffer, so it does not push out what is on screen */
const float* step_field_get(void* ud, int s, int fi) {
    step_scratch* h = ud;
    for (int i = 0; i < CV_CACHE_N; i++)
        if (G.cache[i].vals && G.cache[i].step == s && G.cache[i].field == fi) return G.cache[i].vals;
    const cv_field_desc* d = &G.frd.steps[s].fields[fi];
    size_t need = (size_t)CV_MAX(G.frd.n_nodes, 1) * (size_t)d->ncomp;
    if (need > h->cap) { float* nb = realloc(h->buf, need * sizeof(float)); if (!nb) return NULL; h->buf = nb; h->cap = need; }
    field_read(s, d, h->buf, NULL);
    deck_localize(s, d, h->buf);
    units_apply(d->name, d->ncomp, h->buf, G.frd.n_nodes);
    return h->buf;
}

void refresh_hist(void) {
    if (!G.hist_open || !G.loaded) return;
    char key[480];
    snprintf(key, sizeof key, "%s|%d|%d|%g|%g|%g|%u|%u|%d|%d|%d|%s", G.field_name, G.comp, G.csys, G.csys_o[0], G.csys_o[1],
             G.csys_o[2], G.hist_node, G.hist_elem, G.elem_mode, G.field_src, G.frd.n_steps, G.field_src == 2 ? G.calc_expr : "");
    if (!strcmp(key, G.hist_key)) return;
    snprintf(G.hist_key, sizeof G.hist_key, "%s", key);
    free(G.hist_t); free(G.hist_v); free(G.hist_step);
    G.hist_t = G.hist_v = NULL; G.hist_step = NULL; G.hist_n = 0;
    if (G.field_src == 1 || (G.field_src == 2 && !G.calc) || G.frd.n_steps == 0 || G.hist_node >= G.frd.n_nodes) return;
    int ns = G.frd.n_steps;
    G.hist_t = malloc((size_t)ns * sizeof(float)); G.hist_v = malloc((size_t)ns * sizeof(float));
    G.hist_step = malloc((size_t)ns * sizeof(int));
    if (!G.hist_t || !G.hist_v || !G.hist_step) return;
    /* the nodes averaged: one, or the element's */
    uint32_t one = G.hist_node, *nodes = &one, nn = 1;
    if (G.elem_mode && G.hist_elem < G.frd.n_elems) {
        nodes = G.frd.conn + G.frd.eoff[G.hist_elem];
        nn = G.frd.eoff[G.hist_elem + 1] - G.frd.eoff[G.hist_elem];
    }
    if (G.field_src == 2) {                             /* the formula at those nodes, step by step */
        step_scratch h = { NULL, 0 };
        float* x = malloc((size_t)CV_MAX(nn, 1) * sizeof(float));
        for (int s = 0; x && s < ns; s++) {
            if (!cv_calc_eval(G.calc, &G.frd, s, step_field_get, &h, nodes, nn, x)) continue;
            double sum = 0; int cnt = 0;
            for (uint32_t j = 0; j < nn; j++) if (x[j] == x[j]) { sum += x[j]; cnt++; }
            G.hist_t[G.hist_n] = G.frd.steps[s].time;
            G.hist_v[G.hist_n] = cnt ? (float)(sum / cnt) : NAN;
            G.hist_step[G.hist_n++] = s;
        }
        free(x); free(h.buf);
        return;
    }
    float* buf = NULL; size_t cap = 0;
    for (int s = 0; s < ns; s++) {
        int fi = find_field(s, G.field_name);
        if (fi < 0) continue;
        const cv_field_desc* d = &G.frd.steps[s].fields[fi];
        if (G.comp >= d->ncomp) continue;
        const float* v = NULL;
        for (int i = 0; i < CV_CACHE_N; i++)            /* already decoded? */
            if (G.cache[i].vals && G.cache[i].step == s && G.cache[i].field == fi) v = G.cache[i].vals;
        if (!v) {
            size_t need = (size_t)CV_MAX(G.frd.n_nodes, 1) * (size_t)d->ncomp;
            if (need > cap) { float* nb = realloc(buf, need * sizeof(float)); if (!nb) break; buf = nb; cap = need; }
            field_read(s, d, buf, NULL);
            deck_localize(s, d, buf);
            units_apply(d->name, d->ncomp, buf, G.frd.n_nodes);
            v = buf;
        }
        double sum = 0; int cnt = 0;
        for (uint32_t j = 0; j < nn; j++) {
            uint32_t n = nodes[j];
            float r[CV_MAX_COMP], x;
            memcpy(r, v + (size_t)n * d->ncomp, (size_t)d->ncomp * sizeof(float));
            if (G.csys > 0 && G.comp >= 0 && cv_cyl_applies(d)) cv_cyl_values(d, G.frd.xyz + 3 * (size_t)n, 1, G.csys - 1, G.csys_o, r);
            cv_field_scalar(r, d->ncomp, 1, G.comp, &x);
            if (x == x) { sum += x; cnt++; }
        }
        G.hist_t[G.hist_n] = G.frd.steps[s].time;
        G.hist_v[G.hist_n] = cnt ? (float)(sum / cnt) : NAN;
        G.hist_step[G.hist_n++] = s;
    }
    free(buf);
}

void app_hist_open(uint32_t node, uint32_t elem) {
    G.hist_node = node; G.hist_elem = elem;
    G.hist_open = true;
    G.hist_by_step = false;                     /* time, unless it does not run forward (modal: frequencies) */
    for (int s = 0; s < G.frd.n_steps; s++)
        if (G.frd.steps[s].modal || (s && G.frd.steps[s].time < G.frd.steps[s - 1].time)) G.hist_by_step = true;
    G.hist_key[0] = 0;
    refresh_hist();
}

void app_hist_close(void) {
    free(G.hist_t); free(G.hist_v); free(G.hist_step);
    G.hist_t = G.hist_v = NULL; G.hist_step = NULL; G.hist_n = 0;
    G.hist_open = false; G.hist_key[0] = 0;
}

bool app_hist_csv(const char* path) {
    if (!G.hist_n) return false;
    FILE* o = fopen(path, "wb");
    if (!o) return false;
    cv_fprintf(o, "step,time,%s %s %u\n", G.field_label, G.elem_mode ? "element" : "node",
            G.elem_mode && G.hist_elem < G.frd.n_elems ? G.frd.elem_id[G.hist_elem] : G.frd.node_id[G.hist_node]);
    for (int i = 0; i < G.hist_n; i++) {
        cv_fprintf(o, "%d,%.9g,", G.hist_step[i] + 1, G.hist_t[i]);
        if (G.hist_v[i] == G.hist_v[i]) cv_fprintf(o, "%.9g\n", G.hist_v[i]); else cv_fprintf(o, "nan\n");
    }
    return fclose(o) == 0;
}
