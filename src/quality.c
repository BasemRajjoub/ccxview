/* quality.c -- mesh quality of one element (see quality.h). */
#include "quality.h"
#include "gauss.h"
#include <math.h>
#include <string.h>
#include <strings.h>

#define DEG (3.14159265358979323846 / 180)

static const cv_mq_info kInfo[CV_MQ_N] = {
    [CV_MQ_SIZE]      = { "size", "size",
        "Volume of a solid, area of a shell or plane element, length of a beam.\n"
        "Negative: the element is inside out", false, -1, false, false },
    [CV_MQ_EDGE_MIN]  = { "edgemin", "shortest edge", "The shortest corner to corner edge", false, 1, false, false },
    [CV_MQ_EDGE_MAX]  = { "edgemax", "longest edge", "The longest corner to corner edge", true, 1, false, false },
    [CV_MQ_ASPECT]    = { "aspect", "aspect ratio",
        "Longest over shortest edge; 1 ideal, poor above 10 (Abaqus)", true, 0, false, false },
    [CV_MQ_SJAC]      = { "sjac", "scaled Jacobian",
        "The least over the corners of the volume spanned by the three edges there, over\n"
        "the product of their lengths (Verdict). 1 for a cube or regular tet, 0 flat,\n"
        "negative inverted. Poor below 0.5 (0.3 for quads)", false, 0, false, false },
    [CV_MQ_JRATIO]    = { "jratio", "Jacobian ratio",
        "Least over largest det J at the nodes, mid-side nodes too: how evenly the\n"
        "element maps. 1 for straight edges, negative folded. Poor below 1/30 (ANSYS)", false, 0, false, false },
    [CV_MQ_SKEW]      = { "skew", "skewness",
        "Equiangle skewness: how far the face angles stray from 60 (triangles) or\n"
        "90 degrees (quads). 0 ideal, 1 degenerate, poor above 0.9", true, 0, false, false },
    [CV_MQ_ANGLE_MIN] = { "anglemin", "smallest angle",
        "The smallest face corner angle. Poor below 5 degrees for triangles and tets,\n"
        "10 for the rest (Abaqus)", false, 0, true, false },
    [CV_MQ_ANGLE_MAX] = { "anglemax", "largest angle",
        "The largest face corner angle. Poor above 170 degrees for triangles and tets,\n"
        "160 for the rest (Abaqus)", true, 0, true, false },
    [CV_MQ_WARP]      = { "warp", "warpage",
        "How far quad faces are from flat: the angle between the normals of the two\n"
        "triangles of a face, the worse diagonal. Poor above 10 degrees", true, 0, true, false },
    [CV_MQ_SHAPE]     = { "shape", "shape factor",
        "Triangles and tets: the size over that of the equilateral element with the same\n"
        "circumradius; 1 ideal. Poor below 0.01 (triangles), 0.0001 (tets) (Abaqus)", false, 0, false, false },
    [CV_MQ_CCX]       = { "quality", "ccxview quality",
        "One number from 1 (ideal) to 0 (past a limit): the weakest of aspect ratio,\n"
        "scaled Jacobian, Jacobian ratio, skewness, warpage and shape factor, each scored\n"
        "1 at its ideal down to 0 at its usual limit (ratios on a log scale).\n"
        "Taking the weakest, not the mean: one bad measure spoils the element.\n"
        "0 exactly when some measure is past its limit; the probe names the measure", false, 0, false, true },
    [CV_MQ_HMQI]      = { "hmqi", "HyperMesh QI",
        "HyperMesh's quality index: each measure gets a penalty, 0 up to its \"good\" level,\n"
        "rising to 1 at \"fail\" (0.8 at \"warn\", 80 % of the way), 1 to 10 between \"fail\"\n"
        "and \"worst\", 10 beyond. The element's index: the mean of the penalties that pass\n"
        "plus the sum of those that fail. 0 ideal; 1 or more fails a criterion.\n"
        "On ccxview's measures and limits; the levels are in the Mesh quality window", true, 0, false, true },
    [CV_MQ_ANSYS]     = { "ansys", "ANSYS element quality",
        "ANSYS Element Quality: C V / sqrt((sum of edge length^2)^3) for solids,\n"
        "C A / (sum of edge length^2) for shells and plane elements, C making it 1 for\n"
        "a cube, square, regular tet or triangle, or equilateral wedge. 0 flat or inverted.\n"
        "ANSYS sets no limit; below about 0.1 is usually looked at", false, 0, false, false },
    [CV_MQ_ABAQUS]    = { "abaqus", "Abaqus checks failed",
        "How many of the Abaqus Verify Mesh shape checks the element fails: aspect ratio\n"
        "above 10, smallest face angle below 10 (5 triangles and tets), largest above 160\n"
        "(170), shape factor of triangles and tets, and inside out. 0 passes", true, 0, false, true },
};

