/* log.c -- see log.h. */
#include "log.h"
#include "os.h"
#include <stdarg.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#endif

enum { RING = 24, LINE = 200 };

static struct {
    FILE*  f;
    bool   verbose;
    char   ring[RING][LINE];
    int    head, count;
    char   context[1024];
    char   crash_path[1100];
    char   crash_alt[1100];          /* the temp folder, when crash_path cannot be written */
    char   app[128];                 /* "ccxview 0.1.0", heads the report */
    double t0;
} L;

/* every message the parsers and loaders record also goes to the log */
static void sink(const char* text) { cv_logf("%s", text); }
void (*cv_msg_sink)(const char*) = sink;

void cv_log_open(const char* path) {
    if (L.f) { fclose(L.f); L.f = NULL; }
    if (path && *path) L.f = fopen(path, "a");
    if (L.f) { setvbuf(L.f, NULL, _IOLBF, 0); cv_logf("log opened"); }
}

void cv_log_set_verbose(bool on) { L.verbose = on; }
bool cv_log_verbose(void) { return L.verbose; }

void cv_logf(const char* fmt, ...) {
    char msg[LINE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (L.t0 == 0) L.t0 = cv_now();
    char line[LINE + 24];
    snprintf(line, sizeof line, "[%9.3f] %s", cv_now() - L.t0, msg);
    /* the ring is for the crash report: a torn line there is acceptable */
    snprintf(L.ring[L.head], LINE, "%s", line);
    L.head = (L.head + 1) % RING;
    if (L.count < RING) L.count++;
    if (L.f) fprintf(L.f, "%s\n", line);            /* one call: whole lines from any thread */
    if (L.verbose) fprintf(stderr, "%s\n", line);
}

void cv_log_set_context(const char* what) { snprintf(L.context, sizeof L.context, "%s", what ? what : ""); }

/* Written from a signal handler or the exception filter, where stdio and the
   heap may be broken: raw writes of fixed strings and hand-made numbers only. */
#ifdef _WIN32
typedef HANDLE crash_fd;
static crash_fd crash_open(const char* path) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    return h == INVALID_HANDLE_VALUE ? NULL : h;
}
static bool crash_ok(crash_fd f) { return f != NULL; }
static void crash_put(crash_fd f, const char* s) { DWORD w; if (f) WriteFile(f, s, (DWORD)strlen(s), &w, NULL); }
static void crash_close(crash_fd f) { if (f) CloseHandle(f); }
static crash_fd crash_stderr(void) { HANDLE h = GetStdHandle(STD_ERROR_HANDLE); return h == INVALID_HANDLE_VALUE ? NULL : h; }
#else
typedef int crash_fd;
static crash_fd crash_open(const char* path) { return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644); }
static bool crash_ok(crash_fd f) { return f >= 0; }
static void crash_put(crash_fd f, const char* s) { if (f >= 0) { ssize_t r = write(f, s, strlen(s)); (void)r; } }
static void crash_close(crash_fd f) { if (f >= 0) close(f); }
static crash_fd crash_stderr(void) { return 2; }
#endif

/* v in hex ("0x1a2b") or decimal into b, which ends up holding the digits */
static const char* crash_num(char b[24], unsigned long long v, unsigned base) {
    char* p = b + 23;
    *p = 0;
    do { *--p = "0123456789abcdef"[v % base]; v /= base; } while (v);
    if (base == 16) { *--p = 'x'; *--p = '0'; }
    return p;
}

/* The report: what, where (trace writes the stack; may be NULL), the context and
   the last log lines. Into crash_path, else crash_alt (the temp folder); the
   path written to ends up in crash_path. */
static void write_crash(const char* why, void (*trace)(crash_fd, void*), void* arg) {
    crash_fd f = crash_open(L.crash_path);
    if (!crash_ok(f) && L.crash_alt[0]) {
        f = crash_open(L.crash_alt);
        if (crash_ok(f)) memcpy(L.crash_path, L.crash_alt, sizeof L.crash_alt);
    }
    crash_put(f, L.app[0] ? L.app : "ccxview"); crash_put(f, " crashed: "); crash_put(f, why); crash_put(f, "\n");
    if (L.context[0]) { crash_put(f, "while: "); crash_put(f, L.context); crash_put(f, "\n"); }
    if (trace) { crash_put(f, "stack (scripts/symbolize.sh turns it into function and file:line):\n"); trace(f, arg); }
    crash_put(f, "last log lines:\n");
    for (int i = 0; i < L.count; i++) { crash_put(f, "  "); crash_put(f, L.ring[(L.head - L.count + i + RING) % RING]); crash_put(f, "\n"); }
    crash_close(f);
    crash_fd e = crash_stderr();
    crash_put(e, "ccxview: crashed ("); crash_put(e, why); crash_put(e, "); report written to "); crash_put(e, L.crash_path); crash_put(e, "\n");
}

#ifdef _WIN32
/* One line per frame, "  #3  ccxview.exe+0x1a2b3": the module and the offset in
   it, which symbolize.sh adds to the image base for addr2line. x64 unwinds by the
   .pdata tables every x64 module has: no dbghelp, no symbols needed here. */
