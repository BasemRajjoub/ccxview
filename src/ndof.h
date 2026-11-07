/* ndof.h -- 3D mouse (SpaceMouse, 6DOF) input (#41). Headless: the device layer
   gives a state, cv_ndof_move turns it into one frame's camera motion.

   Backends planned, each behind the same three calls, none written yet (today the
   only one reports no device, so nothing changes for anybody):
     Windows  Raw Input (WM_INPUT, HID usage page 0x01, usage 0x08) on sapp_win32_get_hwnd()
     Linux    spacenavd (FreeSpacenav): its socket protocol, or libspnav loaded with
              dlopen, so ccxview still starts where it is not installed
     macOS    (done in ndof.c, signs untested on a device) 3Dconnexion's 3DconnexionClient.framework, loaded at runtime when installed
     Web      WebHID in Chromium-based browsers (a click picks the device); last, it gets
              no input while 3DxWare runs */
#ifndef CV_NDOF_H
#define CV_NDOF_H

#include "base.h"

/* What the cap does, each axis -1..1 of its full travel: t right, up, toward the
   user; r tilt about x (nose down +), twist about y (right turn +), about z (roll,
   clockwise seen from the user +). buttons: a bit per button. */
typedef struct { float t[3], r[3]; unsigned buttons; } cv_ndof_state;

bool cv_ndof_open(void);                  /* false: no device (or no backend) */
bool cv_ndof_poll(cv_ndof_state* s);      /* false: nothing new / no device; true: s is the cap now */
void cv_ndof_close(void);

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

#endif
