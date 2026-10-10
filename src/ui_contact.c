/* ui_contact.c -- the Contact window (Layers > Contact...): the model and its element
   sets see-through, the ties and contact pairs in their slave and master colours with
   the slave nodes tied or not, the contact elements of the .cel by increment or
   iteration, the contact display (links, status, gap, solids, layer: app_cdisp.c, ui_clayer.c); and the key
   of those colours over the view (app_contact.c). */
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
    char lab[300];
    if (!c) nk_label_colored(ctx, "no jobname.cel beside the model (*NODE FILE, CONTACT ELEMENTS)", NK_TEXT_LEFT, P.dim);
    else {
        snprintf(lab, sizeof lab, "%s: %d iterations", cv_basename(app_contact_cel_path()), c->nsets);
        nk_label_colored(ctx, lab, NK_TEXT_LEFT, P.dim);
    }
    nk_layout_row_dynamic(ctx, row, 3);
    tip(ctx, "Draw the contact: the slave nodes as the display below chooses (links, status, gap, solids),\n"
             "the master faces of the contact elements (blue); surface to surface: the slave faces outlined");
    if (nk_checkbox_label(ctx, "show", &G.cel_show)) app_contact_refresh();
    tip(ctx, "The master faces of the contact elements, filled blue");
    if (nk_checkbox_label(ctx, "master faces", &G.cel_master)) app_contact_refresh();
    tip(ctx, "The slave nodes, links and status patches in front of the model, so the contact zone shows\n"
             "inside an assembly, under a part and on an exaggerated shape");
    nk_checkbox_label(ctx, "in front", &G.cel_front);
    if (!c) return;
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

/* a length that is 0 for automatic: its box, and the value in use beside it */
static bool auto_row(struct nk_context* ctx, float s, float row, const char* lab, const char* id, float* v, float used, const char* help) {
    nk_layout_row_template_begin(ctx, row);
    nk_layout_row_template_push_static(ctx, 110 * s);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 110 * s);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, lab, NK_TEXT_LEFT);
    tip(ctx, help);
    float before = *v, step = CV_MAX(used, 1e-9f) * 0.1f;
    nk_property_float(ctx, id, 0.f, v, 1e30f, step, step * 0.1f);
    char t[48], n[24];
    fmt_num(n, sizeof n, used);
    snprintf(t, sizeof t, *v > 0 ? "%s" : "auto %s", n);
    nk_label_colored(ctx, t, NK_TEXT_RIGHT, P.dim);
    return *v != before;
}

static void section_display(struct nk_context* ctx, float s, float row) {
    const cv_cinfo* I = app_cdisp_info();
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label_colored(ctx, "Contact display", NK_TEXT_LEFT, P.accent);
    static const char* const names[5] = { "links", "status", "gap", "solids", "layer" };
    static const char* const help[5] = {
        "Links: each slave node a ball, filled when closed, a ring when open, and a line to its projection\n"
        "on its master face, coloured by the true gap: red into the face, green at 0, blue open.\n"
        "A closed node sits on its face: no line",
        "Status: the slave faces in a patch per node, coloured by its contact status as Ansys and Abaqus\n"
        "show it: far open, near open (within the near distance), sliding, sticking (|CSHEAR| < mu CPRESS),\n"
        "penetrating (deeper than the tolerance)",
        "Gap: the slave faces coloured by the gap (COPEN, else measured): red overclosed, white 0, blue open;\n"
        "the two ends apart, so a small overclosure still reads against a wide gap",
        "Solids: the contact elements of the .cel as CalculiX wrote them, see-through, from the slave node\n"
        "to its master face: the gap is the layer's thickness, coloured as in Gap",
        "Layer: an interface layer drawn like an adhesive between the bodies: over each slave face a solid\n"
        "up to its corners' projections on the master, as thick as the gap there (none where closed),\n"
        "coloured by the gap, CPRESS, CSLIP, CSHEAR or the status. Only a picture, not a model element" };
    nk_layout_row_dynamic(ctx, row, 5);
    for (int i = 0; i < 5; i++) {
        bool on = (G.cel_mode >> i) & 1;
        tip(ctx, help[i]);
        if (nk_checkbox_label(ctx, names[i], &on)) {
            G.cel_mode = on ? G.cel_mode | 1 << i : G.cel_mode & ~(1 << i);
            G.cel_show = true;
            app_contact_refresh();
        }
    }
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "With the shape exaggerated (deformation scale not 1) the screen shows the initial gap plus\n"
             "the scale times the motion: a closed node seems to sink into its face. At true scale the\n"
             "slave nodes and links are drawn from their projection out by the true gap, and the layer\n"
             "is as thick as the true gap");
    if (nk_checkbox_label(ctx, "links, solids, layer at true scale (shape exaggerated)", &G.cel_true)) app_contact_refresh();
    uii_section_clayer(ctx, s, row);
    bool ch = false;
    ch |= auto_row(ctx, s, row, "gap colours to", "#open", &G.cel_gap_max, I->hi,
                   "The open end of the gap colours (blue): 0 for the widest gap shown");
    ch |= auto_row(ctx, s, row, "overclosure to", "#in", &G.cel_pen_max, -I->lo,
                   "The penetration end of the gap colours (red): 0 for the deepest shown, at least the tolerance");
    ch |= auto_row(ctx, s, row, "tolerance", "#tol", &G.cel_tol, I->tol,
                   "Closed within this gap where there is no CPRESS; penetrating deeper than it.\n0 for 0.5 % of the master faces' mean edge");
    ch |= auto_row(ctx, s, row, "near distance", "#near", &G.cel_near, I->near,
                   "Open within this gap: near open (Ansys' near field); beyond it far open.\n0 for 10 % of the master faces' mean edge");
    if (ch) app_contact_refresh();
    nk_layout_row_dynamic(ctx, row, 1);
    char t[160];
    if (I->nodes) {
        snprintf(t, sizeof t, "closed %d / open %d / penetrating %d  (gap: %s)", I->closed, I->open, I->pen,
                 I->copen && I->measured ? "COPEN, else measured" : I->copen ? "COPEN" : I->measured ? "measured" : "-");
        nk_label(ctx, t, NK_TEXT_LEFT);
    } else nk_label_colored(ctx, "no slave nodes: no contact pair in the deck, no .cel", NK_TEXT_LEFT, P.dim);
}

