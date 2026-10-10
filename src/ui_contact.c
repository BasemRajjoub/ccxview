/* ui_contact.c -- the Contact subgroup of Fields: contact drawn or not, the ties and
   contact pairs ticked, how the slave nodes are drawn (the interface layer, links, the
   .cel's elements: app_cdisp.c, app_clayer.c), what colours them, which nodes show,
   the increment of the .cel and the opacity. Its "more..." button opens the Contact
   settings window with what is set once (true scale, the colours' ends, tolerance and
   near distance, the layer's outline and least thickness, the key, the model's and the
   element sets' opacity). And the key of those colours over the view (app_contact.c). */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "contact.h"
#include <math.h>

static struct nk_color key_col(int k) { return nk_rgb_f(cv_key_rgb[k][0], cv_key_rgb[k][1], cv_key_rgb[k][2]); }

/* a row: a label in the lead column, then the widgets after it (right: a fixed last column) */
static void lead(struct nk_context* ctx, float s, float row, const char* lab, const char* help, float right) {
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 66 * s);
    nk_layout_row_template_push_dynamic(ctx);
    if (right > 0) nk_layout_row_template_push_static(ctx, right);
    nk_layout_row_template_end(ctx);
    if (help) tip(ctx, help);
    nk_label(ctx, lab, NK_TEXT_LEFT);
}

/* a label (in a column "wide" wide), a slider and its value; true when it moved */
static bool alpha_row(struct nk_context* ctx, float s, float row, float wide, const char* lab, float lo, float* a, const char* help) {
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, wide * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 34 * s);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, lab, NK_TEXT_LEFT);
    tip(ctx, help);
    bool ch = ui_slider_float(ctx, lo, a, 1.f, 0.05f);
    char v[16];
    snprintf(v, sizeof v, "%.2f", *a);
    nk_label(ctx, v, NK_TEXT_RIGHT);
    return ch;
}

/* a length that is 0 for automatic: its box, and the value in use beside it */
static bool auto_row(struct nk_context* ctx, float s, float row, const char* lab, float* v, float used, const char* help) {
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 88 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 78 * s);
    nk_layout_row_template_end(ctx);
    tip(ctx, help);
    nk_label(ctx, lab, NK_TEXT_LEFT);
    tip(ctx, help);
    float before = *v, step = CV_MAX(fabsf(used), 1e-9f) * 0.1f;
    nk_property_float(ctx, "#", 0.f, v, 1e30f, step, step * 0.1f);
    char t[48], n[24];
    fmt_num(n, sizeof n, used);
    snprintf(t, sizeof t, *v > 0 ? "%s" : "auto %s", n);
    nk_label_colored(ctx, t, NK_TEXT_RIGHT, P.dim);
    return *v != before;
}

/* "step 1, increment 3, iteration 2" (attempt when not the first) */
static void set_name(const cv_celset* t, char* out, size_t n) {
    if (!t->step) { snprintf(out, n, "%s  (%u)", t->name[0] ? t->name : "no name", t->n); return; }
    if (t->att > 1) snprintf(out, n, "step %d, inc %d, attempt %d, it %d  (%u)", t->step, t->inc, t->att, t->it, t->n);
    else snprintf(out, n, "step %d, increment %d, iteration %d  (%u)", t->step, t->inc, t->it, t->n);
}

static bool is_pair(const cv_link* l) { return l->kind == CV_LINK_TIE || l->kind == CV_LINK_CONTACT; }

bool uii_contact_present(void) {
    if (!G.loaded) return false;
    const cv_inp* d = deck_get();
    for (int i = 0; d && i < d->nlinks; i++) if (is_pair(&d->links[i])) return true;
    for (int f = 0; G.frd.n_steps && f < G.frd.steps[G.step].nfields; f++)
        if (!strcmp(G.frd.steps[G.step].fields[f].name, "CONTACT")) return true;
    return app_contact_cel() != NULL;
}