const cv_mq_info* cv_mq(int q) { return &kInfo[q >= 0 && q < CV_MQ_N ? q : 0]; }

int cv_mq_find(const char* key) {
    for (int q = 0; q < CV_MQ_N; q++)
        if (!strcasecmp(key, kInfo[q].key) || !strcasecmp(key, kInfo[q].name)) return q;
    return -1;
}

/* ---- element kinds ------------------------------------------------------------------ */

enum { K_NONE, K_HEX, K_WEDGE, K_TET, K_TRI, K_QUAD, K_LINE };

static int kind(int t) {
    switch (t) {
    case 1: case 4:   return K_HEX;
    case 2: case 5:   return K_WEDGE;
    case 3: case 6:   return K_TET;
    case 7: case 8:   return K_TRI;
    case 9: case 10:  return K_QUAD;
    case 11: case 12: return K_LINE;
    }
    return K_NONE;
}

int cv_mq_dim(int t) {
    switch (kind(t)) {
    case K_HEX: case K_WEDGE: case K_TET: return 3;
    case K_TRI: case K_QUAD: return 2;
    case K_LINE: return 1;
    }
    return 0;
}

static const int kCorners[] = { 0, 8, 6, 4, 3, 4, 2 };

double cv_mq_limit(int q, int t) {
    int k = kind(t);
    bool simplex = k == K_TRI || k == K_TET;
    switch (q) {
    case CV_MQ_ASPECT:    return k == K_LINE ? NAN : 10;
    case CV_MQ_SJAC:      return k == K_LINE ? NAN : k == K_QUAD ? 0.3 : 0.5;
    case CV_MQ_JRATIO:    return k == K_LINE ? NAN : 1.0 / 30;
    case CV_MQ_SKEW:      return k == K_LINE ? NAN : 0.9;
    case CV_MQ_ANGLE_MIN: return k == K_LINE ? NAN : simplex ? 5 : 10;
    case CV_MQ_ANGLE_MAX: return k == K_LINE ? NAN : simplex ? 170 : 160;
    case CV_MQ_WARP:      return k == K_HEX || k == K_WEDGE || k == K_QUAD ? 10 : NAN;
    case CV_MQ_SHAPE:     return k == K_TRI ? 0.01 : k == K_TET ? 1e-4 : NAN;
    case CV_MQ_CCX:       return k == K_LINE || k == K_NONE ? NAN : 0;
    case CV_MQ_HMQI:      return k == K_LINE || k == K_NONE ? NAN : 1;
    case CV_MQ_ABAQUS:    return k == K_LINE || k == K_NONE ? NAN : 1;
    }
    return NAN;
}

bool cv_mq_poor(int q, int t, double v) {
    double l = cv_mq_limit(q, t);
    if (l != l || v != v) return false;
    if (kInfo[q].incl) return kInfo[q].high_bad ? v >= l : v <= l;
    return kInfo[q].high_bad ? v > l : v < l;
}

/* ---- the overall scores ---------------------------------------------------------------- */

static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

double cv_mq_score(int q, int t, double v) {
    double l = cv_mq_limit(q, t);
    if (l != l || v != v) return NAN;
    switch (q) {
    case CV_MQ_ASPECT: return v >= 1 ? clamp01(1 - log(v) / log(l)) : 1;     /* ratios: log */
    case CV_MQ_JRATIO: return v > 0 ? clamp01(log(v / l) / log(1 / l)) : 0;
    case CV_MQ_SHAPE:  return v > 0 ? clamp01(log(v / l) / log(1 / l)) : 0;
    case CV_MQ_SJAC:   return clamp01((v - l) / (1 - l));
    case CV_MQ_SKEW:   return clamp01(1 - v / l);
    case CV_MQ_WARP:   return clamp01(1 - v / l);
    }
    return NAN;                                      /* sizes, edges, angles (in skew), the scores */
}

