/* os.c -- file mapping, threads, mutex, clock for Win32 and POSIX. */
#include "os.h"
#include <ctype.h>

static int dirent_cmp(const void* a, const void* b) {
    const cv_dirent *x = a, *y = b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    for (const char *p = x->name, *q = y->name;; p++, q++) {
        int c = tolower((unsigned char)*p) - tolower((unsigned char)*q);
        if (c || !*p) return c;
    }
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool cv_map_open(cv_map* m, const char* path) {
    memset(m, 0, sizeof *m);
    wchar_t wpath[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024)) return false;
    HANDLE f = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz)) { CloseHandle(f); return false; }
    if (sz.QuadPart == 0) { m->data = ""; m->handle_[0] = f; return true; }
    HANDLE mp = CreateFileMappingW(f, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!mp) { CloseHandle(f); return false; }
    void* p = MapViewOfFile(mp, FILE_MAP_READ, 0, 0, 0);
    if (!p) { CloseHandle(mp); CloseHandle(f); return false; }
    m->data = (const char*)p;
    m->size = (size_t)sz.QuadPart;
    m->handle_[0] = f;
    m->handle_[1] = mp;
    return true;
}

void cv_map_close(cv_map* m) {
    if (m->size) UnmapViewOfFile(m->data);
    if (m->handle_[1]) CloseHandle((HANDLE)m->handle_[1]);
    if (m->handle_[0]) CloseHandle((HANDLE)m->handle_[0]);
    memset(m, 0, sizeof *m);
}

typedef struct { void (*fn)(void*); void* arg; } thunk_t;
static DWORD WINAPI thread_main(LPVOID p) {
    thunk_t t = *(thunk_t*)p;
    free(p);
    t.fn(t.arg);
    return 0;
}
bool cv_thread_start(cv_thread* t, void (*fn)(void*), void* arg) {
    thunk_t* k = malloc(sizeof *k);
    if (!k) return false;
    k->fn = fn; k->arg = arg;
    t->h_ = CreateThread(NULL, 0, thread_main, k, 0, NULL);
    if (!t->h_) { free(k); return false; }
    return true;
}
void cv_thread_join(cv_thread* t) {
    if (!t->h_) return;
    WaitForSingleObject((HANDLE)t->h_, INFINITE);
    CloseHandle((HANDLE)t->h_);
    t->h_ = NULL;
}

void cv_mutex_init(cv_mutex* m) {
    m->h_ = malloc(sizeof(CRITICAL_SECTION));
    InitializeCriticalSection((CRITICAL_SECTION*)m->h_);
}
void cv_mutex_lock(cv_mutex* m)   { EnterCriticalSection((CRITICAL_SECTION*)m->h_); }
void cv_mutex_unlock(cv_mutex* m) { LeaveCriticalSection((CRITICAL_SECTION*)m->h_); }
void cv_mutex_free(cv_mutex* m) {
    if (!m->h_) return;
    DeleteCriticalSection((CRITICAL_SECTION*)m->h_);
    free(m->h_);
    m->h_ = NULL;
}

