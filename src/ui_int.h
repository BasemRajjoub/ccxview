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
#define window_tbar_edit        uii_window_tbar_edit
#define panel_timebar           uii_panel_timebar
#define panel_status            uii_panel_status
#define window_legend           uii_window_legend
#define window_legend_settings  uii_window_legend_settings
#define window_axes             uii_window_axes
#define ov_drag                 uii_ov_drag
#define overlay_drag            uii_overlay_drag
#define view_box                uii_view_box
#define ink_text                uii_ink_text
#define ink_width               uii_ink_width
#define legend_ink              uii_legend_ink
#define legend_dim              uii_legend_dim
#define wrap_pieces             uii_wrap_pieces
#define window_title            uii_window_title
#define window_title_settings   uii_window_title_settings
#define section_render          uii_section_render
#define title_box               uii_title_box
#define path_dirs               uii_path_dirs
#define window_messages         uii_window_messages
#define window_units            uii_window_units
#define window_failure          uii_window_failure
#define section_failure         uii_section_failure
#define window_mesh             uii_window_mesh
#define section_mesh            uii_section_mesh
#define section_label           uii_section_label
#define section_stl             uii_section_stl
#define units_summary           uii_units_summary
#define window_about            uii_window_about
#define window_deck             uii_window_deck
#define window_menu             uii_window_menu
#define window_nav              uii_window_nav
#define window_find             uii_window_find
#define window_browser          uii_window_browser
#define drop_hint               uii_drop_hint
#define window_path             uii_window_path
#define window_history          uii_window_history
#define window_convergence      uii_window_convergence
#define ui_measure_row          uii_measure_row
#define rows_rebar              uii_rows_rebar
#define search_panel            uii_search_panel
#define section_layers          uii_section_layers
#define section_groups          uii_section_groups
#define section_fields          uii_section_fields
#define section_export          uii_section_export
#define section_file            uii_section_file
#define window_settings         uii_window_settings

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
/* a sub-section with a tick in its header (Contact: show), open or closed in *state; its
   rows indented as a tree node's, ended by uii_node_pop. True when open */
bool uii_node_check(struct nk_context* ctx, const char* title, int* state, bool* on, const char* lab, const char* help);
void uii_node_pop(struct nk_context* ctx);
void uii_scroll_here(struct nk_context* ctx);   /* the sidebar scrolled so the next row is at its top */

/* ---- ui_search.c: the box in the Scene panel; a jump's scroll and flash at a section header (ui_kit.c calls them) */
bool uii_search_panel(struct nk_context* ctx, float s, float row);
void uii_search_prefocus(struct nk_context* ctx);
/* ---- ui_palette.c: the command palette, over every window */
void uii_palette_prefocus(struct nk_context* ctx);
void uii_window_palette(struct nk_context* ctx, float s, float row, int fw, int fh);
void uii_jump_scroll(struct nk_context* ctx, int tree);
void uii_jump_flash(struct nk_context* ctx, int tree, struct nk_rect header);

