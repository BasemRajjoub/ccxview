/* app.h -- application state shared by app.c (loop, loading, camera) and ui.c. */
#ifndef CV_APP_H
#define CV_APP_H

#include "frd.h"
#include "mesh.h"
#include "field.h"
#include "os.h"
#include "render.h"
#include "vmath.h"
#include "dat.h"
#include "inp.h"
#include "fbd.h"
#include "sta.h"

/* what the faces are coloured by */
enum { FM_FIELD, FM_TYPE, FM_MAT, FM_GRP, FM_PLAIN, FM_N };   /* FM_TYPE + axis = FM for that axis */

/* symmetry plane position along its axis */
enum { CV_SYM_ZERO, CV_SYM_MIN, CV_SYM_MAX, CV_SYM_N };

enum { CV_VIEW_ISO, CV_VIEW_PX, CV_VIEW_NX, CV_VIEW_PY, CV_VIEW_NY, CV_VIEW_PZ, CV_VIEW_NZ };

typedef struct {
    v3    target;
    float dist, yaw, pitch, fovy;
    bool  ortho;
} cv_camera;

/* One decoded (step, field) pair. */
typedef struct {
    int    step, field;
    float* vals;
    size_t bytes;
    uint64_t used;           /* LRU stamp */
} cv_cache_entry;

enum { CV_CACHE_N = 8 };

/* Background work: loading a file, or rebuilding the skin after a group change. */
enum { JOB_NONE, JOB_LOAD, JOB_SKIN };

typedef struct {
    int       kind;
    cv_thread thread;
    cv_mutex  lock;
    bool      done;          /* guarded by lock */
    double    started;
    /* inputs */
    char      path[1024];
    const cv_frd* frd;       /* JOB_SKIN: read-only while the job runs */
    uint8_t*  vis;
    bool      crop;
    float     crop_lo[3], crop_hi[3];   /* world coordinates */
    /* outputs */
    bool      ok;
    char      err[256];
    cv_map    map;
    cv_frd    frd_out;
    cv_groups groups;
    cv_skin   skin;
    cv_dat    dat;            /* sibling .dat, if any */
    cv_sta    sta;            /* sibling .sta / .cvg: increments and iterations */
    cv_inp    deck;           /* the .inp (opened, or beside the .frd) */
    char      deck_path[1024];
    bool      has_deck;
    bool      deck_geometry;  /* no .frd: the deck's mesh is what is shown */
    char      dat_path[1024];
    bool      has_dat;
    cv_fbd    fbd;            /* cgx geometry (.fbd opened) */
    bool      has_fbd;
    bool      eval_cgx;       /* evaluate a cgx script with cgx */
    bool      fbd_evaluated;
    char      cgx_log[1024];
    double    seconds;
} cv_job;

