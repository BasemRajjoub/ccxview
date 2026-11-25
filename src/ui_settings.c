/* ui_settings.c -- the Settings window (the gear at the end of the left panel's tabs): what is set once
   and rarely changed. Interface (theme, fonts, size, tips), display (fps limit, axes gizmo, title
   block), the background colour, the mouse, the 3D mouse, and the keys and mouse in words. */
#include "app.h"
#include "ui.h"
#include "cfg.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include <math.h>
#include "ui_int.h"

/* the 3D mouse: we read the device ourselves, so its driver's settings do not apply */
static void view_ndof(struct nk_context* ctx) {
    static const char* names[6] = { "pan x", "pan y", "zoom", "tilt", "spin", "roll" };
    static const char* flips[6] = {
        "Reverse left / right panning", "Reverse up / down panning", "Reverse the zoom direction",
        "Reverse the tilt (twist about the cap's left-right axis)", "Reverse the spin (twist about the vertical axis)",
        "Reverse the roll (twist about the line of sight)" };
    uik_cols(ctx, 1);
    tip(ctx, "ccxview reads the 3D mouse directly: the zoom, direction and sensitivity settings of\n"
             "its own driver (3DxWare, spacenavd...) do not apply, set them here");
    nk_label_colored(ctx, "driver settings do not apply here", NK_TEXT_LEFT, P.dim);
    const char* st = !G.ndof_on ? "off"
#ifdef __APPLE__
        : cv_ndof_get_status() == CV_NDOF_INPUT ? "input received"
        : cv_ndof_get_status() == CV_NDOF_DEVICE ? "device reported"
        : cv_ndof_get_status() == CV_NDOF_DRIVER ? "driver ready, no device reported" : "driver not found";
#elif defined(__linux__)
        : G.ndof_found ? "device found" : "waiting for spacenavd";
#else
        : G.ndof_found ? "device found" : "no device found";
#endif
    nk_label_colored(ctx, st, NK_TEXT_LEFT, P.dim);
    tip(ctx, "3D mouse rotation follows the screen axes and enables free rotation,\nincluding roll; mouse-only navigation can still use the turntable");
    nk_label_colored(ctx, "rotation enables free camera", NK_TEXT_LEFT, P.dim);
    uik_check(ctx, "3D mouse", &G.ndof_on, "Read a 3D mouse (SpaceMouse); the cap moves the model like a hand holding it.\nButton 1 fits the model, button 2 the iso view");
    uik_num(ctx, "speed", &G.ndof_speed, 0.05f, 20.f, 0.05f, "Sensitivity, 1 = default");
    uik_num(ctx, "dead zone %", &G.ndof_dead, 0.f, 30.f, 0.5f, "Cap travel ignored around rest, in %");
    uik_group(ctx, "Zoom");
    uik_cols(ctx, 2);
    tip(ctx, "Zoom with the cap pushed forward / back across the table; the lift axis pans up / down");
    if (nk_option_label(ctx, "zoom: forward / back", !G.ndof_zoom_lift)) G.ndof_zoom_lift = false;
    tip(ctx, "Zoom with the cap pushed down / pulled up; forward / back then pans up / down");
    if (nk_option_label(ctx, "zoom: push / pull", G.ndof_zoom_lift)) G.ndof_zoom_lift = true;
    uik_group(ctx, "Reverse");
    uik_cols(ctx, 2);
    for (int k = 0; k < 6; k++) {
        char b[32]; snprintf(b, sizeof b, "reverse %s", names[k]);
        tip(ctx, flips[k]);
        nk_checkbox_label(ctx, b, &G.ndof_flip[k]);
    }
    uik_group(ctx, "Lock");
    uik_check2(ctx, "lock rotation", &G.ndof_lock_rot, "Twisting the cap does not turn the model",
               "lock pan and zoom", &G.ndof_lock_pan, "Pushing the cap does not pan or zoom");
    uik_check(ctx, "dominant axis only", &G.ndof_dominant, "Only the axis moved furthest acts at a time");
}

