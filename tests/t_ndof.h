#include "../src/ndof.h"
#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
/* t_ndof.h -- the 3D mouse's rate scaling with a fake state (ndof.c). Included by test_main.c. */

static void test_ndof(void) {
    cv_ndof_state s = { { 0.5f, -0.5f, 0.f }, { 0.f, 0.f, 0.f }, 0 };
    cv_ndof_move a, b;
    cv_ndof_state z;
    cv_ndof_open();                                   /* no device here (or none of the backend): nothing to report */
    CHECK(!cv_ndof_poll(&z));
    cv_ndof_close();
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
    {   /* options: flips, zoom axis, locks, dominant axis, dead zone */
        cv_ndof_opt o, od;
        cv_ndof_move u, w;
        cv_ndof_state f = { { 0.5f, 0.4f, 0.8f }, { 0.3f, -0.6f, 0.2f }, 0 };
        cv_ndof_opt_default(&od);
        CHECK(cv_ndof_move_opt(&f, 0.1f, 10.f, &od, &u));
        cv_ndof_move_of(&f, 0.1f, 10.f, 1.f, &w);
        CHECK(u.pan[0] == w.pan[0] && u.zoom == w.zoom && u.yaw == w.yaw);   /* defaults: today's behaviour */
        const int mv[6] = { 0, 1, 2, 3, 4, 5 };
        for (int k = 0; k < 6; k++) {
            o = od; o.flip[mv[k]] = true;
            CHECK(cv_ndof_move_opt(&f, 0.1f, 10.f, &o, &w));
            CHECK_NEAR(w.pan[0], k == 0 ? -u.pan[0] : u.pan[0], 1e-6);
            CHECK_NEAR(w.pan[1], k == 1 ? -u.pan[1] : u.pan[1], 1e-6);
            CHECK_NEAR(w.zoom, k == 2 ? 1.0 / u.zoom : u.zoom, 1e-6);
            CHECK_NEAR(w.pitch, k == 3 ? -u.pitch : u.pitch, 1e-6);
            CHECK_NEAR(w.yaw, k == 4 ? -u.yaw : u.yaw, 1e-6);
            CHECK_NEAR(w.roll, k == 5 ? -u.roll : u.roll, 1e-6);
        }
        o = od; o.zoom_lift = true;                   /* the lift axis zooms, forward / back pans vertically */
        CHECK(cv_ndof_move_opt(&f, 0.1f, 10.f, &o, &w));
        CHECK_NEAR(w.zoom, exp(-0.1 * (0.4 - 0.03) / 0.97), 1e-6);
        CHECK_NEAR(w.pan[1], 10 * 0.1 * (0.8 - 0.03) / 0.97, 1e-6);
        CHECK_NEAR(w.pan[0], u.pan[0], 1e-6);
        o = od; o.lock_rot = true;
        CHECK(cv_ndof_move_opt(&f, 0.1f, 10.f, &o, &w));
        CHECK(w.pitch == 0 && w.yaw == 0 && w.roll == 0 && w.pan[0] == u.pan[0]);
        o = od; o.lock_pan = true;
        CHECK(cv_ndof_move_opt(&f, 0.1f, 10.f, &o, &w));
        CHECK(w.pan[0] == 0 && w.pan[1] == 0 && w.zoom == 1.f && w.yaw == u.yaw);
        o = od; o.lock_pan = o.lock_rot = true;
        CHECK(!cv_ndof_move_opt(&f, 0.1f, 10.f, &o, &w));
        o = od; o.dominant = true;                    /* the zoom (0.8) is the largest */
        CHECK(cv_ndof_move_opt(&f, 0.1f, 10.f, &o, &w));
        CHECK(w.pan[0] == 0 && w.pan[1] == 0 && w.yaw == 0 && w.pitch == 0 && w.roll == 0 && w.zoom == u.zoom);
        o = od; o.dead = 0.65f;                       /* a wide dead zone: only the zoom is outside it */
        CHECK(cv_ndof_move_opt(&f, 0.1f, 10.f, &o, &w));
        CHECK(w.pan[0] == 0 && w.yaw == 0 && w.zoom < 1.f);
        o.dead = 0.9f;
        CHECK(!cv_ndof_move_opt(&f, 0.1f, 10.f, &o, &w));
        o = od; o.dead = 0.f;
        cv_ndof_state tiny = { { 0.01f, 0, 0 }, { 0, 0, 0 }, 0 };
        CHECK(cv_ndof_move_opt(&tiny, 0.1f, 10.f, &o, &w));   /* no dead zone: even a touch moves */
    }
    {   /* turning about a pivot keeps it where it was in the view */
        v3 pv = v3_make(1.f, 2.f, 0.5f), eye0 = v3_make(0, 0, 10);
        v3 f0 = v3_make(0, 0, -1), r0 = v3_make(1, 0, 0), u0 = v3_make(0, 1, 0);
        float a = 0.7f, c = cosf(a), sn = sinf(a);     /* turned about up */
        v3 r1 = v3_make(c, 0, -sn), f1 = v3_make(-sn, 0, -c), u1 = u0;
        v3 tg = cv_pivot_target(pv, eye0, r0, u0, f0, r1, u1, f1, 10.f);
        v3 eye1 = v3_sub(tg, v3_scale(f1, 10.f));
        v3 q0 = v3_sub(pv, eye0), q1 = v3_sub(pv, eye1);
        CHECK_NEAR(v3_dot(q0, r0), v3_dot(q1, r1), 1e-5);
        CHECK_NEAR(v3_dot(q0, u0), v3_dot(q1, u1), 1e-5);
        CHECK_NEAR(v3_dot(q0, f0), v3_dot(q1, f1), 1e-5);
    }
    int16_t raw[6] = { 350, 175, -700, 350, 0, -350 };   /* TX TY TZ RX RY RZ; beyond full travel clamps */
    cv_ndof_state r;
    cv_ndof_from_raw(raw, &r);
    CHECK_NEAR(r.t[0], 1.0, 1e-6); CHECK_NEAR(r.t[1], 1.0, 1e-6); CHECK_NEAR(r.t[2], 0.5, 1e-6);
    CHECK_NEAR(r.r[0], -1.0, 1e-6); CHECK_NEAR(r.r[1], -1.0, 1e-6); CHECK_NEAR(r.r[2], 0.0, 1e-6);
#ifdef __linux__
    {   /* a fake spacenavd: events as libspnav reads them, 8 int32 each */
        const char* path = "build/fake_spnav.sock";
        unlink(path);
        int ls = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un ad; memset(&ad, 0, sizeof ad);
        ad.sun_family = AF_UNIX; strcpy(ad.sun_path, path);
        CHECK(bind(ls, (struct sockaddr*)&ad, sizeof ad) == 0 && listen(ls, 1) == 0);
        cv_ndof_set_socket(path);
        CHECK(cv_ndof_open());
        int c = accept(ls, NULL, NULL);
        CHECK(c >= 0);
        cv_ndof_state q;
        CHECK(!cv_ndof_poll(&q));                         /* connected, silent */
        int32_t m[8] = { 0, 350, 175, 350, 350, 0, -350, 8 };   /* motion x y z rx ry rz period */
        int32_t b[8] = { 1, 2, 0, 0, 0, 0, 0, 0 };              /* button 2 pressed */
        CHECK(write(c, m, 32) == 32 && write(c, b, 32) == 32);
        CHECK(cv_ndof_poll(&q));
        CHECK_NEAR(q.t[0], 1.0, 1e-6); CHECK_NEAR(q.t[1], 0.5, 1e-6); CHECK_NEAR(q.t[2], -1.0, 1e-6);
        CHECK_NEAR(q.r[0], -1.0, 1e-6); CHECK_NEAR(q.r[1], 0.0, 1e-6); CHECK_NEAR(q.r[2], -1.0, 1e-6);
        CHECK(q.buttons == 4u);
        CHECK(cv_ndof_poll(&q));                          /* still held, no new event */
        CHECK_NEAR(q.t[0], 1.0, 1e-6);
        b[0] = 2;
        CHECK(write(c, b, 16) == 16);                     /* half an event: waits */
        CHECK(cv_ndof_poll(&q) && q.buttons == 4u);
        CHECK(write(c, (char*)b + 16, 16) == 16);
        CHECK(cv_ndof_poll(&q) && q.buttons == 0u);       /* released */
        usleep(300000);                                   /* no zero event, 0.2 s of silence: let go */
        CHECK(cv_ndof_poll(&q));
        CHECK_NEAR(q.t[0], 0.0, 1e-9); CHECK_NEAR(q.r[2], 0.0, 1e-9);
        CHECK(!cv_ndof_poll(&q));
        close(c);                                         /* the daemon goes: one rest state, then nothing */
        CHECK(cv_ndof_poll(&q) && q.buttons == 0u);
        CHECK(!cv_ndof_poll(&q));
        cv_ndof_close();
        close(ls); unlink(path);
        cv_ndof_set_socket(NULL);
    }
#endif
}
