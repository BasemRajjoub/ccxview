/* app.c -- sokol_app entry: frame loop, input, title bar, command line. The
   state lives in app_field.c / app_cam.c / app_load.c. */
#include "app_int.h"
#include "cap.h"
#include "cgx.h"
#include "quality.h"
#include "failure.h"
#include "gpu.h"
#include "web.h"
#include "log.h"
#include "export.h"
#include "video.h"
#include "ui.h"
#include "filedlg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include <strings.h>

/* the release version, from the VERSION file: the build passes it bare
   (-DCV_VERSION_NUM=1.2.3, no quotes to survive every shell) */
#define CV_STR_(x) #x
#define CV_STR(x) CV_STR_(x)
#ifdef CV_VERSION_NUM
#define CV_VERSION CV_STR(CV_VERSION_NUM)
#else
#define CV_VERSION "dev"
#endif

const char* app_version(void) { return CV_VERSION; }

/* --crash-test: a deliberate null write two calls deep, to check the crash report
   and that scripts/symbolize.sh names these two functions */
#if defined(__GNUC__)
#define CV_NOINLINE __attribute__((noinline))
#else
#define CV_NOINLINE
#endif
static int* volatile crash_test_ptr;          /* NULL, unknown to the optimiser */
static CV_NOINLINE void crash_test_b(void) { cv_log_set_context("--crash-test"); *crash_test_ptr = 1; }
static CV_NOINLINE void crash_test_a(void) { cv_logf("--crash-test: writing to NULL"); crash_test_b(); cv_logf("survived"); }

cv_app G;
static struct nk_context* g_nk;
static void app_log(const char* tag, uint32_t level, uint32_t item, const char* msg, uint32_t line,
                    const char* file, void* user);
cv_opts O = { .stl_alpha = -1, .faces = -1, .tensor = -1, .traj = -1, .look = -1, .outline = -1, .win_w = 1400, .win_h = 900, .zoom = 1.f, .shot_frames = 30 };
static struct nk_context* g_nk;

/* ---- sokol callbacks --------------------------------------------------------------- */

static char g_last_title[1400];
static void export_now(void);
static void video_frame(void);
static void video_finish(void);
static void seq_prepare(void);
static void seq_end(void);
static void event(const sapp_event* ev);

bool cv_save_png(const char* path, int w, int h);
bool cv_save_png_region(const char* path, int x, int y, int w, int h, int fb_h, const float* bg);
unsigned char* cv_read_pixels_region(int x, int y, int w, int h, int fb_h);
void cv_snk_before_shutdown(void);

static void init(void) {
#ifdef _WIN32
    cv_set_window_icon(sapp_win32_get_hwnd());
#endif
    sg_setup(&(sg_desc){ .environment = sglue_environment(), .logger.func = app_log });
    snk_setup(&(snk_desc_t){
        .dpi_scale = 1.0f,               /* we work in framebuffer pixels; ui.c scales */
        .no_default_font = true,
        .sample_count = sapp_sample_count(),
        .logger.func = app_log,
    });
    cv_render_init();
    cv_gpu_init();
    cv_mutex_init(&G.job.lock);

    G.show_faces = G.show_edges = true;
    G.show_outline = true;
    G.outline_angle = CV_CREASE_DEG;
    G.mid_faces = true;
    G.edges_auto = true;
    G.orbit_cursor = G.zoom_cursor = true;
    G.show_pivot = true;
    G.faces_mode = FM_FIELD;
    G.nodes_field = true;
    G.anim_period = 2.f;
    G.anim_factor = 1.f;
    G.fly_speed = 0.25f;
    G.fly_clip_depth = 0.005f;
    G.sel_elems = true; G.sel_visible = true; G.sel_mark_max = true;
    G.label_px = 13.f; G.minmax_n = 1; G.oor_mode[0] = G.oor_mode[1] = 1;
    G.oor_rgb[0][0] = 0.9f; G.oor_rgb[0][1] = 0.1f; G.oor_rgb[0][2] = 0.1f; G.oor_rgb[1][0] = 0.1f; G.oor_rgb[1][1] = 0.2f; G.oor_rgb[1][2] = 0.9f; G.label_spacing = 10.f; G.label_front = true;
    G.label_rgb[0] = 1.f; G.label_rgb[1] = 0.93f; G.label_rgb[2] = 0.6f;
    G.label_box_rgba[0] = G.label_box_rgba[1] = G.label_box_rgba[2] = 0.f; G.label_box_rgba[3] = 0.65f;
    G.path_lin = true;                   /* the Path window opens on the linearization */
    G.sidecar = true;                    /* each model's post-processing kept beside it */
    app_scl_clear();                     /* the first kept line's name */
    G.lin_asme = true;
    G.path_to = UINT32_MAX;
    G.show_gp = false;                   /* off by default, like nodes */
    G.gp_colored = true;
    G.gp_size = 6.f;
    G.gp_on_top = false;                 /* a point inside a shown solid stays hidden */
    G.show_hl = true;
    G.show_bc = G.show_loads = G.show_disc = G.show_links = true;
    G.vec_colored = true;
    G.vec_pct = 5.f;
    G.tensor_colored = true;
    G.tensor_scale = 1.f;
    G.traj_spacing = 2.f;
    G.bc_scale = G.load_scale = 1.f;
    G.sym_auto = true; G.sym_thick = 1.f;
    G.bg[0] = 0.33f; G.bg[1] = 0.32f; G.bg[2] = 0.31f;   /* neutral warm grey */
    G.hl_size = 8.f;
    G.geo_size = 6.f;
    G.point_size = 3;
    G.cmap = CV_CMAP_FAST;
    G.bands = 12;
    G.mesh_q = CV_MQ_CCX;
    G.mesh_warn_pct = 1.f;
    G.fail_crit = CV_FC_AUTO;
    G.center_zero = true;
    G.deform = true;
    G.deform_auto = true;
    G.deform_scale = 1;
    G.fps = 8;
    G.cam.fovy = 30.f * 3.14159265f / 180.f;
    G.cam.dist = 3;
    G.legend_decimals = 3;
    for (int k = 0; k < CV_TB_N; k++) G.title_line[k] = true;
    snprintf(G.title_free[0][0], sizeof G.title_free[0][0], "Project");
    snprintf(G.title_free[1][0], sizeof G.title_free[1][0], "Company");
    snprintf(G.title_free[2][0], sizeof G.title_free[2][0], "Checked");
    for (int q = 0; q < CV_Q_N; q++) G.unit_in[q] = G.unit_show[q] = -1;   /* the system's, as input */
    G.clip_pos = 0.5f;
    G.clip_cap = true;
    G.rep_n[0] = G.rep_n[1] = G.rep_n[2] = 3;
    G.rep_follow = true;
    G.cyc_n = G.cyc_show = 12; G.cyc_axis = 2;
    G.tree[CV_TREE_LAYERS] = G.tree[CV_TREE_GROUPS] = G.tree[CV_TREE_FIELDS] = G.tree[CV_TREE_VIEW] = 1;
    G.tree[CV_TREE_CAMERA] = G.tree[CV_TREE_COLOURS] = 1;
    G.exp_video = true; G.exp_cycles = 1; G.exp_fps = 30; G.exp_lock_range = true;
    G.watch = O.watch;
    if (!O.ui_test) settings_load();     /* the user's last choices override the defaults above */
    for (int i = 0; i < O.nopts; i++)
        if (!settings_apply(O.opts[i])) fprintf(stderr, "ccxview: --opt %s: unknown key\n", O.opts[i]);
    app_colormap(G.cmap);
    app_view(CV_VIEW_ISO);
    ui_init();
    cv_exe_dir(G.exe_dir, sizeof G.exe_dir);
    if (getenv("CCXVIEW_NO_NATIVE_DIALOG")) G.native_dlg_missing = true;
    if (O.gp) G.show_gp = true;
    if (O.vectors) G.show_vec = true;
    if (O.tensor >= 0) { G.show_tensor = true; G.tensor_style = O.tensor; }
    if (O.traj >= 0) { G.show_traj = true; G.traj_which = O.traj; }
    if (O.bg_set) memcpy(G.bg, O.bg, sizeof G.bg);
    if (O.ui_test) ui_test_start(O.ui_test, event);
    if (O.gp_size > 0) G.gp_size = O.gp_size;
    if (O.gp_under) G.gp_on_top = false;
    if (O.xray) G.gp_on_top = true;
    if (O.no_faces) G.show_faces = G.show_edges = false;   /* points only */
    if (O.no_edges) G.show_edges = false;
    if (O.outline == 0) G.show_outline = false;
    else if (O.outline > 0) { G.show_outline = true; G.outline_angle = O.outline; }
    if (O.argv_path) app_open(O.argv_path);
    if (O.browse) { G.native_dlg_missing = true; app_open_dialog(); }
}

static void fmt_bytes(char* out, size_t n, uint64_t b) {
    if (b >= (1ull << 30)) snprintf(out, n, "%.2f GB", b / 1073741824.0);
    else if (b >= (1ull << 20)) snprintf(out, n, "%.1f MB", b / 1048576.0);
    else snprintf(out, n, "%.0f KB", b / 1024.0);
}

