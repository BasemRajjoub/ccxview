/* filedlg.c -- native open-file dialog: xdg-desktop-portal on Linux (raw D-Bus),
   tinyfiledialogs on Windows and macOS. See filedlg.h. */
#include "filedlg.h"
#include "os.h"

/* ============================================================================
   Minimal D-Bus wire format: just enough to call one portal method and read its
   Response signal. Messages are written in host byte order (the endian flag says
   which); incoming messages of either order are read.
   ============================================================================ */

static bool host_le(void) { uint16_t x = 1; return *(uint8_t*)&x == 1; }

typedef struct { uint8_t* p; size_t n, cap; bool bad; } wbuf;

static void w_raw(wbuf* w, const void* d, size_t k) {
    if (w->bad || k == 0) return;
    if (w->n + k > w->cap) {
        size_t c = w->cap ? w->cap * 2 : 256;
        while (c < w->n + k) c *= 2;
        void* q = realloc(w->p, c);
        if (!q) { w->bad = true; return; }
        w->p = q; w->cap = c;
    }
    memcpy(w->p + w->n, d, k);
    w->n += k;
}
static void w_align(wbuf* w, size_t a) {
    static const uint8_t z[8] = {0};
    w_raw(w, z, (a - w->n % a) % a);
}
static void w_y(wbuf* w, uint8_t v)  { w_raw(w, &v, 1); }
static void w_u(wbuf* w, uint32_t v) { w_align(w, 4); w_raw(w, &v, 4); }
static void w_s(wbuf* w, const char* s) {
    uint32_t n = (uint32_t)strlen(s);
    w_u(w, n);
    w_raw(w, s, n + 1);
}
static void w_g(wbuf* w, const char* s) {
    w_y(w, (uint8_t)strlen(s));
    w_raw(w, s, strlen(s) + 1);
}
/* array: length placeholder, then padding to the element alignment; the length
   counts from the first element, not from the padding. */
typedef struct { size_t len_at, start; } warr;
static warr w_abegin(wbuf* w, size_t elem_align) {
    warr a;
    w_u(w, 0);
    a.len_at = w->n - 4;
    w_align(w, elem_align);
    a.start = w->n;
    return a;
}
static void w_aend(wbuf* w, warr a) {
    if (w->bad) return;
    uint32_t len = (uint32_t)(w->n - a.start);
    memcpy(w->p + a.len_at, &len, 4);
}

enum { DB_CALL = 1, DB_RETURN = 2, DB_ERROR = 3, DB_SIGNAL = 4 };
enum { HF_PATH = 1, HF_IFACE = 2, HF_MEMBER = 3, HF_ERROR = 4, HF_REPLY = 5,
       HF_DEST = 6, HF_SENDER = 7, HF_SIG = 8 };

static void w_field_str(wbuf* w, uint8_t code, const char* sig, const char* v) {
    if (!v) return;
    w_align(w, 8);
    w_y(w, code);
    w_g(w, sig);
    if (sig[0] == 'g') w_g(w, v); else w_s(w, v);
}

/* Header + body. `body` was built from offset 0, which is 8-aligned in the
   final message, so its internal padding stays valid. */
static void build_msg(wbuf* out, int type, uint32_t serial, const char* path, const char* iface,
                      const char* member, const char* dest, const char* sig, const wbuf* body,
                      uint32_t reply_serial) {
    w_y(out, host_le() ? 'l' : 'B');
    w_y(out, (uint8_t)type);
    w_y(out, 0);
    w_y(out, 1);
    w_u(out, body ? (uint32_t)body->n : 0);
    w_u(out, serial);
    warr f = w_abegin(out, 8);
    w_field_str(out, HF_PATH, "o", path);
    w_field_str(out, HF_IFACE, "s", iface);
    w_field_str(out, HF_MEMBER, "s", member);
    w_field_str(out, HF_DEST, "s", dest);
    if (reply_serial) {
        w_align(out, 8); w_y(out, HF_REPLY); w_g(out, "u"); w_u(out, reply_serial);
    }
    if (sig && sig[0]) w_field_str(out, HF_SIG, "g", sig);
    w_aend(out, f);
    w_align(out, 8);
    if (body && body->n) w_raw(out, body->p, body->n);
}

/* ---- reader ---- */

typedef struct { const uint8_t* p; size_t n, i; bool bad, swap; } rbuf;

