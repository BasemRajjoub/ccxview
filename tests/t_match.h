/* t_match.h -- unit tests for cv_frd_match_elems (deck elements against a .frd
   whose solver numbered them differently). */
#ifndef CV_T_MATCH_H
#define CV_T_MATCH_H

/* n Quad4 in a strip: nodes 1..2n+2, element k on nodes (k,k+1) below and above,
   numbered from id0; `rot` turns each element's node list by that many places */
static void match_strip(cv_frd* f, uint32_t n, uint32_t id0, int rot) {
    memset(f, 0, sizeof *f);
    f->n_nodes = 2 * n + 2;
    f->node_id = malloc(f->n_nodes * sizeof(uint32_t));
    f->xyz = calloc(3 * (size_t)f->n_nodes, sizeof(float));
    for (uint32_t i = 0; i < f->n_nodes; i++) f->node_id[i] = i + 1;
    f->n_elems = n;
    f->elem_id = malloc(n * sizeof(uint32_t));
    f->etype = malloc(n);
    f->eoff = malloc((n + 1) * sizeof(uint32_t));
    f->conn = malloc(4 * (size_t)n * sizeof(uint32_t));
    for (uint32_t k = 0; k < n; k++) {
        uint32_t q[4] = { k, k + 1, n + 2 + k, n + 1 + k };
        f->elem_id[k] = id0 + k; f->etype[k] = 9; f->eoff[k] = 4 * k;
        for (int j = 0; j < 4; j++) f->conn[4 * k + j] = q[(j + rot) % 4];
    }
    f->eoff[n] = 4 * n;
    CHECK(cv_frd_build_maps(f, NULL, NULL));
}

static void test_match(void) {
    cv_frd deck, frd;
    cv_elem_match m;
    match_strip(&deck, 5, 1, 0);

    match_strip(&frd, 5, 1, 0);                        /* same ids: identity */
    uint32_t* map = cv_frd_match_elems(&deck, &frd, &m);
    CHECK(map);
    for (uint32_t e = 0; map && e < 5; e++) CHECK_EQ(map[e], e);
    CHECK_EQ(m.by_id, 5); CHECK_EQ(m.by_nodes, 0); CHECK(!m.shifted);
    free(map); cv_frd_free(&frd);

    match_strip(&frd, 5, 0, 1);                        /* FEMaster: from 0, node order turned */
    map = cv_frd_match_elems(&deck, &frd, &m);
    for (uint32_t e = 0; map && e < 5; e++) CHECK_EQ(map[e], e);
    CHECK_EQ(m.by_nodes, 5); CHECK(m.shifted); CHECK_EQ(m.offset, -1); CHECK_EQ(m.none, 0);
    free(map);

    frd.conn[4 * 2] = frd.conn[4 * 2 + 1];             /* element 2 on other nodes: absent */
    frd.conn[4 * 2 + 1] = 0;
    map = cv_frd_match_elems(&deck, &frd, &m);
    CHECK(map && map[2] == UINT32_MAX && map[3] == 3 && map[4] == 4);
    CHECK_EQ(m.none, 1); CHECK_EQ(m.by_nodes, 4);
    free(map); cv_frd_free(&frd);

    match_strip(&frd, 5, 1, 0);                        /* one element differs, ids trusted: kept */
    frd.conn[4 * 2] = 0;
    map = cv_frd_match_elems(&deck, &frd, &m);
    CHECK(map && map[2] == 2);
    CHECK_EQ(m.by_id, 5); CHECK_EQ(m.by_nodes, 0);
    free(map); cv_frd_free(&frd);

    match_strip(&frd, 5, 101, 2);                      /* any other offset */
    map = cv_frd_match_elems(&deck, &frd, &m);
    for (uint32_t e = 0; map && e < 5; e++) CHECK_EQ(map[e], e);
    CHECK(m.shifted); CHECK_EQ(m.offset, 100);
    free(map); cv_frd_free(&frd);

    cv_frd_free(&deck);
}

#endif
