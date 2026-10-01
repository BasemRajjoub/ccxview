/* t_anchor.h -- unit tests for src/anchor.h (legend / gizmo corner anchors). */
#ifndef CV_T_ANCHOR_H
#define CV_T_ANCHOR_H

#include "../src/anchor.h"

static void test_anchor(void) {
    cv_anchor a = { true, CV_BR, 1, 1 };
    CHECK(cv_anchor_parse("tr", &a, 12));
    CHECK(a.set); CHECK_EQ(a.corner, CV_TR); CHECK_NEAR(a.dx, 12, 0); CHECK_NEAR(a.dy, 12, 0);
    CHECK(cv_anchor_parse("bl,30,5.5", &a, 12));
    CHECK_EQ(a.corner, CV_BL); CHECK_NEAR(a.dx, 30, 0); CHECK_NEAR(a.dy, 5.5, 1e-6);
    CHECK(cv_anchor_parse("auto", &a, 12)); CHECK(!a.set);
    a.set = true;
    CHECK(cv_anchor_parse("", &a, 12)); CHECK(!a.set);
    /* garbage leaves the anchor alone */
    a = (cv_anchor){ true, CV_BR, 3, 4 };
    CHECK(!cv_anchor_parse("middle", &a, 12));
    CHECK(!cv_anchor_parse("tr,5", &a, 12));
    CHECK(!cv_anchor_parse("tr,-5,3", &a, 12));
    CHECK(!cv_anchor_parse("tr,5,3x", &a, 12));
    CHECK(a.set && a.corner == CV_BR && a.dx == 3 && a.dy == 4);

    char t[32];
    cv_anchor_format(&a, t, sizeof t);
    CHECK(!strcmp(t, "br,3,4"));
    cv_anchor b;
    CHECK(cv_anchor_parse(t, &b, 0) && b.corner == CV_BR && b.dx == 3 && b.dy == 4);
    cv_anchor_format(&(cv_anchor){ 0 }, t, sizeof t);
    CHECK(!strcmp(t, "auto"));

    /* placed in each corner, at scale 2; then the view grows and the box stays in its corner */
    cv_box view = { 100, 50, 800, 600 };
    cv_box p = cv_anchor_place((cv_anchor){ true, CV_TL, 10, 20 }, view, 100, 50, 2);
    CHECK_NEAR(p.x, 120, 0); CHECK_NEAR(p.y, 90, 0);
    p = cv_anchor_place((cv_anchor){ true, CV_BR, 10, 20 }, view, 100, 50, 2);
    CHECK_NEAR(p.x, 100 + 800 - 100 - 20, 0); CHECK_NEAR(p.y, 50 + 600 - 50 - 40, 0);
    cv_box big = { 100, 50, 1600, 900 };
    cv_box q = cv_anchor_place((cv_anchor){ true, CV_BR, 10, 20 }, big, 100, 50, 2);
    CHECK_NEAR(big.x + big.w - (q.x + q.w), 20, 0); CHECK_NEAR(big.y + big.h - (q.y + q.h), 40, 0);
    /* a gap larger than the view: clamped inside */
    p = cv_anchor_place((cv_anchor){ true, CV_TR, 5000, 5000 }, view, 100, 50, 1);
    CHECK_NEAR(p.x, 100, 0); CHECK_NEAR(p.y, 50 + 600 - 50, 0);

    /* dropped boxes: nearest corner, and placing it again gives the same box */
    cv_box drops[4] = { { 130, 70, 100, 50 }, { 700, 60, 100, 50 }, { 110, 500, 100, 50 }, { 760, 560, 100, 50 } };
    for (int i = 0; i < 4; i++) {
        cv_anchor d = cv_anchor_from_box(drops[i], view, 2);
        CHECK_EQ(d.corner, i);
        cv_box back = cv_anchor_place(d, view, 100, 50, 2);
        CHECK_NEAR(back.x, drops[i].x, 1e-3); CHECK_NEAR(back.y, drops[i].y, 1e-3);
    }

    /* overlap guard: legend top-right 440 tall, gizmo bottom-right in a 578 tall view */
    cv_box leg = { 1200, 60, 150, 440 }, giz = { 1230, 60 + 578 - 132, 120, 120 };
    cv_box f = cv_legend_fit(leg, giz, 10, 100);
    CHECK_NEAR(f.y, 60, 0); CHECK_NEAR(f.y + f.h, giz.y - 10, 1e-3);
    /* the legend below the gizmo keeps its bottom edge */
    cv_box leg2 = { 1200, 150, 150, 440 }, giz2 = { 1230, 70, 120, 120 };
    f = cv_legend_fit(leg2, giz2, 10, 100);
    CHECK_NEAR(f.y, 200, 1e-3); CHECK_NEAR(f.y + f.h, 590, 1e-3);
    /* no overlap (gizmo bottom-left): unchanged */
    f = cv_legend_fit(leg, (cv_box){ 110, 500, 120, 120 }, 10, 100);
    CHECK_NEAR(f.h, 440, 0);
    /* too little room left: unchanged */
    f = cv_legend_fit(leg, (cv_box){ 1230, 250, 120, 120 }, 10, 300);
    CHECK_NEAR(f.h, 440, 0);
}

#endif
