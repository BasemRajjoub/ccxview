/* t_integ.h -- integrals over elements and faces, sums over nodes (integ.c). */
#include "../src/integ.h"

/* A mesh of single elements, each its own nodes, built from natural coordinates:
   hex [-1,1]^3 -> [0,1]^3 (then scaled by sx along x and shifted by ox), tet as is,
   wedge (r, s, z) -> (r, s, (z+1)/2). */
typedef struct { cv_frd f; uint32_t nn, ne; } im_mesh;

static void im_add(im_mesh* m, int t, int nn, double sx, double ox) {
    uint32_t b = m->f.eoff[m->ne];
    for (int i = 0; i < nn; i++) {
        double xi[3], x[3];
        cv_node_param(t, nn, i, xi);
        bool hex = t == 1 || t == 4, wedge = t == 2 || t == 5;
        for (int k = 0; k < 3; k++) x[k] = hex ? (xi[k] + 1) / 2 : xi[k];
        if (wedge) x[2] = (xi[2] + 1) / 2;
        x[0] = x[0] * sx + ox;
        for (int k = 0; k < 3; k++) m->f.xyz[3 * m->nn + k] = (float)x[k];
        m->f.conn[b + (uint32_t)cv_frd_node_pos(t, nn, i)] = m->nn;
        m->nn++;
    }
    m->f.etype[m->ne] = (uint8_t)t;
    m->f.eoff[++m->ne] = b + (uint32_t)nn;
    m->f.n_nodes = m->nn; m->f.n_elems = m->ne;
}

static void im_init(im_mesh* m) {
    memset(m, 0, sizeof *m);
    m->f.xyz = calloc(3 * 200, sizeof(float));
    m->f.conn = calloc(200, sizeof(uint32_t));
    m->f.etype = calloc(16, 1);
    m->f.eoff = calloc(17, sizeof(uint32_t));
}
static void im_free(im_mesh* m) { free(m->f.xyz); free(m->f.conn); free(m->f.etype); free(m->f.eoff); }

/* a nodal field from a function of x, y, z */
static float* im_field(const im_mesh* m, double (*fn)(const float*)) {
    float* v = malloc(m->nn * sizeof(float));
    for (uint32_t i = 0; i < m->nn; i++) v[i] = (float)fn(m->f.xyz + 3 * i);
    return v;
}
static double im_lin(const float* p) { return 1 + 2 * p[0] + 3 * p[1] - p[2]; }
static double im_quad(const float* p) { return p[0] * p[0] + p[1] * p[2]; }

