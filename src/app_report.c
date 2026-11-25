/* app_report.c -- the model report (report.h) from the app: as Markdown or PDF, with
   the parts the Report builder leaves out skipped and its pictures of the view.

   Pictures take a few frames each: the camera goes to a view (not kept for undo),
   the scene is drawn with the builder window out of the way, the viewport is read
   back as an RGB PNG, and the next view follows. After the last one the camera is
   put back as it was and the report written. Markdown links the pictures, saved
   beside it (<report>_iso.png ...); a PDF places them and they are removed. */
#include "app_int.h"
#include "report.h"
#include "web.h"
#include "sokol_app.h"
#include <strings.h>

bool cv_save_png_rgb_region(const char* path, int x, int y, int w, int h, int fb_h);

#define WARM 3          /* frames drawn with a view before it is read back */

const char* app_report_pic_name(int i) {
    static const char* N[CV_REP_PIC_N] = { "Current view", "Isometric", "Front", "Top", "Right" };
    return i >= 0 && i < CV_REP_PIC_N ? N[i] : "";
}

static const char* pic_key(int i) {
    static const char* K[CV_REP_PIC_N] = { "view", "iso", "front", "top", "right" };
    return K[i];
}

/* the preset of picture i, or -1 for the view as it is. Front, top and right as
   the up axis makes them: Z up looks at the front from -Y, Y up from +Z */
static int pic_preset(int i) {
    switch (i) {
    case 1: return CV_VIEW_ISO;
    case 2: return G.up_z ? CV_VIEW_NY : CV_VIEW_PZ;
    case 3: return G.up_z ? CV_VIEW_PZ : CV_VIEW_PY;
    case 4: return CV_VIEW_PX;
    default: return -1;
    }
}

static struct {
    bool        on;
    bool        pdf;
    char        path[1100];          /* the report */
    char        dir[1100];           /* its folder ("" the working one) */
    int         pic[CV_REP_PIC_N], n, i, warm;
    char        file[CV_REP_PIC_N][300];   /* basenames, in dir */
    bool        saved[CV_REP_PIC_N];
    char        shown[700];
    cv_camera   cam;
    bool        pivot_on;
    v3          pivot;
} S;

bool app_report_busy(void) { return S.on; }

/* <model>_report.<ext> beside the model */
static void default_path(char* out, size_t n, bool pdf) {
    char base[1024];
    snprintf(base, sizeof base, "%s", G.path);
    char* dot = strrchr(base, '.');
    char* sep = strrchr(base, cv_path_sep());
    if (dot && (!sep || dot > sep)) *dot = 0;
    snprintf(out, n, "%s_report.%s", base, pdf ? "pdf" : "md");
}

/* the Markdown into a malloc'd string, through a scratch file beside path (no
   open_memstream on Windows, and tmpfile() there wants the drive's root) */
static char* report_text(const char* path, const cv_frd* f, const cv_inp* d, const cv_report_opts* o) {
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE* fp = fopen(tmp, "w+b");
    if (!fp) return NULL;
    char* s = NULL;
    if (cv_report_md(fp, f, d, o) && fseek(fp, 0, SEEK_END) == 0) {
        long n = ftell(fp);
        if (n > 0 && fseek(fp, 0, SEEK_SET) == 0 && (s = malloc((size_t)n + 1))) {
            size_t got = fread(s, 1, (size_t)n, fp);
            s[got] = 0;
        }
    }
    fclose(fp);
    remove(tmp);
    return s;
}

static bool write_report(const char* path, bool pdf, unsigned skip, const cv_report_pic* pics, int npics,
                         const char* shown, const char* dir) {
    const char* ext = strrchr(G.path, '.');
    bool only_deck = deck_loaded() && ext && !strcasecmp(ext, ".inp");    /* an .inp opened alone: its mesh is the model */
    cv_report_opts o = { only_deck ? NULL : G.path, deck_loaded() ? deck_path() : NULL, G.units, skip, pics, npics, shown };
    const cv_frd* f = only_deck ? NULL : &G.frd;
    const cv_inp* d = deck_loaded() ? deck_get() : NULL;
    bool ok;
    if (pdf) {
        char* md = report_text(path, f, d, &o);
        ok = md && cv_report_pdf(md, path, cv_basename(G.path), dir);
        free(md);
    } else {
        FILE* fp = fopen(path, "w");
        ok = fp && cv_report_md(fp, f, d, &o);
        if (fp && fclose(fp)) ok = false;
    }
    snprintf(G.note, sizeof G.note, ok ? "saved %s" : "could not write %s", path);
    G.note_t = cv_now();
    cv_msg_add(&G.msgs, 0, false, G.note);
    if (ok) CV_EXPORTED(path);
    return ok;
}