double cv_mq_hm_good(int q, int t) {
    int k = kind(t);
    bool simplex = k == K_TRI || k == K_TET;
    if (cv_mq_limit(q, t) != cv_mq_limit(q, t)) return NAN;
    switch (q) {
    case CV_MQ_ASPECT:    return 2;
    case CV_MQ_JRATIO:    return 0.5;
    case CV_MQ_SKEW:      return 0.5;
    case CV_MQ_WARP:      return 5;
    case CV_MQ_ANGLE_MIN: return simplex ? 30 : 45;
    case CV_MQ_ANGLE_MAX: return simplex ? 120 : 135;
    case CV_MQ_SHAPE:     return k == K_TRI ? 0.5 : 0.2;
    }
    return NAN;
}

double cv_mq_hm_worst(int q, int t) {
    if (cv_mq_hm_good(q, t) != cv_mq_hm_good(q, t)) return NAN;
    switch (q) {
    case CV_MQ_ASPECT:    return 20;
    case CV_MQ_SKEW:      return 1;
    case CV_MQ_WARP:      return 30;
    case CV_MQ_ANGLE_MAX: return 180;
    }
    return 0;                                        /* Jacobian ratio, smallest angle, shape factor */
}

double cv_mq_penalty(int q, int t, double v) {
    double g = cv_mq_hm_good(q, t), f = cv_mq_limit(q, t), w = cv_mq_hm_worst(q, t);
    if (g != g || v != v) return NAN;
    if (!kInfo[q].high_bad) { g = -g; f = -f; w = -w; v = -v; }   /* larger is worse from here */
    if (v <= g) return 0;
    if (v < f) return (v - g) / (f - g);             /* "warn" at 0.8 */
    if (v < w) return 1 + 9 * (v - f) / (w - f);
    return 10;
}

int cv_mq_governing(int t, const double v[CV_MQ_N]) {
    int best = -1;
    double lo = INFINITY;
    for (int q = 0; q < CV_MQ_SCORE0; q++) {
        double s = cv_mq_score(q, t, v[q]);
        if (s == s && s < lo) { lo = s; best = q; }
    }
    return best;
}

/* the scores from the measures, and the ANSYS quality from size and sum e^2 */
static void scores(int t, double sum_e2, double out[CV_MQ_N]) {
    int g = cv_mq_governing(t, out);
    out[CV_MQ_CCX] = g < 0 ? NAN : cv_mq_score(g, t, out[g]);

    double pass = 0, fail = 0;
    int np = 0, n = 0;
    for (int q = 0; q < CV_MQ_SCORE0; q++) {
        double p = cv_mq_penalty(q, t, out[q]);
        if (p != p) continue;
        n++;
        if (p < 1) { pass += p; np++; } else fail += p;
    }
    out[CV_MQ_HMQI] = n ? (np ? pass / np : 0) + fail : NAN;

    int k = kind(t);
    double C = k == K_HEX ? 41.56921938 : k == K_TET ? 124.70765802 : k == K_WEDGE ? 62.35382905
             : k == K_QUAD ? 4.0 : 6.92820323, V = out[CV_MQ_SIZE];
    if (V == V && sum_e2 > 0)
        out[CV_MQ_ANSYS] = V <= 0 ? 0 : cv_mq_dim(t) == 3 ? C * V / sqrt(sum_e2 * sum_e2 * sum_e2) : C * V / sum_e2;

    static const int checks[] = { CV_MQ_ASPECT, CV_MQ_ANGLE_MIN, CV_MQ_ANGLE_MAX, CV_MQ_SHAPE };
    int bad = V <= 0 || out[CV_MQ_JRATIO] <= 0;      /* inside out: Abaqus stops on it */
    for (size_t i = 0; i < sizeof checks / sizeof checks[0]; i++) bad += cv_mq_poor(checks[i], t, out[checks[i]]);
    out[CV_MQ_ABAQUS] = bad;
}

/* ---- vectors ------------------------------------------------------------------------ */

