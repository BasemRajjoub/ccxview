/* datip.h -- one element's rows of a .dat integration-point block, for the
   Inspector's table: a row per point, sorted by point, every component and, for a
   6-component stress, von Mises. Headless. */
#ifndef CV_DATIP_H
#define CV_DATIP_H

#include "dat.h"

enum { CV_DATIP_MAX = 64 };

typedef struct {
    int      n, ncomp;                       /* rows, components */
    bool     mises;                          /* a stress tensor: vm[] holds von Mises */
    uint16_t ip[CV_DATIP_MAX];               /* 1-based point per row, ascending */
    float    v[CV_DATIP_MAX][CV_DAT_MAX_COMP];
    float    vm[CV_DATIP_MAX];
} cv_datip;

/* the rows of element id elem in b (vm filled); its rows, 0 none */
int  cv_datip_rows(const cv_dat_block* b, uint32_t elem, cv_datip* t);
void cv_datip_mises(cv_datip* t);            /* vm[] again, after v[] changed (units) */
/* the row nearest by distance of points p (3 per row, NaN: unknown) to x; -1 none */
int  cv_datip_nearest(const float* p, int n, const float x[3]);

#endif
