#include "../src/ndof.h"
/* t_ndof.h -- the 3D mouse's rate scaling with a fake state (ndof.c). Included by test_main.c. */

static void test_ndof(void) {
    cv_ndof_state s = { { 0.5f, -0.5f, 0.f }, { 0.f, 0.f, 0.f }, 0 };
    cv_ndof_move a, b;
    CHECK(!cv_ndof_open());                           /* the stub: no device */
    cv_ndof_state z;
    CHECK(!cv_ndof_poll(&z));
    CHECK(cv_ndof_move_of(&s, 0.1f, 10.f, 1.f, &a));
    CHECK(cv_ndof_move_of(&s, 0.1f, 5000.f, 1.f, &b));
    CHECK_NEAR(b.pan[0] / a.pan[0], 500.0, 1e-3);     /* rates proportional to the distance: any size of model feels alike */
    CHECK_NEAR(a.pan[0], -0.5 * 10 * 0.1 * (0.5 - 0.03) / 0.97 / 0.5, 1e-4);   /* the model follows the cap */
    CHECK(a.pan[0] < 0 && a.pan[1] > 0);
    CHECK_NEAR(a.zoom, 1.0, 1e-9);                    /* no push in depth: the distance holds */
    cv_ndof_move c;
    CHECK(cv_ndof_move_of(&s, 0.2f, 10.f, 1.f, &c));
    CHECK_NEAR(c.pan[0] / a.pan[0], 2.0, 1e-5);       /* twice the time, twice the way */
    CHECK(cv_ndof_move_of(&s, 0.1f, 10.f, 3.f, &c));
    CHECK_NEAR(c.pan[0] / a.pan[0], 3.0, 1e-5);       /* the sensitivity */
    cv_ndof_state d = { { 0.02f, 0.f, 0.f }, { 0.f, -0.02f, 0.f }, 0 };
    CHECK(!cv_ndof_move_of(&d, 0.1f, 10.f, 1.f, &c)); /* inside the dead zone: still */
    cv_ndof_state p = { { 0.f, 0.f, 1.f }, { 1.f, 1.f, 1.f }, 0 };
    CHECK(cv_ndof_move_of(&p, 1.f, 10.f, 1.f, &c));
    CHECK_NEAR(c.zoom, exp(-1.0), 1e-6);              /* pulled toward the user: nearer */
    CHECK_NEAR(c.pitch, 1.5, 1e-6);
    CHECK_NEAR(c.yaw, -1.5, 1e-6);
    CHECK_NEAR(c.roll, 1.5, 1e-6);
    int16_t raw[6] = { 350, 175, -700, 350, 0, -350 };   /* TX TY TZ RX RY RZ; beyond full travel clamps */
    cv_ndof_state r;
    cv_ndof_from_raw(raw, &r);
    CHECK_NEAR(r.t[0], 1.0, 1e-6); CHECK_NEAR(r.t[1], 1.0, 1e-6); CHECK_NEAR(r.t[2], 0.5, 1e-6);
    CHECK_NEAR(r.r[0], -1.0, 1e-6); CHECK_NEAR(r.r[1], -1.0, 1e-6); CHECK_NEAR(r.r[2], 0.0, 1e-6);
}
