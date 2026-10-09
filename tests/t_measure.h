/* t_measure.h -- unit tests for src/measure.h (distance, angle, circle through points). */
#ifndef CV_T_MEASURE_H
#define CV_T_MEASURE_H

#include "../src/measure.h"

static void test_measure(void) {
    /* distance and its components, b - a */
    double d[4];
    cv_measure_dist((double[3]){ 1, 2, 3 }, (double[3]){ 4, 6, 3 }, d);
    CHECK_NEAR(d[0], 5, 1e-12); CHECK_NEAR(d[1], 3, 0); CHECK_NEAR(d[2], 4, 0); CHECK_NEAR(d[3], 0, 0);
    cv_measure_dist((double[3]){ 1, 1, 1 }, (double[3]){ 1, 1, 1 }, d);
    CHECK_NEAR(d[0], 0, 0);

    /* angles at b: right, straight, acute, the order of a and c does not matter */
    const double o[3] = { 0, 0, 0 }, x[3] = { 2, 0, 0 }, y[3] = { 0, 3, 0 };
    CHECK_NEAR(cv_measure_angle(x, o, y), 90, 1e-12);
    CHECK_NEAR(cv_measure_angle(y, o, x), 90, 1e-12);
    CHECK_NEAR(cv_measure_angle(x, o, (double[3]){ -5, 0, 0 }), 180, 1e-12);
    CHECK_NEAR(cv_measure_angle(x, o, (double[3]){ 7, 0, 0 }), 0, 1e-12);
    CHECK_NEAR(cv_measure_angle(x, o, (double[3]){ 1, 1, 0 }), 45, 1e-12);
    CHECK_NEAR(cv_measure_angle((double[3]){ 1, 0, 0 }, o, (double[3]){ 0.5, 0.8660254037844386, 0 }), 60, 1e-9);
    CHECK_NEAR(cv_measure_angle((double[3]){ 1, 0, 0 }, o, (double[3]){ 1, 1e-9, 0 }), 1e-9 * 180 / 3.14159265358979323846, 1e-15);   /* tiny: atan2 keeps it */
    double na = cv_measure_angle(o, o, x);
    CHECK(na != na);                                   /* a on b: no angle */

    /* the circle through three points of a known circle, tilted out of the axes */
    double c[3] = { 1, -2, 3 }, R = 5, e1[3] = { 0.6, 0.8, 0 }, e2[3] = { 0, 0, 1 };   /* orthonormal */
    double p[3][3];
    const double th[3] = { 0.3, 2.0, 4.1 };
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) p[i][k] = c[k] + R * (cos(th[i]) * e1[k] + sin(th[i]) * e2[k]);
    double cc[3], r, n[3];
    CHECK(cv_measure_circle(p[0], p[1], p[2], cc, &r, n));
    CHECK_NEAR(r, 5, 1e-12);
    for (int k = 0; k < 3; k++) CHECK_NEAR(cc[k], c[k], 1e-12);
    /* the normal: e1 x e2 = (0.8, -0.6, 0), right-handed with the points going round by +theta */
    CHECK_NEAR(n[0], 0.8, 1e-12); CHECK_NEAR(n[1], -0.6, 1e-12); CHECK_NEAR(n[2], 0, 1e-12);
    /* the other way round turns the normal */
    CHECK(cv_measure_circle(p[2], p[1], p[0], cc, &r, n));
    CHECK_NEAR(n[0], -0.8, 1e-12);
    /* three points close together on a big circle (a quarter degree of R = 1000) */
    for (int i = 0; i < 3; i++) {
        double t = i * 0.002;
        p[i][0] = 1000 * cos(t); p[i][1] = 1000 * sin(t); p[i][2] = 0;
    }
    CHECK(cv_measure_circle(p[0], p[1], p[2], cc, &r, n));
    CHECK_NEAR(r, 1000, 1e-6); CHECK_NEAR(cc[0], 0, 1e-6); CHECK_NEAR(cc[1], 0, 1e-6);
    /* collinear and coincident points: no circle, nothing written */
    cc[0] = 42; r = 42;
    CHECK(!cv_measure_circle(o, x, (double[3]){ 5, 0, 0 }, cc, &r, n));
    CHECK(!cv_measure_circle(o, o, x, cc, &r, n));
    CHECK(!cv_measure_circle(o, x, x, cc, &r, n));
    CHECK(cc[0] == 42 && r == 42);

    /* affine weights: at the corners, the centroid, a point off the plane projects */
    double w[3];
    const double A[3] = { 0, 0, 0 }, B[3] = { 4, 0, 0 }, C[3] = { 0, 2, 0 };
    CHECK(cv_measure_weights(A, B, C, B, w));
    CHECK_NEAR(w[0], 0, 1e-12); CHECK_NEAR(w[1], 1, 1e-12); CHECK_NEAR(w[2], 0, 1e-12);
    CHECK(cv_measure_weights(A, B, C, (double[3]){ 4.0 / 3, 2.0 / 3, 7 }, w));
    for (int k = 0; k < 3; k++) CHECK_NEAR(w[k], 1.0 / 3, 1e-12);
    CHECK(cv_measure_weights(A, B, C, (double[3]){ 4, 2, 0 }, w));      /* outside: a negative weight */
    CHECK_NEAR(w[0], -1, 1e-12); CHECK_NEAR(w[1], 1, 1e-12); CHECK_NEAR(w[2], 1, 1e-12);
    CHECK(!cv_measure_weights(A, B, (double[3]){ 8, 0, 0 }, A, w));
}

#endif
