/* render_ao.c -- screen-space ambient occlusion from the faces' depth (render_ao.h) */
#include "render_ao.h"
#include "base.h"

/* a triangle over the whole target, no vertex buffer */
static const char* kVS =
    GLSL_HDR
    "void main() {\n"
    "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* view-space position of a depth texel. u_p0: P[0] P[5] P[8] P[9]; u_p1: P[12] P[13]
   P[10] P[14]; u_p2.x: P[11] (-1 perspective, 0 parallel); u_sz: the depth's size */
#define AO_COMMON \
    "uniform vec4 u_p0;\n" \
    "uniform vec4 u_p1;\n" \
    "uniform vec4 u_p2;\n" \
    "uniform vec4 u_sz;\n" \
    "uniform sampler2D u_dep;\n" \
    "out vec4 frag;\n" \
    "float zview(float d) {\n" \
    "  float z = d * 2.0 - 1.0;\n" \
    "  return u_p2.x != 0.0 ? -u_p1.w / (z + u_p1.z) : (z - u_p1.w) / u_p1.z;\n" \
    "}\n" \
    "vec3 vp(ivec2 q) {\n" \
    "  q = clamp(q, ivec2(0), ivec2(u_sz.xy) - 1);\n" \
    "  float zv = zview(texelFetch(u_dep, q, 0).r);\n" \
    "  vec2 ndc = (vec2(q) + 0.5) / u_sz.xy * 2.0 - 1.0;\n" \
    "  float w = u_p2.x != 0.0 ? -zv : 1.0;\n" \
    "  return vec3((ndc.x * w - u_p0.z * zv - u_p1.x) / u_p0.x, (ndc.y * w - u_p0.w * zv - u_p1.y) / u_p0.y, zv);\n" \
    "}\n"

/* u_p2.y: radius (view units), z: full-resolution pixels per view unit at depth 1 */
static const char* kAO =
    GLSL_HDR
    AO_COMMON
    "void main() {\n"
    "  ivec2 q = ivec2(gl_FragCoord.xy) * 2;\n"
    "  if (texelFetch(u_dep, q, 0).r >= 1.0) { frag = vec4(1.0); return; }\n"   /* the background */
    "  vec3 p = vp(q);\n"
    /* the normal from the neighbours on the nearer side, so an outline does not bend it */
    "  vec3 a = vp(q + ivec2(1, 0)) - p, b = p - vp(q - ivec2(1, 0));\n"
    "  vec3 c = vp(q + ivec2(0, 1)) - p, d = p - vp(q - ivec2(0, 1));\n"
    "  vec3 n = normalize(cross(abs(a.z) < abs(b.z) ? a : b, abs(c.z) < abs(d.z) ? c : d));\n"
    "  vec3 v = u_p2.x != 0.0 ? -p : vec3(0.0, 0.0, 1.0);\n"
    "  if (dot(n, v) < 0.0) n = -n;\n"
    "  float ppu = u_p2.z / (u_p2.x != 0.0 ? max(-p.z, 1e-6) : 1.0);\n"
    "  float rpx = min(u_p2.y * ppu, 120.0);\n"          /* close up: no wider than this on screen */
    "  if (rpx < 2.0) { frag = vec4(1.0); return; }\n"
    "  float r = rpx / ppu, r2 = r * r;\n"
    "  float phi = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + 0.61803399 * u_sz.z);\n"
    "  float sum = 0.0;\n"
    "  for (int i = 0; i < 12; i++) {\n"
    "    float t = (float(i) + 0.5) / 12.0, ang = t * 6.2831853 * 3.0 + phi;\n"
    "    vec3 w = vp(q + ivec2(vec2(cos(ang), sin(ang)) * t * rpx)) - p;\n"
    "    float ww = dot(w, w);\n"
    "    sum += max(0.0, 1.0 - ww / r2) * max(0.0, dot(w, n) * inversesqrt(ww + 1e-30) - 0.30);\n"
    "  }\n"
    "  frag = vec4(clamp(1.0 - 3.0 * sum / 12.0, 0.0, 1.0));\n"
    "}\n";

/* 5 x 5 over the half-resolution result, only pixels within a fraction of the radius in depth */
static const char* kBlur =
    GLSL_HDR
    AO_COMMON
    "uniform sampler2D u_raw;\n"
    "void main() {\n"
    "  ivec2 h = ivec2(gl_FragCoord.xy), hs = ivec2(u_sz.xy) / 2 - 1;\n"
    "  float zc = zview(texelFetch(u_dep, h * 2, 0).r), s = 0.0, ws = 0.0;\n"
    "  float tol = 0.5 * u_p2.y;\n"
    "  for (int j = -2; j <= 2; j++)\n"
    "    for (int i = -2; i <= 2; i++) {\n"
    "      ivec2 k = clamp(h + ivec2(i, j), ivec2(0), hs);\n"
    "      float w = max(0.0, 1.0 - abs(zview(texelFetch(u_dep, k * 2, 0).r) - zc) / tol);\n"
    "      s += w * texelFetch(u_raw, k, 0).r; ws += w;\n"
    "    }\n"
    "  frag = vec4(ws > 0.0 ? s / ws : 1.0);\n"
    "}\n";

