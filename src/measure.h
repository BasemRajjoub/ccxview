/* measure.h -- measurements between points: a distance with its components, the
   angle at a vertex, the circle through three points. Double throughout. Headless. */
#ifndef CV_MEASURE_H
#define CV_MEASURE_H

#include <stdbool.h>

/* the distance from a to b, and b - a: out = { d, dx, dy, dz } */
void cv_measure_dist(const double a[3], const double b[3], double out[4]);

/* the angle a-b-c at b, in degrees (0 .. 180); NaN when a or c sits on b */
double cv_measure_angle(const double a[3], const double b[3], const double c[3]);

/* the circle through a, b and c: its centre, radius and the unit normal of its plane
   (right-handed a -> b -> c). false when the three are collinear or two coincide:
   then nothing is written. */
bool cv_measure_circle(const double a[3], const double b[3], const double c[3], double centre[3], double* r, double n[3]);

/* the affine weights of p (projected on the plane of a, b, c) in that triangle:
   p ~ w0 a + w1 b + w2 c, w0 + w1 + w2 = 1. false for a degenerate triangle. */
bool cv_measure_weights(const double a[3], const double b[3], const double c[3], const double p[3], double w[3]);

#endif
