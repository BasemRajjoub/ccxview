/* app_title.c -- the title block's text: a template (tbtext.h) of "Label: value"
   lines with placeholders, made from the window's boxes until it is edited by
   hand, filled in here every frame from what the result file, the deck and the
   session say. The heading and the analysis come from the deck when there is one
   (*HEADING, the step's *STATIC / *FREQUENCY ...), else from the .frd header (1U
   records, the 100CL analysis type). ui_title.c draws the lines. */
#include <time.h>
#include "app_int.h"
#include "tbtext.h"
#if !defined(_WIN32)
#include <unistd.h>            /* gethostname */
#endif

static const char* const line_names[CV_TB_N] = {
    "Title", "Result file", "Solver", "Analysis", "Step", "Deformation", "Units", "User", "Date",
};

const char* app_title_line_name(int k) { return k >= 0 && k < CV_TB_N ? line_names[k] : ""; }

static const char* base_name(const char* p) {
    const char* b = p;
    for (const char* c = p; *c; c++) if (*c == '/' || *c == '\\') b = c + 1;
    return b;
}

/* when the solver wrote the result file (1UDATE, 1UTIME), else the file's time;
   false when neither is known */
static bool file_date(struct tm* out) {
    const cv_frd_head* h = &G.frd.head;
    char iso[16];
    int y, mo, d, hh = 0, mi = 0, ss = 0;
    if (cv_frd_date_iso(h->date, iso, sizeof iso) && sscanf(iso, "%d-%d-%d", &y, &mo, &d) == 3) {
        sscanf(h->time, "%d:%d:%d", &hh, &mi, &ss);
        *out = (struct tm){ .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = hh, .tm_min = mi, .tm_sec = ss, .tm_isdst = -1 };
        struct tm t = *out;
        if (mktime(&t) != (time_t)-1) *out = t;        /* the weekday, for %a */
        return true;
    }
    uint64_t m = cv_file_mtime(G.path);
    time_t tt = (time_t)m;
    struct tm* lt = m ? localtime(&tt) : NULL;
    if (lt) *out = *lt;
    return lt != NULL;
}

/* the deck's procedure of the step shown, else the .frd's analysis type */
static const char* analysis(const cv_step* st) {
    const cv_inp* d = deck_get();
    if (d && d->proc && st->step >= 1 && st->step <= d->nsteps && d->proc[st->step - 1])
        return cv_inp_proc_name(d->proc[st->step - 1]);
    static const char* const ict[4] = { "Static", "Time step", "Frequency", "Load step" };
    if (st->ictype >= 0 && st->ictype < 4) return ict[st->ictype];
    return st->modal ? "Frequency" : "";
}

static bool buckling(const cv_step* st) {
    const cv_inp* d = deck_get();
    return d && d->proc && st->step >= 1 && st->step <= d->nsteps && d->proc[st->step - 1] == CV_PROC_BUCKLE;
}

static void step_text(const cv_step* st, char* out, size_t n) {
    if (st->modal)
        snprintf(out, n, "%d, mode %d, %s %.6g", st->step, st->mode ? st->mode : st->inc, buckling(st) ? "factor" : "f =", st->time);
    else
        snprintf(out, n, "%d, increment %d, time %.6g", st->step, st->inc, st->time);
}

static void scale_text(char* out, size_t n) {
    if (find_field(G.step, "DISP") < 0) { out[0] = 0; return; }        /* nothing to deform with */
    if (!G.deform) snprintf(out, n, "undeformed");
    else if (G.deform_scale == 1.f) snprintf(out, n, "true scale (x1)");
    else snprintf(out, n, "x%.4g%s", G.deform_scale, G.deform_auto ? " (auto)" : "");
}

static void user_name(char* out, size_t n) {
    const char* u = getenv("USER");
    if (!u || !u[0]) u = getenv("USERNAME");
    if (!u || !u[0]) u = getenv("LOGNAME");
    if (!u || !u[0]) u = G.frd.head.user;
    snprintf(out, n, "%s", u);
}

static void host_name(char* out, size_t n) {
    const char* h = getenv("COMPUTERNAME");
    if (!h || !h[0]) h = getenv("HOSTNAME");
    out[0] = 0;
    if (h && h[0]) { snprintf(out, n, "%s", h); return; }
#if !defined(_WIN32)
    if (gethostname(out, n) == 0 && out[0]) { out[n - 1] = 0; return; }
#endif
    snprintf(out, n, "%s", G.frd.head.host);            /* where it was solved */
}

/* every placeholder this file fills, in the order of the help line; date,
   date_file and time_now are tbtext's own */
enum { K_TITLE, K_FILE, K_PATH, K_SOLVER, K_ANALYSIS, K_STEP, K_STEP_NO, K_INC, K_TIME, K_MODE, K_FREQ, K_FACTOR,
       K_SCALE, K_UNITS, K_FIELD, K_COMP, K_UNIT, K_USER, K_HOST, K_N };
static const char* const keys[K_N] = {
    "title", "file", "path", "solver", "analysis", "step", "step_no", "increment", "time", "mode", "freq", "factor",
    "scale", "units", "field", "component", "unit", "user", "host",
};
const char app_title_keys[] = "{title} {file} {path} {solver} {analysis} {step} {step_no} {increment} {time} {mode} "
                              "{freq} {factor} {scale} {units} {field} {component} {unit} {user} {host} "
                              "{date} {date_file} {time_now}, {date:%d.%m.%Y %H:%M}; {{ writes a brace";

