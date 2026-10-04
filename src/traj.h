/* traj.h -- principal stress trajectories: curves that follow the direction of
   the largest (S1) or smallest (S3) principal value through the solid. Headless:
   the tensor comes from a callback, so the integrator runs without a mesh;
   app_traj.c samples the model and draws the lines.

   Also the two grids it needs: a bin grid of element boxes for point location,
   and an occupancy grid that keeps lines apart (Jobard & Lefer 1997, simplified:
   a line stops where it enters a cell another line passed first). */
#ifndef CV_TRAJ_H
#define CV_TRAJ_H

#include "base.h"

/* ---- boxes binned on a uniform grid --------------------------------------- */
typedef struct { float lo[3], cell; int n[3]; uint32_t* start; uint32_t* items; } cv_bins;
/* boxes lo/hi (3 floats each, nbox of them) binned on a grid of about `cell` size,
   at most `maxdim` cells per axis (the cell grows to fit). false on OOM. */
bool cv_bins_build(cv_bins* b, const float* lo, const float* hi, uint32_t nbox, float cell, int maxdim);
/* the boxes whose cell holds p: *n of them at the returned pointer (NULL, 0 outside) */
const uint32_t* cv_bins_at(const cv_bins* b, const float p[3], uint32_t* n);
void cv_bins_free(cv_bins* b);

/* ---- occupancy: the 1-based id of the line that first passed a cell, 0 empty -- */
typedef struct { float lo[3], cell; int n[3]; uint32_t* id; } cv_occ;
bool      cv_occ_init(cv_occ* o, const float lo[3], const float hi[3], float cell, int maxdim);
uint32_t* cv_occ_cell(cv_occ* o, const float p[3]);   /* NULL outside */
void      cv_occ_free(cv_occ* o);

/* ---- the integrator ----------------------------------------------------------- */
/* the tensor at p: 6 values (XX YY ZZ XY YZ ZX) and 6 displacement values (DISP,
   then a second part, may be zeros); false when p is outside the solid */
typedef bool (*cv_traj_sample)(void* user, const float p[3], float s[6], float d[6]);

typedef struct {
    int   which;        /* 0: S1 (largest value), 2: S3 (smallest) -- index into cv_principal_dirs' output */
    float h;            /* step length */
    float min_val;      /* stop where |principal value| < min_val (the direction is undefined there) */
    float max_turn;     /* stop when the direction turns more than this (radians) in one step */
    int   max_steps;    /* per direction */
} cv_traj_opts;

/* One trajectory through seed, integrated both ways (RK2 midpoint, the
   eigenvector's sign kept continuous). Appends points to pts (3 floats each),
   disp (6 each) and val (1 each: the principal value) from one end to the other.
   occ (may be NULL): a cell owned by another line stops it; the cells it passes
   are claimed with line_id. Returns the number of points; below 2 nothing is
   appended (and no cell kept). */
size_t cv_traj_trace(const float seed[3], const cv_traj_opts* o, cv_traj_sample f, void* user,
                     cv_occ* occ, uint32_t line_id, cv_fvec* pts, cv_fvec* disp, cv_fvec* val);

#endif
