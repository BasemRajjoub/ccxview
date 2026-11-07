/* ndof.c -- see ndof.h. Backends: macOS (3DconnexionClient loaded at runtime), Linux
   (spacenavd's socket), Windows (Raw Input); the web and anything else: a stub, no device
   (WebHID would need the 3Dconnexion driver stopped, as it holds the device). */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include "ndof.h"
#include <math.h>

/* Raw driver axes (TX TY TZ RX RY RZ, about +-350 at full travel on the 3Dconnexion
   drivers) to a state. Same permutation and signs as Blender's GHOST_NDOFManagerCocoa:
   t = (TX, -TZ, TY), r = (-RX, RZ, -RY). The signs are untested on a device. */
void cv_ndof_from_raw(const int16_t axis[6], cv_ndof_state* s) {
    float v[6];
    for (int i = 0; i < 6; i++) v[i] = fmaxf(-1.f, fminf(1.f, (float)axis[i] / 350.f));
    s->t[0] = v[0]; s->t[1] = -v[2]; s->t[2] = v[1];
    s->r[0] = -v[3]; s->r[1] = v[5]; s->r[2] = -v[4];
}

#if defined(__APPLE__)
/* 3Dconnexion's client API (3DxWare), declared here by hand and found with dlopen, so
   nothing is linked and ccxview starts without the driver. Learnt from Blender's
   https://projects.blender.org/blender/blender/src/branch/main/intern/ghost/intern/GHOST_NDOFManagerCocoa.mm
   (the handler runs on the main run loop thread, which is sokol's NSApp loop: the
   statics need no lock). */
#include <dlfcn.h>
#include <string.h>

#define CX_MODE_TAKEOVER 1
#define CX_MASK_ALL 0x3fff
#define CX_MASK_ALL_BUTTONS 0xffffffffu
#define CX_CMD_BUTTONS 2
#define CX_CMD_AXIS 3
#define CX_MSG_DEVICE_STATE 0x33645352u        /* '3dSR' */

#pragma pack(push, 2)
typedef struct {
    uint16_t version, client, command;
    int16_t param;
    int32_t value;
    uint64_t time;
    uint8_t report[8];
    uint16_t appEventPressed;
    int16_t axis[6];
    uint16_t address;
    uint32_t buttons;
} cx_state;
#pragma pack(pop)
typedef char cx_state_size_is_48[sizeof(cx_state) == 48 ? 1 : -1];

typedef void (*cx_msg_fn)(uint32_t, uint32_t, void*);
typedef void (*cx_dev_fn)(uint32_t);
static int16_t (*p_SetHandlers)(cx_msg_fn, cx_dev_fn, cx_dev_fn, bool);
static void (*p_Cleanup)(void);
static uint16_t (*p_Register)(uint32_t, const char*, uint16_t, uint32_t);
static void (*p_ButtonMask)(uint16_t, uint32_t);
static void (*p_Unregister)(uint16_t);

static void* lib;
static uint16_t client;
static bool is_open, dirty;
static int16_t raw[6];
static unsigned btn;

static void on_msg(uint32_t product, uint32_t type, void* arg) {
    (void)product;
    if (type != CX_MSG_DEVICE_STATE || !arg) return;
    const cx_state* st = (const cx_state*)arg;
    if (st->client != client) return;           /* the state goes to every client */
    if (st->command == CX_CMD_AXIS) { memcpy(raw, st->axis, sizeof raw); dirty = true; }
    else if (st->command == CX_CMD_BUTTONS) { btn = st->buttons; dirty = true; }
}
static void on_dev(uint32_t product) { (void)product; }

