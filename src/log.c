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
#endif

enum { RING = 24, LINE = 200 };

static struct {
    FILE*  f;
    bool   verbose;
    char   ring[RING][LINE];
    int    head, count;
    char   context[1024];
    char   crash_path[1100];
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
   heap may be broken: raw open/write only, path made when installed. */
#ifdef _WIN32
typedef HANDLE crash_fd;
static crash_fd crash_open(void) {
    HANDLE h = CreateFileA(L.crash_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    return h == INVALID_HANDLE_VALUE ? NULL : h;
}
static void crash_put(crash_fd f, const char* s) { DWORD w; if (f) WriteFile(f, s, (DWORD)strlen(s), &w, NULL); }
static void crash_close(crash_fd f) { if (f) CloseHandle(f); }
static crash_fd crash_stderr(void) { return GetStdHandle(STD_ERROR_HANDLE); }
#else
typedef int crash_fd;
static crash_fd crash_open(void) { return open(L.crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0644); }
static void crash_put(crash_fd f, const char* s) { if (f >= 0) { ssize_t r = write(f, s, strlen(s)); (void)r; } }
static void crash_close(crash_fd f) { if (f >= 0) close(f); }
static crash_fd crash_stderr(void) { return 2; }
#endif

static void write_crash(const char* why) {
    crash_fd f = crash_open();
    crash_put(f, "ccxview crashed: "); crash_put(f, why); crash_put(f, "\n");
    if (L.context[0]) { crash_put(f, "while: "); crash_put(f, L.context); crash_put(f, "\n"); }
    crash_put(f, "last log lines:\n");
    for (int i = 0; i < L.count; i++) { crash_put(f, "  "); crash_put(f, L.ring[(L.head - L.count + i + RING) % RING]); crash_put(f, "\n"); }
    crash_close(f);
    crash_fd e = crash_stderr();
    crash_put(e, "ccxview: crashed ("); crash_put(e, why); crash_put(e, "); report written to "); crash_put(e, L.crash_path); crash_put(e, "\n");
}

static void set_crash_path(const char* dir) {
    snprintf(L.crash_path, sizeof L.crash_path, "%s%cccxview-crash.txt", dir && *dir ? dir : ".", cv_path_sep());
}

#ifdef _WIN32
static LONG WINAPI on_exception(EXCEPTION_POINTERS* e) {
    static const char hex[] = "0123456789abcdef";
    char why[] = "exception 0x00000000";
    unsigned long c = (unsigned long)e->ExceptionRecord->ExceptionCode;
    for (int i = 0; i < 8; i++) why[19 - i] = hex[(c >> (4 * i)) & 15];
    write_crash(why);
    return EXCEPTION_CONTINUE_SEARCH;
}
void cv_log_install_crash_handler(const char* dir) {
    set_crash_path(dir);
    SetUnhandledExceptionFilter(on_exception);
}
#else
static void on_signal(int sig) {
    const char* why = sig == SIGSEGV ? "segmentation fault" : sig == SIGBUS ? "bus error" :
                      sig == SIGFPE ? "floating point exception" : sig == SIGILL ? "illegal instruction" : "abort";
    write_crash(why);
    signal(sig, SIG_DFL);
    raise(sig);
}
void cv_log_install_crash_handler(const char* dir) {
    set_crash_path(dir);
    const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) signal(sigs[i], on_signal);
}
#endif