/* Numbers in the title keep their width: figure spaces (U+2007, as wide as a
   digit) in front, so "CPU 9.4%" and "CPU 10.2%" take the same room and the
   title does not jitter. */
#define CV_FIGSP "\xE2\x80\x87"
static void pad_num(char* out, size_t n, int width, const char* num) {
    size_t k = 0;
    out[0] = 0;
    for (int i = (int)strlen(num); i < width && k + 4 < n; i++) k += (size_t)snprintf(out + k, n - k, CV_FIGSP);
    snprintf(out + k, n - k, "%s", num);
}

static void fmt_ram(char* out, size_t n, uint64_t b) {     /* "  39.3 MB", "1.25 GB": five places */
    char num[16];
    if (b >= 1000ull << 20) { snprintf(num, sizeof num, "%.2f", b / 1073741824.0); pad_num(out, n - 3, 5, num); strcat(out, " GB"); }
    else { snprintf(num, sizeof num, "%.1f", b / 1048576.0); pad_num(out, n - 3, 5, num); strcat(out, " MB"); }
}

/* cut at most max bytes, never inside a UTF-8 sequence */
static void cut_utf8(char* s, size_t max) {
    if (strlen(s) <= max) return;
    while (max > 0 && ((unsigned char)s[max] & 0xC0) == 0x80) max--;
    s[max] = 0;
}

/* Title bar: file (size) | FPS | CPU | GPU | RAM, twice a second. */
#define CV_TITLE_MAX 120
static void update_title(void) {
    double now = cv_now();
    G.title_frames++;
    if (G.title_t == 0) { G.title_t = now; cv_cpu_percent(); return; }
    if (now - G.title_t < 0.5) return;
    float fps = (float)(G.title_frames / (now - G.title_t));
    G.title_t = now;
    G.title_frames = 0;
    char num[32], fsz[32], title[1400], stats[400], stats_short[300], fpss[40], cpus[40], ram[48], gpu[160], gpu_short[64];
    snprintf(num, sizeof num, "%.0f", fps);
    pad_num(fpss, sizeof fpss, 3, num);
    snprintf(num, sizeof num, "%.1f", cv_cpu_percent());
    pad_num(cpus, sizeof cpus, 5, num);
    fmt_ram(ram, sizeof ram, cv_rss_bytes());
    /* GPU: on Windows this process's share, as Task Manager shows it; on Linux
       the whole GPU's load ("total"); elsewhere no figure, only what renders */
    {
        bool whole = false;
        float pct = cv_gpu_percent(&whole);
        int k = snprintf(gpu, sizeof gpu, "GPU");
        if (pct >= 0) {
            char p[24];
            snprintf(num, sizeof num, "%.0f", pct);
            pad_num(p, sizeof p, 3, num);
            k += snprintf(gpu + k, sizeof gpu - (size_t)k, "%s %s%%", whole ? " total" : "", p);
        }
        snprintf(gpu_short, sizeof gpu_short, "%s", gpu);
        if (cv_gpu_name()[0]) snprintf(gpu + k, sizeof gpu - (size_t)k, " %s%s", cv_gpu_name(), cv_gpu_is_software() ? " (software)" : "");
    }
    snprintf(stats, sizeof stats, "%s FPS  |  CPU %s%%  |  %s  |  RAM %s", fpss, cpus, gpu, ram);
    snprintf(stats_short, sizeof stats_short, "%s FPS  |  CPU %s%%  |  %s  |  RAM %s", fpss, cpus, gpu_short, ram);
    /* sokol keeps at most 127 bytes of title, and on Windows shows nothing at
       all when that is full: drop the GPU name, then cut the file name */
    for (int pass = 0; pass < 2; pass++) {
        const char* st = pass ? stats_short : stats;
        if (G.loaded) {
            fmt_bytes(fsz, sizeof fsz, G.file_bytes);
            snprintf(title, sizeof title, "ccxview " CV_VERSION " - %s (%s)  |  %s", cv_basename(G.path), fsz, st);
        } else {
            snprintf(title, sizeof title, "ccxview " CV_VERSION "  |  %s", st);
        }
        if (strlen(title) <= CV_TITLE_MAX) break;
    }
    if (strlen(title) > CV_TITLE_MAX) {                     /* a very long file name: keep its start */
        char tail[400];
        const char* bar = strstr(title, ")  |  ");
        snprintf(tail, sizeof tail, "%s", bar ? bar : "");
        size_t keep = strlen(tail) + 3 < CV_TITLE_MAX ? CV_TITLE_MAX - strlen(tail) - 3 : 0;
        if (bar && keep > 10) {
            cut_utf8(title, keep);
            strcat(title, "...");
            strcat(title, tail);
        }
        cut_utf8(title, CV_TITLE_MAX);
    }
    sapp_set_window_title(title);
    snprintf(g_last_title, sizeof g_last_title, "%s", title);
}

/* ---- free flight ---------------------------------------------------------------- */

static bool g_keys[SAPP_MAX_KEYCODES];

void app_set_flight(bool on) {
    G.flight = on;
    if (on) G.cam.ortho = false;              /* a flying eye needs perspective */
    memset(g_keys, 0, sizeof g_keys);
}

/* WASD in the view plane, E/Space up, Q down (the up axis), Shift x4. The step
   is the wall-clock time since the last tick: sapp_frame_duration() is an average
   over many frames, and moving by the average while the frame rate wobbles (as it
   does under a stream of mouse-look events) makes the motion stutter. */
static void tick_flight(void) {
    static double last;
    double now = cv_now();
    float dt = last > 0 ? (float)(now - last) : 0.f;
    last = now;
    if (!G.flight || !G.loaded) return;
    if (dt > 0.1f) dt = 0.1f;
    v3 eye, fwd, right, up;
    cam_basis(&G.cam, &eye, &fwd, &right, &up);
    v3 v = v3_make(0, 0, 0);
    if (g_keys[SAPP_KEYCODE_W]) v = v3_add(v, fwd);
    if (g_keys[SAPP_KEYCODE_S]) v = v3_sub(v, fwd);
    if (g_keys[SAPP_KEYCODE_D]) v = v3_add(v, right);
    if (g_keys[SAPP_KEYCODE_A]) v = v3_sub(v, right);
    if (g_keys[SAPP_KEYCODE_E] || g_keys[SAPP_KEYCODE_SPACE]) v = v3_add(v, cam_up_axis());
    if (g_keys[SAPP_KEYCODE_Q]) v = v3_sub(v, cam_up_axis());
    if (v3_dot(v, v) == 0) return;
    float sp = G.fly_speed * G.diag * dt;
    if (g_keys[SAPP_KEYCODE_LEFT_SHIFT] || g_keys[SAPP_KEYCODE_RIGHT_SHIFT]) sp *= 4;
    G.cam.target = v3_add(G.cam.target, v3_scale(v3_norm(v), sp));   /* the eye moves with it */
}

/* The caps of the clip plane. Walking only slides the eye's plane (a cut of the
   prepared projection); turning the head changes its normal, and a new normal
   projects every node again. Where that costs more than a frame, the caps wait
   while the head turns and come back when it rests; the cut itself never waits. */
static void eye_caps(bool eye_clip, const cv_draw* d) {
    static float last_n[3], built_n[3];
    static double turned, cost;
    double now = cv_now();
    if (memcmp(d->clip_n, last_n, sizeof last_n)) { memcpy(last_n, d->clip_n, sizeof last_n); turned = now; }
    bool on = d->clip && !(eye_clip && cost > 0.012 && now - turned < 0.15);
    bool rebuild = on && memcmp(d->clip_n, built_n, sizeof built_n);
    double t0 = cv_now();
    app_clip_caps(on, d->clip_n, d->clip_d, d->def_scale, d->def_scale2);
    if (rebuild) { cost = cv_now() - t0; memcpy(built_n, d->clip_n, sizeof built_n); }   /* what a new normal costs */
    if (!on) memset(built_n, 0, sizeof built_n);        /* off frees the projection: the next one is a rebuild */
}

/* Displacement animation: the deformation swings within the current step. */
static void tick_anim(void) {
    G.anim_factor2 = 0.f;
    if (!G.anim_on) { G.anim_factor = 1.f; return; }
    double ph = fmod(cv_now(), (double)CV_MAX(G.anim_period, 0.1f)) / CV_MAX(G.anim_period, 0.1f);
    if (G.seq_left > 0 && !G.seq_steps)     /* exporting the deformation: exact cycles */
        ph = (double)(((G.seq_total - G.seq_left) % G.seq_total + G.seq_total) % G.seq_total) / G.seq_total;
    const double two_pi = 6.283185307179586;
    if (G.harmonic) {                    /* a steady-state response turns through its phase */
        G.anim_factor = (float)cos(two_pi * ph);
        G.anim_factor2 = (float)sin(two_pi * ph);
        return;
    }
    G.anim_factor = G.anim_mode == 0 ? (float)(0.5 - 0.5 * cos(two_pi * ph))
                                     : (float)sin(two_pi * ph);
}

