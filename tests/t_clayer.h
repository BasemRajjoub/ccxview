/* t_clayer.h -- unit tests for src/clayer.c: the contact interface layer's tops over
   the slave corners (open, closed, penetrating, clamped), its prisms and edges. */
#ifndef CV_T_CLAYER_H
#define CV_T_CLAYER_H

#include "../src/clayer.h"
#include "../src/contact.h"

static cv_lpt clay_pt(float x, float y, float z, float d) {
    cv_lpt p;
    memset(&p, 0, sizeof p);
    p.p[0] = x; p.p[1] = y; p.p[2] = z;
    for (int i = 0; i < 6; i++) p.d[i] = d;
    return p;
}

/* the volume the triangles close (divergence theorem): positive when turned outward */
static double clay_volume(const cv_lpt* t, int nt) {
    double v = 0;
    for (int i = 0; i < nt; i++) {
        const float* a = t[3 * i].p; const float* b = t[3 * i + 1].p; const float* c = t[3 * i + 2].p;
        v += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0;
    }
    return v;
}

/* slave corner s against the master face m (corners, n of them): its top */
static float clay_top_on(const cv_lpt* s, const float m[][3], int n, int place, float minth, cv_lpt* top, cv_cproj* o) {
    CHECK(cv_contact_project(s->p, m, n, o));
    cv_lpt f = clay_pt(o->foot[0], o->foot[1], o->foot[2], 2.f);   /* the master moves otherwise than the slave */
    return cv_clay_top(s, &f, o->n, o->gap, place, minth, top);
}

