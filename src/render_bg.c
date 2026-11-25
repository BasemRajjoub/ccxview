/* render_bg.c -- the view's backdrop (render_bg.h): one fullscreen triangle, a gradient
   in the fragment shader, dithered by a screen-space noise of half a level so the slow
   ramps do not band. */
#include "render_bg.h"
#include "render_ao.h"
#include "render_shadow.h"
#include "base.h"
#include <string.h>

const char* const cv_bg_names[CV_BG_N] = { "plain", "Abaqus", "ANSYS", "studio", "white" };

static const float TOP[CV_BG_N][3] = { {0}, { 0.16f, 0.22f, 0.38f }, { 0.53f, 0.66f, 0.84f }, { 0.26f, 0.26f, 0.28f }, { 1.f, 1.f, 1.f } };
static const float BOT[CV_BG_N][3] = { {0}, { 0.78f, 0.80f, 0.84f }, { 1.f, 1.f, 1.f },       { 0.14f, 0.14f, 0.15f }, { 1.f, 1.f, 1.f } };

void cv_bg_colors(int mode, const float plain[3], float top[3], float bot[3], float mean[3]) {
    if (mode < 0 || mode >= CV_BG_N) mode = CV_BG_PLAIN;
    for (int k = 0; k < 3; k++) {
        float t = mode ? TOP[mode][k] : plain[k], b = mode ? BOT[mode][k] : plain[k];
        if (top) top[k] = t;
        if (bot) bot[k] = b;
        if (mean) mean[k] = 0.5f * (t + b);
    }
}

static const char* kVS =
    GLSL_HDR
    "void main() {\n"
    "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* the backdrop as a function of the pixel, for the backdrop's own shader and the floor's:
   u_top, u_bot the colours; u_vp the viewport in GL's pixels, x y w h; u_m.x the mode */
#define BG_GLSL \
    "uniform vec4 u_top;\n" \
    "uniform vec4 u_bot;\n" \
    "uniform vec4 u_vp;\n" \
    "uniform vec4 u_m;\n" \
    "vec3 backdrop(vec2 fc) {\n" \
    "  vec2 uv = (fc - u_vp.xy) / u_vp.zw;\n" \
    "  if (u_m.x > 2.5 && u_m.x < 3.5) {\n"                      /* studio: a soft pool of light, darker at the corners */ \
    "    float r = length((uv - 0.5) * vec2(1.0, 0.85)) * 1.55;\n" \
    "    return mix(u_top.rgb, u_bot.rgb, smoothstep(0.0, 1.0, r));\n" \
    "  }\n" \
    "  return mix(u_bot.rgb, u_top.rgb, uv.y);\n" \
    "}\n"

static const char* kFS =
    GLSL_HDR
    BG_GLSL
    "out vec4 frag;\n"
    "void main() {\n"
    "  vec3 c = backdrop(gl_FragCoord.xy);\n"
    "  float n = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
    "  frag = vec4(c + (n - 0.5) / 255.0, 1.0);\n"
    "}\n";

static struct { sg_shader shd; sg_pipeline pip; bool made; } B;

static struct { sg_shader shd; sg_pipeline pip; bool made; } F;

void cv_bg_free(void) {
    if (B.made) { sg_destroy_pipeline(B.pip); sg_destroy_shader(B.shd); B.made = false; }
    if (F.made) { sg_destroy_pipeline(F.pip); sg_destroy_shader(F.shd); F.made = false; }
}

static void make(void) {
    B.made = true;
    B.shd = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = kVS, .fragment_func.source = kFS,
        .uniform_blocks[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .size = 64,
            .glsl_uniforms = { [0] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_top" },
                               [1] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_bot" },
                               [2] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_vp" },
                               [3] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_m" } } },
        .label = "ccxview-bg",
    });
    B.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = B.shd, .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .depth = { .compare = SG_COMPAREFUNC_ALWAYS, .write_enabled = false },
    });
}

/* the shaders' uniforms u_top, u_bot, u_vp, u_m */
static void bg_uniforms(float u[16], int mode, const float plain[3], int vp_x, int vp_y, int vp_w, int vp_h, int fb_h) {
    float top[3], bot[3];
    cv_bg_colors(mode, plain, top, bot, NULL);
    if (mode == CV_BG_STUDIO) {                       /* centre and corner colours: the shader's two ends */
        static const float c0[3] = { 0.30f, 0.30f, 0.31f }, c1[3] = { 0.10f, 0.10f, 0.11f };
        for (int k = 0; k < 3; k++) { top[k] = c0[k]; bot[k] = c1[k]; }
    }
    float v[16] = { top[0], top[1], top[2], 0, bot[0], bot[1], bot[2], 0,
                    (float)vp_x, (float)(fb_h - vp_y - vp_h), (float)vp_w, (float)vp_h, (float)mode, 0, 0, 0 };
    memcpy(u, v, sizeof v);
}