/* watching: reopen when the file on disk changed and stopped growing (ccx
   writes a step at a time; a half-written step reads as truncated, so wait
   for a quiet second) */
static void poll_watch(void) {
    if (!G.watch || !G.loaded || app_busy()) return;
    double now = cv_now();
    if (now - G.watch_t < 1.0) return;
    G.watch_t = now;
    uint64_t mt = cv_file_mtime(G.path), sz = cv_file_size(G.path);
    static uint64_t seen_mt, seen_sz;
    if (mt == G.watch_mtime && sz == G.watch_size) return;
    if (mt == seen_mt && sz == seen_sz) { app_reload(); return; }   /* unchanged for a second: settled */
    seen_mt = mt; seen_sz = sz;
}

static void frame(void) {
    poll_job();
    poll_dialog();
    poll_watch();
    seq_prepare();
    update_title();
    tick_anim();
    tick_flight();
    if (O.menu_set && !O.box_set && G.loaded && !app_busy() && G.vp_w > 0) {   /* --menu: after --box */
        O.menu_set = false;
        app_menu_open(G.vp_x + O.menu[0] * G.vp_w, G.vp_y + O.menu[1] * G.vp_h);
    }
    if (O.box_set && G.loaded && !app_busy() && G.vp_w > 0) {     /* --box: once the view has its size */
        O.box_set = false;
        G.nav_box[0] = G.vp_x + O.box[0] * G.vp_w; G.nav_box[1] = G.vp_y + O.box[1] * G.vp_h;
        G.nav_box[2] = G.vp_x + O.box[2] * G.vp_w; G.nav_box[3] = G.vp_y + O.box[3] * G.vp_h;
        if (app_box_select(G.nav_box[0], G.nav_box[1], G.nav_box[2], G.nav_box[3]))
            fprintf(stderr, "box: %u elements (%s), max %g min %g over %u\n", G.sel_n, G.sel_crossing ? "crossing" : "window",
                    G.boxq.vmax, G.boxq.vmin, G.boxq.n);
    }

    if (O.integ && !O.box_set && G.loaded && !app_busy()) {    /* --integrate: after --box, for a selection */
        if (!app_integ_open_spec(O.integ)) cv_msg_add(&G.msgs, 0, false, G.note);
        O.integ = NULL;
    }

    if (G.playing && G.loaded && G.frd.n_steps > 1) {
        double now = cv_now();
        if (now - G.last_tick >= 1.0 / CV_MAX(G.fps, 0.1f)) {
            G.last_tick = now;
            app_set_step((G.step + 1) % G.frd.n_steps);
        }
    }

    app_traj_tick();
    app_sidecar_tick();
    ui_test_frame(g_nk);                 /* --ui-test: the script's next step, as events */
    g_nk = snk_new_frame();
    ui_frame(g_nk, sapp_width(), sapp_height());

    cv_draw d = {0};
    if (G.loaded) {
        cam_matrices(d.mvp, d.mv, d.proj);
        d.diag = G.diag;
        d.def_scale = G.deform ? G.deform_scale * G.anim_factor : 0.f;
        d.def_scale2 = G.deform ? G.deform_scale * G.anim_factor2 : 0.f;
        d.rmin = G.rmin; d.rmax = G.rmax;
        if (!(d.rmax > d.rmin)) { d.rmin -= 0.5f; d.rmax += 0.5f; }  /* constant field */
        d.bands = G.bands;
        for (int k = 0; k < 2; k++) {            /* values outside a locked range */
            int m = G.range_lock ? G.oor_mode[k] : 0;
            float g = k ? CV_OOR_BELOW : CV_OOR_ABOVE;
            const float* c = m == 2 ? G.oor_rgb[k] : NULL;
            d.oor[k][0] = c ? c[0] : g; d.oor[k][1] = c ? c[1] : g; d.oor[k][2] = c ? c[2] : g;
            d.oor[k][3] = m == 3 ? 3.f : m ? 1.f : 0.f;
        }
        /* a .dat field lives on the Gauss points only: the faces stay plain */
        int fm = (!G.has_field || G.field_src == 1) ? CV_COLOR_SOLID
               : G.elem_mode ? CV_COLOR_ELEM : CV_COLOR_NODAL;
        d.faces = G.show_faces;
        d.faces_color = G.faces_mode == FM_FIELD ? fm
                      : G.faces_mode == FM_PLAIN ? CV_COLOR_SOLID : CV_COLOR_GROUP;
        /* Edges thinner than ~3 px paint more line than face and the surface reads
           as solid black, so the layer steps aside until you zoom in. Wireframe
           (faces off) always keeps them: there the edges ARE the model. It hides
           below 3 px and returns above 4, so a zoom near the limit does not flicker
           them on and off; the panel says so, and "hide when dense" turns it off. */
        float px_per_unit = (float)G.vp_h / (2.f * G.cam.dist * tanf(G.cam.fovy * 0.5f));
        float edge_px = G.mean_edge * px_per_unit;
        if (G.edges_dense ? edge_px > 4.f : edge_px < 3.f) G.edges_dense = !G.edges_dense;
        bool dense = G.edges_auto && G.show_faces && G.edges_dense;      /* nodes and Gauss points step aside too */
        d.edges = G.show_edges && !dense;   d.edges_color = G.edges_field && G.has_field ? CV_COLOR_NODAL : CV_COLOR_SOLID;
        d.outline = G.show_outline;          /* the outline is what is left to read when the edges step aside */
        d.points = G.show_nodes && !dense;  d.points_color = G.nodes_field && G.has_field ? CV_COLOR_NODAL : CV_COLOR_SOLID;
        d.point_size = G.point_size * ui_scale();
        d.shade = G.shading;
        d.gauss_points = G.show_gp && !dense;
        d.highlights = G.show_hl;
        d.geo_points = G.show_geo_pts; d.geo_curves = G.show_geo_crv; d.geo_surfaces = G.show_geo_srf;
        d.geo_size = G.geo_size * ui_scale();
        d.supports = G.show_bc; d.loads = G.show_loads; d.discrete = G.show_disc; d.links = G.show_links;
        d.vectors = G.show_vec; d.vectors_color = G.vec_colored && G.has_field ? CV_COLOR_NODAL : CV_COLOR_SOLID;
        d.tensors = G.show_tensor; d.tensors_color = G.tensor_colored && G.has_field ? CV_COLOR_NODAL : CV_COLOR_SOLID;
        d.sign_lim = app_tensor_cross_lim();
        d.glyph_signed = cv_glyph_signed(G.tensor_style);
        d.traj = G.show_traj;
        d.ghost = G.show_ghost && G.deform && G.disp;
        d.markers = G.show_markers; d.marker_size = 12.f * ui_scale();
        d.path = G.path_n > 0;
        bool eye_clip = G.flight && G.fly_clip == CV_EYE_CUT, eye_hide = G.flight && G.fly_clip == CV_EYE_HIDE;
        if (eye_hide) {
            v3 eye, fwd, right, up;
            cam_basis(&G.cam, &eye, &fwd, &right, &up);
            const float e[3] = { eye.x, eye.y, eye.z }, f[3] = { fwd.x, fwd.y, fwd.z };
            float n[3], dd;
            cv_cap_eye_plane(e, f, G.fly_clip_depth * G.diag, n, &dd);
            app_eye_hide(true, n, dd, d.def_scale, d.def_scale2);
        } else app_eye_hide(false, NULL, 0, 0, 0);
        d.clip = G.clip_on || eye_clip;
        if (eye_clip) {                        /* flying: the eye cuts, the axis plane waits */
            v3 eye, fwd, right, up;
            cam_basis(&G.cam, &eye, &fwd, &right, &up);
            const float e[3] = { eye.x, eye.y, eye.z }, f[3] = { fwd.x, fwd.y, fwd.z };
            cv_cap_eye_plane(e, f, G.fly_clip_depth * G.diag, d.clip_n, &d.clip_d);
        } else if (G.clip_on) {                /* plane normal along an axis, through clip_pos of the model box */
            const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
            int k = G.clip_axis % 3;
            float sgn = G.clip_flip ? -1.f : 1.f;
            d.clip_n[0] = d.clip_n[1] = d.clip_n[2] = 0; d.clip_n[k] = sgn;
            d.clip_d = sgn * (lo[k] + (hi[k] - lo[k]) * G.clip_pos);
        }
        eye_caps(eye_clip, &d);
        d.hl_size = G.hl_size * ui_scale();
        d.labels = G.label_kinds != 0 || app_measure_count() > 0;   /* the measurements are labels too */
        d.labels_on_top = G.label_front || G.label_probe_only ||   /* names, Gauss points and the probe's: inside the model */
                          (G.label_kinds & (1 << CV_LABEL_SETS | 1 << CV_LABEL_LINKS | 1 << CV_LABEL_MATERIALS | 1 << CV_LABEL_GPVALUE | 1 << CV_LABEL_GPID));
        memcpy(d.label_rgb, G.label_rgb, sizeof d.label_rgb); memcpy(d.label_box_rgba, G.label_box_rgba, sizeof d.label_box_rgba);
        d.gauss_on_top = G.gp_on_top;
        d.gauss_points_color = G.gp_colored && G.has_field ? CV_COLOR_NODAL : CV_COLOR_SOLID;
        d.gauss_size = G.gp_size * ui_scale();
        d.gauss_rgb[0] = 0.85f; d.gauss_rgb[1] = 0.25f; d.gauss_rgb[2] = 0.25f;
        float fc[3] = { 0.74f, 0.76f, 0.80f }, ec[3] = { 0.07f, 0.07f, 0.08f }, pc[3] = { 0.10f, 0.10f, 0.14f };
        memcpy(d.face_rgb, fc, sizeof fc); memcpy(d.edge_rgb, ec, sizeof ec); memcpy(d.point_rgb, pc, sizeof pc);
        d.vp_x = G.vp_x; d.vp_y = G.vp_y; d.vp_w = G.vp_w; d.vp_h = G.vp_h;
        app_stl_draw(&d);
    }
    app_marks_sync();
    app_measure_sync();
    if (G.loaded) {
        app_label_frame(&d);
        if (d.labels) cv_render_label_depth(&d);  /* its own pass, before the frame's */
    }
    sg_begin_pass(&(sg_pass){
        .action = {
            .colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = { G.bg[0], G.bg[1], G.bg[2], G.png_alpha && G.export_req ? 0.f : 1.f } },
        },
        .swapchain = sglue_swapchain(),
    });
    if (G.loaded) {
        cv_draw d0 = d;                        /* the model's own frame: imported geometry */
        cv_render_meshes(&d0, false);          /* opaque: before the labels, which leave the depth as it is */
        cv_render_draw(&d);
        int nc = app_copies();
        d.labels = false;                      /* labels belong to the file's nodes: not on the copies */
        if (nc > 1) {                          /* the mirror and replicate copies */
            float mvp0[16], mv0[16], M[16];
            memcpy(mvp0, d.mvp, sizeof mvp0); memcpy(mv0, d.mv, sizeof mv0);
            for (int m = 1; m < nc; m++) {
                app_copy_matrix(m, M);
                m4_mul(d.mvp, mvp0, M);
                m4_mul(d.mv, mv0, M);
                cv_render_draw(&d);
            }
        }
        cv_render_meshes(&d0, true);           /* see-through: over everything drawn */
        sg_apply_viewport(0, 0, sapp_width(), sapp_height(), true);
        sg_apply_scissor_rect(0, 0, sapp_width(), sapp_height(), true);
    }
    snk_render(sapp_width(), sapp_height());
    sg_end_pass();
    /* a transparent PNG: two frames cleared to alpha 0 first (the read-back sees the frame before) */
    if (G.export_req) {
        if (G.png_alpha && G.png_alpha_arm < 2) G.png_alpha_arm++;
        else { G.export_req = false; G.png_alpha_arm = 0; export_now(); }
    }
    if (G.seq_left > 0 && !app_busy() && G.seq_warm > 0) G.seq_warm--;
    else if (G.seq_left > 0 && !app_busy()) {
        if (G.video) video_frame(); else export_now();
        if (--G.seq_left == 0) { seq_end(); if (G.video) video_finish(); }
    }
    /* --shot: wait for the load to finish, then a few frames for the UI to settle */
    if (O.shot_path && !app_busy() && !G.video && G.seq_left == 0 && ++O.frame_no >= O.shot_frames) {
        if (g_last_title[0]) fprintf(stderr, "title: %s\n", g_last_title);
        for (size_t i = 0; i < G.frd.msgs.n; i++) fprintf(stderr, "msg: %s\n", G.frd.msgs.a[i].text);
        for (size_t i = 0; i < G.msgs.n; i++) fprintf(stderr, "msg: %s\n", G.msgs.a[i].text);
        fprintf(stderr, "field: src %d '%s' comp %d has %d step %d\n", G.field_src, G.field_name, G.comp, G.has_field, G.step);
        fprintf(stderr, "view: bbox (%g %g %g)-(%g %g %g) diag %g  deform %d x%g (auto %g)  tris %zu  dist %g\n",
                G.bmin.x, G.bmin.y, G.bmin.z, G.bmax.x, G.bmax.y, G.bmax.z, G.diag, G.deform, G.deform_scale,
                G.auto_scale, G.skin.n_tri, G.cam.dist);
        fprintf(stderr, "cam: target (%g %g %g) vbox (%g %g %g)-(%g %g %g) disp %s pts %zu\n", G.cam.target.x, G.cam.target.y,
                G.cam.target.z, G.vmin.x, G.vmin.y, G.vmin.z, G.vmax.x, G.vmax.y, G.vmax.z, G.disp ? "yes" : "no", G.skin.n_pt);
        if (!cv_save_png(O.shot_path, sapp_width(), sapp_height()))
            fprintf(stderr, "cannot write %s\n", O.shot_path);
        sapp_request_quit();
    }
    if (ui_test_on()) {                  /* --ui-test: a picture of each failure, then quit */
        const char* shot = ui_test_shot();
        if (shot && !cv_save_png(shot, sapp_width(), sapp_height())) fprintf(stderr, "cannot write %s\n", shot);
        if (ui_test_result() >= 0) sapp_request_quit();
    }
    sg_commit();
}

