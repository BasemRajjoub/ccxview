/* ui_search.c -- the search box at the top of the left panel (Ctrl+K): finds any registered feature
   (app_cmds.c) by label, keywords or help (fsearch.h) and shows its live control in the panel's place,
   with its location, a pin for the top bar, and a jump to where the panel has it. Empty and focused:
   the last features used. Pins and recent live in G (the ini). */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"
#include "app_cmds.h"
#include "fsearch.h"

enum { MAXRES = 12, NREC = 8 };
static char query[64];
static int  qlen;
static bool want_focus, want_unfocus, box_active, recent_open, esc;
static struct { int sec, sub; double t0; bool scroll; } J = { -1, -1, -1e9, false };   /* the jump: whose header to scroll to and flash */

void ui_focus_search(void) { want_focus = true; recent_open = true; G.scene_on = true; app_wake(); }
void ui_search_escape(void) { esc = true; }
void ui_search_set(const char* q) { snprintf(query, sizeof query, "%s", q ? q : ""); qlen = (int)strlen(query); recent_open = !qlen; }   /* "": the recent list */
void ui_search_blur(void) { want_unfocus = true; recent_open = false; }
void ui_search_clear(void) { qlen = 0; query[0] = 0; recent_open = false; want_unfocus = true; J.sec = J.sub = -1; }

/* before the frame's windows: Ctrl+K from another window (the legend active) makes the Scene the active one, or its box would be read only */
void uii_search_prefocus(struct nk_context* ctx) {
    struct nk_window* w = want_focus ? nk_window_find(ctx, "Scene") : NULL;
    if (!w) return;
    nk_window_set_focus(ctx, "Scene");
    w->flags &= ~(nk_flags)NK_WINDOW_ROM;
}

/* what the query finds, best first */
static int rank(int* order, int max) {
    static const char* label[512], *kw[512], *help[512];
    int n = CV_MIN(cv_cmd_count(), 512);
    for (int i = 0; i < n; i++) { const cv_cmd* c = cv_cmd_at(i); label[i] = c->label; kw[i] = c->keywords; help[i] = c->help; }
    return cv_fs_rank(query, n, label, kw, help, order, max);
}
bool ui_search_focused(void) { return box_active; }
const char* ui_search_first(void) {
    int o[1];
    if (!qlen || rank(o, 1) < 1) return NULL;
    return cv_cmd_at(o[0])->id;
}

/* ---- the jump ------------------------------------------------------------------------- */
void ui_search_jump(const cv_cmd* c) {
    cv_cmd_used(c->id);
    ui_search_clear();
    G.scene_on = true;
    int tab = cv_cmd_tab(c);
    if (tab >= 0) G.panel_tab = tab;                     /* its tab first */
    else if (!strcmp(c->section, "Settings")) G.show_settings = true;
    if (c->tree < 0 && (tab >= 0 || !strcmp(c->section, "Settings"))) return;
    if (c->tree < 0) {                                   /* not in the left panel: say where */
        snprintf(G.note, sizeof G.note, "%s: %s", c->label, c->section);
        G.note_t = cv_now();
        return;
    }
    G.tree[c->tree] = 1;
    if (c->tree == CV_TREE_VIEW || c->tree == CV_TREE_RENDER || c->tree == CV_TREE_EXPORT) return;   /* no header: the tab shows it */
    if (c->sub >= 0) G.tree[c->sub] = 1;
    if (c->sub == CV_TREE_CONTACT) snprintf(G.fields_open, sizeof G.fields_open, "CONTACT");   /* its own scroll and opening */
    J.sec = c->tree; J.sub = c->sub; J.t0 = cv_now(); J.scroll = true;
    app_wake();
}

/* a section or sub-section header about to be drawn: scroll to it once; after it, a tint that fades in a second */
void uii_jump_scroll(struct nk_context* ctx, int tree) {
    if (J.scroll && tree == (J.sub >= 0 ? J.sub : J.sec)) { uii_scroll_here(ctx); J.scroll = false; }
}
void uii_jump_flash(struct nk_context* ctx, int tree, struct nk_rect b) {
    if (tree != (J.sub >= 0 ? J.sub : J.sec)) return;
    double dt = cv_now() - J.t0;
    if (dt > 1.0) { J.sec = J.sub = -1; return; }
    struct nk_color c = P.accent;
    c.a = (nk_byte)(110.0 * (1.0 - dt));
    nk_fill_rect(nk_window_get_canvas(ctx), b, 3.f, c);
    app_wake();
}