/* the ties and contact pairs, a tick each: drawn or not */
static void rows_pairs(struct nk_context* ctx, float s, float row) {
    const cv_inp* d = deck_get();
    bool* lon = deck_link_flags();
    int k = 0;
    for (int i = 0; d && lon && i < d->nlinks; i++) {
        const cv_link* l = &d->links[i];
        if (!is_pair(l)) continue;
        const char* a = l->surf[0] >= 0 ? d->surfs[l->surf[0]].name : "?";
        const char* b = l->surf[1] >= 0 ? d->surfs[l->surf[1]].name : "?";
        bool tie = l->kind == CV_LINK_TIE;
        char lab[160], help[400];
        uint32_t ns = 0, nf = 0;
        if (tie) app_contact_pair_nodes(i, &ns, &nf);
        if (l->name[0]) snprintf(lab, sizeof lab, "%s%s", l->name, tie ? "  (tie)" : "");
        else snprintf(lab, sizeof lab, "%s %d", tie ? "tie" : "contact", k + 1);
        if (nf) snprintf(lab + strlen(lab), sizeof lab - strlen(lab), ", %u not tied", nf);
        if (tie) snprintf(help, sizeof help, "Tie %s: slave %s (crimson), master %s (blue), %u slave nodes: green where\n"
                          "CalculiX tied them, yellow where it could not (%u)", l->name, a, b, ns, nf);
        else snprintf(help, sizeof help, "Contact pair %s: slave %s, master %s. Ticked: its slave nodes drawn\n"
                      "by the display below (its surfaces in their colours: more)", l->name, a, b);
        lead(ctx, s, row, k ? "" : "Pairs", k ? NULL : "Ties and contact pairs of the deck: tick to draw one", 0);
        tip(ctx, help);
        if (nk_checkbox_label(ctx, lab, &lon[i])) { if (tie) G.show_hl = true; deck_refresh_highlight(); }
        k++;
    }
}

/* how the slave nodes are drawn, one way */
static void row_draw(struct nk_context* ctx, float s, float row) {
    static const char* const help[CV_CDRAW_N] = {
        "Layer: an interface layer like an adhesive between the bodies: over each slave face a solid up to\n"
        "its corners' projections on the master, as thick as the gap there (none where closed).\n"
        "Only a picture, not a model element",
        "Links: a line from each open slave node to its projection on its master face; a closed node sits\n"
        "on its face, no line. With the nodes ticked: a filled ball when closed, a ring when open",
        "ccx elements: the elements as CalculiX wrote them: one slave node paired with one master face,\n"
        "hence pyramids (from the .cel beside the model)" };
    static const char* const names[CV_CDRAW_N] = { "layer", "links", "ccx elements" };
    int n = app_contact_cel() ? 3 : 2;
    /* each choice as wide as its words; the last on a row of its own when they do not fit */
    const struct nk_user_font* f = ctx->style.font;
    float w[CV_CDRAW_N], sp = ctx->style.window.spacing.x, sum = 0;
    for (int i = 0; i < n; i++) sum += w[i] = ink_width(f, names[i], (int)strlen(names[i])) + f->height + 4 * sp;
    bool wrap = 66 * s + sum + n * sp > ctx->current->layout->bounds.w;
    for (int i = 0; i < n; i++) {
        if (i == 0 || (wrap && i == n - 1)) {
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 66 * s);
            for (int k = i; k < (wrap && i == 0 ? n - 1 : n); k++) nk_layout_row_template_push_static(ctx, w[k]);
            nk_layout_row_template_end(ctx);
            if (i == 0) tip(ctx, "How the slave nodes are drawn against their master faces: one way");
            nk_label(ctx, i == 0 ? "Draw" : "", NK_TEXT_LEFT);
        }
        tip(ctx, help[i]);
        if (nk_option_label(ctx, names[i], G.cel_draw == i) && G.cel_draw != i) { G.cel_draw = i; G.cel_show = true; app_contact_refresh(); }
    }
}