/* Export: the 3D view as drawn (legend and axes included, panels excluded),
   next to the model as <model>_step<N>.png, numbered so nothing is overwritten. */
void app_export_png(void) { if (G.loaded) G.export_req = true; }

/* one animation cycle as <model>_stepN_animNN.png: Animate is forced on for
   the run and restored afterwards */
/* MP4: the frames the sequence export would save as PNGs go to the encoder */
static char g_video_path[1100];

static void video_frame(void) {
    unsigned char* px = cv_read_pixels_region(G.vp_x + 2, G.vp_y + 2, G.vp_w - 4, G.vp_h - 4, sapp_height());
    if (!px || !cv_video_frame(G.video, px, G.vp_w - 4, G.vp_h - 4, true)) {
        cv_msg_add(&G.msgs, 0, false, "video: a frame could not be encoded");
        G.seq_left = 1;                        /* stop after this one */
    }
    free(px);
}

static void video_finish(void) {
    bool ok = cv_video_close(G.video);
    G.video = NULL;
    snprintf(G.note, sizeof G.note, ok ? "saved %s" : "could not write %s", g_video_path);
    G.note_t = cv_now();
    cv_msg_add(&G.msgs, 0, false, G.note);
    if (ok) CV_EXPORTED(g_video_path);
}

/* arm a frame export: `per` frames form one unit (a deformation cycle, or all
   steps), repeated `cycles` times; the frame after this one is the first recorded */
static void seq_begin(int per, int cycles, bool steps) {
    G.seq_anim = G.anim_on;
    G.seq_steps = steps;
    G.seq_step0 = G.step;
    G.seq_lock0 = G.range_lock;
    if (G.exp_lock_range) G.range_lock = true;    /* the legend stays put through the clip */
    if (!steps) G.anim_on = true;
    G.seq_total = per;
    G.seq_left = per * cycles;
    G.seq_cycles = cycles;
    G.seq_warm = 1;
}

static void seq_end(void) {
    G.anim_on = G.seq_anim;
    G.range_lock = G.seq_lock0;
    if (G.seq_steps) app_set_step(G.seq_step0);
    G.seq_steps = false;
    if (!G.range_lock) app_refresh_range();
}

/* steps mode: before a frame is drawn, put the increment it shows in place */
static void seq_prepare(void) {
    if (G.seq_left <= 0 || !G.seq_steps || app_busy()) return;
    int all = G.seq_total * G.seq_cycles, done = all - G.seq_left;
    int want = G.seq_warm > 0 ? 0 : (done / CV_MAX(G.seq_hold, 1)) % CV_MAX(G.frd.n_steps, 1);
    if (want != G.step) app_set_step(want);
}

static bool video_begin(const char* suffix) {
    char base[1024];
    snprintf(base, sizeof base, "%s", G.path);
    char* dot = strrchr(base, '.');
    char* sep = strrchr(base, cv_path_sep());
    if (dot && (!sep || dot > sep)) *dot = 0;
    int k = 0;
    do {
        if (k == 0) snprintf(g_video_path, sizeof g_video_path, "%s_%s.mp4", base, suffix);
        else snprintf(g_video_path, sizeof g_video_path, "%s_%s_%d.mp4", base, suffix, k);
        k++;
    } while (cv_file_size(g_video_path) > 0 && k < 1000);
    G.video = cv_video_open(g_video_path, G.vp_w - 4, G.vp_h - 4, CV_MAX(G.exp_fps, 1), 4);
    if (!G.video) cv_msg_add(&G.msgs, 0, false, "video: cannot start the encoder (view too small?)");
    return G.video != NULL;
}

/* the Export section's animation export: what to loop through, and whether
   the frames become PNGs or an MP4 */
