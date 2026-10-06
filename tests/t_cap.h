/* t_cap.h -- the filled cut of a plane through the solids (cap.c) */

static double tc_vol(const float* p, const int t[4]) {
    double a[3], b[3], c[3];
    for (int k = 0; k < 3; k++) { a[k] = p[3 * t[1] + k] - p[3 * t[0] + k]; b[k] = p[3 * t[2] + k] - p[3 * t[0] + k]; c[k] = p[3 * t[3] + k] - p[3 * t[0] + k]; }
    return (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6;
}

/* the cells of a solid fill it: their volumes add up to its own, all of one sign */
static void tc_cells(int type, bool mid, const float* corners, int nc, const int (*edge)[2], int ne, double want) {
    float p[27 * 3];
    memcpy(p, corners, (size_t)nc * 3 * sizeof(float));
    for (int i = 0; i < ne; i++) for (int k = 0; k < 3; k++) p[3 * (nc + i) + k] = 0.5f * (corners[3 * edge[i][0] + k] + corners[3 * edge[i][1] + k]);
    int np = cv_cap_points(type, mid, p, 3), tets[48][4], nt = cv_cap_cells(type, mid, tets, 48);
    CHECK(np <= 27 && nt > 0 && nt <= 48);
    double sum = 0;
    for (int i = 0; i < nt; i++) {
        double v = tc_vol(p, tets[i]);
        CHECK(v > 1e-6);                             /* none folded over, none flat */
        sum += v;
        for (int k = 0; k < 4; k++) CHECK(tets[i][k] >= 0 && tets[i][k] < np);
    }
    CHECK_NEAR(sum, want, 1e-4);
}

static double tc_area(const cv_cap_out* o) {
    double a = 0;
    for (size_t t = 0; t + 8 < o->pos.n; t += 9) {
        const float* p = o->pos.a + t;
        double u[3] = { p[3] - p[0], p[4] - p[1], p[5] - p[2] }, v[3] = { p[6] - p[0], p[7] - p[1], p[8] - p[2] };
        double c[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
        a += 0.5 * sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    }
    return a;
}

static void test_cap(void) {
    static const float hex[24] = { 0,0,0, 2,0,0, 2,3,0, 0,3,0, 0,0,5, 2,0,5, 2,3,5, 0,3,5 };
    static const int hex_e[12][2] = { {0,1}, {1,2}, {2,3}, {3,0}, {0,4}, {1,5}, {2,6}, {3,7}, {4,5}, {5,6}, {6,7}, {7,4} };
    static const float wed[18] = { 0,0,0, 2,0,0, 0,3,0, 0,0,5, 2,0,5, 0,3,5 };
    static const int wed_e[9][2] = { {0,1}, {1,2}, {2,0}, {0,3}, {1,4}, {2,5}, {3,4}, {4,5}, {5,3} };
    static const float tet[12] = { 0,0,0, 2,0,0, 0,3,0, 0,0,5 };
    static const int tet_e[6][2] = { {0,1}, {1,2}, {2,0}, {0,3}, {1,3}, {2,3} };
    tc_cells(1, false, hex, 8, hex_e, 0, 30);  tc_cells(4, true, hex, 8, hex_e, 12, 30);  tc_cells(4, false, hex, 8, hex_e, 12, 30);
    tc_cells(2, false, wed, 6, wed_e, 0, 15);  tc_cells(5, true, wed, 6, wed_e, 9, 15);
    tc_cells(3, false, tet, 4, tet_e, 0, 5);   tc_cells(6, true, tet, 4, tet_e, 6, 5);

    /* two C3D20 side by side along x (0..2, 2..4), 3 deep, 5 high; the field is z^2,
       which the element holds exactly and its corners alone do not */
    enum { NN = 40 };
    static float xyz[NN * 3], val[NN], disp[NN * 3];
    static uint32_t conn[40], eoff[3] = { 0, 20, 40 };
    static uint8_t et[2] = { 4, 4 };
    for (int e = 0; e < 2; e++) {
        for (int i = 0; i < 8; i++) for (int k = 0; k < 3; k++) xyz[3 * (20 * e + i) + k] = hex[3 * i + k] + (k == 0 ? 2.f * e : 0);
        for (int i = 0; i < 12; i++) for (int k = 0; k < 3; k++)
            xyz[3 * (20 * e + 8 + i) + k] = 0.5f * (xyz[3 * (20 * e + hex_e[i][0]) + k] + xyz[3 * (20 * e + hex_e[i][1]) + k]);
        for (int i = 0; i < 20; i++) conn[20 * e + i] = (uint32_t)(20 * e + i);
    }
    for (int i = 0; i < NN; i++) { val[i] = xyz[3 * i + 2] * xyz[3 * i + 2]; disp[3 * i] = 0.1f * xyz[3 * i]; disp[3 * i + 1] = disp[3 * i + 2] = 0; }
    cv_frd f;
    memset(&f, 0, sizeof f);
    f.xyz = xyz; f.n_nodes = NN; f.conn = conn; f.eoff = eoff; f.etype = et; f.n_elems = 2;

    for (int mid = 0; mid < 2; mid++) {
        cv_cap_model m = { .f = &f, .n = { 1, 0, 0 }, .mid = mid };
        cv_cap_prep p;
        CHECK(cv_cap_prepare(&p, &m));
        CHECK_NEAR(p.lo[0], 0, 0); CHECK_NEAR(p.hi[0], 2, 0); CHECK_NEAR(p.lo[1], 2, 0); CHECK_NEAR(p.hi[1], 4, 0);
        cv_cap_out o = {0};
        cv_cap_cut(&m, &p, 0.7f, 0, val, NULL, &o);
        CHECK_NEAR(tc_area(&o), 15.0, 1e-4);         /* the whole cross-section, once: only the first brick is cut */
        CHECK_EQ(o.disp.n, 0);
        CHECK_EQ(o.val.n * 3, o.pos.n);
        double worst = 0;
        for (size_t i = 0; i < o.val.n; i++) {
            CHECK_NEAR(o.pos.a[3 * i], 0.7, 1e-5);
            double z = o.pos.a[3 * i + 2];
            worst = fmax(worst, fabs(o.val.a[i] - z * z));
        }
        /* through the mid nodes the cut knows z^2 at z = 0, 2.5, 5 and is linear between
           (off by at most 2.5^2 / 4); over the corners it is a straight line from 0 to 25 */
        if (mid) CHECK(worst < 1.6); else CHECK(worst > 4.0);
        cv_cap_out_free(&o);

        cv_cap_cut(&m, &p, 2.0f, 0, NULL, NULL, &o); /* on the shared face: the brick that starts there, once */
        CHECK_NEAR(tc_area(&o), 15.0, 1e-4);
        cv_cap_out_free(&o);
        cv_cap_cut(&m, &p, 4.5f, 0, NULL, NULL, &o); /* beside the model */
        CHECK_EQ(o.pos.n, 0);
        cv_cap_out_free(&o);
        cv_cap_prep_free(&p);

        uint8_t vis[2] = { 0, 1 };                   /* a hidden element is not cut */
        m.vis = vis;
        CHECK(cv_cap_prepare(&p, &m));
        cv_cap_cut(&m, &p, 0.7f, 0, NULL, NULL, &o);
        CHECK_EQ(o.pos.n, 0);
        cv_cap_cut(&m, &p, 3.0f, 0, NULL, val, &o);  /* per-element colours: val[1] for element 1 */
        CHECK_NEAR(tc_area(&o), 15.0, 1e-4);
        for (size_t i = 0; i < o.val.n; i++) CHECK_NEAR(o.val.a[i], val[1], 0);
        cv_cap_out_free(&o);
        cv_cap_prep_free(&p);

        /* deformed 1.1 x along x: the plane at 0.77 meets the undeformed 0.7; a vertex
           carries the undeformed place and the displacement that takes it to the plane */
        m.vis = NULL; m.disp = disp; m.f1 = 1;
        CHECK(cv_cap_prepare(&p, &m));
        cv_cap_cut(&m, &p, 0.77f, 0, val, NULL, &o);
        CHECK_EQ(o.disp.n, o.pos.n);
        for (size_t i = 0; i < o.val.n; i++) {
            CHECK_NEAR(o.pos.a[3 * i], 0.7, 1e-5);
            CHECK_NEAR(o.pos.a[3 * i] + o.disp.a[3 * i], 0.77, 1e-5);
        }
        cv_cap_out_free(&o);
        cv_cap_prep_free(&p);
    }

    {   /* the eye's plane: a point nearer than depth along the view is cut, one beyond is kept */
        const float eye[3] = { 1, 2, 3 }, fwd[3] = { 0, 0, -2 };
        float n[3], d;
        cv_cap_eye_plane(eye, fwd, 0.5f, n, &d);
        const float near_p[3] = { 5, -4, 2.8f }, far_p[3] = { -5, 4, 2.4f }, behind[3] = { 1, 2, 4 };
        CHECK(n[0] * near_p[0] + n[1] * near_p[1] + n[2] * near_p[2] > d);
        CHECK(n[0] * behind[0] + n[1] * behind[1] + n[2] * behind[2] > d);
        CHECK(n[0] * far_p[0] + n[1] * far_p[1] + n[2] * far_p[2] < d);
        CHECK_NEAR(n[0] * n[0] + n[1] * n[1] + n[2] * n[2], 1.0, 1e-6);
        CHECK_NEAR(n[2] * 2.5f, d, 1e-6);               /* the plane passes 0.5 ahead of the eye */
    }
}
