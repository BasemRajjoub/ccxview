/* cel.h -- CalculiX contact elements (jobname.cel) and warning node sets
   (jobname_WarnNode*.nam). Headless.

   With *NODE FILE, CONTACT ELEMENTS in a step, ccx writes the contact elements
   it generated in every iteration to jobname.cel, in .inp syntax: one *ELEMENT
   card per element, its ELSET named contactelements_st<step>_in<increment>_
   at<attempt>_it<iteration>, element ids running on past the model's. The nodes
   are the model's (no *NODE block), so the elements follow the deformed shape.
   As ccx 2.22 writes them (verified on solved decks):
     node to surface, triangular master face   C3D4  m1 m2 m3 s
     node to surface, quadrilateral face       C3D6  m1 s m2 m4 s m3
                                               (the face's cycle m1 m2 m3 m4 is
                                               nodes 1 3 6 4; the slave twice)
     surface to surface                        C3D8  m1 m2 m3 m4 s1 s2 s3 s4
                                               (a triangle repeats its last
                                               corner); written once per slave
                                               integration point, so repeated
   Quadratic faces come with their corners only.

   ccx writes jobname_WarnNodeMissTiedContact.nam (2.22) for the slave nodes of
   a *TIE it could not tie, other jobname_WarnNode*.nam files for other
   warnings: *NSET files, read here with the .inp reader.

   Never fails on bad input: unreadable lines are counted and skipped. */
#ifndef CV_CEL_H
#define CV_CEL_H

#include "base.h"

enum { CV_CEL_N2S, CV_CEL_S2S };

/* one contact element: the master face's corners in cycle order (nm 3 or 4)
   and the slave node (n2s, ns 1) or slave face's corners (s2s, ns 3 or 4); node
   ids as the file gives them */
typedef struct {
    uint32_t id;
    uint8_t  kind;          /* CV_CEL_* */
    uint8_t  nm, ns;
    uint32_t m[4], s[4];
} cv_celem;

/* one ELSET of the file: the elements of one iteration, elem[first .. first + n).
   step, inc, att, it from the name (1-based as ccx counts), 0 when the name is
   not contactelements_st.._in.._at.._it.. */
typedef struct {
    char     name[64];
    int      step, inc, att, it;
    uint32_t first, n;
} cv_celset;

typedef struct {
    cv_celem*  elem;  uint32_t n;
    cv_celset* sets;  int nsets;        /* sorted by step, inc, att, it (unnamed last, in file order) */
    uint32_t   bad;                     /* lines or elements skipped */
} cv_cel;

bool cv_cel_parse(cv_cel* c, const char* data, size_t size);   /* false: out of memory */
void cv_cel_free(cv_cel* c);
/* the set holding the converged state of increment inc of step step: the last
   iteration of its last attempt; -1 when the file has none for it */
int  cv_cel_find(const cv_cel* c, int step, int inc);
/* the sets that end an increment (the last iteration of its last attempt), in
   order: their indices to out (may be NULL), how many returned */
int  cv_cel_ends(const cv_cel* c, int* out, int max);
/* the distinct elements of set k (s2s repeats one per integration point): their
   indices into c->elem to out (may be NULL; room for sets[k].n), how many */
uint32_t cv_cel_unique(const cv_cel* c, int k, uint32_t* out);

/* A .nam file (*NSET, NSET=... and node ids): every node id it lists, sorted and
   unique, to *ids (malloc'd, NULL when none); name: the first set's name (may be
   NULL, room for 64). false: out of memory or nothing readable. */
bool cv_nam_parse(const char* data, size_t size, uint32_t** ids, uint32_t* n, char* name);

#endif