/* what colours it: the CONTACT field shown on the model follows */
static void row_by(struct nk_context* ctx, float s, float row) {
    static const char* const names[CV_CBY_N] = { "gap", "pressure (CPRESS)", "slip |CSLIP|", "shear |CSHEAR|", "status" };
    lead(ctx, s, row, "Colour by", NULL, 0);
    tip(ctx, "What colours the drawing (and the balls): the gap (COPEN, else measured: red overclosed, green 0,\n"
             "blue open), CPRESS (0 where open), |CSLIP|, |CSHEAR|, or the status as Ansys and Abaqus show it.\n"
             "The same as picking that component of CONTACT above, which colours the model");
    int by = G.cel_by >= 0 && G.cel_by < CV_CBY_N ? G.cel_by : 0;
    int nb = nk_combo(ctx, names, CV_CBY_N, by, (int)row, nk_vec2(CV_MAX(nk_widget_width(ctx), 170 * s), CV_CBY_N * (row + 4 * s) + 20 * s));
    if (nb == by) return;
    G.cel_by = nb;
    int c = G.field_src == 0 && !strcmp(G.field_name, "CONTACT") ? app_cdisp_by_comp(nb) : -100;
    if (c != -100) app_select("CONTACT", c);             /* the model on CONTACT: the same component */
    app_contact_refresh();
}


/* which slave nodes show as balls; a tie's tied and untied */
static void rows_nodes(struct nk_context* ctx, float s, float row, bool ties) {
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 66 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    tip(ctx, "The slave nodes as balls on top, in the colours of the value");
    nk_label(ctx, "Nodes", NK_TEXT_LEFT);
    bool ch = false;
    tip(ctx, "The closed slave nodes as filled balls");
    ch |= nk_checkbox_label(ctx, "closed", &G.cel_closed);
    tip(ctx, "The open slave nodes as rings");
    ch |= nk_checkbox_label(ctx, "open", &G.cel_open);
    if (ties) {                                   /* tie_nodes: 0 both, 1 tied only, 2 untied only, 3 none */
        bool tied = G.tie_nodes == 0 || G.tie_nodes == 1, free = G.tie_nodes == 0 || G.tie_nodes == 2;
        nk_label(ctx, "", NK_TEXT_LEFT);
        tip(ctx, "A tie's slave nodes CalculiX tied, green");
        bool t1 = nk_checkbox_label(ctx, "tied", &tied);
        tip(ctx, "A tie's slave nodes CalculiX could not tie, yellow (listed in\n"
                 "jobname_WarnNodeMissTiedContact.nam beside the model)");
        bool t2 = nk_checkbox_label(ctx, "not tied", &free);
        if (t1 || t2) { G.tie_nodes = tied ? (free ? 0 : 1) : (free ? 2 : 3); ch = true; }
    }
    if (ch) app_contact_refresh();
}