/* ---- one result: its control with the pin at the row's end, the location under it as a link ------ */
static void result(struct nk_context* ctx, const cv_cmd* c) {
    nk_bool pin = cv_cmd_pinned(c->id);
    uik_trail(ctx, 36);
    switch (c->kind) {
    case CV_CMD_BOOL: {
        nk_bool v = cv_cmd_geti(c) != 0;
        if (uik_check(ctx, c->label, &v, c->help)) { cv_cmd_seti(c, v); cv_cmd_used(c->id); }
        break; }
    case CV_CMD_ENUM: {
        int v = cv_cmd_geti(c);
        if (uik_combo(ctx, c->label, c->items, c->n, &v, c->help)) { cv_cmd_seti(c, v); cv_cmd_used(c->id); }
        break; }
    case CV_CMD_INT: {
        int v = cv_cmd_geti(c);
        if (uik_int(ctx, c->label, &v, (int)c->lo, (int)c->hi, (int)CV_MAX(c->step, 1.f), c->help)) { cv_cmd_seti(c, v); cv_cmd_used(c->id); }
        break; }
    case CV_CMD_FLOAT: {
        float v = cv_cmd_getf(c);
        bool ch = c->hi - c->lo <= 1000.f ? uik_float(ctx, c->label, &v, c->lo, c->hi, c->step, c->help)
                                          : uik_num(ctx, c->label, &v, c->lo, c->hi, c->step, c->help);
        if (ch) { cv_cmd_setf(c, v); cv_cmd_used(c->id); }
        break; }
    default: {
        const char* l = c->label, *t = c->help;
        if (uik_buttons(ctx, NULL, 1, &l, &t) == 0) { cv_cmd_used(c->id); cv_cmd_run(c); }
        break; }
    }
    if (uik_trail_toggle(ctx, &pin, "pin", "Pin to the top bar")) cv_cmd_pin(c->id, pin);
    char path[160];
    cv_cmd_path(c, path, sizeof path);
    if (uik_link(ctx, path, "Go to this in the panel")) ui_search_jump(c);
}

/* the box and, while it has a query or the recent list is open, the results in the panel's place;
   true when they were drawn (the sections then stay out) */
bool uii_search_panel(struct nk_context* ctx, float s, float row) {
    if (want_unfocus) { nk_edit_unfocus(ctx); want_unfocus = false; }
    if (esc) {
        esc = false;
        if (qlen) { qlen = 0; query[0] = 0; } else recent_open = false;
        if (box_active) nk_edit_unfocus(ctx);
    }
    if (recent_open && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT) && ctx->current) {
        const struct nk_rect w = ctx->current->bounds;                 /* a press elsewhere (the view) closes the list */
        if (!NK_INBOX(ctx->input.mouse.pos.x, ctx->input.mouse.pos.y, w.x, w.y, w.w, w.h)) recent_open = false;
    }
    uik_cols(ctx, 1);
    if (want_focus) { nk_edit_focus(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER); want_focus = false; }
    struct nk_rect b = nk_widget_bounds(ctx);
    tip(ctx, G.ui_palette ? "Search every setting and action (Ctrl+K: the command palette). Enter: go to the first result, or run it"
                          : "Search every setting and action (Ctrl+K). Enter: go to the first result, or run it");
    int was = qlen;
    nk_flags ev = nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, query, &qlen, (int)sizeof query - 1, nk_filter_default);
    query[qlen] = 0;
    box_active = (ev & NK_EDIT_ACTIVE) != 0;
    if (box_active && !qlen && !was) recent_open = true;
    if (!qlen) {
        const char* hint = G.ui_palette ? "Search" : "Search  (Ctrl+K)";
        const struct nk_user_font* f = ctx->style.font;
        float pad = ctx->style.edit.padding.x + ctx->style.edit.border + 2 * s;
        nk_draw_text(nk_window_get_canvas(ctx), nk_rect(b.x + pad, b.y, b.w - pad, b.h), hint, (int)strlen(hint), f, nk_rgba(0, 0, 0, 0), P.dim);
    }
    uii_scene_tabs(ctx, row);
    if (!qlen && !recent_open) return false;

    const cv_cmd* list[MAXRES];
    int n = 0;
    if (qlen) {
        int o[MAXRES];
        n = rank(o, MAXRES);
        for (int i = 0; i < n; i++) list[i] = cv_cmd_at(o[i]);
    } else {
        n = cv_cmd_recent(list, NREC);
        uik_group(ctx, "Recent");
    }
    if ((ev & NK_EDIT_COMMITED) && qlen && n > 0) {                    /* Enter: the first one */
        if (list[0]->kind == CV_CMD_ACTION) { cv_cmd_used(list[0]->id); ui_search_clear(); cv_cmd_run(list[0]); }
        else ui_search_jump(list[0]);
        return false;
    }
    if (!n) uik_note(ctx, qlen ? "nothing found" : "nothing used yet: type to search");
    for (int i = 0; i < n; i++) {
        result(ctx, list[i]);
        if (i + 1 < n) uii_hsep(ctx, s);
    }
    return true;
}
