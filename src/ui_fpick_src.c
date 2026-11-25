/* ui_fpick_src.c -- what the field picker (ui_fpick.c) lists: everything the Results tab's Fields can
   colour the model by. The step's .frd fields (a stress group and a shell pair as one), the .dat ones,
   calculated (the formula shown, the saved ones, the examples for this file), failure (outputs, the
   criterion) and mesh quality; each picks with the same call as its section in the panel. */
#include "app.h"
#include "calclib.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "app_fail.h"
#include "quality.h"

#define SEP "  \xe2\x80\xba  "                 /* › */

static bool has_stress(const uii_fstate* f) {
    for (int i = 0; f->ok && i < f->st->nfields; i++)
        if (!strcmp(f->st->fields[i].name, "STRESS") && f->st->fields[i].ncomp >= 6) return true;
    return false;
}

static uii_fpcat* add_cat(uii_fpcat* c, int* n, int max, int kind, int i, const char* id, const char* title, bool shown) {
    if (*n >= max) return NULL;
    uii_fpcat* e = &c[(*n)++];
    e->kind = kind; e->i = i; e->id = id; e->shown = shown;
    snprintf(e->title, sizeof e->title, "%s", title);
    return e;
}

int uii_fp_cats(const uii_fstate* f, uii_fpcat* c, int max) {
    int n = 0;
    for (int i = 0; f->ok && i < f->st->nfields; i++) {
        if (!app_en_listed(f->st, i)) continue;                    /* STRPOS / STRNEG: inside Stress */
        add_cat(c, &n, max, FPK_FRD, i, f->st->fields[i].name, app_field_title(f->st, i), f->cur == &f->st->fields[i]);
    }
    if (gp_loaded()) {
        const char* names[32];
        int nd = gp_fields(names, 32);
        for (int k = 0; k < nd; k++) {
            cv_field_desc d;
            if (!gp_desc(names[k], &d)) continue;
            char t[48];
            snprintf(t, sizeof t, "%s (.dat)", names[k]);
            add_cat(c, &n, max, FPK_DAT, k, names[k], t, G.field_src == 1 && !strcmp(names[k], G.field_name));
        }
    }
    if (f->ok) add_cat(c, &n, max, FPK_CALC, 0, "Calculated", "Calculated", G.field_src == 2);
    if (has_stress(f) || (f->ok && G.field_src == 3)) add_cat(c, &n, max, FPK_FAIL, 0, "Failure", "Failure", G.field_src == 3);
    if (G.loaded && G.frd.n_elems) add_cat(c, &n, max, FPK_MESH, 0, "Mesh quality", "Mesh quality", G.field_src == 4);
    return n;
}

static uii_fprow* add_row(uii_fprow* r, int* n, int max, int act, int a, int b, const char* s, const char* label, bool shown) {
    if (*n >= max) return NULL;
    uii_fprow* e = &r[(*n)++];
    *e = (uii_fprow){ .act = act, .a = a, .b = b, .s = s, .shown = shown };
    snprintf(e->label, sizeof e->label, "%s", label);
    return e;
}

/* a calculated formula, unless an earlier row has it */
static void add_calc(uii_fprow* r, int* n, int max, const char* expr, const char* label, const char* tip) {
    for (int k = 0; k < *n; k++) if (r[k].act == FPA_CALC && !strcmp(r[k].s, expr)) return;
    uii_fprow* e = add_row(r, n, max, FPA_CALC, 0, 0, expr, label, G.field_src == 2 && !strcmp(G.calc_expr, expr));
    if (e) e->tip = tip;
}

