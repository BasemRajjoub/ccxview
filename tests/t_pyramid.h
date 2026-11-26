/* t_pyramid.h -- 5-node pyramids: a C3D5 as CalculiX writes it to the .frd (a Hex8
   with the apex four times) and as the .inp reader keeps one. Its faces, edges and
   outline, its type, its deck faces, its quality. Included by test_main.c after t_mesh.h. */
#ifndef CV_T_PYRAMID_H
#define CV_T_PYRAMID_H

#include "../src/pickinfo.h"
#include "../src/quality.h"

/* no triangle with a repeated corner, no edge from a node to itself */
static void tp_no_degenerate(const cv_skin* s) {
    for (size_t t = 0; t < s->n_tri; t++) {
        const uint32_t* v = s->tri + 3 * t;
        CHECK(v[0] != v[1] && v[1] != v[2] && v[0] != v[2]);
    }
    for (size_t i = 0; i < s->n_edge; i++) CHECK(s->edge[2 * i] != s->edge[2 * i + 1]);
    for (size_t i = 0; i < s->n_fedge; i++) CHECK(s->fedge[2 * i] != s->fedge[2 * i + 1]);
}

/* a unit pyramid on the brick's top face, apex at height h; v: the hex8 as written */
static void tp_pyramid(tm_mesh* m, float z, uint32_t mat, uint32_t v[8]) {
    v[0] = tm_node(m, 0, 0, z); v[1] = tm_node(m, 1, 0, z); v[2] = tm_node(m, 1, 1, z); v[3] = tm_node(m, 0, 1, z);
    v[4] = v[5] = v[6] = v[7] = tm_node(m, .5f, .5f, z + 1);
    tm_elem(m, 1, mat, v, 8);
}

