/* ui_view.c -- the sidebar's View tab: look from, camera, colours and legend, symbol sizes, copies
   (mirror, replicate, cyclic), cuts; and the Export tab's File group. */
#include "app.h"
#include "ui.h"
#include "cfg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"

/* A size as a logarithmic slider (1x sits in the middle of 0.1x .. 10x); the value
   button sets -1, which the caller turns into its default. True when it changed. */
static bool scale_slider(struct nk_context* ctx, const char* label, const char* help, float* v, float lo, float hi) {
    uik_label_row(ctx, label, help, 44);
    float old = *v, t = log10f(CV_MIN(CV_MAX(*v, lo), hi));
    tip(ctx, help);
    if (ui_slider_float(ctx, log10f(lo), &t, log10f(hi), 0.01f)) *v = powf(10.f, t);
    char b[32];
    snprintf(b, sizeof b, *v < 10 ? "%.2f" : "%.1f", *v);
    tip(ctx, "Click: back to the default");
    if (nk_button_label(ctx, b)) *v = -1;    /* the caller's default */
    return *v != old;
}

/* Symbol sizes: supports, springs, loads, highlighted sets (arrows and glyphs: their field's options) */
static void symbol_sizes(struct nk_context* ctx) {
    bool deck = deck_has_bc() || deck_has_loads() || deck_has_discrete();
    if (!uik_section(ctx, "Symbol sizes", CV_TREE_SYMBOLS)) return;
    if (deck) {
        bool ch = false;
        if (uik_check(ctx, "auto size", &G.sym_auto,
                      "Symbols sized from the model: 2.5 % of its diagonal, whatever the mesh.\nOff: the size beside it, in model units, the same for every model"))
            app_symbol_size();
        if (G.sym_auto) {
            char b[48];
            snprintf(b, sizeof b, "%.4g", G.sym_len);
            uik_label_row(ctx, "size", NULL, 0);
            nk_label_colored(ctx, b, NK_TEXT_RIGHT, P.dim);
        } else {
            float v = G.sym_size, big = CV_MAX(G.diag, 1e-6f);
            if (uik_num(ctx, "size", &v, big * 1e-5f, big, G.sym_auto_len * 0.1f, "Symbol size in model units") && v > 0) {
                G.sym_size = v;
                app_symbol_size();
            }
        }
        ch |= scale_slider(ctx, "thickness", "Thickness of the symbols' lines, times the default", &G.sym_thick, 0.2f, 5.f);
        ch |= uik_check(ctx, "thin crowded symbols", &G.sym_thin,
                        "Where supports or loads crowd (a plane held at every node of a fine mesh),\n"
                        "draw one of a kind per patch of their own size. Off: one at every node");
        if (G.sym_thick < 0) G.sym_thick = 1.f;
        if (deck_has_bc() || deck_has_discrete()) {
            ch |= scale_slider(ctx, "supports", "Supports, springs and masses, times the symbol size", &G.bc_scale, 0.1f, 10.f);
            if (G.bc_scale < 0) G.bc_scale = 1.f;
        }
        if (deck_has_loads()) {
            ch |= scale_slider(ctx, "loads", "Load arrows, times the symbol size", &G.load_scale, 0.1f, 10.f);
            if (G.load_scale < 0) G.load_scale = 1.f;
        }
        if (ch) deck_refresh_highlight();
    }
    scale_slider(ctx, "sets px", "Balls of a highlighted node set or surface, pixels", &G.hl_size, 2.f, 40.f);
    if (G.hl_size < 0) G.hl_size = 8.f;
    uik_end(ctx);
}

