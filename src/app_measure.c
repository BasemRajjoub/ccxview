/* app_measure.c -- measurements between nodes, shown as labels (measure.h does the
   geometry): the distance between two nodes with its components, the angle at the
   middle one of three, the circle through three. Each is kept by its file node ids,
   so it outlives a reload; another model clears them. Each has two values: on the
   undeformed mesh, and deformed by the step's DISP at scale 1 (the true deformed
   shape, whatever the scale on screen). Its lines and its label follow the shape
   as drawn. */
#include "app_int.h"
#include "measure.h"
#include "export.h"     /* cv_fprintf */
#include "web.h"
#include <math.h>

typedef struct { int kind; uint32_t id[3]; } meas;
static struct {
    CV_VEC(meas) m;
    unsigned gen;                       /* bumped when the list changes */
    int arm;                            /* the kind + 1 being picked, 0 none */
    uint32_t pick[3]; int npick;        /* the nodes picked so far (indices) */
    char path[1024];                    /* the model they belong to */
} M;

static const char* kind_names[CV_MEAS_N] = { "distance", "angle", "circle" };
const char* app_measure_kind_name(int k) { return k >= 0 && k < CV_MEAS_N ? kind_names[k] : "?"; }
int app_measure_nodes(int k) { return k == CV_MEAS_DIST ? 2 : 3; }
int app_measure_count(void) { return (int)M.m.n; }

static void changed(void) { M.gen++; app_label_changed(); }

bool app_measure_get(int i, int* kind, uint32_t ids[3]) {
    if (i < 0 || (size_t)i >= M.m.n) return false;
    *kind = M.m.a[i].kind;
    memcpy(ids, M.m.a[i].id, sizeof M.m.a[i].id);
    return true;
}

bool app_measure_add(int kind, const uint32_t* node_ids) {
    if (kind < 0 || kind >= CV_MEAS_N || !node_ids) return false;
    meas m = { kind, { 0, 0, 0 } };
    int n = app_measure_nodes(kind);
    for (int j = 0; j < n; j++) {
        if (G.loaded && cv_frd_node_index(&G.frd, node_ids[j]) == UINT32_MAX) return false;
        m.id[j] = node_ids[j];
    }
    if (!cv_push(M.m, m)) return false;
    changed();
    return true;
}

void app_measure_remove(int i) {
    if (i < 0 || (size_t)i >= M.m.n) return;
    memmove(M.m.a + i, M.m.a + i + 1, (M.m.n - (size_t)i - 1) * sizeof *M.m.a);
    M.m.n--;
    changed();
}

void app_measure_clear(void) { M.m.n = 0; changed(); }

bool app_measure_model(const char* path) {
    app_measure_cancel();
    if (!strcmp(path, M.path)) return false;     /* a reload: they stay */
    snprintf(M.path, sizeof M.path, "%s", path);
    app_measure_clear();
    return true;
}

/* ---- the values ------------------------------------------------------------------- */

/* model lengths to the lengths shown, and the shown unit's name ("" when none is set) */
static double len_k(void) { return 1.0 / units_len_raw(); }
static const char* len_unit(void) {
    const cv_unit* u = cv_unit_get(CV_Q_LEN, cv_unit_shown(G.units, cv_sys_temp(G.units), G.unit_in[CV_Q_LEN], CV_Q_LEN, G.unit_show[CV_Q_LEN]));
    return u && G.units ? u->name : "";
}

/* the indices of its nodes, false when one is not in the model */
static bool nodes_of(const meas* m, uint32_t ix[3]) {
    if (!G.loaded) return false;
    for (int j = 0; j < app_measure_nodes(m->kind); j++)
        if ((ix[j] = cv_frd_node_index(&G.frd, m->id[j])) == UINT32_MAX) return false;
    return true;
}

/* a node's position, undeformed (state 0) or with the step's DISP at scale 1 */
static void node_pos(uint32_t i, int state, double p[3]) {
    for (int k = 0; k < 3; k++) p[k] = G.frd.xyz[3 * (size_t)i + k] + (state && G.disp ? (double)G.disp[3 * (size_t)i + k] : 0.0);
}