static void test_pyramid_skin(void) {
    static tm_mesh m;
    uint32_t v[8], p[5];
    cv_skin s;

    /* alone: five faces (a quad and four triangles), eight edges, all of them outline
       (the sides meet at more than the crease angle) */
    tm_init(&m);
    tp_pyramid(&m, 0, 1, v);
    CHECK(cv_hex_pyramid(v, p));
    CHECK(p[0] == v[0] && p[1] == v[1] && p[2] == v[2] && p[3] == v[3] && p[4] == v[4]);
    CHECK_EQ(cv_elem_type(&m.f, 0), CV_PYR5);
    CHECK(!strcmp(cv_frd_type_name(CV_PYR5), "Pyramid5"));
    CHECK_EQ(cv_frd_type_nodes(CV_PYR5), 0);           /* never read from an .frd as such */
    CHECK(cv_skin_build(&s, &m.f, NULL));
    CHECK_EQ(s.n_face, 5);
    CHECK_EQ(s.n_tri, 2 + 4);
    CHECK_EQ(s.n_edge, 8);
    CHECK_EQ(s.n_fedge, 8);
    CHECK_EQ(s.n_pt, 5);
    tp_no_degenerate(&s);
    cv_skin_free(&s);
    uint32_t c[4];
    CHECK_EQ(cv_elem_face_corners(&m.f, 0, 0, c), 4);   /* S1 the base */
    CHECK_EQ(cv_elem_face_corners(&m.f, 0, 1, c), 0);   /* S2 the apex: no face */
    CHECK_EQ(cv_elem_face_corners(&m.f, 0, 2, c), 3);   /* S3 the side 1-5-2 */
    CHECK(c[0] == v[0] && c[1] == v[4] && c[2] == v[1]);
    uint32_t ed[12][3];
    CHECK_EQ(cv_elem_edges(&m.f, 0, ed, 12), 8);

    /* on a brick, sharing its top face: that face is inside, the brick's other five
       and the pyramid's four sides outside */
    tm_init(&m);
    tm_brick(&m, 0, 0, 0, 1);
    tp_pyramid(&m, 1, 1, v);
    CHECK_EQ(cv_elem_type(&m.f, 0), 1);
    CHECK_EQ(cv_elem_type(&m.f, 1), CV_PYR5);
    CHECK(cv_skin_build(&s, &m.f, NULL));
    CHECK_EQ(s.n_face, 5 + 4);
    CHECK_EQ(s.n_tri, 10 + 4);
    tp_no_degenerate(&s);
    cv_skin_free(&s);

    /* a tet on one side: the shared triangle {a, b, apex} is inside. The hex face
       {a, apex, apex, b} keys as the tet's three nodes */
    tm_init(&m);
    tp_pyramid(&m, 0, 1, v);
    uint32_t t4[4] = { v[1], v[2], v[4], tm_node(&m, 2, .5f, .5f) };
    tm_elem(&m, 3, 1, t4, 4);
    CHECK(cv_skin_build(&s, &m.f, NULL));
    CHECK_EQ(s.n_face, 5 - 1 + 4 - 1);
    for (size_t i = 0; i < s.n_face; i++)                  /* the pyramid's S4 (1-5-6-2) not among them */
        CHECK(s.face[i] != (0u << 3 | 3u));
    tp_no_degenerate(&s);
    cv_skin_free(&s);

    /* the type axis: hexes and pyramids apart */
    tm_init(&m);
    tm_brick(&m, 0, 0, 0, 1);
    tp_pyramid(&m, 1, 1, v);
    m.f.egrp = m.emat;                  /* the test mesh carries no groups */
    cv_groups g;
    CHECK(cv_groups_build(&g, &m.f));
    CHECK_EQ(g.axis[CV_AXIS_TYPE].n, 2);
    CHECK_EQ(g.axis[CV_AXIS_TYPE].value[0], 1);
    CHECK_EQ(g.axis[CV_AXIS_TYPE].value[1], CV_PYR5);
    CHECK_EQ(g.axis[CV_AXIS_TYPE].of_elem[1], 1);
    cv_groups_free(&g);

    /* collapsed elsewhere: the apex for nodes 2, 3, 6, 7, the base face 0-4-5-1 */
    tm_init(&m);
    uint32_t a = tm_node(&m, 0, 0, 0), b = tm_node(&m, 1, 0, 0), ap = tm_node(&m, .5f, 1, .5f);
    uint32_t d = tm_node(&m, 0, 0, 1), e = tm_node(&m, 1, 0, 1);
    uint32_t w[8] = { a, b, ap, ap, d, e, ap, ap };
    tm_elem(&m, 1, 1, w, 8);
    CHECK(cv_hex_pyramid(w, p));
    CHECK(p[0] == a && p[1] == d && p[2] == e && p[3] == b && p[4] == ap);
    CHECK(cv_skin_build(&s, &m.f, NULL));
    CHECK_EQ(s.n_face, 5);
    CHECK_EQ(s.n_tri, 6);
    tp_no_degenerate(&s);
    cv_skin_free(&s);

    /* a wedge written as a hex (2 = 3, 6 = 7): no pyramid, but no flat faces either */
    tm_init(&m);
    uint32_t q0 = tm_node(&m, 0, 0, 0), q1 = tm_node(&m, 1, 0, 0), q2 = tm_node(&m, 0, 1, 0);
    uint32_t q4 = tm_node(&m, 0, 0, 1), q5 = tm_node(&m, 1, 0, 1), q6 = tm_node(&m, 0, 1, 1);
    uint32_t wd[8] = { q0, q1, q2, q2, q4, q5, q6, q6 };
    tm_elem(&m, 1, 1, wd, 8);
    CHECK(!cv_hex_pyramid(wd, NULL));
    CHECK_EQ(cv_elem_type(&m.f, 0), 1);
    CHECK(cv_skin_build(&s, &m.f, NULL));
    CHECK_EQ(s.n_face, 5);                                 /* two triangles, three quads */
    CHECK_EQ(s.n_tri, 2 + 6);
    CHECK_EQ(s.n_edge, 9);
    tp_no_degenerate(&s);
    cv_skin_free(&s);

    /* not pyramids: an ordinary hex, a hex with only two top nodes one */
    uint32_t h[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    CHECK(!cv_hex_pyramid(h, NULL));
    h[5] = 4;
    CHECK(!cv_hex_pyramid(h, NULL));
}

/* a C3D5 in a deck: kept as a Hex8 with the apex four times, shown a pyramid, its
   faces S1..S5 the hex's S1, S3..S6 */
static void test_pyramid_inp(void) {
    const char* deck =
        "*NODE\n1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n5,.5,.5,1\n6,2,.5,.5\n"
        "*ELEMENT, TYPE=C3D5, ELSET=EP\n10, 1, 2, 3, 4, 5\n"
        "*ELEMENT, TYPE=C3D4, ELSET=ET\n11, 2, 3, 5, 6\n"
        "*SURFACE, NAME=SP, TYPE=ELEMENT\n10, S1\n10, S3\n"
        "*STEP\n*STATIC\n*DLOAD\n10, P5, 1.\n11, P2, 1.\n*END STEP\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    const cv_frd* f = &d.mesh;
    CHECK_EQ(f->n_elems, 2);
    CHECK_EQ(f->etype[0], 1);
    CHECK_EQ(f->eoff[1] - f->eoff[0], 8);
    static const uint32_t want[8] = { 1, 2, 3, 4, 5, 5, 5, 5 };
    for (int i = 0; i < 8; i++) CHECK_EQ(f->node_id[f->conn[f->eoff[0] + i]], want[i]);
    CHECK_EQ(cv_elem_type(f, 0), CV_PYR5);
    CHECK_EQ(cv_elem_type(f, 1), 3);
    CHECK_EQ(d.ndloads, 2);
    for (uint32_t i = 0; i < d.ndloads; i++) {
        if (d.dloads[i].elem == 10) CHECK_EQ(d.dloads[i].face, 5);   /* P5: 4-5-1, the hex's S6 */
        else CHECK_EQ(d.dloads[i].face, 1);                         /* a tet's P2 as it was */
    }
    CHECK_EQ(d.nsurfs, 1);
    if (d.nsurfs == 1 && d.surfs[0].n == 2) {
        CHECK_EQ(d.surfs[0].face[0], 0);                 /* S1 the base */
        CHECK_EQ(d.surfs[0].face[1], 3);                 /* S3: 2-5-3 */
        uint32_t c[4];
        CHECK_EQ(cv_elem_face_corners(f, 0, d.surfs[0].face[1], c), 3);
        CHECK(f->node_id[c[0]] == 2 && f->node_id[c[1]] == 5 && f->node_id[c[2]] == 3);
        CHECK_EQ(cv_elem_face_corners(f, 0, 5, c), 3);   /* and P5's */
        CHECK(f->node_id[c[0]] == 4 && f->node_id[c[1]] == 5 && f->node_id[c[2]] == 1);
    }
    cv_skin s;                                           /* the face they share is inside */
    CHECK(cv_skin_build(&s, f, NULL));
    CHECK_EQ(s.n_face, 4 + 3);
    tp_no_degenerate(&s);
    cv_skin_free(&s);
    cv_inp_free(&d);
}

/* a regular pyramid (every edge 1) scores ideal; flattened, it degrades */
static void test_pyramid_quality(void) {
    const double h = 1 / sqrt(2.0);
    double x[5][3] = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}, {.5,.5,h} }, q[CV_MQ_N];
    cv_mq_init();
    CHECK_EQ(cv_mq_dim(CV_PYR5), 3);
    cv_mq_elem(CV_PYR5, 5, (const double (*)[3])x, q);
    CHECK_NEAR(q[CV_MQ_SIZE], h / 3, 1e-12);
    CHECK_NEAR(q[CV_MQ_EDGE_MIN], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_ASPECT], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_SJAC], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_JRATIO], 1, 1e-12);
    CHECK_NEAR(q[CV_MQ_SKEW], 0, 1e-12);
    CHECK_NEAR(q[CV_MQ_ANGLE_MIN], 60, 1e-9);
    CHECK_NEAR(q[CV_MQ_ANGLE_MAX], 90, 1e-9);
    CHECK_NEAR(q[CV_MQ_WARP], 0, 1e-9);
    CHECK_NEAR(q[CV_MQ_ANSYS], 1, 1e-9);
    CHECK_NEAR(q[CV_MQ_CCX], 1, 1e-9);
    CHECK_EQ(q[CV_MQ_ABAQUS], 0);
    CHECK(q[CV_MQ_SHAPE] != q[CV_MQ_SHAPE]);
    x[4][2] = 0.05;                                      /* nearly flat */
    cv_mq_elem(CV_PYR5, 5, (const double (*)[3])x, q);
    CHECK(q[CV_MQ_SJAC] < 0.2 && q[CV_MQ_SJAC] > 0);
    CHECK_NEAR(q[CV_MQ_SIZE], 0.05 / 3, 1e-12);
    x[4][2] = -h;                                        /* inside out */
    cv_mq_elem(CV_PYR5, 5, (const double (*)[3])x, q);
    CHECK(q[CV_MQ_SIZE] < 0 && q[CV_MQ_SJAC] < 0);
}

#endif
