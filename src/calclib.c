#include "calclib.h"
#include "cfg.h"
#include "os.h"
#include <strings.h>

bool cv_calclib_default_path(char* out, size_t n) {
    char ini[1024];
    out[0] = 0;
    if (!cv_cfg_default_path(ini, sizeof ini)) return false;
    char* sep = strrchr(ini, '/');
    char* bs = strrchr(ini, '\\');
    if (bs > sep) sep = bs;
    if (sep) *sep = 0; else ini[0] = 0;
    int k = snprintf(out, n, "%s%cccxview_formulas.txt", ini[0] ? ini : ".", cv_path_sep());
    return k > 0 && (size_t)k < n;
}

static size_t trim(const char* s, size_t n, const char** start) {
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;
    *start = s;
    return n;
}

static bool clean(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) if ((unsigned char)s[i] < 32) return false;
    return n > 0;
}

static bool valid(const char* name, const char* expr) {
    size_t a = strlen(name), b = strlen(expr);
    return a > 0 && a < CV_FORMULA_NAME && b > 0 && b < CV_FORMULA_EXPR && clean(name, a) && clean(expr, b)
        && !strchr(name, '=') && name[0] != '#';
}

int cv_calclib_find(const cv_calclib* L, const char* name) {
    for (int i = 0; i < L->n; i++) if (!strcasecmp(L->a[i].name, name)) return i;
    return -1;
}

static bool push(cv_calclib* L, const char* name, const char* expr) {
    if (L->n == L->cap) {
        int nc = L->cap ? L->cap * 2 : 16;
        cv_formula* p = realloc(L->a, (size_t)nc * sizeof *p);
        if (!p) return false;
        L->a = p; L->cap = nc;
    }
    snprintf(L->a[L->n].name, CV_FORMULA_NAME, "%s", name);
    snprintf(L->a[L->n].expr, CV_FORMULA_EXPR, "%s", expr);
    L->n++;
    return true;
}

bool cv_calclib_set(cv_calclib* L, const char* name, const char* expr) {
    char nm[CV_FORMULA_NAME + 8], ex[CV_FORMULA_EXPR + 8];
    const char* p;
    size_t n = trim(name, strlen(name), &p);
    if (n >= sizeof nm) return false;
    memcpy(nm, p, n); nm[n] = 0;
    n = trim(expr, strlen(expr), &p);
    if (n >= sizeof ex) return false;
    memcpy(ex, p, n); ex[n] = 0;
    if (!valid(nm, ex)) return false;
    int i = cv_calclib_find(L, nm);
    if (i >= 0) { snprintf(L->a[i].name, CV_FORMULA_NAME, "%s", nm); snprintf(L->a[i].expr, CV_FORMULA_EXPR, "%s", ex); return true; }
    return push(L, nm, ex);
}

int cv_calclib_merge(cv_calclib* L, const char* text, int* dup, int* bad, int* first_bad) {
    int added = 0, line = 0, d = 0, b = 0, fb = 0;
    const char* s = text;
    while (*s) {
        const char* e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        const char* next = e ? e + 1 : s + len;
        line++;
        const char* t;
        size_t n = trim(s, len, &t);
        s = next;
        if (!n || *t == '#') continue;
        const char* eq = memchr(t, '=', n);
        char nm[CV_FORMULA_NAME + 8] = "", ex[CV_FORMULA_EXPR + 8] = "";
        bool ok = false;
        if (eq) {
            const char *a, *c;
            size_t na = trim(t, (size_t)(eq - t), &a), nc = trim(eq + 1, n - (size_t)(eq + 1 - t), &c);
            if (na < sizeof nm && nc < sizeof ex) {
                memcpy(nm, a, na); nm[na] = 0;
                memcpy(ex, c, nc); ex[nc] = 0;
                ok = valid(nm, ex);
            }
        }
        if (!ok) { b++; if (!fb) fb = line; continue; }
        if (cv_calclib_find(L, nm) >= 0) { d++; continue; }
        if (push(L, nm, ex)) added++;
    }
    if (dup) *dup = d;
    if (bad) *bad = b;
    if (first_bad) *first_bad = fb;
    return added;
}

void cv_calclib_remove(cv_calclib* L, int i) {
    if (i < 0 || i >= L->n) return;
    memmove(&L->a[i], &L->a[i + 1], (size_t)(L->n - i - 1) * sizeof *L->a);
    L->n--;
}

char* cv_calclib_text(const cv_calclib* L) {
    size_t cap = 128;
    for (int i = 0; i < L->n; i++) cap += strlen(L->a[i].name) + strlen(L->a[i].expr) + 4;
    char* out = malloc(cap);
    if (!out) return NULL;
    size_t k = (size_t)snprintf(out, cap, "# ccxview formulas: name = expression, one per line\n");
    for (int i = 0; i < L->n; i++) k += (size_t)snprintf(out + k, cap - k, "%s = %s\n", L->a[i].name, L->a[i].expr);
    return out;
}

bool cv_calclib_load(cv_calclib* L, const char* path, int* bad) {
    if (bad) *bad = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return true;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > (1 << 22)) { fclose(f); return false; }
    char* buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;
    cv_calclib_merge(L, buf, NULL, bad, NULL);
    free(buf);
    return true;
}

bool cv_calclib_save(const cv_calclib* L, const char* path) {
    char* t = cv_calclib_text(L);
    if (!t) return false;
    FILE* f = fopen(path, "wb");
    bool ok = f && fwrite(t, 1, strlen(t), f) == strlen(t);
    if (f && fclose(f) != 0) ok = false;
    free(t);
    return ok;
}

void cv_calclib_free(cv_calclib* L) { free(L->a); memset(L, 0, sizeof *L); }