void app_export_animation(void) {
    if (!G.loaded || G.video || G.seq_left > 0) return;
    int fps = CV_MAX(G.exp_fps, 1);
    if (G.exp_kind == 1) {                        /* every step once, held as long as play shows it */
        if (G.frd.n_steps < 1) return;
        int hold = G.exp_video ? CV_MAX(1, (int)((float)fps / CV_MAX(G.fps, 0.5f) + 0.5f)) : 1;
        if (G.exp_video && !video_begin("steps")) return;
        G.seq_hold = hold;
        seq_begin(G.frd.n_steps * hold, 1, true);
    } else {                                      /* deformation swing of this step */
        char suffix[32];
        snprintf(suffix, sizeof suffix, "step%d", G.step + 1);
        if (G.exp_video && !video_begin(suffix)) return;
        int per_cycle = CV_MAX(2, (int)(fps * CV_MAX(G.anim_period, 0.5f)));
        G.seq_hold = 1;
        seq_begin(per_cycle, CV_MAX(G.exp_cycles, 1), false);
    }
}

int app_export_progress(int* done, int* total) {
    int all = G.video ? G.seq_total * G.seq_cycles : G.seq_total;
    if (G.seq_left <= 0 || all <= 0) return 0;
    *done = all - G.seq_left;
    *total = all;
    return 1;
}

void app_export_cancel(void) {
    if (G.seq_left <= 0) return;
    G.seq_left = 0;
    seq_end();
    if (G.video) {
        video_finish();                        /* keeps the frames written so far */
        char kept[1100];
        snprintf(kept, sizeof kept, "%s", G.note);
        snprintf(G.note, sizeof G.note, "export stopped; %s", kept);
    } else {
        snprintf(G.note, sizeof G.note, "export stopped");
    }
    G.note_t = cv_now();
}


bool app_export_data(bool vtk) {
    if (!G.loaded) return false;
    char base[1024], path[1100];
    snprintf(base, sizeof base, "%s", G.path);
    char* dot = strrchr(base, '.');
    char* sep = strrchr(base, cv_path_sep());
    if (dot && (!sep || dot > sep)) *dot = 0;
    snprintf(path, sizeof path, "%s_step%d.%s", base, G.step + 1, vtk ? "vtk" : "csv");
    const float* sc = G.has_field && G.field_src != 1 && !G.elem_mode ? G.scalar : NULL;
    bool ok = vtk ? cv_export_vtk(path, &G.frd, G.disp, sc, G.field_label, G.vis)
                  : cv_export_csv(path, &G.frd, G.disp, sc, G.field_label, G.vis);
    snprintf(G.note, sizeof G.note, ok ? "saved %s" : "could not write %s", path);
    G.note_t = cv_now();
    cv_msg_add(&G.msgs, 0, false, G.note);
    if (ok) CV_EXPORTED(path);
    return ok;
}

static void export_now(void) {
    char base[1024], path[1100];
    snprintf(base, sizeof base, "%s", G.path);
    char* dot = strrchr(base, '.');
    char* sep = strrchr(base, cv_path_sep());
    if (dot && (!sep || dot > sep)) *dot = 0;
    int k = 0;
    if (G.seq_left > 0 && G.seq_steps) {
        snprintf(path, sizeof path, "%s_steps%03d.png", base, G.step + 1);
    } else if (G.seq_left > 0) {
        snprintf(path, sizeof path, "%s_step%d_anim%02d.png", base, G.step + 1, G.seq_total - G.seq_left);
    } else do {
        if (k == 0) snprintf(path, sizeof path, "%s_step%d.png", base, G.step + 1);
        else snprintf(path, sizeof path, "%s_step%d_%d.png", base, G.step + 1, k);
        k++;
    } while (cv_file_size(path) > 0 && k < 1000);
    /* 2 px in from each side: the neighbouring panels draw their borders on the edge */
    bool ok = cv_save_png_region(path, G.vp_x + 2, G.vp_y + 2, G.vp_w - 4, G.vp_h - 4, sapp_height(), G.png_alpha && G.seq_left == 0 ? G.bg : NULL);
    snprintf(G.note, sizeof G.note, ok ? "saved %s" : "could not write %s", path);
    G.note_t = cv_now();
    cv_msg_add(&G.msgs, 0, false, G.note);
    if (ok) CV_EXPORTED(path);
}

static void cleanup(void) {
    if (app_busy()) cv_thread_join(&G.job.thread);
    if (!O.shot_path && !O.nopts && !O.ui_test) settings_save(sapp_width(), sapp_height());   /* scripted runs leave the file alone */
    settings_free();
    unload();
    cv_render_shutdown();
    cv_snk_before_shutdown();
    snk_shutdown();
    sg_shutdown();
    if (O.ui_test && ui_test_result() != 0) exit(1);     /* failed, or closed before the end */
}

/* A drag and what it does, fixed when the button goes down:
     left: orbit (shift: pan, ctrl: box zoom, alt: roll; X/Y/Z held: about that world axis)
     right / middle: pan (ctrl: zoom by dragging up and down)
   A click without moving: left probes, middle centres the view on the point. */
static struct { bool down; int button, mode; float x0, y0, x, y; bool moved, pivot_on; v3 pivot; cv_camera cam0; } drag;
static float g_mx, g_my;                 /* last mouse position, for the keys that act at the cursor */
static double g_wheel_t;                 /* last wheel zoom: a run of turns is one step of the view history */

static bool in_view(float x, float y) {
    return x >= G.vp_x && y >= G.vp_y && x < G.vp_x + G.vp_w && y < G.vp_y + G.vp_h;
}

static void nav_mark(int mode, v3 p) { G.nav_mode = mode; G.nav_pt = p; }

static void note(const char* s) { snprintf(G.note, sizeof G.note, "%s", s); G.note_t = cv_now(); }

static void roll_by(float a) {
    if (!cam_roll(a)) note("Rotation set to free: a turntable cannot roll");
}

/* one mouse move of a drag */
static void drag_move(float dx, float dy, bool shift) {
    v3 eye0, f0, r0, u0;
    cam_basis(&G.cam, &eye0, &f0, &r0, &u0);
    int mode = drag.mode;
    if (mode == CV_NAV_ROTATE && shift) mode = CV_NAV_PAN;
    switch (mode) {
        case CV_NAV_PAN: {
            /* the point of the model that was grabbed stays under the cursor: in
               perspective a pixel is worth more the deeper the point lies. Off the
               model (or parallel projection, where depth does not matter): the target's. */
            float depth = G.cam.dist;
            if (drag.pivot_on && !G.cam.ortho) depth = CV_MAX(v3_dot(v3_sub(drag.pivot, eye0), f0), 1e-6f * G.cam.dist);
            float k = 2.f * depth * tanf(G.cam.fovy * 0.5f) / (float)CV_MAX(G.vp_h, 1);
            G.cam.target = v3_add(G.cam.target, v3_add(v3_scale(r0, -dx * k), v3_scale(u0, dy * k)));
            nav_mark(CV_NAV_PAN, drag.pivot_on ? drag.pivot : G.cam.target);
            break;
        }
        case CV_NAV_ZOOM: {                          /* drag up: closer */
            float f = powf(1.01f, dy);
            if (G.zoom_cursor) G.cam.target = v3_add(drag.pivot, v3_scale(v3_sub(G.cam.target, drag.pivot), f));
            G.cam.dist *= f;
            nav_mark(CV_NAV_ZOOM, G.zoom_cursor ? drag.pivot : G.cam.target);
            break;
        }
        case CV_NAV_ROLL: {                          /* the angle swept about the view centre */
            float cx = G.vp_x + 0.5f * G.vp_w, cy = G.vp_y + 0.5f * G.vp_h;
            float a = atan2f(drag.y - cy, drag.x - cx) - atan2f(drag.y - dy - cy, drag.x - dx - cx);
            if (a > 3.14159265f) a -= 6.2831853f;
            if (a < -3.14159265f) a += 6.2831853f;
            roll_by(-a);
            nav_mark(CV_NAV_ROLL, G.cam.target);
            break;
        }
        case CV_NAV_BOX:
            G.nav_box[2] = drag.x; G.nav_box[3] = drag.y;
            G.nav_mode = CV_NAV_BOX;
            break;
        case CV_NAV_LOOK: {                          /* flight: turn about the eye, not the target */
            cam_orbit(-dx * 0.004f, dy * 0.004f);
            v3 eye1, f1, r1, u1;
            cam_basis(&G.cam, &eye1, &f1, &r1, &u1);
            G.cam.target = v3_add(eye0, v3_scale(f1, G.cam.dist));
            break;
        }
        default: {                                   /* orbit */
            v3 pv = drag.pivot_on ? drag.pivot : G.cam.target;
            const float k = 0.008f;
            int ax = g_keys[SAPP_KEYCODE_X] ? 0 : g_keys[SAPP_KEYCODE_Y] ? 1 : g_keys[SAPP_KEYCODE_Z] ? 2 : -1;
            if (ax >= 0) {                           /* ParaView: about a world axis only */
                v3 w = v3_make(ax == 0, ax == 1, ax == 2);
                float s = v3_dot(w, u0) < 0 ? -1.f : 1.f;      /* follow the mouse when the axis points down */
                cam_turn(w, -s * (fabsf(dx) >= fabsf(dy) ? dx : dy) * k, pv);
            } else {
                cam_orbit(-dx * k, dy * k);
                if (drag.pivot_on) {                  /* turn about the pivot: it keeps its place in the view */
                    v3 q = v3_sub(drag.pivot, eye0);
                    float a = v3_dot(q, r0), b = v3_dot(q, u0), c = v3_dot(q, f0);
                    v3 eye1, f1, r1, u1;
                    cam_basis(&G.cam, &eye1, &f1, &r1, &u1);
                    v3 e = v3_sub(drag.pivot, v3_add(v3_scale(r1, a), v3_add(v3_scale(u1, b), v3_scale(f1, c))));
                    G.cam.target = v3_add(e, v3_scale(f1, G.cam.dist));
                }
            }
            nav_mark(CV_NAV_ROTATE, drag.pivot_on ? drag.pivot : G.cam.target);
            break;
        }
    }
}

