/* ui_contact.c -- the Contact window (Layers > Contact...): the model and its element
   sets see-through, the ties and contact pairs in their slave and master colours with
   the slave nodes tied or not, the contact elements of the .cel by increment or
   iteration; and the key of those colours over the view (app_contact.c). */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"

static struct nk_color key_col(int k) { return nk_rgb_f(cv_key_rgb[k][0], cv_key_rgb[k][1], cv_key_rgb[k][2]); }

/* a label, a slider 0..1 and its value; true when it moved */
static bool alpha_row(struct nk_context* ctx, float s, float row, const char* lab, float* a, const char* help) {
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 110 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 36 * s);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, lab, NK_TEXT_LEFT);
    tip(ctx, help);
    bool ch = ui_slider_float(ctx, 0.f, a, 1.f, 0.05f);
    char v[16];
    snprintf(v, sizeof v, "%.2f", *a);
    nk_label(ctx, v, NK_TEXT_RIGHT);
    return ch;
}

/* "step 1, increment 3, iteration 2" (attempt when not the first) */
static void set_name(const cv_celset* t, char* out, size_t n) {
    if (!t->step) { snprintf(out, n, "%s  (%u)", t->name[0] ? t->name : "no name", t->n); return; }
    if (t->att > 1) snprintf(out, n, "step %d, inc %d, attempt %d, it %d  (%u)", t->step, t->inc, t->att, t->it, t->n);
    else snprintf(out, n, "step %d, increment %d, iteration %d  (%u)", t->step, t->inc, t->it, t->n);
}

static void section_see(struct nk_context* ctx, float s, float row) {
    if (alpha_row(ctx, s, row, "model opacity", &G.model_alpha,
                  "The faces' opacity: less to see into the model (a contact zone, a tied face);\n"
                  "what lies behind and the model's far side show through, dimmed"))
        app_see_refresh();
    const cv_inp* d = deck_get();
    float* sa = deck_set_alpha();
    int ne = 0;
    for (int i = 0; d && sa && i < d->nsets; i++) if (d->sets[i].is_elem) ne++;
    if (!ne || ne > 40) return;
    for (int i = 0; i < d->nsets; i++) {
        if (!d->sets[i].is_elem) continue;
        char lab[80], help[160];
        snprintf(lab, sizeof lab, "%s", d->sets[i].name);
        snprintf(help, sizeof help, "Element set %s's opacity, times the model's", d->sets[i].name);
        uii_test_mark(ctx, "#set opacity");
        if (alpha_row(ctx, s, row, lab, &sa[i], help)) app_see_refresh();
    }
}

