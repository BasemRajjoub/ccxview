/* contact.h -- contact measured as it is: a slave node's projection onto its master
   face, the signed gap there, and the node's contact status in the categories Ansys
   and Abaqus show (far open, near open, sliding, sticking). Headless.

   A face is given by its corners in cycle order (3: a flat triangle, 4: a bilinear
   quadrilateral, as CalculiX takes a master face; quadratic faces by their corners).
   Its normal follows the corners by the right-hand rule, as CalculiX orders a face:
   outward. The gap is positive on that side (open), negative behind it (penetration),
   as COPEN. */
#ifndef CV_CONTACT_H
#define CV_CONTACT_H

#include "base.h"

typedef struct {
    float foot[3];          /* the projection onto the face, clamped to it */
    float n[3];             /* the face's unit normal there */
    float gap;              /* signed distance from the foot along n */
    float w[4];             /* the foot as weights of the corners (sum 1): it moves with them */
    float xi, eta;          /* where: quad -1..1 each; triangle the weights of corners 2, 3 */
    bool  inside;           /* the projection fell on the face (nothing clamped) */
} cv_cproj;

/* p onto the face of corners c[0..n) (n 3 or 4); false when the face is degenerate */
bool cv_contact_project(const float p[3], const float c[][3], int n, cv_cproj* o);

/* the status of a slave node, Ansys numbering first */
enum { CV_CST_FAR, CV_CST_NEAR, CV_CST_SLIDE, CV_CST_STICK,
       CV_CST_CLOSED,       /* closed, stick or slip unknown (no CSHEAR) */
       CV_CST_PEN,          /* closed and deeper than the tolerance into the master */
       CV_CST_N };
extern const char* const cv_cst_names[CV_CST_N];
extern const float cv_cst_rgb[CV_CST_N][3];

/* what is known of a slave node; NaN for what is not */
typedef struct {
    float gap;              /* COPEN, else the measured gap */
    float press;            /* CPRESS (CalculiX writes a negative one where open) */
    float shear;            /* |CSHEAR1, CSHEAR2| */
    float mu;               /* friction coefficient of its pair; <= 0 frictionless */
    float tol;              /* gap <= tol: closed (without CPRESS); gap < -tol: penetrating */
    float near;             /* open and gap <= near: near open */
    bool  elem;             /* a contact element pairs it: closed when nothing else says */
} cv_cin;

/* Closed: CPRESS > 0 when known, else gap <= tol, else a contact element. Open: near
   or far by the gap. Closed and gap < -tol: penetrating. Else sticking when
   |CSHEAR| < mu CPRESS, sliding when not or frictionless, closed without CSHEAR.
   -1 when nothing is known. */
int cv_contact_status(const cv_cin* in);

/* The gap's colours, zero-anchored with independent ends: lo < 0 < hi. Map 0 (the
   links): red deep in, green at 0 (and within tol of it), blue wide open; map 1 (the
   contour): red, white at 0, blue. g beyond the ends takes the end's colour. */
enum { CV_CGAP_LINKS, CV_CGAP_CONTOUR };
void cv_contact_gap_rgb(int map, float g, float lo, float hi, float tol, float rgb[3]);
/* the same as n texels over lo .. hi, rgb[3 n] (a colour map texture) */
void cv_contact_gap_table(int map, float lo, float hi, float tol, float* rgb, int n);

/* The face nearest a point among many: a uniform grid over their boxes. fc: the
   corners, 4 a face (a triangle repeats its third: nc[i] = 3), 12 floats a face. */
typedef struct cv_cgrid cv_cgrid;
cv_cgrid* cv_cgrid_build(const float* fc, const uint8_t* nc, uint32_t nf);
void      cv_cgrid_free(cv_cgrid* g);
/* the face nearest p (by the distance to its clamped projection) and that projection;
   UINT32_MAX when there is none within maxd */
uint32_t  cv_cgrid_nearest(cv_cgrid* g, const float p[3], float maxd, cv_cproj* o);

#endif
