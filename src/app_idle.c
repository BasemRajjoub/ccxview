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
#define IDLE_SLEEP  0.010    /* s per skipped frame */
#define HOVER_FPS   30.0     /* the mouse moving with no button down: a highlight or a tooltip at most */
#define GAP_SLACK   0.002    /* s: a vsync at the limit's own rate must not lose every other frame */

static int g_left = WAKE_FRAMES, g_hover;
static double g_last_draw;

void app_wake(void) { g_left = WAKE_FRAMES; }
void app_wake_hover(void) { g_hover = WAKE_FRAMES; }

/* Sleep alone is about 15 ms on Windows, which would halve a limit of 60 fps */
static void nap(double s) {
#ifdef _WIN32
    static HANDLE timer;
    static bool tried;
    if (!tried) { tried = true; timer = CreateWaitableTimerExW(NULL, NULL, 0x2 /* high resolution, Windows 10 */, TIMER_ALL_ACCESS); }
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)(s * 1e7);
    if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) { WaitForSingleObject(timer, INFINITE); return; }
    Sleep((DWORD)(s * 1000 + 0.5));
#elif !defined(__EMSCRIPTEN__)
    nanosleep(&(struct timespec){ 0, (long)(s * 1e9) }, NULL);
#else
    (void)s;
#endif
}

enum { DRAW_NONE, DRAW_PACED, DRAW_ALWAYS };

/* why a frame is wanted; gap: the least time between such frames (G.max_fps; 0: as the display goes) */
static int wanted(double now, double* gap) {
    *gap = G.max_fps > 0 ? 1.0 / G.max_fps : 0;
    if (!G.idle_redraw || O.no_idle_skip) return DRAW_ALWAYS;
    if (O.shot_path || ui_test_on() || O.export_anim || O.probe_set || O.menu_set || O.box_set || O.nselect || O.integ) return DRAW_ALWAYS;
    if (G.video || G.seq_left > 0 || G.export_req) return DRAW_ALWAYS;        /* frames written, not shown: never paced */
    if (app_busy() || G.dlg_running || G.flight || G.anim_on || (G.playing && G.loaded && G.frd.n_steps > 1)) return DRAW_PACED;
    if (G.note[0] && now - G.note_t < 5.5) app_wake();          /* the status note fades out at 5 s */
    if (g_left > 0) return DRAW_PACED;
    if (g_hover > 0) { if (*gap < 1.0 / HOVER_FPS) *gap = 1.0 / HOVER_FPS; return DRAW_PACED; }
    if (now - g_last_draw >= HEARTBEAT) { *gap = 0; return DRAW_PACED; }    /* one frame is enough: nothing changed */
    return DRAW_NONE;
}

/* the frame is drawn: stamp it; false: skipped (sleep, nothing swapped) */
bool app_idle_skip(void) {
    double now = cv_now(), gap;
    int w = wanted(now, &gap);
    double wait = w == DRAW_NONE ? IDLE_SLEEP : w == DRAW_PACED ? gap - GAP_SLACK - (now - g_last_draw) : 0;
    if (wait > 0 && sapp_skip_present()) {                      /* where it cannot skip the swap (macOS): draw */
#ifndef __EMSCRIPTEN__
        nap(wait < IDLE_SLEEP ? wait : IDLE_SLEEP);
#endif
        return true;
    }
    if (g_left > 0) g_left--;
    else if (g_hover > 0) g_hover--;
    g_last_draw = now;
    return false;
}
