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
#include "anchor.h"
#include "units.h"

/* what the faces are coloured by */
enum { FM_FIELD, FM_TYPE, FM_MAT, FM_GRP, FM_PLAIN, FM_N };   /* FM_TYPE + axis = FM for that axis */

/* symmetry plane position along its axis */
enum { CV_SYM_ZERO, CV_SYM_MIN, CV_SYM_MAX, CV_SYM_N };

/* the side panel's sections and sub-sections: open or closed, kept in the settings */
enum { CV_TREE_LAYERS, CV_TREE_GROUPS, CV_TREE_FIELDS, CV_TREE_VIEW, CV_TREE_EXPORT,
       CV_TREE_CAMERA, CV_TREE_COLOURS, CV_TREE_DISPLAY, CV_TREE_MIRROR, CV_TREE_REPLICATE,
       CV_TREE_CLIP, CV_TREE_FILE, CV_TREE_SYMBOLS, CV_TREE_CYCLIC, CV_TREE_N };

enum { CV_VIEW_ISO, CV_VIEW_PX, CV_VIEW_NX, CV_VIEW_PY, CV_VIEW_NY, CV_VIEW_PZ, CV_VIEW_NZ };

typedef struct {
    v3    target;
    float dist, yaw, pitch, fovy;    /* turntable: yaw / pitch about the up axis */
    v3    fdir, fup;                 /* free orbit: unit target -> eye, and the screen's up */
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
    float     crease;         /* feature edges: degrees between faces that make a crease */
    bool      mid;            /* quadratic faces through their mid-side nodes */
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
    float     sym_len;        /* symbol size in model units: sym_auto_len, or sym_size when set by hand */
    float     sym_auto_len;   /* 2.5 % of the diagonal of the meshed part, undeformed */
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
    unsigned  field_gen;      /* bumped when the values or displacements change */
    bool      elem_mode;
    int       field_src;      /* 0: .frd (nodal), 1: .dat (integration points), 2: calculated */
    char      calc_expr[256]; /* the calculated field's formula (calc.h), kept across files */
    char      calc_err[128];  /* why the last formula did not compile, "" when it did */
    struct cv_calc* calc;     /* calc_expr compiled for this file, NULL none */
    uint8_t*  vis;            /* last visibility mask (groups + crop), NULL = all */

    /* integration-point display */
    bool      show_gp;
    bool      gp_colored;
    float     gp_size;
    bool      gp_on_top;      /* Gauss points drawn through the faces */
    int       probe_ip;       /* point hit by the probe (1-based), 0 none */
    char      field_label[96];   /* with the unit in [] when a unit set is chosen */
    char      legend_lines[3][96];   /* the legend's title: the field, its component, its unit; any may be empty */

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
    int       tree[CV_TREE_N];       /* CV_TREE_*: panel section expanded (nk_collapse_states) */
    int       seq_warm;              /* frames to let pass before recording: the arming frame is drawn with the old state */
    void*     video;                 /* an MP4 being written (cv_video*), frames of the sequence go there */
    /* clip plane: everything beyond it is cut away at draw time */
    bool      clip_on;
    bool      clip_cap;              /* fill the cut through solid elements */
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
    /* path plot: the field between two picked nodes, on the straight line through the
       solid (sampled, interpolated in the elements) or on the shortest path over the
       surface edges; or from one node along a direction to where the line leaves
       the solid */
    uint32_t  path_a;                /* first node, UINT32_MAX none */
    bool      path_arm;              /* waiting for the second click */
    bool      path_surface;          /* along the surface instead of straight */
    uint32_t  path_end[2];           /* the end nodes; [1] UINT32_MAX when a ray ends on a face */
    float     path_p[2][3];          /* the straight line's ends (undeformed) */
    int       path_dir;              /* 0 to a picked node, 1 along the inward normal, 2..4 along X, Y, Z */
    uint32_t  path_to;               /* the last picked end node, to go back to mode 0 */
    uint32_t  path_tri;              /* the skin triangle clicked at the first node: its normal */
    unsigned  path_gen;              /* bumped on every rebuild: the markers follow */
    uint32_t* path_nodes;  uint32_t path_n;   /* surface: the nodes; straight: path_n samples */
    float*    path_dist;
    uint32_t* path_el;  float* path_w;        /* straight: element (UINT32_MAX outside) and 20 weights per sample */
    bool      path_open;             /* the plot window */
    bool      path_lin;              /* the window shows the linearization of the straight line (default) */
    /* history: the current field at one node (or element) over every step */
    bool      hist_open;
    uint32_t  hist_node, hist_elem;  /* the element's node mean in per-element mode */
    int       hist_n;
    float*    hist_t;  float* hist_v;  int* hist_step;
    bool      hist_by_step;          /* x axis: step number instead of time */
    char      hist_key[480];         /* what the curve was built for */
    /* stress linearization along the straight line between the path's two nodes (ASME) */
    bool      lin_open;
    float     lin_p[2][3];           /* the ends */
    uint32_t  lin_a, lin_b;          /* their nodes, for the labels; UINT32_MAX a point on a face */
    int       lin_fi;                /* the tensor field linearized, -1 none */
    int       lin_n;                 /* sample points */
    float*    lin_s;                 /* 6 per point, NaN where the line is outside the solid */
    float     lin_t;                 /* line length */
    int       lin_q;                 /* shown: 0 von Mises, 1 Tresca, 2..7 a component, 8 S1+S2+S3 */
    bool      lin_asme;              /* bending from the components normal to the line only (5-A.4.1.2) */
    char      lin_key[200];
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
    bool      show_outline;          /* feature edges: borders, creases, material and type changes */
    float     outline_angle;         /* crease angle, degrees; the skin is rebuilt when it changes */
    bool      mid_faces;             /* quadratic faces drawn through their mid-side nodes (the skin is rebuilt when it changes) */
    float*    axis_rgb[CV_AXIS_N];   /* 3 floats per group value */
    float     point_size;
    bool      shading;               /* off: flat true colours; on: light + shadow */
    int       cmap;
    int       bands;

    /* view */
    cv_camera cam;
    bool      sym[3];                /* mirror copies across a plane normal to X, Y, Z */
    int       sym_at[3];             /* CV_SYM_*: where that plane sits */
    bool      rep[3];                /* replicate: a row of copies along X, Y, Z (periodic models) */
    int       rep_n[3];              /* copies along that axis, the model included */
    float     rep_gap[3];            /* space between copies; 0: they touch (pitch = length of the model) */
    bool      rep_follow;            /* copies follow the deformation: shifted by the deformed cell edges */
    bool      cyc_on;                /* cyclic symmetry: the sector turned about an axis */
    int       cyc_n;                 /* sectors in 360 degrees */
    int       cyc_show;              /* sectors drawn, the model included (1..cyc_n) */
    int       cyc_axis;              /* 0 X, 1 Y, 2 Z */
    float     cyc_o[3];              /* a point on the axis */
    bool      up_z;                  /* turntable about world Z instead of Y */
    bool      orbit_free;            /* free orbit: dragging turns about the screen's axes, no fixed up */
    bool      orbit_cursor;          /* drag rotates about the model point under the cursor */
    bool      zoom_cursor;           /* the wheel zooms toward the cursor */
    bool      wheel_invert;          /* wheel up zooms out */
    bool      show_pivot;            /* mark the point the view turns / zooms about while navigating */
    int       nav_mode;              /* CV_NAV_*: what the mouse is doing to the view */
    bool      nav_live;              /* a drag is on: the mark shows only then */
    v3        nav_pt;                /* the pivot / zoom point */
    float     nav_box[4];            /* box zoom: x0, y0, x1, y1 in window pixels */
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
    bool      show_calc_help;        /* the formula builder window */
    bool      show_units;            /* the units window */
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
    float     bc_scale, load_scale;  /* supports / springs and load arrows, times sym_len */
    bool      sym_auto;              /* symbol size follows the model's size (else sym_size) */
    float     sym_size;              /* symbol size by hand, model units */
    float     sym_thick;             /* thickness of the symbols' lines, times the default (4.5 % of the symbol size) */

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
    int       units;                 /* CV_SYS_*: the unit system the model was built in (0: not set) */
    int       unit_in[CV_Q_N];       /* each quantity in the file: unit index (units.h), -1 the system's */
    int       unit_show[CV_Q_N];     /* each quantity shown in: unit index, -1 as input */
    int       csys;                  /* results in: 0 global, 1..3 cylindrical about X, Y, Z */
    float     csys_o[3];             /* ... through this point */
    bool      legend_edit;           /* the legend settings window is open */
    cv_anchor legend_pos, gizmo_pos;  /* dragged to: view corner + gap (unset: top-right, bottom-left) */
} cv_app;