/* the .cel's increment or iteration, and the drawing's opacity */
static void rows_cel_alpha(struct nk_context* ctx, float s, float row) {
    const cv_cel* c = app_contact_cel();
    if (c) {
        lead(ctx, s, row, "Increment", NULL, 0);
        char cur[160] = "on screen", lab[160];
        if (G.cel_pick >= 0 && G.cel_pick < c->nsets) set_name(&c->sets[G.cel_pick], cur, sizeof cur);
        tip(ctx, "Which contact elements of the .cel: those of the increment on screen (the last iteration of its\n"
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
    }
    if (G.cel_draw == CV_CDRAW_LAYER)
        alpha_row(ctx, s, row, 66, "Opacity", 0.05f, &G.cel_alpha, "The interface layer's opacity: less to see its far side and the faces inside it");
    else if (G.cel_draw == CV_CDRAW_CCX)
        alpha_row(ctx, s, row, 66, "Opacity", 0.05f, &G.cel_ccx_alpha, "The contact elements' opacity: they are drawn see-through over the model");
}

/* the Contact settings window: true scale, the colours' ends, the classification, the
   layer, the key, opacities, what there is */
static void rows_more(struct nk_context* ctx, float s, float row, bool pairs) {
    const cv_cinfo* I = app_cdisp_info();
    bool ch = false;
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "With the shape exaggerated (deformation scale not 1) the screen shows the initial gap plus\n"
             "the scale times the motion: a closed node seems to sink into its face. At true scale the\n"
             "slave nodes and links are drawn from their projection out by the true gap, and the layer\n"
             "is as thick as the true gap");
    ch |= nk_checkbox_label(ctx, "gap at true scale (shape exaggerated)", &G.cel_true);
    if (G.cel_by == CV_CBY_GAP) {
        ch |= auto_row(ctx, s, row, "gap colours to", &G.cel_gap_max, I->hi,
                       "The open end of the gap colours (blue): 0 for the widest gap shown");
        ch |= auto_row(ctx, s, row, "overclosure to", &G.cel_pen_max, -I->lo,
                       "The penetration end of the gap colours (red): 0 for the deepest shown, at least the tolerance");
    } else if (G.cel_by != CV_CBY_STATUS) {
        ch |= auto_row(ctx, s, row, "colours from", &G.cel_lo, I->llo, "The low end of the colours (blue): 0 for 0");
        ch |= auto_row(ctx, s, row, "colours to", &G.cel_hi, I->lhi, "The high end of the colours (red): 0 for the largest shown");
    }
    ch |= auto_row(ctx, s, row, "tolerance", &G.cel_tol, I->tol,
                   "Closed within this gap where there is no CPRESS; penetrating deeper than it.\n0 for 0.5 % of the master faces' mean edge");
    ch |= auto_row(ctx, s, row, "near distance", &G.cel_near, I->near,
                   "Open within this gap: near open (Ansys' near field); beyond it far open.\n0 for 10 % of the master faces' mean edge");
    if (G.cel_draw == CV_CDRAW_LAYER) {          /* a length that is 0 for none */
        const char* help = "The layer at least this thick (model units), so a closed contact still shows a thin\n"
                           "coloured skin, grown towards the master; 0: as thick as the gap, nothing where closed";
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 88 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 78 * s);
        nk_layout_row_template_end(ctx);
        tip(ctx, help);
        nk_label(ctx, "least thickness", NK_TEXT_LEFT);
        tip(ctx, help);
        float before = G.cel_min, step = CV_MAX(I->near, 1e-9f) * 0.1f;
        nk_property_float(ctx, "#", 0.f, &G.cel_min, 1e30f, step, step * 0.1f);
        nk_label_colored(ctx, G.cel_min > 0 ? "" : "none", NK_TEXT_RIGHT, P.dim);
        ch |= G.cel_min != before;
    }
    nk_layout_row_dynamic(ctx, row, 2);
    if (G.cel_draw == CV_CDRAW_LAYER) {
        tip(ctx, "The interface layer's edges outlined (a face with a corner penetrating its master is always\n"
                 "outlined, in the penetrating colour)");
        ch |= nk_checkbox_label(ctx, "layer outlined", &G.cel_edges);
    }
    tip(ctx, "The slave nodes and links in front of the model, so the contact zone shows inside an\n"
             "assembly, under a part and on an exaggerated shape");
    nk_checkbox_label(ctx, "in front", &G.cel_front);
    if (app_contact_cel()) {
        tip(ctx, "The master faces of the .cel's contact elements, filled blue");
        ch |= nk_checkbox_label(ctx, "master faces", &G.cel_master);
    }
    if (pairs) {
        tip(ctx, "The ticked contact pairs' surfaces: the slave crimson, the master blue");
        ch |= nk_checkbox_label(ctx, "pair surfaces", &G.cel_surfs);
    }
    tip(ctx, "The key of these colours in a corner of the view while any of them is drawn (drag it to move it)");
    nk_checkbox_label(ctx, "key over the view", &G.contact_key);
    if (ch) app_contact_refresh();
    /* see-through: the model, each element set */
    if (alpha_row(ctx, s, row, 120, "model opacity", 0.f, &G.model_alpha,
                  "The model's opacity: less to see into it (a contact zone, a tied face);\n"
                  "what lies behind and the model's far side show through, dimmed"))
        app_see_refresh();
    const cv_inp* d = deck_get();
    float* sa = deck_set_alpha();
    int ne = 0;
    for (int i = 0; d && sa && i < d->nsets; i++) ne += d->sets[i].is_elem;
    for (int i = 0; ne && ne <= 40 && i < d->nsets; i++) {
        if (!d->sets[i].is_elem) continue;
        char help[160];
        snprintf(help, sizeof help, "Element set %s's opacity, times the model's", d->sets[i].name);
        uii_test_mark(ctx, "#set opacity");
        if (alpha_row(ctx, s, row, 120, d->sets[i].name, 0.f, &sa[i], help)) app_see_refresh();
    }
    /* what there is */
    nk_layout_row_dynamic(ctx, row, 1);
    char t[200];
    const cv_cel* c = app_contact_cel();
    if (c) {
        snprintf(t, sizeof t, "%s: %d iterations", cv_basename(app_contact_cel_path()), c->nsets);
        nk_label_colored(ctx, t, NK_TEXT_LEFT, P.dim);
        snprintf(t, sizeof t, "%u contact elements drawn", app_contact_cel_count());
        nk_label_colored(ctx, t, NK_TEXT_LEFT, P.dim);
    }
    for (int i = 0; i < app_contact_nam_count(); i++) {
        uint32_t n; bool miss;
        const char* nm = app_contact_nam(i, &n, &miss);
        snprintf(t, sizeof t, "%s: %u node%s%s", nm, n, n == 1 ? "" : "s", miss ? " not tied" : "");
        tip(ctx, t);
        nk_label_colored(ctx, t, NK_TEXT_LEFT, miss ? P.warn : P.dim);
    }
    if (I->nodes) {
        snprintf(t, sizeof t, "closed %d, open %d, penetrating %d", I->closed, I->open, I->pen);
        nk_label_colored(ctx, t, NK_TEXT_LEFT, P.dim);
        snprintf(t, sizeof t, "gap: %s", I->copen && I->measured ? "COPEN, else measured" : I->copen ? "COPEN" : I->measured ? "measured" : "-");
        nk_label_colored(ctx, t, NK_TEXT_LEFT, P.dim);
    } else nk_label_colored(ctx, "no slave nodes: no pair ticked, no .cel", NK_TEXT_LEFT, P.dim);
}

