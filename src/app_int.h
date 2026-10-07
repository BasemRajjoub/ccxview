/* app_int.h -- what the app_*.c files share among themselves (not the UI). */
#ifndef CV_APP_INT_H
#define CV_APP_INT_H

#include "app.h"

/* command-line options, parsed in app.c, applied when a file has loaded */
typedef struct {
    int         faces;          /* --faces field|type|material|group|plain, -1 = keep */
    float       crop[6];        /* --crop x0,x1,y0,y1,z0,z1 (fractions) */
    bool        crop_set;
    bool        fly;            /* --fly: start in free flight */
    int         fly_clip;       /* --fly-clip / --fly-hide [DEPTH]: ... the eye cuts / hides elements (CV_EYE_*) */
    float       fly_depth;      /* ... that far ahead; 0 the saved depth */
    bool        gauss;          /* --gauss: start on the first .dat field */
    bool        cgx;            /* --cgx: evaluate a cgx script on open */
    bool        mirror[3];      /* --mirror xyz */
    bool        rep[3];         /* --opt repK=1: replicate along axis K from the start */
    bool        eval_next;      /* the next open evaluates with cgx */
    bool        gp;             /* --gp: Gauss point layer on */
    bool        vectors;        /* --vectors: arrow layer on */
    int         tensor;         /* --tensor STYLE: glyph layer on with that CV_GLYPH_* style, -1 = keep */
    int         traj;           /* --trajectories s1|s3|both: 0 s1, 1 s3, 2 both, -1 = keep */
    bool        conv;           /* --conv: open the convergence window */
    int         win_w, win_h;   /* --size WxH */
    const char* field;          /* --field NAME: start on that .frd field (first option) */
    const char* calc;           /* --calc FORMULA: start on that calculated field */
    const char* fail;           /* --fail CRIT[:OUT]: start on that failure field */
    const char* mesh;           /* --mesh MEASURE: start on that mesh quality field */
    float       target[3];      /* --target x,y,z: orbit centre */
    bool        target_set;
    float       zoom;           /* --zoom F: closer by F */
    int         export_kind;    /* --export [png|seq|csv|vtk]: 0 none, 1 png, 2 sequence, 3 csv, 4 vtk */
    const char* opts[32];       /* --opt key=value, applied after the settings file */
    int         nopts;
    const char* find;           /* --find ID or eID: probe it after load */
    float       box[4];         /* --box x0,y0,x1,y1: max in that box of the view (fractions), after load */
    bool        box_set;
    bool        details;        /* --details: the Details window of the probe (with --find or --box) */
    bool        about;          /* --about: the About window open */
    float       menu[2];        /* --menu x,y: the context menu at that point of the view (fractions) */
    bool        menu_set;
    bool        mesh_window;    /* --mesh-window: the Mesh quality window open, its limits shown */
    const char* view_file;      /* --view FILE: a saved view state, applied after load */
    bool        watch;          /* --watch: reload when the file changes */
    const char* compare;        /* --compare FILE */
    const char* path_ids;       /* --path A,B: node ids to plot between */
    long        hist_id;        /* --history N: node id whose history to plot, 0 none */
    const char* lin_ids;        /* --linearize A,B: node ids of the line */
    int         look;           /* --look iso|+x|-x|+y|-y|+z|-z, -1 = default */
    float       bg[3];          /* --bg white|black|r,g,b */
    bool        bg_set;
    const char* sets[8];        /* --set NAME: tick a deck set / surface */
    int         nsets;
    int         step;           /* --step N (1-based, -1 = last) */
    bool        gp_under;       /* --gp-under: Gauss points depth-tested against faces */
    bool        xray;           /* --xray: Gauss points through the faces */
    bool        no_faces;       /* --no-faces */
    float       gp_size;        /* --gp-size N */
    bool        no_edges;       /* --no-edges */
    float       outline;        /* --outline off|on|DEG: 0 off, > 0 the crease angle, < 0 unset */
    const char* argv_path;      /* the file to open */
    const char* shot_path;      /* --shot out.png: render, save the window, quit */
    int         shot_frames;    /* --frames N */
    int         frame_no;
    bool        browse;         /* --browse: start with the built-in browser open */
    const char* ui_test;        /* --ui-test DIR: run the interface script (ui_test.c), failure pictures into DIR */
} cv_opts;
extern cv_opts O;

/* app_field.c: decoded fields, colours, displacement, the current step */
void         cache_clear(void);
const float* cache_get(int step, int field);
void  units_apply(const char* field, int ncomp, float* v, size_t n);   /* model units -> shown */
float units_len_raw(void);           /* shown lengths -> model lengths (the shape) */
int          find_field(int step, const char* name);
void         init_group_colors(void);
void         refresh_tri_colors(void);
void         refresh_tri_values(void);
void         refresh_field(void);
void         refresh_gauss(void);
void         refresh_markers(void);
#define to_csys app_to_csys
float*       to_csys(const cv_field_desc* d, const cv_frd* f, const float* vals);   /* a copy in the chosen cylindrical system, or NULL */

/* app_overlay.c: vector arrows, clip caps */
void         refresh_vectors(void);
/* app_tensor.c: tensor glyphs */
void         refresh_tensors(void);
/* app_traj.c: principal stress trajectories (CV_INST_TRAJ1 / TRAJ3) */
void         refresh_traj(void);
void         app_traj_tick(void);       /* each frame: a trace put off during playback, once it stops */

/* app_path.c: path plot, history */
void         refresh_path(void);
void         refresh_hist(void);

/* app_linearize.c: stress linearization, locating points in the solid */
#define line_locate app_line_locate
#define line_interp app_line_interp
void         refresh_lin(void);
void         line_locate(const float A[3], const float B[3], int n, uint32_t* el, float* w);   /* elements + shape weights of n points on A..B */
bool         line_interp(uint32_t e, const float* w, const float* v, int nc, float* out);
#define elem_locate app_elem_locate
/* natural coordinates of p in solid element e (types 1..6): true when inside, N its 20
   shape-function weights there (node order of the element as line_interp expects) */
bool         elem_locate(uint32_t e, const double p[3], double N[20]);

/* app_cam.c: camera, symmetry, picking */
void view_bounds(void);
int  sym_auto(int axis);
void sym_matrix(int copy, float* M);     /* model matrix of mirror copy `copy` (bit k = axis k) */
void copy_ray(int i, const float o[3], const float d[3], float mo[3], float md[3]);   /* world ray -> instance i's model frame */
void cam_matrices(float* mvp, float* mv, float* proj_out);
void do_pick(float px, float py);

/* app_load.c: background jobs, opening files, dialogs */
void unload(void);
void poll_job(void);
void poll_dialog(void);

#endif