extern cv_app G;

const char* app_unit(const char* field, int comp);   /* the unit shown, "" when unknown or no unit set */
void app_units_changed(void);        /* file or shown units changed: values decoded again */
/* cv_field_options with component names in the chosen coordinate system */
int  app_field_options(const cv_field_desc* d, cv_scalar_opt* out, int max);

/* actions the UI calls */
void app_open(const char* path);
void app_open_dialog(void);          /* native dialog, else the built-in browser */
void app_start_dir(char* out, size_t n);
void app_set_step(int step);
void app_hist_open(uint32_t node, uint32_t elem);   /* plot the field at this node over all steps */
void app_lin_open(const float A[3], const float B[3], uint32_t na, uint32_t nb);  /* linearize the stress on the line A..B */
/* membrane and bending of the sampled line, the bending masked as lin_asme asks; false if no line */
bool app_lin_mb(double m[6], double b[6]);
void app_lin_close(void);
void app_pick_cancel(void);                 /* drop a pending second click */
void app_marks_sync(void);                  /* the picked-node markers, redrawn when the picks change */
/* the cap of the clip plane n . x = d through the solid elements, deformed by f1 DISP
   (in the shader) + f2 DISPI; rebuilt only when something changed */
void app_clip_caps(bool on, const float n[3], float d, float f1, float f2);
bool app_lin_csv(const char* path);
void app_hist_close(void);
bool app_hist_csv(const char* path);
void app_select(const char* field, int comp);
void app_select_src(const char* field, int comp, int src);
/* Show the formula expr as the field (field_src 2). false: it does not compile,
   calc_err says why, and the field shown stays. */
