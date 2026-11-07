/* ndof.c -- see ndof.h. Backends: macOS (below, 3DconnexionClient loaded at runtime);
   everywhere else a stub: no device. */
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
