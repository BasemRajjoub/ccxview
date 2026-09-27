/* log.h -- one line per event: every parser / loader message, sokol's log,
   timings. Goes to a file (--log FILE, $CCXVIEW_LOG) and, when verbose, to
   stderr. The last lines are kept in a ring so a crash report can show what
   happened before. Headless; safe to call from the loader thread. */
#ifndef CV_LOG_H
#define CV_LOG_H

#include "base.h"

void cv_log_open(const char* path);          /* NULL / "" closes the file */
void cv_log_set_verbose(bool on);
bool cv_log_verbose(void);
void cv_logf(const char* fmt, ...);          /* timestamped; newline added */

/* Crash report: on SIGSEGV & co (or a Windows unhandled exception) write
   <dir>/ccxview-crash.txt with the last log lines and `context` (e.g. the
   file being opened), then let the process die as usual. */
void cv_log_install_crash_handler(const char* dir);
void cv_log_set_context(const char* what);   /* copied; NULL clears */

#endif