/* ---- ui_kit.c: the sidebar's row helpers (read the guide at its top) */
bool uik_section(struct nk_context* ctx, const char* title, int tree);   /* ... uik_end */
bool uik_sub(struct nk_context* ctx, const char* title, int tree);       /* ... uik_end */
void uik_end(struct nk_context* ctx);
void uik_group(struct nk_context* ctx, const char* title);
void uik_cols(struct nk_context* ctx, int n);                          /* a row of n equal cells */
void uik_wide_row(struct nk_context* ctx, float extra);                /* dynamic cell + static cell of extra px */
void uik_note(struct nk_context* ctx, const char* text);
bool uik_link(struct nk_context* ctx, const char* text, const char* help);
bool uik_link_cell(struct nk_context* ctx, const char* text, const char* help, struct nk_color idle, bool* hovered);   /* in the next cell; idle: its colour off the mouse */
bool uik_file_link(struct nk_context* ctx, const char* text, const char* path, bool* hovered);   /* text in the next cell; a click opens path with the system's app */
void uik_trail(struct nk_context* ctx, float px);                      /* the next row gets a static cell of px at its end ... */
bool uik_trail_toggle(struct nk_context* ctx, nk_bool* on, const char* label, const char* help);   /* ... this fills it */
void uik_label_cells(struct nk_context* ctx, const char* label, const char* help, int n, float extra);
void uik_label_row(struct nk_context* ctx, const char* label, const char* help, float extra);
bool uik_check(struct nk_context* ctx, const char* label, nk_bool* v, const char* help);
bool uik_check2(struct nk_context* ctx, const char* la, nk_bool* a, const char* ta, const char* lb, nk_bool* b, const char* tb);
bool uik_combo(struct nk_context* ctx, const char* label, const char* const* items, int n, int* cur, const char* help);
bool uik_combo_cell(struct nk_context* ctx, const char* const* items, int n, int* cur, const char* help, const char* mark);
bool uik_combo_m(struct nk_context* ctx, const char* label, const char* const* items, int n, int* cur, const char* help, const char* mark);
bool uik_check_combo_m(struct nk_context* ctx, const char* label, nk_bool* on, const char* const* items, int n, int* cur,
                       const char* tip_check, const char* tip_combo, const char* mark);
bool uik_check_combo(struct nk_context* ctx, const char* label, nk_bool* on, const char* const* items, int n, int* cur,
                     const char* tip_check, const char* tip_combo);
bool uik_float(struct nk_context* ctx, const char* label, float* v, float lo, float hi, float step, const char* help);
bool uik_slider_i(struct nk_context* ctx, const char* label, int* v, int lo, int hi, const char* shown, const char* help);
bool uik_num(struct nk_context* ctx, const char* label, float* v, float lo, float hi, float step, const char* help);
bool uik_int(struct nk_context* ctx, const char* label, int* v, int lo, int hi, int step, const char* help);
bool uik_nums(struct nk_context* ctx, const char* label, int n, float* v, float lo, float hi, float step, const char* const* tips);
bool uik_color(struct nk_context* ctx, const char* label, float* rgb, const char* help);
int  uik_buttons(struct nk_context* ctx, const char* label, int n, const char* const* labels, const char* const* tips);
bool uik_chips(struct nk_context* ctx, int n, const char* const* labels, nk_bool* const* v, const char* const* tips);   /* modern only */

/* ---- ui_modern.c: phone-like widgets when G.ui_modern (the kit picks them; off: Nuklear's own) */
bool uim_on(struct nk_context* ctx);           /* G.ui_modern, and not in a bar (Toolbar, Timebar, Status) */
nk_bool uim_checkbox_label(struct nk_context* ctx, const char* label, nk_bool* v);   /* a switch */
bool uim_seg_fits(struct nk_context* ctx, const char* const* items, int n);          /* in the next cell */
bool uim_segments(struct nk_context* ctx, const char* const* items, int n, int* cur);
bool uim_segments_t(struct nk_context* ctx, const char* const* items, int n, int* cur, const char* const* tips);
bool uim_chip(struct nk_context* ctx, const char* label, nk_bool* v);                /* in the next cell */
bool uim_slider(struct nk_context* ctx, const void* id, const char* label, float* v, float lo, float hi, float step, const char* shown);
bool uim_chips(struct nk_context* ctx, float row, int n, const char* const* labels, nk_bool* const* v, const char* const* tips);
void uim_stepper_begin(struct nk_context* ctx, int* save);   /* save[2]: - and + on the next nk_property */
void uim_stepper_end(struct nk_context* ctx, const int* save);
#ifndef UI_MODERN_IMPL
#define nk_checkbox_label uim_checkbox_label   /* every check box of the UI: a switch when modern */
#endif

/* ---- ui_panels.c, ui_view.c: the sidebar */
void recent_buttons(struct nk_context* ctx, float row);
void panel_scene(struct nk_context* ctx, float s, float row);
void section_view(struct nk_context* ctx, float s, float row);
const char* calc_draft(void);                       /* the formula in the Calculated box */
void calc_draft_set(const char* t);
extern const char* const calc_examples[][3];   /* { formula, needs S/E/D, what it is } */
extern const int calc_example_count;
bool calc_example_ok(int i);                   /* this file has the fields it needs */
extern int ui_lib_open;                        /* enum nk_collapse_states of the Saved formulas tree */

