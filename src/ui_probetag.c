/* ui_probetag.c -- the probe's tag in the view: a small box beside what is probed
   (the node, the element, face or edge), what it is and the field's value there,
   below them the lines of the Inspect groups ticked "on tag" (G.tag_groups), a
   leader line to it. Read live, as the Inspect tab is; inside the view, in the
   pictures; dragged by hand (app_labelpos.c), the leader follows. G.probe_tag, T. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include <math.h>
#include <stdio.h>

/* "node 123, element 65", "element 65, node 123", "face S2 of element 76", "edge 484 - 531" */
static void what_text(char* out, size_t n) {
    const cv_frd* f = &G.frd;
    uint32_t nd[8];
    int k = app_pick_nodes(nd);
    if (k && G.pick_kind == CV_PK_FACE) snprintf(out, n, "face S%u of element %u", (G.probe_face & 7) + 1, f->elem_id[G.probe_face >> 3]);
    else if (k) snprintf(out, n, "edge %u - %u", f->node_id[nd[0]], f->node_id[nd[k - 1]]);
    else if (G.pick_kind == CV_PK_ELEM) snprintf(out, n, "element %u, node %u", f->elem_id[G.probe.elem], f->node_id[G.probe.node]);
    else snprintf(out, n, "node %u, element %u", f->node_id[G.probe.node], f->elem_id[G.probe.elem]);
}

enum { TAG_MAX = 12, TAG_W = 64 };              /* lines, characters a line */

void uii_window_probe_tag(struct nk_context* ctx, float s) {
    float p[3], ax, ay;
    if (!G.probe_tag || !app_pick_anchor(p) || !app_project(v3_make(p[0], p[1], p[2]), &ax, &ay)) { app_probe_tag_box(NULL); return; }
    char l1[96], l2[160], more[TAG_MAX][TAG_W + 4];
    const char* line[TAG_MAX] = { l1, l2 };
    bool hd[TAG_MAX] = { 0 };
    what_text(l1, sizeof l1);
    uii_probe_value_text(l2, sizeof l2);
    int nl = G.has_field ? 2 : 1;
    int nm = uii_details_tag(line + nl, hd + nl, TAG_MAX - nl);   /* the groups ticked "on tag", read live */
    for (int i = nl; i < nl + nm; i++) {            /* a long line cut short */
        size_t len = strlen(line[i]);
        snprintf(more[i], sizeof more[i], len > TAG_W ? "%.*s..." : "%.*s", TAG_W - (len > TAG_W ? 3 : 0), line[i]);
        line[i] = more[i];
    }
    nl += nm;
    const struct nk_user_font* f = ctx->style.font;
    float pad = 5 * s, lh = f->height + 3 * s, w = 0;
    for (int i = 0; i < nl; i++) w = CV_MAX(w, f->width(f->userdata, f->height, line[i], (int)strlen(line[i])));
    w = CV_MIN(w + 2 * pad, G.vp_w - 8 * s);
    float h = nl * lh + 2 * pad - 3 * s;
    /* up and right of the item, as dragged; inside the view */
    float x = ax + (24 + G.probe_tag_off[0]) * s, y = ay - h - (24 - G.probe_tag_off[1]) * s;
    float x0 = G.vp_x + 4 * s, y0 = G.vp_y + 4 * s, x1 = G.vp_x + G.vp_w - 4 * s - w, y1 = G.vp_y + G.vp_h - 4 * s - h;
    float cx = CV_MIN(CV_MAX(x, x0), CV_MAX(x1, x0)), cy = CV_MIN(CV_MAX(y, y0), CV_MAX(y1, y0));
    if (app_label_dragging() && (cx != x || cy != y)) {   /* a drag stops at the edge, not past it */
        G.probe_tag_off[0] += (cx - x) / s; G.probe_tag_off[1] += (cy - y) / s;
    }
    x = cx; y = cy;
    float box[4] = { x, y, w, h };
    app_probe_tag_box(box);

    struct nk_rect wr = nk_rect(G.vp_x, G.vp_y, G.vp_w, G.vp_h);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    nk_style_push_float(ctx, &ctx->style.window.border, 0);
    if (nk_begin(ctx, "probetag", wr, NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT | NK_WINDOW_BACKGROUND)) {
        nk_window_set_bounds(ctx, "probetag", wr);
        struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
        const struct nk_style_window* st = &ctx->style.window;
        struct nk_color gold = nk_rgba(255, 214, 0, 255), shade = nk_rgba(0, 0, 0, 150);
        float lx = CV_MIN(CV_MAX(ax, x), x + w), ly = CV_MIN(CV_MAX(ay, y), y + h);   /* the box's point nearest the item */
        if (fabsf(lx - ax) + fabsf(ly - ay) > 2 * s) {
            nk_stroke_line(cv, ax, ay, lx, ly, 3.f * s, shade);
            nk_stroke_line(cv, ax, ay, lx, ly, 1.2f * s, gold);
        }
        nk_fill_circle(cv, nk_rect(ax - 2.5f * s, ay - 2.5f * s, 5 * s, 5 * s), gold);
        struct nk_color bg = st->background;
        bg.a = 235;
        nk_fill_rect(cv, nk_rect(x, y, w, h), 3 * s, bg);
        nk_stroke_rect(cv, nk_rect(x, y, w, h), 3 * s, 1.f * s, gold);
        for (int i = 0; i < nl; i++)
            nk_draw_text(cv, nk_rect(x + pad, y + pad + i * lh, w - 2 * pad, f->height), line[i], (int)strlen(line[i]), f, bg,
                         !i ? P.accent : hd[i] ? P.dim : ctx->style.text.color);
    }
    nk_end(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
}
