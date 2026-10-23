/* t_selstats.h -- the selection's totals per kind (selstats.c). Included by
   test_main.c after t_selset.h (its grid of hexes). */
#include "../src/selstats.h"

static void test_selstats(void) {
    sg_grid g;
    sg_make(&g, 2, 1, 1);                              /* two unit hexes along x */
    const cv_frd* f = &g.f;
    float* x = malloc(f->n_nodes * sizeof *x);         /* the field: x */
    for (uint32_t i = 0; i < f->n_nodes; i++) x[i] = f->xyz[3 * i];
    cv_selstat o;
    const uint32_t nd[] = { 0, 1, 2 };
    cv_selstat_nodes(x, nd, 3, &o);
    CHECK(o.n == 3 && o.nval == 3 && o.size == 0 && o.vmin == 0.f && o.vmax == 2.f && fabs(o.avg - 1) < 1e-9);
    cv_selstat_nodes(NULL, nd, 3, &o);
    CHECK(o.n == 3 && o.nval == 0 && o.avg != o.avg);  /* no field: NaN */

    const uint32_t el[] = { 0, 1 };
    const float ev[] = { 1.f, 5.f };
    cv_selstat_elems(f, x, NULL, el, 2, &o);           /* each the mean of its nodes: 0.5, 1.5 */
    CHECK(o.n == 2 && fabs(o.size - 2) < 1e-9 && o.vmin == 0.5f && o.vmax == 1.5f && fabs(o.avg - 1) < 1e-9);
    cv_selstat_elems(f, x, ev, el, 2, &o);
    CHECK(fabs(o.avg - 3) < 1e-9 && o.vmax == 5.f);

    const uint32_t fc[] = { 0 << 3 | 0, 1 << 3 | 0, 0 << 3 | 5 };   /* S1 of both (z = 0), S6 of the first (x = 0) */
    cv_selstat_faces(f, x, NULL, fc, 3, &o);
    CHECK(o.n == 3 && fabs(o.size - 3) < 1e-6 && o.vmin == 0.f && o.vmax == 1.5f && fabs(o.avg - 2.0 / 3) < 1e-6);

    const uint32_t ed[] = { 0, 1, 0, 2 };              /* 1 long at 0.5, 2 long at 1 */
    cv_selstat_edges(f, x, ed, 2, &o);
    CHECK(o.n == 2 && fabs(o.size - 3) < 1e-6 && o.vmin == 0.5f && o.vmax == 1.f && fabs(o.avg - 2.5 / 3) < 1e-6);
    free(x);
    sg_free(&g);
}
