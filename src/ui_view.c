/* ui_view.c -- the sidebar's View section: camera, colours and theme, display,
   symbol sizes, replicate and cyclic symmetry, cuts, the file. */
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
static bool scale_slider(struct nk_context* ctx, float row, const char* label, const char* help, float* v, float lo, float hi) {
    static const float ratio[3] = { 0.3f, 0.5f, 0.2f };
    nk_layout_row(ctx, NK_DYNAMIC, row, 3, ratio);
    tip(ctx, help);
    nk_label(ctx, label, NK_TEXT_LEFT);
    float old = *v, t = log10f(CV_MIN(CV_MAX(*v, lo), hi));
    tip(ctx, help);
    if (ui_slider_float(ctx, log10f(lo), &t, log10f(hi), 0.01f)) *v = powf(10.f, t);
    char b[32];
    snprintf(b, sizeof b, *v < 10 ? "%.2f" : "%.1f", *v);
    tip(ctx, "Click: back to the default");
    if (nk_button_label(ctx, b)) *v = -1;    /* the caller's default */
    return *v != old;
}

/* Symbol sizes: supports, springs, loads, vector arrows, highlighted sets */
static void symbol_sizes(struct nk_context* ctx, float s, float row) {
    bool deck = deck_has_bc() || deck_has_loads() || deck_has_discrete();
    if (!sub_push(ctx, "Symbol sizes", CV_TREE_SYMBOLS)) return;
    (void)s;
    if (deck) {
        bool ch = false;
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Symbols sized from the model: 2.5 % of its diagonal, whatever the mesh.\nOff: the size beside it, in model units, the same for every model");
        if (nk_checkbox_label(ctx, "auto size", &G.sym_auto)) app_symbol_size();
        if (G.sym_auto) {
            char b[48];
            snprintf(b, sizeof b, "%.4g", G.sym_len);
            nk_label_colored(ctx, b, NK_TEXT_RIGHT, P.dim);
        } else {
            float v = G.sym_size, big = CV_MAX(G.diag, 1e-6f);
            tip(ctx, "Symbol size in model units");
            nk_property_float(ctx, "#size", big * 1e-5f, &v, big, G.sym_auto_len * 0.1f, G.sym_auto_len * 0.01f);
            if (v != G.sym_size && v > 0) { G.sym_size = v; app_symbol_size(); }
        }
        ch |= scale_slider(ctx, row, "thickness", "Thickness of the symbols' lines, times the default", &G.sym_thick, 0.2f, 5.f);
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "Where supports or loads crowd (a plane held at every node of a fine mesh),\n"
                 "draw one of a kind per patch of their own size. Off: one at every node");
        ch |= nk_checkbox_label(ctx, "thin crowded symbols", &G.sym_thin);
        if (G.sym_thick < 0) G.sym_thick = 1.f;
        if (deck_has_bc() || deck_has_discrete()) {
            ch |= scale_slider(ctx, row, "supports", "Supports, springs and masses, times the symbol size", &G.bc_scale, 0.1f, 10.f);
            if (G.bc_scale < 0) G.bc_scale = 1.f;
        }
        if (deck_has_loads()) {
            ch |= scale_slider(ctx, row, "loads", "Load arrows, times the symbol size", &G.load_scale, 0.1f, 10.f);
            if (G.load_scale < 0) G.load_scale = 1.f;
        }
        if (ch) deck_refresh_highlight();
    }
    if (app_field_is_vector() && G.show_vec) {
        if (scale_slider(ctx, row, "vectors %", "Longest vector arrow, percent of the model diagonal", &G.vec_pct, 0.2f, 50.f)) {
            if (G.vec_pct < 0) G.vec_pct = 5.f;
            app_vectors_changed();
        }
    }
    if (app_field_is_tensor() && G.show_tensor) {
        if (scale_slider(ctx, row, "tensors x", "Largest tensor glyph, times the mean element size", &G.tensor_scale, 0.1f, 10.f)) {
            if (G.tensor_scale < 0) G.tensor_scale = 1.f;
            app_tensors_changed();
        }
    }
    scale_slider(ctx, row, "sets px", "Balls of a highlighted node set or surface, pixels", &G.hl_size, 2.f, 40.f);
    if (G.hl_size < 0) G.hl_size = 8.f;
    nk_tree_pop(ctx);
}