static bool values_of(const meas* m, double v[2][7], bool* has_def) {
    uint32_t ix[3];
    *has_def = G.disp != NULL;
    memset(v, 0, 2 * 7 * sizeof(double));
    if (!nodes_of(m, ix)) return false;
    double k = len_k();
    for (int s = 0; s < 2; s++) {
        double p[3][3];
        for (int j = 0; j < app_measure_nodes(m->kind); j++) node_pos(ix[j], s, p[j]);
        switch (m->kind) {
        case CV_MEAS_DIST:
            cv_measure_dist(p[0], p[1], v[s]);
            for (int q = 0; q < 4; q++) v[s][q] *= k;
            break;
        case CV_MEAS_ANGLE:
            v[s][0] = cv_measure_angle(p[0], p[1], p[2]);
            if (v[s][0] != v[s][0]) return false;
            break;
        case CV_MEAS_CIRCLE:
            if (!cv_measure_circle(p[0], p[1], p[2], v[s] + 1, v[s], v[s] + 4)) return false;
            for (int q = 0; q < 4; q++) v[s][q] *= k;
            break;
        }
    }
    return true;
}

bool app_measure_values(int i, double v[2][7], bool* has_def) {
    if (i < 0 || (size_t)i >= M.m.n) { *has_def = false; return false; }
    return values_of(&M.m.a[i], v, has_def);
}

/* a number to six digits of the whole it is part of (the distance for its components,
   the radius and centre for the centre's), trailing zeros dropped: a component that is
   float noise against the whole reads 0. The CSV keeps every digit. */
static const char* num(char* b, double v, double whole) {
    whole = fabs(whole) > 0 ? fabs(whole) : fabs(v);
    if (!(whole > 0) || !isfinite(v)) { snprintf(b, 24, "%.6g", v); return b; }
    int dec = CV_MAX(0, CV_MIN(9, 5 - (int)floor(log10(whole))));
    double q = pow(10, dec);
    v = round(v * q) / q;
    if (v == 0) v = 0;                                  /* no "-0" */
    snprintf(b, 24, "%.*f", dec, v);
    char* dot = strchr(b, '.');
    if (dot) { char* e = b + strlen(b) - 1; while (e > dot && *e == '0') *e-- = 0; if (e == dot) *e = 0; }
    return b;
}

void app_measure_text(int i, int which, char* out, size_t n) {
    double v[2][7];
    bool def;
    out[0] = 0;
    if (i < 0 || (size_t)i >= M.m.n) return;
    const meas* m = &M.m.a[i];
    if (!values_of(m, v, &def)) {
        snprintf(out, n, "%s: %s", kind_names[m->kind], m->kind == CV_MEAS_CIRCLE ? "the nodes are in a line" : "two nodes coincide");
        return;
    }
    const char* u = len_unit();
    char a[24], b[24], c[24], e[24];
    if (!def) which = CV_MSHOW_UNDEF;              /* no displacement: the one value */
    if (which == CV_MSHOW_BOTH) {                  /* "d 12.35 -> 12.41 mm": undeformed -> deformed */
        if (m->kind == CV_MEAS_ANGLE) snprintf(out, n, "%s -> %s deg", num(a, v[0][0], 0), num(b, v[1][0], v[0][0]));
        else snprintf(out, n, "%s %s -> %s%s%s", m->kind == CV_MEAS_DIST ? "d" : "R", num(a, v[0][0], 0), num(b, v[1][0], v[0][0]), u[0] ? " " : "", u);
        return;
    }
    const double* x = v[which == CV_MSHOW_DEF];
    switch (m->kind) {
    case CV_MEAS_DIST:
        snprintf(out, n, "d %s%s%s (dx %s dy %s dz %s)", num(a, x[0], 0), u[0] ? " " : "", u, num(b, x[1], x[0]), num(c, x[2], x[0]), num(e, x[3], x[0]));
        break;
    case CV_MEAS_ANGLE:
        snprintf(out, n, "%s deg", num(a, x[0], 0));
        break;
    case CV_MEAS_CIRCLE: {
        double w = fabs(x[1]) + fabs(x[2]) + fabs(x[3]) + x[0];
        snprintf(out, n, "R %s%s%s (c %s, %s, %s)", num(a, x[0], 0), u[0] ? " " : "", u, num(b, x[1], w), num(c, x[2], w), num(e, x[3], w));
        break;
    }
    }
}