void uii_section_contact(struct nk_context* ctx, float s, float row) {
    if (!uii_contact_present()) return;
    if (!strcmp(G.fields_open, "CONTACT")) { G.tree[CV_TREE_CONTACT] = 1; uii_scroll_here(ctx); G.fields_open[0] = 0; }
    const cv_inp* d = deck_get();
    bool pairs = false, ties = false;
    for (int i = 0; d && i < d->nlinks; i++) { pairs |= d->links[i].kind == CV_LINK_CONTACT; ties |= d->links[i].kind == CV_LINK_TIE; }
    bool was = G.cel_show;
    bool open = uii_node_check(ctx, "Contact", &G.tree[CV_TREE_CONTACT], &G.cel_show, "show",
                               "Draw the contact: the slave nodes against their master faces as chosen below,\n"
                               "the ties' surfaces and nodes, the .cel's master faces; off: none of it");
    if (G.cel_show != was) app_contact_refresh();
    if (!open) return;
    rows_pairs(ctx, s, row);
    row_draw(ctx, s, row);
    row_by(ctx, s, row);
    rows_nodes(ctx, s, row, ties);
    rows_cel_alpha(ctx, s, row);
    (void)pairs;
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 100 * s);
    nk_layout_row_template_end(ctx);
    nk_spacing(ctx, 1);
    tip(ctx, "Contact settings: true scale, the colours' ends, tolerance and near distance, the layer's\n"
             "outline and least thickness, the key over the view, the model's and element sets' opacity");
    if (nk_button_label(ctx, "more...")) G.show_contact = !G.show_contact;
    uii_node_pop(ctx);
}

