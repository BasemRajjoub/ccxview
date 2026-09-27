/* filedlg.h -- native "open file" dialog, run on a worker thread.

   Linux: xdg-desktop-portal over D-Bus (KDE/GNOME/... native dialog), spoken
          directly on the session-bus socket -- no libdbus, no GTK.
   Windows / macOS: tinyfiledialogs (comdlg32 / osascript).
   When no native dialog exists (no portal: bare X11, SSH, containers) the poll
   reports CV_DLG_UNAVAILABLE and the caller shows its built-in browser. */
#ifndef CV_FILEDLG_H
#define CV_FILEDLG_H

#include "base.h"

enum { CV_DLG_IDLE, CV_DLG_RUNNING, CV_DLG_DONE, CV_DLG_CANCELLED, CV_DLG_UNAVAILABLE };

/* parent: "x11:<hex window id>" or "" ; start_dir may be NULL. */
void cv_filedlg_start(const char* parent, const char* start_dir);
/* Non-blocking. On CV_DLG_DONE the path is copied to out. Terminal states
   (DONE / CANCELLED / UNAVAILABLE) are reported once, then it is IDLE again. */
int  cv_filedlg_poll(char* out, size_t n);

/* ---- portal internals, exposed for the unit tests -------------------------- */
/* Build a portal Request.Response signal message (little/host endian). */
size_t cv_dbus_build_response(uint8_t* buf, size_t cap, uint32_t code, const char* uri);
/* Parse a whole D-Bus message; if it is a Response signal, return true and fill
   code + the first uri decoded to a local path ("" if none). */
bool   cv_dbus_parse_response(const uint8_t* msg, size_t len, uint32_t* code, char* path, size_t n);
/* file:///a%20b -> /a b. false if not a file:// URI. */
bool   cv_uri_to_path(const char* uri, char* out, size_t n);
#if defined(__linux__)
bool   cv_dbus_hello(char* unique, size_t n);   /* live bus check, no UI */
#endif

#endif
