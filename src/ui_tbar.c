/* ui_tbar.c -- the top bar as a list of ids (app_cmds.h cv_toolbar: custom items of ui_tbar_items.c and
   any registry entry, "-" a gap, "|" a row break). A row that does not fit puts its rest in a "»" list;
   right-click: customize / remove / reset; the Customize toolbar window orders and adds. */
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
#include "icons.h"

enum { MAXT = 96, IDLEN = 48 };
typedef struct { char id[IDLEN]; const tb_custom* cu; const cv_cmd* cmd; float w; int nw; } tok;   /* "-" and "|": neither */

static int split(const char* list, char t[][IDLEN], int max) {
    int n = 0;
    for (const char* p = list; *p && n < max;) {
        const char* e = strchr(p, ',');
        size_t k = e ? (size_t)(e - p) : strlen(p);
        if (k && k < IDLEN) { memcpy(t[n], p, k); t[n][k] = 0; n++; }
        p += k + (e ? 1 : 0);
    }
    return n;
}
static void join(char t[][IDLEN], int n) {
    char out[sizeof G.toolbar_pins] = "";
    size_t o = 0;
    for (int i = 0; i < n; i++) o += (size_t)snprintf(out + o, sizeof out - o, "%s%s", o ? "," : "", t[i]);
    cv_toolbar_set(out);
}
static bool is_sep(const char* id) { return !strcmp(id, "-"); }
static bool is_brk(const char* id) { return !strcmp(id, "|"); }

static const char* name_of(const char* id, char* buf, size_t n) {
    if (is_sep(id)) return "- gap";
    if (is_brk(id)) return "- row break -";
    const tb_custom* cu = uii_tb_find(id, strlen(id));
    if (cu) return cu->label;
    const cv_cmd* c = cv_cmd_find(id);
    if (c) { snprintf(buf, n, "%s", c->label); return buf; }
    return id;
}

/* the bar's items, widths in px; ids that are neither are skipped */
static int parse(struct nk_context* ctx, float s, tok* out, int max) {
    static char t[MAXT][IDLEN];
    int n = split(cv_toolbar(), t, MAXT), m = 0;
    const struct nk_user_font* f = ctx->style.font;
    for (int i = 0; i < n && m < max; i++) {
        tok* k = &out[m];
        memset(k, 0, sizeof *k);
        snprintf(k->id, sizeof k->id, "%s", t[i]);
        if (is_sep(t[i])) { k->w = 24 * s; k->nw = 1; m++; continue; }
        if (is_brk(t[i])) { m++; continue; }
        if ((k->cu = uii_tb_find(t[i], strlen(t[i])))) {
            k->w = (k->cu->wfn ? k->cu->wfn() : k->cu->w) * s;
            k->nw = k->cu->nw;
            m++;
            continue;
        }
        if (!(k->cmd = cv_cmd_find(t[i]))) continue;
        const cv_cmd* c = k->cmd;
        float tw = f->width(f->userdata, f->height, c->label, (int)strlen(c->label));
        k->nw = 1;
        switch (c->kind) {
        case CV_CMD_BOOL: k->w = tw + 30 * s; break;
        case CV_CMD_ENUM:
            for (int j = 0; j < c->n; j++) tw = CV_MAX(tw, f->width(f->userdata, f->height, c->items[j], (int)strlen(c->items[j])));
            k->w = (c->kind == CV_CMD_ENUM ? tw : 0) + 40 * s; break;
        case CV_CMD_INT: case CV_CMD_FLOAT: k->w = CV_MAX(tw + 80 * s, 130 * s); break;
        default: k->w = tw + 26 * s; break;
        }
        m++;
    }
    return m;
}

int uii_tb_rows(void) {
    int n = 1;
    for (const char* p = cv_toolbar(); *p; p++) if (*p == '|') n++;
    return CV_MIN(n, 4);
}