typedef struct {
    /* file */
    char      path[1024];
    char      open_buf[1024];
    int       open_len;
    bool      loaded;
    cv_map    map;
    cv_frd    frd;
    cv_groups groups;
    cv_skin   skin;
    v3        bmin, bmax;
    float     diag;
    v3        vmin, vmax;     /* view box: undeformed + deformed as drawn */
    float     vdiag;
    float     mean_edge;      /* mean skin edge length, for the edge fade */
    cv_msgs   msgs;           /* loader + decode messages (frd.msgs holds the parser's) */
    double    load_seconds;

    cv_job    job;
    bool      skin_dirty;     /* a group changed while a job was running */

    /* crop box, as fractions of the model bounding box per axis */
    bool      crop_on;
    float     crop_lo[3], crop_hi[3];

    /* active field: name + option, looked up again in every step */
    int       step;
    char      field_name[64];   /* .frd names are short; .dat names are phrases */
    int       comp;
    bool      has_field;      /* current step has the selected field */
    bool      elem_mode;
    int       field_src;      /* 0: .frd (nodal), 1: .dat (integration points) */
    uint8_t*  vis;            /* last visibility mask (groups + crop), NULL = all */

    /* integration-point display */
    bool      show_gp;
    bool      gp_colored;
    float     gp_size;
    bool      gp_on_top;      /* Gauss points drawn through the faces */
    int       probe_ip;       /* point hit by the probe (1-based), 0 none */
    char      field_label[64];

    cv_cache_entry cache[CV_CACHE_N];
    uint64_t  cache_clock;

    float*    scalar;         /* n_nodes */
    float*    elem_val;       /* n_elems */
    float*    tri_val;        /* n_tri */
    float*    disp;           /* 3 * n_nodes, NULL when the step has no DISP */
    float*    disp2;          /* harmonic response: -DISPI (imaginary part), NULL otherwise */
    bool      harmonic;       /* the step holds DISP + DISPI: animate as DISP cos wt - DISPI sin wt */
    float     data_min, data_max;
    uint32_t  min_at, max_at;        /* node (or element) holding the extreme, UINT32_MAX none */
    size_t    nan_count;             /* values without data */
    bool      show_markers;          /* min / max balls */
    bool      show_ghost;            /* undeformed edges behind the deformed shape */
    bool      show_ids;              /* node / element ids of the picked element */
    bool      find_open;             /* Ctrl+F window */
    int       seq_left, seq_total;   /* PNG sequence export: frames still to write */
    bool      seq_anim;              /* Animate before the export, restored after */
    int       seq_cycles;            /* video: cycles recorded */
    bool      seq_steps;             /* the frames run through the increments instead of the deformation phase */
    int       seq_hold;              /* steps mode: frames per increment */
    int       seq_step0;             /* steps mode: the step to return to */
    bool      seq_lock0;             /* range lock before the export, restored after */
    /* the Export section's choices */
    int       exp_kind;              /* 0 deformation cycle, 1 every step */
    bool      exp_video;             /* MP4 instead of a PNG sequence */
    int       exp_cycles;            /* deformation cycles recorded */
    int       exp_fps;               /* frames per second of a video / cycle */
    bool      exp_lock_range;        /* keep the colour range fixed while recording */
    int       exp_open;              /* the Export section expanded (nk_collapse_states) */
    int       seq_warm;              /* frames to let pass before recording: the arming frame is drawn with the old state */
    void*     video;                 /* an MP4 being written (cv_video*), frames of the sequence go there */
    /* clip plane: everything beyond it is cut away at draw time (no cap) */
    bool      clip_on;
    int       clip_axis;             /* 0..2 normal along X / Y / Z */
    bool      clip_flip;
    float     clip_pos;              /* fraction of the model box along that axis */
    /* watching the file: reload when the solver writes more */
    bool      watch;
    uint64_t  watch_mtime, watch_size;
    double    watch_t;
    bool      reload_keep;           /* this load keeps camera, step and field */
    int       keep_step;
    char      keep_field[64];
    int       keep_comp;
    cv_camera keep_cam;
    /* path plot: the field along the shortest surface path between two picked nodes */
    uint32_t  path_a;                /* first node, UINT32_MAX none */
    bool      path_arm;              /* waiting for the second click */
    uint32_t* path_nodes;  uint32_t path_n;
    float*    path_dist;
    bool      path_open;             /* the plot window */
    /* comparison: a second results file on the same mesh, shown as A - B */
    cv_map    cmp_map;
    cv_frd    cmp;
    bool      cmp_on;
    char      cmp_path[1024];
    bool      diff_mode;
    bool      dlg_for_compare;       /* the open dialog picks the comparison file */
    float     rmin, rmax;
    bool      range_lock;
    bool      center_zero;

    bool      deform;
    bool      deform_auto;
    float     deform_scale;
    float     auto_scale;

    /* layers */
    bool      show_faces, show_edges, show_nodes;
    int       faces_mode;            /* FM_* */
    bool      edges_field, nodes_field;
    bool      edges_auto;            /* hide the edges while they are denser than ~3 px */
    bool      edges_dense;           /* ... and they are now (with hysteresis) */
    float*    axis_rgb[CV_AXIS_N];   /* 3 floats per group value */
    float     point_size;
    bool      shading;               /* off: flat true colours; on: light + shadow */
    int       cmap;
    int       bands;

    /* view */
    cv_camera cam;
    bool      sym[3];                /* mirror copies across a plane normal to X, Y, Z */
    int       sym_at[3];             /* CV_SYM_*: where that plane sits */
    bool      up_z;                  /* turntable about world Z instead of Y */
    bool      orbit_cursor;          /* drag rotates about the model point under the cursor */
    bool      zoom_cursor;           /* the wheel zooms toward the cursor */
    bool      flight;                /* free flight: WASD + mouse look */
    float     fly_speed;             /* model diagonals per second */
    int       vp_x, vp_y, vp_w, vp_h;   /* 3D area in window pixels */

    /* displacement animation (within one step) */
    bool      anim_on;
    int       anim_mode;             /* 0: 0 -> 1 -> 0,  1: -1 .. +1 (mode shapes) */
    float     anim_period;           /* seconds per cycle */
    float     anim_factor;           /* current multiplier on the deformation */
    float     anim_factor2;          /* ... and on the imaginary part (harmonic steps) */

    /* title bar */
    uint64_t  file_bytes;
    double    title_t;
    int       title_frames;

    /* step playback */
    bool      playing;
    float     fps;
    double    last_tick;

    /* probe */
    bool      probe_on;
    cv_pick   probe;
    float     probe_value;

    bool      show_msgs;
    cv_sta    sta;                   /* convergence history of the run, if the .sta / .cvg were beside it */
    bool      show_conv;             /* the convergence window */
    float     bg[3];                 /* view background */
    bool      export_req;            /* save the 3D view as PNG after this frame */
    char      note[1100];            /* short status-bar note (e.g. where an export went) */
    double    note_t;
    bool      show_geo_pts, show_geo_crv, show_geo_srf;   /* cgx geometry layers */
    float     geo_size;
    bool      show_hl;               /* deck set / surface highlights */
    float     hl_size;
    bool      show_bc, show_loads;   /* deck supports / loads as glyphs */
    bool      show_disc;             /* springs / dashpots / masses as symbols */
    bool      show_links;            /* coupling spiders */
    bool      show_vec;              /* arrows of the current 3-component field */
    bool      vec_colored;
    float     vec_pct;               /* longest arrow, percent of the model diagonal */
    float     glyph_pct;             /* glyph length, percent of the model diagonal */

    /* file dialogs */
    char      exe_dir[1024];
    bool      native_dlg_missing;  /* portal / native dialog not available: use ours */
    bool      dlg_running;
    bool      browser_open;
    char      browse_dir[1024];
    bool      show_help;
    bool      hide_legend, hide_axes; /* View: legend and axes gizmo off */
    bool      hide_panels;           /* H: the 3D view alone, for screenshots */
    /* legend look (right-click the legend) */
    bool      legend_reverse;        /* colour map turned around */
    bool      legend_grey;           /* greyscale, for printing */
    int       legend_fmt;            /* 0 auto, 1 fixed decimals, 2 scientific */
    int       legend_decimals;
    bool      legend_edit;           /* the legend settings window is open */
} cv_app;