static void test_clayer(void) {
    cv_cproj o;
    cv_lpt top;
    /* the slave face in z = 0, its normal +z towards the master; a master face above it at
       z = 0.5 facing down (its corners turning the other way) */
    const float up[4][3] = { { -1, -1, 0.5f }, { -1, 3, 0.5f }, { 3, 3, 0.5f }, { 3, -1, 0.5f } };
    cv_lpt s = clay_pt(1, 1, 0, 1.f);
    float th = clay_top_on(&s, up, 4, CV_CLAY_FOOT, 0, &top, &o);
    CHECK_NEAR(th, 0.5, 1e-6); CHECK_NEAR(o.n[2], -1, 1e-6);
    CHECK_NEAR(top.p[2], 0.5, 1e-6); CHECK_NEAR(top.p[0], 1, 1e-6);
    CHECK_NEAR(top.d[0], 2, 1e-6);                  /* on the foot: it moves with the master */
    th = clay_top_on(&s, up, 4, CV_CLAY_TRUE, 0, &top, &o);
    CHECK_NEAR(th, 0.5, 1e-6); CHECK_NEAR(top.p[2], 0.5, 1e-6);
    CHECK_NEAR(top.d[0], 1, 1e-6);                  /* true scale: it moves with the slave */
    /* closed: no thickness; a least thickness grows a skin towards the master */
    const float on[4][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, 3, 0 }, { 3, -1, 0 } };
    th = clay_top_on(&s, on, 4, CV_CLAY_FOOT, 0, &top, &o);
    CHECK_NEAR(th, 0, 1e-6); CHECK_NEAR(top.p[2], 0, 1e-6);
    th = clay_top_on(&s, on, 4, CV_CLAY_FOOT, 0.1f, &top, &o);
    CHECK_NEAR(top.p[2], 0.1, 1e-6);
    th = clay_top_on(&s, on, 4, CV_CLAY_TRUE, 0.1f, &top, &o);
    CHECK_NEAR(top.p[2], 0.1, 1e-6);
    /* a gap wider than the least thickness keeps its own */
    th = clay_top_on(&s, up, 4, CV_CLAY_FOOT, 0.1f, &top, &o);
    CHECK_NEAR(top.p[2], 0.5, 1e-6);
    /* penetrating: the master face below the slave corner; the gap negative, the top
       inside the slave body (below its face), as deep as the overclosure */
    const float in[4][3] = { { -1, -1, -0.2f }, { -1, 3, -0.2f }, { 3, 3, -0.2f }, { 3, -1, -0.2f } };
    th = clay_top_on(&s, in, 4, CV_CLAY_FOOT, 0, &top, &o);
    CHECK_NEAR(th, -0.2, 1e-6); CHECK_NEAR(top.p[2], -0.2, 1e-6);
    th = clay_top_on(&s, in, 4, CV_CLAY_TRUE, 0, &top, &o);
    CHECK_NEAR(th, -0.2, 1e-6); CHECK_NEAR(top.p[2], -0.2, 1e-6);
    th = clay_top_on(&s, in, 4, CV_CLAY_TRUE, 0.5f, &top, &o);
    CHECK_NEAR(top.p[2], -0.5, 1e-6);               /* the skin deeper still, the same way */
    /* clamped: the master face beside the corner; the top on the face's edge, not above
       the corner */
    const float side[4][3] = { { 2, -1, 0.5f }, { 2, 3, 0.5f }, { 5, 3, 0.5f }, { 5, -1, 0.5f } };
    th = clay_top_on(&s, side, 4, CV_CLAY_FOOT, 0, &top, &o);
    CHECK(!o.inside);
    CHECK_NEAR(top.p[0], 2, 1e-5); CHECK_NEAR(top.p[2], 0.5, 1e-5);
    /* a triangle master face, unknown gap */
    const float tri3[3][3] = { { -1, -1, 0.3f }, { -1, 3, 0.3f }, { 3, -1, 0.3f } };
    th = clay_top_on(&s, tri3, 3, CV_CLAY_FOOT, 0, &top, &o);
    CHECK_NEAR(th, 0.3, 1e-6); CHECK_NEAR(top.p[2], 0.3, 1e-6);
    th = cv_clay_top(&s, NULL, o.n, 0.3f, CV_CLAY_FOOT, 0, &top);
    CHECK_NEAR(th, 0, 1e-9); CHECK_NEAR(top.p[2], 0, 1e-9);
    th = cv_clay_top(&s, &s, o.n, NAN, CV_CLAY_TRUE, 0.1f, &top);
    CHECK_NEAR(th, 0, 1e-9); CHECK_NEAR(top.p[2], 0, 1e-9);

    /* prisms: a 2 x 2 quad face, 0.5 thick: 12 triangles turned outward closing 2.0 */
    cv_lpt b[4] = { clay_pt(0, 0, 0, 0), clay_pt(2, 0, 0, 0), clay_pt(2, 2, 0, 0), clay_pt(0, 2, 0, 0) }, t[4];
    for (int k = 0; k < 4; k++) { t[k] = b[k]; t[k].p[2] = 0.5f; }
    float v[4] = { 1, 2, 3, 4 }, val[3 * CV_CLAY_MAXTRI];
    cv_lpt tr[3 * CV_CLAY_MAXTRI], seg[24];
    int nt = cv_clay_prism(4, b, t, v, false, tr, val);
    CHECK(nt == 12);
    CHECK_NEAR(clay_volume(tr, nt), 2.0, 1e-5);
    nt = cv_clay_prism(4, b, t, v, true, tr, val);
    CHECK(nt == 48);
    CHECK_NEAR(clay_volume(tr, nt), 2.0, 1e-5);
    bool same = true;                               /* each patch in one corner's value */
    for (int i = 0; i < nt; i++) same = same && val[3 * i] == val[3 * i + 1] && val[3 * i] == val[3 * i + 2];
    CHECK(same);
    CHECK(cv_clay_edges(4, b, t, seg) == 12);
    /* a wedge: the corner values interpolate over its faces */
    t[0].p[2] = 0.f;                                /* corner 0 closed */
    nt = cv_clay_prism(3, b, t, v, false, tr, val);
    CHECK(nt == 8);
    CHECK_NEAR(clay_volume(tr, nt), (0 + 0.5 + 0.5) / 3.0 * 2.0, 1e-5);   /* a linear thickness over the triangle */
    CHECK(cv_clay_prism(3, b, t, v, true, tr, val) == 36);
    CHECK(cv_clay_edges(3, b, t, seg) == 9);
    CHECK(cv_clay_prism(5, b, t, v, false, tr, val) == 0);
    /* penetrating everywhere: the prism inside out, its volume negative */
    for (int k = 0; k < 4; k++) t[k].p[2] = -0.2f;
    nt = cv_clay_prism(4, b, t, v, false, tr, val);
    CHECK_NEAR(clay_volume(tr, nt), -0.8, 1e-5);
}

#endif