/* a registry entry as a compact widget in a cell of width w */
static void draw_cmd(struct nk_context* ctx, float s, float row, const cv_cmd* c, float w) {
    nk_layout_row_push(ctx, w);
    char nm[80];
    snprintf(nm, sizeof nm, "#%s", c->label);
    switch (c->kind) {
    case CV_CMD_BOOL: {
        nk_bool v = cv_cmd_geti(c) != 0;
        tip(ctx, c->help);
        if (nk_checkbox_label(ctx, c->label, &v)) { cv_cmd_seti(c, v); cv_cmd_used(c->id); }
        break; }
    case CV_CMD_ENUM: {
        int v = cv_cmd_geti(c);
        tip(ctx, c->help);
        int nv = nk_combo(ctx, c->items, c->n, v, (int)row, nk_vec2(w, CV_MIN(c->n, 12) * row + 20 * s));
        if (nv != v) { cv_cmd_seti(c, nv); cv_cmd_used(c->id); }
        break; }
    case CV_CMD_INT: {
        int v = cv_cmd_geti(c), was = v;
        tip(ctx, c->help);
        nk_property_int(ctx, nm, (int)c->lo, &v, (int)c->hi, (int)CV_MAX(c->step, 1.f), CV_MAX(c->step, 1.f));
        if (v != was) { cv_cmd_seti(c, v); cv_cmd_used(c->id); }
        break; }
    case CV_CMD_FLOAT: {
        float v = cv_cmd_getf(c), was = v;
        tip(ctx, c->help);
        nk_property_float(ctx, nm, c->lo, &v, c->hi, c->step, c->step * 0.1f);
        if (v != was) { cv_cmd_setf(c, v); cv_cmd_used(c->id); }
        break; }
    default:
        tip(ctx, c->help);
        if (nk_button_label(ctx, c->label)) { cv_cmd_used(c->id); cv_cmd_run(c); }
    }
}

static void draw_tok(struct nk_context* ctx, float s, float row, const tok* k) {
    if (k->cu) k->cu->draw(ctx, s, row);
    else if (k->cmd) draw_cmd(ctx, s, row, k->cmd, k->w);
    else if (is_sep(k->id)) { nk_layout_row_push(ctx, 24 * s); uii_vsep(ctx); }
}

/* ---- the right-click menu ---------------------------------------------------------------- */
static int menu_item = -1;                       /* the token under the click, in the list of ids */

static void bar_menu(struct nk_context* ctx, float s, float row, int hover) {
    struct nk_rect wb = nk_window_get_bounds(ctx);
    if (nk_input_mouse_clicked(&ctx->input, NK_BUTTON_RIGHT, wb)) menu_item = hover;
    char ids[MAXT][IDLEN], nb[96], lab[128];
    int n = split(cv_toolbar(), ids, MAXT), items = menu_item >= 0 && menu_item < n ? 3 : 2;
    if (!nk_contextual_begin(ctx, 0, nk_vec2(210 * s, items * (row + 2 * s) + 14 * s), wb)) return;
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "Order, add and remove the items of this bar");
    if (nk_contextual_item_label(ctx, "Customize toolbar...", NK_TEXT_LEFT)) G.tbar_edit = true;
    if (menu_item >= 0 && menu_item < n) {
        snprintf(lab, sizeof lab, "Remove %s", name_of(ids[menu_item], nb, sizeof nb));
        tip(ctx, "Take this item off the bar (Customize toolbar... puts it back)");
        if (nk_contextual_item_label(ctx, lab, NK_TEXT_LEFT)) {
            for (int i = menu_item; i + 1 < n; i++) memcpy(ids[i], ids[i + 1], IDLEN);
            join(ids, n - 1);
            menu_item = -1;
        }
    }
    tip(ctx, "The bar as it ships");
    if (nk_contextual_item_label(ctx, "Reset toolbar", NK_TEXT_LEFT)) cv_toolbar_set(CV_TB_DEFAULT);
    nk_contextual_end(ctx);
}