static void r_align(rbuf* r, size_t a) {
    size_t j = (r->i + a - 1) / a * a;
    if (j > r->n) r->bad = true; else r->i = j;
}
static uint8_t r_y(rbuf* r) {
    if (r->bad || r->i + 1 > r->n) { r->bad = true; return 0; }
    return r->p[r->i++];
}
static uint32_t r_u(rbuf* r) {
    r_align(r, 4);
    if (r->bad || r->i + 4 > r->n) { r->bad = true; return 0; }
    uint32_t v;
    memcpy(&v, r->p + r->i, 4);
    r->i += 4;
    if (r->swap) v = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
    return v;
}
static const char* r_s(rbuf* r) {                 /* string / object path */
    uint32_t n = r_u(r);
    if (r->bad || n > r->n || r->i + n + 1 > r->n || r->p[r->i + n] != 0) { r->bad = true; return ""; }
    const char* s = (const char*)r->p + r->i;
    r->i += n + 1;
    return s;
}
static const char* r_g(rbuf* r) {                 /* signature */
    uint8_t n = r_y(r);
    if (r->bad || r->i + n + 1 > r->n || r->p[r->i + n] != 0) { r->bad = true; return ""; }
    const char* s = (const char*)r->p + r->i;
    r->i += (size_t)n + 1;
    return s;
}

/* One complete type starting at s; returns the character after it. */
static const char* sig_next(const char* s) {
    if (*s == 'a') return sig_next(s + 1);
    if (*s == '(' || *s == '{') {
        char close = *s == '(' ? ')' : '}';
        s++;
        while (*s && *s != close) s = sig_next(s);
        return *s ? s + 1 : s;
    }
    return *s ? s + 1 : s;
}
static size_t sig_align(char c) {
    switch (c) {
        case 'y': case 'g': case 'v': return 1;
        case 'n': case 'q': return 2;
        case 'x': case 't': case 'd': case '(': case '{': return 8;
        default: return 4;
    }
}
static void r_skip(rbuf* r, const char* sig, int depth) {
    if (r->bad || depth > 32) { r->bad = true; return; }
    switch (*sig) {
        case 'y': r_y(r); break;
        case 'n': case 'q': r_align(r, 2); r->i += 2; break;
        case 'b': case 'i': case 'u': case 'h': r_u(r); break;
        case 'x': case 't': case 'd': r_align(r, 8); r->i += 8; break;
        case 's': case 'o': r_s(r); break;
        case 'g': r_g(r); break;
        case 'v': { const char* vs = r_g(r); if (!r->bad) r_skip(r, vs, depth + 1); break; }
        case 'a': {
            uint32_t len = r_u(r);
            r_align(r, sig_align(sig[1]));
            if (r->bad || len > r->n - r->i) { r->bad = true; return; }
            r->i += len;
            break;
        }
        case '(': case '{': {
            r_align(r, 8);
            const char* e = sig_next(sig) - 1;
            for (const char* s = sig + 1; s < e && !r->bad; s = sig_next(s)) r_skip(r, s, depth + 1);
            break;
        }
        default: r->bad = true;
    }
    if (r->i > r->n) r->bad = true;
}

typedef struct {
    int type;
    uint32_t serial, reply_serial;
    const char *path, *member, *iface, *sig, *error;
    rbuf body;
} dmsg;