void uii_window_contact(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_contact || !G.loaded) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Contact", NK_SHOWN);
    was_open = true;
    /* as tall as its rows: opacities, pairs (a tie two, and the slave nodes list), warning
       files, the contact elements' six, the key's box */
    int nr = 13 + uii_clayer_rows() + app_contact_nam_count() + (app_contact_cel() ? 6 : 3), ne = 0, ties = 0;
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
        section_display(ctx, s, row);
        uii_hsep(ctx, s);
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "The key of these colours in a corner of the view while any of them is drawn (drag it to move it)");
        nk_checkbox_label(ctx, "key over the view", &G.contact_key);
    } else G.show_contact = false;
    nk_end(ctx);
}

/* ---- the key: a swatch and a line per colour drawn, over the view ------------------ */
static ov_drag key_drag;

/* a gap colour bar across the key: the map, its ends and 0 below */
static void gap_bar(struct nk_command_buffer* cv, const struct nk_user_font* f, float x, float y, float w, float lh,
                    int map, const cv_cinfo* I, struct nk_color ink, float s) {
    int n = 48;
    float bh = lh * 0.42f, bw = w / (float)n;
    for (int i = 0; i < n; i++) {
        float g = I->lo + (I->hi - I->lo) * ((float)i + 0.5f) / (float)n, c[3];
        cv_contact_gap_rgb(map, g, I->lo, I->hi, map == CV_CGAP_LINKS ? I->tol : 0.f, c);
        nk_fill_rect(cv, nk_rect(x + bw * (float)i, y + lh * 0.3f, bw + 1.f, bh), 0, nk_rgb_f(c[0], c[1], c[2]));
    }
    nk_stroke_rect(cv, nk_rect(x, y + lh * 0.3f, w, bh), 0, 1, ink);
    float z = x + w * (-I->lo) / (I->hi - I->lo);
    nk_stroke_line(cv, z, y + lh * 0.18f, z, y + lh * 0.3f + bh + 2 * s, 1.5f * s, ink);
    char a[24], b[24];
    fmt_num(a, sizeof a, I->lo); fmt_num(b, sizeof b, I->hi);
    float ty = y + lh, wa = ink_width(f, a, (int)strlen(a)), wb = ink_width(f, b, (int)strlen(b)), w0 = ink_width(f, "0", 1);
    ink_text(cv, f, x, ty, wa + 4, a, ink);
    ink_text(cv, f, x + w - wb, ty, wb + 4, b, ink);
    float zx = CV_MAX(x + wa + 6 * s, CV_MIN(z - w0 * 0.5f, x + w - wb - w0 - 6 * s));
    ink_text(cv, f, zx, ty, w0 + 4, "0", ink);
}

