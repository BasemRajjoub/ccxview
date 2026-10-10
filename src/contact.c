/* contact.c -- a slave node against its master face: projection, gap, status (contact.h). */
#include "contact.h"
#include <math.h>

const char* const cv_cst_names[CV_CST_N] = { "far open", "near open", "sliding", "sticking", "closed", "penetrating" };
/* Ansys' contact tool colours by category: open cool, closed warm; a penetration beyond
   the tolerance magenta, the warning */
const float cv_cst_rgb[CV_CST_N][3] = {
    { 0.47f, 0.56f, 0.72f },      /* far open: grey-blue, cold and quiet (the master faces are bright blue) */
    { 1.00f, 0.86f, 0.22f },      /* near open: yellow */
    { 1.00f, 0.56f, 0.10f },      /* sliding: orange */
    { 0.80f, 0.16f, 0.08f },      /* sticking: dark red-orange */
    { 1.00f, 0.66f, 0.40f },      /* closed, stick or slip unknown: light orange */
    { 0.92f, 0.18f, 0.86f },      /* penetrating: magenta */
};

static double dot3(const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const double* a, const double* b, double* o) {
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
static double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }

/* the bilinear quad at (xi, eta): the point, its tangents, the corner weights */
static void quad_at(const double c[4][3], double xi, double eta, double x[3], double t1[3], double t2[3], double N[4]) {
    N[0] = (1 - xi) * (1 - eta) / 4; N[1] = (1 + xi) * (1 - eta) / 4;
    N[2] = (1 + xi) * (1 + eta) / 4; N[3] = (1 - xi) * (1 + eta) / 4;
    const double dx[4] = { -(1 - eta) / 4, (1 - eta) / 4, (1 + eta) / 4, -(1 + eta) / 4 };
    const double de[4] = { -(1 - xi) / 4, -(1 + xi) / 4, (1 + xi) / 4, (1 - xi) / 4 };
    for (int i = 0; i < 3; i++) {
        x[i] = t1[i] = t2[i] = 0;
        for (int k = 0; k < 4; k++) { x[i] += N[k] * c[k][i]; t1[i] += dx[k] * c[k][i]; t2[i] += de[k] * c[k][i]; }
    }
}

/* Gauss-Newton on the tangency conditions (the face's curvature dropped), from the
   centre; box: kept within -1..1 at every step (the nearest point of the face itself) */
static bool quad_solve(const double c[4][3], const double p[3], bool box, double* xi, double* eta) {
    double a = *xi, b = *eta, x[3], t1[3], t2[3], N[4];
    for (int it = 0; it < 40; it++) {
        quad_at(c, a, b, x, t1, t2, N);
        double r[3] = { p[0] - x[0], p[1] - x[1], p[2] - x[2] };
        double f1 = dot3(r, t1), f2 = dot3(r, t2);
        double a11 = dot3(t1, t1), a12 = dot3(t1, t2), a22 = dot3(t2, t2), det = a11 * a22 - a12 * a12;
        if (!(fabs(det) > 1e-300)) return false;
        double d1 = (a22 * f1 - a12 * f2) / det, d2 = (a11 * f2 - a12 * f1) / det;
        double na = a + d1, nb = b + d2;
        if (box) { na = clampd(na, -1, 1); nb = clampd(nb, -1, 1); }
        else { na = clampd(na, -8, 8); nb = clampd(nb, -8, 8); }
        bool done = fabs(na - a) + fabs(nb - b) < 1e-12;
        a = na; b = nb;
        if (done) break;
    }
    *xi = a; *eta = b;
    return true;
}

/* the nearest point to q on segment a-b: its parameter 0..1 */
static double seg_t(const double* q, const double* a, const double* b) {
    double ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, aq[3] = { q[0] - a[0], q[1] - a[1], q[2] - a[2] };
    double l = dot3(ab, ab);
    return l > 0 ? clampd(dot3(aq, ab) / l, 0, 1) : 0;
}