/* the theme, the font and its size, the whole interface's zoom, tooltips */
static void interface_rows(struct nk_context* ctx) {
    const char* names[NTHEMES];
    for (int i = 0; i < NTHEMES; i++) names[i] = themes[i].name;
    int th = U.theme;
    if (uik_combo(ctx, "UI theme", names, NTHEMES, &th, "Colours of the panels and the legend (Nuklear demo themes)")) { U.theme = th; U.restyle = true; }
    {   /* the usual screen UI sizes; set on release, as the rebake moves the rows */
        static const int sizes[] = { 12, 13, 14, 15, 16, 18, 20 };
        enum { NSIZES = sizeof sizes / sizeof sizes[0] };
        static int pend = -1;
        int cur = 0;
        for (int i = 0; i < NSIZES; i++) if (sizes[i] <= ui_get_font_size()) cur = i;
        if (pend < 0) pend = cur;
        const bool pixel = ui_get_pixel_font();
        char val[16];
        snprintf(val, sizeof val, "%d px", pixel ? 13 : sizes[pend]);
        if (pixel) nk_widget_disable_begin(ctx);
        uik_slider_i(ctx, "UI font", &pend, 0, NSIZES - 1, val, "Height of the panel text, pixels at 100% (Ctrl +/- zooms the whole UI)");
        if (pixel) nk_widget_disable_end(ctx);
        nk_bool pf = pixel;
        if (uik_check(ctx, "Pixel font (ProggyClean)", &pf,
                      "Nuklear's ProggyClean, a pixel font for 13 px, instead of Inter;\n"
                      "Greek, maths and the icons still come from the embedded fonts")) ui_set_pixel_font(pf);
        if (!ctx->input.mouse.buttons[NK_BUTTON_LEFT].down) {
            if (pend != cur) ui_set_font_size((float)sizes[pend]);
            pend = -1;
        }
    }
    {   /* the whole interface scaled, for a small screen or a far one (Ctrl +/-/0 do the same) */
        char z[16];
        snprintf(z, sizeof z, "%.0f %%", ui_get_zoom() * 100.f);
        const char* zb[] = { "-", z, "+" };
        static const char* zt[] = { "The whole interface a step smaller (Ctrl -)", "Click: back to 100 % (Ctrl 0)",
                                    "The whole interface a step larger (Ctrl +)" };
        int zi = uik_buttons(ctx, "UI size", 3, zb, zt);
        if (zi >= 0) ui_zoom(zi == 0 ? -1 : zi == 1 ? 0 : +1);
    }
    uik_check(ctx, "Tooltips", &G.tips, "Tooltips over the controls");
    uik_check(ctx, "Command palette (Ctrl+K in the middle)", &G.ui_palette,
              "Ctrl+K (or Ctrl+Shift+P) opens a command palette in the middle of the window;\noff: Ctrl+K goes to the search box of the left panel");
    uik_check(ctx, "Icon rail (panel tabs as icons)", &G.ui_rail,
              "The left panel's tabs as icons at the window's left edge, with search and Settings;\n"
              "a click on the open tab hides the panel, Ctrl+1..5 pick a tab; off: the tabs as a text strip");
    uik_check(ctx, "Field picker (one button, search)", &G.ui_field_picker,
              "The top bar's field and component as one button: fields and their components side by side,\n"
              "a box to search them, the wheel over it steps the component; off: two lists");
    uik_check(ctx, "Modern widgets (switches, segments, chips)", &G.ui_modern,
              "Phone-like widgets: switches for on/off, segments for a few choices, chips for filters,\n"
              "sliders with their name and value inside (Ctrl+click one: type the value); off: Nuklear's own");
    uik_check(ctx, "Glass panels", &G.ui_glass, "The 3D view drawn behind the panels too, seen through them\n"
                                                "(the model stays framed in the free area; exports are as without)");
    if (!G.ui_glass) return;
    uik_float(ctx, "glass opacity", &G.ui_glass_alpha, 0.2f, 1.f, 0.05f, "How opaque the panels are: 1 solid");
    uik_float(ctx, "fields", &G.ui_glass_boxes, 0.2f, 1.f, 0.05f, "How opaque the panels' fields, combos, check boxes and sliders are");
    uik_float(ctx, "buttons", &G.ui_glass_buttons, 0.2f, 1.f, 0.05f, "How opaque the panels' buttons are: 1 solid");
    uik_float(ctx, "headers", &G.ui_glass_headers, 0.2f, 1.f, 0.05f, "How opaque the section headers and the tab strip are");
    uik_check(ctx, "Floating panels", &G.ui_float, "The panes as floating cards: rounded, a gap round each, the view seen in the gaps");
}