/* ---- one row of items; what does not fit goes into "»" ---------------------------------------- */
static void bar_row(struct nk_context* ctx, float s, float row, tok* k, int n, int base, float y, int* hover) {
    const float sp = ctx->style.window.spacing.x;
    struct nk_panel* lay = ctx->current->layout;
    const float x0 = lay->bounds.x, avail = lay->bounds.w;
    const tok* pane = NULL;
    int vis[MAXT], nv = 0;                                   /* the shown items of the row */
    for (int i = 0; i < n; i++) {
        if (!strcmp(k[i].id, "tb:pane1")) { if (k[i].w > 0) pane = &k[i]; continue; }
        if (k[i].w > 0 || is_sep(k[i].id)) vis[nv++] = i;
    }
    float reserve = pane ? pane->w + sp : 0, more = 44 * s + sp;
    int fit = 0;
    float used = 0;
    while (fit < nv && used + k[vis[fit]].w + k[vis[fit]].nw * sp + reserve <= avail) { used += k[vis[fit]].w + k[vis[fit]].nw * sp; fit++; }
    if (fit < nv) {                                          /* room for the "»" button too */
        while (fit > 0 && used + more + reserve > avail) { fit--; used -= k[vis[fit]].w + k[vis[fit]].nw * sp; }
        while (fit > 0 && is_sep(k[vis[fit - 1]].id)) fit--;
    }
    nk_layout_row_begin(ctx, NK_STATIC, row, 40);
    float x = x0;
    for (int i = 0; i < fit; i++) {
        const tok* t = &k[vis[i]];
        struct nk_rect r = nk_rect(x, y, t->w + t->nw * sp, row);
        if (nk_input_is_mouse_hovering_rect(&ctx->input, r)) *hover = base + vis[i];
        draw_tok(ctx, s, row, t);
        x += t->w + t->nw * sp;
    }
    if (fit < nv) {
        nk_layout_row_push(ctx, 44 * s);
        tip(ctx, "More: the items that do not fit in this window");
        float wmax = 120 * s;
        int cnt = 0;
        for (int i = fit; i < nv; i++) if (!is_sep(k[vis[i]].id)) { wmax = CV_MAX(wmax, k[vis[i]].w); cnt++; }
        if (nk_combo_begin_label(ctx, "\xC2\xBB", nk_vec2(wmax + 36 * s, cnt * (row + ctx->style.window.spacing.y) + 16 * s))) {
            for (int i = fit; i < nv; i++) {
                const tok* t = &k[vis[i]];
                if (is_sep(t->id)) continue;
                nk_layout_row_begin(ctx, NK_STATIC, row, 12);
                draw_tok(ctx, s, row, t);
                nk_layout_row_end(ctx);
            }
            nk_combo_end(ctx);
        }
        x += more;
    }
    if (pane) {                                              /* the Inspector's button at the right end */
        nk_layout_row_push(ctx, 1);
        float px = nk_widget_bounds(ctx).x;
        nk_layout_row_push(ctx, CV_MAX(x0 + avail - px - pane->w - sp, 1));
        nk_spacing(ctx, 1);
        draw_tok(ctx, s, row, pane);
    }
    nk_layout_row_end(ctx);
}

void panel_toolbar(struct nk_context* ctx, float s, float row) {
    tok k[MAXT];
    int n = parse(ctx, s, k, MAXT), hover = -1, base = 0, rows = 0;
    float y = ctx->current->layout->at_y;
    bool any = false;
    for (int i = 0; i < n; i++) any |= k[i].cu || k[i].cmd;
    if (!any) {                                     /* every item removed: a way back, not an empty bar for good */
        nk_layout_row_static(ctx, row, (int)(150 * s), 1);
        tip(ctx, "The bar is empty: add items (right-click the bar: Reset toolbar for the shipped one)");
        uii_test_mark(ctx, "#customize toolbar");
        if (nk_button_label(ctx, IC_SETTINGS "  Customize")) G.tbar_edit = true;
        bar_menu(ctx, s, row, -1);
        return;
    }
    for (int i = 0, a = 0; i <= n; i++) {
        if (i < n && !is_brk(k[i].id)) continue;
        if (rows < 4) bar_row(ctx, s, row, k + a, i - a, base + a, y + rows * (row + ctx->style.window.spacing.y), &hover);
        rows++;
        a = i + 1;
    }
    /* hover is an index into the parsed tokens; the menu wants one into the id list (they differ only by skipped unknown ids) */
    if (hover >= 0) {
        char ids[MAXT][IDLEN];
        int m = split(cv_toolbar(), ids, MAXT), j = 0;
        for (int i = 0; i < m && j <= hover; i++)
            if (is_sep(ids[i]) || is_brk(ids[i]) || uii_tb_find(ids[i], strlen(ids[i])) || cv_cmd_find(ids[i])) { if (j++ == hover) { hover = i; break; } }
    }
    bar_menu(ctx, s, row, hover);
}

/* ---- the Customize toolbar window --------------------------------------------------------------- */
static void swap(char a[IDLEN], char b[IDLEN]) { char t[IDLEN]; memcpy(t, a, IDLEN); memcpy(a, b, IDLEN); memcpy(b, t, IDLEN); }

