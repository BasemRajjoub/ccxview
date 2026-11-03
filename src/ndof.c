/* ndof.c -- see ndof.h. The only backend is a stub: no device. */
#include "ndof.h"
#include <math.h>

bool cv_ndof_open(void) { return false; }
bool cv_ndof_poll(cv_ndof_state* s) { (void)s; return false; }
void cv_ndof_close(void) {}

enum { DEAD_PCT = 3 };                    /* % of travel */

static float dead(float v) {
    const float z = DEAD_PCT * 0.01f;
    float a = fabsf(v);
    if (!(a > z)) return 0.f;             /* also NaN */
    a = fminf((a - z) / (1.f - z), 1.f);  /* continuous at the edge of the zone */
    return v < 0 ? -a : a;
}

bool cv_ndof_move_of(const cv_ndof_state* s, float dt, float dist, float speed, cv_ndof_move* m) {
    float t[3], r[3];
    for (int i = 0; i < 3; i++) { t[i] = dead(s->t[i]); r[i] = dead(s->r[i]); }
    const float k = dt * speed;
    m->pan[0] = -t[0] * dist * k;         /* the model follows the cap: the camera goes the other way */
    m->pan[1] = -t[1] * dist * k;
    m->zoom = expf(-t[2] * k);            /* toward the user: the model nearer, the distance shorter */
    const float w = 1.5f * k;
    m->pitch = r[0] * w; m->yaw = -r[1] * w; m->roll = r[2] * w;
    return t[0] != 0 || t[1] != 0 || t[2] != 0 || r[0] != 0 || r[1] != 0 || r[2] != 0;
}
