/* gpu.c -- renderer name, GPU busy/load for the title bar, software fallback. See gpu.h. */
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
void cv_gpu_frame_begin(void) {}
void cv_gpu_frame_end(void) {}
const char* cv_gpu_name(void) { return "WebGL"; }
bool cv_gpu_is_software(void) { return false; }
float cv_gpu_busy_percent(void) { return -1; }
float cv_gpu_load_percent(void) { return -1; }
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

/* ---- the few GL calls we make ourselves (loaded by name, no GL headers) ---- */

typedef unsigned int GLenum_, GLuint_;
typedef const unsigned char* (*fn_GetString)(GLenum_);
typedef void (*fn_GenQueries)(int, GLuint_*);
typedef void (*fn_BeginQuery)(GLenum_, GLuint_);
typedef void (*fn_EndQuery)(GLenum_);
typedef void (*fn_GetQueryObjectui64v)(GLuint_, GLenum_, unsigned long long*);
typedef void (*fn_GetQueryObjectuiv)(GLuint_, GLenum_, GLuint_*);
enum { GL_RENDERER_ = 0x1F01, GL_TIME_ELAPSED_ = 0x88BF, GL_QUERY_RESULT_ = 0x8866, GL_QUERY_RESULT_AVAILABLE_ = 0x8867 };

static struct {
    fn_GetString GetString;
    fn_GenQueries GenQueries;
    fn_BeginQuery BeginQuery;
    fn_EndQuery EndQuery;
    fn_GetQueryObjectui64v GetQueryObjectui64v;
    fn_GetQueryObjectuiv GetQueryObjectuiv;
    char  name[96];
    bool  software;
    bool  queries;
    enum { NQ = 4 } dummy_;
    GLuint_ q[4];
    double  q_wall[4];             /* wall time the frame took */
    int     q_next;
    bool    q_live[4];
    bool    in_query;
    double  frame_t0;
    /* window of results */
    double  gpu_ns, wall_s, since;
    float   busy;
    /* system load */
    void*   nvml;
    void*   nvdev;
    int   (*nvmlUtil)(void*, unsigned*);
    char    amd_path[256];
    float   load;
    double  load_t;
    int     argc; char** argv;
} S = { .busy = -1, .load = -1 };

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

/* ---- init ---------------------------------------------------------------------------- */

static void find_load_source(void) {
#if defined(__linux__)
    /* AMD: /sys/class/drm/cardN/device/gpu_busy_percent */
    for (int c = 0; c < 8 && !S.amd_path[0]; c++) {
        char p[256];
        snprintf(p, sizeof p, "/sys/class/drm/card%d/device/gpu_busy_percent", c);
        FILE* f = fopen(p, "r");
        if (f) { snprintf(S.amd_path, sizeof S.amd_path, "%s", p); fclose(f); }
    }
    if (S.amd_path[0]) return;
    S.nvml = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
#define SYM(h, n) dlsym(h, n)
#elif defined(_WIN32)
    S.nvml = (void*)LoadLibraryA("nvml.dll");
#define SYM(h, n) (void*)GetProcAddress((HMODULE)(h), n)
#else
#define SYM(h, n) NULL
#endif
    if (!S.nvml) return;
    int (*init)(void) = (int (*)(void))SYM(S.nvml, "nvmlInit_v2");
    int (*byidx)(unsigned, void**) = (int (*)(unsigned, void**))SYM(S.nvml, "nvmlDeviceGetHandleByIndex_v2");
    S.nvmlUtil = (int (*)(void*, unsigned*))SYM(S.nvml, "nvmlDeviceGetUtilizationRates");
    if (!init || !byidx || !S.nvmlUtil || init() != 0 || byidx(0, &S.nvdev) != 0) { S.nvml = NULL; S.nvmlUtil = NULL; }
#undef SYM
}

void cv_gpu_init(void) {
    S.GetString = (fn_GetString)gl_proc("glGetString");
    S.GenQueries = (fn_GenQueries)gl_proc("glGenQueries");
    S.BeginQuery = (fn_BeginQuery)gl_proc("glBeginQuery");
    S.EndQuery = (fn_EndQuery)gl_proc("glEndQuery");
    S.GetQueryObjectui64v = (fn_GetQueryObjectui64v)gl_proc("glGetQueryObjectui64v");
    S.GetQueryObjectuiv = (fn_GetQueryObjectuiv)gl_proc("glGetQueryObjectuiv");
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
    if (S.GenQueries && S.BeginQuery && S.EndQuery && S.GetQueryObjectui64v && S.GetQueryObjectuiv) {
        S.GenQueries(4, S.q);
        S.queries = S.q[0] != 0;
    }
    S.since = cv_now();
    find_load_source();
}

/* ---- per frame ------------------------------------------------------------------------ */

void cv_gpu_frame_begin(void) {
    S.frame_t0 = cv_now();
    if (!S.queries) return;
    int i = S.q_next;
    if (S.q_live[i]) {                        /* the ring wrapped before this one was read: wait for it */
        unsigned long long ns = 0;
        S.GetQueryObjectui64v(S.q[i], GL_QUERY_RESULT_, &ns);
        S.gpu_ns += (double)ns; S.wall_s += S.q_wall[i];
        S.q_live[i] = false;
    }
    S.BeginQuery(GL_TIME_ELAPSED_, S.q[i]);
    S.in_query = true;
}

void cv_gpu_frame_end(void) {
    double now = cv_now();
    if (S.queries && S.in_query) {
        S.EndQuery(GL_TIME_ELAPSED_);
        S.in_query = false;
        int i = S.q_next;
        S.q_wall[i] = now - S.frame_t0;
        S.q_live[i] = true;
        S.q_next = (i + 1) % 4;
        for (int k = 0; k < 4; k++) {         /* harvest what is ready, without stalling */
            if (!S.q_live[k]) continue;
            GLuint_ ready = 0;
            S.GetQueryObjectuiv(S.q[k], GL_QUERY_RESULT_AVAILABLE_, &ready);
            if (!ready) continue;
            unsigned long long ns = 0;
            S.GetQueryObjectui64v(S.q[k], GL_QUERY_RESULT_, &ns);
            S.gpu_ns += (double)ns; S.wall_s += S.q_wall[k];
            S.q_live[k] = false;
        }
        if (now - S.since >= 0.5 && S.wall_s > 0) {
            S.busy = (float)(S.gpu_ns * 1e-9 / S.wall_s * 100.0);
            if (S.busy > 100.f) S.busy = 100.f;
            S.gpu_ns = 0; S.wall_s = 0; S.since = now;
        }
    }
    /* system load, twice a second */
    if (now - S.load_t >= 0.5) {
        S.load_t = now;
        if (S.amd_path[0]) {
            FILE* f = fopen(S.amd_path, "r");
            int v = -1;
            if (f) { if (fscanf(f, "%d", &v) != 1) v = -1; fclose(f); }
            S.load = (float)v;
        } else if (S.nvmlUtil && S.nvdev) {
            unsigned u[2] = { 0, 0 };
            S.load = S.nvmlUtil(S.nvdev, u) == 0 ? (float)u[0] : -1.f;
        }
    }
}

const char* cv_gpu_name(void) { return S.name; }
bool cv_gpu_is_software(void) { return S.software; }
float cv_gpu_busy_percent(void) { return S.busy; }
float cv_gpu_load_percent(void) { return S.load; }
#endif /* !__EMSCRIPTEN__ */
