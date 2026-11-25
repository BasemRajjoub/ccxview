/* t_selset.h -- the selection's set algebra (selset.c). Included by test_main.c. */
#include "../src/selset.h"

/* nx * ny * nz unit hexes (C3D8) sharing their nodes; element (i, j, k) is
   i + nx * (j + ny * k), node (i, j, k) is i + (nx + 1) * (j + (ny + 1) * k) */
typedef struct { cv_frd f; int nx, ny, nz; } sg_grid;

static void sg_make(sg_grid* g, int nx, int ny, int nz) {
    memset(g, 0, sizeof *g);
    g->nx = nx; g->ny = ny; g->nz = nz;
    uint32_t N = (uint32_t)((nx + 1) * (ny + 1) * (nz + 1)), E = (uint32_t)(nx * ny * nz);
    cv_frd* f = &g->f;
    f->n_nodes = N; f->n_elems = E;
    f->xyz = calloc(3 * N, sizeof(float));
    f->node_id = calloc(N, sizeof(uint32_t));
    f->elem_id = calloc(E, sizeof(uint32_t));
    f->etype = calloc(E, 1);
    f->emat = calloc(E, sizeof(uint32_t));
    f->eoff = calloc(E + 1, sizeof(uint32_t));
    f->conn = calloc(8 * E, sizeof(uint32_t));
    for (int k = 0; k <= nz; k++) for (int j = 0; j <= ny; j++) for (int i = 0; i <= nx; i++) {
        uint32_t n = (uint32_t)(i + (nx + 1) * (j + (ny + 1) * k));
        f->xyz[3 * n] = (float)i; f->xyz[3 * n + 1] = (float)j; f->xyz[3 * n + 2] = (float)k;
        f->node_id[n] = n + 1;
    }
#define SG_N(i, j, k) (uint32_t)((i) + (nx + 1) * ((j) + (ny + 1) * (k)))
    for (int k = 0; k < nz; k++) for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++) {
        uint32_t e = (uint32_t)(i + nx * (j + ny * k)), *c = f->conn + 8 * e;
        c[0] = SG_N(i, j, k); c[1] = SG_N(i + 1, j, k); c[2] = SG_N(i + 1, j + 1, k); c[3] = SG_N(i, j + 1, k);
        c[4] = SG_N(i, j, k + 1); c[5] = SG_N(i + 1, j, k + 1); c[6] = SG_N(i + 1, j + 1, k + 1); c[7] = SG_N(i, j + 1, k + 1);
        f->etype[e] = 1; f->elem_id[e] = e + 1; f->emat[e] = 1; f->eoff[e + 1] = 8 * (e + 1);
    }
#undef SG_N
}

static void sg_free(sg_grid* g) {
    cv_frd* f = &g->f;
    free(f->xyz); free(f->node_id); free(f->elem_id); free(f->etype); free(f->emat); free(f->eoff); free(f->conn);
}

static bool sg_is(const uint32_t* a, uint32_t n, const uint32_t* want, uint32_t nw) {
    if (n != nw) return false;
    for (uint32_t i = 0; i < n; i++) if (a[i] != want[i]) return false;
    return true;
}

static void test_selset(void) {
    /* ---- combine: the four modes, out of range dropped, duplicates once ---- */
    const uint32_t a[] = { 1, 3, 5, 7 }, b[] = { 3, 4, 5, 5, 99 };
    uint32_t* r = NULL; uint32_t n = 0;
    CHECK(cv_sel_combine(CV_SEL_NEW, a, 4, b, 5, 10, &r, &n));
    { const uint32_t w[] = { 3, 4, 5 }; CHECK(sg_is(r, n, w, 3)); }
    free(r);
    CHECK(cv_sel_combine(CV_SEL_ADD, a, 4, b, 5, 10, &r, &n));
    { const uint32_t w[] = { 1, 3, 4, 5, 7 }; CHECK(sg_is(r, n, w, 5)); }
    free(r);
    CHECK(cv_sel_combine(CV_SEL_REMOVE, a, 4, b, 5, 10, &r, &n));
    { const uint32_t w[] = { 1, 7 }; CHECK(sg_is(r, n, w, 2)); }
    free(r);
    CHECK(cv_sel_combine(CV_SEL_AND, a, 4, b, 5, 10, &r, &n));
    { const uint32_t w[] = { 3, 5 }; CHECK(sg_is(r, n, w, 2)); }
    free(r);
    CHECK(cv_sel_combine(CV_SEL_AND, a, 4, NULL, 0, 10, &r, &n));   /* with nothing: nothing */
    CHECK(n == 0 && r == NULL);
    CHECK(cv_sel_combine(CV_SEL_ADD, NULL, 0, NULL, 0, 0, &r, &n));
    CHECK(n == 0);

    /* ---- mask round trip ---- */
    uint8_t* m = cv_sel_mask(b, 5, 10);
    CHECK(m && m[3] && m[4] && m[5] && !m[0] && !m[9]);
    r = cv_sel_from_mask(m, 10, &n);
    { const uint32_t w[] = { 3, 4, 5 }; CHECK(sg_is(r, n, w, 3)); }
    free(r); free(m);

    /* ---- a 3 x 1 x 1 row of hexes: 16 nodes ---- */
    sg_grid g;
    sg_make(&g, 3, 1, 1);
    const cv_frd* f = &g.f;
    const uint32_t e0[] = { 0 };
    r = cv_sel_elem_nodes(f, e0, 1, &n);              /* element 0: x = 0 and 1 */
    { const uint32_t w[] = { 0, 1, 4, 5, 8, 9, 12, 13 }; CHECK(sg_is(r, n, w, 8)); }
    uint32_t* back = NULL, nb = 0;
    back = cv_sel_node_elems(f, NULL, r, n, false, &nb);   /* every node in: element 0 only */
    CHECK(sg_is(back, nb, e0, 1));
    free(back);
    back = cv_sel_node_elems(f, NULL, r, n, true, &nb);    /* any node: 0 and its neighbour 1 */
    { const uint32_t w[] = { 0, 1 }; CHECK(sg_is(back, nb, w, 2)); }
    free(back);
    uint32_t* inv = cv_sel_invert_nodes(f, NULL, r, n, &nb);   /* x = 2, 3 */
    CHECK(nb == 8);
    free(inv); free(r);
    uint8_t vis[3] = { 1, 1, 0 };                          /* element 2 hidden */
    r = cv_sel_invert_elems(f, vis, e0, 1, &n);
    { const uint32_t w[] = { 1 }; CHECK(sg_is(r, n, w, 1)); }
    free(r);
    r = cv_sel_invert_nodes(f, vis, NULL, 0, &n);          /* every shown node: x = 0, 1, 2 */
    CHECK(n == 12);
    free(r);
    uint8_t* sn = cv_sel_shown_nodes(f, vis);
    CHECK(sn && sn[2] && !sn[3]);
    free(sn);
    const uint32_t all_nodes[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    r = cv_sel_node_elems(f, vis, all_nodes, 16, false, &n);   /* hidden elements stay out */
    { const uint32_t w[] = { 0, 1 }; CHECK(sg_is(r, n, w, 2)); }
    free(r);
    uint32_t fn[8];                                        /* a linear hex face: its four corners */
    CHECK_EQ(cv_elem_face_nodes(f, 0, 0, fn), 4);
    CHECK(fn[0] == 0 && fn[1] == 1 && fn[2] == 5 && fn[3] == 4);
    CHECK_EQ(cv_elem_nfaces(f, 0), 6);
    sg_free(&g);
}
