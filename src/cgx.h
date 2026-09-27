/* cgx.h -- evaluate a cgx script (.fbd) with cgx itself.

   A script can do anything cgx can: sweep, copy, mesh, write files, and (when the
   user's ~/.cgx allows it) run shell commands. So it never runs in the user's
   folder: the folder's files are copied to a fresh temporary directory, cgx runs
   there in batch mode (-bg) and writes the evaluated geometry and the mesh, and
   the directory is removed afterwards. */
#ifndef CV_CGX_H
#define CV_CGX_H

#include "base.h"

/* The helper set the evaluation exports through; not one of the model's own. */
#define CV_CGX_SET "ccxvw0"

/* cgx executable: $CCXVIEW_CGX, then PATH, then ~/.local/bin, then beside ccxview. */
bool cv_cgx_find(char* out, size_t n);

typedef struct {
    char*  fbd;  size_t fbd_n;      /* evaluated geometry (send ... fbd); NULL if none */
    char*  msh;  size_t msh_n;      /* mesh (send ... abq); NULL if the script did not mesh */
    char   err[256];                /* why nothing came back */
    char   log[1024];               /* the tail of cgx's output */
} cv_cgx_result;

/* false: nothing came back (see err). Blocks until cgx exits (5 minutes at most
   where the platform can enforce it). */
bool cv_cgx_eval(const char* cgx, const char* script_path, cv_cgx_result* r);
void cv_cgx_result_free(cv_cgx_result* r);

#endif
