/* calclib.h -- the formula library: named calculated-field formulas kept in a
   plain text file beside the ini, one "name = expression" per line, # comments.
   Headless. Text only: nothing is evaluated here; an expression is compiled by
   calc.h when it is applied. */
#ifndef CV_CALCLIB_H
#define CV_CALCLIB_H

#include "base.h"

enum { CV_FORMULA_NAME = 48, CV_FORMULA_EXPR = 256 };   /* sizes incl. the NUL; expr = G.calc_expr's */

typedef struct { char name[CV_FORMULA_NAME]; char expr[CV_FORMULA_EXPR]; } cv_formula;
typedef struct cv_calclib { cv_formula* a; int n, cap; } cv_calclib;

/* <ini dir>/ccxview_formulas.txt; false if unknown */
bool cv_calclib_default_path(char* out, size_t n);
/* Merge the lines of text: a name already in the library (any case) is skipped,
   so are blank and # lines. Bad lines (no "=", empty or over-long name or
   expression, control characters) count in *bad, the first one's number in
   *first_bad (0 none). Returns the lines added; *dup counts the skipped duplicates. */
int  cv_calclib_merge(cv_calclib* L, const char* text, int* dup, int* bad, int* first_bad);
int  cv_calclib_find(const cv_calclib* L, const char* name);       /* index, -1 none */
/* add, or replace the expression of the same name; false: invalid name / expression */
bool cv_calclib_set(cv_calclib* L, const char* name, const char* expr);
void cv_calclib_remove(cv_calclib* L, int i);
char* cv_calclib_text(const cv_calclib* L);                         /* the file's text, malloc'd */
bool cv_calclib_load(cv_calclib* L, const char* path, int* bad);   /* missing file: empty, true */
bool cv_calclib_save(const cv_calclib* L, const char* path);
void cv_calclib_free(cv_calclib* L);

#endif