void app_measure_line(int i, int state, char* out, size_t n) {
    double v[2][7];
    bool def;
    out[0] = 0;
    if (i < 0 || (size_t)i >= M.m.n) return;
    const meas* m = &M.m.a[i];
    const char* st = state ? "deformed  " : "undeformed";
    if (!values_of(m, v, &def)) { snprintf(out, n, "%s  -", st); return; }
    if (state && !def) { snprintf(out, n, "%s  no displacement in this step", st); return; }
    const double* x = v[state];
    const char* u = len_unit();
    char a[24], b[24], c[24], e[24], f[24], g[24], h[24];
    switch (m->kind) {
    case CV_MEAS_DIST:
        snprintf(out, n, "%s  d %s%s%s   dx %s  dy %s  dz %s", st, num(a, x[0], 0), u[0] ? " " : "", u, num(b, x[1], x[0]), num(c, x[2], x[0]), num(e, x[3], x[0]));
        break;
    case CV_MEAS_ANGLE:
        snprintf(out, n, "%s  %s deg", st, num(a, x[0], 0));
        break;
    case CV_MEAS_CIRCLE: {
        double w = fabs(x[1]) + fabs(x[2]) + fabs(x[3]) + x[0];
        snprintf(out, n, "%s  R %s%s%s   centre %s, %s, %s   normal %s, %s, %s", st, num(a, x[0], 0), u[0] ? " " : "", u,
                 num(b, x[1], w), num(c, x[2], w), num(e, x[3], w), num(f, x[4], 1), num(g, x[5], 1), num(h, x[6], 1));
        break;
    }
    }
}

/* "1 distance  nodes 12, 40" */
static void head_line(int i, char* out, size_t n) {
    const meas* m = &M.m.a[i];
    if (m->kind == CV_MEAS_DIST) snprintf(out, n, "%d %s  nodes %u, %u", i + 1, kind_names[m->kind], m->id[0], m->id[1]);
    else snprintf(out, n, "%d %s  nodes %u, %u, %u", i + 1, kind_names[m->kind], m->id[0], m->id[1], m->id[2]);
}

size_t app_measure_copy(char* out, size_t n) {
    size_t o = 0;
    if (n) out[0] = 0;
    for (int i = 0; i < (int)M.m.n && o + 1 < n; i++) {
        char h[96], l[2][200];
        head_line(i, h, sizeof h);
        app_measure_line(i, 0, l[0], sizeof l[0]); app_measure_line(i, 1, l[1], sizeof l[1]);
        int k = snprintf(out + o, n - o, "%s\n  %s\n  %s\n", h, l[0], l[1]);
        if (k < 0) break;
        o += CV_MIN((size_t)k, n - o - 1);
    }
    return o;
}

bool app_measure_csv(void) {
    if (!G.loaded || !M.m.n) return false;
    char base[1024], path[1100];
    snprintf(base, sizeof base, "%s", G.path);
    char* dot = strrchr(base, '.');
    char* sep = strrchr(base, cv_path_sep());
    if (dot && (!sep || dot > sep)) *dot = 0;
    snprintf(path, sizeof path, "%s_measurements.csv", base);
    FILE* fp = fopen(path, "wb");
    bool ok = fp != NULL;
    if (fp) {
        /* value: the distance, the angle in degrees, the radius; x y z: the components or the centre */
        cv_fprintf(fp, "measurement,kind,node_a,node_b,node_c,state,step,value,x,y,z,nx,ny,nz\n");
        for (int i = 0; i < (int)M.m.n; i++) {
            const meas* m = &M.m.a[i];
            double v[2][7];
            bool def, good = values_of(m, v, &def);
            for (int s = 0; s < 2; s++) {
                cv_fprintf(fp, "%d,%s,%u,%u,", i + 1, kind_names[m->kind], m->id[0], m->id[1]);
                if (m->kind != CV_MEAS_DIST) cv_fprintf(fp, "%u", m->id[2]);
                cv_fprintf(fp, ",%s,%d", s ? "deformed" : "undeformed", G.frd.n_steps ? G.step + 1 : 0);
                if (!good || (s && !def)) { cv_fprintf(fp, ",nan,,,,,,\n"); continue; }
                const double* x = v[s];
                if (m->kind == CV_MEAS_DIST) cv_fprintf(fp, ",%.9g,%.9g,%.9g,%.9g,,,\n", x[0], x[1], x[2], x[3]);
                else if (m->kind == CV_MEAS_ANGLE) cv_fprintf(fp, ",%.9g,,,,,,\n", x[0]);
                else cv_fprintf(fp, ",%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n", x[0], x[1], x[2], x[3], x[4], x[5], x[6]);
            }
        }
        ok = fclose(fp) == 0;
    }
    snprintf(G.note, sizeof G.note, ok ? "saved %s" : "could not write %s", path);
    G.note_t = cv_now();
    cv_msg_add(&G.msgs, 0, false, G.note);
    if (ok) CV_EXPORTED(path);
    return ok;
}

