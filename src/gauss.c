/* gauss.c -- CalculiX integration points, shape functions and face patches. */
#include "gauss.h"

enum { K_NONE, K_HEX, K_TET, K_WEDGE, K_TRI, K_QUAD };

static int kind_of(int t) {
    switch (t) {
        case 1: case 4: return K_HEX;
        case 3: case 6: return K_TET;
        case 2: case 5: return K_WEDGE;
        case 7: case 8: return K_TRI;      /* surface elements: points lie on the face */
        case 9: case 10: return K_QUAD;
        default: return K_NONE;
    }
}

/* ---- point tables (CalculiX gauss.f) ---------------------------------------- */

static const double G2 = 0.577350269189626, G3 = 0.774596669241483;

static const double kTet4[4][3] = {
    {0.138196601125011, 0.138196601125011, 0.138196601125011},
    {0.585410196624968, 0.138196601125011, 0.138196601125011},
    {0.138196601125011, 0.585410196624968, 0.138196601125011},
    {0.138196601125011, 0.138196601125011, 0.585410196624968},
};
static const double kTet15[15][3] = {
    {0.25, 0.25, 0.25},
    {0.091971078052723, 0.091971078052723, 0.091971078052723},
    {0.724086765841831, 0.091971078052723, 0.091971078052723},
    {0.091971078052723, 0.724086765841831, 0.091971078052723},
    {0.091971078052723, 0.091971078052723, 0.724086765841831},
    {0.319793627829630, 0.319793627829630, 0.319793627829630},
    {0.040619116511110, 0.319793627829630, 0.319793627829630},
    {0.319793627829630, 0.040619116511110, 0.319793627829630},
    {0.319793627829630, 0.319793627829630, 0.040619116511110},
    {0.056350832689629, 0.056350832689629, 0.443649167310371},
    {0.443649167310371, 0.056350832689629, 0.056350832689629},
    {0.443649167310371, 0.443649167310371, 0.056350832689629},
    {0.056350832689629, 0.443649167310371, 0.443649167310371},
    {0.056350832689629, 0.443649167310371, 0.056350832689629},
    {0.443649167310371, 0.056350832689629, 0.443649167310371},
};
static const double kWedge18[18][3] = {
    {1.0/6, 1.0/6, -0.774596669241483}, {1.0/6, 2.0/3, -0.774596669241483}, {2.0/3, 1.0/6, -0.774596669241483},
    {0.0, 0.5, -0.774596669241483},     {0.5, 0.0, -0.774596669241483},     {0.5, 0.5, -0.774596669241483},
    {1.0/6, 1.0/6, 0.0}, {1.0/6, 2.0/3, 0.0}, {2.0/3, 1.0/6, 0.0},
    {0.0, 0.5, 0.0},     {0.5, 0.0, 0.0},     {0.5, 0.5, 0.0},
    {1.0/6, 1.0/6, 0.774596669241483}, {1.0/6, 2.0/3, 0.774596669241483}, {2.0/3, 1.0/6, 0.774596669241483},
    {0.0, 0.5, 0.774596669241483},     {0.5, 0.0, 0.774596669241483},     {0.5, 0.5, 0.774596669241483},
};

/* hex n x n x n: xi fastest, then eta, then zeta (gauss3d1/2/3) */
static int hex_n(int nip) { return nip == 1 ? 1 : nip == 8 ? 2 : nip == 27 ? 3 : 0; }
static double g1d(int n, int i) {
    if (n == 1) return 0;
    if (n == 2) return i == 0 ? -G2 : G2;
    return i == 0 ? -G3 : i == 1 ? 0 : G3;
}

/* wedge: layers along zeta x (1 or 3) points per layer, corner order */
static bool wedge_layout(int nip, int* nl, int* nt) {
    switch (nip) {
        case 1: *nl = 1; *nt = 1; return true;
        case 2: *nl = 2; *nt = 1; return true;
        case 6: *nl = 2; *nt = 3; return true;
        case 9: *nl = 3; *nt = 3; return true;
        default: return false;
    }
}
static double wedge_layer_z(int nl, int l) {
    if (nl == 1) return 0;
    if (nl == 2) return l == 0 ? -G2 : G2;
    return l == 0 ? -G3 : l == 1 ? 0 : G3;
}

