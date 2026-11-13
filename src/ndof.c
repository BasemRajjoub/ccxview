/* ndof.c -- see ndof.h. Backends: macOS (3DconnexionClient loaded at runtime), Linux
   (spacenavd's socket), Windows (Raw Input); the web and anything else: a stub, no device
   (WebHID would need the 3Dconnexion driver stopped, as it holds the device). */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "ndof.h"
#include "os.h"
#include <math.h>

/* CCXVIEW_NDOF_DEBUG=1: stderr lines for open / registration / device changes, and the raw
   axes of a non-zero report when they moved by a step (at most ~20 lines a second) */
static bool dbg_on(void) {
    static int on = -1;
    if (on < 0) { const char* e = getenv("CCXVIEW_NDOF_DEBUG"); on = e && *e && *e != '0'; }
    return on;
}
static void dbg(const char* fmt, ...) {
    if (!dbg_on()) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("ndof: ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}
static void dbg_axes(const int16_t a[6]) {   /* callers on another thread hold their lock */
    static int16_t last[6];
    static double t;
    if (!dbg_on()) return;
    int big = 0; bool any = false;
    for (int i = 0; i < 6; i++) { any |= a[i] != 0; big = abs(a[i] - last[i]) > big ? abs(a[i] - last[i]) : big; }
    double now = cv_now();
    if (!any || big < 20 || now - t < 0.05) return;
    t = now; memcpy(last, a, sizeof last);
    dbg("axes %d %d %d %d %d %d", a[0], a[1], a[2], a[3], a[4], a[5]);
}

/* dotted version numbers compared part by part: <0, 0, >0 for a <, =, > b */
int cv_ndof_version_cmp(const char* a, const char* b) {
    for (;;) {
        long x = 0, y = 0;
        while (*a >= '0' && *a <= '9') x = x * 10 + (*a++ - '0');
        while (*b >= '0' && *b <= '9') y = y * 10 + (*b++ - '0');
        if (x != y) return x < y ? -1 : 1;
        if (!*a && !*b) return 0;
        if (*a == '.') a++; else if (*a) return 0;   /* junk: stop, call it equal */
        if (*b == '.') b++; else if (*b) return 0;
    }
}

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
   SetConnexionHandlers is told to call back on a separate thread, so the state the callbacks
   write (client id, axes, buttons, flags) sits behind a mutex; the camera and UI work stay
   on the main thread. */
#include <dlfcn.h>
#include <pthread.h>

#define CX_MODE_TAKEOVER 1
#define CX_MASK_ALL 0x3fff
#define CX_MASK_AXIS 0x3f00
#define CX_MASK_ALL_BUTTONS 0xffffffffu
#define CX_CMD_BUTTONS 2
#define CX_CMD_AXIS 3
#define CX_CMD_APPEVENT 11                     /* newer drivers: a button number and pressed / released */
#define CX_MSG_DEVICE_STATE 0x33645352u        /* '3dSR' */
/* 3DxMacCore from this version (3DxWare 10.8.4) sends buttons as app events, with the
   buttons left unmasked, and only the axes forwarded */
#define CX_NEW_DRIVER "1.3.4.473"
#define CX_FRAMEWORK "/Library/Frameworks/3DconnexionClient.framework"

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

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static void* lib;
static bool is_open, handlers, registered;     /* main thread only */
static uint16_t client;                        /* below: under mu */
static bool dirty, have_dev, got_input;
static int16_t raw[6];
static unsigned btn;

static void on_msg(uint32_t product, uint32_t type, void* arg) {
    (void)product;
    if (type != CX_MSG_DEVICE_STATE || !arg) return;
    const cx_state* st = (const cx_state*)arg;
    pthread_mutex_lock(&mu);
    if (st->client == client && client) {      /* the state goes to every client */
        if (st->command == CX_CMD_AXIS) {
            memcpy(raw, st->axis, sizeof raw); dirty = true; dbg_axes(raw);
            for (int i = 0; i < 6; i++) if (raw[i]) got_input = true;   /* the driver's first message is all zero */
        }
        else if (st->command == CX_CMD_BUTTONS) {
            btn = st->buttons; dirty = true;
            if (btn) got_input = true;
            dbg("buttons mask 0x%08x", btn);
        }
        else if (st->command == CX_CMD_APPEVENT && st->value >= 0 && st->value < 32) {
            got_input = true;
            dbg("button event %d %s", (int)st->value, st->appEventPressed ? "pressed" : "released");   /* its numbering is unverified */
            if (st->appEventPressed) btn |= 1u << st->value; else btn &= ~(1u << st->value);
            dirty = true;
        }
    }
    pthread_mutex_unlock(&mu);
}
static void on_added(uint32_t product) {
    pthread_mutex_lock(&mu); have_dev = true; pthread_mutex_unlock(&mu);
    dbg("device added product=%u", product);
}
static void on_removed(uint32_t product) {
    pthread_mutex_lock(&mu);
    have_dev = false; got_input = false; memset(raw, 0, sizeof raw); btn = 0; dirty = true;
    pthread_mutex_unlock(&mu);
    dbg("device removed product=%u", product);
}

/* the driver framework's CFBundleVersion, through CoreFoundation found at run time; "" if unknown */
static void driver_version(char* out, size_t n) {
    out[0] = 0;
    void* cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LAZY);
    if (!cf) return;
    typedef const void* (*mk_str)(const void*, const char*, uint32_t);
    typedef const void* (*mk_url)(const void*, const void*, long, bool);
    typedef const void* (*mk_bundle)(const void*, const void*);
    typedef const void* (*get_val)(const void*, const void*);
    typedef bool (*to_c)(const void*, char*, long, uint32_t);
    typedef void (*rel)(const void*);
    mk_str str = (mk_str)dlsym(cf, "CFStringCreateWithCString");
    mk_url url = (mk_url)dlsym(cf, "CFURLCreateWithFileSystemPath");
    mk_bundle bun = (mk_bundle)dlsym(cf, "CFBundleCreate");
    get_val gv = (get_val)dlsym(cf, "CFBundleGetValueForInfoDictionaryKey");
    to_c tc = (to_c)dlsym(cf, "CFStringGetCString");
    rel rl = (rel)dlsym(cf, "CFRelease");
    if (!str || !url || !bun || !gv || !tc || !rl) return;
    const uint32_t utf8 = 0x08000100;
    const void* path = str(NULL, CX_FRAMEWORK, utf8);
    const void* key = str(NULL, "CFBundleVersion", utf8);
    const void* u = path ? url(NULL, path, 0 /* POSIX style */, true) : NULL;
    const void* b = u ? bun(NULL, u) : NULL;
    const void* v = (b && key) ? gv(b, key) : NULL;
    if (v && !tc(v, out, (long)n, utf8)) out[0] = 0;
    if (b) rl(b);
    if (u) rl(u);
    if (key) rl(key);
    if (path) rl(path);
}

bool cv_ndof_open(void) {
    if (is_open) return true;
    lib = dlopen(CX_FRAMEWORK "/3DconnexionClient", RTLD_LAZY);
    if (!lib) { dbg("driver framework not found"); return false; }
    *(void**)&p_SetHandlers = dlsym(lib, "SetConnexionHandlers");
    *(void**)&p_Cleanup = dlsym(lib, "CleanupConnexionHandlers");
    *(void**)&p_Register = dlsym(lib, "RegisterConnexionClient");
    *(void**)&p_ButtonMask = dlsym(lib, "SetConnexionClientButtonMask");
    *(void**)&p_Unregister = dlsym(lib, "UnregisterConnexionClient");
    if (!p_SetHandlers || !p_Cleanup || !p_Register || !p_Unregister) { dbg("driver symbols missing"); cv_ndof_close(); return false; }
    int16_t err = p_SetHandlers(on_msg, on_added, on_removed, true);   /* callbacks on a separate thread */
    if (err != 0) { dbg("SetConnexionHandlers failed: %d", (int)err); cv_ndof_close(); return false; }
    handlers = true;
    char ver[64];
    driver_version(ver, sizeof ver);
    bool newd = ver[0] && cv_ndof_version_cmp(ver, CX_NEW_DRIVER) >= 0;
    /* signature 0 and a Pascal string of the real executable's name: the driver tells the
       foreground application by them (ccxview has no CFBundleSignature of its own) */
    const char* nm = getprogname();
    size_t len = nm ? strlen(nm) : 0;
    char pas[256];
    if (len == 0 || len > 255) { dbg("executable name unusable"); cv_ndof_close(); return false; }
    pas[0] = (char)len; memcpy(pas + 1, nm, len);
    pthread_mutex_lock(&mu);
    have_dev = got_input = false; memset(raw, 0, sizeof raw); btn = 0; dirty = false;
    pthread_mutex_unlock(&mu);
    uint16_t id = p_Register(0, pas, CX_MODE_TAKEOVER, newd ? CX_MASK_AXIS : CX_MASK_ALL);
    if (id == 0) { dbg("RegisterConnexionClient failed (executable %s)", nm); cv_ndof_close(); return false; }
    pthread_mutex_lock(&mu); client = id; pthread_mutex_unlock(&mu);
    registered = true;
    if (p_ButtonMask) p_ButtonMask(id, newd ? 0u : CX_MASK_ALL_BUTTONS);
    is_open = true;
    dbg("registered executable=%s signature=0 client=%u driver=%s (%s buttons)", nm, id,
        ver[0] ? ver : "unknown", newd ? "app-event" : "mask");
    return true;
}

/* true while the cap is off centre (the motion is a rate: it goes on while held) or
   when something changed (the return to centre, a button) */
bool cv_ndof_poll(cv_ndof_state* s) {
    if (!is_open) return false;
    int16_t r[6]; unsigned b; bool d;
    pthread_mutex_lock(&mu);
    memcpy(r, raw, sizeof r); b = btn; d = dirty; dirty = false;
    pthread_mutex_unlock(&mu);
    bool held = false;
    for (int i = 0; i < 6; i++) held |= r[i] != 0;
    if (!held && !d) return false;
    cv_ndof_from_raw(r, s);
    s->buttons = b;
    return true;
}

void cv_ndof_clear(void) {
    pthread_mutex_lock(&mu); memset(raw, 0, sizeof raw); btn = 0; dirty = true; pthread_mutex_unlock(&mu);
}

cv_ndof_status cv_ndof_get_status(void) {
    if (!is_open) return CV_NDOF_NO_DRIVER;
    pthread_mutex_lock(&mu);
    cv_ndof_status r = got_input ? CV_NDOF_INPUT : have_dev ? CV_NDOF_DEVICE : CV_NDOF_DRIVER;
    pthread_mutex_unlock(&mu);
    return r;
}

void cv_ndof_close(void) {
    if (registered && p_Unregister) { pthread_mutex_lock(&mu); uint16_t c = client; pthread_mutex_unlock(&mu); p_Unregister(c); }
    if (handlers && p_Cleanup) p_Cleanup();    /* stops the callback thread: no lock is held here */
    if (lib) dlclose(lib);
    lib = NULL; is_open = handlers = registered = false; p_SetHandlers = NULL; p_Cleanup = NULL; p_Register = NULL;
    p_ButtonMask = NULL; p_Unregister = NULL;
    pthread_mutex_lock(&mu); client = 0; have_dev = got_input = false; memset(raw, 0, sizeof raw); btn = 0; dirty = false; pthread_mutex_unlock(&mu);
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
    dbg("spacenavd connected");
    memset(axis, 0, sizeof axis); btn = 0; dirty = false;
}

static void drop(void) { if (sock >= 0) { close(sock); dbg("spacenavd gone"); } sock = -1; memset(axis, 0, sizeof axis); btn = 0; dirty = true; }

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
                int16_t a16[6];
                for (int i = 0; i < 6; i++) a16[i] = (int16_t)e[i + 1];
                dbg_axes(a16);
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
void cv_ndof_clear(void) { memset(axis, 0, sizeof axis); btn = 0; dirty = true; }
cv_ndof_status cv_ndof_get_status(void) { return sock >= 0 ? CV_NDOF_DEVICE : CV_NDOF_NO_DRIVER; }
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
        dbg_axes(raw);
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
    if (!w) { dbg("no window yet"); return false; }
    RAWINPUTDEVICE d = { 0x01, 0x08, 0, w };      /* focused window only: no RIDEV_INPUTSINK */
    if (!RegisterRawInputDevices(&d, 1, sizeof d)) return false;
    old_proc = (WNDPROC)SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)ndof_proc);
    if (!old_proc) {
        d.dwFlags = RIDEV_REMOVE; d.hwndTarget = NULL;
        RegisterRawInputDevices(&d, 1, sizeof d);
        return false;
    }
    hw = w; memset(raw, 0, sizeof raw); btn = 0; dirty = false;
    dbg("raw input registered on the window");
    return true;
}