static void section_pairs(struct nk_context* ctx, float s, float row) {
    const cv_inp* d = deck_get();
    bool* lon = deck_link_flags();
    int np = 0;
    for (int i = 0; d && lon && i < d->nlinks; i++) if (d->links[i].kind == CV_LINK_TIE || d->links[i].kind == CV_LINK_CONTACT) np++;
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label_colored(ctx, "Ties and contact pairs", NK_TEXT_LEFT, P.accent);
    if (!np) {
        nk_label_colored(ctx, d ? "none in the deck" : "no deck beside the results", NK_TEXT_LEFT, P.dim);
        return;
    }
    bool any_tie = false;
    for (int i = 0; i < d->nlinks; i++) {
        const cv_link* l = &d->links[i];
        if (l->kind != CV_LINK_TIE && l->kind != CV_LINK_CONTACT) continue;
        any_tie |= l->kind == CV_LINK_TIE;
        const char* a = l->surf[0] >= 0 ? d->surfs[l->surf[0]].name : "?";
        const char* b = l->surf[1] >= 0 ? d->surfs[l->surf[1]].name : "?";
        char lab[200], sub[160];
        snprintf(lab, sizeof lab, "%s %s: %s / %s", l->kind == CV_LINK_TIE ? "tie" : "contact", l->name, a, b);
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "Draw this pair: the slave surface (first) crimson, the master (second) blue;\n"
                 "a tie's slave nodes green where CalculiX tied them, yellow where it could not");
        if (nk_checkbox_label(ctx, lab, &lon[i])) { G.show_hl = true; deck_refresh_highlight(); }
        if (l->kind == CV_LINK_TIE) {
            uint32_t ns, nf;
            app_contact_pair_nodes(i, &ns, &nf);
            if (nf) snprintf(sub, sizeof sub, "    %u slave nodes, %u not tied", ns, nf);
            else {
                bool any_miss = false;
                for (int q = 0; q < app_contact_nam_count(); q++) { bool m; app_contact_nam(q, NULL, &m); any_miss |= m; }
                snprintf(sub, sizeof sub, "    %u slave nodes%s", ns, any_miss ? ", all tied" : "");
            }
            nk_label_colored(ctx, sub, NK_TEXT_LEFT, nf ? P.warn : P.dim);
        }
    }
    if (any_tie) {
        static const char* modes[4] = { "tied and not tied", "tied only", "not tied only", "none" };
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 110 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        nk_label(ctx, "slave nodes", NK_TEXT_LEFT);
        tip(ctx, "A tie's slave nodes: those CalculiX tied (green), those it could not tie (yellow,\n"
                 "listed in jobname_WarnNodeMissTiedContact.nam beside the model), both or neither");
        int m = nk_combo(ctx, modes, 4, G.tie_nodes, (int)row, nk_vec2(200 * s, 4 * row + 20 * s));
        if (m != G.tie_nodes) { G.tie_nodes = m; app_contact_refresh(); }
    }
    for (int i = 0; i < app_contact_nam_count(); i++) {
        uint32_t n; bool miss;
        const char* nm = app_contact_nam(i, &n, &miss);
        char lab[160];
        snprintf(lab, sizeof lab, "%s: %u node%s%s", nm, n, n == 1 ? "" : "s", miss ? " not tied" : "");
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, lab, NK_TEXT_LEFT, miss ? P.warn : P.dim);
    }
}

static void section_cel(struct nk_context* ctx, float s, float row) {
    const cv_cel* c = app_contact_cel();
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label_colored(ctx, "Contact elements", NK_TEXT_LEFT, P.accent);
    if (!c) {
        nk_label_colored(ctx, "no jobname.cel beside the model (*NODE FILE, CONTACT ELEMENTS)", NK_TEXT_LEFT, P.dim);
        return;
    }
    char lab[300];
    snprintf(lab, sizeof lab, "%s: %d iterations", cv_basename(app_contact_cel_path()), c->nsets);
    nk_label_colored(ctx, lab, NK_TEXT_LEFT, P.dim);
    nk_layout_row_dynamic(ctx, row, 3);
    tip(ctx, "Draw the contact elements: the slave node (orange), the master face it is paired with (blue),\n"
             "a line from the node to the face's centre (white); surface to surface: the slave face outlined");
    if (nk_checkbox_label(ctx, "show", &G.cel_show)) app_contact_refresh();
    tip(ctx, "The master faces of the contact elements, filled blue");
    if (nk_checkbox_label(ctx, "master faces", &G.cel_master)) app_contact_refresh();
    tip(ctx, "A line from each slave node (face) to the centre of its master face: the pairing");
    if (nk_checkbox_label(ctx, "lines", &G.cel_links)) app_contact_refresh();
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "The slave nodes and the lines in front of the model, so the contact zone shows inside an\n"
             "assembly and on an exaggerated shape (where a slave node may sink into its master face)");
    nk_checkbox_label(ctx, "slave nodes and lines in front of the model", &G.cel_front);
    /* which: the increment on screen (its last iteration), or any iteration of the file */
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 70 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, "iteration", NK_TEXT_LEFT);
    char cur[160] = "the increment on screen";
    if (G.cel_pick >= 0 && G.cel_pick < c->nsets) set_name(&c->sets[G.cel_pick], cur, sizeof cur);
    tip(ctx, "Which contact elements: those of the increment on screen (the last iteration of its\n"
             "last attempt; the time bar changes it), or those of one iteration CalculiX made");
    if (nk_combo_begin_label(ctx, cur, nk_vec2(CV_MAX(nk_widget_width(ctx), 300 * s), CV_MIN(16, c->nsets + 1) * (row + 4 * s) + 20 * s))) {
        nk_layout_row_dynamic(ctx, row, 1);
        if (nk_combo_item_label(ctx, "the increment on screen", NK_TEXT_LEFT)) { G.cel_pick = -1; app_contact_refresh(); }
        for (int i = 0; i < c->nsets; i++) {
            set_name(&c->sets[i], lab, sizeof lab);
            if (nk_combo_item_label(ctx, lab, NK_TEXT_LEFT)) { G.cel_pick = i; app_contact_refresh(); }
        }
        nk_combo_end(ctx);
    }
    int k = app_contact_cel_set();
    nk_layout_row_dynamic(ctx, row, 1);
    if (k >= 0) {
        char nm[160];
        set_name(&c->sets[k], nm, sizeof nm);
        char* paren = strstr(nm, "  (");
        if (paren) *paren = 0;
        snprintf(lab, sizeof lab, "%s: %u contact element%s", nm, app_contact_cel_count(), app_contact_cel_count() == 1 ? "" : "s");
        nk_label(ctx, lab, NK_TEXT_LEFT);
    } else nk_label_colored(ctx, "none in the increment on screen", NK_TEXT_LEFT, P.dim);
}