bool cv_ip_param(int t, int nip, int ip, double xi[3]) {
    if (ip < 0 || ip >= nip) return false;
    switch (kind_of(t)) {
        case K_HEX: {
            int n = hex_n(nip);
            if (!n) return false;
            xi[0] = g1d(n, ip % n); xi[1] = g1d(n, (ip / n) % n); xi[2] = g1d(n, ip / (n * n));
            return true;
        }
        case K_TET:
            if (nip == 1) { xi[0] = xi[1] = xi[2] = 0.25; return true; }
            if (nip == 4) { memcpy(xi, kTet4[ip], sizeof(double) * 3); return true; }
            if (nip == 15) { memcpy(xi, kTet15[ip], sizeof(double) * 3); return true; }
            return false;
        case K_WEDGE: {
            if (nip == 18) { memcpy(xi, kWedge18[ip], sizeof(double) * 3); return true; }
            int nl, nt;
            if (!wedge_layout(nip, &nl, &nt)) return false;
            int l = ip / nt, c = ip % nt;
            static const double tri[3][2] = { {1.0/6, 1.0/6}, {2.0/3, 1.0/6}, {1.0/6, 2.0/3} };
            xi[0] = nt == 1 ? 1.0 / 3 : tri[c][0];
            xi[1] = nt == 1 ? 1.0 / 3 : tri[c][1];
            xi[2] = wedge_layer_z(nl, l);
            return true;
        }
        case K_QUAD: {                              /* gauss2d1/2/3: xi fastest */
            int n = nip == 1 ? 1 : nip == 4 ? 2 : nip == 9 ? 3 : 0;
            if (!n) return false;
            xi[0] = g1d(n, ip % n); xi[1] = g1d(n, ip / n); xi[2] = 0;
            return true;
        }
        case K_TRI: {                               /* gauss2d4/5/6 */
            static const double t3[3][2] = { {1.0/6, 1.0/6}, {2.0/3, 1.0/6}, {1.0/6, 2.0/3} };
            static const double t7[7][2] = {
                {0.333333333333333, 0.333333333333333}, {0.797426985353087, 0.101286507323456},
                {0.101286507323456, 0.797426985353087}, {0.101286507323456, 0.101286507323456},
                {0.470142064105115, 0.059715871789770}, {0.059715871789770, 0.470142064105115},
                {0.470142064105115, 0.470142064105115},
            };
            xi[2] = 0;
            if (nip == 1) { xi[0] = xi[1] = 1.0 / 3; return true; }
            if (nip == 3) { xi[0] = t3[ip][0]; xi[1] = t3[ip][1]; return true; }
            if (nip == 7) { xi[0] = t7[ip][0]; xi[1] = t7[ip][1]; return true; }
            return false;
        }
        default: return false;
    }
}

/* ---- shape functions ---------------------------------------------------------- */

static const double kHexNode[20][3] = {
    {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1}, {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1},
    {0,-1,-1}, {1,0,-1}, {0,1,-1}, {-1,0,-1}, {0,-1,1}, {1,0,1}, {0,1,1}, {-1,0,1},
    {-1,-1,0}, {1,-1,0}, {1,1,0}, {-1,1,0},
};
static const double kTetNode[10][3] = {
    {0,0,0}, {1,0,0}, {0,1,0}, {0,0,1},
    {0.5,0,0}, {0.5,0.5,0}, {0,0.5,0}, {0,0,0.5}, {0.5,0,0.5}, {0,0.5,0.5},
};
static const double kQuadNode[8][3] = {
    {-1,-1,0}, {1,-1,0}, {1,1,0}, {-1,1,0}, {0,-1,0}, {1,0,0}, {0,1,0}, {-1,0,0},
};
static const double kTriNode[6][3] = {
    {0,0,0}, {1,0,0}, {0,1,0}, {0.5,0,0}, {0.5,0.5,0}, {0,0.5,0},
};
static const double kWedgeNode[15][3] = {
    {0,0,-1}, {1,0,-1}, {0,1,-1}, {0,0,1}, {1,0,1}, {0,1,1},
    {0.5,0,-1}, {0.5,0.5,-1}, {0,0.5,-1}, {0.5,0,1}, {0.5,0.5,1}, {0,0.5,1},
    {0,0,0}, {1,0,0}, {0,1,0},
};

bool cv_node_param(int t, int nn, int k, double xi[3]) {
    const double* p = NULL;
    switch (kind_of(t)) {
        case K_HEX:   if ((nn == 8 || nn == 20) && k < nn) p = kHexNode[k]; break;
        case K_TET:   if ((nn == 4 || nn == 10) && k < nn) p = kTetNode[k]; break;
        case K_WEDGE: if ((nn == 6 || nn == 15) && k < nn) p = kWedgeNode[k]; break;
        case K_QUAD:  if ((nn == 4 || nn == 8) && k < nn) p = kQuadNode[k]; break;
        case K_TRI:   if ((nn == 3 || nn == 6) && k < nn) p = kTriNode[k]; break;
    }
    if (!p || k < 0) return false;
    memcpy(xi, p, sizeof(double) * 3);
    return true;
}