static void event(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL && g_nk) ui_wheel_focus(g_nk);
    bool wheel = ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL;
    bool over_ui = g_nk && ui_mouse_captured(g_nk, wheel);
    /* Nuklear scrolls its active window wherever the mouse is: a wheel turn over the
       3D view is the view's alone, or the sidebar would scroll while the model zooms */
    bool handled = wheel && !over_ui ? false : snk_handle_event(ev);
    /* Nuklear counts a hovered display-only overlay (the ids, the navigation mark) as its
       own, so with one up every press in the view was taken for the interface. A press is
       the view's unless a window that takes input is under it or a text box is being edited. */
    bool nk_busy = handled && (over_ui || (g_nk && ui_text_focus(g_nk)));

    switch (ev->type) {
        case SAPP_EVENTTYPE_FILES_DROPPED:
#ifdef __EMSCRIPTEN__
            cv_web_fetch_drops();
#else
            if (sapp_get_num_dropped_files() > 0) app_open(sapp_get_dropped_file_path(0));   /* a .stl joins the model */
#endif
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            if (G.menu_on && !over_ui) { G.menu_on = false; if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) break; }   /* a click beside the menu only closes it */
            if (!over_ui && !nk_busy && in_view(ev->mouse_x, ev->mouse_y)) {
                bool ctrl = (ev->modifiers & (SAPP_MODIFIER_CTRL | SAPP_MODIFIER_SUPER)) != 0;
                bool alt = (ev->modifiers & SAPP_MODIFIER_ALT) != 0, shift = (ev->modifiers & SAPP_MODIFIER_SHIFT) != 0;
                bool left = ev->mouse_button == SAPP_MOUSEBUTTON_LEFT;
                drag.down = true; drag.moved = false; drag.button = ev->mouse_button;
                drag.x0 = drag.x = ev->mouse_x; drag.y0 = drag.y = ev->mouse_y;
                drag.cam0 = G.cam;
                G.box_pick = left && !G.flight && ((ctrl && shift) || G.box_arm);   /* max in a box, not a zoom */
                drag.mode = G.flight ? (left ? CV_NAV_LOOK : CV_NAV_PAN)
                          : left ? (ctrl || G.box_pick ? CV_NAV_BOX : alt ? CV_NAV_ROLL : shift ? CV_NAV_PAN : CV_NAV_ROTATE)
                          : ctrl ? CV_NAV_ZOOM : CV_NAV_PAN;
                /* rotate about the part of the model that was grabbed; off the model, about the target */
                bool on = false;
                drag.pivot_on = false;
                if ((drag.mode == CV_NAV_ROTATE && G.orbit_cursor) || drag.mode == CV_NAV_PAN)
                    drag.pivot_on = app_cursor_point(ev->mouse_x, ev->mouse_y, &drag.pivot, &on) && on;
                else if (drag.mode == CV_NAV_ZOOM && !app_cursor_point(ev->mouse_x, ev->mouse_y, &drag.pivot, &on))
                    drag.pivot = G.cam.target;
                if (drag.mode == CV_NAV_BOX) {
                    G.nav_box[0] = G.nav_box[2] = drag.x0; G.nav_box[1] = G.nav_box[3] = drag.y0;
                }
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            if (drag.down && !drag.moved && drag.button == SAPP_MOUSEBUTTON_LEFT && drag.mode != CV_NAV_BOX) {
                do_pick(ev->mouse_x, ev->mouse_y);
            }
            if (drag.down && !drag.moved && drag.button == SAPP_MOUSEBUTTON_RIGHT && !G.flight)   /* a right click: the menu */
                app_menu_open(ev->mouse_x, ev->mouse_y);
            if (drag.down && !drag.moved && drag.button == SAPP_MOUSEBUTTON_MIDDLE && !G.flight) {
                app_view_push();
                app_center_at(ev->mouse_x, ev->mouse_y);
            }
            if (drag.down && drag.mode == CV_NAV_BOX && drag.moved) {
                if (G.box_pick) {
                    G.box_arm = false;
                    if (!app_box_select(G.nav_box[0], G.nav_box[1], G.nav_box[2], G.nav_box[3]))
                        cv_msg_add(&G.msgs, 0, false, G.nav_box[2] < G.nav_box[0] ? "box: no shown element has a node in it"
                                                                                  : "box: no shown element lies wholly in it (drag right to left to take the ones it crosses)");
                } else {
                    app_view_push();
                    app_box_zoom(G.nav_box[0], G.nav_box[1], G.nav_box[2], G.nav_box[3]);
                }
            }
            drag.down = false;
            G.nav_live = false;
            break;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            g_mx = ev->mouse_x; g_my = ev->mouse_y;
            if (drag.down) {
                float dx = ev->mouse_x - drag.x, dy = ev->mouse_y - drag.y;
                drag.x = ev->mouse_x; drag.y = ev->mouse_y;
                if (!drag.moved && fabsf(drag.x - drag.x0) + fabsf(drag.y - drag.y0) > 4) {
                    drag.moved = true;
                    if (drag.mode != CV_NAV_BOX) {       /* the view before the drag, for Ctrl+Z */
                        cv_camera now = G.cam;
                        G.cam = drag.cam0; app_view_push(); G.cam = now;
                    }
                }
                if (!drag.moved && drag.mode == CV_NAV_BOX) break;    /* a click, not a box */
                G.nav_live = drag.mode != CV_NAV_LOOK;
                drag_move(dx, dy, (ev->modifiers & SAPP_MODIFIER_SHIFT) != 0);
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            if (!over_ui && in_view(ev->mouse_x, ev->mouse_y)) {
                if (G.flight) G.fly_speed = CV_MIN(CV_MAX(G.fly_speed * powf(1.2f, ev->scroll_y), 0.005f), 10.f);
                else {
                    double now = cv_now();
                    if (now - g_wheel_t > 0.5) app_view_push();
                    g_wheel_t = now;
                    /* toward the cursor: scaling the view about the point under it keeps that point in place */
                    float f = powf(0.88f, G.wheel_invert ? -ev->scroll_y : ev->scroll_y);
                    v3 p; bool on;
                    if (G.zoom_cursor && app_cursor_point(ev->mouse_x, ev->mouse_y, &p, &on))
                        G.cam.target = v3_add(p, v3_scale(v3_sub(G.cam.target, p), f));
                    G.cam.dist *= f;
                }
            }
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            if ((int)ev->key_code >= 0 && (int)ev->key_code < (int)SAPP_MAX_KEYCODES) g_keys[ev->key_code] = false;
            break;
        case SAPP_EVENTTYPE_UNFOCUSED:
            memset(g_keys, 0, sizeof g_keys);
            break;
        case SAPP_EVENTTYPE_KEY_DOWN: {
            /* typing in a text box. Not nk_busy: that is true whenever the cursor is over
               any Nuklear window (legend, gizmo, the hint bar), and a look drag in flight
               sweeps the cursor over them, which would drop the movement keys */
            if (g_nk && ui_text_focus(g_nk)) break;
            if ((int)ev->key_code >= 0 && (int)ev->key_code < (int)SAPP_MAX_KEYCODES) g_keys[ev->key_code] = true;
            bool ctrl = (ev->modifiers & (SAPP_MODIFIER_CTRL | SAPP_MODIFIER_SUPER)) != 0;
            if (G.flight && !ev->key_repeat && !ctrl) {
                if (ev->key_code == SAPP_KEYCODE_ESCAPE) { app_set_flight(false); break; }
                /* movement keys belong to the flight while it runs */
                switch (ev->key_code) {
                    case SAPP_KEYCODE_W: case SAPP_KEYCODE_A: case SAPP_KEYCODE_S: case SAPP_KEYCODE_D:
                    case SAPP_KEYCODE_E: case SAPP_KEYCODE_Q: case SAPP_KEYCODE_SPACE: goto key_done;
                    default: break;
                }
            }
            switch (ev->key_code) {
                case SAPP_KEYCODE_ESCAPE: app_pick_cancel(); break;
                case SAPP_KEYCODE_0: case SAPP_KEYCODE_KP_0:
                    if (ctrl) { ui_zoom(0); }
                    break;
                case SAPP_KEYCODE_R: if (!ctrl) app_view(CV_VIEW_ISO); break;
                case SAPP_KEYCODE_H: if (!ctrl) G.hide_panels = !G.hide_panels; break;
                case SAPP_KEYCODE_1: case SAPP_KEYCODE_2: case SAPP_KEYCODE_3:
                case SAPP_KEYCODE_4: case SAPP_KEYCODE_5: case SAPP_KEYCODE_6:
                    if (!ctrl) app_view(CV_VIEW_PX + (ev->key_code - SAPP_KEYCODE_1));
                    break;
                /* Windows and the web name keys by their place on a US keyboard: on a German
                   one the + key arrives as ] and the - key as /, so those count too */
                case SAPP_KEYCODE_EQUAL: case SAPP_KEYCODE_KP_ADD: case SAPP_KEYCODE_RIGHT_BRACKET:
                    if (ctrl) ui_zoom(+1); else if (ev->key_code != SAPP_KEYCODE_RIGHT_BRACKET) { G.deform_scale *= 1.25f; G.deform_auto = false; }
                    break;
                case SAPP_KEYCODE_MINUS: case SAPP_KEYCODE_KP_SUBTRACT: case SAPP_KEYCODE_SLASH:
                    if (ctrl) ui_zoom(-1); else if (ev->key_code != SAPP_KEYCODE_SLASH) { G.deform_scale /= 1.25f; G.deform_auto = false; }
                    break;
                case SAPP_KEYCODE_O: if (ctrl) app_open_dialog(); break;
                case SAPP_KEYCODE_L: if (ctrl) ui_focus_open(); break;
                case SAPP_KEYCODE_F: if (ctrl) G.find_open = true; else { app_view_push(); app_fit(); } break;
                case SAPP_KEYCODE_E: if (ctrl) app_export_png(); break;
                case SAPP_KEYCODE_G: if (!ctrl && !ev->key_repeat) app_set_flight(!G.flight); break;
                case SAPP_KEYCODE_SPACE: G.playing = !G.playing; G.last_tick = 0; break;
                case SAPP_KEYCODE_Z: if (ctrl) app_view_undo((ev->modifiers & SAPP_MODIFIER_SHIFT) ? +1 : -1); break;
                case SAPP_KEYCODE_Y: if (ctrl) app_view_undo(+1); break;
                case SAPP_KEYCODE_C:
                    if (!ctrl && !G.flight && in_view(g_mx, g_my)) {
                        app_view_push();
                        app_center_at(g_mx, g_my);
                    }
                    break;
                case SAPP_KEYCODE_N:
                    if (!ctrl && !G.flight && in_view(g_mx, g_my)) {
                        cv_camera c = G.cam;
                        if (app_normal_to(g_mx, g_my)) { cv_camera n = G.cam; G.cam = c; app_view_push(); G.cam = n; }
                        else note("Normal to: point at a face of the model");
                    }
                    break;
                /* arrows: steps; Ctrl: turn the view 15 degrees (Shift: 90), Alt: roll */
                case SAPP_KEYCODE_RIGHT: case SAPP_KEYCODE_LEFT: case SAPP_KEYCODE_UP: case SAPP_KEYCODE_DOWN: {
                    bool alt = (ev->modifiers & SAPP_MODIFIER_ALT) != 0;
                    int k = ev->key_code;
                    if ((ctrl || alt) && !G.flight) {
                        float a = ((ev->modifiers & SAPP_MODIFIER_SHIFT) ? 90.f : 15.f) * 3.14159265f / 180.f;
                        app_view_push();
                        if (alt) { if (k == SAPP_KEYCODE_LEFT || k == SAPP_KEYCODE_RIGHT) roll_by(k == SAPP_KEYCODE_RIGHT ? -a : a); }
                        else if (k == SAPP_KEYCODE_LEFT || k == SAPP_KEYCODE_RIGHT) cam_orbit(k == SAPP_KEYCODE_RIGHT ? -a : a, 0);
                        else cam_orbit(0, k == SAPP_KEYCODE_UP ? -a : a);
                    }
                    else if (k == SAPP_KEYCODE_RIGHT) app_set_step(G.step + 1);
                    else if (k == SAPP_KEYCODE_LEFT) app_set_step(G.step - 1);
                    break;
                }
                case SAPP_KEYCODE_HOME:  app_set_step(0); break;
                case SAPP_KEYCODE_END:   app_set_step(G.frd.n_steps - 1); break;
                default: break;
            }
        key_done:
            break;
        }
        default: break;
    }
}

