/* app_title.c -- the title block's text: what the result file, the deck and the
   session say about the picture, one "Label: value" line each. The heading and
   the analysis come from the deck when there is one (*HEADING, the step's
   *STATIC / *FREQUENCY ...), else from the .frd header (1U records, the 100CL
   analysis type). A line without data is left out; ui_title.c draws them. */
#include <time.h>
#include "app_int.h"

static const char* const line_names[CV_TB_N] = {
    "Title", "Result file", "Solver", "Analysis", "Step", "Deformation", "Units", "User", "Date",
};

const char* app_title_line_name(int k) { return k >= 0 && k < CV_TB_N ? line_names[k] : ""; }

static const char* base_name(const char* p) {
    const char* b = p;
    for (const char* c = p; *c; c++) if (*c == '/' || *c == '\\') b = c + 1;
    return b;
}

static void date_of(time_t t, bool with_time, char* out, size_t n) {
    struct tm* lt = localtime(&t);
    if (!lt) { out[0] = 0; return; }
    strftime(out, n, with_time ? "%Y-%m-%d %H:%M" : "%Y-%m-%d", lt);
}

/* the result file's date: when the solver wrote it (1UDATE, 1UTIME), else the file's time */
static void file_date(char* out, size_t n) {
    const cv_frd_head* h = &G.frd.head;
    char iso[16];
    if (cv_frd_date_iso(h->date, iso, sizeof iso)) {
        snprintf(out, n, "%s%s%.5s", iso, h->time[0] ? " " : "", h->time);
        return;
    }
    uint64_t m = cv_file_mtime(G.path);
    if (m) date_of((time_t)m, true, out, n); else out[0] = 0;
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

static void step_text(const cv_step* st, char* out, size_t n) {
    const cv_inp* d = deck_get();
    bool buckle = d && d->proc && st->step >= 1 && st->step <= d->nsteps && d->proc[st->step - 1] == CV_PROC_BUCKLE;
    if (st->modal)
        snprintf(out, n, "%d, mode %d, %s %.6g", st->step, st->mode ? st->mode : st->inc, buckle ? "factor" : "f =", st->time);
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

int app_title_lines(cv_title_line* out, int max, const char* units) {
    if (!G.loaded) return 0;
    const cv_step* st = G.frd.n_steps > 0 && G.step >= 0 && G.step < G.frd.n_steps ? &G.frd.steps[G.step] : NULL;
    int n = 0;
    for (int k = 0; k < CV_TB_N + CV_TB_FREE && n < max; k++) {
        char t[160] = "";
        const char* label = k < CV_TB_N ? line_names[k] : G.title_free[k - CV_TB_N][0];
        if (k < CV_TB_N && !G.title_line[k]) continue;
        switch (k) {
        case CV_TB_TITLE: {
            const cv_inp* d = deck_get();
            snprintf(t, sizeof t, "%s", d && d->heading[0] ? d->heading : G.frd.head.heading);
            break;
        }
        case CV_TB_FILE:     snprintf(t, sizeof t, "%s", base_name(G.path)); break;
        case CV_TB_SOLVER: {
            const char* v = G.frd.head.version;
            if (!strncmp(v, "Version ", 8)) v += 8;
            snprintf(t, sizeof t, "%s%s%s", G.frd.head.pgm, G.frd.head.pgm[0] && v[0] ? " " : "", v);
            break;
        }
        case CV_TB_ANALYSIS: if (st) snprintf(t, sizeof t, "%s", analysis(st)); break;
        case CV_TB_STEP:     if (st) step_text(st, t, sizeof t); break;
        case CV_TB_SCALE:    scale_text(t, sizeof t); break;
        case CV_TB_UNITS:    if (units && strcmp(units, "not set")) snprintf(t, sizeof t, "%s", units); break;
        case CV_TB_USER:     user_name(t, sizeof t); break;
        case CV_TB_DATE:
            if (G.title_file_date) file_date(t, sizeof t);
            else date_of(time(NULL), false, t, sizeof t);
            break;
        default:             snprintf(t, sizeof t, "%s", G.title_free[k - CV_TB_N][1]); break;
        }
        if (!t[0]) continue;
        snprintf(out[n].label, sizeof out[n].label, "%s", label);
        snprintf(out[n].text, sizeof out[n].text, "%s", t);
        n++;
    }
    return n;
}
