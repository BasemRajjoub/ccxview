/* ui_tbar_items.c -- the top bar's own widgets (deform scale, colour map, field combos, menus ...),
   each one an id the bar list (ui_tbar.c, app_cmds.h) can hold. An item pushes its own cell widths
   into a static layout row; w() is their sum in unscaled px, 0 when the item has nothing to show. */
#include "app.h"
#include "ui.h"
#include "cfg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"
#include "icons.h"
#include "app_cmds.h"

/* ---- the field shown and what its combos offer (ui_int.h) */

uii_fstate uii_field_state(void) {
    uii_fstate f = { 0 };
    f.ok = G.loaded && G.frd.n_steps > 0 && G.step < G.frd.n_steps && G.frd.steps[G.step].nfields > 0;
    if (!f.ok) return f;
    f.st = &G.frd.steps[G.step];
    for (int i = 0; G.field_src == 0 && i < f.st->nfields; i++)
        if (!strcmp(f.st->fields[i].name, G.field_name)) f.cur = &f.st->fields[i];
    return f;
}

/* ---- widths ---------------------------------------------------------------------------- */
static float w_pane0(void) { return G.scene_on ? 0 : 24; }
static float w_lock(void) { return G.range_lock ? 124 : 24; }                 /* its gap or divider, then the note */
/* with the field picker on, tb:field is the picker; tb:comp too when tb:field is not on the bar */
static bool pick_on_comp(void) { return G.ui_field_picker && !strstr(cv_toolbar(), "tb:field"); }
static float w_field(void) { return G.ui_field_picker ? (uii_fpick_ok() ? 24 + uii_fpick_w() : 0) : !uii_field_state().ok ? 0 : 134; }
static float w_comp(void) { return G.ui_field_picker ? (pick_on_comp() && uii_fpick_ok() ? uii_fpick_w() : 0) : uii_field_state().cur ? 150 : 0; }

/* ---- drawing --------------------------------------------------------------------------- */
#define PUSH(w) nk_layout_row_push(ctx, (w) * s)

static void d_pane0(struct nk_context* ctx, float s, float row) { PUSH(24); uii_pane_button(ctx, 0); }
static void d_deform(struct nk_context* ctx, float s, float row) {
    PUSH(80);
    tip(ctx, "Draw the shape displaced by DISP x scale");
    nk_checkbox_label(ctx, "Deform", &G.deform);
}

/* the scale spans many decades (a mass-normalised mode shape wants 1e-5, a stiff
   part 1e4): arrows step it by a quarter, the box takes any number typed (1e-5
   too) and shows four figures. Changing it by hand switches auto off */
static void d_dscale(struct nk_context* ctx, float s, float row) {
    static char buf[32];
    static bool editing;
    float was = G.deform_scale;
    PUSH(24);
    tip(ctx, "Smaller: the scale x0.8");
    if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_LEFT)) G.deform_scale *= 0.8f;
    PUSH(92);
    if (!editing) snprintf(buf, sizeof buf, "%.4g", G.deform_scale);
    tip(ctx, "Displacement multiplier: type any number, 1e-5 too (Enter). Editing it switches auto off");
    nk_flags ef = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, buf, (int)sizeof buf, nk_filter_default);
    editing = (ef & NK_EDIT_ACTIVE) != 0;
    if (ef & (NK_EDIT_COMMITED | NK_EDIT_DEACTIVATED)) {
        char* e;
        double x = strtod(buf, &e);
        if (e != buf && x >= 0 && x < 1e12) G.deform_scale = (float)x;
        editing = false;
        if (ef & NK_EDIT_COMMITED) nk_edit_unfocus(ctx);
    }
    PUSH(24);
    tip(ctx, "Larger: the scale x1.25");
    if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_RIGHT)) G.deform_scale = G.deform_scale > 0 ? G.deform_scale * 1.25f : 1e-6f;
    if (G.deform_scale != was) { G.deform_auto = false; G.deform = true; }
}

