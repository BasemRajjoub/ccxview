/* t_shell.h -- shell section forces (shell.c): a tilted plate of linear solids and a
   three-layer quadratic laminate, built by hand with stresses known through the
   thickness, give back the forces and moments they were made from. Included by
   test_main.c. */
#include "../src/shell.h"

/* a small expanded mesh: nodes found again by position, so layers share theirs */
typedef struct { float xyz[3 * 256]; uint32_t nn; uint32_t conn[256], eoff[16]; uint8_t type[16]; uint32_t ne; } sh_mesh;

static uint32_t sh_node(sh_mesh* m, const double p[3]) {
    for (uint32_t i = 0; i < m->nn; i++)
        if (fabs(m->xyz[3 * i] - p[0]) < 1e-6 && fabs(m->xyz[3 * i + 1] - p[1]) < 1e-6 && fabs(m->xyz[3 * i + 2] - p[2]) < 1e-6) return i;
    for (int k = 0; k < 3; k++) m->xyz[3 * m->nn + k] = (float)p[k];
    return m->nn++;
}

/* the point (u, v, z) of the plate whose axes are the rows of Q, through o */
static void sh_point(const double Q[3][3], const double o[3], double u, double v, double z, double p[3]) {
    for (int k = 0; k < 3; k++) p[k] = o[k] + u * Q[0][k] + v * Q[1][k] + z * Q[2][k];
}

/* a hex over [u0,u1] x [v0,v1] x [z0,z1], .frd order; quadratic: the 20-node one.
   flip: the first face on top */
static void sh_hex(sh_mesh* m, const double Q[3][3], const double o[3], double u0, double u1, double v0, double v1,
                   double z0, double z1, bool quad, bool flip) {
    static const double c[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    static const double e[4][2] = { { .5, 0 }, { 1, .5 }, { .5, 1 }, { 0, .5 } };
    double za = flip ? z1 : z0, zb = flip ? z0 : z1, p[3];
    uint32_t b = m->eoff[m->ne], k = 0;
    for (int f = 0; f < 2; f++)
        for (int i = 0; i < 4; i++) {
            sh_point(Q, o, u0 + c[i][0] * (u1 - u0), v0 + c[i][1] * (v1 - v0), f ? zb : za, p);
            m->conn[b + k++] = sh_node(m, p);
        }
    if (quad) {
        for (int i = 0; i < 4; i++) { sh_point(Q, o, u0 + e[i][0] * (u1 - u0), v0 + e[i][1] * (v1 - v0), za, p); m->conn[b + k++] = sh_node(m, p); }
        for (int i = 0; i < 4; i++) { sh_point(Q, o, u0 + c[i][0] * (u1 - u0), v0 + c[i][1] * (v1 - v0), (za + zb) / 2, p); m->conn[b + k++] = sh_node(m, p); }
        for (int i = 0; i < 4; i++) { sh_point(Q, o, u0 + e[i][0] * (u1 - u0), v0 + e[i][1] * (v1 - v0), zb, p); m->conn[b + k++] = sh_node(m, p); }
    }
    m->type[m->ne] = quad ? 4 : 1;
    m->eoff[++m->ne] = b + k;
}

static cv_frd sh_frd(sh_mesh* m) {
    cv_frd f;
    memset(&f, 0, sizeof f);
    f.n_nodes = m->nn; f.xyz = m->xyz;
    f.n_elems = m->ne; f.eoff = m->eoff; f.conn = m->conn; f.etype = m->type;
    return f;
}

/* the global stress at height z (from the section's middle) of a plate whose local
   stress is linear: N / t + 12 M z / t^3, the shears tau(z) */
typedef struct { double N[3], M[3], t, qx, qy; bool parabola; } sh_state;

static void sh_stress(const double Q[3][3], const sh_state* S, double z, float out[6]) {
    double t = S->t, L[3][3] = { { 0 } }, T[3][3];
    double lin[3];
    for (int k = 0; k < 3; k++) lin[k] = S->N[k] / t + 12 * S->M[k] * z / (t * t * t);
    double shape = S->parabola ? 1.5 * (1 - 4 * z * z / (t * t)) : 1;   /* integrates to t over the thickness */
    L[0][0] = lin[0]; L[1][1] = lin[1]; L[0][1] = L[1][0] = lin[2];
    L[0][2] = L[2][0] = S->qx / t * shape; L[1][2] = L[2][1] = S->qy / t * shape;
    for (int i = 0; i < 3; i++)                          /* T = Q^T L Q */
        for (int j = 0; j < 3; j++) {
            double s = 0;
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++) s += Q[a][i] * L[a][b] * Q[b][j];
            T[i][j] = s;
        }
    out[0] = (float)T[0][0]; out[1] = (float)T[1][1]; out[2] = (float)T[2][2];
    out[3] = (float)T[0][1]; out[4] = (float)T[1][2]; out[5] = (float)T[0][2];
}

