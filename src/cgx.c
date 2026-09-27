/* cgx.c -- run cgx on a script in a temporary copy of its folder. See cgx.h. */
#include "cgx.h"
#include "os.h"
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define CGX_EXE "cgx.exe"
#define LIST_SEP ';'
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define CGX_EXE "cgx"
#define LIST_SEP ':'
#endif

#define WRAPPER "_ccxview_eval.fbd"
#define LOGFILE "_ccxview_eval.log"
#define TMP_TAG "ccxview-cgx-"
#define MAX_COPY (256ull << 20)          /* larger files are results, not inputs */

/* ---- small file helpers ----------------------------------------------------------- */

static FILE* open_w(const char* path) {
#ifdef _WIN32
    wchar_t w[2048];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 2048)) return NULL;
    return _wfopen(w, L"wb");
#else
    return fopen(path, "wb");
#endif
}

static bool is_exec(const char* p) {
#ifdef _WIN32
    return cv_file_size(p) > 0;
#else
    return access(p, X_OK) == 0 && !cv_is_dir(p);
#endif
}

static char* read_all(const char* path, size_t* n) {
    cv_map m;
    *n = 0;
    if (!cv_map_open(&m, path)) return NULL;
    char* d = malloc(m.size + 1);
    if (d) { memcpy(d, m.data, m.size); d[m.size] = 0; *n = m.size; }
    cv_map_close(&m);
    return d;
}

/* A script's own quit/exit would end cgx before the export: drop those lines. */
static bool is_quit_line(const char* s, size_t n) {
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    char w[8]; size_t k = 0;
    while (i < n && k < 7 && isalpha((unsigned char)s[i])) w[k++] = (char)tolower((unsigned char)s[i++]);
    w[k] = 0;
    if (i < n && isalnum((unsigned char)s[i])) return false;        /* a longer word */
    return !strcmp(w, "quit") || !strcmp(w, "exit");
}

static bool copy_file(const char* from, const char* to, bool strip_quit) {
    cv_map m;
    if (!cv_map_open(&m, from)) return false;
    FILE* f = open_w(to);
    bool ok = f != NULL;
    if (ok && !strip_quit) ok = fwrite(m.data, 1, m.size, f) == m.size;
    else if (ok) {
        const char *c = m.data, *end = m.data + m.size;
        while (c < end && ok) {
            const char* nl = memchr(c, '\n', (size_t)(end - c));
            size_t n = (size_t)((nl ? nl + 1 : end) - c);
            if (!is_quit_line(c, n)) ok = fwrite(c, 1, n, f) == n;
            c += n;
        }
    }
    if (f && fclose(f) != 0) ok = false;
    cv_map_close(&m);
    return ok;
}

/* ---- finding cgx -------------------------------------------------------------------- */

bool cv_cgx_find(char* out, size_t n) {
    const char* env = getenv("CCXVIEW_CGX");
    if (env && *env && is_exec(env)) { snprintf(out, n, "%s", env); return true; }
    const char* path = getenv("PATH");
    char cand[2048];
    while (path && *path) {
        const char* e = strchr(path, LIST_SEP);
        size_t l = e ? (size_t)(e - path) : strlen(path);
        if (l && l < 1500) {
            snprintf(cand, sizeof cand, "%.*s%c%s", (int)l, path, cv_path_sep(), CGX_EXE);
            if (is_exec(cand)) { snprintf(out, n, "%s", cand); return true; }
        }
        path = e ? e + 1 : NULL;
    }
    const char* home = getenv("HOME");
    if (home && *home) {
        snprintf(cand, sizeof cand, "%s/.local/bin/%s", home, CGX_EXE);
        if (is_exec(cand)) { snprintf(out, n, "%s", cand); return true; }
    }
    char exe[1024];
    if (cv_exe_dir(exe, sizeof exe)) {
        snprintf(cand, sizeof cand, "%s%c%s", exe, cv_path_sep(), CGX_EXE);
        if (is_exec(cand)) { snprintf(out, n, "%s", cand); return true; }
    }
    return false;
}

