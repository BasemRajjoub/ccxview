/* selstats.h -- the selection's totals per kind: nodes (count, min, max, average of
   the field), elements (count, volume, min, max, volume-weighted average), faces
   (count, area, min, max, area-weighted average of the face averages), edges
   (count, length, min, max, length-weighted average). On the undeformed mesh; a
   value NaN is left out of min, max and average. Headless. */
#ifndef CV_SELSTATS_H
#define CV_SELSTATS_H
#include "frd.h"

typedef struct {
    uint32_t n;          /* how many */
    double   size;       /* volume, area, length (0 for nodes); NaN-free, a non-solid's volume 0 */
    uint32_t nval;       /* with a value */
    float    vmin, vmax; /* NaN when none */
    double   avg;        /* weighted by size (plain when every size is 0); NaN when none */
} cv_selstat;

/* nval: per node, NULL none; eval: per element, NULL: an element's (face's) value
   is the mean of its (the face's) nodes */
void cv_selstat_nodes(const float* nval, const uint32_t* nd, uint32_t n, cv_selstat* o);
void cv_selstat_elems(const cv_frd* f, const float* nval, const float* eval, const uint32_t* el, uint32_t n, cv_selstat* o);
/* faces as element << 3 | face (0-based); edges as node pairs, 2 an edge, straight */
void cv_selstat_faces(const cv_frd* f, const float* nval, const float* eval, const uint32_t* fc, uint32_t n, cv_selstat* o);
void cv_selstat_edges(const cv_frd* f, const float* nval, const uint32_t* ed, uint32_t n, cv_selstat* o);
#endif
