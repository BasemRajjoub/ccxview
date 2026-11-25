/* t_export.h -- unit test for export.c (CSV / legacy VTK writers). Builds a
   tiny cv_frd by hand: no parser, no idmap (export.c never touches either). */
#ifndef CV_T_EXPORT_H
#define CV_T_EXPORT_H

#include "../src/export.h"
#include <math.h>
#include <string.h>

/* Read a whole file into a NUL-terminated buffer (malloc'd, caller frees). */
static char* t_export_slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char* buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

static int t_export_count_lines(const char* s) {
    int n = 0;
    for (; *s; s++) if (*s == '\n') n++;
    return n;
}

/* cv_fprintf: stb_sprintf behind fprintf. Text longer than its 512-byte chunk,
   the size modifiers the writers use, and floats that read back exactly. */
static void test_fprintf(void) {
    const char* path = "build/t_fprintf.txt";
    FILE* o = fopen(path, "wb");
    CHECK(o != NULL);
    if (!o) return;
    char big[2000];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    float v[] = { 601027.5625f, -1.17549435e-38f, 3.40282347e+38f, 1e-7f, 0.1f, 123456789.f };
    CHECK_EQ(cv_fprintf(o, "%s|%zu|%llu|%u|%d\n", big, (size_t)42, 18446744073709551615ull, 7u, -3), 1999 + 30);
    for (size_t i = 0; i < CV_COUNT(v); i++) cv_fprintf(o, "%.9g\n", v[i]);
    fclose(o);
    char* s = t_export_slurp(path);
    CHECK(s != NULL);
    if (!s) return;
    CHECK(strlen(s) > 2000 && strncmp(s + 1999, "|42|18446744073709551615|7|-3\n", 30) == 0);
    char* p = strchr(s, '\n') + 1;
    for (size_t i = 0; i < CV_COUNT(v); i++) {
        char* e;
        CHECK((float)strtod(p, &e) == v[i]);
        CHECK(*e == '\n');
        p = e + 1;
    }
    free(s);
}