/* ---- temporary directory ------------------------------------------------------------ */

static bool make_tmp(char* out, size_t n) {
#ifdef _WIN32
    char base[MAX_PATH + 1];
    DWORD k = GetTempPathA(sizeof base, base);
    if (!k || k > MAX_PATH) return false;
    for (int t = 0; t < 100; t++) {
        snprintf(out, n, "%s" TMP_TAG "%lu-%lu-%d", base, (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(), t);
        if (CreateDirectoryA(out, NULL)) return true;
    }
    return false;
#else
    const char* base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    snprintf(out, n, "%s/" TMP_TAG "XXXXXX", base);
    return mkdtemp(out) != NULL;
#endif
}

/* Remove a directory we made (never follows links out of it). */
static void remove_tree(const char* dir) {
    if (!strstr(dir, TMP_TAG)) return;              /* only ever our own */
#ifdef _WIN32
    char pat[2100];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            char p[2100];
            snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                remove_tree(p);
            else {
                SetFileAttributesA(p, FILE_ATTRIBUTE_NORMAL);
                DeleteFileA(p);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir);
#else
    DIR* d = opendir(dir);
    if (d) {
        struct dirent* de;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char p[4096];
            snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
            struct stat st;
            if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) remove_tree(p);
            else unlink(p);
        }
        closedir(d);
    }
    rmdir(dir);
#endif
}

/* ---- evaluation ------------------------------------------------------------------- */

#ifndef _WIN32
/* 'it'\''s' quoting for sh */
static void sq(char* out, size_t n, const char* s) {
    size_t k = 0;
    if (k + 1 < n) out[k++] = '\'';
    for (; *s && k + 5 < n; s++) {
        if (*s == '\'') { memcpy(out + k, "'\\''", 4); k += 4; }
        else out[k++] = *s;
    }
    if (k + 1 < n) out[k++] = '\'';
    out[k] = 0;
}
#endif

void cv_cgx_result_free(cv_cgx_result* r) {
    free(r->fbd); free(r->msh);
    memset(r, 0, sizeof *r);
}