static void d_dauto(struct nk_context* ctx, float s, float row) {
    PUSH(74);
    tip(ctx, "Scale so the largest displacement is ~10% of the model (never below 1), or a multiple of that");
    if (nk_combo_begin_label(ctx, "auto", nk_vec2(120 * s, 5 * row + 20 * s))) {
        static const char* pre[5] = { "auto", "auto x0.25", "auto x0.5", "auto x2", "auto x5" };
        static const float pf[5] = { 1.f, 0.25f, 0.5f, 2.f, 5.f };
        nk_layout_row_dynamic(ctx, row, 1);
        for (int i = 0; i < 5; i++)
            if (nk_combo_item_label(ctx, pre[i], NK_TEXT_LEFT)) { G.deform_auto = i == 0; G.deform_scale = G.auto_scale * pf[i]; G.deform = true; }
        nk_combo_end(ctx);
    }
}
static void d_d11(struct nk_context* ctx, float s, float row) {
    PUSH(40);
    tip(ctx, "True scale: the displacement as computed (scale 1)");
    if (nk_button_label(ctx, "1:1")) { G.deform_auto = false; G.deform_scale = 1.f; G.deform = true; }
}
static void d_animate(struct nk_context* ctx, float s, float row) {
    PUSH(90);
    tip(ctx, "Swing the deformation of this increment over time");
    if (nk_checkbox_label(ctx, "Animate", &G.anim_on) && G.anim_on) G.deform = true;
}
static void d_animmode(struct nk_context* ctx, float s, float row) {
    static const char* anim_names[2] = { "0..1", "-1..1" };
    PUSH(76);
    tip(ctx, G.harmonic ? "Steady-state response: the shape turns through its phase (DISP cos wt - DISPI sin wt)"
                        : "0..1: grow and relax.  -1..1: swing both ways (mode shapes)");
    if (G.harmonic) nk_label_colored(ctx, "phase", NK_TEXT_CENTERED, P.accent_text);
    else G.anim_mode = nk_combo(ctx, anim_names, 2, G.anim_mode, (int)row, nk_vec2(90 * s, 2 * row + 20 * s));
}
static void d_animsec(struct nk_context* ctx, float s, float row) {
    PUSH(120);
    tip(ctx, "Seconds per animation cycle");
    nk_property_float(ctx, "#sec", 0.2f, &G.anim_period, 20.f, 0.25f, 0.02f);
}
static void d_fit(struct nk_context* ctx, float s, float row) {
    PUSH(64);
    tip(ctx, "Fit the model in the view");
    if (nk_button_label(ctx, IC_SCAN "  Fit")) app_fit();
}
static void d_select(struct nk_context* ctx, float s, float row) {
    PUSH(84);
    tip(ctx, "The Selection tab of the Inspector (S): box, click, by set or id, add / remove / intersect, invert");
    nk_selectable_label(ctx, IC_BOX "  Select", NK_TEXT_CENTERED, &G.show_select);
}
static void d_symbols(struct nk_context* ctx, float s, float row) {
    /* supports, loads, couplings and springs with one box: a picture with them, then a
       look at the stresses without; the Layers panel keeps each on its own */
    nk_bool any = G.show_bc || G.show_loads || G.show_links || G.show_disc;
    PUSH(90);
    tip(ctx, "Every symbol layer at once: supports, loads, couplings, springs (Layers: each on its own)");
    if (nk_checkbox_label(ctx, "Symbols", &any)) G.show_bc = G.show_loads = G.show_links = G.show_disc = any;
}
static void d_pane1(struct nk_context* ctx, float s, float row) { PUSH(24); uii_pane_button(ctx, 1); }

