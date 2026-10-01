/* t_mesh.h -- feature edges of the skin (the part outline). Included by test_main.c. */
#ifndef CV_T_MESH_H
#define CV_T_MESH_H

/* A mesh held in fixed arrays, enough for cv_skin_build. */
typedef struct {
    cv_frd   f;
    float    xyz[3 * 64];
    uint8_t  etype[16];
    uint32_t emat[16], eoff[17], conn[16 * 8];
} tm_mesh;

static void tm_init(tm_mesh* m) {
    memset(m, 0, sizeof *m);
    m->f.xyz = m->xyz; m->f.etype = m->etype; m->f.emat = m->emat;
    m->f.eoff = m->eoff; m->f.conn = m->conn;
}

static uint32_t tm_node(tm_mesh* m, float x, float y, float z) {
    for (uint32_t i = 0; i < m->f.n_nodes; i++)
        if (m->xyz[3 * i] == x && m->xyz[3 * i + 1] == y && m->xyz[3 * i + 2] == z) return i;
    uint32_t i = m->f.n_nodes++;
    m->xyz[3 * i] = x; m->xyz[3 * i + 1] = y; m->xyz[3 * i + 2] = z;
    return i;
}

static void tm_elem(tm_mesh* m, int type, uint32_t mat, const uint32_t* v, int n) {
    uint32_t e = m->f.n_elems++;
    m->etype[e] = (uint8_t)type; m->emat[e] = mat;
    for (int i = 0; i < n; i++) m->conn[m->eoff[e] + i] = v[i];
    m->eoff[e + 1] = m->eoff[e] + (uint32_t)n;
}

/* unit C3D8 with its low corner at (x, y, z) */
static void tm_brick(tm_mesh* m, float x, float y, float z, uint32_t mat) {
    static const int c[8][3] = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}, {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1} };
    uint32_t v[8];
    for (int i = 0; i < 8; i++) v[i] = tm_node(m, x + c[i][0], y + c[i][1], z + c[i][2]);
    tm_elem(m, 1, mat, v, 8);
}

static bool tm_has_fedge(const tm_mesh* m, const cv_skin* s, const float* p, const float* q) {
    uint32_t a = UINT32_MAX, b = UINT32_MAX;
    for (uint32_t i = 0; i < m->f.n_nodes; i++) {
        if (!memcmp(m->xyz + 3 * i, p, 3 * sizeof *p)) a = i;
        if (!memcmp(m->xyz + 3 * i, q, 3 * sizeof *q)) b = i;
    }
    for (size_t i = 0; i < s->n_fedge; i++)
        if ((s->fedge[2 * i] == a && s->fedge[2 * i + 1] == b) || (s->fedge[2 * i] == b && s->fedge[2 * i + 1] == a))
            return true;
    return false;
}

static void test_feature_edges(void) {
    static tm_mesh m;
    cv_skin s;

    /* two bricks in a row: the box's 12 edges (16 segments, the four along x
       cut by the middle nodes), not the lines between the bricks */
    tm_init(&m);
    tm_brick(&m, 0, 0, 0, 1); tm_brick(&m, 1, 0, 0, 1);
    CHECK(cv_skin_build(&s, &m.f, NULL));
    CHECK_EQ(s.n_edge, 20);
    CHECK_EQ(s.n_fedge, 16);
    {
        const float a[3] = { 1, 0, 0 }, b[3] = { 1, 0, 1 }, c[3] = { 0, 0, 0 }, d[3] = { 2, 0, 0 };
        CHECK(!tm_has_fedge(&m, &s, a, b));
        CHECK(tm_has_fedge(&m, &s, c, a) && tm_has_fedge(&m, &s, a, d));
    }
    cv_skin_free(&s);

    /* ... with two materials: the four lines between them join the outline */
    m.emat[1] = 2;
    CHECK(cv_skin_build(&s, &m.f, NULL));
    CHECK_EQ(s.n_fedge, 20);
    {
        const float a[3] = { 1, 0, 0 }, b[3] = { 1, 0, 1 }, c[3] = { 1, 1, 1 };
        CHECK(tm_has_fedge(&m, &s, a, b) && tm_has_fedge(&m, &s, b, c));
    }
    cv_skin_free(&s);

    /* an L of three bricks (two cannot share a whole face and still turn a
       corner): the concave corner is a crease like the convex ones, the lines
       across the flat sides are not. 8 profile segments per side + 6 along y. */
    tm_init(&m);
    tm_brick(&m, 0, 0, 0, 1); tm_brick(&m, 1, 0, 0, 1); tm_brick(&m, 0, 0, 1, 1);
    CHECK(cv_skin_build(&s, &m.f, NULL));
    CHECK_EQ(s.n_fedge, 22);
    {
        const float a[3] = { 1, 0, 1 }, b[3] = { 1, 1, 1 }, c[3] = { 1, 0, 0 }, d[3] = { 1, 1, 0 };
        CHECK(tm_has_fedge(&m, &s, a, b));       /* the inner corner */
        CHECK(!tm_has_fedge(&m, &s, c, d));      /* across the flat bottom */
    }
    cv_skin_free(&s);
    /* a crease angle above 90 degrees leaves the closed block without outline */
    CHECK(cv_skin_build_crease(&s, &m.f, NULL, 100.f));
    CHECK_EQ(s.n_fedge, 0);
    cv_skin_free(&s);

    /* two bricks touching along one edge only: four faces on it, outline */
    tm_init(&m);
    tm_brick(&m, 0, 0, 0, 1); tm_brick(&m, 1, 0, 1, 1);
    CHECK(cv_skin_build_crease(&s, &m.f, NULL, 100.f));
    CHECK_EQ(s.n_fedge, 1);
    cv_skin_free(&s);

    /* a flat 2x2 S4 sheet, one shell numbered the other way round: the 8 border
       edges, nothing inside */
    tm_init(&m);
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            uint32_t v[4] = { tm_node(&m, (float)i, (float)j, 0), tm_node(&m, (float)i + 1, (float)j, 0),
                              tm_node(&m, (float)i + 1, (float)j + 1, 0), tm_node(&m, (float)i, (float)j + 1, 0) };
            if (i == 1 && j == 1) { uint32_t t = v[1]; v[1] = v[3]; v[3] = t; }
            tm_elem(&m, 9, 1, v, 4);
        }
    CHECK(cv_skin_build(&s, &m.f, NULL));
    CHECK_EQ(s.n_edge, 12);
    CHECK_EQ(s.n_fedge, 8);
    {
        const float a[3] = { 1, 1, 0 }, b[3] = { 2, 1, 0 }, c[3] = { 2, 2, 0 };
        CHECK(!tm_has_fedge(&m, &s, a, b));
        CHECK(tm_has_fedge(&m, &s, b, c));
    }
    cv_skin_free(&s);

    /* a beam is outline whatever its neighbours */
    {
        uint32_t v[2] = { tm_node(&m, 2, 2, 0), tm_node(&m, 3, 3, 0) };
        tm_elem(&m, 11, 1, v, 2);
    }
    CHECK(cv_skin_build(&s, &m.f, NULL));
    CHECK_EQ(s.n_fedge, 9);
    cv_skin_free(&s);
}

#endif