void cv_bg_draw(int mode, const float plain[3], int vp_x, int vp_y, int vp_w, int vp_h, int fb_h) {
    if (mode <= CV_BG_PLAIN || mode >= CV_BG_N || vp_w <= 0 || vp_h <= 0) return;
    if (!B.made) make();
    float u[16];
    bg_uniforms(u, mode, plain, vp_x, vp_y, vp_w, vp_h, fb_h);
    sg_apply_viewport(vp_x, vp_y, vp_w, vp_h, true);
    sg_apply_scissor_rect(vp_x, vp_y, vp_w, vp_h, true);
    sg_apply_pipeline(B.pip);
    sg_apply_uniforms(0, &SG_RANGE(u));
    sg_draw(0, 3, 1);
}

/* ---- the floor ---------------------------------------------------------------------- */

/* six vertices of a square from o along ax and ay; v_uv 0..1 over it, v_z the view depth */
static const char* kFloorVS =
    GLSL_HDR
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_mv;\n"
    "uniform vec4 u_o;\n"
    "uniform vec4 u_ax;\n"
    "uniform vec4 u_ay;\n"
    "out vec2 v_uv;\n"
    "out float v_z;\n"
    "out vec3 v_vp;\n"
    "void main() {\n"
    "  int i = gl_VertexID < 3 ? gl_VertexID : (gl_VertexID == 3 ? 0 : (gl_VertexID == 4 ? 2 : 3));\n"
    "  v_uv = vec2(i == 1 || i == 2 ? 1.0 : 0.0, i >= 2 ? 1.0 : 0.0);\n"
    "  vec4 p = vec4(u_o.xyz + u_ax.xyz * v_uv.x + u_ay.xyz * v_uv.y, 1.0);\n"
    "  v_vp = (u_mv * p).xyz;\n"
    "  v_z = -v_vp.z;\n"
    "  gl_Position = u_mvp * p;\n"
    "}\n";

/* u_f: x the contact map's half side over the floor's, y the far plane, z the fade before it,
   w the shadow's strength. The result is premultiplied: the floor's colour times its
   opacity, the shadow darkening what lies under it, the floor's own opacity and the
   shadow's both let through the rest. */
static const char* kFloorFS =
    GLSL_HDR
    BG_GLSL
    "uniform vec4 u_f;\n"
    "uniform vec4 u_up;\n"
    "uniform vec4 u_sh;\n"
    "uniform vec4 u_sh2;\n"
    "uniform mat4 u_shm;\n"
    "uniform sampler2D u_con;\n"
    "uniform sampler2DShadow u_shadow;\n"
    "#define SH_INV u_sh2.x\n"
    SHADOW_GLSL
    "in vec2 v_uv;\n"
    "in float v_z;\n"
    "in vec3 v_vp;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "  vec3 bg = backdrop(gl_FragCoord.xy);\n"
    "  float lum = dot(bg, vec3(0.3333));\n"
    "  vec3 tone = lum < 0.5 ? bg * 1.4 + 0.015 : bg * 0.95;\n"
    "  float r = length(v_uv - 0.5) * 2.0;\n"
    "  float fade = 1.0 - smoothstep(0.1, 0.85, r);\n"
    "  fade *= smoothstep(0.0, u_f.z, u_f.y - v_z);\n"
    "  vec2 cuv = 0.5 + (v_uv - 0.5) / u_f.x;\n"
    "  float s = u_f.w * texture(u_con, cuv).r;\n"
    "  vec3 kl = normalize(vec3(-0.42 + u_sh.z, 0.58 + u_sh.w, 0.70));\n"      /* the key light's shadow, where it shines on the floor */
    "  float sk = 0.4 * (1.0 - shadow(v_vp, u_up.xyz, kl)) * smoothstep(0.1, 0.4, dot(u_up.xyz, kl));\n"
    "  s = 1.0 - (1.0 - s) * (1.0 - sk);\n"
    "  float n = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
    "  frag = vec4(tone * fade * (1.0 - s) + (n - 0.5) / 255.0 * fade, 1.0 - (1.0 - fade) * (1.0 - s));\n"
    "}\n";

