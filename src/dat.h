/* dat.h -- CalculiX .dat reader: the integration-point blocks of *EL PRINT.

   A block looks like
       stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set E and time  1.0
               1   1  1.0E+00 ...
   Only "(elem, integ.pnt., ...)" blocks are kept; nodal prints, energies and
   totals are skipped. A block named "...coordinates..." holds the integration
   point positions (*EL PRINT ... COORD). Like the .frd reader it never fails on
   bad input: unreadable lines are skipped and reported. */
#ifndef CV_DAT_H
#define CV_DAT_H

#include "base.h"

enum { CV_DAT_MAX_COMP = 16 };

typedef struct {
    char      name[48];                   /* "stresses", "equivalent plastic strain" */
    char      set[48];
    int       ncomp;
    char      comp[CV_DAT_MAX_COMP][12];  /* "sxx", "pe", ... */
    float     time;
    int       step;                       /* from the last "S T E P n" line, 0 if none */
    bool      is_coord;                   /* integration point coordinates */
    uint32_t  n;                          /* records */
    uint32_t* elem;                       /* element id per record */
    uint16_t* ip;                         /* 1-based integration point per record */
    float*    vals;                       /* n * ncomp */
} cv_dat_block;

typedef struct {
    int           n;
    cv_dat_block* b;
    cv_msgs       msgs;
} cv_dat;

bool cv_dat_parse(cv_dat* d, const char* data, size_t size);   /* false only on OOM */
void cv_dat_free(cv_dat* d);

#endif
