/* gpu.c -- renderer name, GPU utilisation for the title bar, software fallback. See gpu.h. */
#include "gpu.h"
#include "os.h"
#include <ctype.h>

#ifdef __EMSCRIPTEN__
/* WebGL: the browser owns the context; no timer queries, no fallback to arrange */
void cv_gpu_remember_args(int argc, char** argv) { (void)argc; (void)argv; }
void cv_gpu_software_mode(void) {}
bool cv_gpu_is_software_run(void) { return false; }
void cv_gpu_fallback(void) {}
void cv_gpu_init(void) {}
const char* cv_gpu_name(void) { return "WebGL"; }
bool cv_gpu_is_software(void) { return false; }
float cv_gpu_percent(bool* whole_gpu) { if (whole_gpu) *whole_gpu = false; return -1; }
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

/* ---- the few GL calls we make ourselves (loaded by name, no GL headers) ---- */

typedef unsigned int GLenum_;
typedef const unsigned char* (*fn_GetString)(GLenum_);
enum { GL_RENDERER_ = 0x1F01 };

static struct {
    fn_GetString GetString;
    char  name[96];
    bool  software;
#ifdef _WIN32
    volatile LONG pct10;           /* this process, tenths of a percent, -1 unknown: the PDH thread writes it */
#else
    void*   nvml;                  /* the whole GPU: NVIDIA via NVML, AMD via sysfs */
    void*   nvdev;
    int   (*nvmlUtil)(void*, unsigned*);
    char    amd_path[256];
#endif
    int     argc; char** argv;
} S = {
#ifdef _WIN32
    .pct10 = -1,
#endif
};

static void* gl_proc(const char* name) {
#ifdef _WIN32
    static HMODULE lib;
    if (!lib) lib = GetModuleHandleA("opengl32.dll");
    void* p = (void*)wglGetProcAddress(name);
    if (!p && lib) p = (void*)GetProcAddress(lib, name);
    return p;
#else
    static void* lib;
    if (!lib) {
#if defined(__APPLE__)
        lib = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY);
#else
        lib = dlopen("libGL.so.1", RTLD_LAZY);
        if (lib) {
            void* (*gpa)(const unsigned char*) = (void* (*)(const unsigned char*))dlsym(lib, "glXGetProcAddressARB");
            if (gpa) { void* p = gpa((const unsigned char*)name); if (p) return p; }
        }
#endif
    }
    return lib ? dlsym(lib, name) : NULL;
#endif
}

/* ---- software fallback --------------------------------------------------------- */

void cv_gpu_remember_args(int argc, char** argv) { S.argc = argc; S.argv = argv; }

bool cv_gpu_is_software_run(void) {
#ifdef _WIN32       /* set_env changes the process block; getenv reads the CRT's copy */
    char e[8];
    DWORD n = GetEnvironmentVariableA("CCXVIEW_SOFTWARE", e, sizeof e);
    return n > 0 && n < sizeof e && e[0] != '0';
#else
    const char* e = getenv("CCXVIEW_SOFTWARE");
    return e && *e && *e != '0';
#endif
}

#ifdef _WIN32
static bool set_env(const char* k, const char* v) { return SetEnvironmentVariableA(k, v) != 0; }
#else
static bool set_env(const char* k, const char* v) { return setenv(k, v, 1) == 0; }
#endif

void cv_gpu_software_mode(void) {
    set_env("CCXVIEW_SOFTWARE", "1");
#ifdef _WIN32
    /* A Mesa opengl32.dll beside us, in mesa/. Loaded by full path first, so
       sokol's LoadLibrary("opengl32.dll") gets this module, not System32's. */
    char exe[1024], dll[1100];
    if (cv_exe_dir(exe, sizeof exe)) {
        snprintf(dll, sizeof dll, "%s\\mesa\\opengl32.dll", exe);
        wchar_t w[1100];
        if (MultiByteToWideChar(CP_UTF8, 0, dll, -1, w, 1100)) LoadLibraryW(w);
    }
#elif defined(__linux__)
    set_env("LIBGL_ALWAYS_SOFTWARE", "1");
    set_env("GALLIUM_DRIVER", "llvmpipe");
    set_env("__GLX_VENDOR_LIBRARY_NAME", "mesa");
    /* a bundled Mesa (make bundle-mesa: lib/mesa beside bin/) for hosts without one */
    char exe[1024], mesa[1100], probe[1200];
    if (cv_exe_dir(exe, sizeof exe)) {
        snprintf(mesa, sizeof mesa, "%s/../lib/mesa", exe);
        snprintf(probe, sizeof probe, "%s/libGLX_mesa.so.0", mesa);
        if (cv_file_size(probe) > 0) {
            const char* old = getenv("LD_LIBRARY_PATH");
            char lp[2400];
            snprintf(lp, sizeof lp, "%s:%s/../lib%s%s", mesa, exe, old && *old ? ":" : "", old && *old ? old : "");
            set_env("LD_LIBRARY_PATH", lp);
            snprintf(probe, sizeof probe, "%s/dri", mesa);        /* older Mesa: dri/swrast_dri.so */
            if (cv_is_dir(probe)) set_env("LIBGL_DRIVERS_PATH", probe);
        }
    }
#endif
}

