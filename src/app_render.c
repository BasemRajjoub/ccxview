/* app_render.c -- "Render image": a still by progressive accumulation (no path tracer).
   For N frames the scene (not the UI) is drawn into an offscreen target of the swapchain's
   formats, each time with a sub-pixel camera jitter (Halton 2,3: antialiased edges), a
   turned AO noise and a nudged key light; every sample joins a float running average
   (blend: acc = s * 1/n + acc * (1 - 1/n)), and the average is what the window shows. The
   last frame saves a PNG of the view, named as the view export names it. Esc, or any move
   of the camera, stops it. app.c frame() calls begin / scene_pass / present. */
#include "app_int.h"
#include "render_ao.h"
#include "sokol_app.h"
#include "sokol_glue.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool cv_save_png_region(const char* path, int x, int y, int w, int h, int fb_h, const float* bg);
void export_now(void);                       /* app.c: the next free <model>_stepN.png */

static const char* kVS =
    GLSL_HDR
    "void main() {\n"
    "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char* kFS =
    GLSL_HDR
    "uniform vec4 u_w;\n"                     /* x: this sample's weight (the blend's alpha) */
    "uniform sampler2D u_src;\n"
    "out vec4 frag;\n"
    "void main() { frag = vec4(texelFetch(u_src, ivec2(gl_FragCoord.xy), 0).rgb, u_w.x); }\n";

static struct {
    bool on;
    int n, i, w, h;                           /* samples wanted, done; the target's size */
    float mvp0[16];                           /* the view it started from */
    char path[1100];                          /* "": the view export's name */
    double t0;
    bool made, cli_started;
    int cli_wait;
    bool restore;                             /* render view was off: back off when done */
    sg_shader shd; sg_pipeline pip_acc, pip_show; sg_sampler smp;
    sg_image col, res, dep, acc;
    sg_view col_att, res_att, dep_att, col_tex, acc_att, acc_tex;
} R;

bool app_render_active(void) { return R.on; }
int  app_render_done(void) { return R.i; }

void app_render_cancel(void) {
    if (!R.on) return;
    R.on = false;
    if (R.restore) { R.restore = false; app_render_view(false); }
    snprintf(G.note, sizeof G.note, "render stopped at %d/%d", R.i, R.n);
    G.note_t = cv_now();
    if (O.render_n) sapp_request_quit();
}

void app_render_start(int n, const char* path) {
    if (!G.loaded || R.on) return;
    R.restore = !G.render_view;               /* a still is always of the render view */
    if (R.restore) app_render_view(true);
    R.on = true; R.n = n < 1 ? 1 : n; R.i = 0;
    snprintf(R.path, sizeof R.path, "%s", path ? path : "");
    R.t0 = cv_now();
    app_wake();
}

static float halton(int i, int b) {
    float f = 1.f, r = 0.f;
    for (; i > 0; i /= b) { f /= (float)b; r += f * (float)(i % b); }
    return r;
}

static void free_targets(void) {
    sg_view v[] = { R.col_att, R.res_att, R.dep_att, R.col_tex, R.acc_att, R.acc_tex };
    for (int k = 0; k < 6; k++) if (v[k].id) sg_destroy_view(v[k]);
    sg_image im[] = { R.col, R.res, R.dep, R.acc };
    for (int k = 0; k < 4; k++) if (im[k].id) sg_destroy_image(im[k]);
    memset(&R.col, 0, sizeof R.col); memset(&R.res, 0, sizeof R.res); memset(&R.dep, 0, sizeof R.dep); memset(&R.acc, 0, sizeof R.acc);
    memset(&R.col_att, 0, sizeof R.col_att); memset(&R.res_att, 0, sizeof R.res_att); memset(&R.dep_att, 0, sizeof R.dep_att);
    memset(&R.col_tex, 0, sizeof R.col_tex); memset(&R.acc_att, 0, sizeof R.acc_att); memset(&R.acc_tex, 0, sizeof R.acc_tex);
    R.w = R.h = 0;
}

