/* ui_fail.c -- the failure field in the Fields panel, and the Strength materials
   window: the user's strength data (from templates or typed in), which deck
   material uses which, and what each criterion still lacks. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "app_fail.h"

/* an edit waits for the mouse to come up: a dragged property would evaluate the
   whole model every frame */
static bool dirty;

static void apply_edits(struct nk_context* ctx) {
    if (!dirty || ctx->input.mouse.buttons[NK_BUTTON_LEFT].down) return;
    dirty = false;
    fail_changed();
    if (G.field_src == 3) app_fail_set(G.fail_crit, G.fail_out);
}

/* ---- the Fields panel: criterion, what to show, the window ---------------------------- */

void section_failure(struct nk_context* ctx, float s, float row) {
    bool active = G.field_src == 3;
    if (!nk_tree_push_id(ctx, NK_TREE_NODE, "Failure", active ? NK_MAXIMIZED : NK_MINIMIZED, 400)) return;
    const char* names[CV_FC_N];
    for (int c = 0; c < CV_FC_N; c++) names[c] = cv_fc_title(c);
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "The criterion, from the .frd STRESS in each element's material axes.\n"
             "Auto: LaRC05 for UD plies, von Mises for metals and plastics, Mohr-Coulomb\n"
             "for brittle ones. Any other: the materials of the other kind by their auto one");
    int c = nk_combo(ctx, names, CV_FC_N, G.fail_crit, (int)row, nk_vec2(260 * s, CV_FC_N * (row + 4 * s) + 20 * s));
    if (c != G.fail_crit) app_fail_set(c, G.fail_out);
    for (int o = 0; o < CV_FO_N; o++) {
        bool sel = active && G.fail_out == o;
        tip(ctx, fail_out_tip(o));
        if (nk_option_label(ctx, fail_out_name(o), sel) && !sel) app_fail_set(G.fail_crit, o);
    }
    const char* why = G.has_field ? G.legend_lines[2] : G.legend_lines[1];   /* "(...)": not shown, or not all */
    if (active && why[0] == '(') {
        nk_layout_row_dynamic(ctx, 2 * row, 1);
        nk_label_colored_wrap(ctx, why, P.warn);
        nk_layout_row_dynamic(ctx, row, 1);
    }
    tip(ctx, "Strength data (from templates of well documented materials, or your own),\n"
             "which deck material uses which; kept in the settings file");
    if (nk_button_label(ctx, "Strength materials...")) G.show_fail = !G.show_fail;
    nk_tree_pop(ctx);
}

/* ---- the window ---------------------------------------------------------------------------- */

static const char* unit_name(int u) {
    switch (u) {
    case CV_FU_MPA: return "MPa";
    case CV_FU_DEG: return "deg";
    case CV_FU_NMM: return "N/mm";
    }
    return "";
}

/* a deck material's elastic constants into a strength material */
static bool elastic_from_deck(cv_fmat* m, const cv_inp* d, int k) {
    if (!d || k < 0 || k >= d->nmats || !d->mprop) return false;
    const cv_matprop* p = &d->mprop[k];
    if (p->el == CV_EL_ENG && m->kind == CV_MK_UD) {
        m->E1 = p->c[0]; m->E2 = p->c[1]; m->nu12 = p->c[3]; m->G12 = p->c[6];
        return true;
    }
    if (p->el == CV_EL_ISO) {
        m->E1 = p->c[0]; m->nu12 = p->c[1];
        if (m->kind == CV_MK_ISO && p->sy > 0 && !(m->Sy > 0)) m->Sy = p->sy;
        return true;
    }
    return false;
}

/* the strength material a deck material uses: a combo. Deck material k with none
   assigned shows the template it takes on its own; picking that copies it into the
   list, to edit. */
static void assign_row(struct nk_context* ctx, float s, float row, const cv_inp* d, int k, const char* deck,
                       const char* label, const char* what) {
    const char* cur = fail_assigned(deck);
    cv_fmat am;
    char how[64], au[80] = "";
    bool has_auto = !cur && !fail_assigned("*") && fail_auto(d, k, &am, how, sizeof how);
    if (has_auto) snprintf(au, sizeof au, "auto: %s", how);
    tip(ctx, has_auto ? "None assigned: the nearest built-in template, by the material's name or\n"
                        "else its *ELASTIC data, with the deck's E, nu and *PLASTIC yield" : what);
    nk_label(ctx, label, NK_TEXT_LEFT);
    int n = fail_count();
    if (nk_combo_begin_label(ctx, cur ? cur : has_auto ? au : "(none)", nk_vec2(280 * s, (CV_MIN(n, 14) + 2) * (row + 4 * s) + 20 * s))) {
        nk_layout_row_dynamic(ctx, row, 1);
        if (nk_combo_item_label(ctx, has_auto ? "(none: auto)" : "(none)", NK_TEXT_LEFT)) { fail_assign(deck, NULL); dirty = true; }
        if (has_auto) {
            char t[96];
            snprintf(t, sizeof t, "%s: copy to edit", am.name);
            if (nk_combo_item_label(ctx, t, NK_TEXT_LEFT)) {
                int i = fail_add(&am);
                if (i >= 0) fail_assign(deck, fail_mat(i)->name);
                dirty = true;
            }
        }
        for (int i = 0; i < n; i++)
            if (nk_combo_item_label(ctx, fail_mat(i)->name, NK_TEXT_LEFT)) { fail_assign(deck, fail_mat(i)->name); dirty = true; }
        nk_combo_end(ctx);
    }
}