/* ---- ui_tabs.c: the left panel's tabs; ui_settings.c: the Settings window */
bool uii_tab_strip(struct nk_context* ctx, float row, const char* const* names, const char* const* tips, int n, int* cur,
                   const char* gear_tip);
void uii_scene_tabs(struct nk_context* ctx, float row);                  /* the strip, under the search box */
void uii_scene_body(struct nk_context* ctx, float s, float row);         /* what the tab holds */
float uii_rail_w(float s);                                              /* ui_rail.c: the icon rail's width, 0 when none */
void uii_rail(struct nk_context* ctx, float s, struct nk_rect r);
void window_settings(struct nk_context* ctx, float s, float row, int fw, int fh);
void uii_keys_help(struct nk_context* ctx);                              /* the mouse and keys, in the Settings window */
void section_layers(struct nk_context* ctx, float s, float row);          /* ui_panels.c: the sections of the tabs */
void section_groups(struct nk_context* ctx, float s, float row);
void section_fields(struct nk_context* ctx, float s, float row);
void section_export(struct nk_context* ctx, float s, float row);
void section_file(struct nk_context* ctx, float s, float row);            /* ui_view.c: compare, watch, reload (Export tab) */

/* ---- ui_stl.c: the Model tab's Imported geometry */
void section_stl(struct nk_context* ctx, float s, float row);

/* ---- ui_tbar.c, ui_tbar_items.c: the top bar as a list of ids */
/* the field shown and what its lists offer (the Fields tree picks them the same way) */
typedef struct {
    bool ok;
    const cv_step* st;
    const cv_field_desc* cur;
} uii_fstate;
uii_fstate uii_field_state(void);
float uii_fpick_w(void);                                  /* ui_fpick.c: the field picker, one button for field and component */
void uii_fpick(struct nk_context* ctx, float s, float row);
bool uii_fpick_ok(void);                                 /* it has something to list (a mesh without results too) */
/* ui_fpick_src.c: what the picker lists. Left: the .frd fields, the .dat ones, calculated, failure,
   mesh quality; right: one entry's rows (headings, values, a window to open) */
enum { FPK_FRD, FPK_DAT, FPK_CALC, FPK_FAIL, FPK_MESH };
typedef struct { int kind, i; const char* id; char title[48]; bool shown; } uii_fpcat;
enum { FPA_HEAD, FPA_FRD, FPA_DAT, FPA_CALC, FPA_CRIT, FPA_FAIL, FPA_MESH, FPA_WIN };
typedef struct { int act, a, b; const char* s, *tip; char label[64]; bool shown; } uii_fprow;
int  uii_fp_cats(const uii_fstate* f, uii_fpcat* c, int max);
int  uii_fp_rows(const uii_fstate* f, const uii_fpcat* c, uii_fprow* r, int max);
void uii_fp_pick(const uii_fstate* f, const uii_fprow* r);
void uii_fp_crumb(const uii_fstate* f, char* out, size_t n);   /* "Mesh quality › aspect ratio" */
int  uii_mq_order(int i);                                /* ui_mesh.c: the overall scores first, then the measures */
typedef struct { const char* id, *label; float w; float (*wfn)(void); int nw; void (*draw)(struct nk_context*, float s, float row); } tb_custom;
int uii_tb_count(void);
const tb_custom* uii_tb_at(int i);
const tb_custom* uii_tb_find(const char* id, size_t n);
void panel_toolbar(struct nk_context* ctx, float s, float row);
int  uii_tb_rows(void);                                   /* rows of the bar (the toolbar window's height) */
void window_tbar_edit(struct nk_context* ctx, float s, float row);