bool app_calc_set(const char* expr);
void app_set_elem_mode(bool on);
void app_groups_changed(void);
void app_fit(void);
void app_fit_element(uint32_t e);    /* centre the orbit on element e, close up */
int  app_check(const char* path);    /* --check: headless parse, messages to stdout; exit code */
void app_sym_toggled(int axis);      /* picks the plane, reframes */
void app_sym_changed(void);          /* plane moved: reframe */
float app_sym_plane(int axis);
float app_rep_pitch(int axis);       /* distance between replicated copies along the axis, undeformed */
void app_rep_refresh(void);          /* after the displacements change: the cell's face jumps */
int  app_copies(void);               /* instances drawn: the model, mirror and replicate copies */
void app_copy_matrix(int i, float* M);   /* model matrix of instance i (0: identity) */
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
void app_path_ray(uint32_t node, int dir);  /* from node along dir (1 inward normal, 2..4 X, Y, Z) to the far face */
void app_path_clear(void);
bool app_path_csv(const char* path);     /* dist, (id,) x, y, z, value per point */
float app_path_value(uint32_t i);        /* the shown nodal field at path point i, NaN where none */
void app_path_rebuild(void);             /* after path_surface changed */
bool app_compare_open(const char* path); /* second .frd on the same mesh; false with a message when not */
void app_compare_close(void);
bool app_view_save(const char* path);    /* camera, step, field, layers -> a small INI */
bool app_view_load(const char* path);
void app_view(int preset);
enum { CV_NAV_NONE, CV_NAV_ROTATE, CV_NAV_PAN, CV_NAV_ZOOM, CV_NAV_ROLL, CV_NAV_BOX, CV_NAV_LOOK };
void cam_turn(v3 axis, float a, v3 pivot);       /* the camera turned about a world axis through pivot */
bool cam_roll(float a);                          /* about the view axis; false: turntable, switched to free */
bool app_center_at(float px, float py);         /* the point under the pixel to the view centre (and pivot) */
bool app_normal_to(float px, float py);         /* look straight at the face under the pixel */
void app_box_zoom(float x0, float y0, float x1, float y1);
float app_pixel_size(v3 p);                      /* world length of one pixel at p's depth */
void app_view_push(void);                        /* remember the view for undo */
bool app_view_undo(int dir);                     /* -1: back, +1: forward */
void app_colormap(int cm);
void app_set_faces_mode(int fm);
void app_set_flight(bool on);
void app_group_colors_changed(void);
void app_refresh_range(void);
bool app_busy(void);
size_t app_total_msgs(void);