static void test_shell(void) {
    CHECK(!strcmp(cv_shell_comp(CV_SF_MXX), "Mxx") && !strcmp(cv_shell_comp(CV_SF_QY), "Qy"));
    /* a plate tilted out of every plane: e1, e2 in it, e3 its normal */
    const double a = 0.4, b = 0.7;
    double Q[3][3] = { { cos(a), sin(a), 0 }, { -sin(a) * cos(b), cos(a) * cos(b), sin(b) }, { 0, 0, 0 } };
    Q[2][0] = Q[0][1] * Q[1][2] - Q[0][2] * Q[1][1];
    Q[2][1] = Q[0][2] * Q[1][0] - Q[0][0] * Q[1][2];
    Q[2][2] = Q[0][0] * Q[1][1] - Q[0][1] * Q[1][0];
    const double o[3] = { 10, -5, 3 };
    float q9[9];
    for (int k = 0; k < 9; k++) q9[k] = (float)Q[k / 3][k % 3];

    /* linear: a cantilever's root, M = F L per width, a pull, a twist and a constant
       transverse shear; two C3D8 side by side, one with its faces the other way up,
       and a solid (not a shell) beside them */
    {
        static sh_mesh m;
        memset(&m, 0, sizeof m);
        const double t = 2;
        sh_hex(&m, Q, o, 0, 10, 0, 10, -t / 2, t / 2, false, false);
        sh_hex(&m, Q, o, 10, 20, 0, 10, -t / 2, t / 2, false, true);
        sh_hex(&m, Q, o, 20, 30, 0, 10, -t / 2, t / 2, false, false);
        cv_frd f = sh_frd(&m);
        sh_state S = { { 50, -20, 7 }, { 20.0 * 100 / 20, 30, -4 }, t, 1.5, -0.5, false };
        float s[6 * 256], out[CV_SF_N * 256], q[9 * 3];
        for (uint32_t i = 0; i < m.nn; i++) {
            double z = 0;
            for (int k = 0; k < 3; k++) z += (m.xyz[3 * i + k] - o[k]) * Q[2][k];
            sh_stress(Q, &S, z, s + 6 * i);
        }
        for (int e = 0; e < 3; e++) memcpy(q + 9 * e, q9, sizeof q9);
        const uint32_t shell[3] = { 0, 1, UINT32_MAX };
        CHECK(cv_shell_forces(&f, shell, q, 2, s, 6, out));
        const double want[CV_SF_N] = { S.N[0], S.N[1], S.N[2], S.M[0], S.M[1], S.M[2], S.qx, S.qy };
        int bad = 0;
        for (uint32_t e = 0; e < 2; e++)
            for (uint32_t j = m.eoff[e]; j < m.eoff[e + 1]; j++)
                for (int c = 0; c < CV_SF_N; c++) bad += !(fabs(out[CV_SF_N * m.conn[j] + c] - want[c]) < 1e-3 * (1 + fabs(want[c])));
        CHECK_EQ(bad, 0);
        CHECK_NEAR(out[CV_SF_N * m.conn[0] + CV_SF_MXX], 100, 1e-3);
        uint32_t lone = m.conn[m.eoff[2] + 1];        /* only the solid's */
        CHECK(out[CV_SF_N * lone] != out[CV_SF_N * lone]);
    }

    /* three layers of C3D20, 0.5, 1.0 and 0.7 thick, one shell: stress linear over the
       whole section, transverse shear the parabola. N and M exact at every node; Q
       exact on the lines with a middle node (Simpson), not between them */
    {
        static sh_mesh m;
        memset(&m, 0, sizeof m);
        const double zl[4] = { -1.1, -0.6, 0.4, 1.1 };
        for (int l = 0; l < 3; l++) sh_hex(&m, Q, o, 0, 8, 0, 6, zl[l], zl[l + 1], true, l == 1);
        cv_frd f = sh_frd(&m);
        sh_state S = { { -12, 33, 4 }, { 9, -2.5, 6 }, 2.2, 3, 2, true };
        float s[6 * 256], out[CV_SF_N * 256], q[9 * 3];
        for (uint32_t i = 0; i < m.nn; i++) {
            double z = 0;
            for (int k = 0; k < 3; k++) z += (m.xyz[3 * i + k] - o[k]) * Q[2][k];
            sh_stress(Q, &S, z, s + 6 * i);
        }
        for (int e = 0; e < 3; e++) memcpy(q + 9 * e, q9, sizeof q9);
        const uint32_t shell[3] = { 0, 0, 0 };
        CHECK(cv_shell_forces(&f, shell, q, 1, s, 6, out));
        CHECK_EQ(m.nn, 4 * 4 + 4 * 3 + 4 * 4);           /* the layers share their faces */
        const double want[CV_SF_N] = { S.N[0], S.N[1], S.N[2], S.M[0], S.M[1], S.M[2], S.qx, S.qy };
        int bad = 0, badq = 0;
        for (uint32_t e = 0; e < 3; e++)
            for (uint32_t j = m.eoff[e]; j < m.eoff[e + 1]; j++) {
                const float* v = out + CV_SF_N * m.conn[j];
                for (int c = 0; c < CV_SF_QX; c++) bad += !(fabs(v[c] - want[c]) < 1e-3 * (1 + fabs(want[c])));
                bool corner = j - m.eoff[e] < 8 || (j - m.eoff[e] >= 12 && j - m.eoff[e] < 16);
                for (int c = CV_SF_QX; corner && c < CV_SF_N; c++) badq += !(fabs(v[c] - want[c]) < 1e-3 * (1 + fabs(want[c])));
            }
        CHECK_EQ(bad, 0);
        CHECK_EQ(badq, 0);
        /* between the corners: the trapezoid of a parabola that the layer faces cut */
        const float* mid = out + CV_SF_N * m.conn[m.eoff[0] + 8];
        CHECK(fabs(mid[CV_SF_QX] - S.qx) > 1e-3 && fabs(mid[CV_SF_QX] - S.qx) < 0.2 * S.qx);
        /* a missing stress: no value on its shell, NaN not spread as a number */
        s[6 * m.conn[0]] = NAN;
        CHECK(cv_shell_forces(&f, shell, q, 1, s, 6, out));
        CHECK(out[CV_SF_N * m.conn[0]] != out[CV_SF_N * m.conn[0]]);
    }
}
