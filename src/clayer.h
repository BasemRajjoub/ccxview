/* clayer.h -- the contact interface layer: a solid drawn over each slave face, from the
   face to its corners' projections on their master faces, so that its thickness at a
   corner is the gap there, like an adhesive between the two bodies. Only a picture: not
   an element of the model. Headless.

   A point of the layer is where it lies undeformed and how it moves (cv_lpt; the motion
   as app_node_disp6 gives it, the displacement and its second part): it is drawn at
   p + scale d, so the layer follows the deformed shape on screen whatever the scale. */
#ifndef CV_CLAYER_H
#define CV_CLAYER_H

#include "base.h"

typedef struct { float p[3], d[6]; } cv_lpt;

/* where the top of the layer goes over a slave corner */
enum { CV_CLAY_FOOT,        /* on the corner's foot on its master face, as drawn (exaggerated with the shape) */
       CV_CLAY_TRUE };      /* the corner less its true gap along the face's normal: the thickness the true gap */

/* The top over slave corner s: f its foot on the master face, n the face's unit normal
   there (pointing to the slave side), gap the signed gap (negative: penetrating). minth:
   the least thickness, a skin where the gap is closed, grown towards the master (into
   the slave where the corner penetrates). gap NaN or f NULL: the top on s, 0 thick.
   Returns the thickness: the gap (its sign kept), 0 when unknown. */
float cv_clay_top(const cv_lpt* s, const cv_lpt* f, const float n[3], float gap, int place, float minth, cv_lpt* top);

/* The prism over a slave face of n corners (3 or 4, in the order whose right-hand normal
   points out of the slave body, towards its master): bottom b[] on the face, top t[], a
   value per corner v[]. Its triangles (3 points, 3 values each) turned out of the prism:
   the bottom faces the slave body, the top the master. patches: every face of the prism
   split into a patch per corner (the corner, the middles of its two edges, the face's
   centre) in that corner's value, for a value that is a category and must not blend.
   Returns the count of triangles, at most CV_CLAY_MAXTRI. */
#define CV_CLAY_MAXTRI 48
int cv_clay_prism(int n, const cv_lpt* b, const cv_lpt* t, const float* v, bool patches, cv_lpt* tri, float* val);

/* its edges, 2 points each: the bottom's, the top's, the corners' risers; at most 12 */
int cv_clay_edges(int n, const cv_lpt* b, const cv_lpt* t, cv_lpt* seg);

#endif
