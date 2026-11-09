/* nk.h -- Nuklear with the one configuration every translation unit must share. */
#ifndef CV_NK_H
#define CV_NK_H

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_BOOL
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nkfmt.h"
#define NK_DTOA cv_dtoa   /* Nuklear's nk_dtoa hangs on values >= 2^31 and on inf: see nkfmt.h */
#include "nuklear.h"

#endif