extern cv_app G;

/* actions the UI calls */
void app_open(const char* path);
void app_open_dialog(void);          /* native dialog, else the built-in browser */
void app_start_dir(char* out, size_t n);
void app_set_step(int step);
void app_select(const char* field, int comp);
void app_select_src(const char* field, int comp, int src);
void app_set_elem_mode(bool on);
void app_groups_changed(void);
void app_fit(void);
void app_fit_element(uint32_t e);    /* centre the orbit on element e, close up */
int  app_check(const char* path);    /* --check: headless parse, messages to stdout; exit code */
void app_sym_toggled(int axis);      /* picks the plane, reframes */
void app_sym_changed(void);          /* plane moved: reframe */
float app_sym_plane(int axis);
void app_export_png(void);
void app_export_animation(void);         /* per the Export section: deformation cycles or every step, PNGs or MP4 */
int  app_export_progress(int* done, int* total);  /* a frame export running? fills done / total */
void app_export_cancel(void);            /* stop a frame export; a video keeps what it has */
bool app_export_data(bool vtk);          /* nodes + field as CSV, or mesh + field as VTK, beside the model */
bool app_project(v3 p, float* sx, float* sy);   /* world -> window pixels; false when behind the eye */
void app_goto_node(uint32_t node);       /* centre the camera on a node, probe it */
bool app_find(uint32_t id, bool element);/* by file id: probe + centre; false if absent */
void app_reload(void);                   /* open the same file again, keeping camera, step and field */
void app_path_start(uint32_t node);      /* first node of the path; the next pick ends it */
void app_path_end(uint32_t node);
void app_path_clear(void);
bool app_path_csv(const char* path);     /* dist, id, x, y, z, value per node */
bool app_compare_open(const char* path); /* second .frd on the same mesh; false with a message when not */
void app_compare_close(void);
bool app_view_save(const char* path);    /* camera, step, field, layers -> a small INI */
bool app_view_load(const char* path);
void app_view(int preset);
void app_colormap(int cm);
void app_set_faces_mode(int fm);
void app_set_flight(bool on);
void app_group_colors_changed(void);
void app_refresh_range(void);
bool app_busy(void);
size_t app_total_msgs(void);