bool cv_ndof_open(void) {
    if (is_open) return true;
    lib = dlopen("/Library/Frameworks/3DconnexionClient.framework/3DconnexionClient", RTLD_LAZY);
    if (!lib) return false;
    *(void**)&p_SetHandlers = dlsym(lib, "SetConnexionHandlers");
    *(void**)&p_Cleanup = dlsym(lib, "CleanupConnexionHandlers");
    *(void**)&p_Register = dlsym(lib, "RegisterConnexionClient");
    *(void**)&p_ButtonMask = dlsym(lib, "SetConnexionClientButtonMask");
    *(void**)&p_Unregister = dlsym(lib, "UnregisterConnexionClient");
    if (!p_SetHandlers || !p_Cleanup || !p_Register || !p_Unregister ||
        p_SetHandlers(on_msg, on_dev, on_dev, true) != 0) {
        cv_ndof_close();
        return false;
    }
    /* as Blender does for a plain executable: our own four-letter signature, a Pascal
       string name, take-over mode (the driver's own cursor/keystroke mapping stays off) */
    client = p_Register(0x63637876u /* 'ccxv' */, "\007ccxview", CX_MODE_TAKEOVER, CX_MASK_ALL);
    if (p_ButtonMask) p_ButtonMask(client, CX_MASK_ALL_BUTTONS);
    is_open = true;
    memset(raw, 0, sizeof raw); btn = 0; dirty = false;
    return true;
}

/* true while the cap is off centre (the motion is a rate: it goes on while held) or
   when something changed (the return to centre, a button) */
bool cv_ndof_poll(cv_ndof_state* s) {
    if (!is_open) return false;
    bool held = false;
    for (int i = 0; i < 6; i++) held |= raw[i] != 0;
    if (!held && !dirty) return false;
    dirty = false;
    cv_ndof_from_raw(raw, s);
    s->buttons = btn;
    return true;
}

void cv_ndof_close(void) {
    if (is_open && p_Unregister) p_Unregister(client);
    if (p_Cleanup && lib) p_Cleanup();
    if (lib) dlclose(lib);
    lib = NULL; is_open = false; p_SetHandlers = NULL; p_Cleanup = NULL; p_Register = NULL;
    p_ButtonMask = NULL; p_Unregister = NULL;
}
#elif defined(__linux__)
/* spacenavd's (FreeSpacenav) client socket, spoken directly: no libspnav, no link-time
   dependency, nothing to install for ccxview to start. Learnt from libspnav (BSD-3)
   https://github.com/FreeSpacenav/libspnav/blob/master/src/spnav.c : spnav_open (socket
   path: $SPNAV_SOCKET, $XDG_RUNTIME_DIR/spnav.sock, /var/run/spnav.sock) and
   spnav_poll_event / proc_event (an event is 8 int32 in host order: type 0 motion, 1
   press, 2 release; motion: x y z rx ry rz period; buttons: number). libspnav asks for
   protocol 1 right after connecting, to get more event kinds and to set the sensitivity;
   we stay in protocol 0 (the daemon's default for a client that says nothing), which
   sends exactly these events. Axis signs as Blender's GHOST_NDOFManagerUnix.cc
   https://projects.blender.org/blender/blender/src/branch/main/intern/ghost/intern/GHOST_NDOFManagerUnix.cc
   t = (x, y, -z), r = (-rx, -ry, rz) in the frame cv_ndof_from_raw uses; untested on a
   device. spacenavd's axes go to about +-350 at full travel, like 3DxWare's. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "os.h"

enum { RETRY_S = 5 };    /* a missing daemon is looked for every 5 s; a cap silent for 0.2 s is let go */

static int sock = -1;
static char test_path[sizeof(((struct sockaddr_un*)0)->sun_path)];
static double next_try, last_motion;
static float axis[6];                      /* x y z rx ry rz as sent */
static unsigned btn;
static bool dirty;
static unsigned char carry[32 * 8];
static size_t ncarry;

void cv_ndof_set_socket(const char* path) {   /* test hook: only this path is tried; NULL: the usual ones */
    if (path) snprintf(test_path, sizeof test_path, "%s", path); else test_path[0] = 0;
    next_try = 0;
}

