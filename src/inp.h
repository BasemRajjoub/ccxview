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
#include "field.h"      /* cv_csys */
#include "dat.h"

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

/* What is applied to the model: every data line of every step, in deck order.
   step: the *STEP the line stands in (0 = the first), -1 before any step. A card
   with OP=NEW leaves a marker (node / elem 0, set -2) that drops what its kind
   had in the steps before. cv_inp_applied folds the lines into what holds in one
   step. */
/* *BOUNDARY: node, first..last DOF (1-6 mechanical, 11 temperature), the value
   (0: held, else a prescribed displacement, rotation or temperature) */
typedef struct { uint32_t node; uint8_t dof_lo, dof_hi; int16_t step; float value; } cv_bc;
/* *CLOAD: node, DOF 1-6, magnitude. *CFLUX: DOF 11, heat into the node. */
typedef struct { uint32_t node; uint8_t dof; int16_t step; float value; } cv_cload;
/* On an element face (0-based CalculiX face). P: *DLOAD / *DSLOAD pressure, positive
   pushes on the face; on a plane element (CPS, CPE, CAX) the face is an edge.
   EDGE: *DLOAD EDNORn, a load on edge n of a shell, normal to it in its plane.
   FLUX: *DFLUX Sn, heat into the face. FILM: *FILM Fn, convection (value: the film
   coefficient). RAD: *RADIATE Rn (value: the emissivity). */
enum { CV_DL_P, CV_DL_EDGE, CV_DL_FLUX, CV_DL_FILM, CV_DL_RAD, CV_DL_N };
typedef struct { uint32_t elem; uint8_t face, kind; int16_t step; float value; } cv_dload;
/* On whole elements: an element set (set: index into sets) or one element (set -1).
   GRAV: value x direction v[0..2]. CENTRIF: value = omega^2, a point v[0..2] on the
   axis and its direction v[3..5]. FORCE: *DLOAD BX / BY / BZ, value along v[0..2].
   NEWTON: gravity between the bodies. HEAT: *DFLUX BF, heat per volume. */
enum { CV_BL_GRAV, CV_BL_CENTRIF, CV_BL_FORCE, CV_BL_NEWTON, CV_BL_HEAT, CV_BL_N };
typedef struct { uint8_t kind; int16_t step; int set; uint32_t elem; float value; float v[6]; } cv_body;
/* *TEMPERATURE: a temperature given to a node (a thermal load, not a support) */
typedef struct { uint32_t node; int16_t step; float value; } cv_ntemp;
/* *PRE-TENSION SECTION: a bolt cut at a surface (surf, index into surfs) or a beam
   element (elem), its reference node, and the direction of the preload when the
   deck gives one. The preload itself is a *CLOAD or *BOUNDARY on DOF 1 of ref. */
typedef struct { int surf; uint32_t elem, ref; float dir[3]; bool has_dir; } cv_pretension;

/* What holds in one step, folded from the lines above: one entry per node and DOF
   (dof_lo == dof_hi), per element face and kind, per element set and kind. */
typedef struct {
    cv_bc*    bcs;    uint32_t nbcs;
    cv_cload* cloads; uint32_t ncloads;
    cv_dload* dloads; uint32_t ndloads;
    cv_body*  body;   uint32_t nbody;
    cv_ntemp* temps;  uint32_t ntemps;
} cv_applied;

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
enum { CV_LINK_RIGID, CV_LINK_KINEMATIC, CV_LINK_DISTRIBUTING, CV_LINK_EQUATION, CV_LINK_TIE, CV_LINK_CONTACT };   /* *MPC: as EQUATION */
typedef struct {
    char      name[64];
    uint8_t   kind;
    uint32_t  ref;          /* node id, 0 = none */
    uint32_t  rot;          /* RIGID: the ROT NODE id (its DOFs 1-3 are the body's rotations), 0 = none */
    uint32_t* nodes;  uint32_t n;
    int       surf[2];      /* surface indices, -1 = none */
} cv_link;

/* A material's elastic constants and first yield stress as the deck gives them (the
   first temperature). ISO: E, nu. ENG: E1 E2 E3 nu12 nu13 nu23 G12 G13 G23.
   ORTHO / ANISO: the first nine stiffness terms. sy: *PLASTIC's first stress, 0 none. */
enum { CV_EL_NONE, CV_EL_ISO, CV_EL_ENG, CV_EL_ORTHO, CV_EL_ANISO };
typedef struct { uint8_t el; float c[9]; float sy; } cv_matprop;

/* id -> index pairs, sorted by id */
typedef struct { uint32_t id; int32_t ix; } cv_idix;
/* a composite shell: its layers are layer_ori[lay0 .. lay0 + nlay) */
typedef struct { uint32_t id, lay0, nlay; } cv_layered;