void uii_window_contact_key(struct nk_context* ctx, float s, float row) {
    int what[CV_KEY_N];
    const cv_cinfo* I = app_cdisp_info();
    bool disp = G.cel_show && I->drawn && I->nodes;
    if (!G.loaded || !G.contact_key || (!app_contact_drawn(what) && !disp) || (!G.show_hl && !G.cel_show)) return;
    enum { SW_FILL, SW_BALL, SW_FRAME, SW_LINE, SW_RING, SW_TEXT, SW_BAR_LINKS, SW_BAR_GAP, SW_BAR_LAYER };
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
        float c[3];
        if (I->drawn & CV_CMODE_LINKS) {
            cv_contact_gap_rgb(CV_CGAP_LINKS, 0, I->lo, I->hi, I->tol, c);
            ROW(SW_BALL, nk_rgb_f(c[0], c[1], c[2]), "closed  %d", I->closed - I->pen);
            if (I->pen) { cv_contact_gap_rgb(CV_CGAP_LINKS, I->lo, I->lo, I->hi, I->tol, c); ROW(SW_BALL, nk_rgb_f(c[0], c[1], c[2]), "penetrating  %d", I->pen); }
            cv_contact_gap_rgb(CV_CGAP_LINKS, I->hi, I->lo, I->hi, I->tol, c);
            ROW(SW_RING, nk_rgb_f(c[0], c[1], c[2]), "open  %d", I->open);
            ROW(SW_TEXT, ink, "gap, line to the master face:");
            ROW(SW_BAR_LINKS, ink, " ");
            ROW(SW_TEXT, ink, " ");
        }
        if (I->drawn & CV_CMODE_STATUS) {
            ROW(SW_TEXT, ink, "status (%d slave nodes):", I->nodes);
            for (int k = 0; k < CV_CST_N; k++)
                if (I->cat[k]) ROW(SW_FILL, nk_rgb_f(cv_cst_rgb[k][0], cv_cst_rgb[k][1], cv_cst_rgb[k][2]), "%s  %d", cv_cst_names[k], I->cat[k]);
        }
        if (I->drawn & (CV_CMODE_GAP | CV_CMODE_SOLIDS)) {
            ROW(SW_TEXT, ink, "%s:", (I->drawn & CV_CMODE_GAP) ? ((I->drawn & CV_CMODE_SOLIDS) ? "gap, faces and solids" : "gap on the slave faces")
                                                               : "gap, contact element solids");
            ROW(SW_BAR_GAP, ink, " ");
            ROW(SW_TEXT, ink, " ");
        }
        if ((I->drawn & CV_CMODE_LAYER) && I->lby >= 0) {
            static const char* const what[CV_CLBY_N] = { "gap", "CPRESS (0 where open)", "|CSLIP|", "|CSHEAR|", "status" };
            if (I->lby == CV_CLBY_STATUS) {
                ROW(SW_TEXT, ink, "layer as thick as the gap, by status:");
                if (!(I->drawn & CV_CMODE_STATUS))
                    for (int k = 0; k < CV_CST_N; k++)
                        if (I->cat[k]) ROW(SW_FILL, nk_rgb_f(cv_cst_rgb[k][0], cv_cst_rgb[k][1], cv_cst_rgb[k][2]), "%s  %d", cv_cst_names[k], I->cat[k]);
            } else if (I->lknown) {
                ROW(SW_TEXT, ink, "layer as thick as the gap, by %s:", what[I->lby]);
                ROW(SW_BAR_LAYER, ink, " ");
                ROW(SW_TEXT, ink, " ");
            } else ROW(SW_TEXT, ink, "layer: no %s in the results (grey)", what[I->lby]);
            if (I->lpen) ROW(SW_FRAME, nk_rgb_f(cv_cst_rgb[CV_CST_PEN][0], cv_cst_rgb[CV_CST_PEN][1], cv_cst_rgb[CV_CST_PEN][2]),
                             "layer face penetrating  %d", I->lpen);
        }
        if (!(I->drawn & CV_CMODE_LINKS)) ROW(SW_TEXT, ink, "closed %d / open %d / penetrating %d", I->closed, I->open, I->pen);
        float sc = G.deform ? G.deform_scale : 0.f;
        if (G.disp && fabsf(sc - 1.f) > 1e-4f && (I->drawn & (CV_CMODE_LINKS | CV_CMODE_SOLIDS | CV_CMODE_LAYER))) {
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
            case SW_LINE:                              /* a dark rim: a white line reads on a white key too */
                nk_stroke_line(cv, x + 1, cy, x + sw - 1, cy, 4 * s, rim);
                nk_stroke_line(cv, x + 2, cy, x + sw - 2, cy, 2 * s, rows[i].c);
                break;
            case SW_RING:
                nk_stroke_circle(cv, nk_rect(cx - rr, cy - rr, 2 * rr, 2 * rr), 3.f * s, rim);
                nk_stroke_circle(cv, nk_rect(cx - rr, cy - rr, 2 * rr, 2 * rr), 2.f * s, rows[i].c);
                break;
            case SW_TEXT: case SW_BAR_LINKS: case SW_BAR_GAP: case SW_BAR_LAYER: break;
            default:
                nk_fill_circle(cv, nk_rect(cx - rr, cy - rr, 2 * rr, 2 * rr), rows[i].c);
                nk_stroke_circle(cv, nk_rect(cx - rr, cy - rr, 2 * rr, 2 * rr), 1, rim);
            }
            if (rows[i].sw == SW_BAR_LAYER) uii_clayer_bar(cv, f, x + 2 * s, y, w - 20 * s, lh, ink, s);
            else if (rows[i].sw == SW_BAR_LINKS || rows[i].sw == SW_BAR_GAP)
                gap_bar(cv, f, x + 2 * s, y, w - 20 * s, lh, rows[i].sw == SW_BAR_LINKS ? CV_CGAP_LINKS : CV_CGAP_CONTOUR, I, ink, s);
            else if (rows[i].sw == SW_TEXT) ink_text(cv, f, x, y + (lh - f->height) * 0.5f, w, rows[i].t, legend_dim());
            else ink_text(cv, f, x + sw + 8 * s, y + (lh - f->height) * 0.5f, tw + 4, rows[i].t, ink);
            y += lh;
        }
    }
    nk_end(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
}