void window_tbar_edit(struct nk_context* ctx, float s, float row) {
    static bool was_open;
    static char q[64];
    if (!G.tbar_edit || !G.loaded) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Customize toolbar", NK_SHOWN);
    was_open = true;
    float w = 400 * s, h = 560 * s;
    if (nk_begin(ctx, "Customize toolbar", nk_rect(G.vp_x + 40 * s, G.vp_y + 40 * s, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        static const char* const lab[3] = { "Add gap", "Add row break", "Reset to default" };
        static const char* const tp[3] = { "A vertical line between items, at the end of the bar", "Start another row of the bar after the last item",
                                           "The bar as it ships" };
        char ids[MAXT][IDLEN], nb[96];
        int n = split(cv_toolbar(), ids, MAXT), act = -1;
        uik_cols(ctx, 1);
        nk_label_colored(ctx, "Add an item (all settings and actions can be on the bar)", NK_TEXT_LEFT, P.dim);
        uik_cols(ctx, 1);
        tip(ctx, "Type to find a feature; click it to put it at the end of the bar");
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, q, (int)sizeof q, nk_filter_default);
        {
            static const char* label[600], *kw[600], *help[600];
            static int src[600];
            int m = 0, nc = uii_tb_count();
            for (int i = 0; i < nc && m < 600; i++) { label[m] = uii_tb_at(i)->label; kw[m] = ""; help[m] = ""; src[m++] = -1 - i; }
            for (int i = 0; i < cv_cmd_count() && m < 600; i++) { const cv_cmd* c = cv_cmd_at(i); label[m] = c->label; kw[m] = c->keywords; help[m] = c->help; src[m++] = i; }
            int o[8], got = q[0] ? cv_fs_rank(q, m, label, kw, help, o, 8) : 0;
            for (int i = 0; i < got; i++) {
                const char* id = src[o[i]] < 0 ? uii_tb_at(-1 - src[o[i]])->id : cv_cmd_at(src[o[i]])->id;
                char path[160], line[200];
                path[0] = 0;
                if (src[o[i]] >= 0) cv_cmd_path(cv_cmd_at(src[o[i]]), path, sizeof path);
                snprintf(line, sizeof line, "%s%s%s", label[o[i]], path[0] ? "   " : "", path);
                uik_cols(ctx, 1);
                tip(ctx, cv_cmd_pinned(id) ? "Already on the bar" : "Put it at the end of the bar");
                if (nk_button_label(ctx, line) && !cv_cmd_pinned(id)) { cv_cmd_pin(id, true); q[0] = 0; }
            }
        }
        uik_cols(ctx, 3);
        for (int i = 0; i < 3; i++) {
            tip(ctx, tp[i]);
            if (nk_button_label(ctx, lab[i])) {
                if (i == 2) cv_toolbar_set(CV_TB_DEFAULT);
                else if (n < MAXT - 1) { snprintf(ids[n], IDLEN, "%s", i == 0 ? "-" : "|"); join(ids, n + 1); }
            }
        }
        uik_cols(ctx, 1);
        nk_label_colored(ctx, "The bar, in order", NK_TEXT_LEFT, P.dim);
        n = split(cv_toolbar(), ids, MAXT);
        for (int i = 0; i < n && act < 0; i++) {
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_dynamic(ctx);
            for (int b = 0; b < 3; b++) nk_layout_row_template_push_static(ctx, 28 * s);
            nk_layout_row_template_end(ctx);
            nk_label(ctx, name_of(ids[i], nb, sizeof nb), NK_TEXT_LEFT);
            tip(ctx, "Move up");
            if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_UP) && i > 0) { swap(ids[i], ids[i - 1]); act = 0; }
            tip(ctx, "Move down");
            if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_DOWN) && i + 1 < n) { swap(ids[i], ids[i + 1]); act = 0; }
            tip(ctx, "Remove from the bar");
            if (nk_button_label(ctx, "x")) { for (int j = i; j + 1 < n; j++) memcpy(ids[j], ids[j + 1], IDLEN); n--; act = 0; }
        }
        if (act >= 0) join(ids, n);
    }
    if (nk_window_is_hidden(ctx, "Customize toolbar")) G.tbar_edit = false;
    nk_end(ctx);
}
