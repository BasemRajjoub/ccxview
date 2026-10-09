/* stl.h -- STL reader: a triangle list, binary or ASCII, no ids, no results. For
   geometry shown beside the results (a pin, a clamp, a housing). Headless; never
   crashes on a truncated or garbage file: it says why and returns false. */
#ifndef CV_STL_H
#define CV_STL_H

#include "base.h"

typedef struct {
    float*    xyz;            /* 3 per vertex */
    uint32_t  n_vert;
    uint32_t* tri;            /* 3 vertex indices per triangle, counter-clockwise seen from outside (as written) */
    float*    nrm;            /* 3 per triangle: the unit normal from its vertices (the file's when it is degenerate) */
    uint32_t  n_tri;
    float     lo[3], hi[3];   /* bounds */
    bool      binary;
    uint32_t  skipped;        /* triangles left out: a coordinate not a number */
} cv_stl;

/* Binary when the size is 84 + 50 n for the count n in the header (a binary
   file may begin with "solid" too), else ASCII when it begins with "solid".
   weld: vertices at the very same position shared (exact match), else three
   of its own per triangle. false with err filled when nothing could be read. */
bool cv_stl_parse(cv_stl* s, const char* data, size_t size, bool weld, char* err, size_t errn);
bool cv_stl_read(cv_stl* s, const char* path, bool weld, char* err, size_t errn);
void cv_stl_free(cv_stl* s);
/* the outline: edges where the faces meet at more than deg degrees, open edges and
   those of more than two faces, as vertex pairs (meaningful on a welded mesh);
   *out malloc'd, *n pairs. false: out of memory */
bool cv_stl_edges(const cv_stl* s, float deg, uint32_t** out, uint32_t* n);
/* a binary STL of n triangles (9 floats each), normals from the vertices; for tests and fixtures */
bool cv_stl_write(const char* path, const float* tri9, uint32_t n);

#endif