bool cv_shape(int t, int nn, const double x[3], double* N) {
    const double r = x[0], s = x[1], z = x[2];
    switch (kind_of(t)) {
        case K_QUAD:
            if (nn == 4) {
                for (int i = 0; i < 4; i++) N[i] = 0.25 * (1 + r * kQuadNode[i][0]) * (1 + s * kQuadNode[i][1]);
                return true;
            }
            if (nn == 8) {
                for (int i = 0; i < 4; i++) {
                    const double* p = kQuadNode[i];
                    N[i] = 0.25 * (1 + r * p[0]) * (1 + s * p[1]) * (r * p[0] + s * p[1] - 1);
                }
                for (int i = 4; i < 8; i++) {
                    const double* p = kQuadNode[i];
                    N[i] = p[0] == 0 ? 0.5 * (1 - r * r) * (1 + s * p[1]) : 0.5 * (1 + r * p[0]) * (1 - s * s);
                }
                return true;
            }
            return false;
        case K_TRI: {
            double L[3] = { 1 - r - s, r, s };
            if (nn == 3) { for (int i = 0; i < 3; i++) N[i] = L[i]; return true; }
            if (nn == 6) {
                for (int i = 0; i < 3; i++) N[i] = L[i] * (2 * L[i] - 1);
                N[3] = 4 * L[0] * L[1]; N[4] = 4 * L[1] * L[2]; N[5] = 4 * L[2] * L[0];
                return true;
            }
            return false;
        }
        case K_HEX:
            if (nn == 8) {
                for (int i = 0; i < 8; i++)
                    N[i] = 0.125 * (1 + r * kHexNode[i][0]) * (1 + s * kHexNode[i][1]) * (1 + z * kHexNode[i][2]);
                return true;
            }
            if (nn == 20) {
                for (int i = 0; i < 8; i++) {
                    const double* p = kHexNode[i];
                    N[i] = 0.125 * (1 + r * p[0]) * (1 + s * p[1]) * (1 + z * p[2]) *
                           (r * p[0] + s * p[1] + z * p[2] - 2);
                }
                for (int i = 8; i < 20; i++) {
                    const double* p = kHexNode[i];
                    if (p[0] == 0)      N[i] = 0.25 * (1 - r * r) * (1 + s * p[1]) * (1 + z * p[2]);
                    else if (p[1] == 0) N[i] = 0.25 * (1 + r * p[0]) * (1 - s * s) * (1 + z * p[2]);
                    else                N[i] = 0.25 * (1 + r * p[0]) * (1 + s * p[1]) * (1 - z * z);
                }
                return true;
            }
            return false;
        case K_TET: {
            double L[4] = { 1 - r - s - z, r, s, z };
            if (nn == 4) { for (int i = 0; i < 4; i++) N[i] = L[i]; return true; }
            if (nn == 10) {
                for (int i = 0; i < 4; i++) N[i] = L[i] * (2 * L[i] - 1);
                N[4] = 4 * L[0] * L[1]; N[5] = 4 * L[1] * L[2]; N[6] = 4 * L[2] * L[0];
                N[7] = 4 * L[0] * L[3]; N[8] = 4 * L[1] * L[3]; N[9] = 4 * L[2] * L[3];
                return true;
            }
            return false;
        }
        case K_WEDGE: {
            double L[3] = { 1 - r - s, r, s };
            if (nn == 6) {
                for (int i = 0; i < 3; i++) { N[i] = 0.5 * L[i] * (1 - z); N[i + 3] = 0.5 * L[i] * (1 + z); }
                return true;
            }
            if (nn == 15) {
                for (int i = 0; i < 3; i++) {
                    N[i]     = 0.5 * L[i] * ((2 * L[i] - 1) * (1 - z) - (1 - z * z));
                    N[i + 3] = 0.5 * L[i] * ((2 * L[i] - 1) * (1 + z) - (1 - z * z));
                    N[i + 12] = L[i] * (1 - z * z);
                }
                N[6] = 2 * L[0] * L[1] * (1 - z); N[7] = 2 * L[1] * L[2] * (1 - z); N[8] = 2 * L[2] * L[0] * (1 - z);
                N[9] = 2 * L[0] * L[1] * (1 + z); N[10] = 2 * L[1] * L[2] * (1 + z); N[11] = 2 * L[2] * L[0] * (1 + z);
                return true;
            }
            return false;
        }
        default: return false;
    }
}

/* ---- .frd node order ----------------------------------------------------------- */

int cv_frd_node_pos(int t, int nn, int i) {
    if (t == 4 && nn == 20) {                      /* deck 13-16 top, 17-20 vertical */
        if (i >= 12 && i < 16) return i + 4;
        if (i >= 16 && i < 20) return i - 4;
    }
    if (t == 5 && nn == 15) {                      /* deck 10-12 top, 13-15 vertical */
        if (i >= 9 && i < 12) return i + 3;
        if (i >= 12 && i < 15) return i - 3;
    }
    return i;
}
