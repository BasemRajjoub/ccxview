/* ui_fpick.c -- the field picker (G.ui_field_picker): the top bar's field and component as one button,
   "STRESS › von Mises", "Mesh quality › aspect ratio". Its list: a search box on top; empty, what can
   colour the model on the left (ui_fpick_src.c: the step's fields, then .dat, calculated, failure, mesh
   quality) and the browsed entry's rows on the right; typed, every row that matches (fsearch.h). A click
   on a row (Enter: the one under the cursor) shows it and closes the list; Up / Down move the cursor,
   Left / Right the entry browsed, Esc closes. The wheel over the button steps the shown entry's rows. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"
#include "fsearch.h"

enum { MAXC = 96, MAXR = 64, MAXALL = 640, MAXHIT = 40, MAXROWS = 12 };
#define SEP "  \xe2\x80\xba  "                 /* › */

static struct { bool open, esc, want, foot; int focus; char q[48], wq[48]; int qn; int bf, cur; } F;   /* bf: the entry browsed, cur: the cursor */

void ui_fpick_escape(void) { F.esc = true; }
void ui_fpick_open(const char* q) { F.want = true; snprintf(F.wq, sizeof F.wq, "%s", q ? q : ""); app_wake(); }
float uii_fpick_w(void) { return 210; }
bool uii_fpick_ok(void) { return uii_field_state().ok || (G.loaded && G.frd.n_elems); }

static int wrap(int v, int n) { return n > 0 ? ((v % n) + n) % n : 0; }
static bool pickable(const uii_fprow* r) { return r->act != FPA_HEAD; }
static bool steppable(const uii_fprow* r) { return r->act != FPA_HEAD && r->act != FPA_WIN && r->act != FPA_CRIT; }
static int move(const uii_fprow* r, int n, int cur, int d) {        /* the next pickable row, d = +-1 */
    for (int k = 1; k <= n; k++) { int j = wrap(cur + d * k, n); if (pickable(&r[j])) return j; }
    return cur;
}
static int shown_row(const uii_fprow* r, int n) { for (int k = 0; k < n; k++) if (r[k].shown) return k; return -1; }
static int first_row(const uii_fprow* r, int n) { int k = shown_row(r, n); return k >= 0 ? k : n ? move(r, n, -1, 1) : 0; }

