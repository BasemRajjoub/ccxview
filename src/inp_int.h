/* inp_int.h -- what the inp*.c files share; not for other modules (inp.h is the
   interface). Include it after the headers inp.c includes. The short names are
   macros for inp_ symbols, so the code reads as it did in one file and the
   linker sees names no other module uses. */
#ifndef CV_INP_INT_H
#define CV_INP_INT_H

#define upcase      inp_upcase
#define cmp_idix    inp_cmp_idix
#define find_idix   inp_find_idix

/* ---- inp.c: small text helpers */
void upcase(char* s);                                           /* in place, ASCII */
int cmp_idix(const void* a, const void* b);                     /* qsort: by id, then index */
int32_t find_idix(const cv_idix* v, uint32_t n, uint32_t id);   /* ix of id in a sorted list, -2 none */

#endif