void window_failure(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    static int sel = -1;
    static char name[48];
    static int name_len, name_for = -2;
    if (!G.show_fail) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Strength materials", NK_SHOWN);
    was_open = true;
    float w = CV_MIN(720 * s, fw * 0.92f), h = CV_MIN(fh * 0.88f, 900 * s);
    if (nk_begin(ctx, "Strength materials", nk_rect((fw - w) / 2, (fh - h) / 2, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        const cv_inp* d = deck_get();
        char t[200];
        if (sel >= fail_count()) sel = fail_count() - 1;

        /* which deck material uses which */
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "Assignment", NK_TEXT_LEFT, P.accent);
        nk_layout_row_dynamic(ctx, row, 2);
        if (d && d->nmats) {
            for (int k = 0; k < d->nmats; k++) {
                const cv_matprop* p = d->mprop ? &d->mprop[k] : NULL;
                snprintf(t, sizeof t, "%s%s", d->mats[k], p && p->el == CV_EL_ENG ? "  (engineering constants)"
                                                         : p && p->el == CV_EL_ISO ? "  (isotropic)" : "");
                assign_row(ctx, s, row, d, k, d->mats[k], t, "The deck's *MATERIAL; its strength data from the list below");
            }
            assign_row(ctx, s, row, d, -1, "*", "any other element", "Elements whose deck material has none assigned\n"
                                                                     "(set: used instead of the auto templates)");
        } else {
            assign_row(ctx, s, row, NULL, -1, "*", "every element", "No deck (.inp) beside the results: one material for all,\n"
                                                                    "its fibre direction global X; none set: S235 assumed");
        }
        uii_hsep(ctx, s);

        /* the list */
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "Materials", NK_TEXT_LEFT, P.accent);
        nk_layout_row_dynamic(ctx, row, 3);
        const cv_ftemplate* tp;
        int nt = cv_ftemplates(&tp);
        tip(ctx, "A copy of a well documented material: WWFE-I/II plies, IM7/8552, T800S/M21, AS4/PEEK;\n"
                 "steels, stainless, aluminium, titanium, cast iron; PEEK, PA66, PC, POM, ABS, PP");
        if (nk_combo_begin_label(ctx, "Add from template", nk_vec2(420 * s, CV_MIN(nt, 18) * (row + 4 * s) + 20 * s))) {
            nk_layout_row_dynamic(ctx, row, 1);
            for (int i = 0; i < nt; i++) {
                snprintf(t, sizeof t, "%s  (%s)", tp[i].m.name, tp[i].m.kind == CV_MK_UD ? "UD" : "isotropic");
                tip(ctx, tp[i].source);
                if (nk_combo_item_label(ctx, t, NK_TEXT_LEFT)) { int k = fail_add(&tp[i].m); if (k >= 0) sel = k; dirty = true; }
            }
            nk_combo_end(ctx);
        }
        for (int kind = 0; kind < 2; kind++) {
            tip(ctx, kind == CV_MK_UD ? "A unidirectional ply: strengths along and across the fibres"
                                      : "An isotropic material: yield and ultimate strengths");
            if (nk_button_label(ctx, kind == CV_MK_UD ? "New UD ply" : "New isotropic")) {
                cv_fmat m;
                cv_fmat_defaults(&m, kind);
                snprintf(m.name, sizeof m.name, "%s", kind == CV_MK_UD ? "ply" : "metal");
                int k = fail_add(&m);
                if (k >= 0) sel = k;
                dirty = true;
            }
        }
        if (!fail_count()) {
            nk_layout_row_dynamic(ctx, 2 * row, 1);
            nk_label_colored_wrap(ctx, "None yet: add one from a template, or a new one to fill in.", P.dim);
        }
        nk_layout_row_dynamic(ctx, row, 3);
        for (int i = 0; i < fail_count(); i++) {
            nk_bool on = i == sel;
            snprintf(t, sizeof t, "%s%s", fail_mat(i)->name, fail_mat(i)->kind == CV_MK_UD ? "" : "  (iso)");
            if (nk_selectable_label(ctx, t, NK_TEXT_LEFT, &on) && on) sel = i;
        }

        /* the one chosen */
        cv_fmat* m = fail_mat(sel);
        if (m) {
            uii_hsep(ctx, s);
            if (name_for != sel || (strcmp(name, m->name) && !nk_window_has_focus(ctx))) {
                snprintf(name, sizeof name, "%s", m->name);
                name_len = (int)strlen(name);
                name_for = sel;
            }
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 70 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, 130 * s);
            nk_layout_row_template_push_static(ctx, 80 * s);
            nk_layout_row_template_end(ctx);
            nk_label(ctx, "Name", NK_TEXT_LEFT);
            tip(ctx, "Enter to rename; assignments follow");
            nk_flags ev = nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, name, &name_len, (int)sizeof name - 1, nk_filter_default);
            name[name_len] = 0;
            if ((ev & (NK_EDIT_COMMITED | NK_EDIT_DEACTIVATED)) && strcmp(name, m->name)) {
                if (fail_rename(sel, name)) dirty = true;
                else { snprintf(name, sizeof name, "%s", m->name); name_len = (int)strlen(name); }
            }
            /* the elastic constants of a deck material that uses it */
            int from = -1;
            for (int k = 0; d && k < d->nmats && from < 0; k++) {
                const char* a = fail_assigned(d->mats[k]);
                if (a && !strcmp(a, m->name) && d->mprop && d->mprop[k].el != CV_EL_NONE) from = k;
            }
            tip(ctx, from >= 0 ? "E1, E2, G12, nu12 from the deck material that uses this one (LaRC needs them)"
                               : "Assign it to a deck material with *ELASTIC to take its constants");
            if (nk_button_label(ctx, "Elastic from deck") && from >= 0 && elastic_from_deck(m, d, from)) dirty = true;
            tip(ctx, "Remove it, and its assignments");
            if (nk_button_label(ctx, "Delete")) { fail_remove(sel); sel = -1; dirty = true; m = NULL; }
        }
        if (m) {
            const cv_fprop* pr;
            int np = cv_fprops(&pr);
            float cw = nk_window_get_content_region(ctx).w;
            int cols = cw > 560 * s ? 2 : 1;
            nk_layout_row_template_begin(ctx, row);
            for (int c = 0; c < cols; c++) {
                nk_layout_row_template_push_static(ctx, 70 * s);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_push_static(ctx, 44 * s);
            }
            nk_layout_row_template_end(ctx);
            for (int i = 0; i < np; i++) {
                if (!(pr[i].kinds & (1 << m->kind))) continue;
                double* v = cv_fmat_val(m, &pr[i]);
                const char* lab = pr[i].label;
                if (m->kind == CV_MK_ISO && !strcmp(pr[i].key, "E1")) lab = "E";
                if (m->kind == CV_MK_ISO && !strcmp(pr[i].key, "nu12")) lab = "nu";
                tip(ctx, pr[i].tip);
                nk_label(ctx, lab, NK_TEXT_LEFT);
                char id[24];
                snprintf(id, sizeof id, "#%s", pr[i].key);
                double step = pr[i].unit == CV_FU_MPA ? (*v > 2000 ? 100 : *v > 200 ? 5 : 1) : pr[i].unit == CV_FU_NONE ? 0.01 : 0.01;
                if (pr[i].unit == CV_FU_DEG) step = 1;
                double lo = !strcmp(pr[i].key, "f12") ? -1 : 0, hi = pr[i].unit == CV_FU_DEG ? 90 : 1e7;
                double nv = nk_propertyd(ctx, id, lo, *v, hi, step, (float)step * 0.2f);
                if (nv != *v) { *v = nv; dirty = true; }
                nk_label_colored(ctx, unit_name(pr[i].unit), NK_TEXT_LEFT, P.dim);
            }
            /* what each criterion of its kind still lacks */
            nk_layout_row_dynamic(ctx, row, 1);
            nk_label_colored(ctx, "Criteria", NK_TEXT_LEFT, P.accent);
            nk_layout_row_dynamic(ctx, row, 2);
            for (int c = 0; c < CV_FC_N; c++) {
                if (c == CV_FC_AUTO) {
                    int a = cv_fc_auto(m);
                    if (a < 0) continue;
                    snprintf(t, sizeof t, "Auto: %s", cv_fc_title(a));
                    nk_label(ctx, t, NK_TEXT_LEFT);
                    continue;
                }
                if (cv_fc_ud(c) != (m->kind == CV_MK_UD)) continue;
                char why[96];
                bool ok = cv_fmat_valid(m, c, why, sizeof why);
                if (ok) { snprintf(t, sizeof t, "%s: ready", cv_fc_title(c)); nk_label(ctx, t, NK_TEXT_LEFT); }
                else { tip(ctx, why); nk_label_colored(ctx, why, NK_TEXT_LEFT, P.warn); }
            }
        }
        nk_layout_row_dynamic(ctx, 8 * s, 1);
        nk_spacing(ctx, 1);
        nk_layout_row_dynamic(ctx, 3 * row, 1);
        nk_label_colored_wrap(ctx, "Stresses in MPa: the .frd STRESS converted from the input units (Units window; "
                              "none set: taken as MPa). Plies are judged in their material axes; a composite "
                              "layer is embedded or outer for the LaRC in-situ strengths.", P.dim);
        apply_edits(ctx);
    }
    if (nk_window_is_hidden(ctx, "Strength materials")) G.show_fail = false;
    nk_end(ctx);
}
