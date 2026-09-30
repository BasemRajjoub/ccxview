/* os.h -- the only platform-specific code: file mapping, threads, mutex, clock. */
#ifndef CV_OS_H
#define CV_OS_H

#include "base.h"

typedef struct {
    const char* data;
    size_t      size;
    void*       handle_[2];   /* platform handles */
} cv_map;

bool cv_map_open(cv_map* m, const char* path);  /* false: cannot open/map */
void cv_map_close(cv_map* m);

typedef struct { void* h_; } cv_thread;
bool cv_thread_start(cv_thread* t, void (*fn)(void*), void* arg);
void cv_thread_join(cv_thread* t);

typedef struct { void* h_; } cv_mutex;
void cv_mutex_init(cv_mutex* m);
void cv_mutex_lock(cv_mutex* m);
void cv_mutex_unlock(cv_mutex* m);
void cv_mutex_free(cv_mutex* m);

double cv_now(void);   /* seconds, monotonic */

/* Directory listing for the built-in file browser. `*out` is malloc'd; returns
   the entry count, or -1 when the directory cannot be read. Sorted: folders
   first, then by name (case-insensitive). */
typedef struct { char name[256]; bool dir; uint64_t size; } cv_dirent;
int  cv_list_dir(const char* dir, cv_dirent** out);
bool cv_exe_dir(char* out, size_t n);        /* folder holding the executable */
bool cv_is_dir(const char* path);
bool cv_abs_path(const char* path, char* out, size_t n);   /* absolute, normalised; false if it cannot be resolved */
char cv_path_sep(void);

/* Process stats for the title bar. CPU: percent of ONE core since the previous
   call (can exceed 100 with worker threads). RSS: resident bytes, 0 if unknown. */
float    cv_cpu_percent(void);
uint64_t cv_rss_bytes(void);
uint64_t cv_file_size(const char* path);
uint64_t cv_file_mtime(const char* path);    /* seconds since the epoch, 0 if unknown */

/* Windows GUI build: send stdout/stderr to the parent's console when there is one. */
#ifdef _WIN32
void cv_attach_console(void);
/* the window's title bar and taskbar icon from the exe's icon resource (res/ccxview.rc) */
void cv_set_window_icon(const void* hwnd);
#endif

#endif