bool cv_contact_project(const float p[3], const float cf[][3], int n, cv_cproj* o) {
    memset(o, 0, sizeof *o);
    if (n != 3 && n != 4) return false;
    double c[4][3], q[3] = { p[0], p[1], p[2] }, x[3], nn[3], w[4] = { 0, 0, 0, 0 };
    for (int k = 0; k < n; k++) for (int i = 0; i < 3; i++) c[k][i] = cf[k][i];
    if (n == 3) {
        double e1[3], e2[3], r[3];
        for (int i = 0; i < 3; i++) { e1[i] = c[1][i] - c[0][i]; e2[i] = c[2][i] - c[0][i]; r[i] = q[i] - c[0][i]; }
        cross3(e1, e2, nn);
        double l = sqrt(dot3(nn, nn));
        if (!(l > 0)) return false;
        for (int i = 0; i < 3; i++) nn[i] /= l;
        double a11 = dot3(e1, e1), a12 = dot3(e1, e2), a22 = dot3(e2, e2), det = a11 * a22 - a12 * a12;
        double f1 = dot3(r, e1), f2 = dot3(r, e2);
        double u = (a22 * f1 - a12 * f2) / det, v = (a11 * f2 - a12 * f1) / det;
        o->inside = u >= -1e-6 && v >= -1e-6 && u + v <= 1 + 1e-6;
        if (o->inside) { w[0] = 1 - u - v; w[1] = u; w[2] = v; }
        else {                                     /* the nearest of the three edges */
            double best = INFINITY;
            for (int k = 0; k < 3; k++) {
                const double* a = c[k]; const double* b = c[(k + 1) % 3];
                double t = seg_t(q, a, b), y[3], d = 0;
                for (int i = 0; i < 3; i++) { y[i] = a[i] + t * (b[i] - a[i]); d += (q[i] - y[i]) * (q[i] - y[i]); }
                if (d < best) { best = d; w[0] = w[1] = w[2] = 0; w[k] = 1 - t; w[(k + 1) % 3] = t; }
            }
        }
        for (int i = 0; i < 3; i++) x[i] = w[0] * c[0][i] + w[1] * c[1][i] + w[2] * c[2][i];
        o->xi = (float)w[1]; o->eta = (float)w[2];
    } else {
        double xi = 0, eta = 0, t1[3], t2[3];
        if (!quad_solve(c, q, false, &xi, &eta)) return false;
        o->inside = fabs(xi) <= 1 + 1e-6 && fabs(eta) <= 1 + 1e-6;
        if (!o->inside) {
            xi = clampd(xi, -1, 1); eta = clampd(eta, -1, 1);
            if (!quad_solve(c, q, true, &xi, &eta)) return false;
        }
        quad_at(c, xi, eta, x, t1, t2, w);
        cross3(t1, t2, nn);
        double l = sqrt(dot3(nn, nn));
        if (!(l > 0)) return false;
        for (int i = 0; i < 3; i++) nn[i] /= l;
        o->xi = (float)xi; o->eta = (float)eta;
    }
    double r[3] = { q[0] - x[0], q[1] - x[1], q[2] - x[2] };
    o->gap = (float)dot3(r, nn);
    for (int i = 0; i < 3; i++) { o->foot[i] = (float)x[i]; o->n[i] = (float)nn[i]; }
    for (int k = 0; k < 4; k++) o->w[k] = (float)w[k];
    return true;
}

int cv_contact_status(const cv_cin* in) {
    bool hg = in->gap == in->gap, hp = in->press == in->press, hs = in->shear == in->shear;
    bool closed;
    if (hp) closed = in->press > 0;
    else if (hg) closed = in->gap <= in->tol;
    else if (in->elem) closed = true;
    else return -1;
    if (!closed) return hg && in->gap <= in->near ? CV_CST_NEAR : CV_CST_FAR;
    if (hg && in->gap < -in->tol) return CV_CST_PEN;
    if (!hs || !hp) return CV_CST_CLOSED;
    if (!(in->mu > 0)) return CV_CST_SLIDE;
    return in->shear < 0.995f * in->mu * in->press ? CV_CST_STICK : CV_CST_SLIDE;
}

/* ---- the gap's colours -------------------------------------------------------------- */

static void mix3(const float* a, const float* b, float t, float* o) { for (int i = 0; i < 3; i++) o[i] = a[i] + (b[i] - a[i]) * t; }

/* along stops s[0..n) at positions x[0..n) (rising), at t */
static void ramp(const float (*s)[3], const float* x, int n, float t, float* o) {
    if (t <= x[0]) { memcpy(o, s[0], 3 * sizeof(float)); return; }
    for (int k = 1; k < n; k++)
        if (t <= x[k]) { mix3(s[k - 1], s[k], (t - x[k - 1]) / (x[k] - x[k - 1]), o); return; }
    memcpy(o, s[n - 1], 3 * sizeof(float));
}