bool cv_cgx_eval(const char* cgx, const char* script_path, cv_cgx_result* r) {
    memset(r, 0, sizeof *r);
    /* folder + file name of the script */
    char dir[1024], name[256];
    snprintf(dir, sizeof dir, "%s", script_path);
    char* sl = strrchr(dir, '/');
#ifdef _WIN32
    char* sl2 = strrchr(dir, '\\');
    if (sl2 > sl) sl = sl2;
#endif
    if (sl) { snprintf(name, sizeof name, "%s", sl + 1); *sl = 0; if (!dir[0]) snprintf(dir, sizeof dir, "/"); }
    else { snprintf(name, sizeof name, "%s", dir); snprintf(dir, sizeof dir, "."); }

    char tmp[1024];
    if (!make_tmp(tmp, sizeof tmp)) { snprintf(r->err, sizeof r->err, "cannot create a temporary folder"); return false; }

    /* copy the folder's files (not subfolders, not big result files) */
    cv_dirent* ents = NULL;
    int ne = cv_list_dir(dir, &ents);
    bool have_script = false;
    for (int i = 0; i < ne; i++) {
        if (ents[i].dir || ents[i].size > MAX_COPY) continue;
        char from[2048], to[2048];
        snprintf(from, sizeof from, "%s%c%s", dir, cv_path_sep(), ents[i].name);
        snprintf(to, sizeof to, "%s%c%s", tmp, cv_path_sep(), ents[i].name);
        if (copy_file(from, to, cv_ends_with_ci(ents[i].name, ".fbd")) && !strcmp(ents[i].name, name)) have_script = true;
    }
    free(ents);
    if (!have_script) {
        snprintf(r->err, sizeof r->err, "cannot copy %s to a temporary folder", name);
        remove_tree(tmp);
        return false;
    }

    /* the wrapper: run the script, then export everything through a fresh set */
    char path[2048];
    snprintf(path, sizeof path, "%s%c%s", tmp, cv_path_sep(), WRAPPER);
    FILE* f = open_w(path);
    if (!f) { snprintf(r->err, sizeof r->err, "cannot write to the temporary folder"); remove_tree(tmp); return false; }
    fprintf(f, "read %s\nseta " CV_CGX_SET " se all\nsend " CV_CGX_SET " fbd\nsend " CV_CGX_SET " abq\nquit\n", name);
    fclose(f);

#ifdef _WIN32
    /* No shell: the temp folder and cgx path go straight to CreateProcess, so a
       folder named  x" & evil & "  cannot inject commands. Output to the log
       file through an inherited handle; 5 minute cap like the POSIX branch. */
    {
        wchar_t wdir[2048], wlog[2048], wcmd[4096];
        char logp[2100], cmdl[4096];
        snprintf(logp, sizeof logp, "%s\\" LOGFILE, tmp);
        /* argv[0] quoted; cgx's own path may hold spaces; a quote inside it is refused */
        if (strchr(cgx, '"')) { snprintf(r->err, sizeof r->err, "cgx path contains a quote"); remove_tree(tmp); return false; }
        snprintf(cmdl, sizeof cmdl, "\"%s\" -bg " WRAPPER, cgx);
        if (!MultiByteToWideChar(CP_UTF8, 0, tmp, -1, wdir, 2048) ||
            !MultiByteToWideChar(CP_UTF8, 0, logp, -1, wlog, 2048) ||
            !MultiByteToWideChar(CP_UTF8, 0, cmdl, -1, wcmd, 4096)) {
            snprintf(r->err, sizeof r->err, "path too long"); remove_tree(tmp); return false;
        }
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
        HANDLE hlog = CreateFileW(wlog, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        HANDLE hnul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        STARTUPINFOW si;
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hnul; si.hStdOutput = hlog; si.hStdError = hlog;
        PROCESS_INFORMATION pi;
        if (CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, wdir, &si, &pi)) {
            if (WaitForSingleObject(pi.hProcess, 300 * 1000) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
        if (hlog != INVALID_HANDLE_VALUE) CloseHandle(hlog);
        if (hnul != INVALID_HANDLE_VALUE) CloseHandle(hnul);
    }
#else
    char cmd[8192], qt[2100], qc[2100];
    sq(qt, sizeof qt, tmp);
    sq(qc, sizeof qc, cgx);
    snprintf(cmd, sizeof cmd,
             "cd %s && if command -v timeout >/dev/null 2>&1; then timeout 300 %s -bg " WRAPPER
             "; else %s -bg " WRAPPER "; fi > " LOGFILE " 2>&1 < /dev/null", qt, qc, qc);
    int rc = system(cmd);
    (void)rc;
#endif

    /* collect */
    snprintf(path, sizeof path, "%s%c" CV_CGX_SET ".fbd", tmp, cv_path_sep());
    r->fbd = read_all(path, &r->fbd_n);
    snprintf(path, sizeof path, "%s%c" CV_CGX_SET ".msh", tmp, cv_path_sep());
    r->msh = read_all(path, &r->msh_n);
    size_t ln = 0;
    snprintf(path, sizeof path, "%s%c" LOGFILE, tmp, cv_path_sep());
    char* log = read_all(path, &ln);
    if (log) {
        size_t from = ln > sizeof r->log - 1 ? ln - (sizeof r->log - 1) : 0;
        snprintf(r->log, sizeof r->log, "%s", log + from);
        free(log);
    }
    remove_tree(tmp);
    if (!r->fbd) {
        snprintf(r->err, sizeof r->err, "cgx wrote no geometry (see the cgx output in Messages)");
        return false;
    }
    return true;
}
