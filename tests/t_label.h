/* t_label.h -- labels: glyph layout, thinning, the coarse pass. Included by test_main.c. */
#ifndef CV_T_LABEL_H
#define CV_T_LABEL_H

/* a stub font: 'A' .. 'C' and '?', every glyph 8 x 10 px from the pen, advance 10 */
static cv_label_metrics tl_font(void) {
    static cv_label_glyph g[64];
    for (int i = 0; i < 64; i++) g[i] = (cv_label_glyph){ 10, 0, 0, 8, 10, 0.1f * i, 0, 0.1f * i + 0.05f, 0.5f };
    cv_label_metrics m = { g, 32, 64, 12, 0.99f, 0.99f };   /* codepoints 32 .. 95 */
    return m;
}

static void test_label(void) {
    cv_label_metrics m = tl_font();
    const float anchor[9] = { 1, 2, 3, 0.1f, 0.2f, 0.3f, 0, 0, 0 };
    cv_fvec gly = {0}, box = {0};

    /* layout: one quad per glyph, the pen advancing; the box over all of it */
    float w = cv_label_layout(&m, anchor, "AB", 4, -14, true, 2, 0.5f, &gly, &box);
    CHECK_NEAR(w, 20, 0);
    CHECK_EQ(gly.n, 2 * CV_LABEL_FLOATS);
    CHECK_EQ(box.n, CV_LABEL_FLOATS);
    const float* a = gly.a;
    CHECK_NEAR(a[0], 1, 0); CHECK_NEAR(a[4], 0.2f, 0);          /* pos, disp carried */
    CHECK_NEAR(a[9], 4, 0); CHECK_NEAR(a[10], -14, 0);          /* first glyph at dx, dy */
    CHECK_NEAR(a[11], 8, 0); CHECK_NEAR(a[12], 10, 0);          /* its size */
    CHECK_NEAR(a[13], 0.1f * ('A' - 32), 1e-6);                 /* its uv */
    CHECK_NEAR(a[CV_LABEL_FLOATS + 9], 14, 0);                  /* second glyph 10 px on */
    const float* b = box.a;
    CHECK_NEAR(b[9], 2, 0); CHECK_NEAR(b[10], -16, 0);          /* padded by 2 */
    CHECK_NEAR(b[11], 24, 0); CHECK_NEAR(b[12], 16, 0);         /* 20 + 4 wide, 12 + 4 high */
    CHECK_NEAR(b[13], 0.99f, 0); CHECK_NEAR(b[15], 0.99f, 0);   /* the white pixel */
    CHECK_NEAR(b[17], 0.5f, 0); CHECK_NEAR(a[17], 0.5f, 0);     /* the pull, on box and glyphs alike */
    box.n = 0;
    cv_label_leader(&m, anchor, 4, 30, 4, 0, 0.5f, &box);        /* a 1 px line from the point down 30 px */
    CHECK_EQ(box.n, (size_t)CV_LABEL_FLOATS);
    CHECK_NEAR(box.a[9], 3.5f, 0); CHECK_NEAR(box.a[10], 0.f, 0); CHECK_NEAR(box.a[11], 1.f, 0); CHECK_NEAR(box.a[12], 30.f, 0);
    CHECK_NEAR(box.a[13], 0.99f, 0); CHECK_NEAR(box.a[17], 0.5f, 0);
    CHECK_NEAR(cv_label_width(&m, "ABC"), 30, 0);
    box.n = 0;
    cv_label_leader(&m, anchor, 0, 0, -25, 0, 0.5f, &box);        /* level: one box, leftward */
    CHECK_EQ(box.n, (size_t)CV_LABEL_FLOATS);
    CHECK_NEAR(box.a[9], -25.f, 0); CHECK_NEAR(box.a[10], -0.5f, 0); CHECK_NEAR(box.a[11], 25.f, 0); CHECK_NEAR(box.a[12], 1.f, 0);
    box.n = 0;
    cv_label_leader(&m, anchor, 0, 0, 10, -5, 0.5f, &box);        /* a slant, wider than high: a row per pixel up */
    CHECK_EQ(box.n, (size_t)5 * CV_LABEL_FLOATS);
    {
        float x = 0;                                              /* the rows join end to end, 0 .. 10, each 1 px high */
        for (int k = 0; k < 5; k++) {
            const float* q = box.a + k * CV_LABEL_FLOATS;
            CHECK_NEAR(q[9], x, 1e-4); CHECK_NEAR(q[11], 2.f, 1e-4); CHECK_NEAR(q[12], 1.f, 0);
            CHECK_NEAR(q[10], -(k + 0.5f) - 0.5f, 1e-4);          /* row k centred on y = -(k + 0.5) */
            x += q[11];
        }
        CHECK_NEAR(x, 10.f, 1e-4);
    }
    box.n = 0;
    cv_label_leader(&m, anchor, 0, 0, -3, 40, 0.5f, &box);        /* steep: a column per pixel across */
    CHECK_EQ(box.n, (size_t)3 * CV_LABEL_FLOATS);
    CHECK_NEAR(box.a[11], 1.f, 0); CHECK_NEAR(box.a[12] * 3, 40.f, 1e-3);
    {
        float qx, qy;
        cv_label_nearest(10, 10, 20, 8, 0, 0, &qx, &qy);          /* above left: the corner */
        CHECK_NEAR(qx, 10, 0); CHECK_NEAR(qy, 10, 0);
        cv_label_nearest(10, 10, 20, 8, 15, 40, &qx, &qy);        /* below: the bottom edge */
        CHECK_NEAR(qx, 15, 0); CHECK_NEAR(qy, 18, 0);
    }

    /* moved labels: the sidecar's lines and the command line's form */
    {
        cv_label_off o;
        char t[128];
        CHECK(cv_label_off_parse("node 940 12 -30", &o));
        CHECK(!strcmp(o.kind, "node") && !strcmp(o.id, "940")); CHECK_NEAR(o.dx, 12, 0); CHECK_NEAR(o.dy, -30, 0);
        CHECK(cv_label_off_parse("  set  MY SET 2  1.5 -0.25\r\n", &o));   /* a name with spaces, a number in it */
        CHECK(!strcmp(o.kind, "set") && !strcmp(o.id, "MY SET 2")); CHECK_NEAR(o.dx, 1.5f, 0); CHECK_NEAR(o.dy, -0.25f, 0);
        cv_label_off_format(&o, t, sizeof t);
        CHECK(!strcmp(t, "set MY SET 2 1.5 -0.2") || !strcmp(t, "set MY SET 2 1.5 -0.3"));   /* a tenth of a pixel */
        CHECK(!cv_label_off_parse("node 940 12", &o));            /* no id before the numbers */
        CHECK(!cv_label_off_parse("node 940 x -30", &o));
        CHECK(!cv_label_off_parse("", &o));
        o = (cv_label_off){ "max", "2", 40, -12 };
        cv_label_off_format(&o, t, sizeof t);
        CHECK(!strcmp(t, "max 2 40 -12"));
        cv_label_off p2;
        CHECK(cv_label_off_parse(t, &p2) && !strcmp(p2.kind, "max") && !strcmp(p2.id, "2") && p2.dx == 40 && p2.dy == -12);
        CHECK(cv_label_off_parse_arg("node:940:12,-30", &o));
        CHECK(!strcmp(o.kind, "node") && !strcmp(o.id, "940")); CHECK_NEAR(o.dx, 12, 0); CHECK_NEAR(o.dy, -30, 0);
        CHECK(cv_label_off_parse_arg("gp:12:3:5,6", &o));         /* an id with a colon */
        CHECK(!strcmp(o.kind, "gp") && !strcmp(o.id, "12:3"));
        CHECK(!cv_label_off_parse_arg("node:940", &o));
        CHECK(!cv_label_off_parse_arg("node:940:12", &o));
        CHECK(!cv_label_off_parse_arg("node::1,2", &o));
        CHECK(!cv_label_off_parse_arg(":5:1,2", &o));
        CHECK(!cv_label_off_parse_arg("node:5:1,2x", &o));
    }

    /* the hit test: the topmost box under the point */
    {
        const float b[] = { 0, 0, 10, 10,   5, 5, 10, 10,   100, 100, 4, 4 };
        CHECK_EQ(cv_label_hit(b, 3, 2, 2), 0);
        CHECK_EQ(cv_label_hit(b, 3, 7, 7), 1);                    /* both hold it: the later one is on top */
        CHECK_EQ(cv_label_hit(b, 3, 14, 14), 1);
        CHECK_EQ(cv_label_hit(b, 3, 15, 15), -1);                 /* the far edge is outside */
        CHECK_EQ(cv_label_hit(b, 3, 50, 50), -1);
        CHECK_EQ(cv_label_hit(b, 0, 2, 2), -1);
    }

    /* layout: codepoints outside the table draw as ? */
    gly.n = box.n = 0;
    cv_label_layout(&m, anchor, "A\xc3\xa4", 0, 0, false, 0, 0, &gly, &box);   /* "Aä" */
    CHECK_EQ(gly.n, 2 * CV_LABEL_FLOATS);
    CHECK_NEAR(gly.a[CV_LABEL_FLOATS + 13], 0.1f * ('?' - 32), 1e-6);
    CHECK_EQ(box.n, 0);
    cv_free_vec(gly); cv_free_vec(box);

    /* thin: nearest first, one per box, off-screen and behind-the-eye dropped */
    cv_label_pt p[] = {
        { 100, 100, 0.5f, 0 },      /* a cluster: 1 is nearer than 0 and 2 */
        { 105, 102, 0.2f, 1 },
        { 112, 100, 0.7f, 2 },
        { 400, 400, 0.9f, 3 },      /* alone */
        { -5, 100, 0.1f, 4 },       /* off screen */
        { 200, 200, -1.f, 5 },      /* behind the eye: depth < 0 */
        { 200, 200, 2.f, 6 },       /* beyond the far plane */
    };
    uint32_t out[8];
    uint32_t n = cv_label_thin(p, 7, 0, 20, 20, 0, 0, 800, 600, out, 8);
    CHECK_EQ(n, 2);
    CHECK_EQ(out[0], 1); CHECK_EQ(out[1], 3);                   /* nearest first */
    /* thin: spacing 0 keeps every one inside */
    n = cv_label_thin(p, 7, 0, 0, 0, 0, 0, 800, 600, out, 8);
    CHECK_EQ(n, 4);
    /* thin: max_out caps */
    n = cv_label_thin(p, 7, 0, 0, 0, 0, 0, 800, 600, out, 1);
    CHECK_EQ(n, 1);
    /* thin: pinned points are taken first, in their order, whatever they overlap; the
       rest keep clear of them (3 and 4 overlap pinned 0 and 1; far 5 is kept) */
    cv_label_pt q[] = { { 100, 100, 0.9f, 0 }, { 102, 100, 0.95f, 1 }, { -5, 5, 0.5f, 2 }, { 105, 102, 0.2f, 3 },
                        { 112, 100, 0.1f, 4 }, { 400, 400, 0.9f, 5 } };
    n = cv_label_thin(q, 6, 3, 20, 20, 0, 0, 800, 600, out, 8);
    CHECK_EQ(n, 3);
    CHECK_EQ(out[0], 0); CHECK_EQ(out[1], 1); CHECK_EQ(out[2], 5);

    /* coarse: one per cell, the first met */
    const float xyz[] = { 0, 0, 0,  0.2f, 0.1f, 0,  5, 0, 0,  0.3f, 0, 0.4f };
    n = cv_label_coarse(xyz, 4, 1.f, out);
    CHECK_EQ(n, 2);
    CHECK_EQ(out[0], 0); CHECK_EQ(out[1], 2);

    /* thin: 200 000 anchors in well under a frame's budget (catches O(n^2), not noise) */
    {
        uint32_t N = 200000;
        cv_label_pt* big = malloc(N * sizeof *big);
        uint32_t* o = malloc(N * sizeof *o);
        unsigned s = 12345;
        for (uint32_t i = 0; i < N; i++) {
            s = s * 1103515245u + 12345u; float x = (float)(s >> 8 & 0xffff) / 65535.f;
            s = s * 1103515245u + 12345u; float y = (float)(s >> 8 & 0xffff) / 65535.f;
            s = s * 1103515245u + 12345u; float z = (float)(s >> 8 & 0xffff) / 65535.f;
            big[i] = (cv_label_pt){ x * 1600, y * 1000, z, i };
        }
        double t0 = cv_now();
        n = cv_label_thin(big, N, 0, 48, 16, 0, 0, 1600, 1000, o, N);
        double ms = (cv_now() - t0) * 1e3;
        printf("label thin: %u of %u in %.1f ms\n", n, N, ms);
        CHECK(n > 1000 && n < 4000);                            /* about one per 48 x 16 px box */
        CHECK(ms < 200);
        free(big); free(o);
    }

    {   /* the k smallest and largest: ordered, NaN and inf left out, a subset by ids, fewer than k */
        static const float v[] = { 5, NAN, 1, 9, 3, INFINITY, 7, -2 };
        uint32_t lo[3], hi[3];
        uint32_t m = cv_label_extremes(v, NULL, 8, 3, lo, hi);
        CHECK(m == 3);
        CHECK(lo[0] == 7 && lo[1] == 2 && lo[2] == 4);          /* -2 1 3 */
        CHECK(hi[0] == 3 && hi[1] == 6 && hi[2] == 0);          /* 9 7 5 */
        static const uint32_t ids[] = { 0, 4, 6 };
        m = cv_label_extremes(v, ids, 3, 5, lo, hi);
        CHECK(m == 3 && lo[0] == 4 && lo[2] == 6 && hi[0] == 6 && hi[2] == 4);
        CHECK(cv_label_extremes(v, NULL, 8, 0, lo, hi) == 0);
        static const float nan2[] = { NAN, NAN };
        CHECK(cv_label_extremes(nan2, NULL, 2, 2, lo, hi) == 0);
    }
}
#endif
