/* t_path.h -- unit tests for src/path.c (Dijkstra over the skin edge graph). */
#ifndef CV_T_PATH_H
#define CV_T_PATH_H

#include "../src/path.h"
#include <math.h>

/* 3x3 grid, z=0, node index = y*3+x (ids 0..8); grid edges (horizontal +
   vertical) plus one long diagonal shortcut straight from corner to corner,
   which is shorter than the 4-edge manhattan route around the rim. */
static void path_build_grid(cv_frd* f, cv_skin* sk, uint32_t* edge_buf, size_t cap) {
    memset(f, 0, sizeof *f);
    f->n_nodes = 9;
    f->xyz = malloc(9 * 3 * sizeof *f->xyz);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++) {
            int i = y * 3 + x;
            f->xyz[3 * i] = (float)x; f->xyz[3 * i + 1] = (float)y; f->xyz[3 * i + 2] = 0.f;
        }

    size_t n = 0;
#define IDX(x, y) ((uint32_t)((y) * 3 + (x)))
#define ADD(u, v) do { edge_buf[2 * n] = (u); edge_buf[2 * n + 1] = (v); n++; } while (0)
    for (int y = 0; y < 3; y++) for (int x = 0; x < 2; x++) ADD(IDX(x, y), IDX(x + 1, y));
    for (int x = 0; x < 3; x++) for (int y = 0; y < 2; y++) ADD(IDX(x, y), IDX(x, y + 1));
    ADD(IDX(0, 0), IDX(2, 2));           /* the diagonal shortcut */
#undef ADD
#undef IDX
    (void)cap;
    memset(sk, 0, sizeof *sk);
    sk->edge = edge_buf;
    sk->n_edge = n;
}

static void test_path(void) {
    cv_frd f;
    cv_skin sk;
    uint32_t edges[2 * 13];
    path_build_grid(&f, &sk, edges, CV_COUNT(edges));

    /* corner to corner: the diagonal (length 2*sqrt(2)) beats the rim (length 4) */
    uint32_t* path; uint32_t n; float* dist;
    CHECK(cv_path_find(&f, &sk, 0, 8, &path, &n, &dist));
    CHECK_EQ(n, 2);
    if (n == 2) {
        CHECK_EQ(path[0], 0);
        CHECK_EQ(path[1], 8);
        CHECK_NEAR(dist[0], 0.0, 1e-6);
        CHECK_NEAR(dist[1], 2.0 * sqrt(2.0), 1e-6);
    }
    free(path); free(dist);

    /* a == b: a one-node path, zero length */
    CHECK(cv_path_find(&f, &sk, 4, 4, &path, &n, &dist));
    CHECK_EQ(n, 1);
    if (n == 1) { CHECK_EQ(path[0], 4); CHECK_NEAR(dist[0], 0.0, 1e-6); }
    free(path); free(dist);

    /* dist may be NULL: caller doesn't want cumulative lengths */
    CHECK(cv_path_find(&f, &sk, 0, 8, &path, &n, NULL));
    CHECK_EQ(n, 2);
    free(path);

    /* an isolated node (no edges reach it) is unreachable from anywhere else */
    {
        cv_frd f2 = f;
        float xyz10[30];
        memcpy(xyz10, f.xyz, 27 * sizeof(float));
        xyz10[27] = 5.f; xyz10[28] = 5.f; xyz10[29] = 5.f;
        f2.xyz = xyz10;
        f2.n_nodes = 10;
        path = (uint32_t*)0x1; n = 999; dist = (float*)0x1;
        CHECK(!cv_path_find(&f2, &sk, 0, 9, &path, &n, &dist));
        CHECK(path == NULL); CHECK_EQ(n, 0); CHECK(dist == NULL);
    }

    /* out-of-range indices */
    path = (uint32_t*)0x1; n = 999; dist = (float*)0x1;
    CHECK(!cv_path_find(&f, &sk, 0, 100, &path, &n, &dist));
    CHECK(path == NULL); CHECK_EQ(n, 0); CHECK(dist == NULL);
    CHECK(!cv_path_find(&f, &sk, 100, 0, &path, &n, NULL));

    /* unreachable when there is no path at all: a graph with no edges */
    {
        cv_skin empty = {0};
        CHECK(!cv_path_find(&f, &empty, 0, 8, &path, &n, &dist));
    }

    free(f.xyz);

    /* fuzz: random small graphs must never crash, whatever the wiring */
    uint32_t rng = 424242;
    for (int it = 0; it < 100; it++) {
        rng = rng * 1664525u + 1013904223u;
        uint32_t nn = 1 + (rng % 8);
        cv_frd rf; memset(&rf, 0, sizeof rf);
        rf.n_nodes = nn;
        rf.xyz = malloc((size_t)nn * 3 * sizeof(float));
        for (uint32_t i = 0; i < nn * 3; i++) {
            rng = rng * 1664525u + 1013904223u;
            rf.xyz[i] = (float)((int)(rng % 2001) - 1000) * 0.01f;
        }
        rng = rng * 1664525u + 1013904223u;
        size_t ne = rng % (2 * nn + 1);
        uint32_t* eb = malloc(CV_MAX(ne, 1) * 2 * sizeof(uint32_t));
        for (size_t i = 0; i < ne; i++) {
            rng = rng * 1664525u + 1013904223u; eb[2 * i] = rng % (nn + 2);      /* some out of range */
            rng = rng * 1664525u + 1013904223u; eb[2 * i + 1] = rng % (nn + 2);
        }
        cv_skin rsk = {0}; rsk.edge = eb; rsk.n_edge = ne;
        rng = rng * 1664525u + 1013904223u; uint32_t ra = rng % (nn + 2);
        rng = rng * 1664525u + 1013904223u; uint32_t rb = rng % (nn + 2);
        uint32_t* rp; uint32_t rn; float* rd;
        if (cv_path_find(&rf, &rsk, ra, rb, &rp, &rn, &rd)) { free(rp); free(rd); }
        free(eb);
        free(rf.xyz);
    }
    CHECK(1);
}

#endif