/* app_gauss.c: integration-point fields from a .dat */
void gp_set(cv_dat* d, const char* path);
void gp_clear(void);
bool gp_loaded(void);
const char* gp_path(void);
int  gp_fields(const char** names, int max);
bool gp_desc(const char* name, cv_field_desc* d);
bool gp_refresh(void);
void gp_refresh_geometry(void);
void gp_build_nodal(void);
void app_gauss_changed(void);          /* layer toggled: rebuild the points */
void gp_values(const float** v, size_t* n);
bool gp_probe(uint32_t e, const float hit[3], float scale, int* ip, float* value);
bool gp_sibling(const char* frd_path, char* out, size_t n);

/* app_deck.c: the input deck -- sets, surfaces, materials */
void deck_set(cv_inp* d, const char* path);
void deck_clear(void);
bool deck_loaded(void);
const cv_inp* deck_get(void);
const char* deck_path(void);
bool* deck_set_flags(void);
bool* deck_surf_flags(void);
bool deck_file_reader(void* user, const char* path, char** data, size_t* size);
const char* deck_material_name(uint32_t k);
bool deck_any_elset_on(void);
void deck_apply_mask(const cv_frd* f, uint8_t* vis);
void deck_refresh_highlight(void);   /* highlights and the support / load glyphs */
bool deck_has_bc(void);
bool deck_has_loads(void);
bool deck_has_discrete(void);
bool* deck_link_flags(void);         /* per link: drawn (spiders) / highlighted (tie, contact surfaces) */
/* arrow as line pairs: head at tip, shaft back along dir (unit) by len; d = displacement carried */
void deck_arrow(cv_fvec* pos, cv_fvec* disp, const float tip[3], const float dir[3], float len, const float d[3], bool twin);
bool app_field_is_vector(void);      /* the selected .frd field has 3 components */
void app_vectors_changed(void);
bool deck_sibling(const char* path, const char* ext, char* out, size_t n);

/* app_fbd.c: cgx geometry */
void geo_set(cv_fbd* g, bool evaluated);
void geo_clear(void);
bool geo_loaded(void);
bool geo_evaluated(void);
const cv_fbd* geo_get(void);
bool* geo_set_flags(void);
bool geo_set_hidden(const char* name);
bool geo_any_on(void);
bool geo_bounds(v3* lo, v3* hi);
void geo_refresh(void);
void geo_set_toggled(int i);
void geo_show_all(void);
void geo_sets_into_deck(const cv_fbd* g, cv_inp* d);
void app_eval_cgx(void);             /* re-open the .fbd, evaluated by cgx */

/* app_settings.c: the ini file */
void settings_load(void);                    /* into G, before the first frame */
void settings_window_size(int* w, int* h);   /* before the window exists */
void settings_save(int win_w, int win_h);
bool settings_apply(const char* key_eq_value);   /* --opt; false for an unknown key */
void settings_add_recent(const char* path);
int  settings_recent(const char** out, int max);
const char* settings_last_dir(void);
void settings_free(void);

/* colour map lookup with the legend's reverse / grey applied */
void app_cmap_rgb(float t, float rgb[3]);
void app_legend_fmt(char* out, size_t n, double v);   /* a legend number in the chosen format */

/* camera helpers */
void cam_basis(const cv_camera* c, v3* eye, v3* fwd, v3* right, v3* up);
v3   cam_up_axis(void);                  /* world +Y or +Z */
bool app_cursor_point(float px, float py, v3* out, bool* on_model);

#endif