/* ---- ui_bars.c */
void cmap_combo(struct nk_context* ctx, float s, float row);
void legend_controls(struct nk_context* ctx, float s, float row);
void uii_legend_panel(struct nk_context* ctx, float s, float row);   /* the same in the View tab, less what the top bar has */
void panel_timebar(struct nk_context* ctx, float s, float row, float width);
void panel_status(struct nk_context* ctx, float s, float row, float width);
void window_legend(struct nk_context* ctx, float s, float row);
void window_legend_settings(struct nk_context* ctx, float s, float row);
void window_axes(struct nk_context* ctx, float s);
/* overlays on the 3D view (legend, gizmo, title block): moved by a drag from any
   point of them, kept as a view corner (anchor.h); text in the legend's ink */
typedef struct { bool down, moving, dropped, clicked; float px, py, ox, oy; } ov_drag;   /* clicked: let go unmoved */
cv_box view_box(void);
void overlay_drag(struct nk_context* ctx, ov_drag* d, const char* name, cv_box at, float keep_right,
                  cv_anchor* pos, float s);
void ink_text(struct nk_command_buffer* cv, const struct nk_user_font* f, float x, float y, float w,
              const char* txt, struct nk_color c);
/* the width ink_text gives n bytes of t: its digits in cells of one width (tabular figures) */
float ink_width(const struct nk_user_font* f, const char* t, int n);
struct nk_color legend_ink(void);
struct nk_color legend_dim(void);
/* byte lengths of the pieces txt wraps to in width w (after a blank or comma when
   one is near the end, never inside a UTF-8 sequence), at most max; the count */
int wrap_pieces(const struct nk_user_font* f, const char* txt, float w, int* len, int max);

/* ---- ui_title.c: the title block and its settings window */
void window_title(struct nk_context* ctx, float s, float row);
void window_title_settings(struct nk_context* ctx, float s, float row);
void section_render(struct nk_context* ctx, float s, float row);                 /* ui_look.c: the Rendering section */
void uii_render_menu(struct nk_context* ctx, float s, float row);                /* the top bar's Render list (in a combo) */
bool title_box(cv_box* b);          /* where the block is this frame; false while it is not shown */

/* ---- ui_windows.c */
extern const char* path_dirs[5];
void window_messages(struct nk_context* ctx, float s, float row, int fw, int fh);
void window_calc_help(struct nk_context* ctx, float s, float row, int fw, int fh);
void window_units(struct nk_context* ctx, float s, float row, int fw, int fh);
void window_failure(struct nk_context* ctx, float s, float row, int fw, int fh);
void uii_window_cpal(struct nk_context* ctx, float s, float row, int fw, int fh);   /* ui_cpal.c */
void uii_window_report(struct nk_context* ctx, float s, float row, int fw, int fh); /* ui_report.c */
void section_failure(struct nk_context* ctx, float s, float row);
void window_mesh(struct nk_context* ctx, float s, float row, int fw, int fh);     /* ui_mesh.c */
void section_mesh(struct nk_context* ctx, float s, float row);
void section_label(struct nk_context* ctx, float s, float row);    /* ui_label.c */
/* "measure:  distance  angle  circle" on a row; the kind clicked, -1 none (the caller arms it) */
int  ui_measure_row(struct nk_context* ctx, float s, float row, const char* mark);
void rows_rebar(struct nk_context* ctx, float s, float row);    /* ui_rebar.c: REBAR's design values */
void units_summary(char* out, size_t n);     /* "mm, MPa" or "not set", for buttons */
/* the rows of a tool window, without its window: to sit in a floating window or a pane tab */
void uii_details_body(struct nk_context* ctx, float s, float row);      /* ui_info.c: all about the probe */
void uii_details_copy(void);                                            /* ... its lines to the clipboard */
int uii_details_tag(const char* out[], bool hd[], int max);                       /* ... the groups' lines on the tag */
void uii_measure_body(struct nk_context* ctx, float s, float row);      /* ui_measure.c: the measurements */
void window_about(struct nk_context* ctx, float s, float row, int fw, int fh);     /* ui_info.c */
void window_deck(struct nk_context* ctx, float s, float row, int fw, int fh);      /* ui_deck.c */
void window_menu(struct nk_context* ctx, float s, float row, int fw, int fh);      /* ui_menu.c */
void window_nav(struct nk_context* ctx, float s);
void window_find(struct nk_context* ctx, float s, float row);
void window_browser(struct nk_context* ctx, float s, float row, int fw, int fh);
void drop_hint(struct nk_context* ctx, float s, float row);

