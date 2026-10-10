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
/* what the dialog is for: its title and the file types it shows first */
enum { CV_DLG_MODEL, CV_DLG_STL, CV_DLG_COMPARE };

/* parent: "x11:<hex window id>" or "" ; start_dir may be NULL; kind CV_DLG_MODEL ...
   false when it cannot start: an abandoned dialog is still closing. */
bool cv_filedlg_start(const char* parent, const char* start_dir, int kind);
/* Non-blocking. On CV_DLG_DONE the path is copied to out. Terminal states
   (DONE / CANCELLED / UNAVAILABLE) are reported once, then it is IDLE again. */
int  cv_filedlg_poll(char* out, size_t n);
/* Give up on the running dialog: the portal's is closed (the connection to the
   bus dropped), and whatever it answers is not reported. */
void cv_filedlg_abandon(void);
/* The name and file patterns of the dialog's first filter for kind, as the native
   dialogs show them ("STL geometry (*.stl)", { "*.stl", NULL }). For tests. */
const char* cv_filedlg_filter(int kind, const char* const** globs);

/* ---- portal internals, exposed for the unit tests -------------------------- */
/* Build a portal Request.Response signal message (little/host endian). */
size_t cv_dbus_build_response(uint8_t* buf, size_t cap, uint32_t code, const char* uri);
/* Parse a whole D-Bus message; if it is a Response signal, return true and fill
   code + the first uri decoded to a local path ("" if none). */
bool   cv_dbus_parse_response(const uint8_t* msg, size_t len, uint32_t* code, char* path, size_t n);
/* The body of a portal OpenFile call for kind: its title, filters and current_filter. */
size_t cv_dbus_openfile_body(uint8_t* buf, size_t cap, const char* parent, const char* token, const char* dir, int kind);
/* Read such a body back: true when it is well formed; its title and current filter's name. */
bool   cv_dbus_openfile_check(const uint8_t* body, size_t len, char* title, size_t tn, char* filter, size_t fn);
/* file:///a%20b -> /a b. false if not a file:// URI. */
bool   cv_uri_to_path(const char* uri, char* out, size_t n);
#if defined(__linux__)
bool   cv_dbus_hello(char* unique, size_t n);   /* live bus check, no UI */
#endif

#endif