int uii_fp_rows(const uii_fstate* f, const uii_fpcat* c, uii_fprow* r, int max) {
    int n = 0;
    cv_scalar_opt o[CV_MAX_OPTS];
    switch (c->kind) {
    case FPK_FRD: {
        int no = app_field_options(&f->st->fields[c->i], o, CV_MAX_OPTS);
        for (int j = 0; j < no; j++)
            add_row(r, &n, max, FPA_FRD, c->i, o[j].comp, NULL, o[j].label, c->shown && G.comp == o[j].comp);
        break;
    }
    case FPK_DAT: {
        cv_field_desc d;
        if (!gp_desc(c->id, &d)) break;
        int no = cv_field_options(&d, o, CV_MAX_OPTS);
        for (int j = 0; j < no; j++)
            add_row(r, &n, max, FPA_DAT, 0, o[j].comp, c->id, o[j].label, c->shown && G.comp == o[j].comp);
        break;
    }
    case FPK_CALC: {
        const cv_calclib* lib = app_formulas();
        bool listed = !G.calc_expr[0];              /* the formula shown: on top unless saved or an example */
        for (int i = 0; !listed && i < lib->n; i++) listed = !strcmp(lib->a[i].expr, G.calc_expr);
        for (int i = 0; !listed && i < calc_example_count; i++) listed = calc_example_ok(i) && !strcmp(calc_examples[i][0], G.calc_expr);
        if (!listed) add_calc(r, &n, max, G.calc_expr, G.calc_expr, "the formula shown");
        if (lib->n) add_row(r, &n, max, FPA_HEAD, 0, 0, NULL, "Saved formulas", false);
        for (int i = 0; i < lib->n; i++) add_calc(r, &n, max, lib->a[i].expr, lib->a[i].name, lib->a[i].expr);
        add_row(r, &n, max, FPA_HEAD, 0, 0, NULL, "Examples", false);
        for (int i = 0; i < calc_example_count; i++)
            if (calc_example_ok(i)) add_calc(r, &n, max, calc_examples[i][0], calc_examples[i][0], calc_examples[i][2]);
        uii_fprow* w = add_row(r, &n, max, FPA_WIN, 0, 0, NULL, "Formula...", false);
        if (w) w->tip = "Build a formula from buttons: this file's fields, operators, functions and examples";
        break;
    }
    case FPK_FAIL: {
        for (int k = 0; k < CV_FO_N; k++) {
            uii_fprow* e = add_row(r, &n, max, FPA_FAIL, k, 0, NULL, fail_out_name(k), c->shown && G.fail_out == k);
            if (e) e->tip = fail_out_tip(k);
        }
        add_row(r, &n, max, FPA_HEAD, 0, 0, NULL, "Criterion", false);
        for (int k = 0; k < CV_FC_N; k++) add_row(r, &n, max, FPA_CRIT, k, 0, NULL, cv_fc_title(k), c->shown && G.fail_crit == k);
        uii_fprow* w = add_row(r, &n, max, FPA_WIN, 1, 0, NULL, "Strength materials...", false);
        if (w) w->tip = "Strength data, which deck material uses which";
        break;
    }
    case FPK_MESH:
        for (int i = 0; i < CV_MQ_N; i++) {
            int q = uii_mq_order(i);
            if (i == 0 || i == CV_MQ_N - CV_MQ_SCORE0) add_row(r, &n, max, FPA_HEAD, 0, 0, NULL, i ? "Measures" : "Overall scores", false);
            uii_fprow* e = add_row(r, &n, max, FPA_MESH, q, 0, NULL, cv_mq(q)->name, c->shown && G.mesh_q == q);
            if (e) e->tip = cv_mq(q)->tip;
        }
        uii_fprow* w = add_row(r, &n, max, FPA_WIN, 2, 0, NULL, "Mesh quality...", false);
        if (w) w->tip = "The mesh at a glance: element types, size, and each measure's range against the usual limits";
        break;
    }
    return n;
}

void uii_fp_pick(const uii_fstate* f, const uii_fprow* r) {
    switch (r->act) {
    case FPA_FRD: {
        int i = r->a;
        app_pick_field(f->st->fields[i].name, r->b, 0);
        break;
    }
    case FPA_DAT: { char nm[32]; snprintf(nm, sizeof nm, "%s", r->s); app_pick_field(nm, r->b, 1); break; }
    case FPA_CALC: calc_draft_set(r->s); app_pick_calc(calc_draft()); break;      /* the box in Calculated shows it too */
    case FPA_FAIL: app_pick_fail(G.fail_crit, r->a); break;
    case FPA_CRIT: app_pick_fail(r->a, G.fail_out); break;
    case FPA_MESH: app_pick_mesh(r->a); break;
    case FPA_WIN:
        if (r->a == 0) G.show_calc_help = true;
        else if (r->a == 1) G.show_fail = true;
        else G.show_mesh = true;
        break;
    }
}

/* "Hashin (1980)" -> "Hashin", "von Mises (yield)" -> "Mises": the button is narrow */
static void crit_short(char* out, size_t n, int c) {
    const char* t = cv_fc_title(c);
    snprintf(out, n, "%s", strncmp(t, "von ", 4) ? t : t + 4);
    char* p = strstr(out, " (");
    if (p) *p = 0;
}

void uii_fp_crumb(const uii_fstate* f, char* out, size_t n) {
    cv_scalar_opt o[CV_MAX_OPTS];
    char t[48];
    switch (G.field_src) {
    case 1: {
        cv_field_desc d;
        int no = gp_desc(G.field_name, &d) ? cv_field_options(&d, o, CV_MAX_OPTS) : 0;
        const char* cl = "component";
        for (int j = 0; j < no; j++) if (o[j].comp == G.comp) cl = o[j].label;
        snprintf(out, n, "%s (.dat)" SEP "%s", G.field_name, cl);
        return;
    }
    case 2: snprintf(out, n, "Calculated" SEP "%s", G.calc_expr); return;
    case 3: crit_short(t, sizeof t, G.fail_crit); snprintf(out, n, "Failure" SEP "%s (%s)", fail_out_name(G.fail_out), t); return;
    case 4: snprintf(out, n, "Mesh quality" SEP "%s", cv_mq(G.mesh_q)->name); return;
    }
    const char* fname = f->cur ? app_field_title(f->st, (int)(f->cur - f->st->fields)) : cv_field_title(G.field_name);
    if (!fname[0]) fname = "field";
    if (!f->cur) { snprintf(out, n, "%s", fname); return; }
    int no = app_field_options(f->cur, o, CV_MAX_OPTS);
    const char* cl = "component";
    for (int j = 0; j < no; j++) if (o[j].comp == G.comp) cl = o[j].label;
    snprintf(out, n, "%s" SEP "%s", fname, cl);
}