/* each box's line in the template */
static const char* const line_keys[CV_TB_N] = {
    "title", "file", "solver", "analysis", "step", "scale", "units", "user", "date",
};

void app_title_generate(char* out, size_t n) {
    size_t o = 0;
    out[0] = 0;
    for (int k = 0; k < CV_TB_N + CV_TB_FREE; k++) {
        char lab[160], txt[160];
        if (k < CV_TB_N) {
            if (!G.title_line[k]) continue;
            snprintf(lab, sizeof lab, "%s", line_names[k]);
            snprintf(txt, sizeof txt, "{%s}", k == CV_TB_DATE && G.title_file_date ? "date_file" : line_keys[k]);
        } else {                                        /* a free line: as typed, shown when it has text */
            char (*f)[64] = G.title_free[k - CV_TB_N];
            if (!f[1][0]) continue;
            cv_tb_literal(f[0], lab, sizeof lab);
            cv_tb_literal(f[1], txt, sizeof txt);
        }
        int w = snprintf(out + o, n - o, "%s%s%s%s", o ? "\n" : "", lab, lab[0] ? ": " : "", txt);
        if (w < 0 || (size_t)w >= n - o) { out[o] = 0; break; }       /* no half line */
        o += (size_t)w;
    }
}

void app_title_sync(void) {
    if (!G.title_hand) app_title_generate(G.title_text, sizeof G.title_text);
}

int app_title_lines(cv_title_line* out, int max, const char* units) {
    if (!G.loaded) return 0;
    app_title_sync();
    const cv_step* st = G.frd.n_steps > 0 && G.step >= 0 && G.step < G.frd.n_steps ? &G.frd.steps[G.step] : NULL;
    static char v[K_N][256];
    for (int k = 0; k < K_N; k++) v[k][0] = 0;
    const cv_inp* d = deck_get();
    snprintf(v[K_TITLE], sizeof v[0], "%s", d && d->heading[0] ? d->heading : G.frd.head.heading);
    snprintf(v[K_FILE], sizeof v[0], "%s", base_name(G.path));
    snprintf(v[K_PATH], sizeof v[0], "%s", G.path);
    const char* ver = G.frd.head.version;
    if (!strncmp(ver, "Version ", 8)) ver += 8;
    snprintf(v[K_SOLVER], sizeof v[0], "%s%s%s", G.frd.head.pgm, G.frd.head.pgm[0] && ver[0] ? " " : "", ver);
    if (st) {
        snprintf(v[K_ANALYSIS], sizeof v[0], "%s", analysis(st));
        step_text(st, v[K_STEP], sizeof v[0]);
        snprintf(v[K_STEP_NO], sizeof v[0], "%d", st->step);
        if (st->modal) {
            snprintf(v[K_MODE], sizeof v[0], "%d", st->mode ? st->mode : st->inc);
            snprintf(v[buckling(st) ? K_FACTOR : K_FREQ], sizeof v[0], "%.6g", st->time);
        } else {
            snprintf(v[K_INC], sizeof v[0], "%d", st->inc);
            snprintf(v[K_TIME], sizeof v[0], "%.6g", st->time);
        }
    }
    scale_text(v[K_SCALE], sizeof v[0]);
    if (units && strcmp(units, "not set")) snprintf(v[K_UNITS], sizeof v[0], "%s", units);
    if (G.has_field) {
        snprintf(v[K_FIELD], sizeof v[0], "%s", G.legend_lines[0]);
        snprintf(v[K_COMP], sizeof v[0], "%s", G.legend_lines[1]);
        const char* u = G.legend_lines[2];                 /* "[MPa]" */
        size_t ul = strlen(u);
        if (ul >= 2 && u[0] == '[' && u[ul - 1] == ']') snprintf(v[K_UNIT], sizeof v[0], "%.*s", (int)ul - 2, u + 1);
    }
    user_name(v[K_USER], sizeof v[0]);
    host_name(v[K_HOST], sizeof v[0]);

    cv_tb_kv kv[K_N];
    for (int k = 0; k < K_N; k++) kv[k] = (cv_tb_kv){ keys[k], v[k] };
    time_t now_t = time(NULL);
    struct tm now = { 0 }, file = { 0 }, *lt = localtime(&now_t);
    if (lt) now = *lt;
    bool has_file = file_date(&file);
    cv_tb_ctx c = { kv, K_N, G.title_date_fmt, lt ? &now : NULL, has_file ? &file : NULL };

    int n = 0;
    for (const char* p = G.title_text; *p && n < max;) {
        const char* e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char t[512];
        int split;
        if (cv_tb_line(p, len, &c, t, sizeof t, &split)) {
            cv_title_line* L = &out[n++];
            L->span = split < 0;
            if (split >= 0) {
                snprintf(L->label, sizeof L->label, "%.*s", split, t);
                snprintf(L->text, sizeof L->text, "%s", t + split + 2);
            } else {
                L->label[0] = 0;
                snprintf(L->text, sizeof L->text, "%s", t);
            }
        }
        p += len + (e != NULL);
    }
    return n;
}
