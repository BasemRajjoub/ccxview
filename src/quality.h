/* quality.h -- mesh quality of one element: its size, edges and shape. Headless:
   node coordinates in, numbers out.

   The measures and their usual limits follow the common checks of the pre-processors
   (Abaqus Verify Mesh, ANSYS shape checking, HyperMesh) and the Verdict library
   (Sandia, SAND2007-1751) for the scaled Jacobian:
     size       volume (solids), area (shells, plane elements), length (beams)
     edges      shortest and longest corner to corner edge, and their ratio (aspect)
     scaled Jacobian  the least over the corners of det(e1 e2 e3) / (|e1||e2||e3|),
                the edges from that corner; 1 for a cube or regular tet, <= 0 inverted
     Jacobian ratio   least / largest det J over all nodes (mid-side nodes too), the
                shape functions' own mapping; 1 for straight-sided, < 0 folded
     skew       equiangle skewness: how far the face angles stray from 60 (triangle)
                or 90 degrees (quad), 0 ideal, 1 degenerate
     angles     smallest and largest face corner angle
     warpage    how far a quad face is from flat: the angle between the normals of
                its two triangles, the worse of both diagonals
     shape factor  triangles and tets: size over that of the equilateral element with
                the same circumradius (Abaqus), 1 ideal
   And four overall scores that combine them, each the way its program does:
     ccxview    0 to 1, the weakest of aspect, scaled Jacobian, Jacobian ratio, skew,
                warpage and shape factor, each scored 1 ideal to 0 at its limit
     HyperMesh  quality index: per measure a penalty, 0 up to "good", 1 at "fail",
                10 at "worst"; the mean of the passing penalties plus the sum of
                the failing ones. 0 ideal, 1 or more fails a criterion
     ANSYS      element quality: C V / sqrt(sum e^2)^3 (solids), C A / sum e^2
                (shells), 1 for a cube, square or regular tet, 0 flat or inverted
     Abaqus     how many Verify Mesh shape checks the element fails (aspect,
                angles, shape factor) or is inside out; 0 passes
   A measure that does not apply to the element is NaN. Quadratic elements are
   measured on their corners, except the Jacobian ratio, which sees curved edges. */
#ifndef CV_QUALITY_H
#define CV_QUALITY_H

#include <stdbool.h>

enum {
    CV_MQ_SIZE, CV_MQ_EDGE_MIN, CV_MQ_EDGE_MAX, CV_MQ_ASPECT, CV_MQ_SJAC, CV_MQ_JRATIO,
    CV_MQ_SKEW, CV_MQ_ANGLE_MIN, CV_MQ_ANGLE_MAX, CV_MQ_WARP, CV_MQ_SHAPE,
    CV_MQ_CCX, CV_MQ_HMQI, CV_MQ_ANSYS, CV_MQ_ABAQUS,     /* the overall scores */
    CV_MQ_N
};
enum { CV_MQ_SCORE0 = CV_MQ_CCX };

typedef struct {
    const char* key;        /* short, for the command line and settings: "sjac" */
    const char* name;       /* "scaled Jacobian" */
    const char* tip;        /* what it is and what is poor */
    bool high_bad;          /* larger is worse */
    int  len_pow;           /* a length (1), or for size the element's dimension (-1); 0 none */
    bool deg;               /* in degrees */
    bool incl;              /* the limit itself counts as poor */
} cv_mq_info;

const cv_mq_info* cv_mq(int q);
int    cv_mq_find(const char* key);         /* by key or name, case blind; -1 none */
int    cv_mq_dim(int frd_type);             /* 3 solid, 2 shell / plane, 1 beam, 0 unknown */

/* the limit past which an element counts as poor: the user's (cv_mq_set_limit) or
   the usual one; NaN: none for this type. The scores are made against it. */
double cv_mq_limit(int q, int frd_type);
double cv_mq_usual_limit(int q, int frd_type);
void   cv_mq_set_limit(int q, double v);        /* every element type; 0: the usual again */
bool   cv_mq_poor(int q, int frd_type, double v);

/* how the overall scores are made, for a measure q of an element of type t:
   cv_mq_score   its ccxview score, 1 ideal to 0 at the limit; NaN: not in the score
   cv_mq_hm_good, cv_mq_hm_worst   the HyperMesh "good" and "worst" levels ("fail" is
                 cv_mq_limit); NaN: not in the quality index
   cv_mq_penalty its HyperMesh penalty, 0 to 10
   cv_mq_governing  the measure that sets the ccxview score of an element, -1 none */
double cv_mq_score(int q, int frd_type, double v);
double cv_mq_hm_good(int q, int frd_type);
double cv_mq_hm_worst(int q, int frd_type);
double cv_mq_penalty(int q, int frd_type, double v);
int    cv_mq_governing(int frd_type, const double v[CV_MQ_N]);

/* every measure of one element; x: its nn nodes in .frd order. Thread safe once
   cv_mq_init() has run (the shape function tables). */
void   cv_mq_init(void);
void   cv_mq_elem(int frd_type, int nn, const double (*x)[3], double out[CV_MQ_N]);

#endif