/* a row of the list; cur: under the cursor (filled), shown: what the view shows (accent) */
static bool row_item(struct nk_context* ctx, const char* mark, const char* text, bool cur, bool shown, bool* hov) {
    uii_test_mark(ctx, mark);
    if (hov && nk_widget_is_hovered(ctx)) *hov = true;
    nk_bool on = cur;
    if (shown) {
        nk_style_push_color(ctx, &ctx->style.selectable.text_normal, P.accent);
        nk_style_push_color(ctx, &ctx->style.selectable.text_hover, P.accent);
        nk_style_push_color(ctx, &ctx->style.selectable.text_normal_active, P.accent);
        nk_style_push_color(ctx, &ctx->style.selectable.text_hover_active, P.accent);
    }
    struct nk_style_selectable* st = &ctx->style.selectable;
    struct nk_color bg = ctx->style.window.background, fill = mix(bg, P.text, 0.12f);   /* plain rows, the cursor's faint */
    nk_style_push_style_item(ctx, &st->normal, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    nk_style_push_style_item(ctx, &st->hover, nk_style_item_color(mix(bg, P.text, 0.07f)));
    nk_style_push_style_item(ctx, &st->normal_active, nk_style_item_color(fill));
    nk_style_push_style_item(ctx, &st->hover_active, nk_style_item_color(fill));
    bool hit = nk_selectable_label(ctx, text, NK_TEXT_LEFT, &on) != 0;
    for (int k = 0; k < 4; k++) nk_style_pop_style_item(ctx);
    if (shown) for (int k = 0; k < 4; k++) nk_style_pop_color(ctx);
    return hit;
}

/* a heading in the right column, a thin line in the left */
static void heading(struct nk_context* ctx, const char* t) { nk_label_colored(ctx, t, NK_TEXT_LEFT, P.dim); }
static void divider(struct nk_context* ctx, float s, float row) {
    nk_layout_row_dynamic(ctx, roundf(7 * s), 1);
    struct nk_rect r;
    if (nk_widget(&r, ctx)) {
        float y = roundf(r.y + r.h * 0.5f) + 0.5f;
        nk_stroke_line(nk_window_get_canvas(ctx), r.x + 4 * s, y, r.x + r.w - 4 * s, y, 1.f, mix(P.dim, ctx->style.window.background, 0.45f));
    }
    nk_layout_row_dynamic(ctx, row, 1);
}

/* typed: every entry's rows that match, best first */
typedef struct { int c; uii_fprow r; } fhit;
static int hits(const uii_fstate* f, const uii_fpcat* c, int nc, fhit* out) {
    static uii_fprow all[MAXALL];
    static char kw[MAXALL][256];
    static const char* label[MAXALL], *kws[MAXALL];
    static int ac[MAXALL];
    int n = 0;
    for (int k = 0; k < nc; k++) {
        uii_fprow r[MAXR];
        int nr = uii_fp_rows(f, &c[k], r, MAXR);
        for (int j = 0; j < nr && n < MAXALL; j++) {
            if (!pickable(&r[j])) continue;
            all[n] = r[j]; ac[n] = k;
            snprintf(kw[n], sizeof kw[n], "%s %s %s", c[k].title, c[k].id, r[j].tip ? r[j].tip : "");
            label[n] = all[n].label; kws[n] = kw[n];
            n++;
        }
    }
    int ord[MAXHIT], m = cv_fs_rank(F.q, n, label, kws, NULL, ord, MAXHIT);
    for (int r = 0; r < m; r++) { out[r].c = ac[ord[r]]; out[r].r = all[ord[r]]; }
    return m;
}

/* the first line of a row's tip under the list */
static void footer(struct nk_context* ctx, float row, const char* t) {
    char b[160];
    snprintf(b, sizeof b, "%s", t ? t : "");
    char* nl = strchr(b, '\n');
    if (nl) *nl = 0;
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label_colored(ctx, b, NK_TEXT_LEFT, P.dim);
}

void uii_fpick(struct nk_context* ctx, float s, float row) {
    uii_fstate f = uii_field_state();
    bool esc = F.esc;
    F.esc = false;
    if (!uii_fpick_ok()) return;
    static uii_fpcat c[MAXC];
    int nc = uii_fp_cats(&f, c, MAXC), sc = -1, most = 1, nfrd = 0;
    for (int k = 0; k < nc; k++) {
        uii_fprow r[MAXR];
        most = CV_MAX(most, uii_fp_rows(&f, &c[k], r, MAXR));
        if (c[k].shown && sc < 0) sc = k;
        if (c[k].kind == FPK_FRD) nfrd++;
    }
    most = CV_MAX(most, nc + (nfrd && nfrd < nc));
    char lab[160];
    uii_fp_crumb(&f, lab, sizeof lab);

    struct nk_input* in = &ctx->input;
    struct nk_rect b = nk_widget_bounds(ctx);
    bool listing = ctx->current->popup.win && ctx->current->popup.active;
    if (!listing && sc >= 0 && in->mouse.scroll_delta.y != 0 && nk_input_is_mouse_hovering_rect(in, b)) {
        uii_fprow r[MAXR];
        int nr = uii_fp_rows(&f, &c[sc], r, MAXR), j = shown_row(r, nr), d = in->mouse.scroll_delta.y < 0 ? 1 : -1;   /* down: the next */
        for (int k = 1; k <= nr; k++) {
            int i = wrap((j < 0 ? 0 : j) + d * k, nr);
            if (steppable(&r[i])) { if (i != j) uii_fp_pick(&f, &r[i]); break; }
        }
        in->mouse.scroll_delta.y = 0;
    }
    tip(ctx, "The field and component shown: a click lists them all with a search box (fields, .dat,\n"
             "calculated, failure, mesh quality), the wheel steps the component");

    const float sp = ctx->style.window.spacing.y, rh = row + sp;
    const float gp = 2 * ctx->style.window.group_padding.y + 4 * s;
    float lh = CV_MIN(CV_MAX(most, 4), MAXROWS) * rh + gp;     /* the popup grows to what it holds */
    struct nk_mouse was_mouse = in->mouse;
    bool fake = F.want && !listing;                 /* --field-picker: a click on the button, made up */
    if (fake) {
        struct nk_vec2 cp = nk_vec2(b.x + b.w * 0.5f, b.y + b.h * 0.5f);
        in->mouse.pos = cp; in->mouse.buttons[NK_BUTTON_LEFT].clicked = 1;
        in->mouse.buttons[NK_BUTTON_LEFT].clicked_pos = cp; in->mouse.buttons[NK_BUTTON_LEFT].down = 1;
    }
    bool shown = nk_combo_begin_label(ctx, lab, nk_vec2(roundf(440 * s), rh + lh + (F.foot ? rh : 0) + 2 * ctx->style.window.padding.y + 8 * s));
    if (fake) { in->mouse = was_mouse; F.want = false; }
    if (!shown) {
        F.open = false;
        return;
    }
    bool opened = !F.open;
    if (opened) {                                   /* just opened: on what shows, the cursor in the box */
        F.open = true; F.focus = 2;                 /* again the next frame: the press that opened it took the box's focus */
        snprintf(F.q, sizeof F.q, "%s", fake ? F.wq : "");
        F.qn = (int)strlen(F.q);
        F.bf = CV_MAX(sc, 0);
    }
    bool up = nk_input_is_key_pressed(in, NK_KEY_UP), down = nk_input_is_key_pressed(in, NK_KEY_DOWN);
    bool left = nk_input_is_key_pressed(in, NK_KEY_LEFT), right = nk_input_is_key_pressed(in, NK_KEY_RIGHT);
    bool enter = nk_input_is_key_pressed(in, NK_KEY_ENTER);

    nk_layout_row_dynamic(ctx, row, 1);
    if (F.focus) { nk_edit_focus(ctx, NK_EDIT_FIELD); F.focus--; }
    struct nk_rect eb = nk_widget_bounds(ctx);
    int was = F.qn;
    nk_edit_string(ctx, NK_EDIT_FIELD, F.q, &F.qn, (int)sizeof F.q - 1, nk_filter_default);
    F.q[F.qn] = 0;
    if (F.qn != was) F.cur = 0;
    if (!F.qn) {
        const char* hint = "search fields, components, formulas, failure, mesh quality";
        float pad = ctx->style.edit.padding.x + ctx->style.edit.border + 2 * s;
        nk_draw_text(nk_window_get_canvas(ctx), nk_rect(eb.x + pad, eb.y, eb.w - pad, eb.h), hint, (int)strlen(hint),
                     ctx->style.font, nk_rgba(0, 0, 0, 0), P.dim);
    }
    bool done = esc, hov = false, foot = false;     /* foot: rows with a tip listed */
    const char* ft = NULL;                          /* the footer: the hovered row's tip, else the cursor's */
    char mk[128], text[160];
    if (F.qn) {                                     /* typed: one list of matches */
        static fhit h[MAXHIT];
        int m = hits(&f, c, nc, h);
        if (up) F.cur--;
        if (down) F.cur++;
        F.cur = wrap(F.cur, m);
        nk_layout_row_dynamic(ctx, CV_MIN(CV_MAX(m, 1), MAXROWS) * rh + gp, 1);
        if (nk_group_begin(ctx, "fpick_hits", m <= MAXROWS ? NK_WINDOW_NO_SCROLLBAR : 0)) {
            nk_layout_row_dynamic(ctx, row, 1);
            if (!m) nk_label_colored(ctx, "nothing found", NK_TEXT_LEFT, P.dim);
            for (int r = 0; r < m; r++) {
                snprintf(text, sizeof text, "%s" SEP "%s", c[h[r].c].title, h[r].r.label);
                snprintf(mk, sizeof mk, "#fpick hit %s %s", c[h[r].c].id, h[r].r.label);
                hov = false;
                if (row_item(ctx, mk, text, r == F.cur, h[r].r.shown, &hov)) { uii_fp_pick(&f, &h[r].r); done = true; }
                if (hov || (!ft && r == F.cur)) ft = h[r].r.tip ? h[r].r.tip : "";
                if (h[r].r.tip) foot = true;
            }
            nk_group_end(ctx);
        }
        if (enter && m && !done) { uii_fp_pick(&f, &h[F.cur].r); done = true; }
    } else {                                        /* entries | the browsed one's rows */
        F.bf = CV_MIN(CV_MAX(F.bf, 0), CV_MAX(nc - 1, 0));
        static uii_fprow r[MAXR];
        int nr = nc ? uii_fp_rows(&f, &c[F.bf], r, MAXR) : 0;
        if (opened) F.cur = first_row(r, nr);
        if (left || right) {
            F.bf = wrap(F.bf + (right ? 1 : -1), nc);
            nr = uii_fp_rows(&f, &c[F.bf], r, MAXR);
            F.cur = first_row(r, nr);
        }
        F.cur = CV_MIN(CV_MAX(F.cur, 0), CV_MAX(nr - 1, 0));
        if (nr && !pickable(&r[F.cur])) F.cur = move(r, nr, F.cur, 1);
        if (up) F.cur = move(r, nr, F.cur, -1);
        if (down) F.cur = move(r, nr, F.cur, 1);
        nk_layout_row_template_begin(ctx, lh);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        int cb = F.bf;
        if (nk_group_begin(ctx, "fpick_fields", most <= MAXROWS ? NK_WINDOW_NO_SCROLLBAR : 0)) {
            nk_layout_row_dynamic(ctx, row, 1);
            for (int k = 0; k < nc; k++) {
                if (k && c[k].kind != FPK_FRD && c[k - 1].kind == FPK_FRD) divider(ctx, s, row);
                snprintf(mk, sizeof mk, "#fpick field %s", c[k].id);
                if (row_item(ctx, mk, c[k].title, k == F.bf, c[k].shown, NULL)) {
                    if (F.bf != k) cb = k;
                    F.focus = 1;                    /* the keys stay with the list */
                }
            }
            nk_group_end(ctx);
        }
        if (nk_group_begin(ctx, "fpick_comps", nr <= MAXROWS ? NK_WINDOW_NO_SCROLLBAR : 0)) {
            nk_layout_row_dynamic(ctx, row, 1);
            for (int j = 0; j < nr; j++) {
                if (!pickable(&r[j])) { heading(ctx, r[j].label); continue; }
                snprintf(mk, sizeof mk, "#fpick comp %s %s", c[F.bf].id, r[j].label);
                hov = false;
                if (row_item(ctx, mk, r[j].label, j == F.cur, r[j].shown, &hov)) { uii_fp_pick(&f, &r[j]); done = true; }
                if (hov) ft = r[j].tip ? r[j].tip : "";
            }
            nk_group_end(ctx);
        }
        foot = nr && c[F.bf].kind >= FPK_CALC;
        if (!ft && foot) ft = r[F.cur].tip ? r[F.cur].tip : "";
        if (enter && nr && !done && pickable(&r[F.cur])) { uii_fp_pick(&f, &r[F.cur]); done = true; }
        if (cb != F.bf) { F.bf = cb; nr = uii_fp_rows(&f, &c[F.bf], r, MAXR); F.cur = first_row(r, nr); }
    }
    if (F.foot) footer(ctx, row, ft);
    F.foot = foot;                                  /* the popup's height follows the next frame */
    if (done) { nk_combo_close(ctx); F.open = false; }
    nk_combo_end(ctx);
}