/* app_gauss.c: integration-point fields from a .dat */
void gp_set(cv_dat* d, const char* path);
void gp_localize(void);                /* records in local systems to global, with the deck */
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
/* values CalculiX wrote in local systems turned to global in place (inp.h) */
void deck_localize(int step, const cv_field_desc* d, float* vals);   /* step: index into G.frd */
void deck_localize_dat(cv_dat* dat, const cv_frd* f);
bool deck_read(const char* path, cv_inp* d);                         /* includes from its folder */
void deck_compare_open(const char* frd_path);                         /* the comparison run's own deck */
void deck_compare_localize(const cv_frd* f, int step, const cv_field_desc* d, float* vals);
void deck_compare_close(void);
bool* deck_link_flags(void);         /* per link: drawn (spiders) / highlighted (tie, contact surfaces) */
/* arrow as line pairs: head at tip, shaft back along dir (unit) by len; d = displacement carried */
/* d: 6 wide, DISP then -DISPI (app_node_disp6); disp gets 6 per vertex, uploaded with app_aux_upload */
void deck_seg(cv_fvec* pos, cv_fvec* disp, const float* a, const float* b, const float* d);   /* one line, both ends moving by d */
/* where a deck node is drawn and how it moves; false when it is not shown */
bool deck_node_pd(uint32_t id, float p[3], float d[6]);
uint32_t deck_elem(const cv_frd* f, uint32_t id);    /* the shown element of a deck element id, UINT32_MAX if none */
void app_symbol_size(void);          /* G.sym_len from the model or the hand-set size; redraws the symbols */
/* a symbol body (render.h, cv_render_inst) appended to v: from a to b, a radius at either end */
void deck_inst(cv_fvec* v, const float a[3], const float b[3], float ra, float rb, float scal, const float d[6]);
/* the lines of pos / disp (deck_seg pairs) as tubes of radius r, uploaded to instance layer `which` */
void deck_lines_inst(int which, const cv_fvec* pos, const cv_fvec* disp, float r);
float deck_stroke(void);             /* the radius of a symbol's stroke: G.sym_len and G.sym_thick */
void loads_refresh(void);            /* app_loads.c: the support and load glyphs of the step on screen */
void app_node_disp6(uint32_t node, float d[6]);
void app_aux_upload(int which, const cv_fvec* pos, const cv_fvec* disp6, const float* scal);
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
void cam_orbit(float yaw, float pitch);  /* turn by a drag: turntable or free, per G.orbit_free */
void app_set_orbit_free(bool on);        /* switch, keeping the current view */
bool app_cursor_point(float px, float py, v3* out, bool* on_model);

#endif