double cv_now(void) {
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

char cv_path_sep(void) { return '\\'; }

bool cv_abs_path(const char* path, char* out, size_t n) {
    wchar_t w[1024], full[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return false;
    DWORD k = GetFullPathNameW(w, 1024, full, NULL);
    if (!k || k >= 1024) return false;
    return WideCharToMultiByte(CP_UTF8, 0, full, -1, out, (int)n, NULL, NULL) != 0;
}

bool cv_is_dir(const char* path) {
    wchar_t w[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return false;
    DWORD a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int cv_list_dir(const char* dir, cv_dirent** out) {
    *out = NULL;
    char pat[1100];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    wchar_t w[1100];
    if (!MultiByteToWideChar(CP_UTF8, 0, pat, -1, w, 1100)) return -1;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    CV_VEC(cv_dirent) v = {0};
    do {
        cv_dirent e;
        if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, e.name, sizeof e.name, NULL, NULL)) continue;
        if (!strcmp(e.name, ".") || !strcmp(e.name, "..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
        e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (!cv_push(v, e)) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (v.n) qsort(v.a, v.n, sizeof *v.a, dirent_cmp);
    *out = v.a;
    return (int)v.n;
}

bool cv_exe_dir(char* out, size_t n) {
    wchar_t w[1024];
    DWORD k = GetModuleFileNameW(NULL, w, 1024);
    if (!k || k >= 1024) return false;
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL)) return false;
    char* s = strrchr(out, '\\');
    if (s) *s = 0;
    return true;
}

float cv_cpu_percent(void) {
    static ULONGLONG prev_cpu, prev_wall;
    FILETIME c, e, k, u, now;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    GetSystemTimeAsFileTime(&now);
    ULONGLONG cpu = (((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime) +
                    (((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime);
    ULONGLONG wall = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    float pct = (prev_wall && wall > prev_wall) ? (float)(cpu - prev_cpu) / (float)(wall - prev_wall) * 100.f : 0.f;
    prev_cpu = cpu; prev_wall = wall;
    return pct;
}

/* K32GetProcessMemoryInfo lives in kernel32 (Win7+): no psapi.lib needed. */
typedef struct { DWORD cb, PageFaultCount; SIZE_T PeakWorkingSetSize, WorkingSetSize, a, b, c, d, e, f; } cv_pmc;
BOOL WINAPI K32GetProcessMemoryInfo(HANDLE, void*, DWORD);
uint64_t cv_rss_bytes(void) {
    cv_pmc m = { sizeof m };
    return K32GetProcessMemoryInfo(GetCurrentProcess(), &m, sizeof m) ? (uint64_t)m.WorkingSetSize : 0;
}

uint64_t cv_file_size(const char* path) {
    wchar_t w[1024];
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return 0;
    if (!GetFileAttributesExW(w, GetFileExInfoStandard, &a)) return 0;
    return ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}

uint64_t cv_file_mtime(const char* path) {
    wchar_t w[1024];
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return 0;
    if (!GetFileAttributesExW(w, GetFileExInfoStandard, &a)) return 0;
    uint64_t t = ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
    return t / 10000000ull;                  /* 100 ns ticks -> seconds; the epoch offset does not matter for comparing */
}

void cv_attach_console(void) {
    /* A GUI-subsystem exe starts without stdout. Redirected output (> file, a pipe)
       is inherited and kept; otherwise write to the console of the shell that
       started us, if any. Double-clicked, there is none and nothing opens. */
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
}

#else  /* POSIX */
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/resource.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach/mach.h>
#endif

bool cv_map_open(cv_map* m, const char* path) {
    memset(m, 0, sizeof *m);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); return false; }
    if (st.st_size == 0) { close(fd); m->data = ""; return true; }
    void* p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return false;
    madvise(p, (size_t)st.st_size, MADV_SEQUENTIAL);
    m->data = (const char*)p;
    m->size = (size_t)st.st_size;
    return true;
}

void cv_map_close(cv_map* m) {
    if (m->size) munmap((void*)m->data, m->size);
    memset(m, 0, sizeof *m);
}

typedef struct { void (*fn)(void*); void* arg; } thunk_t;
static void* thread_main(void* p) {
    thunk_t t = *(thunk_t*)p;
    free(p);
    t.fn(t.arg);
    return NULL;
}
bool cv_thread_start(cv_thread* t, void (*fn)(void*), void* arg) {
#ifdef __EMSCRIPTEN__
    t->h_ = NULL;                            /* no threads on plain web hosting: run it now */
    fn(arg);
    return true;
#endif
    thunk_t* k = malloc(sizeof *k);
    pthread_t* h = malloc(sizeof *h);
    if (!k || !h) { free(k); free(h); return false; }
    k->fn = fn; k->arg = arg;
    if (pthread_create(h, NULL, thread_main, k) != 0) { free(k); free(h); return false; }
    t->h_ = h;
    return true;
}
void cv_thread_join(cv_thread* t) {
    if (!t->h_) return;
    pthread_join(*(pthread_t*)t->h_, NULL);
    free(t->h_);
    t->h_ = NULL;
}

void cv_mutex_init(cv_mutex* m) {
    m->h_ = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init((pthread_mutex_t*)m->h_, NULL);
}
void cv_mutex_lock(cv_mutex* m)   { pthread_mutex_lock((pthread_mutex_t*)m->h_); }
void cv_mutex_unlock(cv_mutex* m) { pthread_mutex_unlock((pthread_mutex_t*)m->h_); }
void cv_mutex_free(cv_mutex* m) {
    if (!m->h_) return;
    pthread_mutex_destroy((pthread_mutex_t*)m->h_);
    free(m->h_);
    m->h_ = NULL;
}

double cv_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

char cv_path_sep(void) { return '/'; }

bool cv_is_dir(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool cv_abs_path(const char* path, char* out, size_t n) {
    char buf[4096];
    if (!realpath(path, buf)) return false;
    return snprintf(out, n, "%s", buf) < (int)n;
}

int cv_list_dir(const char* dir, cv_dirent** out) {
    *out = NULL;
    DIR* d = opendir(dir);
    if (!d) return -1;
    CV_VEC(cv_dirent) v = {0};
    struct dirent* de;
    char full[4096];
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;                 /* ., .., hidden */
        cv_dirent e;
        snprintf(e.name, sizeof e.name, "%s", de->d_name);
        snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;                 /* dangling link */
        e.dir = S_ISDIR(st.st_mode);
        e.size = (uint64_t)st.st_size;
        if (!cv_push(v, e)) break;
    }
    closedir(d);
    if (v.n) qsort(v.a, v.n, sizeof *v.a, dirent_cmp);
    *out = v.a;
    return (int)v.n;
}

bool cv_exe_dir(char* out, size_t n) {
#if defined(__APPLE__)
    uint32_t sz = (uint32_t)n;
    if (_NSGetExecutablePath(out, &sz) != 0) return false;
#else
    ssize_t k = readlink("/proc/self/exe", out, n - 1);
    if (k <= 0) return false;
    out[k] = 0;
#endif
    char* s = strrchr(out, '/');
    if (s) *s = 0;
    return true;
}

float cv_cpu_percent(void) {
    static double prev_cpu = -1, prev_wall;
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);       /* all threads of the process */
    double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6;
    double wall = cv_now();
    float pct = (prev_cpu >= 0 && wall > prev_wall) ? (float)((cpu - prev_cpu) / (wall - prev_wall) * 100.0) : 0.f;
    prev_cpu = cpu; prev_wall = wall;
    return pct;
}

uint64_t cv_rss_bytes(void) {
#if defined(__APPLE__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) != KERN_SUCCESS) return 0;
    return (uint64_t)info.resident_size;
#else
    FILE* f = fopen("/proc/self/statm", "r");       /* size resident ... in pages */
    if (!f) return 0;
    unsigned long long size = 0, res = 0;
    int k = fscanf(f, "%llu %llu", &size, &res);
    fclose(f);
    return k == 2 ? res * (uint64_t)sysconf(_SC_PAGESIZE) : 0;
#endif
}

uint64_t cv_file_size(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 ? (uint64_t)st.st_size : 0;
}

uint64_t cv_file_mtime(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 ? (uint64_t)st.st_mtime : 0;
}
#endif