void cv_contact_gap_rgb(int map, float g, float lo, float hi, float tol, float rgb[3]) {
    static const float lk[5][3] = { { 0.88f, 0.10f, 0.10f }, { 0.96f, 0.78f, 0.12f }, { 0.22f, 0.82f, 0.32f },
                                    { 0.12f, 0.72f, 0.86f }, { 0.22f, 0.36f, 1.00f } };
    static const float ct[5][3] = { { 0.70f, 0.02f, 0.10f }, { 0.96f, 0.56f, 0.46f }, { 0.96f, 0.96f, 0.94f },
                                    { 0.52f, 0.72f, 0.98f }, { 0.10f, 0.24f, 0.78f } };
    static const float x[5] = { -1.f, -0.4f, 0.f, 0.4f, 1.f };
    if (g != g) { rgb[0] = rgb[1] = rgb[2] = 0.6f; return; }
    float s = g < 0 ? (lo < 0 ? -fminf(g / lo, 1.f) : -1.f) : (hi > 0 ? fminf(g / hi, 1.f) : 1.f);
    if (map == CV_CGAP_LINKS && fabsf(g) <= tol) s = 0;
    ramp(map == CV_CGAP_LINKS ? lk : ct, x, 5, s, rgb);
}

void cv_contact_gap_table(int map, float lo, float hi, float tol, float* rgb, int n) {
    for (int i = 0; i < n; i++) {
        float g = n > 1 ? lo + (hi - lo) * (float)i / (float)(n - 1) : 0.f;
        cv_contact_gap_rgb(map, g, lo, hi, tol, rgb + 3 * i);
    }
}

/* ---- the nearest face: a uniform grid ------------------------------------------------- */

struct cv_cgrid {
    const float* fc; const uint8_t* nc; uint32_t nf;
    float lo[3], h;
    int dim[3];
    uint32_t* off; uint32_t* ids;          /* per cell: its faces (CSR) */
    uint32_t* seen; uint32_t stamp;        /* per face: tested in this query */
};

static int cell_of(const cv_cgrid* g, float x, int k) {
    int c = (int)floorf((x - g->lo[k]) / g->h);
    return c < 0 ? 0 : c >= g->dim[k] ? g->dim[k] - 1 : c;
}

static void face_box(const float* f, int n, float lo[3], float hi[3]) {
    for (int i = 0; i < 3; i++) { lo[i] = hi[i] = f[i]; }
    for (int k = 1; k < n; k++) for (int i = 0; i < 3; i++) { lo[i] = fminf(lo[i], f[3 * k + i]); hi[i] = fmaxf(hi[i], f[3 * k + i]); }
}

cv_cgrid* cv_cgrid_build(const float* fc, const uint8_t* nc, uint32_t nf) {
    if (!fc || !nc || !nf) return NULL;
    cv_cgrid* g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->fc = fc; g->nc = nc; g->nf = nf;
    float lo[3] = { INFINITY, INFINITY, INFINITY }, hi[3] = { -INFINITY, -INFINITY, -INFINITY };
    double ext = 0;
    for (uint32_t f = 0; f < nf; f++) {
        float a[3], b[3];
        face_box(fc + 12 * (size_t)f, nc[f] == 3 ? 3 : 4, a, b);
        for (int i = 0; i < 3; i++) { lo[i] = fminf(lo[i], a[i]); hi[i] = fmaxf(hi[i], b[i]); }
        ext += fmaxf(b[0] - a[0], fmaxf(b[1] - a[1], b[2] - a[2]));
    }
    float span = fmaxf(hi[0] - lo[0], fmaxf(hi[1] - lo[1], hi[2] - lo[2]));
    g->h = fmaxf((float)(ext / nf), span / 128.f);
    if (!(g->h > 0)) g->h = 1;
    size_t cells = 1;
    for (int i = 0; i < 3; i++) {
        g->lo[i] = lo[i];
        g->dim[i] = (int)((hi[i] - lo[i]) / g->h) + 1;
        if (g->dim[i] > 128) g->dim[i] = 128;
        cells *= (size_t)g->dim[i];
    }
    g->off = calloc(cells + 1, sizeof(uint32_t));
    g->seen = calloc(nf, sizeof(uint32_t));
    if (!g->off || !g->seen) { cv_cgrid_free(g); return NULL; }
    for (int pass = 0; pass < 2; pass++) {             /* count, then fill */
        for (uint32_t f = 0; f < nf; f++) {
            float a[3], b[3];
            face_box(fc + 12 * (size_t)f, nc[f] == 3 ? 3 : 4, a, b);
            int c0[3], c1[3];
            for (int i = 0; i < 3; i++) { c0[i] = cell_of(g, a[i], i); c1[i] = cell_of(g, b[i], i); }
            for (int z = c0[2]; z <= c1[2]; z++) for (int y = c0[1]; y <= c1[1]; y++) for (int x = c0[0]; x <= c1[0]; x++) {
                size_t c = ((size_t)z * g->dim[1] + y) * g->dim[0] + x;
                if (pass == 0) g->off[c + 1]++;
                else g->ids[g->off[c]++] = f;
            }
        }
        if (pass == 0) {
            for (size_t c = 0; c < cells; c++) g->off[c + 1] += g->off[c];
            g->ids = malloc(CV_MAX(g->off[cells], 1) * sizeof(uint32_t));
            if (!g->ids) { cv_cgrid_free(g); return NULL; }
        }
    }
    for (size_t c = cells; c > 0; c--) g->off[c] = g->off[c - 1];      /* the fill moved each start to the next */
    g->off[0] = 0;
    return g;
}

