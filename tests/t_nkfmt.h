/* t_nkfmt.h -- the number text of Nuklear's property boxes (nkfmt.h): it returns for
   every double, the large ones (2e11 Pa, inf) that made Nuklear's own nk_dtoa loop forever
   included (issue 43). Included by test_main.c. */
#include "../src/nkfmt.h"
#include <string.h>

static void test_nkfmt(void) {
    char s[64];
    static const double v[] = { 2e11, -3e9, 2147483648.0, 1e13, 1e15, 1e30, 3.4e38, 1e100, 1e300, -1e30,
                                1e-30, 5e-324, 0.0, -0.0, 0.0854 };
    for (size_t i = 0; i < sizeof v / sizeof *v; i++) {
        s[0] = 0;
        CHECK(cv_dtoa(s, v[i]) == s);
        CHECK(s[0] != 0 && strlen(s) < 63);
        CHECK_NEAR(strtod(s, 0), v[i], fabs(v[i]) * (fabs(v[i]) < 1e15 ? 1e-5 : 0.5) + 1e-14);   /* beyond 1e15 one digit */
    }
    CHECK(!strcmp(cv_dtoa(s, 2e11), "200000000000"));
    CHECK(!strcmp(cv_dtoa(s, 1e30), "1e+30"));
    CHECK(!strncmp(cv_dtoa(s, 0.0854), "0.0854", 6));
    CHECK(!strcmp(cv_dtoa(s, INFINITY), "inf"));
    CHECK(!strcmp(cv_dtoa(s, -INFINITY), "-inf"));
    CHECK(!strcmp(cv_dtoa(s, NAN), "nan"));
}
