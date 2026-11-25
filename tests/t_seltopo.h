/* t_seltopo.h -- selection by the mesh's shape (seltopo.c). Included by test_main.c
   after t_selset.h (its grid of hexes). */
#include "../src/seltopo.h"

static void test_seltopo(void) {
    sg_grid g;
    sg_make(&g, 3, 1, 1);                              /* a row of three hexes */
    const cv_frd* f = &g.f;
    uint32_t n = 0, *r;
    const uint32_t e0[] = { 0 }, e01[] = { 0, 1 }, e012[] = { 0, 1, 2 };

    /* ---- grow and shrink ---- */
    r = cv_sel_grow_elems(f, NULL, e0, 1, &n);
    CHECK(sg_is(r, n, e01, 2)); free(r);
    r = cv_sel_shrink_elems(f, NULL, e01, 2, &n);     /* 1 touches 2, which is not selected */
    CHECK(sg_is(r, n, e0, 1)); free(r);
    r = cv_sel_shrink_elems(f, NULL, e012, 3, &n);    /* nothing outside: all stay */
    CHECK(sg_is(r, n, e012, 3)); free(r);
    uint8_t vis[3] = { 1, 1, 0 };
    r = cv_sel_shrink_elems(f, vis, e01, 2, &n);      /* a hidden neighbour does not count */
    CHECK(sg_is(r, n, e01, 2)); free(r);
    const uint32_t n0[] = { 0 };
    r = cv_sel_grow_nodes(f, NULL, n0, 1, &n);         /* node 0: the nodes of element 0 */
    CHECK(n == 8); free(r);
    uint32_t nn0 = 0, *nd0 = cv_sel_elem_nodes(f, e0, 1, &nn0);
    r = cv_sel_shrink_nodes(f, NULL, nd0, nn0, &n);    /* element 1's nodes go: x = 0 stays */
    { const uint32_t w[] = { 0, 4, 8, 12 }; CHECK(sg_is(r, n, w, 4)); }
    free(r); free(nd0);

    /* ---- the part ---- */
    r = cv_sel_part(f, NULL, 0, &n);
    CHECK(sg_is(r, n, e012, 3)); free(r);
    uint8_t vis1[3] = { 1, 0, 1 };
    r = cv_sel_part(f, vis1, 0, &n);                   /* the middle one hidden: two parts */
    CHECK(sg_is(r, n, e0, 1)); free(r);
    sg_free(&g);

    /* ---- the boundary of a 3 x 3 x 3 block: all but the middle node set and element ---- */
    sg_make(&g, 3, 3, 3);
    f = &g.f;
    uint32_t all[27];
    for (uint32_t i = 0; i < 27; i++) all[i] = i;
    uint32_t *bn = NULL, *be = NULL, nbn = 0, nbe = 0;
    CHECK(cv_sel_boundary(f, all, 27, &bn, &nbn, &be, &nbe));
    CHECK_EQ(nbn, 64 - 8);
    CHECK_EQ(nbe, 26);
    for (uint32_t i = 0; i < nbe; i++) CHECK(be[i] != 13);   /* the middle element has no free face */
    free(bn); free(be);
    CHECK(cv_sel_boundary(f, e0, 1, &bn, &nbn, &be, &nbe));  /* one hex: its 8 nodes */
    CHECK(nbn == 8 && nbe == 1);
    free(bn); free(be);
    sg_free(&g);

    /* ---- skin faces up to the creases, the chain of feature edges ---- */
    sg_make(&g, 3, 1, 1);
    f = &g.f;
    cv_skin sk;
    CHECK(cv_skin_build_crease(&sk, f, NULL, 30.f));
    uint32_t top = UINT32_MAX;                         /* element 0's top face (z = 1): local face 1 */
    for (size_t k = 0; k < sk.n_face; k++) if (sk.face[k] == (0u << 3 | 1u)) top = (uint32_t)k;
    CHECK(top != UINT32_MAX);
    r = cv_sel_face_flood(f, &sk, top, 30.f, &n);       /* the three top faces, not the sides */
    CHECK_EQ(n, 3);
    for (uint32_t i = 0; i < n; i++) CHECK((sk.face[r[i]] & 7) == 1);
    free(r);
    r = cv_sel_face_flood(f, &sk, top, 95.f, &n);       /* a blunt crease: every outer face */
    CHECK_EQ(n, (uint32_t)sk.n_face);
    free(r);
    CHECK(cv_skin_face_of_tri(f, &sk, 0) != UINT32_MAX);
    size_t ke = sk.n_fedge;                            /* a feature edge along x at y = 0, z = 1 */
    for (size_t k = 0; k < sk.n_fedge; k++) {
        const float* a = f->xyz + 3 * sk.fedge[2 * k]; const float* b = f->xyz + 3 * sk.fedge[2 * k + 1];
        if (a[1] == 0 && b[1] == 0 && a[2] == 1 && b[2] == 1 && a[0] != b[0]) ke = k;
    }
    CHECK(ke < sk.n_fedge);
    r = cv_sel_edge_chain(f, &sk, ke, 30.f, &n);        /* corner to corner: x = 0 .. 3 */
    CHECK_EQ(n, 4);
    free(r);
    cv_skin_free(&sk);
    sg_free(&g);

    /* ---- the lasso's test ---- */
    const float sq[] = { 0, 0, 10, 0, 10, 10, 0, 10 }, vee[] = { 0, 0, 5, 8, 10, 0, 5, 10 };
    CHECK(cv_point_in_poly(5, 5, sq, 4));
    CHECK(!cv_point_in_poly(11, 5, sq, 4));
    CHECK(!cv_point_in_poly(5, 4, vee, 4));            /* in the notch */
    CHECK(cv_point_in_poly(2, 4, vee, 4));
}
