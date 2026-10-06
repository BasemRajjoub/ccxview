/* app_mesh.h -- mesh quality as a field (field_src 4): every quality measure of
   every element (quality.c), worked out once per file on the undeformed mesh, and
   what the Mesh quality window says about them. */
#ifndef CV_APP_MESH_H
#define CV_APP_MESH_H

#include "quality.h"
#include <stdint.h>
#include <stddef.h>

/* one measure over the mesh, in shown units */
typedef struct {
    uint32_t n;             /* elements it applies to */
    uint32_t poor;          /* past the usual limit */
    double   min, max, mean;
    uint32_t worst;         /* element index, UINT32_MAX none */
    uint32_t hist[10];      /* between min and max */
} mesh_stat;

/* the mesh at a glance */
typedef struct {
    uint32_t nodes, elems, used_nodes;
    uint32_t per_type[16];  /* elements per .frd type */
    uint32_t mats, groups;  /* distinct numbers */
    double   volume, area, length;   /* summed over solids, shells, beams; shown units */
    double   box[6];        /* bounding box min xyz, max xyz; shown units */
    uint32_t poor_any;      /* elements past any limit */
} mesh_info;

/* fills G.scalar and G.elem_val with measure G.mesh_q: per element its value, per
   node the worst of the elements around it; false with why[] saying why not */
bool mesh_eval_field(char* why, size_t n);
const char* mesh_unit(int q);                 /* "mm", "mm^3", "deg", "" */
bool mesh_stats(mesh_stat st[CV_MQ_N], mesh_info* info);   /* false: no mesh */
/* every measure of an element, as the probe shows it */
bool mesh_probe_text(uint32_t elem, char* out, size_t n);
double mesh_value(uint32_t elem, int q);      /* shown units, NaN none */
unsigned mesh_gen(void);                     /* changes whenever the measures are worked out again */
double mesh_len_scale(void);                 /* model -> shown lengths */
void mesh_clear(void);                       /* results freed (unload) */
void mesh_limits_check(void);                /* G.mq_lim changed: measured again, the field too */

#endif