static void sub(const double* a, const double* b, double* o) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static double dot(const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static double len(const double* a) { return sqrt(dot(a, a)); }
static void cross(const double* a, const double* b, double* o) {
    double t[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    memcpy(o, t, sizeof t);
}
static double det3(const double* a, const double* b, const double* c) { double t[3]; cross(b, c, t); return dot(a, t); }
static double angle(const double* a, const double* b) {        /* degrees, robust near 0 and 180 */
    double c[3];
    cross(a, b, c);
    return atan2(len(c), dot(a, b)) / DEG;
}

/* ---- topology (corner indices, .frd order) ------------------------------------------ */

static const int kHexEdges[][2]   = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
static const int kWedgeEdges[][2] = { {0,1},{1,2},{2,0},{3,4},{4,5},{5,3},{0,3},{1,4},{2,5} };
static const int kTetEdges[][2]   = { {0,1},{1,2},{2,0},{0,3},{1,3},{2,3} };
static const int kTriEdges[][2]   = { {0,1},{1,2},{2,0} };
static const int kQuadEdges[][2]  = { {0,1},{1,2},{2,3},{3,0} };

/* faces: 4 indices, the last -1 for a triangle */
static const int kHexFaces[][4]   = { {0,1,2,3},{4,7,6,5},{0,4,5,1},{1,5,6,2},{2,6,7,3},{3,7,4,0} };
static const int kWedgeFaces[][4] = { {0,1,2,-1},{3,4,5,-1},{0,1,4,3},{1,2,5,4},{2,0,3,5} };
static const int kTetFaces[][4]   = { {0,1,2,-1},{0,3,1,-1},{1,3,2,-1},{2,3,0,-1} };
static const int kTriFaces[][4]   = { {0,1,2,-1} };
static const int kQuadFaces[][4]  = { {0,1,2,3} };

/* the three edges at each corner, right-handed for a valid element */
static const int kHexCorner[][3]   = { {1,3,4},{2,0,5},{3,1,6},{0,2,7},{7,5,0},{4,6,1},{5,7,2},{6,4,3} };
static const int kWedgeCorner[][3] = { {1,2,3},{2,0,4},{0,1,5},{5,4,0},{3,5,1},{4,3,2} };
static const int kTetCorner[][3]   = { {1,2,3},{2,0,3},{0,1,3},{2,1,0} };

/* ---- shape function derivatives: at a few integration points (size) and every node
   (Jacobian ratio), per element kind and node count. Central differences are exact:
   the shape functions are at most quadratic in each coordinate. */

enum { MAXN = 20, MAXP = 8 };
typedef struct {
    bool ok;
    int np;                          /* integration points */
    double w;                        /* their weight, all equal */
    double dq[MAXP][MAXN][3];        /* dN_i / dxi_a at the points */
    double dn[MAXN][MAXN][3];        /* ... at the nodes */
} dtab;

static dtab gTab[16][2];             /* [frd type][quadratic] */

static void deriv(int t, int nn, const double xi[3], int dim, double d[MAXN][3]) {
    const double h = 0.25;
    double Np[MAXN], Nm[MAXN];
    for (int a = 0; a < 3; a++) {
        if (a >= dim) { for (int i = 0; i < nn; i++) d[i][a] = 0; continue; }
        double p[3] = { xi[0], xi[1], xi[2] }, m[3] = { xi[0], xi[1], xi[2] };
        p[a] += h; m[a] -= h;
        cv_shape(t, nn, p, Np);
        cv_shape(t, nn, m, Nm);
        for (int i = 0; i < nn; i++) d[i][a] = (Np[i] - Nm[i]) / (2 * h);
    }
}

static const dtab* table(int t, int nn) {
    if (t <= 0 || t >= 16 || nn > MAXN) return NULL;
    int k = kind(t), q = nn > kCorners[k];
    dtab* T = &gTab[t][q];
    if (T->ok) return T;
    double N[MAXN], xi[3] = { 0, 0, 0 };
    if (k == K_LINE || !cv_shape(t, nn, xi, N)) return NULL;
    const double g = 1 / sqrt(3.0);
    double pts[MAXP][3];
    int np = 0;
    double vol = 0;
    switch (k) {
    case K_HEX:
        for (int i = 0; i < 8; i++) { pts[np][0] = i & 1 ? g : -g; pts[np][1] = i & 2 ? g : -g; pts[np][2] = i & 4 ? g : -g; np++; }
        vol = 8; break;
    case K_QUAD:
        for (int i = 0; i < 4; i++) { pts[np][0] = i & 1 ? g : -g; pts[np][1] = i & 2 ? g : -g; pts[np][2] = 0; np++; }
        vol = 4; break;
    case K_TET: {
        const double a = 0.5854101966249685, b = 0.1381966011250105;
        const double p[4][3] = { {b,b,b},{a,b,b},{b,a,b},{b,b,a} };
        memcpy(pts, p, sizeof p); np = 4; vol = 1.0 / 6; break;
    }
    case K_WEDGE: case K_TRI: {
        const double tp[3][2] = { {1.0/6,1.0/6},{2.0/3,1.0/6},{1.0/6,2.0/3} };
        int nz = k == K_WEDGE ? 2 : 1;
        for (int z = 0; z < nz; z++)
            for (int i = 0; i < 3; i++) { pts[np][0] = tp[i][0]; pts[np][1] = tp[i][1]; pts[np][2] = k == K_WEDGE ? (z ? g : -g) : 0; np++; }
        vol = k == K_WEDGE ? 1 : 0.5; break;
    }
    }
    int dim = k == K_TRI || k == K_QUAD ? 2 : 3;
    T->np = np;
    T->w = vol / np;
    for (int p = 0; p < np; p++) deriv(t, nn, pts[p], dim, T->dq[p]);
    for (int i = 0; i < nn; i++) {
        if (!cv_node_param(t, nn, i, xi)) return NULL;
        deriv(t, nn, xi, dim, T->dn[i]);
    }
    T->ok = true;
    return T;
}

void cv_mq_init(void) {
    static const int nn[][2] = { { 1, 8 }, { 4, 20 }, { 2, 6 }, { 5, 15 }, { 3, 4 }, { 6, 10 },
                                 { 7, 3 }, { 8, 6 }, { 9, 4 }, { 10, 8 }, { 4, 8 }, { 5, 6 }, { 6, 4 }, { 8, 3 }, { 10, 4 } };
    for (size_t i = 0; i < sizeof nn / sizeof nn[0]; i++) table(nn[i][0], nn[i][1]);
}

/* det J at a point: solids the 3x3 determinant, surfaces |dx/dr x dx/ds| signed by n */
static double detj(const double d[MAXN][3], int nn, const double (*x)[3], int dim, const double* n) {
    double J[3][3] = { { 0 } };
    for (int i = 0; i < nn; i++)
        for (int a = 0; a < dim; a++)
            for (int c = 0; c < 3; c++) J[a][c] += d[i][a] * x[i][c];
    if (dim == 3) return det3(J[0], J[1], J[2]);
    double g[3];
    cross(J[0], J[1], g);
    double l = len(g);
    return dot(g, n) < 0 ? -l : l;
}

/* ---- the measures --------------------------------------------------------------------- */

static void faces_of(int k, const int (**f)[4], int* nf) {
    switch (k) {
    case K_HEX:   *f = kHexFaces;   *nf = 6; return;
    case K_WEDGE: *f = kWedgeFaces; *nf = 5; return;
    case K_TET:   *f = kTetFaces;   *nf = 4; return;
    case K_TRI:   *f = kTriFaces;   *nf = 1; return;
    case K_QUAD:  *f = kQuadFaces;  *nf = 1; return;
    }
    *f = NULL; *nf = 0;
}

static void edges_of(int k, const int (**e)[2], int* ne) {
    switch (k) {
    case K_HEX:   *e = kHexEdges;   *ne = 12; return;
    case K_WEDGE: *e = kWedgeEdges; *ne = 9; return;
    case K_TET:   *e = kTetEdges;   *ne = 6; return;
    case K_TRI:   *e = kTriEdges;   *ne = 3; return;
    case K_QUAD:  *e = kQuadEdges;  *ne = 4; return;
    }
    *e = NULL; *ne = 0;
}

/* the angle between the normals of the two triangles of quad a b c d split on a-c */
static double warp_split(const double* a, const double* b, const double* c, const double* d) {
    double u[3], v[3], w[3], n1[3], n2[3];
    sub(b, a, u); sub(c, a, v); sub(d, a, w);
    cross(u, v, n1); cross(v, w, n2);
    return angle(n1, n2);
}

/* the circumradius of a tet a b c d (corner a) and of a triangle */
static double tet_circum(const double* a, const double* b, const double* c, const double* d) {
    double u[3], v[3], w[3], vw[3], wu[3], uv[3];
    sub(b, a, u); sub(c, a, v); sub(d, a, w);
    cross(v, w, vw); cross(w, u, wu); cross(u, v, uv);
    double den = 2 * dot(u, vw), uu = dot(u, u), vv = dot(v, v), ww = dot(w, w), o[3];
    if (den == 0) return INFINITY;
    for (int k = 0; k < 3; k++) o[k] = (uu * vw[k] + vv * wu[k] + ww * uv[k]) / den;
    return len(o);
}

void cv_mq_elem(int t, int nn, const double (*x)[3], double out[CV_MQ_N]) {
    for (int q = 0; q < CV_MQ_N; q++) out[q] = NAN;
    int k = kind(t);
    if (k == K_NONE || nn < kCorners[k]) return;
    int nc = kCorners[k];
    double c[8][3];                                  /* the corners */
    for (int i = 0; i < nc; i++) memcpy(c[i], x[k == K_LINE && i ? nn - 1 : i], sizeof c[i]);

    if (k == K_LINE) {
        double e[3];
        sub(c[1], c[0], e);
        out[CV_MQ_EDGE_MIN] = out[CV_MQ_EDGE_MAX] = len(e);
        if (nn >= 3) {                               /* end, middle, end: 3-point Gauss */
            const double gp[3] = { -sqrt(0.6), 0, sqrt(0.6) }, gw[3] = { 5.0 / 9, 8.0 / 9, 5.0 / 9 };
            double L = 0;
            for (int p = 0; p < 3; p++) {
                double r = gp[p], d0 = r - 0.5, d1 = -2 * r, d2 = r + 0.5, dx[3];
                for (int a = 0; a < 3; a++) dx[a] = d0 * x[0][a] + d1 * x[1][a] + d2 * x[2][a];
                L += gw[p] * len(dx);
            }
            out[CV_MQ_SIZE] = L;
        } else out[CV_MQ_SIZE] = len(e);
        return;
    }

    /* edges */
    const int (*E)[2]; int ne;
    edges_of(k, &E, &ne);
    double lmin = INFINITY, lmax = 0, sum_e2 = 0;
    for (int i = 0; i < ne; i++) {
        double e[3];
        sub(c[E[i][1]], c[E[i][0]], e);
        double l = len(e);
        sum_e2 += l * l;
        if (l < lmin) lmin = l;
        if (l > lmax) lmax = l;
    }
    out[CV_MQ_EDGE_MIN] = lmin;
    out[CV_MQ_EDGE_MAX] = lmax;
    out[CV_MQ_ASPECT] = lmin > 0 ? lmax / lmin : INFINITY;

    /* faces: angles, skew, warpage */
    const int (*F)[4]; int nf;
    faces_of(k, &F, &nf);
    double amin = 180, amax = 0, skew = 0, warp = NAN;
    for (int f = 0; f < nf; f++) {
        int m = F[f][3] < 0 ? 3 : 4;
        double flo = 180, fhi = 0;
        for (int i = 0; i < m; i++) {
            double u[3], v[3];
            sub(c[F[f][(i + 1) % m]], c[F[f][i]], u);
            sub(c[F[f][(i + m - 1) % m]], c[F[f][i]], v);
            double a = angle(u, v);
            if (a < flo) flo = a;
            if (a > fhi) fhi = a;
        }
        double te = m == 3 ? 60 : 90;
        double s1 = (fhi - te) / (180 - te), s2 = (te - flo) / te;
        if (s1 > skew) skew = s1;
        if (s2 > skew) skew = s2;
        if (flo < amin) amin = flo;
        if (fhi > amax) amax = fhi;
        if (m == 4) {
            const double *p0 = c[F[f][0]], *p1 = c[F[f][1]], *p2 = c[F[f][2]], *p3 = c[F[f][3]];
            double w = fmax(warp_split(p0, p1, p2, p3), warp_split(p1, p2, p3, p0));
            if (!(warp >= w)) warp = w;
        }
    }
    out[CV_MQ_ANGLE_MIN] = amin;
    out[CV_MQ_ANGLE_MAX] = amax;
    out[CV_MQ_SKEW] = skew;
    out[CV_MQ_WARP] = warp;

    /* the element normal of a surface (Newell, over the corners) */
    double n[3] = { 0, 0, 0 };
    if (k == K_TRI || k == K_QUAD)
        for (int i = 0; i < nc; i++) {
            const double *a = c[i], *b = c[(i + 1) % nc];
            n[0] += (a[1] - b[1]) * (a[2] + b[2]);
            n[1] += (a[2] - b[2]) * (a[0] + b[0]);
            n[2] += (a[0] - b[0]) * (a[1] + b[1]);
        }
    double nl = len(n);
    if (nl > 0) for (int a = 0; a < 3; a++) n[a] /= nl;

    /* scaled Jacobian at the corners */
    double sj = INFINITY;
    if (k == K_TRI || k == K_QUAD) {
        for (int i = 0; i < nc; i++) {
            double u[3], v[3], g[3];
            sub(c[(i + 1) % nc], c[i], u);
            sub(c[(i + nc - 1) % nc], c[i], v);
            cross(u, v, g);
            double l = len(u) * len(v), s = l > 0 ? dot(g, n) / l : 0;
            if (k == K_TRI) s *= 2 / sqrt(3.0);
            if (s < sj) sj = s;
        }
    } else {
        const int (*C)[3] = k == K_HEX ? kHexCorner : k == K_WEDGE ? kWedgeCorner : kTetCorner;
        double f = k == K_TET ? sqrt(2.0) : k == K_WEDGE ? 2 / sqrt(3.0) : 1;
        for (int i = 0; i < nc; i++) {
            double e[3][3];
            for (int j = 0; j < 3; j++) sub(c[C[i][j]], c[i], e[j]);
            double l = len(e[0]) * len(e[1]) * len(e[2]), s = l > 0 ? f * det3(e[0], e[1], e[2]) / l : 0;
            if (s < sj) sj = s;
        }
        if (k == K_HEX) {                            /* and the principal axes at the centre */
            double X[3][3];
            for (int a = 0; a < 3; a++) {
                X[0][a] = c[1][a] - c[0][a] + c[2][a] - c[3][a] + c[5][a] - c[4][a] + c[6][a] - c[7][a];
                X[1][a] = c[3][a] - c[0][a] + c[2][a] - c[1][a] + c[7][a] - c[4][a] + c[6][a] - c[5][a];
                X[2][a] = c[4][a] - c[0][a] + c[5][a] - c[1][a] + c[6][a] - c[2][a] + c[7][a] - c[3][a];
            }
            double l = len(X[0]) * len(X[1]) * len(X[2]), s = l > 0 ? det3(X[0], X[1], X[2]) / l : 0;
            if (s < sj) sj = s;
        }
    }
    if (k != K_HEX && k != K_QUAD && sj > 1) sj = 1;
    out[CV_MQ_SJAC] = sj;

    /* size and the Jacobian ratio, from the shape functions (CalculiX node order) */
    int dim = k == K_TRI || k == K_QUAD ? 2 : 3;
    const dtab* T = table(t, nn);
    int un = nn;
    if (!T) { T = table(t, nc); un = nc; }          /* an odd node count: the corners alone */
    if (T) {
        double xc[MAXN][3];
        for (int i = 0; i < un; i++) memcpy(xc[i], x[cv_frd_node_pos(t, un, i)], sizeof xc[i]);
        double V = 0, jmin = INFINITY, jmax = -INFINITY;
        for (int p = 0; p < T->np; p++) V += T->w * detj(T->dq[p], un, (const double (*)[3])xc, dim, n);
        for (int i = 0; i < un; i++) {
            double j = detj(T->dn[i], un, (const double (*)[3])xc, dim, n);
            if (j < jmin) jmin = j;
            if (j > jmax) jmax = j;
        }
        double big = fmax(fabs(jmin), fabs(jmax));
        out[CV_MQ_SIZE] = V;
        out[CV_MQ_JRATIO] = big > 0 ? (jmin > 0 ? jmin / jmax : jmin / big) : NAN;
    }

    /* shape factor */
    if (k == K_TET) {
        double R = tet_circum(c[0], c[1], c[2], c[3]), u[3], v[3], w[3];
        sub(c[1], c[0], u); sub(c[2], c[0], v); sub(c[3], c[0], w);
        double V = det3(u, v, w) / 6;
        out[CV_MQ_SHAPE] = R > 0 && R < INFINITY ? V / (8 * sqrt(3.0) / 27 * R * R * R) : 0;
    } else if (k == K_TRI) {
        double u[3], v[3], w[3], g[3];
        sub(c[1], c[0], u); sub(c[2], c[0], v); sub(c[2], c[1], w);
        cross(u, v, g);
        double A = len(g) / 2, R = A > 0 ? len(u) * len(v) * len(w) / (4 * A) : 0;
        out[CV_MQ_SHAPE] = R > 0 ? A / (3 * sqrt(3.0) / 4 * R * R) : 0;
    }
    scores(t, sum_e2, out);
}
