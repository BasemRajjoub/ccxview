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
    bool        gauss;          /* --gauss: start on the first .dat field */
    bool        cgx;            /* --cgx: evaluate a cgx script on open */
    bool        mirror[3];      /* --mirror xyz */
    bool        rep[3];         /* --opt repK=1: replicate along axis K from the start */
    bool        eval_next;      /* the next open evaluates with cgx */
    bool        gp;             /* --gp: Gauss point layer on */
    bool        vectors;        /* --vectors: arrow layer on */
    bool        conv;           /* --conv: open the convergence window */
    int         win_w, win_h;   /* --size WxH */
    const char* field;          /* --field NAME: start on that .frd field (first option) */
    float       target[3];      /* --target x,y,z: orbit centre */
    bool        target_set;
    float       zoom;           /* --zoom F: closer by F */
    int         export_kind;    /* --export [png|seq|csv|vtk]: 0 none, 1 png, 2 sequence, 3 csv, 4 vtk */
    const char* opts[32];       /* --opt key=value, applied after the settings file */
    int         nopts;
    const char* find;           /* --find ID or eID: probe it after load */
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
} cv_opts;
extern cv_opts O;

/* app_field.c: decoded fields, colours, displacement, the current step */
void         cache_clear(void);
const float* cache_get(int step, int field);
int          find_field(int step, const char* name);
void         init_group_colors(void);
void         refresh_tri_colors(void);
void         refresh_tri_values(void);
void         refresh_field(void);
void         refresh_gauss(void);
void         refresh_vectors(void);
void         refresh_markers(void);
void         refresh_path(void);

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
