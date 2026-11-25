/* cpal.h -- the user's own colour palettes for the legend (#53): a list of colours,
   the first for the lowest values, in bands (one band per colour, as other
   solvers' legends) or blended. Kept in ccxview.ini (paletteN), one per line:

       paletteN = name|bands|#d3d3d3 #005cff ... #ff0000      (or |blend|)

   and imported / exported as JSON or XML, the shape other programs write:

       { "UserDefinedPalettes": { "name": [ "211,211,211,255,LightGray", ... ] } }
       <UserDefinedPalettes> <name value="211,211,211,255,LightGray"/> ... </UserDefinedPalettes>

   A palette is colour map CV_CMAP_N + its index (render.h). No GUI here. */
#ifndef CV_CPAL_H
#define CV_CPAL_H

#include "base.h"

enum { CV_PAL_MAX = 32, CV_PAL_COLS = 64, CV_PAL_NAME = 48 };
typedef struct {
    char    name[CV_PAL_NAME];
    int     n;                      /* colours, 2 .. CV_PAL_COLS */
    bool    blend;                  /* false: a band per colour; true: linear between them */
    uint8_t rgb[CV_PAL_COLS][3];
} cv_pal;

int     cv_pal_count(void);
cv_pal* cv_pal_at(int i);                       /* NULL out of range */
int     cv_pal_find(const char* name);          /* -1: none */
/* added at the end; one of the same name is replaced in place. -1: full or n < 2 */
int     cv_pal_add(const cv_pal* p);
void    cv_pal_remove(int i);
void    cv_pal_move(int i, int to);
void    cv_pal_clear(void);
bool    cv_pal_rename(int i, const char* name); /* false: empty, taken, or a '|' in it */
/* a name not yet taken: base, else "base 2", "base 3" ... */
void    cv_pal_unique(const char* base, char* out, size_t n);
unsigned cv_pal_gen(void);                      /* bumped by every change above */
void    cv_pal_touch(void);                     /* a palette's colours were edited in place */

void    cv_pal_sample(const cv_pal* p, float t, float rgb[3]);

/* the ini line after "name|": false when it holds fewer than two colours */
bool    cv_pal_parse(const char* line, cv_pal* p);
void    cv_pal_format(const cv_pal* p, char* out, size_t n);
/* one colour: "r,g,b[,a[,label]]" (0..255) or "#rrggbb" */
bool    cv_pal_color(const char* s, uint8_t rgb[3]);

/* JSON or XML text: the palettes in it added (same name: replaced). Their number;
   *bad: colours that could not be read. */
int     cv_pal_import(const char* text, int* bad);
/* every palette (only = -1) or one, as JSON; malloc'd */
char*   cv_pal_export_json(int only);

#endif