/* replicate: rows of copies of a periodic model along X / Y / Z */
static void view_replicate(struct nk_context* ctx) {
    static const char* ax[3] = { "Along X", "Along Y", "Along Z" };
    uik_check(ctx, "follow deformation", &G.rep_follow,
              "With Deform on, each copy sits one deformed cell edge from the next, so a periodic\n"
              "cell under shear or stretch stays joined (from the mean displacement of opposite faces).\n"
              "Off: the copies keep the undeformed spacing");
    for (int k = 0; k < 3; k++) {
        if (uik_check(ctx, ax[k], &G.rep[k], "Draw the model again in a row of copies along this axis\n"
                                              "(a periodic model: several cells side by side). Axes combine into a grid")) app_sym_changed();
        if (!G.rep[k]) continue;
        if (uik_int(ctx, "copies", &G.rep_n[k], 2, 100, 1, "Copies along this axis, the model included")) app_sym_changed();
        char pitch[32], tp[128];
        fmt_num(pitch, sizeof pitch, app_rep_pitch(k));
        snprintf(tp, sizeof tp, "Space between neighbouring copies (0: they touch).\nPitch now %s", pitch);
        float step = CV_MAX(G.diag * 0.01f, 1e-6f);
        if (uik_num(ctx, "gap", &G.rep_gap[k], -1e9f, 1e9f, step, tp)) app_sym_changed();
    }
}

static void view_cyclic(struct nk_context* ctx) {
    static const char* ax[3] = { "axis X", "axis Y", "axis Z" };
    bool ch = uik_check_combo(ctx, "Cyclic copies", &G.cyc_on, ax, 3, &G.cyc_axis,
                              "Draw the sector again turned about an axis, one copy per sector (static cyclic:\n"
                              "every copy shows the same values; vector and tensor components are not turned,\n"
                              "so pick cylindrical coordinates in Fields to compare them)", "The axis the sectors turn about");
    int n = G.cyc_n, sh = G.cyc_show;
    uik_int(ctx, "sectors", &n, 1, 720, 1, "Sectors in the full circle: the copy angle is 360 / n");
    uik_int(ctx, "drawn", &sh, 1, CV_MAX(n, 1), 1, "Sectors drawn, the model included");
    if (n != G.cyc_n) { if (G.cyc_show == G.cyc_n) sh = n; G.cyc_n = n; ch = true; }
    sh = CV_MIN(sh, G.cyc_n);
    if (sh != G.cyc_show) { G.cyc_show = sh; ch = true; }
    static const char* nm[3] = { "axis point x", "axis point y", "axis point z" };
    for (int k = 0; k < 3; k++)
        ch |= uik_num(ctx, nm[k], &G.cyc_o[k], -1e30f, 1e30f, CV_MAX(G.diag * 0.01f, 1e-6f), "A point on the axis");
    if (ch) app_sym_changed();
}