/* sokol's logger, plus the software fallback: a fatal GL context error on the
   window backend restarts the process with a software rasteriser. */
static void app_log(const char* tag, uint32_t level, uint32_t item, const char* msg, uint32_t line,
                    const char* file, void* user) {
    /* llvmpipe & co strip unused shader globals; sokol then warns on every start */
    if (level >= 2 && tag && strcmp(tag, "sg") == 0 && !cv_log_verbose() &&
        (item == SG_LOGITEM_GL_UNIFORMBLOCK_NAME_NOT_FOUND_IN_SHADER || item == SG_LOGITEM_GL_IMAGE_SAMPLER_NAME_NOT_FOUND_IN_SHADER))
        return;
    cv_logf("%s: %s%s (item %u, line %u)", tag ? tag : "?", level == 0 ? "PANIC " : level == 1 ? "error " : level == 2 ? "warning " : "",
            msg ? msg : "", item, line);
    if (level == 0 && tag && strcmp(tag, "sapp") == 0) {
        bool gl_fail = false;
#if defined(_WIN32)
        gl_fail = item >= SAPP_LOGITEM_WIN32_WGL_FIND_PIXELFORMAT_FAILED && item <= SAPP_LOGITEM_WIN32_WGL_CREATE_CONTEXT_ATTRIBS_FAILED_OTHER;
#elif defined(__linux__)
        gl_fail = item >= SAPP_LOGITEM_LINUX_GLX_LOAD_LIBGL_FAILED && item <= SAPP_LOGITEM_LINUX_GLX_CREATE_WINDOW_FAILED;
#endif
        if (gl_fail) cv_gpu_fallback();       /* returns only when no fallback exists */
    }
    slog_func(tag, level, item, msg, line, file, user);
}

