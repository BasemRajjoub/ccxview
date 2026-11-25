/* ui_glass.c -- glass panels: the docked panes (Scene, Toolbar, Timebar, Inspector, Status) see-through
   over the 3D view, their boxes, buttons and headers at their own opacity, and floating panes: cards
   with a gap round them and rounded corners. The style is changed for a pane's own begin..end only
   and put back after, so the floating windows stay as they are; a pane's own combos and menus keep
   an opaque background (pushed for its nk_begin alone), their items take the pane's widget look. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"

bool uii_glass(void) { return G.ui_glass && G.loaded && !G.hide_panels; }
bool uii_float(void) { return uii_glass() && G.ui_float; }
float uii_float_gap(float s) { return uii_float() ? roundf(8.f * s) : 0.f; }

/* what a pane changed, put back at its end */
static struct { struct nk_style_item* p; struct nk_style_item v; } SI[96];
static struct { struct nk_color* p; struct nk_color v; } SC[16];
static struct { float* p; float v; } SF[4];
static int nsi, nsc, nsf;

static float clamp01(float a) { return CV_MIN(CV_MAX(a, 0.2f), 1.f); }
static float lum(struct nk_color c) { return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.f; }

static void fade_item(struct nk_style_item* p, float a) {
    if (nsi >= (int)CV_COUNT(SI) || p->type != NK_STYLE_ITEM_COLOR) return;
    SI[nsi].p = p; SI[nsi].v = *p; nsi++;
    p->data.color.a = (nk_byte)lroundf(p->data.color.a * a);
}
static void fade_color(struct nk_color* p, float a) {
    if (nsc >= (int)CV_COUNT(SC)) return;
    SC[nsc].p = p; SC[nsc].v = *p; nsc++;
    p->a = (nk_byte)lroundf(p->a * a);
}
static void set_float(float* p, float v) {
    if (nsf >= (int)CV_COUNT(SF)) return;
    SF[nsf].p = p; SF[nsf].v = *p; nsf++;
    *p = v;
}
/* normal at a; hover and pressed keep more, so the feedback stays plain */
static float up(float a, float k) { return a + (1.f - a) * k; }
static void fade3(struct nk_style_item* n, struct nk_style_item* h, struct nk_style_item* x, float a) {
    fade_item(n, a); fade_item(h, up(a, 0.5f)); fade_item(x, up(a, 0.7f));
}
static void fade_button(struct nk_style_button* b, float a) { fade3(&b->normal, &b->hover, &b->active, a); }

static void fade_widgets(struct nk_style* st, bool light) {
    /* a light theme's dim text reads worse through: its widgets stay more solid */
    float lo = light ? 0.45f : 0.2f;
    float box = CV_MAX(clamp01(G.ui_glass_boxes), lo), btn = CV_MAX(clamp01(G.ui_glass_buttons), lo),
          hdr = CV_MAX(clamp01(G.ui_glass_headers), lo);
    if (box < 1.f) {
        fade3(&st->edit.normal, &st->edit.hover, &st->edit.active, box);
        fade3(&st->combo.normal, &st->combo.hover, &st->combo.active, box);
        fade_button(&st->combo.button, box);
        fade3(&st->property.normal, &st->property.hover, &st->property.active, box);
        fade3(&st->property.edit.normal, &st->property.edit.hover, &st->property.edit.active, box);
        fade_button(&st->property.inc_button, box); fade_button(&st->property.dec_button, box);
        fade3(&st->checkbox.normal, &st->checkbox.hover, &st->checkbox.active, box);
        fade3(&st->option.normal, &st->option.hover, &st->option.active, box);
        fade3(&st->slider.normal, &st->slider.hover, &st->slider.active, box);
        fade_color(&st->slider.bar_normal, box); fade_color(&st->slider.bar_hover, up(box, 0.5f));
        fade_color(&st->slider.bar_active, up(box, 0.7f));
        fade3(&st->scrollv.normal, &st->scrollv.hover, &st->scrollv.active, box);
    }
    if (btn < 1.f) {
        fade_button(&st->button, btn);
        fade_button(&st->contextual_button, btn);
        fade_button(&st->menu_button, btn);
        fade_button(&st->tab.tab_maximize_button, btn); fade_button(&st->tab.tab_minimize_button, btn);
        fade_button(&st->tab.node_maximize_button, btn); fade_button(&st->tab.node_minimize_button, btn);
        fade3(&st->selectable.normal_active, &st->selectable.hover_active, &st->selectable.pressed_active, btn);
    }
    if (hdr < 1.f) {
        fade_item(&st->tab.background, hdr);
        fade3(&st->selectable.normal, &st->selectable.hover, &st->selectable.pressed, hdr);   /* the tab strip */
    }
}

/* a docked pane's window: nk_end it with uii_pane_end, open or not */
bool uii_pane_begin(struct nk_context* ctx, const char* name, struct nk_rect r, nk_flags flags) {
    nsi = nsc = nsf = 0;
    if (!uii_glass()) return nk_begin(ctx, name, r, flags);
    struct nk_style* st = &ctx->style;
    struct nk_color c = st->window.fixed_background.data.color;
    bool light = lum(c) >= 0.5f;
    float a = clamp01(G.ui_glass_alpha);
    /* the text has to read over a bright model: a dark theme's a shade darker, a light one's
       a little lighter and never quite as clear */
    if (!light) c = mix(c, nk_rgb(0, 0, 0), 0.35f);
    else { c = mix(c, nk_rgb(255, 255, 255), 0.25f); a = CV_MAX(a, 0.85f); }
    c.a = (nk_byte)lroundf(a * 255.f);
    fade_widgets(st, light);
    if (uii_float()) {
        float s = ui_scale();
        set_float(&st->window.rounding, roundf(8.f * s));
        struct nk_color b = light ? nk_rgba(0, 0, 0, 40) : nk_rgba(255, 255, 255, 28);
        if (nsc < (int)CV_COUNT(SC)) { SC[nsc].p = &st->window.border_color; SC[nsc].v = st->window.border_color; nsc++; }
        st->window.border_color = b;
        set_float(&st->window.border, 1.f);
    }
    nk_style_push_style_item(ctx, &st->window.fixed_background, nk_style_item_color(c));
    bool open = nk_begin(ctx, name, r, flags);
    nk_style_pop_style_item(ctx);
    return open;
}

void uii_pane_end(struct nk_context* ctx) {
    nk_end(ctx);                               /* the border drawn with the pane's rounding */
    while (nsi > 0) { nsi--; *SI[nsi].p = SI[nsi].v; }
    while (nsc > 0) { nsc--; *SC[nsc].p = SC[nsc].v; }
    while (nsf > 0) { nsf--; *SF[nsf].p = SF[nsf].v; }
}