/* ---- picking ---------------------------------------------------------------------- */

void app_measure_arm(int kind, uint32_t first) {
    if (kind < 0 || kind >= CV_MEAS_N || !G.loaded) return;
    G.path_arm = false;                          /* one thing at a time waits for a click */
    M.arm = kind + 1;
    M.npick = 0;
    if (first < G.frd.n_nodes) M.pick[M.npick++] = first;
}

void app_measure_cancel(void) { M.arm = 0; M.npick = 0; }
int app_measure_armed(void) { return M.arm; }

bool app_measure_pick(uint32_t node) {
    if (!M.arm || node >= G.frd.n_nodes) return false;
    int kind = M.arm - 1, need = app_measure_nodes(kind);
    for (int j = 0; j < M.npick; j++) if (M.pick[j] == node) return true;   /* the same node again: still waiting */
    M.pick[M.npick++] = node;
    if (M.npick < need) return true;
    uint32_t ids[3] = { 0, 0, 0 };
    for (int j = 0; j < need; j++) ids[j] = G.frd.node_id[M.pick[j]];
    app_measure_cancel();
    if (app_measure_add(kind, ids)) {
        char t[200];
        app_measure_text((int)M.m.n - 1, G.meas_show, t, sizeof t);
        snprintf(G.note, sizeof G.note, "%s %d: %s", kind_names[kind], (int)M.m.n, t);
        G.note_t = cv_now();
    }
    return true;
}

const char* app_measure_prompt(void) {
    if (!M.arm) return "";
    static char t[120];
    static const char* what[CV_MEAS_N][3] = {
        { "the first node", "the second node", "" },
        { "the first node", "the vertex node", "the third node" },
        { "a node on the circle", "a second node on it", "a third node on it" },
    };
    int kind = M.arm - 1;
    snprintf(t, sizeof t, "%s: click %s (%d of %d; Esc cancels)", kind_names[kind], what[kind][M.npick],
             M.npick + 1, app_measure_nodes(kind));
    return t;
}

/* ---- lines and labels --------------------------------------------------------------- */

static void node_pd(uint32_t i, float p[3], float d[6]) {
    memcpy(p, G.frd.xyz + 3 * (size_t)i, 3 * sizeof *p);
    app_node_disp6(i, d);
}

/* a straight piece from p0 to p1 in short ones, the motion linear along it */
#define MEAS_SUB 32
static void seg(cv_fvec* pos, cv_fvec* disp, const float p0[3], const float d0[6], const float p1[3], const float d1[6], int sub) {
    for (int j = 0; j < sub; j++)
        for (int e = 0; e < 2; e++) {
            float f = (float)(j + e) / sub;
            for (int k = 0; k < 3; k++) cv_push(*pos, p0[k] + f * (p1[k] - p0[k]));
            for (int k = 0; k < 6; k++) cv_push(*disp, d0[k] + f * (d1[k] - d0[k]));
        }
}

/* a point in the plane of three nodes moves as the plane does: the nodes' motions
   weighted by its affine weights in their triangle (exact at the nodes) */
static void plane_disp(const float* P[3], const float* D[3], const double q[3], float d[6]) {
    double a[3], b[3], c[3], w[3] = { 1.0 / 3, 1.0 / 3, 1.0 / 3 };
    for (int k = 0; k < 3; k++) { a[k] = P[0][k]; b[k] = P[1][k]; c[k] = P[2][k]; }
    cv_measure_weights(a, b, c, q, w);
    for (int k = 0; k < 6; k++) d[k] = (float)(w[0] * D[0][k] + w[1] * D[1][k] + w[2] * D[2][k]);
}