static void floor_make(void) {
    F.made = true;
    F.shd = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = kFloorVS, .fragment_func.source = kFloorFS,
        .uniform_blocks[0] = { .stage = SG_SHADERSTAGE_VERTEX, .size = 176,
            .glsl_uniforms = { [0] = { .type = SG_UNIFORMTYPE_MAT4, .glsl_name = "u_mvp" },
                               [1] = { .type = SG_UNIFORMTYPE_MAT4, .glsl_name = "u_mv" },
                               [2] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_o" },
                               [3] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_ax" },
                               [4] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_ay" } } },
        .uniform_blocks[1] = { .stage = SG_SHADERSTAGE_FRAGMENT, .size = 192,
            .glsl_uniforms = { [0] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_top" },
                               [1] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_bot" },
                               [2] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_vp" },
                               [3] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_m" },
                               [4] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_f" },
                               [5] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_up" },
                               [6] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_sh" },
                               [7] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_sh2" },
                               [8] = { .type = SG_UNIFORMTYPE_MAT4, .glsl_name = "u_shm" } } },
        .views[0].texture = { .stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D, .sample_type = SG_IMAGESAMPLETYPE_FLOAT },
        .views[1].texture = { .stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D, .sample_type = SG_IMAGESAMPLETYPE_DEPTH },
        .samplers[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .sampler_type = SG_SAMPLERTYPE_FILTERING },
        .samplers[1] = { .stage = SG_SHADERSTAGE_FRAGMENT, .sampler_type = SG_SAMPLERTYPE_COMPARISON },
        .texture_sampler_pairs[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .view_slot = 0, .sampler_slot = 0, .glsl_name = "u_con" },
        .texture_sampler_pairs[1] = { .stage = SG_SHADERSTAGE_FRAGMENT, .view_slot = 1, .sampler_slot = 1, .glsl_name = "u_shadow" },
        .label = "ccxview-floor",
    });
    sg_blend_state bl = { .enabled = true, .src_factor_rgb = SG_BLENDFACTOR_ONE, .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                          .src_factor_alpha = SG_BLENDFACTOR_ONE, .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA };
    F.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = F.shd, .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .face_winding = SG_FACEWINDING_CCW, .cull_mode = SG_CULLMODE_BACK,   /* seen from above only */
        .depth = { .compare = SG_COMPAREFUNC_ALWAYS, .write_enabled = false },
        .colors[0].blend = bl,
    });
}

void cv_floor_draw_now(const cv_floor_draw* f) {
    if (!f->contact.id || f->vp_w <= 0 || f->vp_h <= 0) return;
    if (!F.made) floor_make();
    int iu = f->up == 2 ? 0 : 2, iv = f->up == 2 ? 1 : 0;     /* the model axes along the floor, u x v = up */
    struct { float mvp[16], mv[16], o[4], ax[4], ay[4]; } vs;
    memcpy(vs.mvp, f->mvp, sizeof vs.mvp); memcpy(vs.mv, f->mv, sizeof vs.mv);
    memset(vs.o, 0, sizeof vs.o); memset(vs.ax, 0, sizeof vs.ax); memset(vs.ay, 0, sizeof vs.ay);
    for (int k = 0; k < 3; k++) vs.o[k] = f->c[k];
    vs.o[f->up] = f->level; vs.o[iu] -= f->half; vs.o[iv] -= f->half;
    vs.ax[iu] = 2.f * f->half; vs.ay[iv] = 2.f * f->half;
    struct { float bg[16], f[4], up[4], sh[4], sh2[4], shm[16]; } fs;
    bg_uniforms(fs.bg, f->bg_mode, f->bg_plain, f->vp_x, f->vp_y, f->vp_w, f->vp_h, f->fb_h);
    memcpy(fs.up, f->up_view, 3 * sizeof(float)); fs.up[3] = 0;
    memcpy(fs.sh, f->kinfo, sizeof fs.sh); fs.sh2[0] = f->kinv; fs.sh2[1] = fs.sh2[2] = fs.sh2[3] = 0;
    memcpy(fs.shm, f->klm, sizeof fs.shm);
    fs.f[0] = f->hc / f->half; fs.f[1] = f->zfar; fs.f[2] = f->zfade; fs.f[3] = f->strength;
    sg_apply_viewport(f->vp_x, f->vp_y, f->vp_w, f->vp_h, true);
    sg_apply_scissor_rect(f->vp_x, f->vp_y, f->vp_w, f->vp_h, true);
    sg_apply_pipeline(F.pip);
    sg_apply_bindings(&(sg_bindings){ .views = { [0] = f->contact, [1] = f->kmap }, .samplers = { [0] = f->smp, [1] = f->kcmp } });
    sg_apply_uniforms(0, &SG_RANGE(vs));
    sg_apply_uniforms(1, &SG_RANGE(fs));
    sg_draw(0, 6, 1);
}