static void test_integ(void) {
    /* volume, a linear field, a quadratic one: unit cube, unit tet, unit wedge */
    static const struct { int t, nn; double V, lin, quad; bool quadratic; } E[] = {
        { 1, 8,  1, 3, 0, false },        { 4, 20, 1, 3, 7.0 / 12, true },
        { 3, 4,  1.0 / 6, 1.0 / 3, 0, false }, { 6, 10, 1.0 / 6, 1.0 / 3, 1.0 / 40, true },
        { 2, 6,  0.5, 13.0 / 12, 0, false },   { 5, 15, 0.5, 13.0 / 12, 1.0 / 6, true },
    };
    for (size_t k = 0; k < sizeof E / sizeof E[0]; k++) {
        im_mesh m; im_init(&m);
        im_add(&m, E[k].t, E[k].nn, 1, 0);
        cv_integ r;
        cv_integ_volume(&m.f, NULL, 0, NULL, 0, &r);
        CHECK_NEAR(r.size, E[k].V, 1e-12);
        CHECK_EQ(r.n, 1);
        float* v = im_field(&m, im_lin);
        cv_integ_volume(&m.f, NULL, 0, v, 1, &r);
        CHECK_NEAR(r.integ[0], E[k].lin, 1e-6);
        free(v);
        if (E[k].quadratic) {
            v = im_field(&m, im_quad);
            cv_integ_volume(&m.f, NULL, 0, v, 1, &r);
            CHECK_NEAR(r.integ[0], E[k].quad, 1e-6);
            free(v);
        }
        im_free(&m);
    }

    /* the rules themselves: monomials over the unit tet, a! b! c! / (a+b+c+3)! up to degree 5 */
    {
        double xi[CV_RULE_MAX][3], w[CV_RULE_MAX];
        int np = cv_solid_rule(3, xi, w);
        CHECK_EQ(np, 15);
        static const int pw[][3] = { { 0, 0, 0 }, { 5, 0, 0 }, { 2, 2, 1 }, { 1, 1, 3 }, { 0, 4, 1 } };
        static const double want[] = { 1.0 / 6, 120.0 / 40320, 4.0 / 40320, 6.0 / 40320, 24.0 / 40320 };
        for (int q = 0; q < 5; q++) {
            double s = 0;
            for (int p = 0; p < np; p++) s += w[p] * pow(xi[p][0], pw[q][0]) * pow(xi[p][1], pw[q][1]) * pow(xi[p][2], pw[q][2]);
            CHECK_NEAR(s, want[q], 1e-12);
        }
        np = cv_solid_rule(2, xi, w);                  /* wedge: x^2 y^3 z^4 over tri x [-1,1]: 2!3!/7! * 2/5 */
        double s = 0;
        for (int p = 0; p < np; p++) s += w[p] * xi[p][0] * xi[p][0] * pow(xi[p][1], 3) * pow(xi[p][2], 4);
        CHECK_NEAR(s, 12.0 / 5040 * 0.4, 1e-12);
    }

    /* a distorted hex: the frustum with the bottom [0,1]^2, the top [0,.5]^2 one higher,
       V = (1 + 1/4 + 1/2) / 3; linear and with mid-side nodes on the edges */
    for (int q = 0; q < 2; q++) {
        int nn = q ? 20 : 8, t = q ? 4 : 1;
        double x[20][3];
        for (int i = 0; i < nn; i++) {
            double xi[3];
            cv_node_param(t, nn, i, xi);
            double z = (xi[2] + 1) / 2, s = 1 - z / 2;
            double p[3] = { (xi[0] + 1) / 2 * s, (xi[1] + 1) / 2 * s, z };
            memcpy(x[cv_frd_node_pos(t, nn, i)], p, sizeof p);
        }
        CHECK_NEAR(cv_elem_volume(t, nn, (const double (*)[3])x), 7.0 / 12, 1e-12);
    }
    CHECK(isnan(cv_elem_volume(9, 4, NULL)));             /* a shell is not a solid */

    /* a pressure on a face pushes along -n with p A; a tensor's traction S n A */
    {
        im_mesh m; im_init(&m);
        im_add(&m, 4, 20, 1, 0);                      /* C3D20 unit cube */
        im_add(&m, 6, 10, 1, 0);                      /* C3D10 unit tet */
        float* p = malloc(m.nn * sizeof(float));
        for (uint32_t i = 0; i < m.nn; i++) p[i] = 5;
        uint32_t el[4] = { 0, 0, 1, 0 };
        uint8_t fc[4] = { 1, 3, 2, 0 };                /* S2 top (+z), S4 x = 1, the tet's slanted S3, S1 bottom */
        cv_integ r;
        cv_integ_faces(&m.f, el, fc, 1, p, 1, &r);
        CHECK_NEAR(r.size, 1, 1e-12); CHECK_NEAR(r.integ[0], 5, 1e-6);
        CHECK_NEAR(r.push[0], 0, 1e-9); CHECK_NEAR(r.push[1], 0, 1e-9); CHECK_NEAR(r.push[2], -5, 1e-6);
        cv_integ_faces(&m.f, el + 3, fc + 3, 1, p, 1, &r);
        CHECK_NEAR(r.push[2], 5, 1e-6);
        cv_integ_faces(&m.f, el + 2, fc + 2, 1, p, 1, &r);
        CHECK_NEAR(r.size, sqrt(3.0) / 2, 1e-12);
        for (int k = 0; k < 3; k++) CHECK_NEAR(r.push[k], -5 * 0.5, 1e-6);   /* p A n, n = (1,1,1)/sqrt 3 */
        cv_integ_faces(&m.f, el, fc, 4, p, 1, &r);
        CHECK_EQ(r.n, 4);
        uint32_t fn[8];
        CHECK_EQ(cv_face_nodes(&m.f, 0, 1, fn), 8);   /* a C3D20 face: 4 corners, 4 mid-sides, all at z = 1 */
        for (int k = 0; k < 8; k++) CHECK_NEAR(m.f.xyz[3 * fn[k] + 2], 1, 1e-6);
        CHECK_EQ(cv_face_nodes(&m.f, 1, 2, fn), 6);   /* the tet's slanted face */
        for (int k = 0; k < 6; k++) CHECK_NEAR(m.f.xyz[3 * fn[k]] + m.f.xyz[3 * fn[k] + 1] + m.f.xyz[3 * fn[k] + 2], 1, 1e-6);
        uint8_t bad = 6;
        cv_integ_faces(&m.f, el, &bad, 1, p, 1, &r);
        CHECK_EQ(r.skipped, 1);
        float* S = calloc(6 * m.nn, sizeof(float));
        for (uint32_t i = 0; i < m.nn; i++) { S[6 * i] = 7; S[6 * i + 3] = 2; }     /* SXX 7, SXY 2 */
        cv_integ_faces(&m.f, el + 1, fc + 1, 1, S, 6, &r);
        CHECK_NEAR(r.traction[0], 7, 1e-6); CHECK_NEAR(r.traction[1], 2, 1e-6); CHECK_NEAR(r.traction[2], 0, 1e-9);
        /* a NaN at a node leaves the element out */
        p[0] = NAN;
        cv_integ_faces(&m.f, el, fc, 1, p, 1, &r);
        CHECK_EQ(r.missing, 1); CHECK_EQ(r.n, 0);
        free(p); free(S);
        im_free(&m);
    }

    /* an RVE of two phases: a unit cube at stress 100 and a 2x1x1 block at 10 beside it:
       volume fractions 1/3 and 2/3, <S> = 100/3 + 20/3 = 40 */
    {
        im_mesh m; im_init(&m);
        im_add(&m, 4, 20, 1, 0);
        im_add(&m, 1, 8, 2, 1);
        float* S = calloc(6 * m.nn, sizeof(float));
        for (uint32_t i = 0; i < m.nn; i++) { S[6 * i] = i < 20 ? 100 : 10; S[6 * i + 1] = i < 20 ? -3 : 6; }
        cv_integ r;
        cv_integ_volume(&m.f, NULL, 0, S, 6, &r);
        CHECK_NEAR(r.size, 3, 1e-12);
        CHECK_NEAR(r.integ[0] / r.size, 40, 1e-5);
        CHECK_NEAR(r.integ[1] / r.size, 3, 1e-5);
        uint32_t one = 1;
        cv_integ_volume(&m.f, &one, 1, S, 6, &r);
        CHECK_NEAR(r.size, 2, 1e-12); CHECK_NEAR(r.integ[0] / r.size, 10, 1e-5);
        free(S);
        im_free(&m);
    }

    /* sums over nodes: the force and its moment about a point */
    {
        im_mesh m; im_init(&m);
        im_add(&m, 1, 8, 1, 0);
        float F[24] = { 0 };
        uint32_t nodes[2] = { 1, 2 };                  /* (1,0,0) and (1,1,0) */
        F[3 * 1 + 1] = 10; F[3 * 2 + 0] = -4;
        double about[3] = { 0, 0, 0 };
        cv_nsum r;
        cv_integ_nodes(&m.f, nodes, 2, F, 3, about, &r);
        CHECK_EQ(r.n, 2);
        CHECK_NEAR(r.sum[0], -4, 1e-12); CHECK_NEAR(r.sum[1], 10, 1e-12);
        CHECK_NEAR(r.moment[2], 10 + 4, 1e-12);       /* (1,0,0) x (0,10,0) + (1,1,0) x (-4,0,0) */
        F[3 * 2] = NAN;
        cv_integ_nodes(&m.f, nodes, 2, F, 3, about, &r);
        CHECK_EQ(r.missing, 1);
        im_free(&m);
    }
}
