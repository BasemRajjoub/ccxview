/* fsearch.h -- fuzzy matching for the feature search box. Headless, libc only. */
#ifndef CV_FSEARCH_H
#define CV_FSEARCH_H

/* 0 = no match, higher = better. Every query word must match (AND), case-insensitive.
   Empty query -> 1. keywords / help may be NULL. */
int cv_fs_score(const char* query, const char* label, const char* keywords, const char* help);

/* Scores n entries (kw / help may be NULL or hold NULLs); writes the best `max` indices into
   order, best first, ties by original order. Returns the count. */
int cv_fs_rank(const char* query, int n, const char* const* label, const char* const* kw,
               const char* const* help, int* order, int max);

#endif
