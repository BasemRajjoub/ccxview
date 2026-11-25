/* measure.c -- distance, angle and circle through points (measure.h). */
#include "measure.h"
#include <math.h>

static double dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross(const double a[3], const double b[3], double o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
static void sub(const double a[3], const double b[3], double o[3]) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }

void cv_measure_dist(const double a[3], const double b[3], double out[4]) {
    double d[3];
    sub(b, a, d);
    out[0] = sqrt(dot(d, d));
    out[1] = d[0]; out[2] = d[1]; out[3] = d[2];
}

/* atan2 of the cross and dot products: exact near 0 and 180 degrees, where acos is not */
double cv_measure_angle(const double a[3], const double b[3], const double c[3]) {
    double u[3], v[3], x[3];
    sub(a, b, u); sub(c, b, v);
    if (!(dot(u, u) > 0) || !(dot(v, v) > 0)) return NAN;
    cross(u, v, x);
    return atan2(sqrt(dot(x, x)), dot(u, v)) * (180.0 / 3.14159265358979323846);
}

/* the circumcentre: a + (|u|^2 (v x w) + |v|^2 (w x u)) / (2 |w|^2), u = b - a, v = c - a, w = u x v */
bool cv_measure_circle(const double a[3], const double b[3], const double c[3], double centre[3], double* r, double n[3]) {
    double u[3], v[3], w[3], p[3], q[3];
    sub(b, a, u); sub(c, a, v);
    cross(u, v, w);
    double ww = dot(w, w), uu = dot(u, u), vv = dot(v, v);
    if (!(uu > 0) || !(vv > 0) || !(ww > 1e-24 * uu * vv)) return false;   /* sin of the angle at a below 1e-12 */
    cross(v, w, p); cross(w, u, q);
    double o[3];
    for (int k = 0; k < 3; k++) o[k] = (uu * p[k] + vv * q[k]) / (2 * ww);
    double l = sqrt(ww);
    for (int k = 0; k < 3; k++) { centre[k] = a[k] + o[k]; n[k] = w[k] / l; }
    *r = sqrt(dot(o, o));
    return true;
}

bool cv_measure_weights(const double a[3], const double b[3], const double c[3], const double p[3], double w[3]) {
    double u[3], v[3], d[3];
    sub(b, a, u); sub(c, a, v); sub(p, a, d);
    double uu = dot(u, u), uv = dot(u, v), vv = dot(v, v), du = dot(d, u), dv = dot(d, v);
    double det = uu * vv - uv * uv;
    if (!(det > 1e-24 * uu * vv)) return false;
    double s = (du * vv - dv * uv) / det, t = (dv * uu - du * uv) / det;
    w[0] = 1 - s - t; w[1] = s; w[2] = t;
    return true;
}
