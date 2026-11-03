/* app_idle.c -- no redraw while nothing changes (issue #40). The scene and the UI
   were drawn at every vsync even with no file loaded, which kept the GPU busy.
   Now a frame is drawn only for a reason: any event (the counter stays up a few
   frames, Nuklear needs them to settle after input: hover, popups, tooltips), an
   animation, a load, a video or export, a note still on screen, flight mode, the
   test and screenshot runs; plus one frame every 2 s as a safety net (a lost
   window picture, a driver that dropped the buffer).
   A skipped frame makes no sg_ call at all and asks sokol_app not to swap /
   present (sapp_skip_present, a small patch in vendor/sokol_app.h): the picture on
   screen is simply the last one, which is correct on GL (a double buffer swapped
   without drawing would show a stale or flickering frame), D3D11 (flip model,
   discarded contents) and WebGL (the canvas keeps its picture). Without the swap
   there is no vsync wait, so the loop sleeps a few ms instead (the browser paces
   itself). Where sapp_skip_present is unsupported (macOS) every frame is drawn. */
#include "app_int.h"
#include "ui.h"
#include "sokol_app.h"
#ifdef _WIN32
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <time.h>
#endif

#define WAKE_FRAMES 4
#define HEARTBEAT   2.0      /* s between forced frames when idle */
#define IDLE_SLEEP  10       /* ms per skipped frame */

static int g_left = WAKE_FRAMES;
static double g_last_draw;

void app_wake(void) { g_left = WAKE_FRAMES; }

bool app_redraw_needed(void) {
    if (!G.idle_redraw || O.no_idle_skip) return true;
    if (O.shot_path || ui_test_on() || O.export_anim || O.probe_set || O.menu_set || O.box_set || O.nselect || O.integ) return true;
    if (app_busy() || G.video || G.seq_left > 0 || G.export_req || G.dlg_running) return true;
    if (G.flight || G.anim_on || (G.playing && G.loaded && G.frd.n_steps > 1)) return true;
    if (G.note[0] && cv_now() - G.note_t < 5.5) app_wake();          /* the status note fades out at 5 s */
    if (g_left > 0) { g_left--; return true; }
    double now = cv_now();
    if (now - g_last_draw >= HEARTBEAT) return true;    /* one frame is enough: nothing changed */
    return false;
}

/* the frame is drawn: stamp it; false: skipped (sleep, nothing swapped) */
bool app_idle_skip(void) {
    if (app_redraw_needed()) { g_last_draw = cv_now(); return false; }
    if (!sapp_skip_present()) { g_last_draw = cv_now(); return false; }   /* cannot skip the swap here: draw */
#ifdef _WIN32
    Sleep(IDLE_SLEEP);
#elif !defined(__EMSCRIPTEN__)
    nanosleep(&(struct timespec){ 0, IDLE_SLEEP * 1000000L }, NULL);
#endif
    return true;
}