/* replicate: rows of copies of a periodic model along X / Y / Z */
static void view_replicate(struct nk_context* ctx, float s, float row) {
    static const char* ax[3] = { "Along X", "Along Y", "Along Z" };
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "With Deform on, each copy sits one deformed cell edge from the next, so a periodic\n"
             "cell under shear or stretch stays joined (from the mean displacement of opposite faces).\n"
             "Off: the copies keep the undeformed spacing");
    nk_checkbox_label(ctx, "follow deformation", &G.rep_follow);
    for (int k = 0; k < 3; k++) {
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 80 * s);
        if (G.rep[k]) { nk_layout_row_template_push_dynamic(ctx); nk_layout_row_template_push_dynamic(ctx); }
        nk_layout_row_template_end(ctx);
        tip(ctx, "Draw the model again in a row of copies along this axis\n(a periodic model: several cells side by side). Axes combine into a grid");
        if (nk_checkbox_label(ctx, ax[k], &G.rep[k])) app_sym_changed();
        if (!G.rep[k]) continue;
        int n = G.rep_n[k];
        tip(ctx, "Copies along this axis, the model included");
        nk_property_int(ctx, "#n", 2, &n, 100, 1, 0.2f);
        if (n != G.rep_n[k]) { G.rep_n[k] = n; app_sym_changed(); }
        char pitch[32], tp[128];
        fmt_num(pitch, sizeof pitch, app_rep_pitch(k));
        snprintf(tp, sizeof tp, "Space between neighbouring copies (0: they touch).\nPitch now %s", pitch);
        tip(ctx, tp);
        float g = G.rep_gap[k], step = CV_MAX(G.diag * 0.01f, 1e-6f);
        nk_property_float(ctx, "#gap", -1e9f, &g, 1e9f, step, step * 0.1f);
        if (g != G.rep_gap[k]) { G.rep_gap[k] = g; app_sym_changed(); }
    }
}

static void view_cyclic(struct nk_context* ctx, float s, float row) {
    static const char* ax[3] = { "axis X", "axis Y", "axis Z" };
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "Draw the sector again turned about an axis, one copy per sector (static cyclic:\n"
             "every copy shows the same values; vector and tensor components are not turned,\n"
             "so pick cylindrical coordinates in Fields to compare them)");
    bool ch = nk_checkbox_label(ctx, "Cyclic copies", &G.cyc_on);
    int a = nk_combo(ctx, ax, 3, G.cyc_axis, (int)row, nk_vec2(120 * s, 3 * row + 20 * s));
    if (a != G.cyc_axis) { G.cyc_axis = a; ch = true; }
    nk_layout_row_dynamic(ctx, row, 2);
    int n = G.cyc_n, sh = G.cyc_show;
    tip(ctx, "Sectors in the full circle: the copy angle is 360 / n");
    nk_property_int(ctx, "#sectors", 1, &n, 720, 1, 0.2f);
    tip(ctx, "Sectors drawn, the model included");
    nk_property_int(ctx, "#drawn", 1, &sh, CV_MAX(n, 1), 1, 0.2f);
    if (n != G.cyc_n) { if (G.cyc_show == G.cyc_n) sh = n; G.cyc_n = n; ch = true; }
    sh = CV_MIN(sh, G.cyc_n);
    if (sh != G.cyc_show) { G.cyc_show = sh; ch = true; }
    float st = CV_MAX(G.diag * 0.01f, 1e-6f);
    static const char* nm[3] = { "#x0", "#y0", "#z0" };
    nk_layout_row_dynamic(ctx, row, 3);
    for (int k = 0; k < 3; k++) {
        tip(ctx, "A point on the axis");
        float v = nk_propertyf(ctx, nm[k], -1e30f, G.cyc_o[k], 1e30f, st, st * 0.1f);
        if (v != G.cyc_o[k]) { G.cyc_o[k] = v; ch = true; }
    }
    if (ch) app_sym_changed();
}

