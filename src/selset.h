/* selset.h -- the selection's set algebra: lists of element or node indices
   (dense, as in cv_frd) combined new / add / remove / intersect, inverted
   within what is shown, and turned from elements into their nodes and back.
   Every list a function returns is sorted, each index once, malloc'd (NULL when
   empty: check the count, not the pointer). "Shown" is a per element mask
   (vis, NULL: every element); the shown nodes are the nodes of shown elements.
   Headless. */
#ifndef CV_SELSET_H
#define CV_SELSET_H

#include "frd.h"

enum { CV_SEL_NEW, CV_SEL_ADD, CV_SEL_REMOVE, CV_SEL_AND, CV_SEL_MODES };

/* a per index mask (1: in the list) of total entries, NULL when out of memory */
uint8_t*  cv_sel_mask(const uint32_t* a, uint32_t n, uint32_t total);
/* the indices whose mask entry is non-zero */
uint32_t* cv_sel_from_mask(const uint8_t* m, uint32_t total, uint32_t* n);

/* a (the current list) with b (what was just picked) by mode: NEW b, ADD a or b,
   REMOVE a and not b, AND a and b. Indices >= total are dropped. false only when
   out of memory (out and n untouched). */
bool cv_sel_combine(int mode, const uint32_t* a, uint32_t na, const uint32_t* b, uint32_t nb,
                    uint32_t total, uint32_t** out, uint32_t* n);

/* per node, 1: a node of a shown element */
uint8_t*  cv_sel_shown_nodes(const cv_frd* f, const uint8_t* vis);

/* the shown elements not in el / the shown nodes not in nd */
uint32_t* cv_sel_invert_elems(const cv_frd* f, const uint8_t* vis, const uint32_t* el, uint32_t ne, uint32_t* n);
uint32_t* cv_sel_invert_nodes(const cv_frd* f, const uint8_t* vis, const uint32_t* nd, uint32_t nn, uint32_t* n);

/* the nodes of the elements */
uint32_t* cv_sel_elem_nodes(const cv_frd* f, const uint32_t* el, uint32_t ne, uint32_t* n);
/* the shown elements with every node (any: at least one) among nd */
uint32_t* cv_sel_node_elems(const cv_frd* f, const uint8_t* vis, const uint32_t* nd, uint32_t nn, bool any, uint32_t* n);

#endif
