/* inp.h -- CalculiX / Abaqus .inp reader: mesh, named sets, surfaces, materials.

   Builds the same cv_frd mesh the .frd reader does (no steps), so the whole
   viewer works on an unsolved deck. Element node lists are reordered into .frd
   order (quadratic hex/wedge mid-edge nodes) -- one convention everywhere.

   *INCLUDE is followed through a caller-supplied reader, so an included file may
   carry data lines for a block its parent opened (cgx writes decks that way).
   Like the other readers it never fails on bad input: unknown keywords are
   skipped, bad lines reported, elements with missing nodes dropped. */
#ifndef CV_INP_H
#define CV_INP_H

#include "frd.h"

typedef struct {
    char      name[64];
    bool      is_elem;      /* ELSET (element ids) or NSET (node ids) */
    uint32_t* ids;          /* raw ids, deduplicated, sorted */
    uint32_t  n;
} cv_set;

typedef struct {
    char      name[64];
    uint32_t* elem;         /* element id per face (TYPE=ELEMENT) */
    uint8_t*  face;         /* 0-based CalculiX face (S1 -> 0) */
    uint32_t  n;
    uint32_t* nodes;        /* node ids (TYPE=NODE) */
    uint32_t  nn;
} cv_surface;

/* *BOUNDARY: node, first..last DOF (1-6 mechanical, 11 temperature) */
typedef struct { uint32_t node; uint8_t dof_lo, dof_hi; } cv_bc;
/* *CLOAD: node, DOF, magnitude (the last *STEP that sets it wins per node+dof) */
typedef struct { uint32_t node; uint8_t dof; float value; } cv_cload;
/* *DLOAD pressure: element, 0-based face, magnitude (positive = pushes on the face) */
typedef struct { uint32_t elem; uint8_t face; float value; } cv_dload;

/* Discrete elements: springs, dashpots, masses, gaps. Two-node ones are also
   mesh elements (FRD type 11, as CalculiX writes them); one-node ones live only
   here. dof: the direction of a SPRING1/2 or DASHPOT (from *SPRING / *DASHPOT),
   0 when unknown. */
enum { CV_DISC_SPRING, CV_DISC_DASHPOT, CV_DISC_MASS, CV_DISC_GAP, CV_DISC_DCOUP };
typedef struct { uint32_t id, n[2]; uint8_t kind, nn, dof; } cv_discrete;

/* Links between nodes: multi-point constraints and surface pairs.
   RIGID / KINEMATIC / DISTRIBUTING: a reference node and the nodes it drives
   (kinematic couplings name a surface instead; its nodes are resolved when
   drawn). EQUATION: the nodes of its terms, ref = the first. TIE / CONTACT:
   two surfaces (surf[0] slave, surf[1] master), no nodes. */
enum { CV_LINK_RIGID, CV_LINK_KINEMATIC, CV_LINK_DISTRIBUTING, CV_LINK_EQUATION, CV_LINK_TIE, CV_LINK_CONTACT };
typedef struct {
    char      name[64];
    uint8_t   kind;
    uint32_t  ref;          /* node id, 0 = none */
    uint32_t* nodes;  uint32_t n;
    int       surf[2];      /* surface indices, -1 = none */
} cv_link;

typedef struct {
    cv_frd      mesh;       /* nodes + elements; emat = material index + 1, 0 = none */
    cv_set*     sets;       int nsets;
    cv_surface* surfs;      int nsurfs;
    cv_bc*      bcs;        uint32_t nbcs;      /* every *BOUNDARY line, all steps */
    cv_cload*   cloads;     uint32_t ncloads;
    cv_dload*   dloads;     uint32_t ndloads;
    cv_discrete* disc;      uint32_t ndisc;
    cv_link*    links;      int nlinks;
    char      (*mats)[64];  int nmats;
    char        heading[128];
    cv_msgs     msgs;
} cv_inp;

/* Read an included file; path as written in the deck. Returns a malloc'd buffer
   the parser frees, or false when the file cannot be read. */
typedef bool (*cv_inp_reader)(void* user, const char* path, char** data, size_t* size);

bool cv_inp_parse(cv_inp* d, const char* data, size_t size, cv_inp_reader rd, void* user);
void cv_inp_free(cv_inp* d);
const cv_set* cv_inp_set(const cv_inp* d, const char* name, bool is_elem);   /* case-insensitive */

/* CalculiX element type name -> FRD type code (0 = not drawable), node count. */
int cv_inp_elem_type(const char* name, int* nn);

#endif