static bool parse_msg(const uint8_t* p, size_t len, dmsg* m) {
    memset(m, 0, sizeof *m);
    if (len < 16 || (p[0] != 'l' && p[0] != 'B')) return false;
    rbuf r = { p, len, 0, false, (p[0] == 'l') != host_le() };
    r.i = 1;
    m->type = r_y(&r);
    r_y(&r); r_y(&r);
    uint32_t body_len = r_u(&r);
    m->serial = r_u(&r);
    uint32_t flen = r_u(&r);
    r_align(&r, 8);
    size_t end = r.i + flen;
    if (r.bad || flen > len || end > len) return false;
    while (!r.bad && r.i < end) {
        r_align(&r, 8);
        uint8_t code = r_y(&r);
        const char* vs = r_g(&r);
        if (r.bad) break;
        if (vs[0] == 'u' && !vs[1]) { uint32_t v = r_u(&r); if (code == HF_REPLY) m->reply_serial = v; continue; }
        if ((vs[0] == 's' || vs[0] == 'o') && !vs[1]) {
            const char* s = r_s(&r);
            if (code == HF_PATH) m->path = s;
            else if (code == HF_MEMBER) m->member = s;
            else if (code == HF_IFACE) m->iface = s;
            else if (code == HF_ERROR) m->error = s;
            continue;
        }
        if (vs[0] == 'g' && !vs[1]) { const char* s = r_g(&r); if (code == HF_SIG) m->sig = s; continue; }
        r_skip(&r, vs, 0);
    }
    r.i = end;
    r_align(&r, 8);
    if (r.bad || body_len > len - r.i) return false;
    m->body = (rbuf){ p + r.i, body_len, 0, false, r.swap };
    return true;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool cv_uri_to_path(const char* uri, char* out, size_t n) {
    if (strncmp(uri, "file://", 7) != 0 || n == 0) return false;
    const char* s = uri + 7;
    if (*s != '/') { s = strchr(s, '/'); if (!s) return false; }   /* file://host/path */
    size_t k = 0;
    for (; *s && k + 1 < n; s++) {
        if (*s == '%' && hexval(s[1]) >= 0 && hexval(s[2]) >= 0) {
            out[k++] = (char)(hexval(s[1]) * 16 + hexval(s[2]));
            s += 2;
        } else {
            out[k++] = *s;
        }
    }
    out[k] = 0;
    return true;
}

bool cv_dbus_parse_response(const uint8_t* p, size_t len, uint32_t* code, char* path, size_t n) {
    dmsg m;
    if (!parse_msg(p, len, &m) || m.type != DB_SIGNAL || !m.member || strcmp(m.member, "Response") != 0)
        return false;
    if (!m.sig || strcmp(m.sig, "ua{sv}") != 0) return false;
    rbuf* r = &m.body;
    *code = r_u(r);
    if (n) path[0] = 0;
    uint32_t alen = r_u(r);
    r_align(r, 8);
    if (r->bad || alen > r->n - r->i) return !r->bad;
    size_t end = r->i + alen;
    while (!r->bad && r->i < end) {
        r_align(r, 8);
        const char* key = r_s(r);
        const char* vs = r_g(r);
        if (r->bad) break;
        if (strcmp(key, "uris") == 0 && strcmp(vs, "as") == 0) {
            uint32_t l2 = r_u(r);
            size_t e2 = r->i + l2;
            if (l2 && !r->bad) {
                const char* uri = r_s(r);
                if (!r->bad && n) cv_uri_to_path(uri, path, n);
            }
            r->i = e2 <= r->n ? e2 : r->n;
        } else {
            r_skip(r, vs, 0);
        }
    }
    return !r->bad;
}

size_t cv_dbus_build_response(uint8_t* buf, size_t cap, uint32_t code, const char* uri) {
    wbuf body = {0}, msg = {0};
    w_u(&body, code);
    warr a = w_abegin(&body, 8);
    w_align(&body, 8); w_s(&body, "choices"); w_g(&body, "a(ss)");   /* something to skip */
    { warr c = w_abegin(&body, 8); w_align(&body, 8); w_s(&body, "k"); w_s(&body, "v"); w_aend(&body, c); }
    if (uri) {
        w_align(&body, 8); w_s(&body, "uris"); w_g(&body, "as");
        warr u = w_abegin(&body, 4); w_s(&body, uri); w_aend(&body, u);
    }
    w_aend(&body, a);
    build_msg(&msg, DB_SIGNAL, 7, "/org/freedesktop/portal/desktop/request/1_2/t",
              "org.freedesktop.portal.Request", "Response", NULL, "ua{sv}", &body, 0);
    size_t n = (!msg.bad && !body.bad && msg.n <= cap) ? msg.n : 0;
    if (n) memcpy(buf, msg.p, n);
    free(body.p); free(msg.p);
    return n;
}

/* ============================================================================
   Linux: the portal call
   ============================================================================ */
#if defined(__EMSCRIPTEN__)
#include "web.h"
void cv_filedlg_start(const char* parent, const char* start_dir) { (void)parent; (void)start_dir; cv_web_pick_start(); }
int  cv_filedlg_poll(char* out, size_t n) { return cv_web_pick_poll(out, n); }
#elif defined(__linux__)
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int bus_connect(void) {
    char addr[512] = "";
    const char* env = getenv("DBUS_SESSION_BUS_ADDRESS");
    if (env) snprintf(addr, sizeof addr, "%s", env);
    else if (getenv("XDG_RUNTIME_DIR")) snprintf(addr, sizeof addr, "unix:path=%s/bus", getenv("XDG_RUNTIME_DIR"));
    for (char* tok = strtok(addr, ";"); tok; tok = strtok(NULL, ";")) {
        if (strncmp(tok, "unix:", 5) != 0) continue;
        struct sockaddr_un sa;
        memset(&sa, 0, sizeof sa);
        sa.sun_family = AF_UNIX;
        socklen_t sl = 0;
        for (char* kv = tok + 5; kv && *kv;) {
            char* next = strchr(kv, ',');
            if (next) *next++ = 0;
            if (strncmp(kv, "path=", 5) == 0 && strlen(kv + 5) < sizeof sa.sun_path) {
                strcpy(sa.sun_path, kv + 5);
                sl = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(sa.sun_path) + 1);
            } else if (strncmp(kv, "abstract=", 9) == 0 && strlen(kv + 9) < sizeof sa.sun_path - 1) {
                memcpy(sa.sun_path + 1, kv + 9, strlen(kv + 9));
                sl = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen(kv + 9));
            }
            kv = next;
        }
        if (!sl) continue;
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) continue;
        if (connect(fd, (struct sockaddr*)&sa, sl) == 0) return fd;
        close(fd);
    }
    return -1;
}

