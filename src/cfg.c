/* cfg.c -- flat "key = value" settings file; see cfg.h. */
#include "cfg.h"
#include "os.h"
#include <ctype.h>

#ifdef _WIN32
#include <direct.h>          /* _mkdir */
#else
#include <sys/stat.h>         /* mkdir */
#endif

/* Case-insensitive trim of key/value out of one line (no trailing '\n').
   Returns false for blank lines, comments (# or ;) and lines without '='. */
static bool parse_kv(const char* s, const char* e, char* key, size_t keysz, char* val, size_t valsz) {
    while (s < e && isspace((unsigned char)*s)) s++;
    while (e > s && isspace((unsigned char)e[-1])) e--;
    if (s >= e || *s == '#' || *s == ';') return false;
    const char* eq = memchr(s, '=', (size_t)(e - s));
    if (!eq) return false;
    const char* ks = s, *ke = eq;
    while (ke > ks && isspace((unsigned char)ke[-1])) ke--;
    if (ks >= ke) return false;                       /* empty key */
    const char* vs = eq + 1, *ve = e;
    while (vs < ve && isspace((unsigned char)*vs)) vs++;
    size_t kn = (size_t)(ke - ks); if (kn >= keysz) kn = keysz - 1;
    memcpy(key, ks, kn); key[kn] = 0;
    size_t vn = (size_t)(ve - vs); if (vn >= valsz) vn = valsz - 1;
    memcpy(val, vs, vn); val[vn] = 0;
    return true;
}

static bool ci_eq(const char* a, const char* b) {
    for (;; a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return false;
        if (!*a) return true;
    }
}

bool cv_cfg_default_path(char* out, size_t n) {
    out[0] = 0;
#ifdef __EMSCRIPTEN__
    /* the browser has no folder of its own: a MEMFS file mirrored to localStorage */
    const char* home = getenv("HOME");
    if (!home || !*home) return false;
    int k = snprintf(out, n, "%s/.config/ccxview/ccxview.ini", home);
#else
    /* portable: the settings travel with the executable */
    char dir[1024];
    if (!cv_exe_dir(dir, sizeof dir) || !dir[0]) return false;
    int k = snprintf(out, n, "%s%cccxview.ini", dir, cv_path_sep());
#endif
    return k > 0 && (size_t)k < n;
}

bool cv_cfg_load(cv_cfg* c, const char* path) {
    memset(c, 0, sizeof *c);
    snprintf(c->path, sizeof c->path, "%s", path ? path : "");
    FILE* f = fopen(path ? path : "", "rb");
    if (!f) return true;                              /* missing file: empty config, not an error */

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return true; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return true; }
    rewind(f);
    char* buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    c->raw = buf;
    c->raw_n = rd;

    cv_cfg_item* items = NULL;
    int n = 0;
    const char* p = buf, *end = buf + rd;
    while (p < end) {
        const char* nl = memchr(p, '\n', (size_t)(end - p));
        const char* le = nl ? nl : end;
        char key[64], val[1024];
        if (parse_kv(p, le, key, sizeof key, val, sizeof val)) {
            int found = -1;
            for (int i = 0; i < n; i++) if (strcmp(items[i].key, key) == 0) { found = i; break; }
            if (found >= 0) {
                snprintf(items[found].val, sizeof items[found].val, "%s", val);
            } else {
                cv_cfg_item* np = realloc(items, (size_t)(n + 1) * sizeof *np);
                if (!np) { free(items); free(buf); memset(c, 0, sizeof *c); snprintf(c->path, sizeof c->path, "%s", path ? path : ""); return false; }
                items = np;
                snprintf(items[n].key, sizeof items[n].key, "%s", key);
                snprintf(items[n].val, sizeof items[n].val, "%s", val);
                n++;
            }
        }
        p = nl ? nl + 1 : end;
    }
    c->a = items;
    c->n = n;
    return true;
}

static void dirname_of(const char* path, char* dir, size_t n) {
    const char* cut = NULL;
    for (const char* q = path; *q; q++) if (*q == '/' || *q == '\\') cut = q;
    if (!cut) { dir[0] = 0; return; }
    size_t k = (size_t)(cut - path);
    if (k >= n) k = n - 1;
    memcpy(dir, path, k);
    dir[k] = 0;
}

/* mkdir -p: every component; failures (exists, no permission) are ignored and
   show up as the fopen failing afterwards */
static void ensure_dir(const char* dir) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (char* p = tmp + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        *p = 0;
#ifdef _WIN32
        _mkdir(tmp);
#else
        mkdir(tmp, 0700);
#endif
        *p = '/';
    }
#ifdef _WIN32
    if (tmp[0]) _mkdir(tmp);
#else
    if (tmp[0]) mkdir(tmp, 0700);
#endif
}