/* a polyline of points q (n, double, in the plane of P) as segments, moving with the plane */
static void plane_poly(cv_fvec* pos, cv_fvec* disp, const float* P[3], const float* D[3], double (*q)[3], int n) {
    float p0[3], p1[3], d0[6], d1[6];
    for (int i = 0; i + 1 < n; i++) {
        for (int k = 0; k < 3; k++) { p0[k] = (float)q[i][k]; p1[k] = (float)q[i + 1][k]; }
        plane_disp(P, D, q[i], d0); plane_disp(P, D, q[i + 1], d1);
        seg(pos, disp, p0, d0, p1, d1, 2);
    }
}

/* where its label sits and how it moves: the segment's middle, the vertex, the centre */
static bool label_at(const meas* m, float p[3], float d[6]) {
    uint32_t ix[3];
    if (!nodes_of(m, ix)) return false;
    float P[3][3], D[3][6];
    for (int j = 0; j < app_measure_nodes(m->kind); j++) node_pd(ix[j], P[j], D[j]);
    if (m->kind == CV_MEAS_DIST) {
        for (int k = 0; k < 3; k++) p[k] = 0.5f * (P[0][k] + P[1][k]);
        for (int k = 0; k < 6; k++) d[k] = 0.5f * (D[0][k] + D[1][k]);
    } else if (m->kind == CV_MEAS_ANGLE) {
        memcpy(p, P[1], sizeof P[1]); memcpy(d, D[1], sizeof D[1]);
    } else {
        double a[3], b[3], c[3], cc[3], r, nn[3];
        for (int k = 0; k < 3; k++) { a[k] = P[0][k]; b[k] = P[1][k]; c[k] = P[2][k]; }
        if (!cv_measure_circle(a, b, c, cc, &r, nn)) { memcpy(p, P[1], sizeof P[1]); memcpy(d, D[1], sizeof D[1]); return true; }   /* "in a line" there */
        const float* PP[3] = { P[0], P[1], P[2] }, *DD[3] = { D[0], D[1], D[2] };
        for (int k = 0; k < 3; k++) p[k] = (float)cc[k];
        plane_disp(PP, DD, cc, d);
    }
    return true;
}

/* the lines of one: the segment, the two legs and an arc between them, the circle and
   a cross at its centre */
