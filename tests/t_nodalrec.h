#include "../src/nodalrec.h"
#include "../src/gauss.h"
/* t_nodalrec.h -- unit tests for nodalrec.c (integration points -> nodes, threshold averaging). Included by test_main.c. */

/* a linear field of the natural coordinates */
static double nr_lin(const double xi[3]) { return 3 + 2 * xi[0] + 0.5 * xi[1] - xi[2]; }

/* its values at the points come back exact at every node, mid-side ones included */
static void nr_exact(int t, int nn, int nip) {
    float M[CV_NR_NN * CV_NR_NIP], v[CV_NR_NIP];
    CHECK(cv_nr_matrix(t, nn, nip, M));
    for (int k = 0; k < nip; k++) { double xi[3]; CHECK(cv_ip_param(t, nip, k, xi)); v[k] = (float)nr_lin(xi); }
    for (int i = 0; i < nn; i++) {                  /* CalculiX node i sits at conn position cv_frd_node_pos */
        double xi[3], s = 0;
        CHECK(cv_node_param(t, nn, i, xi));
        int p = cv_frd_node_pos(t, nn, i);
        for (int k = 0; k < nip; k++) s += M[p * nip + k] * v[k];
        CHECK_NEAR(s, nr_lin(xi), 1e-5);
    }
}

static void test_nodalrec(void) {
    nr_exact(1, 8, 8);                              /* C3D8 */
    nr_exact(4, 20, 8);                             /* C3D20R */
    nr_exact(4, 20, 27);                            /* C3D20, least squares through 27 points */
    nr_exact(3, 4, 4);                              /* C3D4 */
    nr_exact(6, 10, 4);                             /* C3D10 */
    nr_exact(2, 6, 6);                              /* wedge: linear in the plane and the thickness */
    nr_exact(5, 15, 9);                             /* C3D15 */
    {   /* no rule: says so, and the matrix is the constant (the mean of the points) */
        float M[CV_NR_NN * CV_NR_NIP];
        CHECK(!cv_nr_matrix(1, 8, 1, M));            /* C3D8R, one point */
        for (int i = 0; i < 8; i++) CHECK_NEAR(M[i], 1.0, 1e-6);
        CHECK(!cv_nr_matrix(4, 20, 5, M));            /* a point count without a table */
        CHECK_NEAR(M[3 * 5 + 2], 0.2, 1e-6);
        CHECK(!cv_nr_matrix(11, 2, 1, M));            /* a beam type */
    }
    {   /* the fit is not the mean: xi*eta*zeta at the corner is 3 sqrt(3) times its value at the points */
        float M[CV_NR_NN * CV_NR_NIP], v[8];
        CHECK(cv_nr_matrix(1, 8, 8, M));
        for (int k = 0; k < 8; k++) { double xi[3]; cv_ip_param(1, 8, k, xi); v[k] = (float)(xi[0] * xi[1] * xi[2]); }
        double xi[3], s = 0;
        cv_node_param(1, 8, 0, xi);
        for (int k = 0; k < 8; k++) s += M[cv_frd_node_pos(1, 8, 0) * 8 + k] * v[k];
        CHECK_NEAR(s, xi[0] * xi[1] * xi[2], 1e-4);          /* +-1 at the corner from +-0.19245 at the points */
    }

    /* two hexes sharing nodes 4..7: element 0 = nodes 0..7, element 1 = nodes 4..11 */
    uint32_t eoff[3] = { 0, 8, 16 }, conn[16];
    for (int i = 0; i < 8; i++) { conn[i] = (uint32_t)i; conn[8 + i] = (uint32_t)(4 + i); }
    cv_nr_topo T;
    CHECK(cv_nr_topo_build(&T, 12, 2, eoff, conn));
    CHECK_EQ(T.n_slots, 16);
    CHECK_EQ(T.start[5] - T.start[4], 2);            /* a shared node: two slots */
    CHECK_EQ(T.start[1] - T.start[0], 1);
    float v[16], o[16];
    for (int i = 0; i < 4; i++) { v[i] = 0; v[4 + i] = 10; v[8 + i] = 14; v[12 + i] = 20; }
    /* element 0: 0 away from the face, 10 on it; element 1: 14 on it, 20 beyond. Range 20, the shared nodes vary 4: 20 % */
    CHECK_NEAR(cv_nr_average(&T, v, NULL, NULL, 0, o), 20, 1e-9);
    CHECK_NEAR(o[4], 10, 1e-6); CHECK_NEAR(o[8], 14, 1e-6);          /* 0 %: nothing averaged */
    cv_nr_average(&T, v, NULL, NULL, 19, o);
    CHECK_NEAR(o[4], 10, 1e-6); CHECK_NEAR(o[8], 14, 1e-6);          /* below the variation: kept */
    cv_nr_average(&T, v, NULL, NULL, 21, o);
    CHECK_NEAR(o[4], 12, 1e-6); CHECK_NEAR(o[8], 12, 1e-6);          /* above: the node's mean */
    CHECK_NEAR(o[0], 0, 1e-6); CHECK_NEAR(o[15], 20, 1e-6);          /* a node of one element is its own */
    cv_nr_average(&T, v, NULL, NULL, 100, o);
    CHECK_NEAR(o[7], 12, 1e-6); CHECK_NEAR(o[11], 12, 1e-6);         /* 100 %: always */
    uint32_t mat[2] = { 1, 2 };
    cv_nr_average(&T, v, NULL, mat, 100, o);
    CHECK_NEAR(o[4], 10, 1e-6); CHECK_NEAR(o[8], 14, 1e-6);          /* not across materials: the jump stays */
    mat[1] = 1;
    cv_nr_average(&T, v, NULL, mat, 100, o);
    CHECK_NEAR(o[4], 12, 1e-6);                                       /* the same material: averaged */
    uint8_t vis[2] = { 1, 0 };
    double r = cv_nr_average(&T, v, vis, NULL, 100, o);
    CHECK_NEAR(r, 10, 1e-9);                                          /* the range of what is shown */
    CHECK_NEAR(o[4], 10, 1e-6); CHECK(o[8] != o[8]);                  /* a hidden element: no value, nothing to average */
    float vn[16];
    memcpy(vn, v, sizeof v);
    vn[8] = NAN;
    cv_nr_average(&T, vn, NULL, NULL, 100, o);
    CHECK_NEAR(o[4], 10, 1e-6);                                       /* a missing contribution is left out */
    cv_nr_topo_free(&T);
}