void uii_window_contact(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    (void)fw;
    if (!G.show_contact || !uii_contact_present()) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Contact settings", NK_SHOWN);
    was_open = true;
    const cv_inp* d = deck_get();
    bool pairs = false;
    int ne = 0;
    for (int i = 0; d && i < d->nlinks; i++) pairs |= d->links[i].kind == CV_LINK_CONTACT;
    for (int i = 0; d && i < d->nsets; i++) ne += d->sets[i].is_elem;
    int nr = 17 + (ne <= 40 ? ne : 0) + app_contact_nam_count() + (app_contact_cel() ? 2 : 0);
    float w = CV_MIN(420 * s, G.vp_w * 0.6f), h = CV_MIN(nr * (row + ctx->style.window.spacing.y) + 60 * s, fh * 0.85f);
    if (nk_begin(ctx, "Contact settings", nk_rect(G.vp_x + 10 * s, G.vp_y + 10 * s, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER))
        rows_more(ctx, s, row, pairs);
    else G.show_contact = false;
    nk_end(ctx);
}

/* ---- the key: a swatch and a line per colour drawn, over the view ------------------ */
static ov_drag key_drag;

/* the colour bar of the value the display is coloured by: the map, its ends, 0 on the gap's */
static void value_bar(struct nk_command_buffer* cv, const struct nk_user_font* f, float x, float y, float w, float lh,
                      const cv_cinfo* I, struct nk_color ink, float s) {
    int n = 48;
    float bh = lh * 0.42f, bw = w / (float)n;
    for (int i = 0; i < n; i++) {
        float v = I->llo + (I->lhi - I->llo) * ((float)i + 0.5f) / (float)n, c[3];
        if (I->by == CV_CBY_GAP) cv_contact_gap_rgb(CV_CGAP_LINKS, v, I->llo, I->lhi, I->tol, c);
        else app_cby_map((v - I->llo) / CV_MAX(I->lhi - I->llo, 1e-30f), c);
        nk_fill_rect(cv, nk_rect(x + bw * (float)i, y + lh * 0.3f, bw + 1.f, bh), 0, nk_rgb_f(c[0], c[1], c[2]));
    }
    nk_stroke_rect(cv, nk_rect(x, y + lh * 0.3f, w, bh), 0, 1, ink);
    char a[24], b[24];
    fmt_num(a, sizeof a, I->llo); fmt_num(b, sizeof b, I->lhi);
    float ty = y + lh, wa = ink_width(f, a, (int)strlen(a)), wb = ink_width(f, b, (int)strlen(b));
    ink_text(cv, f, x, ty, wa + 4, a, ink);
    ink_text(cv, f, x + w - wb, ty, wb + 4, b, ink);
    if (I->by == CV_CBY_GAP && I->llo < 0 && I->lhi > 0) {
        float z = x + w * (-I->llo) / (I->lhi - I->llo), w0 = ink_width(f, "0", 1);
        nk_stroke_line(cv, z, y + lh * 0.18f, z, y + lh * 0.3f + bh + 2 * s, 1.5f * s, ink);
        float zx = CV_MAX(x + wa + 6 * s, CV_MIN(z - w0 * 0.5f, x + w - wb - w0 - 6 * s));
        ink_text(cv, f, zx, ty, w0 + 4, "0", ink);
    }
}

void uii_window_contact_key(struct nk_context* ctx, float s, float row) {
    int what[CV_KEY_N];
    const cv_cinfo* I = app_cdisp_info();
    bool disp = G.cel_show && (I->draw >= 0 || I->balls) && I->nodes;
    if (!G.loaded || !G.contact_key || !G.cel_show || (!app_contact_drawn(what) && !disp)) return;
    enum { SW_FILL, SW_BALL, SW_FRAME, SW_RING, SW_TEXT, SW_BAR };
    struct { int sw; struct nk_color c; char t[96]; } rows[CV_KEY_N + 40];
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
    if (what[CV_KEY_CSLAVE] && app_contact_s2s()) ROW(SW_FRAME, key_col(CV_KEY_CSLAVE), "slave face of a contact element");
    struct nk_color ink = legend_ink();
    if (disp) {
        static const char* const style[CV_CDRAW_N] = { "layer as thick as the gap", "links to the master face", "ccx elements" };
        static const char* const value[CV_CBY_N] = { "gap", "CPRESS (0 where open)", "|CSLIP|", "|CSHEAR|", "status" };
        ROW(SW_TEXT, ink, "%s, by %s:", I->draw >= 0 ? style[I->draw] : "slave nodes", value[I->by]);
        if (I->by == CV_CBY_STATUS) {
            for (int k = 0; k < CV_CST_N; k++)
                if (I->cat[k]) ROW(SW_FILL, nk_rgb_f(cv_cst_rgb[k][0], cv_cst_rgb[k][1], cv_cst_rgb[k][2]), "%s  %d", cv_cst_names[k], I->cat[k]);
        } else if (I->lknown) {
            ROW(SW_BAR, ink, " ");
            ROW(SW_TEXT, ink, " ");
        } else ROW(SW_TEXT, ink, "  no %s in the results (grey)", value[I->by]);
        if (I->balls) {
            if (G.cel_closed) ROW(SW_BALL, ink, "closed node  %d", I->closed);
            if (G.cel_open) ROW(SW_RING, ink, "open node  %d", I->open);
        }
        if (I->draw == CV_CDRAW_LAYER && I->lpen)
            ROW(SW_FRAME, nk_rgb_f(cv_cst_rgb[CV_CST_PEN][0], cv_cst_rgb[CV_CST_PEN][1], cv_cst_rgb[CV_CST_PEN][2]), "layer face penetrating  %d", I->lpen);
        ROW(SW_TEXT, ink, "closed %d / open %d / penetrating %d", I->closed, I->open, I->pen);
        float sc = G.deform ? G.deform_scale : 0.f;
        if (G.disp && fabsf(sc - 1.f) > 1e-4f && I->draw >= 0) {
            if (G.cel_true) ROW(SW_TEXT, ink, "gap drawn at true scale (shape x%.3g)", sc);
            else ROW(SW_TEXT, ink, "gap on screen x%.3g, colours true scale", sc);
        }
        ROW(SW_TEXT, ink, "gap: %s", I->copen && I->measured ? "COPEN, else measured" : I->copen ? "COPEN" : I->measured ? "measured from the shape" : "-");
    }
    #undef ROW
    const struct nk_user_font* f = ctx->style.font;
    char head[160] = "Contact";
    const cv_cel* c = app_contact_cel();
    int ks = app_contact_cel_set();
    if (c && ks >= 0 && c->sets[ks].step)
        snprintf(head, sizeof head, "Contact  inc %d it %d", c->sets[ks].inc, c->sets[ks].it);
    float tw = ink_width(f, head, (int)strlen(head));
    float lh = row * 0.95f, sw = row * 1.1f;
    for (int i = 0; i < n; i++) {
        float wi = ink_width(f, rows[i].t, (int)strlen(rows[i].t));
        tw = CV_MAX(tw, rows[i].sw == SW_TEXT ? wi - sw - 8 * s : wi);
    }
    tw = CV_MAX(tw, 150 * s);
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
        struct nk_color rim = nk_rgb(30, 30, 30);
        ink = legend_ink();
        ink_text(cv, f, x, y + (lh - f->height) * 0.5f, w, head, ink);
        y += lh;
        for (int i = 0; i < n; i++) {
            float cy = y + lh * 0.5f, cx = x + sw * 0.5f, rr = lh * 0.28f;
            struct nk_rect q = nk_rect(x + 1, cy - lh * 0.3f, sw - 2, lh * 0.6f);
            switch (rows[i].sw) {
            case SW_FILL: nk_fill_rect(cv, q, 0, rows[i].c); nk_stroke_rect(cv, q, 0, 1, ink); break;
            case SW_FRAME: nk_stroke_rect(cv, nk_rect(q.x + 2 * s, q.y, q.w - 4 * s, q.h), 0, 2 * s, rows[i].c); break;
            case SW_RING:
                nk_stroke_circle(cv, nk_rect(cx - rr, cy - rr, 2 * rr, 2 * rr), 3.f * s, rim);
                nk_stroke_circle(cv, nk_rect(cx - rr, cy - rr, 2 * rr, 2 * rr), 2.f * s, rows[i].c);
                break;
            case SW_TEXT: case SW_BAR: break;
            default:
                nk_fill_circle(cv, nk_rect(cx - rr, cy - rr, 2 * rr, 2 * rr), rows[i].c);
                nk_stroke_circle(cv, nk_rect(cx - rr, cy - rr, 2 * rr, 2 * rr), 1, rim);
            }
            if (rows[i].sw == SW_BAR) value_bar(cv, f, x + 2 * s, y, w - 20 * s, lh, I, ink, s);
            else if (rows[i].sw == SW_TEXT) ink_text(cv, f, x, y + (lh - f->height) * 0.5f, w, rows[i].t, legend_dim());
            else ink_text(cv, f, x + sw + 8 * s, y + (lh - f->height) * 0.5f, tw + 4, rows[i].t, ink);
            y += lh;
        }
    }
    nk_end(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
}