void cv_ndof_clear(void) { memset(raw, 0, sizeof raw); btn = 0; dirty = true; }
cv_ndof_status cv_ndof_get_status(void) { return hw ? CV_NDOF_DRIVER : CV_NDOF_NO_DRIVER; }

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
void cv_ndof_clear(void) {}
cv_ndof_status cv_ndof_get_status(void) { return CV_NDOF_NO_DRIVER; }
#endif

enum { DEAD_PCT = 3 };                    /* % of travel */

static float dead_z(float v, float z) {
    float a = fabsf(v);
    if (!(a > z)) return 0.f;             /* also NaN */
    a = fminf((a - z) / (1.f - z), 1.f);  /* continuous at the edge of the zone */
    return v < 0 ? -a : a;
}

void cv_ndof_opt_default(cv_ndof_opt* o) {
    memset(o, 0, sizeof *o);
    o->speed = 1.f; o->dead = DEAD_PCT * 0.01f;
}

bool cv_ndof_move_opt(const cv_ndof_state* s, float dt, float dist, const cv_ndof_opt* o, cv_ndof_move* m) {
    const float z = fminf(fmaxf(o->dead, 0.f), 0.9f);
    float t[3], r[3];
    for (int i = 0; i < 3; i++) { t[i] = dead_z(s->t[i], z); r[i] = dead_z(s->r[i], z); }
    /* effective axes: pan x, pan y, zoom, tilt, spin, roll */
    float v[6] = { t[0], o->zoom_lift ? -t[2] : t[1], o->zoom_lift ? t[1] : t[2], r[0], r[1], r[2] };
    for (int i = 0; i < 6; i++) if (o->flip[i]) v[i] = -v[i];
    if (o->lock_pan) v[0] = v[1] = v[2] = 0.f;
    if (o->lock_rot) v[3] = v[4] = v[5] = 0.f;
    if (o->dominant) {
        int k = 0;
        for (int i = 1; i < 6; i++) if (fabsf(v[i]) > fabsf(v[k])) k = i;
        for (int i = 0; i < 6; i++) if (i != k) v[i] = 0.f;
    }
    const float k = dt * o->speed;
    m->pan[0] = -v[0] * dist * k;         /* the model follows the cap: the camera goes the other way */
    m->pan[1] = -v[1] * dist * k;
    m->zoom = expf(-v[2] * k);            /* toward the user: the model nearer, the distance shorter */
    const float w = 1.5f * k;
    m->pitch = v[3] * w; m->yaw = v[4] * w; m->roll = v[5] * w;
    for (int i = 0; i < 6; i++) if (v[i] != 0) return true;
    return false;
}

bool cv_ndof_move_of(const cv_ndof_state* s, float dt, float dist, float speed, cv_ndof_move* m) {
    cv_ndof_opt o;
    cv_ndof_opt_default(&o);
    o.speed = speed;
    return cv_ndof_move_opt(s, dt, dist, &o, m);
}

v3 cv_pivot_target(v3 pivot, v3 eye0, v3 r0, v3 u0, v3 f0, v3 r1, v3 u1, v3 f1, float dist) {
    v3 q = v3_sub(pivot, eye0);
    float a = v3_dot(q, r0), b = v3_dot(q, u0), c = v3_dot(q, f0);
    v3 e = v3_sub(pivot, v3_add(v3_scale(r1, a), v3_add(v3_scale(u1, b), v3_scale(f1, c))));
    return v3_add(e, v3_scale(f1, dist));
}