bool app_export_report(const char* path, bool pdf) {
    if (!G.loaded) return false;
    char def[1100];
    default_path(def, sizeof def, pdf);
    if (!path) path = def;
    else { const char* e = strrchr(path, '.'); pdf = e && !strcasecmp(e, ".pdf"); }
    return write_report(path, pdf, 0, NULL, 0, NULL, NULL);
}

static void view_to(int i) {
    int p = pic_preset(S.pic[i]);
    if (p >= 0) app_view_nohist(p);
    else { G.cam = S.cam; G.orbit_pivot_on = S.pivot_on; G.orbit_pivot = S.pivot; }
    S.warm = WARM;
}

static void finish(void) {
    G.cam = S.cam;                                   /* the view as it was */
    G.orbit_pivot_on = S.pivot_on;
    G.orbit_pivot = S.pivot;
    cv_report_pic pics[CV_REP_PIC_N];
    char cap[CV_REP_PIC_N][64];
    int np = 0;
    for (int k = 0; k < S.n; k++) {
        if (!S.saved[k]) continue;
        snprintf(cap[np], sizeof cap[np], "%s", app_report_pic_name(S.pic[k]));
        pics[np] = (cv_report_pic){ S.file[k], cap[np] };
        np++;
    }
    write_report(S.path, S.pdf, (unsigned)G.rep_skip, pics, np, S.shown, S.dir);
    if (S.pdf) for (int k = 0; k < S.n; k++) if (S.saved[k]) {   /* placed in the PDF: not needed beside it */
        char p[1500];
        snprintf(p, sizeof p, "%s%s%s", S.dir, S.dir[0] ? "/" : "", S.file[k]);
        remove(p);
    }
    S.on = false;
}

void app_report_build(void) {
    if (!G.loaded || S.on || app_busy()) return;
    memset(&S, 0, sizeof S);
    S.pdf = G.rep_pdf;
    default_path(S.path, sizeof S.path, S.pdf);
    snprintf(S.dir, sizeof S.dir, "%s", S.path);
    char* sep = strrchr(S.dir, cv_path_sep());
#ifdef _WIN32
    char* s2 = strrchr(S.dir, '/');
    if (s2 && (!sep || s2 > sep)) sep = s2;
#endif
    const char* name = sep ? sep + 1 : S.path;
    char stem[300];
    snprintf(stem, sizeof stem, "%s", name);
    char* dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    if (sep) *sep = 0; else S.dir[0] = 0;
    for (int i = 0; i < CV_REP_PIC_N; i++) if (G.rep_pics & (1 << i)) {
        S.pic[S.n] = i;
        snprintf(S.file[S.n], sizeof S.file[0], "%s_%s%s.png", stem, pic_key(i), S.pdf ? "_tmp" : "");
        S.n++;
    }
    if (!S.n || G.vp_w < 8 || G.vp_h < 8) { write_report(S.path, S.pdf, (unsigned)G.rep_skip, NULL, 0, NULL, NULL); return; }
    app_title_shown(S.shown, sizeof S.shown);
    S.cam = G.cam;
    S.pivot_on = G.orbit_pivot_on;
    S.pivot = G.orbit_pivot;
    S.on = true;
    view_to(0);
    snprintf(G.note, sizeof G.note, "taking the report's pictures ...");
    G.note_t = cv_now();
}

void app_report_tick(void) {
    if (!S.on) return;
    if (!G.loaded) { S.on = false; G.cam = S.cam; return; }       /* the model went away */
    if (app_busy()) return;
    if (S.warm > 0) { S.warm--; return; }
    char p[1500];
    snprintf(p, sizeof p, "%s%s%s", S.dir, S.dir[0] ? "/" : "", S.file[S.i]);
    S.saved[S.i] = cv_save_png_rgb_region(p, G.vp_x + 2, G.vp_y + 2, G.vp_w - 4, G.vp_h - 4, sapp_height());
    if (++S.i < S.n) view_to(S.i);
    else finish();
}
