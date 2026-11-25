/* render_shadow.c -- the shadows' targets and fullscreen passes (render_shadow.h) */
#include "render_shadow.h"
#include "render_ao.h"
#include "base.h"
#include <math.h>
#include <string.h>

static const char* kVS =
    GLSL_HDR
    "void main() {\n"
    "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* u_a: x tight radius, y wide radius (both in texels), z the height the depth spans in
   model sizes (diag), w unused. A tap holds if the map has a surface there (depth < 1);
   its weight falls with that surface's height above the floor. */
static const char* kContact =
    GLSL_HDR
    "uniform vec4 u_a;\n"
    "uniform sampler2D u_dep;\n"
    "out vec4 frag;\n"
    "const int N = 32;\n"
    "void main() {\n"
    "  ivec2 sz = textureSize(u_dep, 0);\n"
    "  float rot = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
    "  float c = 0.0, w = 0.0, ws = 0.0;\n"
    "  for (int i = 0; i < N; i++) {\n"
    "    float t = (float(i) + 0.5) / float(N), ang = float(i) * 2.3999632 + rot;\n"
    "    vec2 o = vec2(cos(ang), sin(ang)) * sqrt(t);\n"
    "    float k = 1.0 - 0.7 * t;\n"
    "    ws += k;\n"
    "    float d1 = texelFetch(u_dep, clamp(ivec2(gl_FragCoord.xy + o * u_a.x), ivec2(0), sz - 1), 0).r;\n"
    "    float d2 = texelFetch(u_dep, clamp(ivec2(gl_FragCoord.xy + o * u_a.y), ivec2(0), sz - 1), 0).r;\n"
    "    if (d1 < 1.0) c += k * exp(-d1 * u_a.z / 0.03);\n"       /* only what is within a few percent of the size of the floor */
    "    if (d2 < 1.0) w += k * exp(-d2 * u_a.z / 0.5);\n"
    "  }\n"
    "  c /= ws; w /= ws;\n"
    "  frag = vec4(1.0 - (1.0 - 0.90 * c) * (1.0 - 0.70 * w));\n"
    "}\n";

static struct {
    sg_shader shd;  sg_pipeline pip;  sg_sampler smp_near, smp_lin;
    sg_image dimg;  sg_view datt, dtex;                 /* the contact map's depth */
    sg_image rimg;  sg_view ratt, rtex;                 /* its shadow */
    bool made, have;
    sg_image kimg;  sg_view katt, ktex;  int ksize;     /* the key light's map */
    sg_image mimg;  sg_view mtex;                       /* the dummy */
    sg_sampler kcmp;
    bool kmade;
} S;

static void make(void) {
    S.made = true;
    S.shd = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = kVS, .fragment_func.source = kContact,
        .uniform_blocks[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .size = 16,
            .glsl_uniforms = { [0] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_a" } } },
        .views[0].texture = { .stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D, .sample_type = SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT },
        .samplers[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .sampler_type = SG_SAMPLERTYPE_NONFILTERING },
        .texture_sampler_pairs[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .view_slot = 0, .sampler_slot = 0, .glsl_name = "u_dep" },
        .label = "ccxview-contact",
    });
    S.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = S.shd, .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .colors[0].pixel_format = SG_PIXELFORMAT_R8, .depth.pixel_format = SG_PIXELFORMAT_NONE, .sample_count = 1,
    });
    S.smp_near = sg_make_sampler(&(sg_sampler_desc){ .min_filter = SG_FILTER_NEAREST, .mag_filter = SG_FILTER_NEAREST,
                                                    .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE });
    S.smp_lin = sg_make_sampler(&(sg_sampler_desc){ .min_filter = SG_FILTER_LINEAR, .mag_filter = SG_FILTER_LINEAR,
                                                   .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE });
    int n = CV_CONTACT_SIZE;
    S.dimg = sg_make_image(&(sg_image_desc){ .usage.depth_stencil_attachment = true, .width = n, .height = n,
                                             .pixel_format = SG_PIXELFORMAT_DEPTH, .sample_count = 1 });
    S.datt = sg_make_view(&(sg_view_desc){ .depth_stencil_attachment.image = S.dimg });
    S.dtex = sg_make_view(&(sg_view_desc){ .texture.image = S.dimg });
    S.rimg = sg_make_image(&(sg_image_desc){ .usage.color_attachment = true, .width = n, .height = n,
                                             .pixel_format = SG_PIXELFORMAT_R8, .sample_count = 1 });
    S.ratt = sg_make_view(&(sg_view_desc){ .color_attachment.image = S.rimg });
    S.rtex = sg_make_view(&(sg_view_desc){ .texture.image = S.rimg });
}

sg_view cv_sh_contact_target(void) {
    if (!S.made) make();
    return S.datt;
}

void cv_sh_contact_run(float diag, float hc, float hmax) {
    if (!S.made) return;
    float px = 2.f * hc / (float)CV_CONTACT_SIZE;            /* model units per texel */
    float u[4] = { 0.035f * diag / px, 0.25f * diag / px, hmax / diag, 0 };
    sg_begin_pass(&(sg_pass){ .action.colors[0] = { .load_action = SG_LOADACTION_DONTCARE }, .attachments.colors[0] = S.ratt });
    sg_apply_pipeline(S.pip);
    sg_apply_bindings(&(sg_bindings){ .views[0] = S.dtex, .samplers[0] = S.smp_near });
    sg_apply_uniforms(0, &SG_RANGE(u));
    sg_draw(0, 3, 1);
    sg_end_pass();
    S.have = true;
}

