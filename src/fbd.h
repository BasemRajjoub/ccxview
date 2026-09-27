/* fbd.h -- cgx geometry (.fbd): points, lines (straight / arc / spline),
   surfaces and named sets, ready to draw.

   Reads the *evaluated* form cgx writes with `send all fbd` (PNT, SEQA, LINE,
   LCMB, GSUR, GBOD, SETA, ...). A hand-written script that only uses those
   records with plain numbers reads directly too; anything else -- expressions,
   sweeps, copies, loops -- needs cgx to evaluate it first (cv_fbd_parse then
   reports needs_cgx). Never fails on bad input. */
#ifndef CV_FBD_H
#define CV_FBD_H

#include "base.h"

typedef struct {
    char      name[32];
    uint32_t* pts;   uint32_t npts;       /* point indices */
    uint32_t* crv;   uint32_t ncrv;       /* curve (line) indices */
    uint32_t* srf;   uint32_t nsrf;       /* surface indices */
    uint32_t* nodes; uint32_t nnod;       /* mesh node ids (after cgx meshed) */
    uint32_t* elems; uint32_t nel;        /* mesh element ids */
} cv_gset;

typedef struct {
    /* points */
    uint32_t  npts;
    float*    pxyz;                       /* 3 per point */
    char    (*pname)[32];
    /* curves, tessellated: curve i is polyline cxyz[coff[i] .. coff[i+1]) */
    uint32_t  ncrv;
    char    (*cname)[32];
    uint32_t* coff;
    float*    cxyz;                       /* 3 per polyline vertex */
    /* surfaces, triangulated: surface i owns triangles soff[i] .. soff[i+1] */
    uint32_t  nsrf;
    char    (*sname)[32];
    uint32_t* soff;
    float*    txyz;                       /* 9 per triangle */
    uint32_t  ntri;
    /* named sets */
    cv_gset*  sets;  int nsets;
    bool      needs_cgx;                  /* a script, not evaluated geometry */
    char      needs_why[96];              /* the first command that needs cgx */
    cv_msgs   msgs;
} cv_fbd;

bool cv_fbd_parse(cv_fbd* g, const char* data, size_t size);   /* false only on OOM */
void cv_fbd_free(cv_fbd* g);

#endif