static bool send_all(int fd, const void* p, size_t n) {
    const char* c = p;
    while (n) {
        ssize_t k = send(fd, c, n, MSG_NOSIGNAL);
        if (k <= 0) return false;
        c += k; n -= (size_t)k;
    }
    return true;
}
static bool recv_all(int fd, void* p, size_t n) {
    char* c = p;
    while (n) {
        ssize_t k = recv(fd, c, n, 0);
        if (k <= 0) return false;
        c += k; n -= (size_t)k;
    }
    return true;
}

static bool bus_auth(int fd) {
    char cmd[64], hex[32] = "", uid[16];
    snprintf(uid, sizeof uid, "%u", (unsigned)getuid());
    for (size_t i = 0; uid[i]; i++) snprintf(hex + 2 * i, 3, "%02x", (unsigned char)uid[i]);
    snprintf(cmd, sizeof cmd, "AUTH EXTERNAL %s\r\n", hex);
    if (!send_all(fd, "", 1) || !send_all(fd, cmd, strlen(cmd))) return false;
    char line[256];
    size_t k = 0;
    while (k + 1 < sizeof line) {
        if (!recv_all(fd, line + k, 1)) return false;
        if (line[k++] == '\n') break;
    }
    line[k] = 0;
    if (strncmp(line, "OK", 2) != 0) return false;
    return send_all(fd, "BEGIN\r\n", 7);
}

/* Receive one message into *buf (grown as needed). */
static bool bus_recv(int fd, uint8_t** buf, size_t* cap, size_t* len) {
    uint8_t h[16];
    if (!recv_all(fd, h, 16)) return false;
    bool swap = (h[0] == 'l') != host_le();
    uint32_t blen, flen;
    memcpy(&blen, h + 4, 4); memcpy(&flen, h + 12, 4);
    if (swap) {
        blen = (blen >> 24) | ((blen >> 8) & 0xFF00) | ((blen << 8) & 0xFF0000) | (blen << 24);
        flen = (flen >> 24) | ((flen >> 8) & 0xFF00) | ((flen << 8) & 0xFF0000) | (flen << 24);
    }
    if (blen > (64u << 20) || flen > (1u << 20)) return false;
    size_t hdr = (16 + (size_t)flen + 7) / 8 * 8;
    size_t total = hdr + blen;
    if (total > *cap) {
        uint8_t* q = realloc(*buf, total);
        if (!q) return false;
        *buf = q; *cap = total;
    }
    memcpy(*buf, h, 16);
    if (!recv_all(fd, *buf + 16, total - 16)) return false;
    *len = total;
    return true;
}

static uint32_t g_serial;

static bool bus_call(int fd, const char* dest, const char* path, const char* iface, const char* member,
                     const char* sig, const wbuf* body, uint32_t* serial) {
    wbuf m = {0};
    *serial = ++g_serial;
    build_msg(&m, DB_CALL, *serial, path, iface, member, dest, sig, body, 0);
    bool ok = !m.bad && send_all(fd, m.p, m.n);
    free(m.p);
    return ok;
}