static int connect_to(const char* path) {
    struct sockaddr_un a;
    if (!path || !*path || strlen(path) >= sizeof a.sun_path) return -1;
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return -1;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX; strcpy(a.sun_path, path);
    if (connect(s, (struct sockaddr*)&a, sizeof a) != 0) { close(s); return -1; }
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0 || fcntl(s, F_SETFL, fl | O_NONBLOCK) != 0) { close(s); return -1; }
    return s;
}

static void try_connect(double now) {
    next_try = now + RETRY_S;
    char buf[512];
    const char* env = getenv("SPNAV_SOCKET");
    const char* xdg = getenv("XDG_RUNTIME_DIR");
    int s = -1;
    if (test_path[0]) s = connect_to(test_path);
    else {
        if (env) s = connect_to(env);
        if (s < 0 && xdg && *xdg && snprintf(buf, sizeof buf, "%s/spnav.sock", xdg) < (int)sizeof buf) s = connect_to(buf);
        if (s < 0) s = connect_to("/var/run/spnav.sock");
    }
    if (s < 0) return;
    sock = s; ncarry = 0;
    memset(axis, 0, sizeof axis); btn = 0; dirty = false;
}

static void drop(void) { if (sock >= 0) close(sock); sock = -1; memset(axis, 0, sizeof axis); btn = 0; dirty = true; }

/* The backend is always "open": the daemon may start later, so the socket is looked for
   at most every RETRY_S seconds (one failed connect(): nothing to measure). */
bool cv_ndof_open(void) { try_connect(cv_now()); return true; }

bool cv_ndof_poll(cv_ndof_state* s) {
    double now = cv_now();
    if (sock < 0) {
        if (now < next_try) return false;
        try_connect(now);
        if (sock < 0) return false;
    }
    for (;;) {                             /* everything pending, newest motion wins */
        ssize_t n = read(sock, carry + ncarry, sizeof carry - ncarry);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) { drop(); next_try = now + RETRY_S; break; }
        if (n < 0) { if (errno == EINTR) continue; break; }
        ncarry += (size_t)n;
        size_t used = 0;
        for (; ncarry - used >= 32; used += 32) {
            int32_t e[8];
            memcpy(e, carry + used, 32);
            if (e[0] == 0) {
                for (int i = 0; i < 6; i++) axis[i] = (float)e[i + 1];
                last_motion = now; dirty = true;
            } else if ((e[0] == 1 || e[0] == 2) && e[1] >= 0 && e[1] < 32) {
                if (e[0] == 1) btn |= 1u << e[1]; else btn &= ~(1u << e[1]);
                dirty = true;
            }
        }
        memmove(carry, carry + used, ncarry - used); ncarry -= used;
        if (ncarry == 0 && n < (ssize_t)sizeof carry) break;   /* drained */
    }
    bool held = false;
    for (int i = 0; i < 6; i++) held |= axis[i] != 0.f;
    if (held && now - last_motion > 0.2) {   /* some daemons send no zero event on release */
        memset(axis, 0, sizeof axis); held = false; dirty = true;
    }
    if (!held && !dirty) return false;
    dirty = false;
    const float sgn[6] = { 1, 1, -1, -1, -1, 1 };   /* t = (x, y, -z), r = (-rx, -ry, rz) */
    s->buttons = btn;
    for (int i = 0; i < 3; i++) {
        s->t[i] = fmaxf(-1.f, fminf(1.f, sgn[i] * axis[i] / 350.f));
        s->r[i] = fmaxf(-1.f, fminf(1.f, sgn[i + 3] * axis[i + 3] / 350.f));
    }
    return true;
}

void cv_ndof_close(void) { drop(); dirty = false; next_try = 0; }
#elif defined(_WIN32)
/* Raw Input (user32, already linked): the 3Dconnexion HID devices are usage page 0x01 usage
   0x08 (multi-axis controller), so WM_INPUT arrives on the window while it has focus,
   with or without 3DxWare running. sokol_app owns the window procedure, so ours sits in
   front of it (SetWindowLongPtr) and hands every message on, WM_INPUT included.
   Report layout and axis frame from Blender, the same driver frame as the macOS client's
   (so cv_ndof_from_raw serves both):
   https://projects.blender.org/blender/blender/src/branch/main/intern/ghost/intern/GHOST_SystemWin32.cc
   (processNDOF): id 1 = TX TY TZ as int16 LE (12 bytes after the id when rotation comes
   with it, which Blender has never seen but is cheap to take), id 2 = RX RY RZ, id 3 = a
   32-bit button mask. Newer devices' button-number reports (ids 0x1c, 0x1d) are ignored.
   Untested on a device. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

extern const void* sapp_win32_get_hwnd(void);   /* sokol_app.h */