static void d_cmap(struct nk_context* ctx, float s, float row) { PUSH(130); cmap_combo(ctx, s, row); }
static void d_bands(struct nk_context* ctx, float s, float row) {
    /* the combo shows the usual counts; any other number (legend settings) shows as "N bands" */
    static const int band_vals[] = { 0, 6, 12, 24 };
    char custom[24];
    snprintf(custom, sizeof custom, "%d bands", G.bands);
    const char* band_names[] = { "smooth", "6 bands", "12 bands", "24 bands", custom };
    int bi = 4;
    for (int i = 0; i < 4; i++) if (band_vals[i] == G.bands) bi = i;
    PUSH(90);
    tip(ctx, "Contour bands: discrete colour steps, or a smooth gradient (any count: legend settings)");
    int bj = nk_combo(ctx, band_names, bi == 4 ? 5 : 4, bi, (int)row, nk_vec2(110 * s, 180 * s));
    if (bj != bi && bj < 4) G.bands = band_vals[bj];
}
static void d_lock(struct nk_context* ctx, float s, float row) {
    PUSH(24);
    if (G.range_lock) uii_vsep(ctx); else nk_spacing(ctx, 1);
    if (G.range_lock) {
        PUSH(100);
        char a[32], b[32], lab[80];
        legend_num(a, sizeof a, G.rmin); legend_num(b, sizeof b, G.rmax);
        snprintf(lab, sizeof lab, "locked %s .. %s", a, b);
        nk_label_colored(ctx, lab, NK_TEXT_LEFT, P.warn);
    }
}
static void d_field(struct nk_context* ctx, float s, float row) {
    uii_fstate f = uii_field_state();
    if (G.ui_field_picker ? !uii_fpick_ok() : !f.ok) return;
    const cv_step* st = f.st;
    PUSH(24);
    uii_vsep(ctx);
    if (G.ui_field_picker) { PUSH(uii_fpick_w()); uii_fpick(ctx, s, row); return; }
    PUSH(110);
    const char* fname = G.field_src == 2 ? "calculated" : G.field_src == 3 ? "failure"   /* what shows, whatever its source */
                      : G.field_src == 4 ? "mesh quality" : f.cur ? app_field_title(st, (int)(f.cur - st->fields)) : cv_field_title(G.field_name);
    tip(ctx, "The result field shown (Fields tree: the same, with the calculated and failure fields)");
    if (nk_combo_begin_label(ctx, fname[0] ? fname : "field", nk_vec2(220 * s, CV_MIN(st->nfields, 12) * row + 20 * s))) {
        nk_layout_row_dynamic(ctx, row, 1);
        for (int i = 0; i < st->nfields; i++) {
            if (!app_en_listed(st, i)) continue;           /* STRPOS / STRNEG are in Stress */
            if (nk_combo_item_label(ctx, app_field_title(st, i), NK_TEXT_LEFT)) {
                cv_scalar_opt o[CV_MAX_OPTS];
                int n = app_field_options(&st->fields[i], o, CV_MAX_OPTS);
                app_pick_field(st->fields[i].name, n ? o[0].comp : 0, 0);
            }
        }
        nk_combo_end(ctx);
    }
}
static void d_comp(struct nk_context* ctx, float s, float row) {
    uii_fstate f = uii_field_state();
    if (G.ui_field_picker) { if (pick_on_comp() && uii_fpick_ok()) { PUSH(uii_fpick_w()); uii_fpick(ctx, s, row); } return; }
    if (!f.cur) return;
    cv_scalar_opt o[CV_MAX_OPTS];
    int n = app_field_options(f.cur, o, CV_MAX_OPTS), ci = -1;
    for (int i = 0; i < n; i++) if (o[i].comp == G.comp) ci = i;
    PUSH(150);
    tip(ctx, "The component or invariant of the field");
    if (nk_combo_begin_label(ctx, ci >= 0 ? o[ci].label : "component", nk_vec2(200 * s, CV_MIN(n, 12) * row + 20 * s))) {
        nk_layout_row_dynamic(ctx, row, 1);
        for (int i = 0; i < n; ++i)
            if (nk_combo_item_label(ctx, o[i].label, NK_TEXT_LEFT)) app_pick_field(f.cur->name, o[i].comp, 0);
        nk_combo_end(ctx);
    }
}
/* a shell's surface stresses: the face its normal leaves (STRPOS), the reverse (STRNEG), or one on both */
static float w_sides(void) { return app_en_has_shells() ? 96 : 0; }
static void d_sides(struct nk_context* ctx, float s, float row) {
    static const char* names[CV_EN_SIDE_N] = { "both faces", "top", "bottom" };
    PUSH(96);
    tip(ctx, "Shell stress side: both faces (STRPOS on the face the normal leaves, STRNEG on the reverse), or top or bottom on both");
    int v = G.en_side;
    v = nk_combo(ctx, names, CV_EN_SIDE_N, v, (int)row, nk_vec2(120 * s, CV_EN_SIDE_N * row + 20 * s));
    if (v != G.en_side) { G.en_side = v; app_en_changed(); }
}
static void d_view(struct nk_context* ctx, float s, float row) {
    PUSH(64);
    tip(ctx, "Look from a side, or the isometric view (keys 1-6, R)");
    if (nk_combo_begin_label(ctx, "View", nk_vec2(120 * s, 8 * row + 24 * s))) {
        static const char* vn[7] = { "Iso", "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
        nk_layout_row_dynamic(ctx, row, 1);
        for (int v = 0; v < 7; v++) if (nk_combo_item_label(ctx, vn[v], NK_TEXT_LEFT)) app_view(v);
        if (nk_combo_item_label(ctx, "Title block...", NK_TEXT_LEFT)) G.title_on = G.title_edit = true;
        nk_combo_end(ctx);
    }
}
static void d_ortho(struct nk_context* ctx, float s, float row) {
    PUSH(60);
    tip(ctx, "Parallel projection: no perspective, distances compare directly");
    nk_checkbox_label(ctx, "ortho", &G.cam.ortho);
}
static void d_labels(struct nk_context* ctx, float s, float row) {
    /* the label kinds as boxes in a list: ids, values, names ... any mix (Fields > Labels: size, colours, gap) */
    char lab[24];
    int n = 0;
    for (int k = 1; k < CV_LABEL_N; k++) n += app_label_on(k);
    if (n) snprintf(lab, sizeof lab, "Labels %d", n); else snprintf(lab, sizeof lab, "Labels");
    PUSH(84);
    tip(ctx, "Labels on the model: ids, values, set and material names, loads, supports, the extremes; any mix (Fields > Labels: size, colours)");
    if (nk_combo_begin_label(ctx, lab, nk_vec2(170 * s, (CV_LABEL_N - 1) * row + 20 * s))) {
        nk_layout_row_dynamic(ctx, row, 1);
        for (int k = 1; k < CV_LABEL_N; k++) {
            nk_bool on = app_label_on(k);
            if (nk_checkbox_label(ctx, app_label_name(k), &on)) app_label_set(k, on);
        }
        nk_combo_end(ctx);
    }
}
static void d_render(struct nk_context* ctx, float s, float row) {
    PUSH(76);
    tip(ctx, "Render view (V): the studio light, backdrop, floor, shadows and ambient occlusion. Off: the plain view");
    bool rv = G.render_view;
    if (nk_checkbox_label(ctx, "Render", &rv)) app_render_view(rv);
}
static void d_rmenu(struct nk_context* ctx, float s, float row) {
    PUSH(28);
    tip(ctx, "Render options: floor, shadows, ambient occlusion, the backdrop, Render image");
    if (nk_combo_begin_symbol(ctx, NK_SYMBOL_TRIANGLE_DOWN, nk_vec2(220 * s, 10 * (row + 4 * s) + 16 * s))) {
        uii_render_menu(ctx, s, row);
        nk_combo_end(ctx);
    }
}

/* ---- the table: id, name in the Customize window, width (unscaled; fn: 0 = hidden), cells ---- */
static const tb_custom items[] = {
    { "tb:pane0",    "Scene pane button",     24, w_pane0, 1, d_pane0 },
    { "tb:deform",   "Deform",                80, NULL, 1, d_deform },
    { "tb:dscale",   "Deform scale",         140, NULL, 3, d_dscale },
    { "tb:dauto",    "Deform auto menu",      74, NULL, 1, d_dauto },
    { "tb:d11",      "Deform 1:1",            40, NULL, 1, d_d11 },
    { "tb:animate",  "Animate",               90, NULL, 1, d_animate },
    { "tb:animmode", "Animation mode",        76, NULL, 1, d_animmode },
    { "tb:animsec",  "Animation seconds",    120, NULL, 1, d_animsec },
    { "tb:fit",      "Fit",                   64, NULL, 1, d_fit },
    { "tb:select",   "Select",                84, NULL, 1, d_select },
    { "tb:symbols",  "Symbols",               90, NULL, 1, d_symbols },
    { "tb:pane1",    "Inspector pane button", 24, NULL, 1, d_pane1 },
    { "tb:cmap",     "Colour map",           130, NULL, 1, d_cmap },
    { "tb:bands",    "Bands",                 90, NULL, 1, d_bands },
    { "tb:lock",     "Locked range note",     24, w_lock, 2, d_lock },
    { "tb:field",    "Field",                134, w_field, 2, d_field },
    { "tb:comp",     "Component",            150, w_comp, 1, d_comp },
    { "tb:sides",    "Shell stress side",     96, w_sides, 1, d_sides },
    { "tb:view",     "View menu",             64, NULL, 1, d_view },
    { "tb:ortho",    "Ortho",                 60, NULL, 1, d_ortho },
    { "tb:labels",   "Labels menu",           84, NULL, 1, d_labels },
    { "tb:render",   "Render",                76, NULL, 1, d_render },
    { "tb:rmenu",    "Render options menu",   28, NULL, 1, d_rmenu },
};
int uii_tb_count(void) { return (int)(sizeof items / sizeof items[0]); }
const tb_custom* uii_tb_at(int i) { return &items[i]; }
const tb_custom* uii_tb_find(const char* id, size_t n) {
    for (int i = 0; i < uii_tb_count(); i++) if (strlen(items[i].id) == n && !strncmp(items[i].id, id, n)) return &items[i];
    return NULL;
}