static void display_rows(struct nk_context* ctx) {
    uik_int(ctx, "fps limit", &G.max_fps, 0, 1000, 10, "The most frames a second, to save energy. 0: as the display goes (its refresh rate)");
    nk_bool ax = !G.hide_axes;
    if (uik_check(ctx, "Axes gizmo", &ax, "The axes gizmo in the corner (click its tips to look from there)")) G.hide_axes = !ax;
    uik_check(ctx, "Title block", &G.title_on, "A title block in the view: file, solver, analysis, step, date ... (also in exports)");
    static const char* tb[] = { "Title block..." };
    static const char* tb_tip[] = { "The title block's lines, free text, its date (also: right-click the block)" };
    if (uik_buttons(ctx, NULL, 1, tb, tb_tip) == 0) G.title_on = G.title_edit = true;
}

static void background_rows(struct nk_context* ctx) {
    uik_color(ctx, "Colour", G.bg, "The view's background colour when the Render view is off (Backdrop: plain), in exports too");
    static const char* presets[] = { "white", "grey", "black" };
    int p = uik_buttons(ctx, NULL, 3, presets, NULL);
    if (p >= 0) {                                   /* soft white and grey, as the usual viewers */
        static const float pc[3][3] = { { 0.96f, 0.96f, 0.95f }, { 0.33f, 0.32f, 0.31f }, { 0, 0, 0 } };
        G.bg_mode = 0;
        for (int k = 0; k < 3; k++) G.bg[k] = pc[p][k];
    }
}

static void mouse_rows(struct nk_context* ctx) {
    uik_check(ctx, "Rotate about the cursor", &G.orbit_cursor, "Drag rotates about the point of the model you grab;\noff, about the view centre");
    uik_check(ctx, "Rotate about the model's box", &G.orbit_box, "Grabbing empty space: rotate about where the cursor meets the box of\neverything drawn (copies too, clipping ignored), so it stays next to the model;\noff, about the view centre");
    uik_check(ctx, "Zoom toward the cursor", &G.zoom_cursor, "The wheel zooms toward the point under the cursor; off, toward the view centre");
    uik_check(ctx, "Invert wheel zoom", &G.wheel_invert, "Wheel up zooms out instead of in");
    uik_check(ctx, "Show rotation centre", &G.show_pivot,
              "While navigating, mark the point the view turns or zooms about:\nan axis cross at the rotation centre, a ring at the zoom point");
}

/* the mouse and keys in words (it stood at the foot of the left panel) */
void uii_keys_help(struct nk_context* ctx) {
    static const char* const l[] = {
        "drag: orbit   shift/right/middle: pan", "hold X/Y/Z + drag: about that axis", "ctrl drag: box zoom   ctrl right: zoom",
        "ctrl shift drag: max in a box", "alt drag: roll   middle click, C: centre", "N: normal to face   ctrl Z/Y: view back/fwd",
        "ctrl arrows: turn 15 (shift 90)   alt: roll", "wheel: zoom   click: probe   F: fit", "space: play   arrows: step",
        "+/-: deform scale   R: reset view   1-6: views", "G: free flight   H: view only", "ctrl +/-/0: UI size", "ctrl K: search   ctrl 1-5: panel tabs" };
    uik_cols(ctx, 1);
    for (size_t i = 0; i < CV_COUNT(l); i++) nk_label_colored(ctx, l[i], NK_TEXT_LEFT, P.dim);
}

void window_settings(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    (void)row;
    if (!G.show_settings) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Settings", NK_SHOWN);
    was_open = true;
    float w = CV_MIN(380 * s, fw * 0.9f), h = CV_MIN(fh * 0.86f, 700 * s);
    if (nk_begin(ctx, "Settings", nk_rect(CV_MAX(G.vp_x, 0) + 24 * s, 56 * s, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        uik_group(ctx, "Interface");
        interface_rows(ctx);
        uik_group(ctx, "Display");
        display_rows(ctx);
        uik_group(ctx, "Background");
        background_rows(ctx);
        uik_group(ctx, "Mouse");
        mouse_rows(ctx);
        if (uik_section(ctx, "3D mouse", CV_TREE_NDOF)) { view_ndof(ctx); uik_end(ctx); }
        if (uik_section(ctx, "Keys and mouse", CV_TREE_KEYS)) { uii_keys_help(ctx); uik_end(ctx); }
    }
    if (nk_window_is_hidden(ctx, "Settings")) G.show_settings = false;
    nk_end(ctx);
}
