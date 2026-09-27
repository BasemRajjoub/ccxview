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
#endif

enum { RING = 24, LINE = 200 };

static struct {
    FILE*  f;
    bool   verbose;
    char   ring[RING][LINE];
    int    head, count;
    char   context[1024];
    char   crash_dir[1024];
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

/* Written from a signal handler: only what can be done there (open/write). */
static void write_crash(const char* why) {
    char path[1100];
    snprintf(path, sizeof path, "%s%cccxview-crash.txt", L.crash_dir[0] ? L.crash_dir : ".", cv_path_sep());
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "ccxview crashed: %s\n", why);
    if (L.context[0]) fprintf(f, "while: %s\n", L.context);
    fprintf(f, "last log lines:\n");
    for (int i = 0; i < L.count; i++) fprintf(f, "  %s\n", L.ring[(L.head - L.count + i + RING) % RING]);
    fclose(f);
    fprintf(stderr, "ccxview: crashed (%s); report written to %s\n", why, path);
}

#ifdef _WIN32
static LONG WINAPI on_exception(EXCEPTION_POINTERS* e) {
    char why[64];
    snprintf(why, sizeof why, "exception 0x%08lx", (unsigned long)e->ExceptionRecord->ExceptionCode);
    write_crash(why);
    return EXCEPTION_CONTINUE_SEARCH;
}
void cv_log_install_crash_handler(const char* dir) {
    snprintf(L.crash_dir, sizeof L.crash_dir, "%s", dir ? dir : "");
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
    snprintf(L.crash_dir, sizeof L.crash_dir, "%s", dir ? dir : "");
    const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) signal(sigs[i], on_signal);
}
#endif