void cv_gpu_fallback(void) {
    if (cv_gpu_is_software_run()) return;                 /* already tried: let sokol report */
#ifdef _WIN32
    char exe[1024], dll[1100];
    if (!cv_exe_dir(exe, sizeof exe)) return;
    snprintf(dll, sizeof dll, "%s\\mesa\\opengl32.dll", exe);
    if (cv_file_size(dll) == 0) {
        MessageBoxA(NULL, "No OpenGL 4.1 capable graphics driver was found.\n\n"
                    "For software rendering put Mesa's opengl32.dll (llvmpipe) into a folder\n"
                    "named 'mesa' beside ccxview.exe and start it again.",
                    "ccxview: no GPU", MB_OK | MB_ICONERROR);
        return;
    }
    fprintf(stderr, "ccxview: no GL 4.1 context, restarting with mesa\\opengl32.dll\n");
    set_env("CCXVIEW_SOFTWARE", "1");
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (CreateProcessW(NULL, GetCommandLineW(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        ExitProcess(0);
    }
#elif defined(__linux__)
    fprintf(stderr, "ccxview: no GL 4.1 context, restarting with Mesa llvmpipe (software)\n");
    cv_gpu_software_mode();
    if (S.argv) execv("/proc/self/exe", S.argv);
    /* execv failed: fall through, sokol aborts with its own message */
#endif
}

/* ---- utilisation ---------------------------------------------------------------- */

#ifdef _WIN32
/* What Task Manager shows in its GPU column: this process's share of its
   busiest GPU engine, from the "GPU Engine" performance counters (Windows 10
   1709 and later, any vendor). pdh.dll is loaded by name; collecting walks the
   counters of every process, which can take tens of milliseconds, so a thread
   of its own does it once a second. */
typedef struct { DWORD CStatus; union { LONG l; double d; LONGLONG ll; const char* s; } v; } pdh_value;
typedef struct { char* name; pdh_value value; } pdh_item;
enum { PDH_FMT_DOUBLE_ = 0x200, PDH_FMT_NOCAP100_ = 0x8000 };
#define PDH_MORE_DATA_ ((LONG)0x800007D2)
typedef LONG (WINAPI *fn_PdhOpenQuery)(const char*, DWORD_PTR, void**);
typedef LONG (WINAPI *fn_PdhAddCounter)(void*, const char*, DWORD_PTR, void**);
typedef LONG (WINAPI *fn_PdhCollect)(void*);
typedef LONG (WINAPI *fn_PdhArray)(void*, DWORD, DWORD*, DWORD*, pdh_item*);

static void pdh_thread(void* arg) {
    (void)arg;
    HMODULE lib = LoadLibraryA("pdh.dll");
    if (!lib) return;
    fn_PdhOpenQuery open = (fn_PdhOpenQuery)(void*)GetProcAddress(lib, "PdhOpenQueryA");
    fn_PdhAddCounter add = (fn_PdhAddCounter)(void*)GetProcAddress(lib, "PdhAddEnglishCounterA");
    fn_PdhCollect collect = (fn_PdhCollect)(void*)GetProcAddress(lib, "PdhCollectQueryData");
    fn_PdhArray array = (fn_PdhArray)(void*)GetProcAddress(lib, "PdhGetFormattedCounterArrayA");
    void *q = NULL, *c = NULL;
    if (!open || !add || !collect || !array || open(NULL, 0, &q) != 0 ||
        add(q, "\\GPU Engine(*)\\Utilization Percentage", 0, &c) != 0) return;
    char pid[32];
    int np = snprintf(pid, sizeof pid, "pid_%lu_", (unsigned long)GetCurrentProcessId());
    collect(q);                                   /* a rate needs two samples */
    pdh_item* items = NULL;
    DWORD cap = 0;
    for (;;) {
        Sleep(1000);
        if (collect(q) != 0) continue;
        DWORD bytes = cap, n = 0;
        LONG r = array(c, PDH_FMT_DOUBLE_ | PDH_FMT_NOCAP100_, &bytes, &n, items);
        if (r == PDH_MORE_DATA_) {
            pdh_item* grown = realloc(items, bytes);
            if (!grown) continue;
            items = grown; cap = bytes;
            r = array(c, PDH_FMT_DOUBLE_ | PDH_FMT_NOCAP100_, &bytes, &n, items);
        }
        if (r != 0) continue;
        /* one instance per engine of each process ("pid_1234_luid_..._eng_0_engtype_3D"):
           ours, its busiest engine */
        double best = 0;
        for (DWORD k = 0; k < n; k++)
            if (items[k].name && !strncmp(items[k].name, pid, (size_t)np) && items[k].value.CStatus <= 1 &&
                items[k].value.v.d > best) best = items[k].value.v.d;
        InterlockedExchange(&S.pct10, (LONG)(CV_MIN(best, 100.0) * 10 + 0.5));
    }
}

static void find_load_source(void) {
    static cv_thread t;
    cv_thread_start(&t, pdh_thread, NULL);
}
#else
/* Linux has no per-process GPU figure that works with every driver: the whole
   GPU's load where the driver reports it (AMD sysfs, NVIDIA NVML), else none */
static void find_load_source(void) {
#if defined(__linux__)
    for (int c = 0; c < 8 && !S.amd_path[0]; c++) {
        char p[256];
        snprintf(p, sizeof p, "/sys/class/drm/card%d/device/gpu_busy_percent", c);
        FILE* f = fopen(p, "r");
        if (f) { snprintf(S.amd_path, sizeof S.amd_path, "%s", p); fclose(f); }
    }
    if (S.amd_path[0]) return;
    S.nvml = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
    if (!S.nvml) return;
    int (*init)(void) = (int (*)(void))dlsym(S.nvml, "nvmlInit_v2");
    int (*byidx)(unsigned, void**) = (int (*)(unsigned, void**))dlsym(S.nvml, "nvmlDeviceGetHandleByIndex_v2");
    S.nvmlUtil = (int (*)(void*, unsigned*))dlsym(S.nvml, "nvmlDeviceGetUtilizationRates");
    if (!init || !byidx || !S.nvmlUtil || init() != 0 || byidx(0, &S.nvdev) != 0) { S.nvml = NULL; S.nvmlUtil = NULL; }
#endif
}
#endif

float cv_gpu_percent(bool* whole_gpu) {
#ifdef _WIN32
    if (whole_gpu) *whole_gpu = false;
    LONG v = S.pct10;
    return v < 0 ? -1.f : v / 10.f;
#else
    if (whole_gpu) *whole_gpu = true;
    if (S.amd_path[0]) {
        FILE* f = fopen(S.amd_path, "r");
        int v = -1;
        if (f) { if (fscanf(f, "%d", &v) != 1) v = -1; fclose(f); }
        return (float)v;
    }
    unsigned u[2] = { 0, 0 };
    if (S.nvmlUtil && S.nvdev && S.nvmlUtil(S.nvdev, u) == 0) return (float)u[0];
    return -1.f;
#endif
}

/* ---- init ---------------------------------------------------------------------------- */

void cv_gpu_init(void) {
    S.GetString = (fn_GetString)gl_proc("glGetString");
    if (S.GetString) {
        const char* r = (const char*)S.GetString(GL_RENDERER_);
        if (r) {
            snprintf(S.name, sizeof S.name, "%s", r);
            /* trim the marketing tail: "AMD Radeon RX 6600 (radeonsi, navi23, LLVM 17...)" */
            char* cut = strstr(S.name, " (");
            if (cut && cut > S.name + 4 && !strstr(S.name, "llvmpipe")) *cut = 0;
            cut = strstr(S.name, "/PCIe"); if (cut) *cut = 0;
            static const char* soft[] = { "llvmpipe", "softpipe", "SwiftShader", "GDI Generic", "Software", "Mesa Offscreen", "Apple Software" };
            for (size_t i = 0; i < CV_COUNT(soft); i++) if (strstr(r, soft[i])) S.software = true;
            if (strstr(S.name, "llvmpipe")) snprintf(S.name, sizeof S.name, "llvmpipe");
        }
    }
    find_load_source();
}

const char* cv_gpu_name(void) { return S.name; }
bool cv_gpu_is_software(void) { return S.software; }
#endif /* !__EMSCRIPTEN__ */
