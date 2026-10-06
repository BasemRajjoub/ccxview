/* t_quality.h -- mesh quality measures (quality.c): ideal elements score their ideal
   values, distorted, warped, curved and inverted ones what hand geometry gives.
   Included by test_main.c. */
#include "../src/quality.h"

/* the natural coordinates of an element type as its nodes (.frd order), scaled */
static void q_natural(int t, int nn, double s, double (*x)[3]) {
    for (int i = 0; i < nn; i++) {
        double xi[3];
        cv_node_param(t, nn, i, xi);
        for (int a = 0; a < 3; a++) x[cv_frd_node_pos(t, nn, i)][a] = s * xi[a];
    }
}

static void test_quality(void) {
    double x[20][3], q[CV_MQ_N];
    const double r3 = sqrt(3.0);

    /* the info table and lookup */
    CHECK_EQ(cv_mq_find("SJAC"), CV_MQ_SJAC);
    CHECK_EQ(cv_mq_find("aspect ratio"), CV_MQ_ASPECT);
    CHECK_EQ(cv_mq_find("nope"), -1);
    for (int i = 0; i < CV_MQ_N; i++) CHECK(cv_mq(i)->key && cv_mq(i)->name && cv_mq(i)->tip);
    CHECK_EQ(cv_mq_dim(4), 3); CHECK_EQ(cv_mq_dim(8), 2); CHECK_EQ(cv_mq_dim(12), 1);
    CHECK(cv_mq_poor(CV_MQ_ASPECT, 1, 11) && !cv_mq_poor(CV_MQ_ASPECT, 1, 9));
    CHECK(cv_mq_poor(CV_MQ_SJAC, 3, 0.4) && !cv_mq_poor(CV_MQ_SJAC, 9, 0.4));
    CHECK(!cv_mq_poor(CV_MQ_WARP, 3, 50));               /* tets have no quad faces */
    cv_mq_set_limit(CV_MQ_ASPECT, 5);                   /* the user's limit, for every type */
    CHECK(cv_mq_poor(CV_MQ_ASPECT, 1, 6) && cv_mq_poor(CV_MQ_ASPECT, 3, 6) && !cv_mq_poor(CV_MQ_ASPECT, 1, 4));
    CHECK_NEAR(cv_mq_usual_limit(CV_MQ_ASPECT, 1), 10, 0);
    CHECK(cv_mq_limit(CV_MQ_ASPECT, 11) != cv_mq_limit(CV_MQ_ASPECT, 11));   /* beams still have none */
    cv_mq_set_limit(CV_MQ_ASPECT, 0);
    CHECK(!cv_mq_poor(CV_MQ_ASPECT, 1, 6));             /* back to the usual 10 */

    /* the [-1,1]^3 cube, linear and quadratic */
    for (int t = 1; t <= 4; t += 3) {
        int nn = t == 1 ? 8 : 20;
        q_natural(t, nn, 1, x);
        cv_mq_elem(t, nn, (const double (*)[3])x, q);
        CHECK_NEAR(q[CV_MQ_SIZE], 8, 1e-12);
        CHECK_NEAR(q[CV_MQ_EDGE_MIN], 2, 1e-12);
        CHECK_NEAR(q[CV_MQ_ASPECT], 1, 1e-12);
        CHECK_NEAR(q[CV_MQ_SJAC], 1, 1e-12);
        CHECK_NEAR(q[CV_MQ_JRATIO], 1, 1e-12);
        CHECK_NEAR(q[CV_MQ_SKEW], 0, 1e-12);
        CHECK_NEAR(q[CV_MQ_ANGLE_MIN], 90, 1e-9);
        CHECK_NEAR(q[CV_MQ_ANGLE_MAX], 90, 1e-9);
        CHECK_NEAR(q[CV_MQ_WARP], 0, 1e-9);
        CHECK(q[CV_MQ_SHAPE] != q[CV_MQ_SHAPE]);
    }
    /* a curved hex20: one mid-side node pulled along its edge -> uneven mapping */
    q_natural(4, 20, 1, x);
    x[cv_frd_node_pos(4, 20, 8)][0] = 0.3;               /* node 9, on edge 1-2 */
    cv_mq_elem(4, 20, (const double (*)[3])x, q);
    CHECK(q[CV_MQ_JRATIO] < 0.5 && q[CV_MQ_JRATIO] > 0);
    CHECK_NEAR(q[CV_MQ_SJAC], 1, 1e-12);                 /* the corners do not see it */
    CHECK_NEAR(q[CV_MQ_SIZE], 8, 1e-9);                  /* along the edge: the volume stays */
    /* past the quarter point the mapping folds */
    x[cv_frd_node_pos(4, 20, 8)][0] = 0.9;
    cv_mq_elem(4, 20, (const double (*)[3])x, q);
    CHECK(q[CV_MQ_JRATIO] < 0);

    /* stretched 10 x 1 x 1, and sheared: the top moved by one edge */
    q_natural(1, 8, 1, x);
    for (int i = 0; i < 8; i++) x[i][0] *= 10;
    cv_mq_elem(1, 8, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_ASPECT], 10, 1e-12);
    CHECK_NEAR(q[CV_MQ_SJAC], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_SIZE], 80, 1e-9);
    q_natural(1, 8, 1, x);
    for (int i = 4; i < 8; i++) x[i][0] += 2;
    cv_mq_elem(1, 8, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SJAC], 1 / sqrt(2.0), 1e-12);
    CHECK_NEAR(q[CV_MQ_SIZE], 8, 1e-9);
    CHECK_NEAR(q[CV_MQ_JRATIO], 1, 1e-12);               /* affine: det J the same everywhere */
    CHECK_NEAR(q[CV_MQ_ANGLE_MIN], 45, 1e-9);
    CHECK_NEAR(q[CV_MQ_SKEW], 0.5, 1e-9);
    /* a warped hex: one top corner lifted */
    q_natural(1, 8, 1, x);
    x[6][2] = 1.5;
    cv_mq_elem(1, 8, (const double (*)[3])x, q);
    CHECK(q[CV_MQ_WARP] > 10 && q[CV_MQ_WARP] < 30);
    CHECK(q[CV_MQ_JRATIO] < 1 && q[CV_MQ_JRATIO] > 0);

    /* the regular tet, edge 1 */
    const double T[4][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0.5, r3 / 2, 0 }, { 0.5, r3 / 6, sqrt(2.0 / 3) } };
    memcpy(x, T, sizeof T);
    cv_mq_elem(3, 4, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SIZE], 1 / (6 * sqrt(2.0)), 1e-12);
    CHECK_NEAR(q[CV_MQ_SJAC], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_SHAPE], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_SKEW], 0, 1e-9);
    CHECK_NEAR(q[CV_MQ_ANGLE_MIN], 60, 1e-9);
    CHECK_NEAR(q[CV_MQ_JRATIO], 1, 1e-12);
    CHECK(q[CV_MQ_WARP] != q[CV_MQ_WARP]);
    /* the same as tet10 with straight edges (mid nodes 4..9 on edges 01 12 20 03 13 23) */
    const int TE[6][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 }, { 0, 3 }, { 1, 3 }, { 2, 3 } };
    for (int i = 0; i < 6; i++)
        for (int a = 0; a < 3; a++) x[4 + i][a] = (T[TE[i][0]][a] + T[TE[i][1]][a]) / 2;
    cv_mq_elem(6, 10, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SIZE], 1 / (6 * sqrt(2.0)), 1e-12);
    CHECK_NEAR(q[CV_MQ_JRATIO], 1, 1e-9);
    /* inside out: two corners swapped */
    memcpy(x, T, sizeof T);
    memcpy(x[1], T[2], sizeof x[1]); memcpy(x[2], T[1], sizeof x[2]);
    cv_mq_elem(3, 4, (const double (*)[3])x, q);
    CHECK(q[CV_MQ_SIZE] < 0);
    CHECK_NEAR(q[CV_MQ_SJAC], -1, 1e-12);
    CHECK_NEAR(q[CV_MQ_JRATIO], -1, 1e-12);
    CHECK(cv_mq_poor(CV_MQ_SJAC, 3, q[CV_MQ_SJAC]));
    /* a sliver: two crossing edges nearly in one plane */
    const double S[4][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0.01 }, { 0, -1, 0.01 } };
    memcpy(x, S, sizeof S);
    cv_mq_elem(3, 4, (const double (*)[3])x, q);
    CHECK(fabs(q[CV_MQ_SHAPE]) < 0.05 && fabs(q[CV_MQ_SJAC]) < 0.05);
    CHECK(q[CV_MQ_ASPECT] < 1.5);                        /* the edges cannot see a sliver */

    /* a wedge: equilateral base of edge 1, height 1 */
    const double W[6][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0.5, r3 / 2, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 0.5, r3 / 2, 1 } };
    memcpy(x, W, sizeof W);
    cv_mq_elem(2, 6, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SIZE], r3 / 4, 1e-12);
    CHECK_NEAR(q[CV_MQ_SJAC], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_SKEW], 0, 1e-9);
    CHECK_NEAR(q[CV_MQ_WARP], 0, 1e-9);
    CHECK_NEAR(q[CV_MQ_JRATIO], 1, 1e-12);

    /* quads: the unit square, a 60 degree rhombus, a warped one, quad8 */
    const double Q[4][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
    memcpy(x, Q, sizeof Q);
    cv_mq_elem(9, 4, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SIZE], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_SJAC], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_WARP], 0, 1e-9);
    CHECK_NEAR(q[CV_MQ_JRATIO], 1, 1e-12);
    const double R[4][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1.5, r3 / 2, 0 }, { 0.5, r3 / 2, 0 } };
    memcpy(x, R, sizeof R);
    cv_mq_elem(9, 4, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SJAC], r3 / 2, 1e-12);
    CHECK_NEAR(q[CV_MQ_SKEW], 1.0 / 3, 1e-9);
    CHECK_NEAR(q[CV_MQ_ANGLE_MAX], 120, 1e-9);
    CHECK_NEAR(q[CV_MQ_SIZE], r3 / 2, 1e-12);
    memcpy(x, Q, sizeof Q);
    x[2][2] = 0.2;
    cv_mq_elem(9, 4, (const double (*)[3])x, q);
    CHECK(q[CV_MQ_WARP] > 5 && q[CV_MQ_WARP] < 20);
    q_natural(10, 8, 1, x);
    cv_mq_elem(10, 8, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SIZE], 4, 1e-12);
    CHECK_NEAR(q[CV_MQ_JRATIO], 1, 1e-12);
    /* a bow tie: no normal, no shape */
    memcpy(x, Q, sizeof Q);
    memcpy(x[2], Q[3], sizeof x[2]); memcpy(x[3], Q[2], sizeof x[3]);
    cv_mq_elem(9, 4, (const double (*)[3])x, q);
    CHECK(q[CV_MQ_SJAC] <= 1e-12);

    /* triangles: equilateral, right isosceles, tri6 */
    const double E[3][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0.5, r3 / 2, 0 } };
    memcpy(x, E, sizeof E);
    cv_mq_elem(7, 3, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SIZE], r3 / 4, 1e-12);
    CHECK_NEAR(q[CV_MQ_SJAC], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_SHAPE], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_SKEW], 0, 1e-9);
    q_natural(7, 3, 1, x);
    cv_mq_elem(7, 3, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_ANGLE_MIN], 45, 1e-9);
    CHECK_NEAR(q[CV_MQ_ANGLE_MAX], 90, 1e-9);
    CHECK_NEAR(q[CV_MQ_SKEW], 0.25, 1e-9);
    CHECK_NEAR(q[CV_MQ_SJAC], 2 / r3 * sqrt(0.5), 1e-12);
    CHECK_NEAR(q[CV_MQ_ASPECT], sqrt(2.0), 1e-12);
    q_natural(8, 6, 2, x);
    cv_mq_elem(8, 6, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SIZE], 2, 1e-12);
    CHECK_NEAR(q[CV_MQ_JRATIO], 1, 1e-12);

    /* beams: straight and bent three-node */
    const double L[3][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } };
    memcpy(x, L, sizeof L);
    cv_mq_elem(12, 3, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SIZE], 2, 1e-12);
    CHECK_NEAR(q[CV_MQ_EDGE_MAX], 2, 1e-12);
    CHECK(q[CV_MQ_SJAC] != q[CV_MQ_SJAC]);
    cv_mq_elem(11, 2, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SIZE], 1, 1e-12);
    x[1][1] = 0.5;
    cv_mq_elem(12, 3, (const double (*)[3])x, q);
    CHECK(q[CV_MQ_SIZE] > 2.2 && q[CV_MQ_SIZE] < 2.4);   /* a parabola, length 2.296 */

    /* overall scores: ideal elements score ideal in all four */
    {
        const int ts[] = { 1, 3, 2, 9, 7 };
        for (int i = 0; i < 5; i++) {
            int t = ts[i], nn = t == 1 ? 8 : t == 3 ? 4 : t == 2 ? 6 : t == 9 ? 4 : 3;
            if (t == 1) q_natural(1, 8, 1, x);
            else memcpy(x, t == 3 ? T : t == 2 ? W : t == 9 ? Q : E, sizeof(double) * 3 * nn);
            cv_mq_elem(t, nn, (const double (*)[3])x, q);
            CHECK_NEAR(q[CV_MQ_CCX], 1, 1e-9);
            CHECK_NEAR(q[CV_MQ_HMQI], 0, 1e-9);
            CHECK_NEAR(q[CV_MQ_ANSYS], 1, 1e-6);
            CHECK_NEAR(q[CV_MQ_ABAQUS], 0, 0);
        }
    }
    CHECK_NEAR(cv_mq_penalty(CV_MQ_ASPECT, 1, 2), 0, 1e-12);       /* good */
    CHECK_NEAR(cv_mq_penalty(CV_MQ_ASPECT, 1, 8.4), 0.8, 1e-12);   /* warn: 80 % to fail */
    CHECK_NEAR(cv_mq_penalty(CV_MQ_ASPECT, 1, 10), 1, 1e-12);      /* fail */
    CHECK_NEAR(cv_mq_penalty(CV_MQ_ASPECT, 1, 15), 5.5, 1e-12);
    CHECK_NEAR(cv_mq_penalty(CV_MQ_ASPECT, 1, 50), 10, 1e-12);     /* past worst */
    CHECK_NEAR(cv_mq_penalty(CV_MQ_ANGLE_MIN, 1, 27.5), 0.5, 1e-12); /* smaller is worse */
    CHECK_NEAR(cv_mq_score(CV_MQ_ASPECT, 1, sqrt(10.0)), 0.5, 1e-12);
    CHECK(cv_mq_score(CV_MQ_ANGLE_MIN, 1, 45) != cv_mq_score(CV_MQ_ANGLE_MIN, 1, 45));
    /* stretched 24 x 2 x 2: past the aspect limit in every score */
    q_natural(1, 8, 1, x);
    for (int i = 0; i < 8; i++) x[i][0] *= 12;
    cv_mq_elem(1, 8, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_CCX], 0, 1e-12);
    CHECK(cv_mq_poor(CV_MQ_CCX, 1, q[CV_MQ_CCX]));
    CHECK_NEAR(q[CV_MQ_HMQI], 2.8, 1e-9);
    CHECK_NEAR(q[CV_MQ_ANSYS], 41.56921938 * 96 / sqrt(2336.0 * 2336 * 2336), 1e-9);
    CHECK_NEAR(q[CV_MQ_ABAQUS], 1, 0);
    /* sheared by one edge: the scaled Jacobian governs, HyperMesh still all good */
    q_natural(1, 8, 1, x);
    for (int i = 4; i < 8; i++) x[i][0] += 2;
    cv_mq_elem(1, 8, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_CCX], sqrt(2.0) - 1, 1e-9);
    CHECK_EQ(cv_mq_governing(1, q), CV_MQ_SJAC);
    CHECK(!cv_mq_poor(CV_MQ_CCX, 1, q[CV_MQ_CCX]));
    CHECK_NEAR(q[CV_MQ_HMQI], 0, 1e-9);
    CHECK_NEAR(q[CV_MQ_ABAQUS], 0, 0);
    /* inside out: the worst of everything */
    memcpy(x, T, sizeof T);
    memcpy(x[1], T[2], sizeof x[1]); memcpy(x[2], T[1], sizeof x[2]);
    cv_mq_elem(3, 4, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_CCX], 0, 0);
    CHECK(q[CV_MQ_HMQI] >= 10);
    CHECK_NEAR(q[CV_MQ_ANSYS], 0, 0);
    CHECK(q[CV_MQ_ABAQUS] >= 1);
    /* beams: no score */
    memcpy(x, L, sizeof L);
    cv_mq_elem(12, 3, (const double (*)[3])x, q);
    for (int k = CV_MQ_SCORE0; k < CV_MQ_N; k++) CHECK(q[k] != q[k]);

    /* unknown type, too few nodes */
    cv_mq_elem(99, 4, (const double (*)[3])x, q);
    CHECK(q[CV_MQ_SIZE] != q[CV_MQ_SIZE]);
    cv_mq_elem(1, 4, (const double (*)[3])x, q);
    CHECK(q[CV_MQ_ASPECT] != q[CV_MQ_ASPECT]);
}
