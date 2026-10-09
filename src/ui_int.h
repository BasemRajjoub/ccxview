/* ui_int.h -- what the ui*.c files share; not for other modules (ui.h is the
   interface). Include it after the headers ui.c includes. The short names are
   macros for uii_ symbols, so the code reads as it did in one file and the
   linker sees names no other module uses. */
#ifndef CV_UI_INT_H
#define CV_UI_INT_H

#define U                       uii_U
#define P                       uii_P
#define themes                  uii_themes
#define mix                     uii_mix
#define apply_scale             uii_apply_scale
#define tip                     uii_tip
#define tip_show                uii_tip_show
#define begin_background        uii_begin_background
#define g_focus_open            uii_g_focus_open
#define fmt_num                 uii_fmt_num
#define tick_num                uii_tick_num
#define sub_push                uii_sub_push
#define recent_buttons          uii_recent_buttons
#define panel_scene             uii_panel_scene
#define section_view            uii_section_view
#define cmap_combo              uii_cmap_combo
#define legend_controls         uii_legend_controls
#define panel_toolbar           uii_panel_toolbar
#define panel_timebar           uii_panel_timebar
#define panel_status            uii_panel_status
#define window_legend           uii_window_legend
#define window_legend_settings  uii_window_legend_settings
#define window_axes             uii_window_axes
#define path_dirs               uii_path_dirs
#define window_messages         uii_window_messages
#define window_units            uii_window_units
#define window_failure          uii_window_failure
#define section_failure         uii_section_failure
#define window_mesh             uii_window_mesh
#define section_mesh            uii_section_mesh
#define section_label           uii_section_label
#define units_summary           uii_units_summary
#define window_probe            uii_window_probe
#define ui_sel_what             uii_sel_what
#define window_details          uii_window_details
#define window_about            uii_window_about
#define window_menu             uii_window_menu
#define window_nav              uii_window_nav
#define window_find             uii_window_find
#define window_browser          uii_window_browser
#define drop_hint               uii_drop_hint
#define window_path             uii_window_path
#define window_history          uii_window_history
#define window_convergence      uii_window_convergence
#define window_measure          uii_window_measure
#define ui_measure_row          uii_measure_row

/* ---- ui_style.c: scale + font, palette, themes */
struct uii_scale {
    float  zoom;              /* user factor, Ctrl +/- */
    float  scale;             /* applied scale (desktop x zoom) */
    struct nk_font_atlas atlas;
    bool   atlas_live;
    sg_image img; sg_view view; sg_sampler smp; snk_image_t snk;
    struct nk_font* font;
    int    theme;             /* index into themes[] */
    bool   restyle;           /* theme changed: rebuild the style next frame */
    float  font_size;         /* user setting, pixels at scale 1 (Inter) */
    bool   pixel_font;        /* user setting: Nuklear's ProggyClean instead of Inter */
    bool   baked_pixel;       /* the atlas has ProggyClean */
    float  px;                /* font height in pixels (scaled) */
};
void uii_vsep(struct nk_context* ctx);              /* a vertical line in the next cell */
void uii_hsep(struct nk_context* ctx, float s);     /* a horizontal line across the row */
extern struct uii_scale U;

/* the colours we draw ourselves (hints, plots, legend), derived from the theme */
struct uii_palette {
    struct nk_color text, dim, warn, accent, accent_text, plot_bg, grid, frame, tick;
};
extern struct uii_palette P;

struct uii_theme { const char* name; int nk; };
enum { NTHEMES = 5 };
extern const struct uii_theme themes[NTHEMES];

struct nk_color mix(struct nk_color a, struct nk_color b, float t);
void apply_scale(struct nk_context* ctx);
/* ink for what is drawn straight over the 3D view (legend, gizmo): the theme
   does not know G.bg, so dark ink on a light background, light ink on a dark one */
struct nk_color uii_on_bg(void);
struct nk_color uii_on_bg_dim(void);      /* secondary text and ticks: the ink 40% toward G.bg */
struct nk_color uii_bg(int alpha);        /* G.bg itself, for halos behind that ink */

/* ---- ui.c: small helpers */
void tip(struct nk_context* ctx, const char* text);    /* tooltip for the widget laid out next */
void tip_show(struct nk_context* ctx, const char* text);  /* a tooltip this frame, whatever is hovered */
const char* uii_tip_shown(void);                       /* the tooltip drawn in the last frame, "" for none */
/* Nuklear's overlay buffer, drawn over every window and popup (sokol_impl.c) */
struct nk_command_buffer* cv_nk_overlay_begin(struct nk_context* ctx);
void cv_nk_overlay_end(struct nk_context* ctx);
/* nk_begin of a window that stays under the others (NK_WINDOW_BACKGROUND) and
   leaves the active window as it was, unless it is the one clicked */
