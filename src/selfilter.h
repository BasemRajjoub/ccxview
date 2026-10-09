/* selfilter.h -- which elements or nodes of a list pass a test: a coordinate in
   a range (x y z, or r theta z about an axis through a point), the field above or
   below a value, the top N % of the field, an element type or a material.
   Positions are the undeformed ones; an element is placed at the mean of its
   nodes, and with a nodal field it is judged by its highest node (its lowest
   for "below"), so "above 100" takes every element that reaches 100 somewhere.
   Headless. */
#ifndef CV_SELFILTER_H
#define CV_SELFILTER_H

#include "frd.h"

enum { CV_SQ_COORD, CV_SQ_ABOVE, CV_SQ_BELOW, CV_SQ_TOP, CV_SQ_TYPE, CV_SQ_MAT, CV_SQ_KINDS };
enum { CV_SC_X, CV_SC_Y, CV_SC_Z, CV_SC_R, CV_SC_THETA, CV_SC_AXIAL, CV_SC_N };

typedef struct {
    int      kind;          /* CV_SQ_* */
    int      coord;         /* CV_SC_*: r, theta (degrees, -180 .. 180) and axial about the axis */
    float    lo, hi;        /* the coordinate's range, ends included */
    int      axis;          /* 0..2: the cylinder's axis along X, Y, Z ... */
    float    at[3];         /* ... through this point */
    float    value;         /* above / below it; top: the percentage */
    uint32_t code;          /* the FRD type code, the material number */
} cv_selfilter;

/* A coordinate of point p as the filter reads it */
float cv_selfilter_coord(const cv_selfilter* q, const float p[3]);
/* Keep the passing entries of el (ne) in place, in order; returns how many.
   nval: a value per node, eval: per element (either may be NULL; eval wins);
   with neither a field test passes nothing. */
uint32_t cv_selfilter_elems(const cv_frd* f, const cv_selfilter* q, const float* nval, const float* eval,
                            uint32_t* el, uint32_t ne);
uint32_t cv_selfilter_nodes(const cv_frd* f, const cv_selfilter* q, const float* nval, uint32_t* nd, uint32_t nn);

/* "x>10", "x<10", "10<x<20", "r<5", "theta>30", "axial<2" (spaces allowed): kind
   CV_SQ_COORD with coord, lo and hi set (axis and at untouched); false when it is
   not one of these */
bool cv_selfilter_parse(const char* text, cv_selfilter* q);

#endif