static void win_trace(crash_fd f, void* arg) {
#if defined(_M_X64) || defined(__x86_64__)
    CONTEXT ctx = *(CONTEXT*)arg;
    char nb[24];
    for (unsigned i = 0; i < 64 && ctx.Rip; i++) {
        DWORD64 pc = ctx.Rip;
        HMODULE m = NULL;
        char name[MAX_PATH];
        crash_put(f, "  #"); crash_put(f, crash_num(nb, i, 10)); crash_put(f, i < 10 ? "   " : "  ");
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)(uintptr_t)pc, &m) && GetModuleFileNameA(m, name, sizeof name)) {
            const char* b = name;
            for (const char* p = name; *p; p++) if (*p == '\\' || *p == '/') b = p + 1;
            crash_put(f, b); crash_put(f, "+"); crash_put(f, crash_num(nb, pc - (DWORD64)(uintptr_t)m, 16));
        } else crash_put(f, crash_num(nb, pc, 16));
        crash_put(f, "\n");
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(pc, &base, NULL);
        if (fn) {
            PVOID hd;
            DWORD64 est;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, pc, fn, &ctx, &hd, &est, NULL);
        } else {                                        /* a leaf: the return address is on top */
            if (!ctx.Rsp) break;
            ctx.Rip = *(DWORD64*)(uintptr_t)ctx.Rsp;
            ctx.Rsp += 8;
        }
    }
#else
    (void)f; (void)arg;
#endif
}

static LONG WINAPI on_exception(EXCEPTION_POINTERS* e) {
    static volatile LONG once;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;   /* a crash in the report */
    char why[80] = "exception ", nb[24];
    unsigned long c = (unsigned long)e->ExceptionRecord->ExceptionCode;
    strcat(why, crash_num(nb, c, 16));
    strcat(why, c == EXCEPTION_ACCESS_VIOLATION ? " (access violation)" :
                c == EXCEPTION_STACK_OVERFLOW ? " (stack overflow)" :
                c == EXCEPTION_INT_DIVIDE_BY_ZERO ? " (integer divide by zero)" :
                c == EXCEPTION_ILLEGAL_INSTRUCTION ? " (illegal instruction)" : "");
    if (c == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters >= 2) {
        strcat(why, e->ExceptionRecord->ExceptionInformation[0] == 1 ? " writing " : " reading ");
        strcat(why, crash_num(nb, e->ExceptionRecord->ExceptionInformation[1], 16));
    }
    write_crash(why, win_trace, e->ContextRecord);
    if (!crash_stderr()) {                           /* started from the desktop: nobody reads stderr */
        char msg[1300] = "ccxview has crashed. A report was written to\n\n";
        size_t n = strlen(msg), k = strlen(L.crash_path);
        if (n + k + 1 < sizeof msg) memcpy(msg + n, L.crash_path, k + 1);
        MessageBoxA(NULL, msg, "ccxview", MB_OK | MB_ICONERROR | MB_TASKMODAL);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void install(void) { SetUnhandledExceptionFilter(on_exception); }
static void set_paths(void) {
    char t[MAX_PATH];
    DWORD n = GetTempPathA(sizeof t, t);
    if (n && n < sizeof t) snprintf(L.crash_alt, sizeof L.crash_alt, "%sccxview-crash.txt", t);
    char full[MAX_PATH];                             /* the working folder may change later */
    if (GetFullPathNameA(L.crash_path, sizeof full, full, NULL)) snprintf(L.crash_path, sizeof L.crash_path, "%s", full);
}
#else
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
/* "ccxview(+0x1a2b3) [0x55...]" per frame: symbolize.sh runs addr2line on the
   offsets. backtrace() ran once at install, so it allocates nothing here. */
static void posix_trace(crash_fd f, void* arg) {
    (void)arg;
    void* fr[64];
    int n = backtrace(fr, 64);
    if (f >= 0) backtrace_symbols_fd(fr, n, f);
}
#define POSIX_TRACE posix_trace
#else
#define POSIX_TRACE NULL
#endif
static void on_signal(int sig, siginfo_t* si, void* uc) {
    (void)uc;
    char why[80], nb[24];
    strcpy(why, sig == SIGSEGV ? "segmentation fault" : sig == SIGBUS ? "bus error" :
                sig == SIGFPE ? "floating point exception" : sig == SIGILL ? "illegal instruction" : "abort");
    if ((sig == SIGSEGV || sig == SIGBUS) && si) {
        strcat(why, " at ");
        strcat(why, crash_num(nb, (unsigned long long)(uintptr_t)si->si_addr, 16));
    }
    write_crash(why, POSIX_TRACE, NULL);
    signal(sig, SIG_DFL);
    raise(sig);
}
static void install(void) {
#if defined(__GLIBC__) || defined(__APPLE__)
    void* warm[1];
    backtrace(warm, 1);                              /* loads libgcc's unwinder now, not in the handler */
#endif
    static char alt[64 * 1024];                      /* a stack overflow still gets its report */
    stack_t ss;
    memset(&ss, 0, sizeof ss);
    ss.ss_sp = alt;
    ss.ss_size = sizeof alt;
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) sigaction(sigs[i], &sa, NULL);
}
static void set_paths(void) {
    const char* t = getenv("TMPDIR");
    snprintf(L.crash_alt, sizeof L.crash_alt, "%s/ccxview-crash.txt", t && *t ? t : "/tmp");
    char cwd[1024], p[sizeof L.crash_path];          /* the working folder may change later */
    if (L.crash_path[0] != '/' && getcwd(cwd, sizeof cwd) &&
        (size_t)snprintf(p, sizeof p, "%s/%s", cwd, L.crash_path) < sizeof p)
        memcpy(L.crash_path, p, sizeof p);
}
#endif

void cv_log_install_crash_handler(const char* dir, const char* app) {
    snprintf(L.app, sizeof L.app, "%s", app ? app : "");
    snprintf(L.crash_path, sizeof L.crash_path, "%s%cccxview-crash.txt", dir && *dir ? dir : ".", cv_path_sep());
    set_paths();
    install();
}