sg_view cv_sh_contact_view(void) { return S.have ? S.rtex : (sg_view){0}; }
sg_sampler cv_sh_linear(void) { if (!S.made) make(); return S.smp_lin; }

/* ---- the key light's map ---------------------------------------------------------- */

static void key_free(void) {
    if (S.ktex.id) sg_destroy_view(S.ktex);
    if (S.katt.id) sg_destroy_view(S.katt);
    if (S.kimg.id) sg_destroy_image(S.kimg);
    S.ktex.id = S.katt.id = S.kimg.id = 0; S.ksize = 0;
}

static void key_make(void) {
    S.kmade = true;
    S.kcmp = sg_make_sampler(&(sg_sampler_desc){ .min_filter = SG_FILTER_LINEAR, .mag_filter = SG_FILTER_LINEAR,
                                                .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
                                                .compare = SG_COMPAREFUNC_LESS_EQUAL });
    S.mimg = sg_make_image(&(sg_image_desc){ .usage.depth_stencil_attachment = true, .width = 4, .height = 4,
                                             .pixel_format = SG_PIXELFORMAT_DEPTH, .sample_count = 1 });
    sg_view att = sg_make_view(&(sg_view_desc){ .depth_stencil_attachment.image = S.mimg });
    S.mtex = sg_make_view(&(sg_view_desc){ .texture.image = S.mimg });
    sg_begin_pass(&(sg_pass){ .action.depth = { .load_action = SG_LOADACTION_CLEAR, .clear_value = 1.f }, .attachments.depth_stencil = att });
    sg_end_pass();
    sg_destroy_view(att);
}

sg_view cv_sh_key_target(int size) {
    if (!S.kmade) key_make();
    if (size <= 0) return (sg_view){0};
    if (S.ksize != size) {
        key_free();
        S.kimg = sg_make_image(&(sg_image_desc){ .usage.depth_stencil_attachment = true, .width = size, .height = size,
                                                 .pixel_format = SG_PIXELFORMAT_DEPTH, .sample_count = 1 });
        S.katt = sg_make_view(&(sg_view_desc){ .depth_stencil_attachment.image = S.kimg });
        S.ktex = sg_make_view(&(sg_view_desc){ .texture.image = S.kimg });
        S.ksize = size;
    }
    return S.katt;
}

sg_view cv_sh_key_view(void) { return S.ktex.id ? S.ktex : S.mtex; }
sg_view cv_sh_key_dummy(void) { return S.mtex; }
sg_sampler cv_sh_key_cmp(void) { return S.kcmp; }

void cv_sh_key_dir(float dir[3], const float j[2]) {
    float x = -0.42f + j[0], y = 0.58f + j[1], z = 0.70f, n = sqrtf(x * x + y * y + z * z);
    dir[0] = x / n; dir[1] = y / n; dir[2] = z / n;
}

void cv_sh_key_matrix(float lm[16], float dir[3], const float c[3], float r, const float j[2]) {
    cv_sh_key_dir(dir, j);
    float up[3] = { 0, 1, 0 };
    float u[3] = { up[1] * dir[2] - up[2] * dir[1], up[2] * dir[0] - up[0] * dir[2], up[0] * dir[1] - up[1] * dir[0] };
    float un = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (int k = 0; k < 3; k++) u[k] /= un;
    float v[3] = { dir[1] * u[2] - dir[2] * u[1], dir[2] * u[0] - dir[0] * u[2], dir[0] * u[1] - dir[1] * u[0] };
    r *= 1.05f;
    memset(lm, 0, 16 * sizeof(float));
    for (int k = 0; k < 3; k++) {            /* column-major: row 0 u, row 1 v, row 2 -dir (nearer the light, smaller depth) */
        lm[4 * k] = u[k] / r;  lm[4 * k + 1] = v[k] / r;  lm[4 * k + 2] = -dir[k] / r;
    }
    lm[12] = -(u[0] * c[0] + u[1] * c[1] + u[2] * c[2]) / r;
    lm[13] = -(v[0] * c[0] + v[1] * c[1] + v[2] * c[2]) / r;
    lm[14] = (dir[0] * c[0] + dir[1] * c[1] + dir[2] * c[2]) / r;
    lm[15] = 1.f;
}

void cv_sh_free(void) {
    if (S.kmade) {
        key_free();
        sg_destroy_view(S.mtex); sg_destroy_image(S.mimg); sg_destroy_sampler(S.kcmp);
        S.kmade = false;
    }
    if (!S.made) return;
    sg_destroy_view(S.dtex); sg_destroy_view(S.datt); sg_destroy_image(S.dimg);
    sg_destroy_view(S.rtex); sg_destroy_view(S.ratt); sg_destroy_image(S.rimg);
    sg_destroy_pipeline(S.pip); sg_destroy_shader(S.shd);
    sg_destroy_sampler(S.smp_near); sg_destroy_sampler(S.smp_lin);
    S.made = S.have = false;
}
