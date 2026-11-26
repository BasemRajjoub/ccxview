/* ui_look.c -- the sidebar's Render tab and the top bar's Render list: the render
   view switch, the light (flat, headlight, studio), backdrop, floor, shadows, ambient
   occlusion, smooth normals, exposure, and the look of each deck material
   (look.h's presets, guessed or chosen; app_look.c keeps them in the ini). */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "look.h"
#include "render_bg.h"
#include <stdio.h>

/* "Lighting  [flat | headlight | studio]": with render view off, the plain light (flat | headlight) */
static void look_combo(struct nk_context* ctx) {
    static const char* items[] = { "flat", "headlight", "studio" };
    const char* help = "Flat: exact colours, as in the legend. Headlight: light from the eye.\n"
                       "Studio: physically based light under a rig that follows the camera, with the options below (render view on)";
    int k = app_lighting();
    if (uik_combo(ctx, "Lighting", items, G.render_view ? 3 : 2, &k, help)) app_set_lighting(k);   /* render view off: flat | headlight */
}

/* the top bar's Render list: floor, shadows, AO, the backdrop, Render image (the Export picture) */
void uii_render_menu(struct nk_context* ctx, float s, float row) {
    nk_layout_row_dynamic(ctx, row, 1);
    tip(ctx, "A floor under the model, fading out to the backdrop, with a soft shadow where parts touch or hover over it");
    nk_checkbox_label(ctx, "Floor", &G.look_floor);
    tip(ctx, "The key light's shadows, from a shadow map drawn every frame");
    nk_checkbox_label(ctx, "Shadows", &G.look_shadow);
    tip(ctx, "Darkens creases and corners from the depth (half resolution, about 1-2 ms on a GPU)");
    nk_checkbox_label(ctx, "Ambient occlusion", &G.look_ao);
    nk_label_colored(ctx, "Backdrop", NK_TEXT_LEFT, P.dim);
    tip(ctx, "The view's background: the colour of Settings > Background (plain), a gradient as in Abaqus or ANSYS, a studio vignette, or white");
    for (int k = 0; k < CV_BG_N; k++) {
        char b[48];
        snprintf(b, sizeof b, "%s %s", G.bg_mode == k ? "*" : " ", cv_bg_names[k]);
        if (nk_combo_item_label(ctx, b, NK_TEXT_LEFT)) G.bg_mode = k;
    }
    tip(ctx, "The Export picture window, Rendered: a still by accumulating samples, then the Save dialog");
    if (nk_combo_item_label(ctx, "Render image...", NK_TEXT_LEFT)) app_picture_window(true);
    (void)s;
}

static void materials(struct nk_context* ctx) {
    const cv_axis* ax = &G.groups.axis[CV_AXIS_MAT];
    if (!ax->n) { uik_note(ctx, "no materials in the model"); return; }
    for (int i = 0; i < ax->n; i++) {
        uint32_t v = ax->value[i];
        const char* dn = deck_material_name(v);
        char name[80], why[160], autoname[80];
        if (dn && *dn) snprintf(name, sizeof name, "%s", dn); else snprintf(name, sizeof name, "Material %u", v);
        bool user = false;
        int cur = app_look_preset(v, &user, why, sizeof why);
        const char* items[CV_LOOK_N + 1];
        if (user) snprintf(autoname, sizeof autoname, "auto");
        else snprintf(autoname, sizeof autoname, "auto: %s", cv_look_mats[cur].name);
        items[0] = autoname;
        for (int k = 0; k < CV_LOOK_N; k++) items[k + 1] = cv_look_mats[k].name;
        char help[256];
        if (dn && *dn) snprintf(help, sizeof help, "%s\nThe look of this material; kept by its name", user ? "Chosen" : why);
        else snprintf(help, sizeof help, "Not a material of the deck: it keeps the default look");
        uik_label_row(ctx, name, help, 0);
        if (!dn || !*dn) { nk_label_colored(ctx, cv_look_mats[cur].name, NK_TEXT_LEFT, P.dim); continue; }
        tip(ctx, help);
        char mk[48];
        snprintf(mk, sizeof mk, "#look material %d", i + 1);
        uii_test_mark(ctx, mk);
        int sel = nk_combo(ctx, items, CV_LOOK_N + 1, user ? cur + 1 : 0, (int)(U.px + 10 * U.scale),
                           nk_vec2(nk_widget_bounds(ctx).w, (CV_LOOK_N + 1) * (U.px + 10 * U.scale) + 20 * U.scale));
        if (sel != (user ? cur + 1 : 0)) app_look_set(v, sel - 1);
    }
}

/* ---- the Render tab of the sidebar */
void section_render(struct nk_context* ctx, float s, float row) {
    (void)s; (void)row;
    bool rv = G.render_view;
    if (uik_check(ctx, "Render view (V)", &rv,
                  "On: the studio settings below. Off: the plain view (flat or headlight light, the background colour, no floor, shadows or "
                  "occlusion). The settings are kept either way (key V)")) app_render_view(rv);
    if (!G.render_view) uik_note(ctx, "render view is off: the settings below wait");
    uik_group(ctx, "Light and backdrop");
    look_combo(ctx);
    uik_combo(ctx, "Backdrop", (const char* const*)cv_bg_names, CV_BG_N, &G.bg_mode,
              "The view's background: the colour of Settings > Background (plain), a gradient as in Abaqus or ANSYS, a studio vignette, or white");
    uik_check2(ctx, "Floor", &G.look_floor, "A floor under the model, fading out to the backdrop, with a soft shadow where parts touch or hover over it",
               "Shadows", &G.look_shadow, "The key light's shadows, from a shadow map drawn every frame (about the cost of one more pass over the faces)");
    uik_float(ctx, "exposure", &G.look_exposure, 0.5f, 2.f, 0.01f, "The light's scale, 1 as tuned");
    uik_check(ctx, "Smooth normals", &G.look_smooth, "Average the normals across the skin, sharp edges (over 40 degrees) kept");
    uik_group(ctx, "Ambient occlusion");
    uik_check(ctx, "Ambient occlusion", &G.look_ao, "Darkens creases and corners from the depth (half resolution, about 1-2 ms on a GPU)");
    uik_float(ctx, "strength", &G.look_ao_strength, 0.f, 1.f, 0.01f, "How dark the creases get");
    float pct = G.look_ao_radius * 100.f;
    uik_float(ctx, "radius %", &pct, 0.5f, 20.f, 0.1f, "How far a point occludes, as a percentage of the model's size");
    G.look_ao_radius = pct / 100.f;
    uik_check(ctx, "also while moving", &G.look_ao_moving, "Keep the occlusion while the view moves. Never on a software renderer: it is too slow there");
    uik_group(ctx, "Image");
    static const char* ib[] = { "Render image..." };
    static const char* it[] = { "The Export picture window, Rendered: a still by accumulating samples (jittered camera, ambient occlusion and "
                                "light: smooth edges, little noise), its look and what is on it, then the Save dialog" };
    if (uik_buttons(ctx, NULL, 1, ib, it) == 0) app_picture_window(true);
    uik_group(ctx, "Materials");
    materials(ctx);
    static const char* b[] = { "Show materials" };
    static const char* t[] = { "Colour the faces by material, lit by the studio: each material takes its look" };
    if (uik_buttons(ctx, NULL, 1, b, t) == 0) {
        app_show_materials();
    }
}
