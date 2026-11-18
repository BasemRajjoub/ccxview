/* report.h -- model summary report as Markdown: the model definition only (mesh,
   materials, sections, loads, constraints, steps), no results. Headless. */
#ifndef CV_REPORT_H
#define CV_REPORT_H

#include <stdio.h>
#include "inp.h"

typedef struct {
    const char* frd_path;       /* result file, NULL none */
    const char* inp_path;       /* deck, NULL none */
    int         units;          /* CV_SYS_*, 0 not set */
} cv_report_opts;

/* f: the .frd (may be NULL when the deck is the model), deck: may be NULL, o: may be NULL.
   Each section only when its data exists. false: nothing to report or a write error. */
bool cv_report_md(FILE* out, const cv_frd* f, const cv_inp* deck, const cv_report_opts* o);

#endif