static void test_export(void) {
    /* One hex8 (nodes 0-7, ids 1-8) + one tet4 (nodes 8-11, ids 9-12), sharing
       nothing. eoff/conn use dense node indices, as frd.c leaves them. */
    cv_frd f;
    memset(&f, 0, sizeof f);
    f.n_nodes = 12;
    uint32_t node_id[12];
    float xyz[36] = {
        0,0,0,  1,0,0,  1,1,0,  0,1,0,  0,0,1,  1,0,1,  1,1,1,  0,1,1,   /* hex8 corners */
        5,5,5,  6,5,5,  5,6,5,  5,5,6,                                  /* tet4 */
    };
    for (int i = 0; i < 12; i++) node_id[i] = (uint32_t)(i + 1);
    f.node_id = node_id;
    f.xyz = xyz;

    f.n_elems = 2;
    uint32_t elem_id[2] = { 100, 200 };
    uint8_t  etype[2]   = { 1, 3 };            /* Hex8, Tet4 */
    uint32_t emat[2]    = { 1, 2 };
    uint32_t egrp[2]    = { 0, 0 };
    uint32_t eoff[3]    = { 0, 8, 12 };
    uint32_t conn[12]   = { 0,1,2,3,4,5,6,7, 8,9,10,11 };
    f.elem_id = elem_id; f.etype = etype; f.emat = emat; f.egrp = egrp;
    f.eoff = eoff; f.conn = conn;

    float disp[36];
    for (int i = 0; i < 36; i++) disp[i] = (float)i * 0.1f;
    disp[3 * 9 + 1] = NAN;                     /* one NaN component, to check it round-trips */
    float scalar[12];
    for (int i = 0; i < 12; i++) scalar[i] = (float)i;

    const char* csv = "build/t_export.csv";
    const char* vtk = "build/t_export.vtk";

    /* ---- CSV, all nodes, with displacement and a scalar ---- */
    CHECK(cv_export_csv(csv, &f, disp, scalar, "Mises", NULL));
    char* s = t_export_slurp(csv);
    CHECK(s != NULL);
    if (s) {
        CHECK(strncmp(s, "id,x,y,z,dx,dy,dz,Mises\n", 24) == 0);
        CHECK_EQ(t_export_count_lines(s), 13);      /* header + 12 nodes */
        CHECK(strstr(s, "nan") != NULL);            /* the NaN component */
        free(s);
    }

    /* ---- CSV, no disp/scalar, vis hides the tet: only hex8's 8 nodes ---- */
    uint8_t vis[2] = { 1, 0 };
    CHECK(cv_export_csv(csv, &f, NULL, NULL, NULL, vis));
    s = t_export_slurp(csv);
    CHECK(s != NULL);
    if (s) {
        CHECK(strncmp(s, "id,x,y,z\n", 9) == 0);
        CHECK_EQ(t_export_count_lines(s), 9);       /* header + 8 nodes */
        free(s);
    }

    /* ---- VTK, everything visible ---- */
    CHECK(cv_export_vtk(vtk, &f, disp, scalar, "Mises", NULL));
    s = t_export_slurp(vtk);
    CHECK(s != NULL);
    if (s) {
        CHECK(strncmp(s, "# vtk DataFile Version 3.0\nccxview\nASCII\n"
                          "DATASET UNSTRUCTURED_GRID\n", 66) == 0);
        CHECK(strstr(s, "POINTS 12 float") != NULL);
        CHECK(strstr(s, "8 0 1 2 3 4 5 6 7\n") != NULL);       /* the hex cell */
        CHECK(strstr(s, "CELL_TYPES 2\n12\n10\n") != NULL);     /* hex then tet */
        CHECK(strstr(s, "VECTORS displacement float") != NULL);
        CHECK(strstr(s, "SCALARS Mises float 1") != NULL);
        free(s);
    }

    /* ---- VTK with the tet masked out: only the hex cell/type remain ---- */
    CHECK(cv_export_vtk(vtk, &f, NULL, NULL, NULL, vis));
    s = t_export_slurp(vtk);
    CHECK(s != NULL);
    if (s) {
        CHECK(strstr(s, "POINTS 12 float") != NULL);            /* points are never filtered */
        CHECK(strstr(s, "CELLS 1 9\n") != NULL);
        CHECK(strstr(s, "8 0 1 2 3 4 5 6 7\n") != NULL);
        CHECK(strstr(s, "CELL_TYPES 1\n12\n") != NULL);
        CHECK(strstr(s, "VECTORS displacement") == NULL);
        free(s);
    }

    /* ---- a lone hex20, to check the .frd -> VTK mid-node reorder ---- */
    cv_frd h;
    memset(&h, 0, sizeof h);
    h.n_nodes = 20;
    uint32_t hid[20]; float hxyz[60] = {0};
    for (int i = 0; i < 20; i++) hid[i] = (uint32_t)(i + 1);
    h.node_id = hid; h.xyz = hxyz;
    h.n_elems = 1;
    uint32_t heid[1] = { 1 };
    uint8_t  hety[1] = { 4 };            /* Hex20 */
    uint32_t hmat[1] = { 0 }, hgrp[1] = { 0 }, heoff[2] = { 0, 20 };
    uint32_t hconn[20];
    for (int i = 0; i < 20; i++) hconn[i] = (uint32_t)i;     /* dense index == position */
    h.elem_id = heid; h.etype = hety; h.emat = hmat; h.egrp = hgrp;
    h.eoff = heoff; h.conn = hconn;

    CHECK(cv_export_vtk(vtk, &h, NULL, NULL, NULL, NULL));
    s = t_export_slurp(vtk);
    CHECK(s != NULL);
    if (s) {
        /* .frd 12..15 (0-based, vertical mid-edges) must land at VTK slots 16..19;
           .frd 16..19 (top mid-edges) must land at VTK slots 12..15. */
        CHECK(strstr(s,
            "20 0 1 2 3 4 5 6 7 8 9 10 11 16 17 18 19 12 13 14 15\n") != NULL);
        free(s);
    }
}

#endif