/* ---- ui_inspector.c: the panes at the sides of the view, the Inspector at the right */
void uii_pane_widths(float W, float s, float* sw, float* iw);   /* px this frame, 0 hidden */
void uii_pane_edges(struct nk_context* ctx, float s, float W, float g, float scene_l, float scene_x, float insp_x, float y0, float y1);
/* ui_glass.c: glass panels, floating panes (g: their gap, 0 docked) */
bool uii_glass(void);
bool uii_float(void);
float uii_float_gap(float s);
bool uii_pane_begin(struct nk_context* ctx, const char* name, struct nk_rect r, nk_flags flags);
void uii_pane_end(struct nk_context* ctx);
bool uii_pane_edge_captured(float x, float y);       /* on an inner edge, or one is being dragged */
void uii_pane_button(struct nk_context* ctx, int which);   /* hide / show: 0 the Scene pane, 1 the Inspector */
void uii_panel_inspector(struct nk_context* ctx, float s, float row);
void uii_tabs_sync(void);    /* G.show_select / show_measure set: the pane on that tab; and back */
void uii_probe_value_text(char* out, size_t n);        /* "S Mises = 4.5e2", over a face or edge the average */
void uii_window_probe_tag(struct nk_context* ctx, float s);   /* the probe's tag in the view */
void uii_probe_readout(char* out, size_t n);           /* "node 123  element 4  S Mises = 4.5e2" */
void uii_kind_boxes(struct nk_context* ctx, const char* const tips[CV_PK_N]);   /* the pick kinds, a box each (G.pick_kinds) */

/* ---- ui_test.c: --ui-test. tip() reports its widget; widgets without a tooltip are
   marked by a name starting with '#'. Nothing unless a test runs. */
void uii_test_mark(struct nk_context* ctx, const char* text);
void uii_test_mark_row(struct nk_context* ctx, const char* text);
void uii_test_mark_at(struct nk_context* ctx, const char* text, struct nk_rect r);   /* a part of a widget */   /* the tree header laid out next */

/* ---- ui_plots.c */
void window_path(struct nk_context* ctx, float s, float row, int fw, int fh);
void window_history(struct nk_context* ctx, float s, float row, int fw, int fh);
void window_convergence(struct nk_context* ctx, float s, float row, int fw, int fh);
/* val[i] against the time t[i] (or the step number step[i] + 1) in area; hover and click */
void uii_plot_steps(struct nk_context* ctx, float s, struct nk_rect area, int n, const float* t, const int* step,
                    const float* val, bool by_step);

/* ---- ui_integ.c: the Integrals window */
void uii_window_integrals(struct nk_context* ctx, float s, float row, int fw, int fh);

/* ---- ui_contact.c: the Contact subgroup of Fields (when there is contact), the key of its
   colours over the view */
bool uii_contact_present(void);
void uii_section_contact(struct nk_context* ctx, float s, float row);
void uii_window_contact(struct nk_context* ctx, float s, float row, int fw, int fh);   /* Contact settings */
void uii_window_contact_key(struct nk_context* ctx, float s, float row);

/* ---- ui_fopts.c: a field's options subgroup in Fields; the open state of its subgroups by name */
int* uii_fnode_state(const char* key, bool open);
void uii_field_options(struct nk_context* ctx, float s, float row, const cv_field_desc* d, bool active);
void uii_gauss_options(struct nk_context* ctx, float s, float row, const char* name);

/* ---- ui_select.c: the Inspector's Selection tab */
void uii_select_body(struct nk_context* ctx, float s, float row);       /* its rows */
void uii_select_ids(const char* text, bool nodes);    /* the id box's text (the interface test) */
void uii_select_name(const char* text);               /* the name box's text (the interface test) */

#endif
