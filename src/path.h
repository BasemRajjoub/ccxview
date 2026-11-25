/* path.h -- shortest node path along the skin's edges, for plotting a field
   along a line on the surface. Headless. */
#ifndef CV_PATH_H
#define CV_PATH_H

#include "mesh.h"

/* Dijkstra over skin->edge (undirected, weight = undeformed length) from node
   a to node b (dense indices). Writes the node sequence a..b into *out
   (malloc'd, caller frees) and its length into *n. false when no path exists
   or on OOM. Cumulative arc length per node goes into *dist if not NULL
   (malloc'd, same count). */
bool cv_path_find(const cv_frd* f, const cv_skin* skin, uint32_t a, uint32_t b,
                  uint32_t** out, uint32_t* n, float** dist);

#endif