bool begin_background(struct nk_context* ctx, const char* name, struct nk_rect r, nk_flags flags);
extern bool g_focus_open;                              /* Ctrl+L: put the cursor in the path box */
void fmt_num(char* out, size_t n, double v);
void tick_num(char* out, size_t n, double v, double step, double big);
/* the legend's numbers follow the format chosen in its settings window */
#define legend_num(out, n, v) app_legend_fmt(out, n, v)
bool ui_slider_float(struct nk_context* ctx, float lo, float* v, float hi, float step);
bool ui_slider_int(struct nk_context* ctx, int lo, int* v, int hi, int step);
bool sub_push(struct nk_context* ctx, const char* title, int t);

/* ---- ui_panels.c, ui_view.c: the sidebar */
void recent_buttons(struct nk_context* ctx, float row);
void panel_scene(struct nk_context* ctx, float s, float row);
void section_view(struct nk_context* ctx, float s, float row);
const char* calc_draft(void);                       /* the formula in the Calculated box */
void calc_draft_set(const char* t);
extern const char* const calc_examples[][3];   /* { formula, needs S/E/D, what it is } */
extern const int calc_example_count;
bool calc_example_ok(int i);                   /* this file has the fields it needs */

/* ---- ui_bars.c */
void cmap_combo(struct nk_context* ctx, float s, float row);
void legend_controls(struct nk_context* ctx, float s, float row);
void panel_toolbar(struct nk_context* ctx, float s, float row);
void panel_timebar(struct nk_context* ctx, float s, float row, float width);
void panel_status(struct nk_context* ctx, float s, float row, float width);
void window_legend(struct nk_context* ctx, float s, float row);
void window_legend_settings(struct nk_context* ctx, float s, float row);
void window_axes(struct nk_context* ctx, float s);

/* ---- ui_windows.c */
extern const char* path_dirs[5];
void window_messages(struct nk_context* ctx, float s, float row, int fw, int fh);
void window_calc_help(struct nk_context* ctx, float s, float row, int fw, int fh);
void window_units(struct nk_context* ctx, float s, float row, int fw, int fh);
void window_failure(struct nk_context* ctx, float s, float row, int fw, int fh);
void section_failure(struct nk_context* ctx, float s, float row);
void window_mesh(struct nk_context* ctx, float s, float row, int fw, int fh);     /* ui_mesh.c */
void section_mesh(struct nk_context* ctx, float s, float row);
void section_label(struct nk_context* ctx, float s, float row);    /* ui_label.c */
void window_measure(struct nk_context* ctx, float s, float row, int fw, int fh);   /* ui_measure.c */
/* "measure:  distance  angle  circle" on a row; the kind clicked, -1 none (the caller arms it) */
int  ui_measure_row(struct nk_context* ctx, float s, float row, const char* mark);
void units_summary(char* out, size_t n);     /* "mm, MPa" or "not set", for buttons */
void window_probe(struct nk_context* ctx, float s, float row);
void ui_sel_what(struct nk_context* ctx, float row);   /* ui_windows.c: what a box selects */
void window_details(struct nk_context* ctx, float s, float row, int fw, int fh);   /* ui_info.c */
void window_about(struct nk_context* ctx, float s, float row, int fw, int fh);     /* ui_info.c */
void window_menu(struct nk_context* ctx, float s, float row, int fw, int fh);      /* ui_menu.c */
void window_nav(struct nk_context* ctx, float s);
void window_find(struct nk_context* ctx, float s, float row);
void window_browser(struct nk_context* ctx, float s, float row, int fw, int fh);
void drop_hint(struct nk_context* ctx, float s, float row);

/* ---- ui_test.c: --ui-test. tip() reports its widget; widgets without a tooltip are
   marked by a name starting with '#'. Nothing unless a test runs. */
void uii_test_mark(struct nk_context* ctx, const char* text);

/* ---- ui_plots.c */
void window_path(struct nk_context* ctx, float s, float row, int fw, int fh);
void window_history(struct nk_context* ctx, float s, float row, int fw, int fh);
void window_convergence(struct nk_context* ctx, float s, float row, int fw, int fh);

#endif
