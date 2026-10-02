/* anchor.h -- where a floating overlay (the legend, the axes gizmo) sits in the
   3D view: a corner of the view plus the gap from that corner's two edges.
   Kept as a corner, not as x/y, so a resized or maximised window leaves the
   overlay in its corner instead of somewhere in the middle. The gaps are in
   unscaled pixels (times the UI scale on screen), so a zoom change keeps them
   in proportion. Text form, for the ini and --opt: "tr" or "tr,dx,dy"
   (tl tr bl br); "auto" or "" means the overlay's own default place. */
#ifndef CV_ANCHOR_H
#define CV_ANCHOR_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CV_TL, CV_TR, CV_BL, CV_BR };

typedef struct {
    bool  set;      /* false: the overlay's default corner and gap */
    int   corner;   /* CV_TL .. CV_BR */
    float dx, dy;   /* gap from the corner's vertical / horizontal edge, unscaled px */
} cv_anchor;

typedef struct { float x, y, w, h; } cv_box;

static const char* const cv_corner_name[4] = { "tl", "tr", "bl", "br" };

/* false on text that is none of the forms above (the anchor is left alone);
   a bare corner gets def_gap on both edges */
static inline bool cv_anchor_parse(const char* s, cv_anchor* a, float def_gap) {
    while (*s == ' ') s++;
    if (!*s || !strcmp(s, "auto")) { *a = (cv_anchor){ 0 }; return true; }
    int c = -1;
    for (int i = 0; i < 4; i++)
        if (!strncmp(s, cv_corner_name[i], 2)) c = i;
    if (c < 0) return false;
    s += 2;
    float dx = def_gap, dy = def_gap;
    if (*s == ',') {
        char* e;
        dx = strtof(s + 1, &e);
        if (e == s + 1 || *e != ',') return false;
        s = e;
        dy = strtof(s + 1, &e);
        if (e == s + 1) return false;
        s = e;
    }
    while (*s == ' ') s++;
    if (*s || dx != dx || dy != dy || dx < 0 || dy < 0 || dx > 1e5f || dy > 1e5f) return false;
    *a = (cv_anchor){ true, c, dx, dy };
    return true;
}

static inline void cv_anchor_format(const cv_anchor* a, char* out, size_t n) {
    if (!a->set) snprintf(out, n, "auto");
    else snprintf(out, n, "%s,%.0f,%.0f", cv_corner_name[a->corner & 3], a->dx, a->dy);
}

/* The overlay's box (w x h) in the view, kept inside it */
static inline cv_box cv_anchor_place(cv_anchor a, cv_box view, float w, float h, float scale) {
    bool right = a.corner == CV_TR || a.corner == CV_BR, bottom = a.corner == CV_BL || a.corner == CV_BR;
    cv_box b = { right ? view.x + view.w - w - a.dx * scale : view.x + a.dx * scale,
                 bottom ? view.y + view.h - h - a.dy * scale : view.y + a.dy * scale, w, h };
    if (b.x > view.x + view.w - w) b.x = view.x + view.w - w;
    if (b.y > view.y + view.h - h) b.y = view.y + view.h - h;
    if (b.x < view.x) b.x = view.x;
    if (b.y < view.y) b.y = view.y;
    return b;
}

/* The anchor of a box dropped at b: the view corner nearest its centre */
static inline cv_anchor cv_anchor_from_box(cv_box b, cv_box view, float scale) {
    bool right = b.x + b.w * 0.5f > view.x + view.w * 0.5f, bottom = b.y + b.h * 0.5f > view.y + view.h * 0.5f;
    float dx = right ? view.x + view.w - (b.x + b.w) : b.x - view.x;
    float dy = bottom ? view.y + view.h - (b.y + b.h) : b.y - view.y;
    if (scale <= 0) scale = 1;
    cv_anchor a = { true, (bottom ? CV_BL : CV_TL) + (right ? 1 : 0), dx < 0 ? 0 : dx / scale, dy < 0 ? 0 : dy / scale };
    return a;
}

/* The legend shortened so it ends `gap` short of the gizmo where the two would
   overlap: it keeps its top and ends above the gizmo when it is the upper one,
   keeps its bottom and starts below it otherwise. The gizmo never moves. A
   legend that would get shorter than min_h stays as it is: a stub of a colour
   bar is worse than an overlap. */
static inline cv_box cv_legend_fit(cv_box leg, cv_box giz, float gap, float min_h) {
    bool cross_x = leg.x < giz.x + giz.w && giz.x < leg.x + leg.w;
    bool cross_y = leg.y < giz.y + giz.h + gap && giz.y < leg.y + leg.h + gap;
    if (!cross_x || !cross_y) return leg;
    cv_box r = leg;
    if (leg.y + leg.h * 0.5f < giz.y + giz.h * 0.5f) {
        r.h = giz.y - gap - leg.y;
    } else {
        r.y = giz.y + giz.h + gap;
        r.h = leg.y + leg.h - r.y;
    }
    return r.h >= min_h ? r : leg;
}

#endif
