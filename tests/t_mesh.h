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

/* ---- quadratic faces through their mid-side nodes ---------------------------------- */

static double tm_area(const cv_frd* f, const cv_skin* s) {
    double a = 0;
    for (size_t t = 0; t < s->n_tri; t++) {
        const float *p = f->xyz + 3 * s->tri[3 * t], *q = f->xyz + 3 * s->tri[3 * t + 1], *r = f->xyz + 3 * s->tri[3 * t + 2];
        double u[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] }, v[3] = { r[0] - p[0], r[1] - p[1], r[2] - p[2] };
        double c[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
        a += 0.5 * sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    }
    return a;
}

/* the skin of f with and without mid-side nodes covers the same area (straight-sided
   elements), with `per` times the triangles; every triangle has an area */
static void tm_same_skin(const cv_frd* f, double per) {
    cv_skin a, b;
    CHECK(cv_skin_build_opt(&a, f, NULL, CV_CREASE_DEG, false));
    CHECK(cv_skin_build_opt(&b, f, NULL, CV_CREASE_DEG, true));
    double aa = tm_area(f, &a), ab = tm_area(f, &b);
    CHECK(aa > 0);
    CHECK_NEAR(ab / aa, 1.0, 1e-5);
    if (per > 0) CHECK_NEAR((double)b.n_tri / (double)a.n_tri, per, 1e-9);
    CHECK_EQ(a.n_face, b.n_face);
    cv_skin one = b;
    for (size_t t = 0; t < b.n_tri; t++) {
        one.tri = b.tri + 3 * t; one.n_tri = 1;
        CHECK(tm_area(f, &one) > 1e-9 * aa);
    }
    cv_skin_free(&a); cv_skin_free(&b);
}

static void test_mid_faces(void) {
    /* one C3D20 in .frd order: corners, the mid nodes of the bottom face, of the
       upright edges, of the top face */
    static const float c[8][3] = { {0,0,0}, {2,0,0}, {2,2,0}, {0,2,0}, {0,0,2}, {2,0,2}, {2,2,2}, {0,2,2} };
    static const int ed[12][2] = { {0,1}, {1,2}, {2,3}, {3,0}, {0,4}, {1,5}, {2,6}, {3,7}, {4,5}, {5,6}, {6,7}, {7,4} };
    static tm_mesh m;
    tm_init(&m);
    uint32_t v[20];
    for (int i = 0; i < 8; i++) v[i] = tm_node(&m, c[i][0], c[i][1], c[i][2]);
    for (int i = 0; i < 12; i++)
        v[8 + i] = tm_node(&m, 0.5f * (c[ed[i][0]][0] + c[ed[i][1]][0]), 0.5f * (c[ed[i][0]][1] + c[ed[i][1]][1]),
                           0.5f * (c[ed[i][0]][2] + c[ed[i][1]][2]));
    {   /* tm_mesh holds 8 nodes per element: this one needs room of its own */
        static uint32_t conn[20], eoff[2] = { 0, 20 };
        static uint8_t et[1] = { 4 };
        static uint32_t mat[1] = { 1 };
        memcpy(conn, v, sizeof conn);
        m.f.conn = conn; m.f.eoff = eoff; m.f.etype = et; m.f.emat = mat; m.f.n_elems = 1;
    }
    cv_skin s;
    CHECK(cv_skin_build_opt(&s, &m.f, NULL, CV_CREASE_DEG, false));
    CHECK_EQ(s.n_tri, 12); CHECK_EQ(s.n_edge, 12); CHECK_EQ(s.n_fedge, 12);
    cv_skin_free(&s);
    CHECK(cv_skin_build(&s, &m.f, NULL));            /* the default: through the mid nodes */
    CHECK_EQ(s.n_tri, 36);                           /* six per face */
    CHECK_EQ(s.n_edge, 24);                          /* every side in two pieces */
    CHECK_EQ(s.n_fedge, 24);                         /* the outline follows them */
    bool seen[20] = { false };
    for (size_t i = 0; i < 3 * s.n_tri; i++) seen[s.tri[i]] = true;
    for (int i = 0; i < 20; i++) CHECK(seen[i]);     /* every node of the element colours a face */
    {
        const float a[3] = { 0, 0, 0 }, mid[3] = { 1, 0, 0 }, b[3] = { 2, 0, 0 };
        CHECK(tm_has_fedge(&m, &s, a, mid) && tm_has_fedge(&m, &s, mid, b) && !tm_has_fedge(&m, &s, a, b));
    }
    cv_skin_free(&s);
    tm_same_skin(&m.f, 3.0);

    /* the element zoo as CalculiX wrote it: C3D20R, C3D10, C3D15 and the solids its
       shells and beams became. A mid node taken from the wrong edge would fold a
       face over and change its area. */
    FILE* fp = fopen("samples/elements/elements.frd", "rb");
    CHECK(fp != NULL);
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long n = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char* buf = malloc((size_t)n + 1);
        CHECK(buf && fread(buf, 1, (size_t)n, fp) == (size_t)n);
        fclose(fp);
        cv_frd f;
        CHECK(cv_frd_parse(&f, buf, (size_t)n));
        tm_same_skin(&f, 0);
        cv_frd_free(&f); free(f.msgs.a);
        free(buf);
    }
}

#endif