/* ---- View: camera, colours, display, symmetry, cuts, the file */
void section_view(struct nk_context* ctx, float s, float row) {
    if (!nk_tree_state_push(ctx, NK_TREE_TAB, "View", (enum nk_collapse_states*)&G.tree[CV_TREE_VIEW])) return;
    static const char* views[] = { "Iso", "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label(ctx, "Look from", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, row, 7);
    for (int v = 0; v < 7; v++)
        if (nk_button_label(ctx, views[v])) app_view(v);
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "Parallel projection: no perspective, distances compare directly");
    nk_checkbox_label(ctx, "orthographic", &G.cam.ortho);
    if (nk_button_label(ctx, "Fit (F)")) app_fit();
    nk_layout_row_dynamic(ctx, row, 2);
    tip(ctx, "Distances, angles and circles between nodes, as labels on the model: the list, new ones, CSV");
    if (nk_button_label(ctx, "Measurements...")) G.show_measure = !G.show_measure;
    tip(ctx, "Select elements and nodes: box, click, by set or id, add / remove / intersect, invert (S)");
    if (nk_button_label(ctx, "Selection...")) G.show_select = !G.show_select;

    if (sub_push(ctx, "Camera", CV_TREE_CAMERA)) {
        nk_layout_row_dynamic(ctx, row, 3);
        nk_label(ctx, "Up axis", NK_TEXT_LEFT);
        {
            bool z0 = G.up_z;
            tip(ctx, "Dragging turns the model about this world axis, which stays vertical on screen");
            if (nk_option_label(ctx, "Y", !G.up_z)) G.up_z = false;
            tip(ctx, "Dragging turns the model about this world axis, which stays vertical on screen");
            if (nk_option_label(ctx, "Z", G.up_z)) G.up_z = true;
            if (G.up_z != z0) app_view(CV_VIEW_ISO);
        }
        nk_label(ctx, "Rotation", NK_TEXT_LEFT);
        tip(ctx, "Turntable: the up axis stays vertical on screen, like FreeCAD's turntable");
        if (nk_option_label(ctx, "turntable", !G.orbit_free)) app_set_orbit_free(false);
        tip(ctx, "Free: drag turns the model any way round (trackball), the up axis is not kept.\nA view preset (1-6, Iso) sets it upright again");
        if (nk_option_label(ctx, "free", G.orbit_free)) app_set_orbit_free(true);
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "Drag rotates about the point of the model you grab;\noff, or when grabbing empty space, about the view centre");
        nk_checkbox_label(ctx, "Rotate about the cursor", &G.orbit_cursor);
        tip(ctx, "The wheel zooms toward the point under the cursor; off, toward the view centre");
        nk_checkbox_label(ctx, "Zoom toward the cursor", &G.zoom_cursor);
        tip(ctx, "Wheel up zooms out instead of in");
        nk_checkbox_label(ctx, "Invert wheel zoom", &G.wheel_invert);
        tip(ctx, "While navigating, mark the point the view turns or zooms about:\nan axis cross at the rotation centre, a ring at the zoom point");
        nk_checkbox_label(ctx, "Show rotation centre", &G.show_pivot);
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "The view before the last change (Ctrl+Z)");
        if (nk_button_label(ctx, "< view back")) app_view_undo(-1);
        tip(ctx, "Forward again (Ctrl+Y)");
        if (nk_button_label(ctx, "view forward >")) app_view_undo(+1);
        nk_layout_row_dynamic(ctx, row, 1);
        nk_bool fl = G.flight;
        tip(ctx, "Walk through the model: WASD, E/Space up, Q down, Shift fast, drag to look");
        if (nk_checkbox_label(ctx, "Free flight (G)", &fl)) app_set_flight(fl);
        if (G.flight) {
            nk_property_float(ctx, "#speed", 0.005f, &G.fly_speed, 10.f, 0.05f, 0.005f);
            nk_layout_row_dynamic(ctx, row, 2);
            static const char* eye_modes[] = { "eye: no cut", "eye: cuts", "eye: hides elements" };
            tip(ctx, "What lies just in front of the eye: cut away (the cut filled), or whole elements\nhidden; fly into the model and see inside it (the axis clip waits meanwhile)");
            G.fly_clip = nk_combo(ctx, eye_modes, 3, G.fly_clip, (int)row, nk_vec2(170 * s, 3 * row + 20 * s));
            if (G.fly_clip) {
                tip(ctx, "How far ahead of the eye the cut lies, in model diagonals");
                nk_property_float(ctx, "#depth", 0.f, &G.fly_clip_depth, 0.2f, 0.001f, 0.0002f);
            } else nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, row, 1);
            nk_label_colored(ctx, "WASD move  E/Space up  Q down", NK_TEXT_LEFT, P.dim);
            nk_label_colored(ctx, "Shift fast  drag look  wheel speed", NK_TEXT_LEFT, P.dim);
            nk_label_colored(ctx, "Esc or G: back to orbit", NK_TEXT_LEFT, P.dim);
        }
        nk_tree_state_pop(ctx);
    }

    if (sub_push(ctx, "Colours & legend", CV_TREE_COLOURS)) {
        nk_layout_row_dynamic(ctx, row, 2);
        cmap_combo(ctx, s, row);
        {
            nk_bool leg = !G.hide_legend;
            tip(ctx, "The colour legend / group key in the view (also in exports)");
            if (nk_checkbox_label(ctx, "show legend", &leg)) G.hide_legend = !leg;
        }
        legend_controls(ctx, s, row);
        nk_tree_state_pop(ctx);
    }

    if (sub_push(ctx, "Display", CV_TREE_DISPLAY)) {
        nk_layout_row_dynamic(ctx, row, 2);
        {
            nk_bool ax = !G.hide_axes;
            tip(ctx, "The axes gizmo in the corner (click its tips to look from there)");
            if (nk_checkbox_label(ctx, "Axes gizmo", &ax)) G.hide_axes = !ax;
        }
        tip(ctx, "Light the faces. Off: exact colours, as in the legend");
        nk_checkbox_label(ctx, "Shading", &G.shading);
        tip(ctx, "A title block in the view: file, solver, analysis, step, date ... (also in exports)");
        nk_checkbox_label(ctx, "Title block", &G.title_on);
        tip(ctx, "The title block's lines, free text, its date (also: right-click the block)");
        if (nk_button_label(ctx, "Title block...")) G.title_on = G.title_edit = true;

        /* background: presets and a picker; exports use it too */
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 84 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, row * 1.4f);
        nk_layout_row_template_end(ctx);
        nk_label(ctx, "Background", NK_TEXT_LEFT);
        if (nk_button_label(ctx, "white")) { G.bg[0] = 0.96f; G.bg[1] = 0.96f; G.bg[2] = 0.95f; }   /* soft, as the usual viewers */
        if (nk_button_label(ctx, "grey"))  { G.bg[0] = 0.33f; G.bg[1] = 0.32f; G.bg[2] = 0.31f; }
        if (nk_button_label(ctx, "black")) { G.bg[0] = G.bg[1] = G.bg[2] = 0.f; }
        static bool bg_pick;
        struct nk_rect bb = nk_widget_bounds(ctx);
        if (nk_button_color(ctx, nk_rgb_f(G.bg[0], G.bg[1], G.bg[2]))) bg_pick = !bg_pick;
        if (bg_pick) {
            struct nk_rect clip = ctx->current->layout->clip;
            float pw = 230 * s, ph = 170 * s + 3 * row + 6 * ctx->style.window.spacing.y;
            struct nk_rect pr = nk_rect(bb.x + bb.w - pw - clip.x, bb.y + bb.h - clip.y, pw, ph);
            if (nk_popup_begin(ctx, NK_POPUP_STATIC, "bgpick", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, pr)) {
                nk_layout_row_dynamic(ctx, 170 * s, 1);
                struct nk_colorf cf = nk_color_picker(ctx, (struct nk_colorf){ G.bg[0], G.bg[1], G.bg[2], 1 }, NK_RGB);
                G.bg[0] = cf.r; G.bg[1] = cf.g; G.bg[2] = cf.b;
                nk_layout_row_dynamic(ctx, row, 3);
                G.bg[0] = nk_propertyf(ctx, "#R", 0, G.bg[0], 1, 0.01f, 0.005f);
                G.bg[1] = nk_propertyf(ctx, "#G", 0, G.bg[1], 1, 0.01f, 0.005f);
                G.bg[2] = nk_propertyf(ctx, "#B", 0, G.bg[2], 1, 0.01f, 0.005f);
                nk_layout_row_dynamic(ctx, row, 1);
                if (nk_button_label(ctx, "done")) { bg_pick = false; nk_popup_close(ctx); }
                nk_popup_end(ctx);
            } else {
                bg_pick = false;
            }
        }
        nk_layout_row_template_begin(ctx, row);
        nk_layout_row_template_push_static(ctx, 84 * s);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        nk_label(ctx, "UI theme", NK_TEXT_LEFT);
        {
            const char* names[NTHEMES];
            for (int i = 0; i < NTHEMES; i++) names[i] = themes[i].name;
            tip(ctx, "Colours of the panels and the legend (Nuklear demo themes)");
            int t = nk_combo(ctx, names, NTHEMES, U.theme, (int)row, nk_vec2(200 * s, 6 * row + 20 * s));
            if (t != U.theme) { U.theme = t; U.restyle = true; }
        }
        {   /* the usual screen UI sizes; set on release, as the rebake moves the rows */
            static const int sizes[] = { 12, 13, 14, 15, 16, 18, 20 };
            enum { NSIZES = sizeof sizes / sizeof sizes[0] };
            static int pend = -1;
            int cur = 0;
            for (int i = 0; i < NSIZES; i++) if (sizes[i] <= ui_get_font_size()) cur = i;
            if (pend < 0) pend = cur;
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 84 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, 44 * s);
            nk_layout_row_template_end(ctx);
            const bool pixel = ui_get_pixel_font();
            if (pixel) nk_widget_disable_begin(ctx);
            nk_label(ctx, "UI font", NK_TEXT_LEFT);
            tip(ctx, "Height of the panel text, pixels at 100% (Ctrl +/- zooms the whole UI)");
            ui_slider_int(ctx, 0, &pend, NSIZES - 1, 1);
            char v[16];
            snprintf(v, sizeof v, "%d px", pixel ? 13 : sizes[pend]);
            nk_label(ctx, v, NK_TEXT_RIGHT);
            if (pixel) nk_widget_disable_end(ctx);
            nk_layout_row_dynamic(ctx, row, 1);
            nk_bool pf = pixel;
            tip(ctx, "Nuklear's ProggyClean, a pixel font for 13 px, instead of Inter;\n"
                     "Greek, maths and the icons still come from the embedded fonts");
            if (nk_checkbox_label(ctx, "Pixel font (ProggyClean)", &pf)) ui_set_pixel_font(pf);
            if (!ctx->input.mouse.buttons[NK_BUTTON_LEFT].down) {
                if (pend != cur) ui_set_font_size((float)sizes[pend]);
                pend = -1;
            }
        }
        {   /* the whole interface scaled, for a small screen or a far one (Ctrl +/-/0 do the same) */
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 84 * s);
            nk_layout_row_template_push_static(ctx, 32 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, 32 * s);
            nk_layout_row_template_end(ctx);
            nk_label(ctx, "UI size", NK_TEXT_LEFT);
            tip(ctx, "The whole interface a step smaller (Ctrl -)");
            if (nk_button_label(ctx, "-")) ui_zoom(-1);
            char z[16];
            snprintf(z, sizeof z, "%.0f %%", ui_get_zoom() * 100.f);
            tip(ctx, "Click: back to 100 % (Ctrl 0)");
            if (nk_button_label(ctx, z)) ui_zoom(0);
            tip(ctx, "The whole interface a step larger (Ctrl +)");
            if (nk_button_label(ctx, "+")) ui_zoom(+1);
        }
        nk_tree_state_pop(ctx);
    }

    symbol_sizes(ctx, s, row);
    if (sub_push(ctx, "Mirror", CV_TREE_MIRROR)) {
        /* the model mirrored across planes normal to X / Y / Z */
        {
            static const char* ax[3] = { "Mirror X", "Mirror Y", "Mirror Z" };
            const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
            for (int k = 0; k < 3; k++) {
                nk_layout_row_dynamic(ctx, row, G.sym[k] ? 2 : 1);
                tip(ctx, "Draw the model again reflected across a plane normal to this axis\n(a half or quarter model shown whole)");
                bool was = G.sym[k];
                if (nk_checkbox_label(ctx, ax[k], &G.sym[k])) app_sym_toggled(k);
                if (!was) continue;
                char a0[32], a1[32], it[CV_SYM_N][48];
                fmt_num(a0, sizeof a0, lo[k]);
                fmt_num(a1, sizeof a1, hi[k]);
                snprintf(it[CV_SYM_ZERO], sizeof it[0], "at %c = 0", 'x' + k);
                snprintf(it[CV_SYM_MIN], sizeof it[0], "at min %s", a0);
                snprintf(it[CV_SYM_MAX], sizeof it[0], "at max %s", a1);
                const char* items[CV_SYM_N] = { it[0], it[1], it[2] };
                int s2 = nk_combo(ctx, items, CV_SYM_N, G.sym_at[k], (int)row, nk_vec2(160 * s, 4 * row + 20 * s));
                if (s2 != G.sym_at[k]) { G.sym_at[k] = s2; app_sym_changed(); }
            }
        }

        nk_tree_state_pop(ctx);
    }

    if (sub_push(ctx, "Replicate", CV_TREE_REPLICATE)) {
        view_replicate(ctx, s, row);
        nk_tree_state_pop(ctx);
    }

    if (sub_push(ctx, "Cyclic symmetry", CV_TREE_CYCLIC)) {
        view_cyclic(ctx, s, row);
        nk_tree_state_pop(ctx);
    }

    if (sub_push(ctx, "Clip & crop", CV_TREE_CLIP)) {
        /* clip plane: cut at draw time along an axis; the cut through solids filled */
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Cut the drawing at a plane (the crop box below removes whole elements instead)");
        nk_checkbox_label(ctx, "Clip plane", &G.clip_on);
        if (G.clip_on) {
            static const char* axes[] = { "normal X", "normal Y", "normal Z" };
            G.clip_axis = nk_combo(ctx, axes, 3, G.clip_axis, (int)row, nk_vec2(120 * s, 3 * row + 20 * s));
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 50 * s);
            nk_layout_row_template_push_static(ctx, 50 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);
            tip(ctx, "Keep the other side instead");
            nk_checkbox_label(ctx, "flip", &G.clip_flip);
            tip(ctx, "Fill the cut through solid elements, coloured by the field (off: hollow)");
            nk_checkbox_label(ctx, "fill", &G.clip_cap);
            ui_slider_float(ctx, 0.f, &G.clip_pos, 1.f, 0.002f);
        }
        /* crop box: elements whose centre is outside are removed, so the cut
           shows real element faces, not a hollow shell */
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Remove elements whose centre lies outside the box, so the cut shows real element faces");
        if (nk_checkbox_label(ctx, "Crop box", &G.crop_on)) app_groups_changed();
        if (G.crop_on && nk_button_label(ctx, "reset")) {
            for (int k = 0; k < 3; k++) { G.crop_lo[k] = 0.f; G.crop_hi[k] = 1.f; }
            app_groups_changed();
        }
        if (G.crop_on) {
            /* one row per axis: label, min box, max box (world units, drag or arrows);
               the fractions behind them stay clamped to the model box */
            const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
            static const char* ax[3] = { "X", "Y", "Z" };
            nk_layout_row_template_begin(ctx, row);
            nk_layout_row_template_push_static(ctx, 16 * s);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);
            for (int k = 0; k < 3; k++) {
                float ext = hi[k] - lo[k], step = CV_MAX(ext * 0.01f, 1e-6f);
                float a = lo[k] + ext * G.crop_lo[k], b = lo[k] + ext * G.crop_hi[k], a0 = a, b0 = b;
                nk_label(ctx, ax[k], NK_TEXT_LEFT);
                tip(ctx, "Lower cut along this axis: drag, arrows, or click to type");
                a = nk_propertyf(ctx, "#", lo[k], a, hi[k], step, step * 0.1f);
                tip(ctx, "Upper cut along this axis");
                b = nk_propertyf(ctx, "#", lo[k], b, hi[k], step, step * 0.1f);
                if (a != a0 || b != b0) {
                    if (a > b) { if (a != a0) a = b; else b = a; }         /* keep min <= max */
                    G.crop_lo[k] = ext > 0 ? (a - lo[k]) / ext : 0.f;
                    G.crop_hi[k] = ext > 0 ? (b - lo[k]) / ext : 1.f;
                    app_groups_changed();
                }
            }
        }
        nk_tree_state_pop(ctx);
    }

    if (sub_push(ctx, "File", CV_TREE_FILE)) {
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Open a second .frd of the same mesh and show fields as the difference (Fields > minus ...)");
        if (nk_button_label(ctx, G.cmp_on ? "Compare: off" : "Compare with...")) {
            if (G.cmp_on) { app_compare_close(); app_select(G.field_name, G.comp); }
            else { G.dlg_for_compare = true; app_open_dialog(); }
        }
        tip(ctx, "Import geometry that was not analysed (an STL file) to show with the results; listed in Groups");
        if (nk_button_label(ctx, "Import STL...")) { G.dlg_for_stl = true; app_open_dialog(); }
        nk_layout_row_dynamic(ctx, row, 2);
        tip(ctx, "Reload when the file changes on disk (a running solver): camera, step and field stay");
        nk_checkbox_label(ctx, "Watch file", &G.watch);
        tip(ctx, "Reopen the file now, keeping camera, step and field");
        if (nk_button_label(ctx, "Reload")) app_reload();
        if (app_sidecar_on()) {
            char p[1100], t[1300];
            app_sidecar_path(p, sizeof p);
            snprintf(t, sizeof t, "Forget what was set up for this model (views, sets, paths, kept lines ...):\n"
                                  "delete %s and open the model afresh", cv_basename(p));
            nk_layout_row_dynamic(ctx, row, 1);
            tip(ctx, t);
            if (nk_button_label(ctx, "Forget post-processing")) app_sidecar_forget();
        }

        nk_tree_state_pop(ctx);
    }
    nk_tree_state_pop(ctx);
}
