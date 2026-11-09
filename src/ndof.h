/* ndof.h -- 3D mouse (SpaceMouse, 6DOF) input (#41). Headless: the device layer
   gives a state, cv_ndof_move turns it into one frame's camera motion.

   Backends, each behind the same three calls (untested on a device so far):
     Windows  (done) Raw Input (WM_INPUT, HID usage page 0x01, usage 0x08) on sapp_win32_get_hwnd()
     Linux    (done) spacenavd (FreeSpacenav): its socket protocol spoken directly
     macOS    (done) 3Dconnexion's 3DconnexionClient.framework, loaded at runtime when installed
     Web      none: WebHID gets no input while 3DxWare runs */
#ifndef CV_NDOF_H
#define CV_NDOF_H

#include "base.h"
#include "vmath.h"

/* What the cap does, each axis -1..1 of its full travel: t right, up, toward the
   user; r tilt about x (nose down +), twist about y (right turn +), about z (roll,
   clockwise seen from the user +). buttons: a bit per button. */
typedef struct { float t[3], r[3]; unsigned buttons; } cv_ndof_state;

bool cv_ndof_open(void);                  /* false: no device (or no backend) */
bool cv_ndof_poll(cv_ndof_state* s);      /* false: nothing new / no device; true: s is the cap now */
void cv_ndof_close(void);
void cv_ndof_clear(void);                 /* drop held axes and buttons (the window lost focus); the next poll reports the rest */
/* What is known of the device; honest on macOS (driver framework loaded and client registered;
   a device reported; input received), elsewhere only NO_DRIVER / DRIVER / DEVICE for "nothing /
   backend up / daemon connected". Debug: CCXVIEW_NDOF_DEBUG=1 prints to stderr. */
typedef enum { CV_NDOF_NO_DRIVER, CV_NDOF_DRIVER, CV_NDOF_DEVICE, CV_NDOF_INPUT } cv_ndof_status;
cv_ndof_status cv_ndof_get_status(void);
int cv_ndof_version_cmp(const char* a, const char* b);   /* dotted versions: <0, 0, >0 */
/* roll asked for in turntable mode: *held accumulates seconds of |roll| above the dead zone
   (fraction of travel); true once sustained (0.4 s) */
bool cv_ndof_roll_hint(float* held, const cv_ndof_state* s, float dead, float dt);
#ifdef __linux__
void cv_ndof_set_socket(const char* path);   /* test hook: try only this AF_UNIX path (NULL: the usual ones) */
#endif

/* raw driver axes TX TY TZ RX RY RZ (+-350 = full travel) to a state; buttons left alone */
void cv_ndof_from_raw(const int16_t axis[6], cv_ndof_state* s);

/* One frame of camera motion from a state: the model follows the cap (object mode).
   Rates are proportional to the distance to the pivot (dist), so a 10 um part and a
   50 m structure move alike: pan in world lengths along the view's right and up,
   zoom as the factor for the distance, angles in radians. speed scales everything
   (1: a full push crosses about its own distance in a second, a full twist turns
   ~1.5 rad/s). A dead zone leaves the model still. false: all within it. */
typedef struct { float pan[2], zoom, yaw, pitch, roll; } cv_ndof_move;
bool cv_ndof_move_of(const cv_ndof_state* s, float dt, float dist, float speed, cv_ndof_move* m);

/* The same with the user's options (the driver's own settings do not reach us). Applied in
   order: dead zone, which axis zooms (zoom_lift: the cap lifted / pushed down zooms and
   forward / back pans vertically, instead of the reverse), flips, locks, dominant axis.
   flip: pan x, pan y, zoom, tilt, spin, roll. dead: fraction of travel (0.03 default). */
typedef struct {
    float speed, dead;
    bool zoom_lift, flip[6], lock_rot, lock_pan, dominant;
} cv_ndof_opt;
void cv_ndof_opt_default(cv_ndof_opt* o);
bool cv_ndof_move_opt(const cv_ndof_state* s, float dt, float dist, const cv_ndof_opt* o, cv_ndof_move* m);

/* Turning about a pivot: the target that keeps pivot where it was in the view. The view
   before the turn is eye0 with right r0, up u0, forward f0; after it r1 u1 f1 and dist. */
v3 cv_pivot_target(v3 pivot, v3 eye0, v3 r0, v3 u0, v3 f0, v3 r1, v3 u1, v3 f1, float dist);

#endif