void cv_cgrid_free(cv_cgrid* g) {
    if (!g) return;
    free(g->off); free(g->ids); free(g->seen);
    free(g);
}

uint32_t cv_cgrid_nearest(cv_cgrid* g, const float p[3], float maxd, cv_cproj* o) {
    if (!g) return UINT32_MAX;
    if (++g->stamp == 0) { memset(g->seen, 0, g->nf * sizeof(uint32_t)); g->stamp = 1; }
    int c[3];
    for (int i = 0; i < 3; i++) c[i] = cell_of(g, p[i], i);
    /* how far p lies outside the grid: the first rings may be empty */
    float out = 0;
    for (int i = 0; i < 3; i++) {
        float d = p[i] < g->lo[i] ? g->lo[i] - p[i] : p[i] - (g->lo[i] + g->h * g->dim[i]);
        if (d > out) out = d;
    }
    int rmax = CV_MAX(g->dim[0], CV_MAX(g->dim[1], g->dim[2]));
    uint32_t best = UINT32_MAX;
    float bd = INFINITY;
    for (int r = 0; r <= rmax; r++) {
        for (int z = c[2] - r; z <= c[2] + r; z++) {
            if (z < 0 || z >= g->dim[2]) continue;
            for (int y = c[1] - r; y <= c[1] + r; y++) {
                if (y < 0 || y >= g->dim[1]) continue;
                for (int x = c[0] - r; x <= c[0] + r; x++) {
                    if (x < 0 || x >= g->dim[0]) continue;
                    if (abs(x - c[0]) != r && abs(y - c[1]) != r && abs(z - c[2]) != r) continue;   /* the shell only */
                    size_t cell = ((size_t)z * g->dim[1] + y) * g->dim[0] + x;
                    for (uint32_t j = g->off[cell]; j < g->off[cell + 1]; j++) {
                        uint32_t f = g->ids[j];
                        if (g->seen[f] == g->stamp) continue;
                        g->seen[f] = g->stamp;
                        cv_cproj q;
                        const float (*fc)[3] = (const float (*)[3])(g->fc + 12 * (size_t)f);
                        if (!cv_contact_project(p, fc, g->nc[f] == 3 ? 3 : 4, &q)) continue;
                        float d = 0;
                        for (int i = 0; i < 3; i++) d += (p[i] - q.foot[i]) * (p[i] - q.foot[i]);
                        d = sqrtf(d);
                        if (d < bd || (d == bd && q.inside && !o->inside)) { bd = d; best = f; *o = q; }
                    }
                }
            }
        }
        /* a face not yet seen lies in a cell at least r cells away */
        float reach = fmaxf(out, g->h * (float)r);
        if (best != UINT32_MAX && bd <= reach) break;
        if (reach > maxd) break;
    }
    return best != UINT32_MAX && bd <= maxd ? best : UINT32_MAX;
}