void uii_window_contact(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_contact || !G.loaded) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Contact", NK_SHOWN);
    was_open = true;
    /* as tall as its rows: opacities, pairs (a tie two, and the slave nodes list), warning
       files, the contact elements' six, the key's box */
    int nr = 6 + app_contact_nam_count() + (app_contact_cel() ? 6 : 1), ne = 0, ties = 0;
    const cv_inp* d = deck_get();
    for (int i = 0; d && i < d->nsets; i++) ne += d->sets[i].is_elem;
    for (int i = 0; d && i < d->nlinks; i++) { nr += d->links[i].kind == CV_LINK_TIE ? 2 : d->links[i].kind == CV_LINK_CONTACT; ties += d->links[i].kind == CV_LINK_TIE; }
    nr += (ne <= 40 ? ne : 0) + (ties > 0);
    float w = CV_MIN(470 * s, G.vp_w * 0.6f), h = CV_MIN(nr * (row + ctx->style.window.spacing.y) + 70 * s, G.vp_h * 0.85f);
    if (nk_begin(ctx, "Contact", nk_rect(G.vp_x + 10 * s, G.vp_y + 10 * s, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        section_see(ctx, s, row);
        uii_hsep(ctx, s);
        section_pairs(ctx, s, row);
        uii_hsep(ctx, s);
        section_cel(ctx, s, row);
        uii_hsep(ctx, s);
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "The key of these colours in a corner of the view while any of them is drawn (drag it to move it)");
        nk_checkbox_label(ctx, "key over the view", &G.contact_key);
    } else G.show_contact = false;
    nk_end(ctx);
}

/* ---- the key: a swatch and a line per colour drawn, over the view ------------------ */
static ov_drag key_drag;