bool cv_cfg_save(const cv_cfg* c) {
    if (!c->path[0]) return false;
    char dir[1024];
    dirname_of(c->path, dir, sizeof dir);
    ensure_dir(dir);

    FILE* f = fopen(c->path, "wb");
    if (!f) return false;

    bool* written = NULL;
    if (c->n > 0) {
        written = calloc((size_t)c->n, sizeof *written);
        if (!written) { fclose(f); return false; }
    }

    if (c->raw) {
        const char* p = c->raw, *end = c->raw + c->raw_n;
        while (p < end) {
            const char* nl = memchr(p, '\n', (size_t)(end - p));
            const char* le = nl ? nl : end;
            char key[64], val[1024];
            int idx = -1;
            if (parse_kv(p, le, key, sizeof key, val, sizeof val))
                for (int i = 0; i < c->n; i++) if (strcmp(c->a[i].key, key) == 0) { idx = i; break; }
            if (idx >= 0) {
                fprintf(f, "%s = %s\n", c->a[idx].key, c->a[idx].val);
                written[idx] = true;
            } else {
                fwrite(p, 1, (size_t)(le - p), f);      /* comment / blank / unknown: keep verbatim */
                fputc('\n', f);
            }
            p = nl ? nl + 1 : end;
        }
    }
    for (int i = 0; i < c->n; i++)
        if (!written || !written[i]) fprintf(f, "%s = %s\n", c->a[i].key, c->a[i].val);

    free(written);
    bool ok = fclose(f) == 0;
    return ok;
}

void cv_cfg_free(cv_cfg* c) {
    free(c->a);
    free(c->raw);
    memset(c, 0, sizeof *c);
}

const char* cv_cfg_get(const cv_cfg* c, const char* key, const char* dflt) {
    if (c) for (int i = 0; i < c->n; i++) if (strcmp(c->a[i].key, key) == 0) return c->a[i].val;
    return dflt;
}

int cv_cfg_get_int(const cv_cfg* c, const char* key, int dflt) {
    const char* s = cv_cfg_get(c, key, NULL);
    if (!s || !*s) return dflt;
    char* endp;
    long v = strtol(s, &endp, 10);
    if (endp == s || *endp) return dflt;
    return (int)v;
}

float cv_cfg_get_float(const cv_cfg* c, const char* key, float dflt) {
    const char* s = cv_cfg_get(c, key, NULL);
    if (!s || !*s) return dflt;
    char* endp;
    float v = strtof(s, &endp);
    if (endp == s || *endp) return dflt;
    return v;
}

bool cv_cfg_get_bool(const cv_cfg* c, const char* key, bool dflt) {
    const char* s = cv_cfg_get(c, key, NULL);
    if (!s || !*s) return dflt;
    if (!strcmp(s, "1") || ci_eq(s, "true") || ci_eq(s, "yes") || ci_eq(s, "on")) return true;
    if (!strcmp(s, "0") || ci_eq(s, "false") || ci_eq(s, "no") || ci_eq(s, "off")) return false;
    return dflt;
}

/* Grows c->a by exactly one slot; config files are tiny so this is not hot. */
static bool cfg_grow(cv_cfg* c) {
    cv_cfg_item* p = realloc(c->a, (size_t)(c->n + 1) * sizeof *p);
    if (!p) return false;
    c->a = p;
    return true;
}

void cv_cfg_set(cv_cfg* c, const char* key, const char* val) {
    if (!c || !key || !*key) return;
    for (int i = 0; i < c->n; i++)
        if (strcmp(c->a[i].key, key) == 0) {
            snprintf(c->a[i].val, sizeof c->a[i].val, "%s", val ? val : "");
            return;
        }
    if (!cfg_grow(c)) return;
    snprintf(c->a[c->n].key, sizeof c->a[c->n].key, "%s", key);
    snprintf(c->a[c->n].val, sizeof c->a[c->n].val, "%s", val ? val : "");
    c->n++;
}

void cv_cfg_set_int(cv_cfg* c, const char* key, int v) {
    char buf[32];
    snprintf(buf, sizeof buf, "%d", v);
    cv_cfg_set(c, key, buf);
}

void cv_cfg_set_float(cv_cfg* c, const char* key, float v) {
    char buf[48];
    snprintf(buf, sizeof buf, "%g", (double)v);
    cv_cfg_set(c, key, buf);
}

void cv_cfg_set_bool(cv_cfg* c, const char* key, bool v) {
    cv_cfg_set(c, key, v ? "true" : "false");
}

int cv_cfg_recent(const cv_cfg* c, const char** out, int max) {
    int n = 0;
    for (int i = 0; i < CV_CFG_RECENT && n < max; i++) {
        char key[16];
        snprintf(key, sizeof key, "recent%d", i);
        const char* v = cv_cfg_get(c, key, NULL);
        if (!v || !*v) break;                          /* the list is always contiguous from 0 */
        out[n++] = v;
    }
    return n;
}

void cv_cfg_add_recent(cv_cfg* c, const char* path) {
    if (!c || !path || !*path) return;
    const char* cur[CV_CFG_RECENT];
    int n = cv_cfg_recent(c, cur, CV_CFG_RECENT);

    /* copy out before any cv_cfg_set(), which may realloc c->a and move `cur` */
    char list[CV_CFG_RECENT][1024];
    int m = 0;
    snprintf(list[m], sizeof list[0], "%s", path);
    m++;
    for (int i = 0; i < n && m < CV_CFG_RECENT; i++) {
        if (strcmp(cur[i], path) == 0) continue;         /* dedupe: exact match moves to front */
        snprintf(list[m], sizeof list[0], "%s", cur[i]);
        m++;
    }
    for (int i = 0; i < m; i++) {
        char key[16];
        snprintf(key, sizeof key, "recent%d", i);
        cv_cfg_set(c, key, list[i]);
    }
}
