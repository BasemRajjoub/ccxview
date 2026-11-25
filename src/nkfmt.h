/* nkfmt.h -- number text for Nuklear's property boxes. Nuklear's own nk_dtoa takes a magnitude
   through (int)n, which is undefined past 2^31 and then loops forever on any value of 2e9 or more
   (a stress in Pa) or on inf; this one is bounded for every double. Headless: t_nkfmt.h tests it. */
#ifndef CV_NKFMT_H
#define CV_NKFMT_H
#include <math.h>
#include <stdio.h>

/* n into s (Nuklear gives 64 bytes): fixed with decimals up to 1e9 (Nuklear then cuts it to
   NK_MAX_FLOAT_PRECISION), whole numbers up to 1e15, "2e+11" style beyond; nan and inf as text */
static inline char* cv_dtoa(char* s, double n) {
    double a = fabs(n);
    if (n != n) snprintf(s, 64, "nan");
    else if (isinf(n)) snprintf(s, 64, n < 0 ? "-inf" : "inf");
    else if (a < 1e9) snprintf(s, 64, "%.14f", n);
    else if (a < 1e15) snprintf(s, 64, "%.0f", n);
    else snprintf(s, 64, "%.0e", n);
    return s;
}
#endif
