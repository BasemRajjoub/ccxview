/* t_selfilter.h -- tests on a selection's entries (selfilter.c). Included by
   test_main.c after t_selset.h (its grid of hexes). */
#include "../src/selfilter.h"

static void test_selfilter(void) {
    sg_grid g;
    sg_make(&g, 4, 1, 1);                              /* four hexes along x: centroids x = 0.5 .. 3.5 */
    cv_frd* f = &g.f;
    uint32_t el[4], n;
    cv_selfilter q;
#define SF_ALL() for (uint32_t i = 0; i < 4; i++) el[i] = i

    /* ---- parsing ---- */
    CHECK(cv_selfilter_parse("x>1", &q) && q.coord == CV_SC_X && q.lo == 1 && isinf(q.hi));
    CHECK(cv_selfilter_parse(" 1 < y < 2 ", &q) && q.coord == CV_SC_Y && q.lo == 1 && q.hi == 2);
    CHECK(cv_selfilter_parse("theta<-45", &q) && q.coord == CV_SC_THETA && q.hi == -45);
    CHECK(cv_selfilter_parse("r>2.5", &q) && q.coord == CV_SC_R && q.lo == 2.5f);
    CHECK(!cv_selfilter_parse("q>1", &q));
    CHECK(!cv_selfilter_parse("x=1", &q));
    CHECK(!cv_selfilter_parse("x>1 junk", &q));

    /* ---- coordinates: elements by their centre, nodes where they are ---- */
    memset(&q, 0, sizeof q);
    CHECK(cv_selfilter_parse("1<x<3", &q));
    SF_ALL();
    n = cv_selfilter_elems(f, &q, NULL, NULL, el, 4);
    CHECK(n == 2 && el[0] == 1 && el[1] == 2);
    uint32_t nd[20];
    for (uint32_t i = 0; i < 20; i++) nd[i] = i;
    CHECK_EQ(cv_selfilter_nodes(f, &q, NULL, nd, 20), 12);   /* x = 1, 2, 3: four nodes each */
    /* about the Z axis through (2, 0.5, 0): r of element centres 1.5, 0.5, 0.5, 1.5 */
    q.coord = CV_SC_R; q.axis = 2; q.at[0] = 2; q.at[1] = 0.5f; q.at[2] = 0; q.lo = 0; q.hi = 1;
    SF_ALL();
    n = cv_selfilter_elems(f, &q, NULL, NULL, el, 4);
    CHECK(n == 2 && el[0] == 1 && el[1] == 2);
    const float p[3] = { 2, 1.5f, 7 };
    q.coord = CV_SC_THETA; CHECK_NEAR(cv_selfilter_coord(&q, p), 90, 1e-4);
    q.coord = CV_SC_AXIAL; CHECK_NEAR(cv_selfilter_coord(&q, p), 7, 1e-6);
    q.axis = 0; q.coord = CV_SC_THETA;                  /* about X: theta from Y toward Z */
    CHECK_NEAR(cv_selfilter_coord(&q, p), atan2(7, 1) * 180 / 3.14159265358979, 1e-3);

    /* ---- the field: a node's value is its x; an element by its highest node ---- */
    float nv[20], ev[4] = { 5, 1, 7, 3 };
    for (uint32_t i = 0; i < 20; i++) nv[i] = f->xyz[3 * i];
    memset(&q, 0, sizeof q);
    q.kind = CV_SQ_ABOVE; q.value = 2.5f;
    SF_ALL();
    n = cv_selfilter_elems(f, &q, nv, NULL, el, 4);     /* reaching x = 3 or 4: elements 2, 3 */
    CHECK(n == 2 && el[0] == 2);
    q.kind = CV_SQ_BELOW; q.value = 1.5f;
    SF_ALL();
    n = cv_selfilter_elems(f, &q, nv, NULL, el, 4);     /* down to x = 0 or 1: elements 0, 1 */
    CHECK(n == 2 && el[1] == 1);
    q.kind = CV_SQ_TOP; q.value = 50;
    SF_ALL();
    n = cv_selfilter_elems(f, &q, nv, ev, el, 4);       /* per element: the top half of 5 1 7 3 */
    CHECK(n == 2 && el[0] == 0 && el[1] == 2);
    for (uint32_t i = 0; i < 20; i++) nd[i] = i;
    q.value = 20;                                       /* nodes: the top 20 % of 20: the 4 at x = 4 */
    CHECK_EQ(cv_selfilter_nodes(f, &q, nv, nd, 20), 4);
    CHECK_EQ(cv_selfilter_nodes(f, &q, NULL, nd, 4), 0);   /* no field: nothing */

    /* ---- type and material ---- */
    f->emat[3] = 2;
    q.kind = CV_SQ_MAT; q.code = 2;
    SF_ALL();
    n = cv_selfilter_elems(f, &q, NULL, NULL, el, 4);
    CHECK(n == 1 && el[0] == 3);
    q.kind = CV_SQ_TYPE; q.code = 1;
    SF_ALL();
    CHECK_EQ(cv_selfilter_elems(f, &q, NULL, NULL, el, 4), 4);
#undef SF_ALL
    sg_free(&g);
}
