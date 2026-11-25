/* t_pickinfo.h -- a picked face's ring, area and normal; an element's edges
   (pickinfo.c). Included by test_main.c after t_selset.h (its grid of hexes). */
#include "../src/pickinfo.h"

static void test_pickinfo(void) {
    sg_grid g;
    sg_make(&g, 2, 1, 1);                              /* two unit hexes along x */
    const cv_frd* f = &g.f;
    uint32_t r[8];
    int n = cv_face_ring(f, 0, 0, r);                  /* S1: the bottom, z = 0 */
    CHECK_EQ(n, 4);
    float p[24], nrm[3];
    for (int i = 0; i < n; i++) for (int k = 0; k < 3; k++) p[3 * i + k] = f->xyz[3 * r[i] + k];
    CHECK(fabsf(cv_poly_area(p, n, nrm) - 1.f) < 1e-6f);
    CHECK(fabsf(fabsf(nrm[2]) - 1.f) < 1e-6f && fabsf(nrm[0]) < 1e-6f);
    for (int i = 0; i < n; i++) CHECK(f->xyz[3 * r[i] + 2] == 0.f);
    const float tri[9] = { 0, 0, 0, 2, 0, 0, 0, 3, 0 };   /* 2 x 3 / 2, counter-clockwise from +z */
    CHECK(fabsf(cv_poly_area(tri, 3, nrm) - 3.f) < 1e-6f && nrm[2] > 0.99f);
    CHECK_EQ(cv_face_ring(f, 0, 9, r), 0);             /* no such face */

    uint32_t ed[32][3];
    int m = cv_elem_edges(f, 0, ed, 32);
    CHECK_EQ(m, 12);                                   /* a hex: twelve, once each */
    for (int i = 0; i < m; i++) {
        float l = 0;
        for (int k = 0; k < 3; k++) { float d = f->xyz[3 * ed[i][0] + k] - f->xyz[3 * ed[i][1] + k]; l += d * d; }
        CHECK(fabsf(l - 1.f) < 1e-6f && ed[i][2] == UINT32_MAX);   /* unit long, no mid-side node */
    }
    CHECK_EQ(cv_elem_edges(f, 0, ed, 5), 5);           /* no more than asked for */
    sg_free(&g);
}
