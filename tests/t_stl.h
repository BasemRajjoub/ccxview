/* t_stl.h -- unit tests for src/stl.c: binary and ASCII STL, welding, bad files. */
#ifndef CV_T_STL_H
#define CV_T_STL_H

#include "../src/stl.h"
#include <math.h>

/* a unit tetrahedron, outward counter-clockwise: 4 triangles, 4 corners */
static const float kTet[36] = {
    0, 0, 0,  0, 1, 0,  1, 0, 0,     /* z = 0, normal -z */
    0, 0, 0,  1, 0, 0,  0, 0, 1,     /* y = 0, normal -y */
    0, 0, 0,  0, 0, 1,  0, 1, 0,     /* x = 0, normal -x */
    1, 0, 0,  0, 1, 0,  0, 0, 1,     /* the slanted face */
};

static void stl_put(const char* path, const void* p, size_t n) {
    FILE* o = fopen(path, "wb");
    if (!o) { fprintf(stderr, "cannot create %s\n", path); return; }
    fwrite(p, 1, n, o);
    fclose(o);
}

static void test_stl(void) {
    char err[256];
    cv_stl s;

    /* binary, written and read back; welded: 4 corners, else 12 */
    const char* bin = "build/t_stl_bin.stl";
    CHECK(cv_stl_write(bin, kTet, 4));
    CHECK(cv_stl_read(&s, bin, false, err, sizeof err));
    CHECK(s.binary); CHECK_EQ(s.n_tri, 4); CHECK_EQ(s.n_vert, 12);
    cv_stl_free(&s);
    CHECK(cv_stl_read(&s, bin, true, err, sizeof err));
    CHECK_EQ(s.n_tri, 4); CHECK_EQ(s.n_vert, 4);
    if (s.n_tri == 4) {
        CHECK_NEAR(s.nrm[2], -1, 1e-6);                         /* the base faces down */
        CHECK_NEAR(s.nrm[9], 1 / sqrt(3.0), 1e-6);              /* the slanted face */
        CHECK_NEAR(s.lo[0], 0, 0); CHECK_NEAR(s.hi[2], 1, 0);
        CHECK(s.tri[0] == s.tri[3] && s.tri[0] == s.tri[6]);   /* the corner at 0 shared */
    }
    {   /* the outline: every edge of the corner tetrahedron is sharp; past 100 degrees only the slanted face's (125) */
        uint32_t* e; uint32_t ne;
        CHECK(cv_stl_edges(&s, 30.f, &e, &ne)); CHECK_EQ(ne, 6); free(e);
        CHECK(cv_stl_edges(&s, 100.f, &e, &ne)); CHECK_EQ(ne, 3); free(e);
    }
    cv_stl_free(&s);

    /* ASCII, with a polygon of four vertices (a fan of two) and a name after solid */
    const char* asc = "build/t_stl_asc.stl";
    const char* txt =
        "solid part one\n"
        " facet normal 0 0 1\n  outer loop\n   vertex 0 0 0\n   vertex 1 0 0\n   vertex 1 1 0\n  endloop\n endfacet\n"
        " facet normal 0 0 1\n  outer loop\n   vertex 0 0 1\n   vertex 2.5e0 0 1\n   vertex 2.5 1 1\n   vertex 0 1 1\n  endloop\n endfacet\n"
        "endsolid part one\n";
    stl_put(asc, txt, strlen(txt));
    CHECK(cv_stl_read(&s, asc, true, err, sizeof err));
    CHECK(!s.binary); CHECK_EQ(s.n_tri, 3); CHECK_EQ(s.n_vert, 7);
    CHECK_NEAR(s.hi[0], 2.5, 0);
    {   /* the flat quad of two: its four sides open, the diagonal not; the lone triangle three */
        uint32_t* e; uint32_t ne;
        CHECK(cv_stl_edges(&s, 30.f, &e, &ne)); CHECK_EQ(ne, 7); free(e);
    }
    if (s.n_tri == 3) CHECK_NEAR(s.nrm[2], 1, 1e-6);
    cv_stl_free(&s);

    /* a binary file whose header begins with "solid": the size decides */
    {
        size_t n = 84 + 50 * 4;
        unsigned char* b = calloc(1, n);
        FILE* f = fopen(bin, "rb");
        if (f) { CHECK(fread(b, 1, n, f) == n); fclose(f); }
        memcpy(b, "solid but binary", 16);
        const char* sb = "build/t_stl_solid.stl";
        stl_put(sb, b, n);
        CHECK(cv_stl_read(&s, sb, true, err, sizeof err));
        CHECK(s.binary); CHECK_EQ(s.n_tri, 4);
        cv_stl_free(&s);
        /* truncated binary: says so, nothing kept */
        stl_put(sb, b, n - 30);
        CHECK(!cv_stl_read(&s, sb, true, err, sizeof err));
        CHECK(strstr(err, "truncated") != NULL);
        CHECK(s.tri == NULL && s.n_tri == 0);
        /* a coordinate not a number: that triangle left out */
        memcpy(b, "plain header", 12);
        float nan = NAN;
        memcpy(b + 84 + 12, &nan, 4);
        stl_put(sb, b, n);
        CHECK(cv_stl_read(&s, sb, false, err, sizeof err));
        CHECK_EQ(s.n_tri, 3); CHECK_EQ(s.skipped, 1);
        cv_stl_free(&s);
        /* garbage of every length never crashes */
        for (size_t k = 0; k < n; k += 7) { CHECK(cv_stl_parse(&s, (const char*)b, k, true, err, sizeof err) || err[0]); cv_stl_free(&s); }
        free(b);
    }

    /* truncated ASCII: inside a facet */
    stl_put(asc, txt, 100);
    CHECK(!cv_stl_read(&s, asc, false, err, sizeof err));
    CHECK(strstr(err, "truncated") != NULL);
    /* a vertex with two numbers */
    const char* bad = "solid x\nfacet normal 0 0 1\nouter loop\nvertex 0 0\nendloop\nendfacet\nendsolid\n";
    CHECK(!cv_stl_parse(&s, bad, strlen(bad), false, err, sizeof err));
    CHECK(err[0] != 0);

    /* empty and missing files */
    const char* emp = "build/t_stl_empty.stl";
    stl_put(emp, "", 0);
    CHECK(!cv_stl_read(&s, emp, false, err, sizeof err));
    CHECK(strstr(err, "empty") != NULL);
    CHECK(!cv_stl_read(&s, "build/no_such_file.stl", false, err, sizeof err));
    CHECK(err[0] != 0);
    remove(bin); remove(asc); remove(emp); remove("build/t_stl_solid.stl");

    /* an STL alone as the model: welded vertices the nodes, Tri3 elements, ids from 1 */
    {
        CHECK(cv_stl_write(bin, kTet, 4));
        CHECK(cv_stl_read(&s, bin, true, err, sizeof err));
        cv_frd f;
        uint32_t dropped = 9;
        CHECK(cv_stl_to_frd(&s, &f, &dropped));
        CHECK_EQ(dropped, 0);
        CHECK_EQ(f.n_nodes, 4); CHECK_EQ(f.n_elems, 4); CHECK_EQ(f.n_steps, 0);
        CHECK(f.node_id[0] == 1 && f.node_id[3] == 4 && f.elem_id[0] == 1 && f.elem_id[3] == 4);
        CHECK_EQ(cv_frd_node_index(&f, 3), 2); CHECK_EQ(cv_frd_elem_index(&f, 4), 3);
        CHECK_EQ(cv_frd_node_index(&f, 5), UINT32_MAX);
        bool ok = true;
        for (uint32_t e = 0; e < f.n_elems; e++) {
            ok = ok && f.etype[e] == 7 && f.eoff[e] == 3 * e && cv_frd_type_nodes(f.etype[e]) == 3;
            for (int c = 0; c < 3; c++) {                     /* the same corners in the same order (outward) */
                const float* p = f.xyz + 3 * (size_t)f.conn[3 * e + c];
                ok = ok && p[0] == kTet[9 * e + 3 * c] && p[1] == kTet[9 * e + 3 * c + 1] && p[2] == kTet[9 * e + 3 * c + 2];
            }
        }
        CHECK(ok); CHECK_EQ(f.eoff[4], 12);
        cv_frd_free(&f); free(f.msgs.a);
        /* a triangle with two corners at one point is left out, the rest kept */
        s.tri[4] = s.tri[3];
        CHECK(cv_stl_to_frd(&s, &f, &dropped));
        CHECK_EQ(dropped, 1); CHECK_EQ(f.n_elems, 3); CHECK_EQ(f.n_nodes, 4);
        CHECK(f.elem_id[2] == 3 && f.conn[3] == s.tri[6]);   /* numbered on, the third triangle second */
        cv_frd_free(&f); free(f.msgs.a);
        cv_stl_free(&s);
        remove(bin);
    }

    /* see-through layers back to front: the farthest box centre from the eye first */
    {
        const float lo[] = { 0, 0, 0,   10, 0, 0,   -5, 0, 0,   0, 0, 0 };
        const float hi[] = { 2, 2, 2,   12, 2, 2,   -3, 2, 2,   2, 2, 2 };   /* centres x 1, 11, -4, 1 */
        const float eye[3] = { 20, 1, 1 };
        int o[4];
        cv_stl_order_far(lo, hi, 4, eye, o);
        CHECK(o[0] == 2 && o[1] == 0 && o[2] == 3 && o[3] == 1);         /* the tie keeps its order */
        const float eye2[3] = { -20, 1, 1 };
        cv_stl_order_far(lo, hi, 4, eye2, o);
        CHECK(o[0] == 1 && o[1] == 0 && o[2] == 3 && o[3] == 2);
    }
}

#endif