static void make(int w, int h) {
    if (!R.made) {
        R.made = true;
        R.shd = sg_make_shader(&(sg_shader_desc){
            .vertex_func.source = kVS, .fragment_func.source = kFS,
            .uniform_blocks[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .size = 16,
                .glsl_uniforms = { [0] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_w" } } },
            .views[0].texture = { .stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D, .sample_type = SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT },
            .samplers[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .sampler_type = SG_SAMPLERTYPE_NONFILTERING },
            .texture_sampler_pairs[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .view_slot = 0, .sampler_slot = 0, .glsl_name = "u_src" },
            .label = "ccxview-render" });
        /* RGBA32F where it renders (desktop GL; WebGL2 with EXT_color_buffer_float), else RGBA16F */
        sg_pixel_format af = sg_query_pixelformat(SG_PIXELFORMAT_RGBA32F).render ? SG_PIXELFORMAT_RGBA32F : SG_PIXELFORMAT_RGBA16F;
        R.pip_acc = sg_make_pipeline(&(sg_pipeline_desc){
            .shader = R.shd, .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .colors[0] = { .pixel_format = af, .blend = { .enabled = true,
                .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA, .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .src_factor_alpha = SG_BLENDFACTOR_ONE, .dst_factor_alpha = SG_BLENDFACTOR_ZERO } },
            .depth.pixel_format = SG_PIXELFORMAT_NONE, .sample_count = 1 });
        R.pip_show = sg_make_pipeline(&(sg_pipeline_desc){        /* into the swapchain: its defaults */
            .shader = R.shd, .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .depth = { .compare = SG_COMPAREFUNC_ALWAYS, .write_enabled = false } });
        R.smp = sg_make_sampler(&(sg_sampler_desc){ .min_filter = SG_FILTER_NEAREST, .mag_filter = SG_FILTER_NEAREST,
                                                    .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE });
    }
    free_targets();
    sg_environment_defaults e = sglue_environment().defaults;
    sg_pixel_format af = sg_query_pixelformat(SG_PIXELFORMAT_RGBA32F).render ? SG_PIXELFORMAT_RGBA32F : SG_PIXELFORMAT_RGBA16F;
    R.col = sg_make_image(&(sg_image_desc){ .usage.color_attachment = true, .width = w, .height = h, .pixel_format = e.color_format, .sample_count = e.sample_count });
    R.col_att = sg_make_view(&(sg_view_desc){ .color_attachment.image = R.col });
    if (e.sample_count > 1) {
        R.res = sg_make_image(&(sg_image_desc){ .usage.resolve_attachment = true, .width = w, .height = h, .pixel_format = e.color_format, .sample_count = 1 });
        R.res_att = sg_make_view(&(sg_view_desc){ .resolve_attachment.image = R.res });
        R.col_tex = sg_make_view(&(sg_view_desc){ .texture.image = R.res });
    } else R.col_tex = sg_make_view(&(sg_view_desc){ .texture.image = R.col });
    R.dep = sg_make_image(&(sg_image_desc){ .usage.depth_stencil_attachment = true, .width = w, .height = h, .pixel_format = e.depth_format, .sample_count = e.sample_count });
    R.dep_att = sg_make_view(&(sg_view_desc){ .depth_stencil_attachment.image = R.dep });
    R.acc = sg_make_image(&(sg_image_desc){ .usage.color_attachment = true, .width = w, .height = h, .pixel_format = af, .sample_count = 1 });
    R.acc_att = sg_make_view(&(sg_view_desc){ .color_attachment.image = R.acc });
    R.acc_tex = sg_make_view(&(sg_view_desc){ .texture.image = R.acc });
    R.w = w; R.h = h;
}

/* a pixel-sized shift added to clip x, y: row r += e * row 3 of the column-major m */
static void shift(float* m, float ex, float ey) {
    for (int c = 0; c < 4; c++) { m[c * 4] += ex * m[c * 4 + 3]; m[c * 4 + 1] += ey * m[c * 4 + 3]; }
}

bool app_render_begin(cv_draw* d) {
    if (!R.on) return false;
    int w = sapp_width(), h = sapp_height();
    if (R.i == 0) memcpy(R.mvp0, d->mvp, sizeof R.mvp0);
    else if (memcmp(R.mvp0, d->mvp, sizeof R.mvp0) || w != R.w || h != R.h) { app_render_cancel(); return false; }
    if (w != R.w || h != R.h) make(w, h);
    float jx = halton(R.i + 1, 2) - 0.5f, jy = halton(R.i + 1, 3) - 0.5f;
    float ex = 2.f * jx / (float)CV_MAX(d->vp_w, 1), ey = 2.f * jy / (float)CV_MAX(d->vp_h, 1);
    shift(d->mvp, ex, ey); shift(d->proj, ex, ey);
    d->light_jitter[0] = (halton(R.i + 1, 5) - 0.5f) * 0.12f;
    d->light_jitter[1] = (halton(R.i + 1, 7) - 0.5f) * 0.12f;
    cv_ao_sample(R.i);
    snprintf(G.note, sizeof G.note, "rendering %d/%d  (Esc stops)", R.i + 1, R.n);
    G.note_t = cv_now();
    return true;
}

sg_pass app_render_scene_pass(const float bg[3]) {
    sg_pass p = { .action.colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = { bg[0], bg[1], bg[2], 1.f } },
                  .attachments = { .colors[0] = R.col_att, .resolves[0] = R.res_att, .depth_stencil = R.dep_att } };
    return p;
}

static void quad(sg_pipeline pip, sg_view tex, float wgt) {
    sg_apply_pipeline(pip);
    sg_bindings b = { .views[0] = tex, .samplers[0] = R.smp };
    sg_apply_bindings(&b);
    float u[4] = { wgt, 0, 0, 0 };
    sg_apply_uniforms(0, &SG_RANGE(u));
    sg_draw(0, 3, 1);
}

/* the scene pass is over: add it to the average, then the swapchain pass sp is open with the
   average drawn into it; on the last sample the PNG is saved from it and the pass reopened on top */
void app_render_present(sg_pass* sp) {
    sg_begin_pass(&(sg_pass){ .action.colors[0].load_action = SG_LOADACTION_LOAD, .attachments.colors[0] = R.acc_att });
    quad(R.pip_acc, R.col_tex, 1.f / (float)(R.i + 1));
    sg_end_pass();
    R.i++;
    sg_begin_pass(sp);
    quad(R.pip_show, R.acc_tex, 1.f);
    if (R.i < R.n) return;
    sg_end_pass();
    R.on = false;
    if (R.restore) { R.restore = false; app_render_view(false); }
    fprintf(stderr, "rendered %d samples in %.1f s\n", R.n, cv_now() - R.t0);
    if (O.render_n || R.path[0]) {
        bool ok = cv_save_png_region(R.path[0] ? R.path : O.shot_path, G.vp_x + 2, G.vp_y + 2, G.vp_w - 4, G.vp_h - 4, sapp_height(), NULL);
        if (!ok) fprintf(stderr, "cannot write %s\n", R.path[0] ? R.path : O.shot_path);
    } else export_now();
    if (O.render_n) sapp_request_quit();
    sg_pass again = *sp;
    again.action.colors[0].load_action = SG_LOADACTION_LOAD;
    sg_begin_pass(&again);
}

/* --render N out.png: after the load and a few frames, as --shot waits */
void app_render_tick(void) {
    if (!O.render_n || R.cli_started || !G.loaded || app_busy() || G.video || G.seq_left > 0) return;
    if (++O.frame_no < O.shot_frames) return;
    R.cli_started = true;
    app_render_start(O.render_n, NULL);
}

void app_render_free(void) {
    free_targets();
    if (R.made) { sg_destroy_pipeline(R.pip_acc); sg_destroy_pipeline(R.pip_show); sg_destroy_shader(R.shd); sg_destroy_sampler(R.smp); }
    R.made = false;
}
