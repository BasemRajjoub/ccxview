/* frd.h -- CalculiX .frd reader (ASCII + binary).

   Two stages, so a 1 GB file opens at the cost of its geometry:
     cv_frd_parse()      one pass: nodes, elements, and an INDEX of every field
                         (name, components, byte offset) -- no field values;
     cv_frd_read_field() decodes one field of one step on demand.

   Never fails on bad input: unreadable records are skipped and reported in
   `msgs`; the only false return is running out of memory. */
#ifndef CV_FRD_H
#define CV_FRD_H

#include "base.h"

enum { CV_MAX_COMP = 32 };

typedef struct {
    char     name[16];
    int      ncomp;                      /* stored components (pseudo "ALL" excluded) */
    char     comp[CV_MAX_COMP][12];
    int      fmt;                        /* 1 ASCII, 2 binary float, 3 binary double */
    uint64_t data_off;                   /* byte offset of the first data record */
    uint64_t line;                       /* 1-based line of data_off (ASCII) */
    uint32_t n_rec;                      /* binary: records per 6-component pass */
} cv_field_desc;

typedef struct {
    int            step, inc;
    float          time;           /* for a modal increment: the eigenfrequency */
    bool           modal;          /* *FREQUENCY / buckling mode: amplitude is arbitrary */
    int            nfields;
    cv_field_desc* fields;
} cv_step;

/* node id -> dense index. Flat table when ids are dense, else open addressing. */
typedef struct {
    uint32_t* flat;  uint32_t flat_n;
    uint32_t* keys;  uint32_t* vals; uint32_t hcap;
} cv_idmap;

typedef struct {
    const char* data;  size_t size;      /* not owned: the caller's mapping */

    uint32_t  n_nodes;
    uint32_t* node_id;
    float*    xyz;                       /* 3 * n_nodes */
    cv_idmap  idmap;
    cv_idmap  emap;                      /* element id -> index */

    uint32_t  n_elems;                   /* CSR connectivity, dense node indices */
    uint32_t* elem_id;
    uint8_t*  etype;                     /* FRD type code 1..12 */
    uint32_t* emat;                      /* raw material number */
    uint32_t* egrp;                      /* raw group number */
    uint32_t* eoff;                      /* n_elems + 1 */
    uint32_t* conn;

    int       n_steps;
    cv_step*  steps;

    cv_msgs   msgs;
} cv_frd;

bool     cv_frd_parse(cv_frd* f, const char* data, size_t size);
void     cv_frd_free(cv_frd* f);
uint32_t cv_frd_node_index(const cv_frd* f, uint32_t id);   /* UINT32_MAX if absent */
uint32_t cv_frd_elem_index(const cv_frd* f, uint32_t id);   /* UINT32_MAX if absent */
/* How cv_frd_match_elems resolved the elements of `from`. */
typedef struct {
    uint32_t by_id, by_nodes, none;
    bool     shifted;                    /* every by_nodes match at one id offset: */
    int64_t  offset;                     /* to id - from id */
} cv_elem_match;
/* Per element of `from`, the index of the same element in `to` (UINT32_MAX none);
   malloc'd, NULL on OOM. Ids are not trusted blindly: some solvers renumber
   elements (FEMaster writes them from 0, its deck from 1). The same id counts when
   it has the same nodes (by node id, any order), else the element of `to` with
   those nodes; failing both, the same id while most ids agreed (CalculiX expands
   shells into solids with new nodes but keeps element numbers). info may be NULL. */
uint32_t* cv_frd_match_elems(const cv_frd* from, const cv_frd* to, cv_elem_match* info);
/* (Re)build the node and element id maps from node_id / elem_id; false on OOM. */
bool     cv_frd_build_maps(cv_frd* f, size_t* node_dups, size_t* elem_dups);

/* Decode one field into out[node * ncomp + c]. Nodes without a value get NaN.
   Problems go to `msgs` (may be NULL). */
void     cv_frd_read_field(const cv_frd* f, const cv_field_desc* d, float* out, cv_msgs* msgs);

int         cv_frd_type_nodes(int frd_type);   /* nodes per element, 0 if unknown */
const char* cv_frd_type_name(int frd_type);

/* Parse a number the way FRD writes it: E12.5, Fortran 'D' exponents, blanks. */
bool cv_parse_num(const char* s, const char* e, double* out);

#endif
