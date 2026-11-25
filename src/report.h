/* report.h -- model summary report as Markdown or PDF: the model definition only (mesh,
   materials, sections, loads, constraints, steps), no results. Headless. */
#ifndef CV_REPORT_H
#define CV_REPORT_H

#include <stdio.h>
#include "inp.h"

/* the parts of the report (cv_report_opts.skip: the ones left out) */
enum {
    CV_RP_GENERAL = 1 << 0,     /* files, solver, units */
    CV_RP_STEPS   = 1 << 1,     /* the analysis steps table */
    CV_RP_MESH    = 1 << 2,     /* counts, bounding box, element types */
    CV_RP_SETS    = 1 << 3,     /* node and element sets */
    CV_RP_QUALITY = 1 << 4,     /* mesh quality metrics */
    CV_RP_MATS    = 1 << 5,
    CV_RP_SECTS   = 1 << 6,     /* sections and orientations */
    CV_RP_LOADS   = 1 << 7,     /* boundary conditions, loads, amplitudes */
    CV_RP_CONSTR  = 1 << 8,     /* constraints and interactions */
    CV_RP_N       = 9
};

typedef struct {
    const char* file;           /* the picture as the Markdown links it (beside the report) */
    const char* caption;
} cv_report_pic;

typedef struct {
    const char* frd_path;       /* result file, NULL none */
    const char* inp_path;       /* deck, NULL none */
    int         units;          /* CV_SYS_*, 0 not set */
    unsigned    skip;           /* CV_RP_* left out; 0 everything */
    const cv_report_pic* pics;  /* pictures, in a section after General */
    int         npics;
    const char* shown;          /* what the pictures show (field, step, scale), NULL none */
} cv_report_opts;

/* the label of a CV_RP_* bit (index 0..CV_RP_N-1), for the Report builder */
const char* cv_report_part_name(int i);

/* f: the .frd (may be NULL when the deck is the model), deck: may be NULL, o: may be NULL.
   Each section only when its data exists and is not skipped; sections numbered as
   written. false: nothing to report or a write error. */
bool cv_report_md(FILE* out, const cv_frd* f, const cv_inp* deck, const cv_report_opts* o);

/* report_pdf.c: that Markdown (md, as cv_report_md writes it) laid out as an A4 PDF
   at path; name (may be NULL) in the title and the page footers. Picture lines
   "![caption](file)" are placed from dir/file (dir NULL: file as given), RGB PNG or
   JPEG; one that cannot be read becomes its caption in grey. false: a write error. */
bool cv_report_pdf(const char* md, const char* path, const char* name, const char* dir);

#endif