sapp_desc sokol_main(int argc, char* argv[]) {
#ifdef __EMSCRIPTEN__
    cv_web_init();
#endif
#ifdef _WIN32
    cv_attach_console();                      /* --check, --help etc. from a terminal */
#endif
    cv_gpu_remember_args(argc, argv);
    bool software = cv_gpu_is_software_run();
    const char* check = NULL;
    bool size_set = false, crash_test = false;
    if (getenv("CCXVIEW_LOG")) cv_log_open(getenv("CCXVIEW_LOG"));
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) O.shot_path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) O.shot_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--browse")) O.browse = true;
        else if (!strcmp(argv[i], "--fly")) O.fly = true;
        else if (!strcmp(argv[i], "--mesh-window")) O.mesh_window = true;
        else if (!strcmp(argv[i], "--measure-window")) O.measure_window = true;
        else if (!strcmp(argv[i], "--details")) O.details = true;
        else if (!strcmp(argv[i], "--about")) O.about = true;
        else if (!strcmp(argv[i], "--title-block") && O.nopts < 32) O.opts[O.nopts++] = "title_on=1";
        else if (!strcmp(argv[i], "--deck-window")) O.deck_window = true;
        else if (!strcmp(argv[i], "--labels") && i + 1 < argc) O.labels = argv[++i];
        else if (!strcmp(argv[i], "--range") && i + 1 < argc) O.range_set = sscanf(argv[++i], "%f,%f", &O.range[0], &O.range[1]) == 2;
        else if (!strcmp(argv[i], "--menu") && i + 1 < argc) O.menu_set = sscanf(argv[++i], "%f,%f", &O.menu[0], &O.menu[1]) == 2;
        else if (!strcmp(argv[i], "--box") && i + 1 < argc)
            O.box_set = sscanf(argv[++i], "%f,%f,%f,%f", &O.box[0], &O.box[1], &O.box[2], &O.box[3]) == 4;
        else if (!strcmp(argv[i], "--fly-clip") || !strcmp(argv[i], "--fly-hide")) {
            O.fly = true; O.fly_clip = argv[i][6] == 'c' ? CV_EYE_CUT : CV_EYE_HIDE;
            char* end;
            if (i + 1 < argc) { float v = strtof(argv[i + 1], &end); if (end != argv[i + 1] && !*end && v > 0) { O.fly_depth = v; i++; } }
        }
        else if (!strcmp(argv[i], "--gauss")) O.gauss = true;
        else if (!strcmp(argv[i], "--cgx")) O.cgx = true;
        else if (!strcmp(argv[i], "--mirror") && i + 1 < argc)
            for (const char* c = argv[++i]; *c; c++) {
                int k = (*c | 32) - 'x';
                if (k >= 0 && k < 3) O.mirror[k] = true;
            }
        else if (!strcmp(argv[i], "--gp")) O.gp = true;
        else if (!strcmp(argv[i], "--vectors")) O.vectors = true;
        else if (!strcmp(argv[i], "--tensor") && i + 1 < argc) {
            O.tensor = cv_glyph_style(argv[++i]);
            if (O.tensor < 0)
                fprintf(stderr, "ccxview: --tensor %s: ellipsoid, superquadric, cross, schultz-kindlmann, reynolds or hwy\n", argv[i]);
        }
        else if (!strcmp(argv[i], "--trajectories") && i + 1 < argc) {
            const char* t = argv[++i];
            O.traj = !strcmp(t, "s1") ? 0 : !strcmp(t, "s3") ? 1 : !strcmp(t, "both") ? 2 : -1;
            if (O.traj < 0) fprintf(stderr, "ccxview: --trajectories %s: s1, s3 or both\n", t);
        }
        else if (!strcmp(argv[i], "--conv")) O.conv = true;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            int w, h;
            if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 200 && h > 200) { O.win_w = w; O.win_h = h; size_set = true; }
        }
        else if (!strcmp(argv[i], "--software")) software = true;
        else if (!strcmp(argv[i], "--crash-test")) crash_test = true;
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) cv_log_open(argv[++i]);
        else if (!strcmp(argv[i], "--verbose")) cv_log_set_verbose(true);
        else if (!strcmp(argv[i], "--check") && i + 1 < argc) check = argv[++i];
        else if (!strcmp(argv[i], "--ui-test") && i + 1 < argc) O.ui_test = argv[++i];
        else if (!strcmp(argv[i], "--version")) { printf("ccxview %s\n", CV_VERSION); exit(0); }
        else if (!strcmp(argv[i], "--field") && i + 1 < argc) O.field = argv[++i];
        else if (!strcmp(argv[i], "--calc") && i + 1 < argc) O.calc = argv[++i];
        else if (!strcmp(argv[i], "--fail") && i + 1 < argc) O.fail = argv[++i];
        else if (!strcmp(argv[i], "--mesh") && i + 1 < argc) O.mesh = argv[++i];
        else if (!strcmp(argv[i], "--target") && i + 1 < argc)
            O.target_set = sscanf(argv[++i], "%f,%f,%f", &O.target[0], &O.target[1], &O.target[2]) == 3;
        else if (!strcmp(argv[i], "--zoom") && i + 1 < argc) O.zoom = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--export")) {
            const char* k = i + 1 < argc ? argv[i + 1] : "";
            O.export_kind = !strcmp(k, "csv") ? 3 : !strcmp(k, "vtk") ? 4 : !strcmp(k, "seq") ? 5 : !strcmp(k, "mp4") ? 6
                          : !strcmp(k, "seqsteps") ? 7 : !strcmp(k, "mp4steps") ? 8 : 1;
            if (O.export_kind != 1 || !strcmp(k, "png")) i++;
        }
        else if (!strcmp(argv[i], "--opt") && i + 1 < argc && O.nopts < 32) O.opts[O.nopts++] = argv[++i];
        else if (!strcmp(argv[i], "--find") && i + 1 < argc) O.find = argv[++i];
        else if (!strcmp(argv[i], "--view") && i + 1 < argc) O.view_file = argv[++i];
        else if (!strcmp(argv[i], "--no-sidecar")) O.no_sidecar = true;
        else if (!strcmp(argv[i], "--watch")) O.watch = true;
        else if (!strcmp(argv[i], "--compare") && i + 1 < argc) O.compare = argv[++i];
        else if (!strcmp(argv[i], "--stl") && i + 1 < argc) { if (O.nstl < CV_MESH_N) O.stl[O.nstl++] = argv[i + 1]; i++; }
        else if (!strcmp(argv[i], "--stl-alpha") && i + 1 < argc) O.stl_alpha = fminf(fmaxf((float)atof(argv[++i]), 0.f), 1.f);
        else if (!strcmp(argv[i], "--path") && i + 1 < argc) O.path_ids = argv[++i];
        else if (!strcmp(argv[i], "--measure") && i + 1 < argc && O.nmeasure < 16) O.measure[O.nmeasure++] = argv[++i];
        else if (!strcmp(argv[i], "--history") && i + 1 < argc) O.hist_id = atol(argv[++i]);
        else if (!strcmp(argv[i], "--linearize") && i + 1 < argc) O.lin_ids = argv[++i];
        else if (!strcmp(argv[i], "--integrate") && i + 1 < argc) O.integ = argv[++i];
        else if (!strcmp(argv[i], "--integrate-csv") && i + 1 < argc) O.integ_csv = argv[++i];
        else if (!strcmp(argv[i], "--look") && i + 1 < argc) {
            static const char* names[] = { "iso", "+x", "-x", "+y", "-y", "+z", "-z" };
            const char* v = argv[++i];
            for (int k = 0; k < 7; k++) if (!strcasecmp(v, names[k])) O.look = k;
        }
        else if (!strcmp(argv[i], "--bg") && i + 1 < argc) {
            const char* v = argv[++i];
            O.bg_set = true;
            if (!strcmp(v, "white")) O.bg[0] = O.bg[1] = O.bg[2] = 1.f;
            else if (!strcmp(v, "black")) O.bg[0] = O.bg[1] = O.bg[2] = 0.f;
            else if (sscanf(v, "%f,%f,%f", &O.bg[0], &O.bg[1], &O.bg[2]) != 3) O.bg_set = false;
        }
        else if (!strcmp(argv[i], "--set") && i + 1 < argc && O.nsets < 8) O.sets[O.nsets++] = argv[++i];
        else if (!strcmp(argv[i], "--hide-set") && i + 1 < argc && O.nhide_sets < 8) O.hide_sets[O.nhide_sets++] = argv[++i];
        else if (!strcmp(argv[i], "--step") && i + 1 < argc) O.step = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gp-under")) O.gp_under = true;
        else if (!strcmp(argv[i], "--xray")) O.xray = true;
        else if (!strcmp(argv[i], "--no-faces")) O.no_faces = true;
        else if (!strcmp(argv[i], "--no-edges")) O.no_edges = true;
        else if (!strcmp(argv[i], "--outline") && i + 1 < argc) {
            const char* v = argv[++i];
            O.outline = !strcmp(v, "off") ? 0.f : !strcmp(v, "on") ? CV_CREASE_DEG : fminf(fmaxf((float)atof(v), 0.1f), 180.f);
        }
        else if (!strcmp(argv[i], "--gp-size") && i + 1 < argc) O.gp_size = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--crop") && i + 1 < argc)
            O.crop_set = sscanf(argv[++i], "%f,%f,%f,%f,%f,%f", &O.crop[0], &O.crop[1], &O.crop[2],
                                &O.crop[3], &O.crop[4], &O.crop[5]) == 6;
        else if (!strcmp(argv[i], "--faces") && i + 1 < argc) {
            static const char* n[FM_N] = { "field", "type", "material", "group", "plain" };
            i++;
            for (int k = 0; k < FM_N; k++) if (!strcmp(argv[i], n[k])) O.faces = k;
        }
        else O.argv_path = argv[i];
    }
    if (check) exit(app_check(check));      /* headless: parse, print the messages, no window */
    if (O.integ_csv) {                       /* headless: the integrals' CSV, no window */
        if (!O.integ || !O.argv_path) { fprintf(stderr, "--integrate-csv OUT needs a model and --integrate KIND:TARGET\n"); exit(1); }
        exit(app_integ_headless(O.argv_path, O.integ, O.field, O.integ_csv));
    }
    if (!size_set && !O.ui_test) settings_window_size(&O.win_w, &O.win_h);
    cv_log_install_crash_handler(".", "ccxview " CV_VERSION);
    if (crash_test) crash_test_a();
    if (software) cv_gpu_software_mode();
    if (getenv("CCXVIEW_FAKE_NO_GPU") && !software) {   /* testing the fallback path */
        fprintf(stderr, "ccxview: CCXVIEW_FAKE_NO_GPU set, pretending the GL context failed\n");
        cv_gpu_fallback();
    }
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .event_cb = event,
#ifdef __EMSCRIPTEN__
        .html5.canvas_selector = "#canvas",    /* the canvas follows the page: no width / height here */
        .max_dropped_files = 8,                /* a .frd with its siblings */
#else
        .width = O.win_w,
        .height = O.win_h,
        .max_dropped_files = 1,
#endif
        .sample_count = 4,
        .high_dpi = true,
        .enable_dragndrop = true,
        .enable_clipboard = true,
        .clipboard_size = 1 << 20,           /* "copy all" in the message window */
        .max_dropped_file_path_length = 4096,
        .window_title = "ccxview",
        .gl = { .major_version = 4, .minor_version = 1 },
        .logger.func = app_log,
    };
}

