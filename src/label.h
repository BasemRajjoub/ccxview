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
   toward the eye for its visibility test; 0: a couple of pixels) */
#define CV_LABEL_FLOATS 18

/* the glyph quads of text at the anchor (pos[3] disp[3] disp2[3]), the text's top-left
   dx, dy px from the anchor's point; appended to gly. With box, one quad over the text
   padded by pad px, uv the white pixel, appended to box. Returns the text width in px. */
float cv_label_layout(const cv_label_metrics* m, const float anchor[9], const char* text,
                      float dx, float dy, bool box, float pad, float pull, cv_fvec* gly, cv_fvec* boxes);
float cv_label_width(const cv_label_metrics* m, const char* text);

/* thinning: pts projected to window px with a depth (nearer = smaller, 0 .. 1); those
   inside the viewport, nearest first, no two closer than dx px across and dy px down (the
   labels' box: their widest plus a gap, their height plus the gap; 0: every one inside).
   Chosen ids to out (at most max_out), returns how many. pts is reordered. */
typedef struct { float sx, sy, depth; uint32_t id; } cv_label_pt;
uint32_t cv_label_thin(cv_label_pt* pts, uint32_t n, float dx, float dy, float vx, float vy, float vw, float vh,
                       uint32_t* out, uint32_t max_out);

/* a coarse pass for huge sets: of the points in one 3D cell of size cell, the first
   met is kept; xyz 3 per point; kept indices to out, returns how many */
uint32_t cv_label_coarse(const float* xyz, uint32_t n, float cell, uint32_t* out);
#endif