static void lines_of(const meas* m, cv_fvec* pos, cv_fvec* disp, cv_fvec* npos, cv_fvec* ndisp) {
    uint32_t ix[3];
    if (!nodes_of(m, ix)) return;
    int nn = app_measure_nodes(m->kind);
    float P[3][3], D[3][6];
    for (int j = 0; j < nn; j++) {
        node_pd(ix[j], P[j], D[j]);
        for (int k = 0; k < 3; k++) cv_push(*npos, P[j][k]);
        for (int k = 0; k < 6; k++) cv_push(*ndisp, D[j][k]);
    }
    if (m->kind == CV_MEAS_DIST) { seg(pos, disp, P[0], D[0], P[1], D[1], MEAS_SUB); return; }
    const float* PP[3] = { P[0], P[1], P[2] }, *DD[3] = { D[0], D[1], D[2] };
    double a[3], b[3], c[3];
    for (int k = 0; k < 3; k++) { a[k] = P[0][k]; b[k] = P[1][k]; c[k] = P[2][k]; }
    enum { NARC = 96 };
    double q[NARC + 1][3];
    if (m->kind == CV_MEAS_ANGLE) {
        seg(pos, disp, P[1], D[1], P[0], D[0], MEAS_SUB);
        seg(pos, disp, P[1], D[1], P[2], D[2], MEAS_SUB);
        double u[3], v[3], lu = 0, lv = 0, uv = 0;          /* the arc: a quarter of the shorter leg out */
        for (int k = 0; k < 3; k++) { u[k] = a[k] - b[k]; v[k] = c[k] - b[k]; lu += u[k] * u[k]; lv += v[k] * v[k]; }
        lu = sqrt(lu); lv = sqrt(lv);
        if (!(lu > 0) || !(lv > 0)) return;
        for (int k = 0; k < 3; k++) { u[k] /= lu; v[k] /= lv; uv += u[k] * v[k]; }
        double e[3], le = 0, th = cv_measure_angle(a, b, c) * 3.14159265358979323846 / 180, rho = 0.25 * CV_MIN(lu, lv);
        for (int k = 0; k < 3; k++) { e[k] = v[k] - uv * u[k]; le += e[k] * e[k]; }
        if (!((le = sqrt(le)) > 1e-12)) return;              /* straight: no arc */
        int n = CV_MAX(4, (int)(NARC * th / 3.14159265358979323846));
        for (int i = 0; i <= n; i++) {
            double t = th * i / n;
            for (int k = 0; k < 3; k++) q[i][k] = b[k] + rho * (cos(t) * u[k] + sin(t) * e[k] / le);
        }
        plane_poly(pos, disp, PP, DD, q, n + 1);
        return;
    }
    double cc[3], r, nrm[3];
    if (!cv_measure_circle(a, b, c, cc, &r, nrm)) return;
    double u[3], w[3];
    for (int k = 0; k < 3; k++) u[k] = (a[k] - cc[k]) / r;
    w[0] = nrm[1] * u[2] - nrm[2] * u[1]; w[1] = nrm[2] * u[0] - nrm[0] * u[2]; w[2] = nrm[0] * u[1] - nrm[1] * u[0];
    for (int i = 0; i <= NARC; i++) {
        double t = 2 * 3.14159265358979323846 * i / NARC;
        for (int k = 0; k < 3; k++) q[i][k] = cc[k] + r * (cos(t) * u[k] + sin(t) * w[k]);
    }
    plane_poly(pos, disp, PP, DD, q, NARC + 1);
    for (int s = 0; s < 2; s++) {                            /* the centre: a cross a fifth of the radius across */
        double x[2][3];
        for (int k = 0; k < 3; k++) {
            double dir = s ? w[k] : u[k];
            x[0][k] = cc[k] - 0.1 * r * dir; x[1][k] = cc[k] + 0.1 * r * dir;
        }
        plane_poly(pos, disp, PP, DD, x, 2);
    }
}

void app_measure_sync(void) {
    uint64_t k[] = { G.loaded, (uintptr_t)G.frd.xyz, (uintptr_t)G.disp, (uintptr_t)G.disp2, G.field_gen, M.gen,
                     (uint64_t)M.arm, (uint64_t)M.npick, M.pick[0], M.pick[1], (uint64_t)(units_len_raw() * 1e6) };
    static uint64_t last[sizeof k / sizeof k[0]];
    if (!memcmp(k, last, sizeof k)) return;
    bool values = k[2] != last[2] || k[4] != last[4] || k[10] != last[10];
    memcpy(last, k, sizeof k);
    cv_fvec pos = {0}, disp = {0}, npos = {0}, ndisp = {0};
    for (size_t i = 0; G.loaded && i < M.m.n; i++) lines_of(&M.m.a[i], &pos, &disp, &npos, &ndisp);
    for (int j = 0; G.loaded && M.arm && j < M.npick; j++) {
        float p[3], d[6];
        node_pd(M.pick[j], p, d);
        for (int q = 0; q < 3; q++) cv_push(npos, p[q]);
        for (int q = 0; q < 6; q++) cv_push(ndisp, d[q]);
    }
    app_aux_upload(CV_AUX_MEASLN, &pos, &disp, NULL);
    app_aux_upload(CV_AUX_MEASPT, &npos, &ndisp, NULL);
    cv_free_vec(pos); cv_free_vec(disp); cv_free_vec(npos); cv_free_vec(ndisp);
    if (values && M.m.n) app_label_changed();               /* the deformed values with them */
}

uint32_t app_measure_anchors(float* anc, char (*txt)[96], uint32_t* which, uint32_t max) {
    uint32_t n = 0;
    for (size_t i = 0; i < M.m.n && n < max; i++) {
        float p[3], d[6];
        if (!label_at(&M.m.a[i], p, d)) continue;
        memcpy(anc + 9 * n, p, sizeof p); memcpy(anc + 9 * n + 3, d, sizeof d);
        app_measure_text((int)i, G.meas_show, txt[n], sizeof txt[n]);
        which[n++] = (uint32_t)i;
    }
    return n;
}
