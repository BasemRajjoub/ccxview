/* sta.h -- CalculiX .sta / .cvg readers: one line per increment attempt (.sta)
   and per Newton iteration (.cvg), for the convergence plot. Headless; never
   fails on bad input (unreadable lines are skipped). */
#ifndef CV_STA_H
#define CV_STA_H

#include "base.h"

typedef struct {
    int   step, inc, att, iters;
    bool  cutback;         /* attempt flagged U: unconverged, increment retried smaller */
    float total_time, step_time, inc_time;
} cv_sta_inc;

typedef struct {
    int   step, inc, att, iter;
    int   contact_elems;
    float resid_force, corr_disp, resid_flux, corr_temp;   /* percent, as printed */
} cv_cvg_iter;

typedef struct {
    cv_sta_inc*  inc;   uint32_t ninc;
    cv_cvg_iter* it;    uint32_t nit;
} cv_sta;

bool cv_sta_parse(cv_sta* s, const char* data, size_t size);     /* the .sta file */
bool cv_cvg_parse(cv_sta* s, const char* data, size_t size);     /* the .cvg file, into the same struct */
void cv_sta_free(cv_sta* s);

#endif