void uii_window_contact_key(struct nk_context* ctx, float s, float row) {
    int what[CV_KEY_N];
    if (!G.loaded || !G.contact_key || !app_contact_drawn(what) || (!G.show_hl && !G.cel_show)) return;
    enum { SW_FILL, SW_BALL, SW_FRAME, SW_LINE };
    struct { int sw; struct nk_color c; char t[96]; } rows[CV_KEY_N + 1];
    int n = 0;
    #define ROW(kind, col, ...) do { rows[n].sw = kind; rows[n].c = col; snprintf(rows[n].t, sizeof rows[n].t, __VA_ARGS__); n++; } while (0)
    if (what[CV_KEY_MASTER]) ROW(SW_FILL, key_col(CV_KEY_MASTER), "master surface");
    if (what[CV_KEY_SLAVE]) ROW(SW_FILL, key_col(CV_KEY_SLAVE), "slave surface");
    if (what[CV_KEY_SLAVE] && what[CV_KEY_MASTER]) {        /* the slave half see-through over its master */
        const float* a = cv_key_rgb[CV_KEY_SLAVE]; const float* b = cv_key_rgb[CV_KEY_MASTER];
        ROW(SW_FILL, nk_rgb_f(0.62f * a[0] + 0.38f * b[0], 0.62f * a[1] + 0.38f * b[1], 0.62f * a[2] + 0.38f * b[2]), "slave on master");
    }
    if (what[CV_KEY_TIED]) ROW(SW_BALL, key_col(CV_KEY_TIED), "tied slave node  %d", what[CV_KEY_TIED]);
    if (what[CV_KEY_FREE]) ROW(SW_BALL, key_col(CV_KEY_FREE), "slave node not tied  %d", what[CV_KEY_FREE]);
    if (what[CV_KEY_CSLAVE])
        ROW(app_contact_s2s() ? SW_FRAME : SW_BALL, key_col(CV_KEY_CSLAVE), "%s  %u",
            app_contact_s2s() ? "slave face in contact" : "slave node in contact", app_contact_cel_count());
    if (what[CV_KEY_LINK]) ROW(SW_LINE, key_col(CV_KEY_LINK), "slave to master face");
    #undef ROW
    const struct nk_user_font* f = ctx->style.font;
    char head[160] = "Contact";
    const cv_cel* c = app_contact_cel();
    int ks = app_contact_cel_set();
    if (c && ks >= 0 && (what[CV_KEY_CSLAVE] || what[CV_KEY_LINK]) && c->sets[ks].step)
        snprintf(head, sizeof head, "Contact  inc %d it %d", c->sets[ks].inc, c->sets[ks].it);
    float tw = ink_width(f, head, (int)strlen(head));
    for (int i = 0; i < n; i++) tw = CV_MAX(tw, ink_width(f, rows[i].t, (int)strlen(rows[i].t)));
    float lh = row * 0.95f, sw = row * 1.1f;
    float w = tw + sw + 26 * s, h = (n + 1) * lh + 14 * s;
    cv_anchor a = G.ckey_pos.set ? G.ckey_pos : (cv_anchor){ true, CV_BL, 10, 112 };   /* above the axes gizmo */
    overlay_drag(ctx, &key_drag, "Contact key", cv_anchor_place(a, view_box(), w, h, s), 0, &G.ckey_pos, s);
    if (G.ckey_pos.set) a = G.ckey_pos;
    cv_box b = cv_anchor_place(a, view_box(), w, h, s);
    struct nk_rect r = nk_rect(b.x, b.y, b.w, b.h);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(G.legend_box ? nk_rgba(255, 255, 255, 235) : uii_bg(215)));
    nk_style_push_float(ctx, &ctx->style.window.border, G.legend_box ? 1.f : 0.f);
    if (begin_background(ctx, "Contact key", r, NK_WINDOW_NO_SCROLLBAR | (G.legend_box ? NK_WINDOW_BORDER : 0))) {
        nk_window_set_bounds(ctx, "Contact key", r);
        struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
        nk_push_scissor(cv, r);
        float x = r.x + 8 * s, y = r.y + 6 * s;
        struct nk_color ink = legend_ink(), rim = nk_rgb(30, 30, 30);
        ink_text(cv, f, x, y + (lh - f->height) * 0.5f, w, head, ink);
        y += lh;
        for (int i = 0; i < n; i++) {
            float cy = y + lh * 0.5f, cx = x + sw * 0.5f, rr = lh * 0.28f;
            struct nk_rect q = nk_rect(x + 1, cy - lh * 0.3f, sw - 2, lh * 0.6f);
            switch (rows[i].sw) {
            case SW_FILL: nk_fill_rect(cv, q, 0, rows[i].c); nk_stroke_rect(cv, q, 0, 1, ink); break;
            case SW_FRAME: nk_stroke_rect(cv, nk_rect(q.x + 2 * s, q.y, q.w - 4 * s, q.h), 0, 2 * s, rows[i].c); break;
            case SW_LINE:                              /* a dark rim: a white line reads on a white key too */
                nk_stroke_line(cv, x + 1, cy, x + sw - 1, cy, 4 * s, rim);
                nk_stroke_line(cv, x + 2, cy, x + sw - 2, cy, 2 * s, rows[i].c);
                break;
            default:
                nk_fill_circle(cv, nk_rect(cx - rr, cy - rr, 2 * rr, 2 * rr), rows[i].c);
                nk_stroke_circle(cv, nk_rect(cx - rr, cy - rr, 2 * rr, 2 * rr), 1, rim);
            }
            ink_text(cv, f, x + sw + 8 * s, y + (lh - f->height) * 0.5f, tw + 4, rows[i].t, ink);
            y += lh;
        }
    }
    nk_end(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
}