/* ---- the View tab: look from, camera, cuts, copies, colours, symbol sizes */
void section_view(struct nk_context* ctx, float s, float row) {
    (void)s; (void)row;
    static const char* views[] = { "Iso", "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
    uik_group(ctx, "Look from");
    int v = uik_buttons(ctx, NULL, 7, views, NULL);
    if (v >= 0) app_view(v);
    static const char* fit[] = { "Fit (F)" };
    if (uik_buttons(ctx, NULL, 1, fit, NULL) == 0) app_fit();
    static const char* tools[] = { "Measurements...", "Selection..." };
    static const char* tool_tips[] = {
        "Distances, angles and circles between nodes, as labels on the model: the list, new ones, CSV",
        "Select elements and nodes: box, click, by set or id, add / remove / intersect, invert (S)" };
    int t = uik_buttons(ctx, NULL, 2, tools, tool_tips);
    if (t == 0) G.show_measure = !G.show_measure;
    if (t == 1) G.show_select = !G.show_select;

    if (uik_section(ctx, "Camera", CV_TREE_CAMERA)) {
        static const char* upax[] = { "Y", "Z" };
        int up = G.up_z;
        if (uik_combo(ctx, "Up axis", upax, 2, &up, "Dragging turns the model about this world axis, which stays vertical on screen")) {
            G.up_z = up == 1;
            app_view(CV_VIEW_ISO);
        }
        static const char* rot[] = { "turntable", "free" };
        int fr = G.orbit_free;
        if (uik_combo(ctx, "Rotation", rot, 2, &fr,
                      "Turntable: the up axis stays vertical on screen, like FreeCAD's turntable.\n"
                      "Free: drag turns the model any way round (trackball), the up axis is not kept.\n"
                      "A view preset (1-6, Iso) sets it upright again")) app_set_orbit_free(fr == 1);
        static const char* hist[] = { "< view back", "view forward >" };
        static const char* hist_tips[] = { "The view before the last change (Ctrl+Z)", "Forward again (Ctrl+Y)" };
        int h = uik_buttons(ctx, NULL, 2, hist, hist_tips);
        if (h == 0) app_view_undo(-1);
        if (h == 1) app_view_undo(+1);
        uik_group(ctx, "Flight");
        nk_bool fl = G.flight;
        if (uik_check(ctx, "Free flight (G)", &fl, "Walk through the model: WASD, E/Space up, Q down, Shift fast, drag to look")) app_set_flight(fl);
        if (G.flight) {
            uik_num(ctx, "speed", &G.fly_speed, 0.005f, 10.f, 0.05f, "How fast the eye moves");
            static const char* eye_modes[] = { "eye: no cut", "eye: cuts", "eye: hides elements" };
            uik_combo(ctx, NULL, eye_modes, 3, &G.fly_clip,
                      "What lies just in front of the eye: cut away (the cut filled), or whole elements\nhidden; fly into the model and see inside it (the axis clip waits meanwhile)");
            if (G.fly_clip) uik_num(ctx, "depth", &G.fly_clip_depth, 0.f, 0.2f, 0.001f, "How far ahead of the eye the cut lies, in model diagonals");
            uik_note(ctx, "WASD move  E/Space up  Q down");
            uik_note(ctx, "Shift fast  drag look  wheel speed");
            uik_note(ctx, "Esc or G: back to orbit");
        }
        uik_end(ctx);
    }

    if (uik_section(ctx, "Clip & crop", CV_TREE_CLIP)) {
        /* clip plane: cut at draw time along an axis; the cut through solids filled */
        static const char* axes[] = { "normal X", "normal Y", "normal Z" };
        uik_check_combo(ctx, "Clip plane", &G.clip_on, axes, 3, &G.clip_axis,
                        "Cut the drawing at a plane (the crop box below removes whole elements instead)", "The axis the plane is normal to");
        if (G.clip_on) {
            uik_check2(ctx, "flip", &G.clip_flip, "Keep the other side instead",
                       "fill", &G.clip_cap, "Fill the cut through solid elements, coloured by the field (off: hollow)");
            uik_float(ctx, "position", &G.clip_pos, 0.f, 1.f, 0.002f, "Where the plane sits along the axis, as a share of the model's size");
        }
        /* crop box: elements whose centre is outside are removed, so the cut
           shows real element faces, not a hollow shell */
        if (uik_check(ctx, "Crop box", &G.crop_on, "Remove elements whose centre lies outside the box, so the cut shows real element faces"))
            app_groups_changed();
        if (G.crop_on) {
            static const char* reset[] = { "reset" };
            if (uik_buttons(ctx, NULL, 1, reset, NULL) == 0) {
                for (int k = 0; k < 3; k++) { G.crop_lo[k] = 0.f; G.crop_hi[k] = 1.f; }
                app_groups_changed();
            }
            /* one row per axis: label, min box, max box (world units, drag or arrows);
               the fractions behind them stay clamped to the model box */
            const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
            static const char* ax[3] = { "X", "Y", "Z" };
            static const char* tips[2] = { "Lower cut along this axis: drag, arrows, or click to type", "Upper cut along this axis" };
            for (int k = 0; k < 3; k++) {
                float ext = hi[k] - lo[k], step = CV_MAX(ext * 0.01f, 1e-6f);
                float ab[2] = { lo[k] + ext * G.crop_lo[k], lo[k] + ext * G.crop_hi[k] }, a0 = ab[0];
                if (uik_nums(ctx, ax[k], 2, ab, lo[k], hi[k], step, tips)) {
                    if (ab[0] > ab[1]) { if (ab[0] != a0) ab[0] = ab[1]; else ab[1] = ab[0]; }         /* keep min <= max */
                    G.crop_lo[k] = ext > 0 ? (ab[0] - lo[k]) / ext : 0.f;
                    G.crop_hi[k] = ext > 0 ? (ab[1] - lo[k]) / ext : 1.f;
                    app_groups_changed();
                }
            }
        }
        uik_end(ctx);
    }

    if (uik_section(ctx, "Copies", CV_TREE_MIRROR)) {
        uik_group(ctx, "Mirror");
        /* the model mirrored across planes normal to X / Y / Z */
        static const char* ax[3] = { "Mirror X", "Mirror Y", "Mirror Z" };
        const char* mtip = "Draw the model again reflected across a plane normal to this axis\n(a half or quarter model shown whole)";
        const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
        for (int k = 0; k < 3; k++) {
            bool was = G.sym[k];
            if (!was) { if (uik_check(ctx, ax[k], &G.sym[k], mtip)) app_sym_toggled(k); continue; }
            char a0[32], a1[32], it[CV_SYM_N][48];
            fmt_num(a0, sizeof a0, lo[k]);
            fmt_num(a1, sizeof a1, hi[k]);
            snprintf(it[CV_SYM_ZERO], sizeof it[0], "at %c = 0", 'x' + k);
            snprintf(it[CV_SYM_MIN], sizeof it[0], "at min %s", a0);
            snprintf(it[CV_SYM_MAX], sizeof it[0], "at max %s", a1);
            const char* items[CV_SYM_N] = { it[0], it[1], it[2] };
            int at = G.sym_at[k];
            uik_check_combo(ctx, ax[k], &G.sym[k], items, CV_SYM_N, &at, mtip, "Where the mirror plane sits");
            if (G.sym[k] != was) app_sym_toggled(k);
            if (at != G.sym_at[k]) { G.sym_at[k] = at; app_sym_changed(); }
        }
        uik_group(ctx, "Replicate");
        view_replicate(ctx);
        uik_group(ctx, "Cyclic symmetry");
        view_cyclic(ctx);
        uik_end(ctx);
    }

    if (uik_section(ctx, "Colours & legend", CV_TREE_COLOURS)) {
        nk_bool leg = !G.hide_legend;
        if (uik_check(ctx, "show legend", &leg, "The colour legend / group key in the view (also in exports)")) G.hide_legend = !leg;
        uii_legend_panel(ctx, s, row);
        uik_end(ctx);
    }

    symbol_sizes(ctx);
}

/* the Export tab's File group: a second result to compare, watch, reload */
void section_file(struct nk_context* ctx, float s, float row) {
    (void)s; (void)row;
    uik_group(ctx, "File");
    const char* fb[] = { G.cmp_on ? "Compare: off" : "Compare with..." };
    static const char* ft[] = { "Open a second .frd of the same mesh and show fields as the difference (Results > minus ...)" };
    if (uik_buttons(ctx, NULL, 1, fb, ft) == 0) {
        if (G.cmp_on) { app_compare_close(); app_select(G.field_name, G.comp); }
        else app_open_dialog(CV_DLG_COMPARE);
    }
    uik_check(ctx, "Watch file", &G.watch, "Reload when the file changes on disk (a running solver): camera, step and field stay");
    static const char* rl[] = { "Reload" };
    static const char* rl_tip[] = { "Reopen the file now, keeping camera, step and field" };
    if (uik_buttons(ctx, NULL, 1, rl, rl_tip) == 0) app_reload();
    if (app_sidecar_on()) {
        char p[1100], t[1300];
        app_sidecar_path(p, sizeof p);
        snprintf(t, sizeof t, "Forget what was set up for this model (views, sets, paths, kept lines ...):\n"
                              "delete %s and open the model afresh", cv_basename(p));
        const char* forget[] = { "Forget post-processing" };
        const char* ftip[] = { t };
        if (uik_buttons(ctx, NULL, 1, forget, ftip) == 0) app_sidecar_forget();
    }
}