static bool add_match(int fd, const char* path) {
    char rule[512];
    snprintf(rule, sizeof rule,
             "type='signal',interface='org.freedesktop.portal.Request',member='Response',path='%s'", path);
    wbuf b = {0};
    w_s(&b, rule);
    uint32_t s;
    bool ok = !b.bad && bus_call(fd, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                 "AddMatch", "s", &b, &s);
    free(b.p);
    return ok;
}

/* 1 = chosen, 0 = cancelled, -1 = no portal */
static int portal_open(const char* parent, const char* start_dir, char* out, size_t n) {
    int fd = bus_connect();
    if (fd < 0) return -1;
    int result = -1;
    uint8_t* buf = NULL;
    size_t cap = 0, len = 0;
    wbuf body = {0};
    char unique[128] = "", req_path[256] = "", handle[256] = "", token[64];
    uint32_t serial;
    dmsg m;

    if (!bus_auth(fd)) goto done;
    if (!bus_call(fd, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "Hello",
                  NULL, NULL, &serial)) goto done;
    for (;;) {                                               /* Hello reply: our unique name */
        if (!bus_recv(fd, &buf, &cap, &len)) goto done;
        if (!parse_msg(buf, len, &m)) continue;
        if (m.reply_serial != serial) continue;
        if (m.type != DB_RETURN) goto done;
        snprintf(unique, sizeof unique, "%s", r_s(&m.body));
        break;
    }

    static unsigned counter;
    snprintf(token, sizeof token, "ccxview%u_%u", (unsigned)getpid(), ++counter);
    char sender[128];
    snprintf(sender, sizeof sender, "%s", unique[0] == ':' ? unique + 1 : unique);
    for (char* c = sender; *c; c++) if (*c == '.') *c = '_';
    snprintf(req_path, sizeof req_path, "/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
    if (!add_match(fd, req_path)) goto done;

    /* OpenFile(parent_window s, title s, options a{sv}) */
    w_s(&body, parent ? parent : "");
    w_s(&body, "Open CalculiX model or results");
    warr opts = w_abegin(&body, 8);
    w_align(&body, 8); w_s(&body, "handle_token"); w_g(&body, "s"); w_s(&body, token);
    w_align(&body, 8); w_s(&body, "modal"); w_g(&body, "b"); w_u(&body, 1);
    w_align(&body, 8); w_s(&body, "filters"); w_g(&body, "a(sa(us))");
    {
        /* one filter can hold several globs: (name, [(0, glob), ...]) */
        static const char* names[3] = { "CalculiX (*.frd, *.inp, *.dat, *.fbd)", "STL geometry (*.stl)", "All files" };
        static const char* globs[3][5] = { { "*.frd", "*.inp", "*.dat", "*.fbd", NULL }, { "*.stl", NULL }, { "*", NULL } };
        warr fl = w_abegin(&body, 8);
        for (int i = 0; i < 3; i++) {
            w_align(&body, 8);
            w_s(&body, names[i]);
            warr pats = w_abegin(&body, 8);
            for (int g = 0; globs[i][g]; g++) { w_align(&body, 8); w_u(&body, 0); w_s(&body, globs[i][g]); }
            w_aend(&body, pats);
        }
        w_aend(&body, fl);
    }
    if (start_dir && start_dir[0]) {
        w_align(&body, 8); w_s(&body, "current_folder"); w_g(&body, "ay");
        warr ay = w_abegin(&body, 1);
        w_raw(&body, start_dir, strlen(start_dir) + 1);
        w_aend(&body, ay);
    }
    w_aend(&body, opts);
    if (body.bad) goto done;
    if (!bus_call(fd, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                  "org.freedesktop.portal.FileChooser", "OpenFile", "ssa{sv}", &body, &serial)) goto done;

    for (;;) {
        if (!bus_recv(fd, &buf, &cap, &len)) goto done;
        if (!parse_msg(buf, len, &m)) continue;
        if (m.reply_serial == serial) {
            if (m.type == DB_ERROR) goto done;               /* no FileChooser portal */
            const char* h = r_s(&m.body);
            if (!m.body.bad && strcmp(h, req_path) != 0) {   /* old portals pick their own path */
                snprintf(handle, sizeof handle, "%s", h);
                add_match(fd, handle);
            }
            continue;
        }
        if (m.type == DB_SIGNAL && m.path && (strcmp(m.path, req_path) == 0 || strcmp(m.path, handle) == 0)) {
            uint32_t code = 2;
            char path[1024] = "";
            if (!cv_dbus_parse_response(buf, len, &code, path, sizeof path)) goto done;
            if (code == 0 && path[0]) { snprintf(out, n, "%s", path); result = 1; }
            else result = 0;
            break;
        }
    }
done:
    free(buf);
    free(body.p);
    close(fd);
    return result;
}

static int native_open(const char* parent, const char* start_dir, char* out, size_t n) {
    return portal_open(parent, start_dir, out, n);
}

/* Connect + authenticate + Hello, nothing visible. For tests. */
bool cv_dbus_hello(char* unique, size_t n) {
    int fd = bus_connect();
    if (fd < 0) return false;
    bool ok = false;
    uint8_t* buf = NULL;
    size_t cap = 0, len = 0;
    uint32_t serial;
    dmsg m;
    if (bus_auth(fd) && bus_call(fd, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                 "org.freedesktop.DBus", "Hello", NULL, NULL, &serial)) {
        while (bus_recv(fd, &buf, &cap, &len)) {
            if (!parse_msg(buf, len, &m) || m.reply_serial != serial) continue;
            if (m.type == DB_RETURN) { snprintf(unique, n, "%s", r_s(&m.body)); ok = !m.body.bad; }
            break;
        }
    }
    free(buf);
    close(fd);
    return ok;
}

#else  /* Windows, macOS: tinyfiledialogs */
#include "tinyfiledialogs.h"

static int native_open(const char* parent, const char* start_dir, char* out, size_t n) {
    (void)parent;
    char def[1100] = "";
    if (start_dir && start_dir[0]) {
#ifdef _WIN32
        snprintf(def, sizeof def, "%s\\", start_dir);
#else
        snprintf(def, sizeof def, "%s/", start_dir);
#endif
    }
    const char* pats[5] = { "*.frd", "*.inp", "*.dat", "*.fbd", "*.stl" };
    const char* r = tinyfd_openFileDialog("Open CalculiX model or results", def, 5, pats, "CalculiX files, STL geometry", 0);
    if (!r) return 0;
    snprintf(out, n, "%s", r);
    return 1;
}
#endif

/* ============================================================================
   worker thread
   ============================================================================ */
#ifndef __EMSCRIPTEN__

static struct {
    cv_mutex  lock;
    bool      lock_ready;
    cv_thread thread;
    int       state;
    char      parent[64], start[1024], result[1024];
} D;

static void worker(void* p) {
    (void)p;
    char out[1024] = "";
    int r = native_open(D.parent, D.start[0] ? D.start : NULL, out, sizeof out);
    cv_mutex_lock(&D.lock);
    snprintf(D.result, sizeof D.result, "%s", out);
    D.state = r > 0 ? CV_DLG_DONE : r == 0 ? CV_DLG_CANCELLED : CV_DLG_UNAVAILABLE;
    cv_mutex_unlock(&D.lock);
}

void cv_filedlg_start(const char* parent, const char* start_dir) {
    if (!D.lock_ready) { cv_mutex_init(&D.lock); D.lock_ready = true; }
    cv_mutex_lock(&D.lock);
    int s = D.state;
    cv_mutex_unlock(&D.lock);
    if (s == CV_DLG_RUNNING) return;
    if (D.thread.h_) cv_thread_join(&D.thread);
    snprintf(D.parent, sizeof D.parent, "%s", parent ? parent : "");
    snprintf(D.start, sizeof D.start, "%s", start_dir ? start_dir : "");
    D.state = CV_DLG_RUNNING;
    if (!cv_thread_start(&D.thread, worker, NULL)) D.state = CV_DLG_UNAVAILABLE;
}

int cv_filedlg_poll(char* out, size_t n) {
    if (!D.lock_ready) return CV_DLG_IDLE;
    cv_mutex_lock(&D.lock);
    int s = D.state;
    if (s == CV_DLG_DONE) snprintf(out, n, "%s", D.result);
    if (s != CV_DLG_RUNNING) D.state = CV_DLG_IDLE;
    cv_mutex_unlock(&D.lock);
    if (s != CV_DLG_RUNNING && s != CV_DLG_IDLE && D.thread.h_) cv_thread_join(&D.thread);
    return s;
}
#endif
