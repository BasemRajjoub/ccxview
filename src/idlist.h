/* idlist.h -- lists of node or element ids as text, the way people write them:
   "1-100, 205, 300-310". Formatting compresses runs into ranges; parsing takes
   spaces, commas, semicolons and newlines as separators, ranges either way
   round ("10-1"), and reports what it could not read instead of failing.
   Headless. */
#ifndef CV_IDLIST_H
#define CV_IDLIST_H

#include "base.h"

/* The ids as compressed ranges, ", " between them: a malloc'd string ("" for
   none), NULL when out of memory. Sorted input gives the shortest text;
   unsorted input is written run by run as it comes. */
char* cv_idlist_format(const uint32_t* ids, size_t n);

typedef struct {
    uint32_t* ids;           /* sorted, each once; NULL when none */
    size_t    n;
    int       bad;           /* tokens that were not an id or a range */
    char      bad_text[96];  /* the first of them, ", " between, cut short */
} cv_idlist;

enum { CV_IDLIST_MAX = 100000000 };   /* ids one text may expand to: "1-4000000000" is a typo */

/* Parse text into out (free it with cv_idlist_free). false only when out of
   memory or the ranges would expand past CV_IDLIST_MAX ids (out then holds
   nothing); bad tokens are skipped and counted. */
bool cv_idlist_parse(const char* text, cv_idlist* out);
void cv_idlist_free(cv_idlist* l);

#endif
