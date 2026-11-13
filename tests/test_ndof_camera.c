/* End-to-end raw axes -> options -> actual camera, without a window or device. */
#include "../src/app_int.h"
#include <assert.h>
#include <stdio.h>

cv_app G;
static int wakes;
void app_wake(void) { wakes++; }
double cv_now(void) { return 0; }

static void near(v3 a, v3 b) {
    assert(fabsf(a.x - b.x) < 2e-5f);
    assert(fabsf(a.y - b.y) < 2e-5f);
    assert(fabsf(a.z - b.z) < 2e-5f);
}

static v3 turned(v3 v, v3 axis, float angle) {
    return v3_add(v3_add(v3_scale(v, cosf(angle)), v3_scale(v3_cross(axis, v), sinf(angle))),
                  v3_scale(axis, v3_dot(axis, v) * (1 - cosf(angle))));
}

static void setup(float pitch, bool up_z, bool free_mode, float roll) {
    memset(&G, 0, sizeof G);
    G.loaded = true;
    G.diag = 1;
    G.cam.target = v3_make(1, 2, 3);
    G.cam.dist = 10;
    G.cam.yaw = .7f;
    G.cam.pitch = pitch;
    G.up_z = up_z;
    G.ndof_speed = 1;
    G.ndof_dead = 3;
    if (free_mode) {
        app_set_orbit_free(true);
        v3 e, f, r, u;
        cam_basis(&G.cam, &e, &f, &r, &u);
        cam_turn(f, roll, G.cam.target);
    }
}

static void rotation(float pitch, bool up_z, bool free_mode, float roll,
                     int rx, int ry, int rz, int flip, bool pivot_on) {
    setup(pitch, up_z, free_mode, roll);
    if (flip >= 0) G.ndof_flip[flip] = true;
    G.orbit_pivot_on = pivot_on;
    G.orbit_pivot = v3_make(-2, 1, 0);
    v3 e0, f0, r0, u0, e1, f1, r1, u1;
    cam_basis(&G.cam, &e0, &f0, &r0, &u0);
    v3 target = G.cam.target, pivot = pivot_on ? G.orbit_pivot : target;
    int16_t raw[6] = { 0, 0, 0, 350 * rx, 350 * ry, 350 * rz };
    cv_ndof_state state = {0};
    cv_ndof_from_raw(raw, &state);
    /* SolidCraft object-mode camera vector in screen right/up/back: (-RX, RZ, -RY). */
    v3 local = v3_make(-rx * (flip == 3 ? -1 : 1),
                       rz * (flip == 4 ? -1 : 1),
                      -ry * (flip == 5 ? -1 : 1));
    float length = sqrtf(v3_dot(local, local));
    v3 axis = v3_scale(v3_add(v3_add(v3_scale(r0, local.x), v3_scale(u0, local.y)),
                            v3_scale(f0, -local.z)), 1 / length);
    float angle = length * 1.5f * .02f;
    assert(app_ndof_apply(&state, .02f));
    assert(G.orbit_free);
    cam_basis(&G.cam, &e1, &f1, &r1, &u1);
    near(r1, turned(r0, axis, angle));
    near(u1, turned(u0, axis, angle));
    near(f1, turned(f0, axis, angle));
    near(e1, v3_add(pivot, turned(v3_sub(e0, pivot), axis, angle)));
    near(G.cam.target, v3_add(pivot, turned(v3_sub(target, pivot), axis, angle)));
    assert(fabsf(v3_dot(r1, u1)) < 1e-5f);
    assert(fabsf(G.cam.dist - 10) < 1e-6f);
}

int main(void) {
    const float pitches[] = { 0, .6f, 1.57079633f, -1.57079633f, 2.2f };
    const int axes[][3] = { {1,0,0}, {0,1,0}, {0,0,1}, {-1,0,0},
                           {0,-1,0}, {0,0,-1}, {1,1,1}, {1,-1,1} };
    for (size_t p = 0; p < CV_COUNT(pitches); p++)
        for (size_t a = 0; a < CV_COUNT(axes); a++)
            for (int z = 0; z < 2; z++)
                for (int free_mode = 0; free_mode < 2; free_mode++)
                    for (int pivot = 0; pivot < 2; pivot++)
                        for (int flip = 2; flip < 6; flip++)
                            rotation(pitches[p], z, free_mode, free_mode ? 1.3f : 0,
                                     axes[a][0], axes[a][1], axes[a][2], flip, pivot);

    setup(.6f, false, false, 0);
    cv_camera before = G.cam;
    cv_ndof_state state = {0};
    assert(!app_ndof_apply(&state, .02f) && !G.orbit_free);
    state.r[0] = .01f;
    assert(!app_ndof_apply(&state, .02f) && !G.orbit_free);
    near(G.cam.target, before.target);
    state.r[0] = state.r[1] = state.r[2] = 1;
    G.ndof_lock_rot = true;
    assert(!app_ndof_apply(&state, .02f) && !G.orbit_free);
    G.ndof_lock_rot = false;
    G.loaded = false;
    assert(!app_ndof_apply(&state, .02f) && !G.orbit_free);
    G.loaded = true;
    memset(&state, 0, sizeof state);
    state.t[0] = 1;
    assert(app_ndof_apply(&state, .02f) && !G.orbit_free);
    v3 e, f, r, u;
    cam_basis(&before, &e, &f, &r, &u);
    near(G.cam.target, v3_add(before.target, v3_scale(r, -.2f)));
    G.ndof_lock_pan = true;
    assert(!app_ndof_apply(&state, .02f));

    setup(.6f, false, false, 0);
    state = (cv_ndof_state){ {0,0,0}, {.2f,.8f,-.3f}, 0 };
    G.ndof_dominant = true;
    assert(app_ndof_apply(&state, .02f));
    cv_camera dominant = G.cam;
    setup(.6f, false, false, 0);
    state.r[0] = state.r[2] = 0;
    assert(app_ndof_apply(&state, .02f));
    near(G.cam.fdir, dominant.fdir);
    near(G.cam.fup, dominant.fup);

    setup(.6f, false, false, 0);
    before = G.cam;
    state = (cv_ndof_state){ {1,1,1}, {1,0,0}, 0 };
    G.ndof_lock_pan = true;
    assert(app_ndof_apply(&state, .02f) && G.orbit_free);
    near(G.cam.target, before.target);
    assert(G.cam.dist == before.dist);

    setup(.6f, false, true, .8f);
    state = (cv_ndof_state){ {0,0,0}, {.4f,-.5f,.6f}, 0 };
    for (int i = 0; i < 10; i++) assert(app_ndof_apply(&state, .01f));
    cv_camera repeated = G.cam;
    setup(.6f, false, true, .8f);
    assert(app_ndof_apply(&state, .1f));
    near(G.cam.fdir, repeated.fdir);
    near(G.cam.fup, repeated.fup);
    assert(wakes > 0);
    puts("ndof camera: raw axes, signs, screen frame, poles, roll, pivots, flips, locks and frame steps OK");
    return 0;
}
