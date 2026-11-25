/* label.h -- labels on the model, the headless part: glyph quads from a metrics table,
   thinning of projected anchors to a spacing on screen (nearest first), a coarse 3D
   pass for huge sets. No G, no GPU. */
#ifndef CV_LABEL_H
#define CV_LABEL_H
#include "base.h"

/* one glyph of a baked font: pen advance, box from the pen (px, y down), atlas uv */
typedef struct { float xadvance, x0, y0, x1, y1, u0, v0, u1, v1; } cv_label_glyph;
/* a font as metrics: glyphs for codepoints first .. first + n - 1 (others draw as '?'),
   the line height, the atlas's white pixel (boxes are drawn with it) */
typedef struct { const cv_label_glyph* g; int first, n; float height; float white_u, white_v; } cv_label_metrics;

/* an instance (a glyph quad or a label's box), CV_LABEL_FLOATS numbers:
   pos[3], disp[3], disp2[3], off[2] (px from the anchor's screen point to the quad's
   top-left, y down), size[2] (px), uv0[2], uv1[2], pull (model units the point is moved
   toward the eye for its visibility test; 0: a couple of pixels; negative: this one is
   drawn in front of the model, the pull its magnitude) */
#define CV_LABEL_FLOATS 18

/* the glyph quads of text at the anchor (pos[3] disp[3] disp2[3]), the text's top-left
   dx, dy px from the anchor's point; appended to gly. With box, one quad over the text
   padded by pad px, uv the white pixel, appended to box. Returns the text width in px. */
float cv_label_layout(const cv_label_metrics* m, const float anchor[9], const char* text,
                      float dx, float dy, bool box, float pad, float pull, cv_fvec* gly, cv_fvec* boxes);
float cv_label_width(const cv_label_metrics* m, const char* text);
/* a leader: a 1 px wide line from (x0, y0) to (x1, y1) px off the anchor's point, to
   boxes: one box when it is upright or level, else a box per pixel row (or column, the
   shorter way) of the slant, each spanning its part of the line; a long slant in at most
   CV_LABEL_LEADER_STEPS steps, each a row (column) and a 1 px riser to the next, so a
   leader is at most 2 * CV_LABEL_LEADER_STEPS - 1 boxes however far it reaches */
enum { CV_LABEL_LEADER_STEPS = 48 };
void cv_label_leader(const cv_label_metrics* m, const float anchor[9], float x0, float y0, float x1, float y1, float pull, cv_fvec* boxes);

/* the point on a box (x, y, w, h) nearest to (px, py): where a leader meets it */
void cv_label_nearest(float x, float y, float w, float h, float px, float py, float* qx, float* qy);

/* a moved label: its key (a kind and an id, a number or a name) and its offset in px
   from where it would be, x right, y down. As text "node 940 12 -30": the kind, the
   id (it may hold spaces), the two numbers last. */
typedef struct { char kind[16]; char id[256]; float dx, dy; } cv_label_off;
bool cv_label_off_parse(const char* s, cv_label_off* o);      /* false: not two numbers after a kind and an id */
void cv_label_off_format(const cv_label_off* o, char* out, size_t n);
/* the command line's KIND:ID:DX,DY ("set:EHOLE:40,-20"; the id may hold ':') */
bool cv_label_off_parse_arg(const char* s, cv_label_off* o);

/* the topmost of n boxes (x, y, w, h, 4 floats each, later ones drawn over earlier)
   holding the point; -1 none */
int cv_label_hit(const float* box, uint32_t n, float x, float y);

/* thinning: pts projected to window px with a depth (nearer = smaller, 0 .. 1); those
   inside the viewport, nearest first, no two closer than dx px across and dy px down (the
   labels' box: their widest plus a gap, their height plus the gap; 0: every one inside).
   The first n_pin points are pinned: taken whatever overlaps them, first and in their
   order, and the rest keep clear of them. Chosen ids to out (at most max_out), returns
   how many. pts is reordered. */
typedef struct { float sx, sy, depth; uint32_t id; } cv_label_pt;
uint32_t cv_label_thin(cv_label_pt* pts, uint32_t n, uint32_t n_pin, float dx, float dy, float vx, float vy, float vw, float vh,
                       uint32_t* out, uint32_t max_out);

/* a coarse pass for huge sets: of the points in one 3D cell of size cell, the first
   met is kept; xyz 3 per point; kept indices to out, returns how many */
uint32_t cv_label_coarse(const float* xyz, uint32_t n, float cell, uint32_t* out);

/* the k smallest and the k largest of n values (v[ids[i]], or v[i] without ids), NaN and
   infinities left out: their indices to lo (smallest first) and hi (largest first), both k
   long; returns how many each holds (less than k when there are fewer values) */
uint32_t cv_label_extremes(const float* v, const uint32_t* ids, uint32_t n, uint32_t k, uint32_t* lo, uint32_t* hi);
#endif