/* Output variables whose GLOBAL= decides whether the .frd holds local values:
   U (DISP, also FORCI), RF (FORC), V (VELO), VF (V3DF) in the *TRANSFORM system of
   their node; S (STRESS), E (TOSTRAIN, MESTRAIN), HFL (FLUX) in the *ORIENTATION
   of their element. */
enum { CV_OUT_U, CV_OUT_RF, CV_OUT_V, CV_OUT_VF, CV_OUT_S, CV_OUT_E, CV_OUT_HFL, CV_OUT_N };

typedef struct {
    cv_frd      mesh;       /* nodes + elements; emat = material index + 1, 0 = none */
    cv_set*     sets;       int nsets;
    cv_surface* surfs;      int nsurfs;
    cv_bc*      bcs;        uint32_t nbcs;      /* every *BOUNDARY line, all steps */
    cv_cload*   cloads;     uint32_t ncloads;
    cv_dload*   dloads;     uint32_t ndloads;
    cv_body*    body;       uint32_t nbody;
    cv_ntemp*   temps;      uint32_t ntemps;
    cv_pretension* pret;    uint32_t npret;
    int         cyc_n;      float cyc_axis[6];  /* *CYCLIC SYMMETRY MODEL: N= and the two points of its axis; 0 = none */
    cv_discrete* disc;      uint32_t ndisc;
    cv_link*    links;      int nlinks;
    char      (*mats)[64];  int nmats;
    cv_matprop* mprop;                          /* per material */
    cv_csys*    transforms; int ntransforms;
    cv_idix*    node_tr;    uint32_t nnode_tr;  /* node -> transform; a later *TRANSFORM wins */
    cv_csys*    orients;    int norients;
    char      (*orient_names)[64];              /* per orientation, upper case */
    cv_idix*    elem_ori;   uint32_t nelem_ori; /* element -> orientation; -1: one that cannot be
                                                   rebuilt (*DISTRIBUTION, composite shell) */
    /* per *STEP, per CV_OUT_: the system of the .frd output as CalculiX sets it
       from *NODE FILE / *EL FILE / *NODE OUTPUT / *ELEMENT OUTPUT: 'G' global,
       'L' local, ' ' not requested (the first card of a step clears the previous
       requests of its kind; a step without one keeps them) */
    char      (*outsys)[CV_OUT_N]; int nsteps;
    uint32_t*   shells;     uint32_t nshells;   /* ids of the shell elements (S3..S8R), sorted */
    cv_layered* comps;      uint32_t ncomps;    /* composite shells, by id */
    int32_t*    layer_ori;                      /* per layer: orientation, -2 none, -1 cannot be rebuilt */
    int32_t*    layer_mat;                      /* per layer: material index, -1 none */
    float*      layer_t;                        /* per layer: thickness, 0 not given */
    char        heading[128];
    cv_msgs     msgs;
} cv_inp;

/* Read an included file; path as written in the deck. Returns a malloc'd buffer
   the parser frees, or false when the file cannot be read. */
typedef bool (*cv_inp_reader)(void* user, const char* path, char** data, size_t* size);

bool cv_inp_parse(cv_inp* d, const char* data, size_t size, cv_inp_reader rd, void* user);
void cv_inp_free(cv_inp* d);
const cv_set* cv_inp_set(const cv_inp* d, const char* name, bool is_elem);   /* case-insensitive */
/* inp_loads.c: what is applied in step `step` (0-based; outside 0 .. nsteps-1: the
   last step). Supports given before the first step always hold; the lines of the
   steps up to this one follow each other, a later value for the same place
   replacing the earlier one, OP=NEW dropping all of its kind. false: out of memory. */
bool cv_inp_applied(const cv_inp* d, int step, cv_applied* a);
void cv_applied_free(cv_applied* a);

/* Results CalculiX wrote in local systems (GLOBAL=NO with *TRANSFORM, *ORIENTATION
   or shell elements) turned back to global. The .frd does not say which system its
   values are in; the deck does. Built once per (deck, .frd) pair.

   Nodal values (DISP, FORC, ...) are in the *TRANSFORM of their node: exact.
   Element values (STRESS, TOSTRAIN, FLUX) are turned at each integration point into
   the element's system, then extrapolated and averaged at the nodes. That is exact
   to undo where all elements around a node share one system. Every expanded shell
   element has its own system (gen3dfrom2d.f: the orientation's, or the global, x
   projected on the shell), so on a curved shell, as in a cylindrical orientation,
   the systems around a node differ: turned with their mean, approximately, while
   they differ little, else NaN. */