static HWND hw;
static WNDPROC old_proc;
static int16_t raw[6];
static unsigned btn;
static bool dirty;

static void on_report(const BYTE* d, size_t n) {
    if (n < 7) { if (n == 5 && d[0] == 3) { memcpy(&btn, d + 1, 4); dirty = true; } return; }
    if (d[0] == 1 || d[0] == 2) {
        int16_t a[6];
        size_t k = (n >= 13 && d[0] == 1) ? 6 : 3;
        memcpy(a, d + 1, k * 2);
        if (d[0] == 1) { memcpy(raw, a, k * 2); } else memcpy(raw + 3, a, 6);
        dirty = true;
    } else if (d[0] == 3) { unsigned b = 0; memcpy(&b, d + 1, n - 1 < 4 ? n - 1 : 4); btn = b; dirty = true; }
}

static void on_input(HRAWINPUT h) {
    union { RAWINPUT r; BYTE b[512]; } u;
    UINT sz = sizeof u;
    if (GetRawInputData(h, RID_INPUT, &u, &sz, sizeof(RAWINPUTHEADER)) == (UINT)-1) return;
    if (u.r.header.dwType != RIM_TYPEHID || u.r.data.hid.dwSizeHid == 0) return;
    for (DWORD i = 0; i < u.r.data.hid.dwCount; i++)
        on_report(u.r.data.hid.bRawData + i * u.r.data.hid.dwSizeHid, u.r.data.hid.dwSizeHid);
}

static LRESULT CALLBACK ndof_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT) on_input((HRAWINPUT)lp);
    return CallWindowProcW(old_proc, h, msg, wp, lp);
}

bool cv_ndof_open(void) {
    if (hw) return true;
    HWND w = (HWND)sapp_win32_get_hwnd();
    if (!w) return false;
    RAWINPUTDEVICE d = { 0x01, 0x08, 0, w };      /* focused window only: no RIDEV_INPUTSINK */
    if (!RegisterRawInputDevices(&d, 1, sizeof d)) return false;
    old_proc = (WNDPROC)SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)ndof_proc);
    if (!old_proc) {
        d.dwFlags = RIDEV_REMOVE; d.hwndTarget = NULL;
        RegisterRawInputDevices(&d, 1, sizeof d);
        return false;
    }
    hw = w; memset(raw, 0, sizeof raw); btn = 0; dirty = false;
    return true;
}

/* like the macOS backend: true while the cap is off centre or something changed */
bool cv_ndof_poll(cv_ndof_state* s) {
    if (!hw) return false;
    bool held = false;
    for (int i = 0; i < 6; i++) held |= raw[i] != 0;
    if (!held && !dirty) return false;
    dirty = false;
    cv_ndof_from_raw(raw, s);
    s->buttons = btn;
    return true;
}

void cv_ndof_close(void) {
    if (!hw) return;
    RAWINPUTDEVICE d = { 0x01, 0x08, RIDEV_REMOVE, NULL };
    RegisterRawInputDevices(&d, 1, sizeof d);
    if (GetWindowLongPtrW(hw, GWLP_WNDPROC) == (LONG_PTR)ndof_proc) SetWindowLongPtrW(hw, GWLP_WNDPROC, (LONG_PTR)old_proc);
    hw = NULL; old_proc = NULL;
}
#else
bool cv_ndof_open(void) { return false; }
bool cv_ndof_poll(cv_ndof_state* s) { (void)s; return false; }
void cv_ndof_close(void) {}
#endif

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