typedef struct { float p0[4], p1[4], p2[4], sz[4]; } ao_params;

static struct {
    sg_shader   shd[2];
    sg_pipeline pip[2];
    sg_sampler  smp;
    sg_image    img[2];  sg_view att[2], tex[2];   /* raw, blurred */
    int         w, h;                              /* the depth's size they were made for */
    bool        made;
    int         sample;
} AO;

static sg_shader shader(const char* fs, bool blur) {
    sg_shader_desc d = {
        .vertex_func.source = kVS,
        .fragment_func.source = fs,
        .uniform_blocks[0] = {
            .stage = SG_SHADERSTAGE_FRAGMENT, .size = sizeof(ao_params),
            .glsl_uniforms = {
                [0] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_p0" },
                [1] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_p1" },
                [2] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_p2" },
                [3] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_sz" },
            },
        },
        .views = {
            [0].texture = { .stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D, .sample_type = SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT },
            [1].texture = { .stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D, .sample_type = SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT },
        },
        .samplers[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .sampler_type = SG_SAMPLERTYPE_NONFILTERING },
        .texture_sampler_pairs = {
            [0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .view_slot = 0, .sampler_slot = 0, .glsl_name = "u_dep" },
            [1] = { .stage = SG_SHADERSTAGE_FRAGMENT, .view_slot = 1, .sampler_slot = 0, .glsl_name = "u_raw" },
        },
        .label = blur ? "ccxview-ao-blur" : "ccxview-ao",
    };
    if (!blur) { d.views[1].texture.stage = SG_SHADERSTAGE_NONE; d.texture_sampler_pairs[1].stage = SG_SHADERSTAGE_NONE; }
    return sg_make_shader(&d);
}

static void make(void) {
    AO.made = true;
    for (int k = 0; k < 2; k++) {
        AO.shd[k] = shader(k ? kBlur : kAO, k == 1);
        AO.pip[k] = sg_make_pipeline(&(sg_pipeline_desc){
            .shader = AO.shd[k],
            .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .colors[0].pixel_format = SG_PIXELFORMAT_R8,
            .depth.pixel_format = SG_PIXELFORMAT_NONE,
            .sample_count = 1,
        });
    }
    AO.smp = sg_make_sampler(&(sg_sampler_desc){ .min_filter = SG_FILTER_NEAREST, .mag_filter = SG_FILTER_NEAREST,
                                                .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE });
}

static void free_targets(void) {
    for (int k = 0; k < 2; k++) {
        if (AO.tex[k].id) sg_destroy_view(AO.tex[k]);
        if (AO.att[k].id) sg_destroy_view(AO.att[k]);
        if (AO.img[k].id) sg_destroy_image(AO.img[k]);
        AO.tex[k].id = AO.att[k].id = AO.img[k].id = 0;
    }
    AO.w = AO.h = 0;
}

void cv_ao_free(void) {
    free_targets();
    if (AO.made) {
        for (int k = 0; k < 2; k++) { sg_destroy_pipeline(AO.pip[k]); sg_destroy_shader(AO.shd[k]); }
        sg_destroy_sampler(AO.smp);
    }
    AO.made = false;
}

sg_view cv_ao_view(void) { return AO.tex[1]; }
void cv_ao_sample(int i) { AO.sample = i; }

void cv_ao_run(sg_view depth, int w, int h, const float P[16], float radius) {
    if (w < 4 || h < 4 || !depth.id) return;
    if (!AO.made) make();
    if (AO.w != w || AO.h != h) {
        free_targets();
        for (int k = 0; k < 2; k++) {
            AO.img[k] = sg_make_image(&(sg_image_desc){ .usage.color_attachment = true, .width = w / 2, .height = h / 2,
                                                        .pixel_format = SG_PIXELFORMAT_R8, .sample_count = 1 });
            AO.att[k] = sg_make_view(&(sg_view_desc){ .color_attachment.image = AO.img[k] });
            AO.tex[k] = sg_make_view(&(sg_view_desc){ .texture.image = AO.img[k] });
        }
        AO.w = w; AO.h = h;
    }
    ao_params u = {
        .p0 = { P[0], P[5], P[8], P[9] },
        .p1 = { P[12], P[13], P[10], P[14] },
        .p2 = { P[11], radius, 0.5f * P[0] * (float)w, 0 },
        .sz = { (float)w, (float)h, (float)AO.sample, 0 },
    };
    for (int k = 0; k < 2; k++) {
        sg_begin_pass(&(sg_pass){ .action.colors[0] = { .load_action = SG_LOADACTION_DONTCARE },
                                  .attachments.colors[0] = AO.att[k] });
        sg_apply_pipeline(AO.pip[k]);
        sg_bindings b = { .views = { [0] = depth, [1] = k ? AO.tex[0] : (sg_view){0} }, .samplers[0] = AO.smp };
        sg_apply_bindings(&b);
        sg_apply_uniforms(0, &SG_RANGE(u));
        sg_draw(0, 3, 1);
        sg_end_pass();
    }
}