enum { CV_LOC_GLOBAL, CV_LOC_EXACT, CV_LOC_NEAR, CV_LOC_NONE };   /* cv_localsys.est */
typedef struct {
    int32_t* tr;            /* per .frd node: transform, -1 none */
    uint8_t* est;           /* per .frd node: CV_LOC_ state of its element values */
    float*   q;             /* per .frd node: 3x3, rows e1 e2 e3, when est is EXACT / NEAR */
    bool     any_tr, any_ori;
} cv_localsys;

enum { CV_LOC_TURNED = 1,   /* values were local and are now global */
       CV_LOC_NAN = 2,      /* some nodes could not be turned: set to NaN */
       CV_LOC_APPROX = 4 }; /* some were turned with the mean of differing systems */
/* largest turn (degrees) between the systems an averaged nodal value mixes before it
   is NaN. Measured at 10 degrees per element: about 0.3% of the peak for solids, 1% for
   shells; the error grows with the square of the angle. */
#define CV_LOC_SPAN 12.0

bool cv_localsys_init(cv_localsys* L, const cv_inp* d, const cv_frd* f);
/* Field `desc` of CalculiX step `step` (cv_step.step), decoded into vals: turned
   in place. Returns CV_LOC_ bits, 0 when the values were global already. */
int  cv_localsys_apply(const cv_localsys* L, const cv_inp* d, const cv_frd* f, int step,
                       const cv_field_desc* desc, float* vals);
void cv_localsys_free(cv_localsys* L);

/* The records of a .dat block CalculiX printed in a local system (*EL PRINT,
   GLOBAL=NO, the default) turned to global, exactly: each names its system. f is
   the mesh the element ids refer to (the .frd, or the deck's), for the integration
   point positions a cylindrical orientation needs. Returns CV_LOC_ bits; records
   whose system cannot be rebuilt become NaN. */
int  cv_localsys_dat(const cv_inp* d, const cv_frd* f, cv_dat_block* b);

/* What each .frd element is made of, and its material axes. CalculiX writes each
   layer of a composite shell as an element of its own (numbered on from the largest
   element number); those map back to their shell and layer here. Built once per
   (deck, .frd) pair. */
typedef struct {
    uint32_t* f2d;          /* per .frd element: the deck element (index), UINT32_MAX none */
    uint32_t* comp;         /* per .frd element: index into d->comps when a layer, else UINT32_MAX */
    uint16_t* layer;        /* per .frd element: its layer, when one */
    uint32_t* lay_off;      /* per d->comps, + 1: the shell's first entry in lay_elem */
    uint32_t* lay_elem;     /* per layer of each shell: its .frd element, UINT32_MAX none */
    uint32_t* disc;         /* ids of the discrete elements, sorted */
    bool      lost;         /* the .frd's extra elements are not this deck's layers */
} cv_elemmap;

bool cv_elemmap_init(cv_elemmap* m, const cv_inp* d, const cv_frd* f);
void cv_elemmap_free(cv_elemmap* m);
/* the material of .frd element e: index into d->mats, -1 none; *thick the layer's
   thickness (0 when not a layer or not given), *layer its layer (-1 none) */
int  cv_elemmap_mat(const cv_elemmap* m, const cv_inp* d, uint32_t e, float* thick, int* layer);
/* the axes .frd element e writes local (material) values in, at point x: rows e1 e2
   e3 in global. 0 global (Q = I), 1 Q, 2 Q but it changes inside the element
   (cylindrical), -1 cannot be rebuilt, -2 no element values (discrete) */
int  cv_elemmap_axes(const cv_elemmap* m, const cv_inp* d, const cv_frd* f, uint32_t e,
                     const float* x, double Q[3][3]);
/* .frd element e as part of a shell, for its section forces (shell.h): *shell the
   deck element (index) of the shell, the layers of a composite all naming theirs;
   Q its axes, rows e1 e2 e3 (the normal): the shell's *ORIENTATION, else the global
   x on it, as CalculiX's; a composite's the orientation its layers share, else the
   global x on it. false: not a shell, or its system cannot be rebuilt */
bool cv_elemmap_shell(const cv_elemmap* m, const cv_inp* d, uint32_t e, uint32_t* shell, double Q[3][3]);
/* a .dat record of composite shell `id`, integration point ip (1-based) of nip
   printed: the .frd element of its layer and *lip, the point within it. CalculiX
   prints a composite shell's points layer by layer. UINT32_MAX: not a composite. */
uint32_t cv_elemmap_dat(const cv_elemmap* m, const cv_inp* d, uint32_t id, int ip, int nip, int* lip);

/* the *TRANSFORM a node id stands in: index into d->transforms, -1 none */
int cv_inp_node_transform(const cv_inp* d, uint32_t node);

/* CalculiX element type name -> FRD type code (0 = not drawable), node count. */
int cv_inp_elem_type(const char* name, int* nn);

#endif
