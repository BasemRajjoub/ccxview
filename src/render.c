/* render.c -- sokol_gfx pipelines, buffers and colormaps (GL 4.1 core). */
#include "render.h"
#include "sokol_gfx.h"
#include <math.h>

void cv_gl_enable_point_size(void);   /* sokol_impl.c: glEnable(GL_PROGRAM_POINT_SIZE) */

/* ---- colormaps ---------------------------------------------------------------
   Fast and CoolWarm: Moreland's published tables (kennethmoreland.com/color-advice).
   Viridis, Inferno and Turbo: the usual polynomial fits. Rainbow and Jet last, on
   purpose; later maps are appended so saved indices keep their meaning. */

const char* const cv_cmap_names[CV_CMAP_N] = { "Fast", "Cool-warm", "Viridis", "Turbo", "Heat", "Rainbow", "Jet", "Inferno",
                                               "Rainbow desat." };

static const float kFast[32][3] = {
    {0.0549f,0.0549f,0.4706f},{0.1098f,0.1373f,0.5333f},{0.1490f,0.2118f,0.5961f},{0.1843f,0.2863f,0.6588f},
    {0.2118f,0.3608f,0.7216f},{0.2353f,0.4353f,0.7882f},{0.2667f,0.5098f,0.8353f},{0.2980f,0.5804f,0.8706f},
    {0.3255f,0.6510f,0.9098f},{0.3529f,0.7255f,0.9451f},{0.4314f,0.7765f,0.9490f},{0.5176f,0.8235f,0.9412f},
    {0.5922f,0.8667f,0.9294f},{0.6627f,0.9137f,0.9216f},{0.7569f,0.9333f,0.8745f},{0.8549f,0.9412f,0.8039f},
    {0.9137f,0.9255f,0.7216f},{0.9373f,0.8824f,0.6275f},{0.9529f,0.8431f,0.5333f},{0.9529f,0.7882f,0.4667f},
    {0.9451f,0.7333f,0.4157f},{0.9373f,0.6745f,0.3608f},{0.9255f,0.6157f,0.3098f},{0.8980f,0.5529f,0.2745f},
    {0.8667f,0.4941f,0.2392f},{0.8392f,0.4314f,0.2039f},{0.8078f,0.3686f,0.1686f},{0.7686f,0.3137f,0.1529f},
    {0.7216f,0.2627f,0.1451f},{0.6784f,0.2078f,0.1373f},{0.6353f,0.1490f,0.1294f},{0.5882f,0.0784f,0.1176f},
};
static const float kCoolWarm[32][3] = {
    {0.2314f,0.2980f,0.7529f},{0.2667f,0.3569f,0.8039f},{0.3059f,0.4118f,0.8471f},{0.3451f,0.4627f,0.8863f},
    {0.3882f,0.5176f,0.9216f},{0.4314f,0.5647f,0.9490f},{0.4745f,0.6118f,0.9725f},{0.5216f,0.6588f,0.9882f},
    {0.5647f,0.6980f,0.9961f},{0.6078f,0.7373f,1.0000f},{0.6549f,0.7686f,0.9961f},{0.6980f,0.8000f,0.9843f},
    {0.7373f,0.8235f,0.9686f},{0.7765f,0.8431f,0.9451f},{0.8157f,0.8549f,0.9176f},{0.8510f,0.8627f,0.8863f},
    {0.8824f,0.8588f,0.8431f},{0.9137f,0.8392f,0.7961f},{0.9373f,0.8118f,0.7451f},{0.9529f,0.7843f,0.6980f},
    {0.9647f,0.7490f,0.6471f},{0.9686f,0.7059f,0.5961f},{0.9686f,0.6627f,0.5451f},{0.9608f,0.6157f,0.4941f},
    {0.9490f,0.5647f,0.4431f},{0.9294f,0.5098f,0.3961f},{0.9020f,0.4510f,0.3490f},{0.8745f,0.3882f,0.3059f},
    {0.8392f,0.3216f,0.2627f},{0.8000f,0.2471f,0.2235f},{0.7529f,0.1608f,0.1843f},{0.7059f,0.0157f,0.1490f},
};

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

void cv_colormap_rgb(int cm, float t, float o[3]) {
    t = clamp01(t);
    switch (cm) {
        case CV_CMAP_FAST: case CV_CMAP_COOLWARM: {
            const float (*tb)[3] = cm == CV_CMAP_FAST ? kFast : kCoolWarm;
            float x = t * 31.f;
            int i = (int)x; if (i > 30) i = 30;
            float f = x - (float)i;
            for (int k = 0; k < 3; k++) o[k] = tb[i][k] + (tb[i + 1][k] - tb[i][k]) * f;
            return;
        }
        case CV_CMAP_VIRIDIS: case CV_CMAP_INFERNO: {
            static const float cv[7][3] = {
                {0.2777273f, 0.0054073f, 0.3340998f}, {0.1050930f, 1.4046135f, 1.3845902f},
                {-0.3308618f, 0.2148476f, 0.0950952f}, {-4.6342305f, -5.7991010f, -19.3324410f},
                {6.2282699f, 14.1799334f, 56.6905526f}, {4.7763850f, -13.7451454f, -65.3530326f},
                {-5.4354559f, 4.6458526f, 26.3124352f},
            };
            static const float ci[7][3] = {
                {0.0002189f, 0.0016510f, -0.0194809f}, {0.1065134f, 0.5639564f, 3.9327124f},
                {11.6024931f, -3.9728540f, -15.9423941f}, {-41.7039961f, 17.4363989f, 44.3541452f},
                {77.1629357f, -33.4023589f, -81.8073093f}, {-71.3194282f, 32.6260643f, 73.2095199f},
                {25.1311262f, -12.2426690f, -23.0703250f},
            };
            const float (*c)[3] = cm == CV_CMAP_VIRIDIS ? cv : ci;
            for (int k = 0; k < 3; k++) {
                float v = c[6][k];
                for (int j = 5; j >= 0; j--) v = c[j][k] + t * v;
                o[k] = clamp01(v);
            }
            return;
        }
        case CV_CMAP_TURBO: {
            float x = t, x2 = x * x, x3 = x2 * x, x4 = x2 * x2, x5 = x4 * x;
            o[0] = clamp01(0.13572138f + 4.61539260f * x - 42.66032258f * x2 + 132.13108234f * x3 - 152.94239396f * x4 + 59.28637943f * x5);
            o[1] = clamp01(0.09140261f + 2.19418839f * x + 4.84296658f * x2 - 14.18503333f * x3 + 4.27729857f * x4 + 2.82956604f * x5);
            o[2] = clamp01(0.10667330f + 12.64194608f * x - 60.58204836f * x2 + 110.36276771f * x3 - 89.90310912f * x4 + 27.34824973f * x5);
            return;
        }
        case CV_CMAP_HEAT: {                    /* black - red - yellow - white, for temperatures */
            static const float r[5][3] = { {0.05f,0,0.1f}, {0.6f,0.05f,0.05f}, {0.95f,0.4f,0}, {1,0.85f,0.2f}, {1,1,0.95f} };
            float s = t * 4.f;
            int i = (int)s; if (i > 3) i = 3;
            float f = s - (float)i;
            for (int k = 0; k < 3; k++) o[k] = r[i][k] + (r[i + 1][k] - r[i][k]) * f;
            return;
        }
        case CV_CMAP_JET: {                     /* MATLAB jet: dark blue - blue - cyan - yellow - red - dark red */
            float x = 4.f * t;
            o[0] = clamp01(fminf(x - 1.5f, 4.5f - x));
            o[1] = clamp01(fminf(x - 0.5f, 3.5f - x));
            o[2] = clamp01(fminf(x + 0.5f, 2.5f - x));
            return;
        }
        case CV_CMAP_RAINBOW_DESAT: {           /* ParaView's: the rainbow with darker, calmer ends */
            static const float r[8][3] = {
                {0.2784f,0.2784f,0.8588f}, {0.0f,0.0f,0.3608f}, {0.0f,1.0f,1.0f}, {0.0f,0.5020f,0.0f},
                {1.0f,1.0f,0.0f}, {1.0f,0.3804f,0.0f}, {0.4196f,0.0f,0.0f}, {0.8784f,0.3020f,0.3020f},
            };
            float s = t * 7.f;
            int i = (int)s; if (i > 6) i = 6;
            float f = s - (float)i;
            for (int k = 0; k < 3; k++) o[k] = r[i][k] + (r[i + 1][k] - r[i][k]) * f;
            return;
        }
        default: {
            static const float r[5][3] = { {0,0,1}, {0,1,1}, {0,1,0}, {1,1,0}, {1,0,0} };
            float s = t * 4.f;
            int i = (int)s; if (i > 3) i = 3;
            float f = s - (float)i;
            for (int k = 0; k < 3; k++) o[k] = r[i][k] + (r[i + 1][k] - r[i][k]) * f;
        }
    }
}

/* ---- shader ------------------------------------------------------------------ */

/* Desktop GL and WebGL2 (GLSL ES 3.00) share the source; only the header differs.
   Varyings match by name, so they carry no layout(location). */
#ifdef __EMSCRIPTEN__
#define GLSL_HDR "#version 300 es\nprecision highp float; precision highp int; precision highp sampler2D;\n"
#else
#define GLSL_HDR "#version 410\n"
#endif

static const char* kVS =
    GLSL_HDR
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_mv;\n"
    "uniform vec4 u_p;\n"                    /* x: deform scale, y: point size, z: on top, w: pull */
    "uniform vec4 u_q;\n"                    /* x: P[1][1] * viewport height (px -> view units), y: scale of a_disp2 */
    "in vec3 a_pos;\n"
    "in vec3 a_disp;\n"
    "in float a_scal;\n"
    "in vec3 a_disp2;\n"
    "out vec3 v_vpos;\n"
    "out float v_s;\n"
    "out float v_r;\n"                       /* point: sphere radius in view units */
    "out vec3 v_wpos;\n"                     /* deformed model-space position, for the clip plane */
    "void main() {\n"
    "  vec3 p = a_pos + a_disp * u_p.x + a_disp2 * u_q.y;\n"
    "  gl_Position = u_mvp * vec4(p, 1.0);\n"
    /* "on top": squeeze depth into the front 2% of the range, so these points
       pass in front of the faces yet still hide one another near-to-far */
    "  if (u_p.z > 0.5) gl_Position.z = -gl_Position.w + (gl_Position.z + gl_Position.w) * 0.02;\n"
    /* edges lie ON faces: pulled a hair toward the eye so they win against them */
    "  gl_Position.z -= u_p.w * gl_Position.w;\n"
    "  v_r = u_p.y * gl_Position.w / max(u_q.x, 1e-6);\n"
    "  v_vpos = (u_mv * vec4(p, 1.0)).xyz;\n"
    "  v_s = a_scal;\n"
    "  v_wpos = p;\n"
    "  gl_PointSize = u_p.y;\n"
    "}\n";

static const char* kFS =
    GLSL_HDR
    "uniform vec4 u_color;\n"                /* solid colour */
    "uniform vec4 u_rng;\n"                  /* min, max, bands, mode */
    "uniform vec4 u_flags;\n"                /* x: grey out of range (light above, dark below), y: shade, z: on top */
    "uniform vec4 u_pz;\n"                   /* projection: z_clip = x*z + y, w_clip = z*z + w */
    "uniform vec4 u_clip;\n"                 /* clip plane normal, d; w = 1e30 when off */
    "uniform sampler2D u_cmap;\n"
    "uniform sampler2D u_etex;\n"
    "in vec3 v_vpos;\n"
    "in float v_s;\n"
    "in float v_r;\n"
    "in vec3 v_wpos;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "  if (dot(v_wpos, u_clip.xyz) > u_clip.w) discard;\n"
    /* Points are balls: each disc pixel takes the depth of the ball's surface there,
       so a point behind a face is hidden, one crossing it shows only its front part,
       and one in front shows whole. */
    "#ifdef SPHERE\n"
    "  vec2 pc = gl_PointCoord * 2.0 - 1.0;\n"
    "  float d2 = dot(pc, pc);\n"
    "  if (d2 > 1.0) discard;\n"
    "  float nz = sqrt(1.0 - d2);\n"
    "  float vz = v_vpos.z + nz * v_r;\n"
    "  float zd = (u_pz.x * vz + u_pz.y) / (u_pz.z * vz + u_pz.w) * 0.5 + 0.5;\n"
    "  gl_FragDepth = u_flags.z > 0.5 ? zd * 0.02 : zd;\n"
    "#endif\n"
    "  int mode = int(u_rng.w + 0.5);\n"
    "  vec3 c = u_color.rgb;\n"
    "  if (false) {\n"
    "  } else if (mode > 0) {\n"
    "    float s = v_s;\n"
    "#ifdef PRIM\n"
    "    if (mode == 2) {\n"
    "      int id = gl_PrimitiveID;\n"
    "      s = texelFetch(u_etex, ivec2(id & 4095, id >> 12), 0).r;\n"
    "    }\n"
    "#endif\n"
    "    if (isnan(s)) {\n"
    "      c = vec3(0.62, 0.62, 0.60);\n"      /* no data */
    "    } else {\n"
    "      float t = (s - u_rng.x) / max(u_rng.y - u_rng.x, 1e-30);\n"
    "      float t0 = t;\n"
    "      t = clamp(t, 0.0, 1.0);\n"
    /* keep in step with cv_band_center() in field.h */
    "      if (u_rng.z > 0.5) { float b = u_rng.z; t = (min(floor(t * b), b - 1.0) + 0.5) / b; }\n"
    "      c = textureLod(u_cmap, vec2(t, 0.5), 0.0).rgb;\n"
    "      if (u_flags.x > 0.5 && t0 > 1.0001) c = vec3(0.85);\n"      /* CV_OOR_ABOVE */
    "      if (u_flags.x > 0.5 && t0 < -1e-4) c = vec3(0.48);\n"       /* CV_OOR_BELOW */
    "    }\n"
    "  }\n"
    "  if (u_flags.y > 0.5) {\n"
    "    vec3 n = normalize(cross(dFdx(v_vpos), dFdy(v_vpos)));\n"
    "    c *= 0.30 + 0.70 * abs(n.z);\n"
    "  }\n"
    "#ifdef SPHERE\n"
    "  c *= 0.72 + 0.28 * nz;\n"               /* a touch of rim darkening: reads as a ball */
    "#endif\n"
    "  frag = vec4(c, 1.0);\n"
    "}\n";

typedef struct { float mvp[16]; float mv[16]; float p[4]; float q[4]; } vs_params;
typedef struct { float color[4]; float rng[4]; float flags[4]; float pz[4]; float clip[4]; } fs_params;

/* ---- state ------------------------------------------------------------------- */

enum { ETEX_W = 4096 };

static struct {
    sg_shader   shd, shd_prim, shd_pt;
    sg_pipeline pip_tri, pip_tri_prim, pip_line, pip_pt;
    sg_pipeline pip_tri_ni, pip_line_ni, pip_pt_ni;   /* non-indexed: the aux vertex sets */
    sg_image    cmap_img;  sg_view cmap_view;
    sg_image    etex_img;  sg_view etex_view;
    sg_buffer   ib_grp;               /* skin triangles ordered by group */
    uint32_t*   grp_first;            /* ngroups + 1 */
    float*      grp_rgb;              /* 3 per group */
    int         ngroups;
    sg_sampler  smp_lin, smp_near;
    sg_buffer   pos, disp, disp2, scal;
    sg_buffer   ib_tri, ib_edge, ib_pt;
    size_t      n_tri, n_edge, n_pt;
    uint32_t    n_nodes;
} R;

static void kill_buf(sg_buffer* b) {
    if (b->id) sg_destroy_buffer(*b);
    b->id = 0;
}

static sg_buffer make_buf(const void* p, size_t bytes, bool index) {
    if (!p || bytes == 0) return (sg_buffer){0};
    sg_buffer_desc d = { .data = { p, bytes } };
    if (index) { d.usage.index_buffer = true; d.usage.vertex_buffer = false; }
    else d.usage.vertex_buffer = true;
    return sg_make_buffer(&d);
}

void cv_render_groups(const uint32_t* tri, size_t n_tri, const uint32_t* first, const float* rgb, int ng) {
    kill_buf(&R.ib_grp);
    free(R.grp_first); free(R.grp_rgb);
    R.grp_first = NULL; R.grp_rgb = NULL; R.ngroups = 0;
    if (!tri || !n_tri || ng <= 0) return;
    R.grp_first = malloc(((size_t)ng + 1) * sizeof(uint32_t));
    R.grp_rgb = malloc((size_t)ng * 3 * sizeof(float));
    if (!R.grp_first || !R.grp_rgb) { free(R.grp_first); free(R.grp_rgb); R.grp_first = NULL; R.grp_rgb = NULL; return; }
    memcpy(R.grp_first, first, ((size_t)ng + 1) * sizeof(uint32_t));
    memcpy(R.grp_rgb, rgb, (size_t)ng * 3 * sizeof(float));
    R.ib_grp = make_buf(tri, n_tri * 12, true);
    R.ngroups = R.ib_grp.id ? ng : 0;
}

static void make_etex(const float* v, int w, int h) {
    if (R.etex_view.id) sg_destroy_view(R.etex_view);
    if (R.etex_img.id) sg_destroy_image(R.etex_img);
    R.etex_img = sg_make_image(&(sg_image_desc){
        .width = w, .height = h, .pixel_format = SG_PIXELFORMAT_R32F,
        .data.mip_levels[0] = { v, (size_t)w * (size_t)h * sizeof(float) },
    });
    R.etex_view = sg_make_view(&(sg_view_desc){ .texture.image = R.etex_img });
}

void cv_render_colormap(int cm, bool reverse, bool grey) {
    uint8_t px[256 * 4];
    for (int i = 0; i < 256; i++) {
        float c[3];
        cv_colormap_rgb(cm, reverse ? 1.f - (float)i / 255.f : (float)i / 255.f, c);
        if (grey) c[0] = c[1] = c[2] = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
        for (int k = 0; k < 3; k++) px[4 * i + k] = (uint8_t)(c[k] * 255.f + 0.5f);
        px[4 * i + 3] = 255;
    }
    if (R.cmap_view.id) sg_destroy_view(R.cmap_view);
    if (R.cmap_img.id) sg_destroy_image(R.cmap_img);
    R.cmap_img = sg_make_image(&(sg_image_desc){
        .width = 256, .height = 1, .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = { px, sizeof px },
    });
    R.cmap_view = sg_make_view(&(sg_view_desc){ .texture.image = R.cmap_img });
}

static sg_shader make_shader(const char* fs_src) {
    return sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = kVS,
        .fragment_func.source = fs_src,
        .attrs = {
            [0] = { .glsl_name = "a_pos" },
            [1] = { .glsl_name = "a_disp" },
            [2] = { .glsl_name = "a_scal" },
            [3] = { .glsl_name = "a_disp2" },
        },
        .uniform_blocks[0] = {
            .stage = SG_SHADERSTAGE_VERTEX, .size = sizeof(vs_params),
            .glsl_uniforms = {
                [0] = { .type = SG_UNIFORMTYPE_MAT4, .glsl_name = "u_mvp" },
                [1] = { .type = SG_UNIFORMTYPE_MAT4, .glsl_name = "u_mv" },
                [2] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_p" },
                [3] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_q" },
            },
        },
        .uniform_blocks[1] = {
            .stage = SG_SHADERSTAGE_FRAGMENT, .size = sizeof(fs_params),
            .glsl_uniforms = {
                [0] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_color" },
                [1] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_rng" },
                [2] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_flags" },
                [3] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_pz" },
                [4] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_clip" },
            },
        },
        .views = {
            [0].texture = { .stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D,
                            .sample_type = SG_IMAGESAMPLETYPE_FLOAT },
            [1].texture = { .stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D,
                            .sample_type = SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT },
        },
        .samplers = {
            [0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .sampler_type = SG_SAMPLERTYPE_FILTERING },
            [1] = { .stage = SG_SHADERSTAGE_FRAGMENT, .sampler_type = SG_SAMPLERTYPE_NONFILTERING },
        },
        .texture_sampler_pairs = {
            [0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .view_slot = 0, .sampler_slot = 0, .glsl_name = "u_cmap" },
            [1] = { .stage = SG_SHADERSTAGE_FRAGMENT, .view_slot = 1, .sampler_slot = 1, .glsl_name = "u_etex" },
        },
        .label = "ccxview",
    });
}

void cv_render_init(void) {
    memset(&R, 0, sizeof R);
    cv_gl_enable_point_size();
    /* Three variants of one source. Only the per-element and group-colour modes
       read gl_PrimitiveID: under Mesa's llvmpipe (VMs, remote desktops, Xvfb) a
       fragment shader that reads it gets its varyings scrambled, which turned
       every contour a single colour. Keeping it out of the default shader keeps
       the everyday view correct on every driver. */
    const char* body = kFS + strlen(GLSL_HDR);
    R.shd = make_shader(kFS);
#ifdef __EMSCRIPTEN__
    R.shd_prim = R.shd;                       /* WebGL2 has no gl_PrimitiveID; the expanded stream is used instead */
#else
    {
        static char prim[8192];
        snprintf(prim, sizeof prim, "%s#define PRIM 1\n%s", GLSL_HDR, body);
        R.shd_prim = make_shader(prim);
    }
#endif
    /* the point variant: same source with SPHERE defined after the header */
    {
        static char sph[8192];
        snprintf(sph, sizeof sph, "%s#define SPHERE\n%s", GLSL_HDR, body);
        R.shd_pt = make_shader(sph);
    }

    sg_vertex_layout_state layout = {
        .buffers = { [0].stride = 12, [1].stride = 12, [2].stride = 4, [3].stride = 12 },
        .attrs = {
            [0] = { .buffer_index = 0, .format = SG_VERTEXFORMAT_FLOAT3 },
            [1] = { .buffer_index = 1, .format = SG_VERTEXFORMAT_FLOAT3 },
            [2] = { .buffer_index = 2, .format = SG_VERTEXFORMAT_FLOAT },
            [3] = { .buffer_index = 3, .format = SG_VERTEXFORMAT_FLOAT3 },
        },
    };
    sg_pipeline_desc pd = {
        .shader = R.shd,
        .layout = layout,
        .index_type = SG_INDEXTYPE_UINT32,
        .depth = { .compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true },
    };
    pd.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    R.pip_tri = sg_make_pipeline(&pd);        /* faces keep their true depth */
    pd.shader = R.shd_prim;
    R.pip_tri_prim = sg_make_pipeline(&pd);   /* per-element / group colours */
    pd.shader = R.shd;
    pd.primitive_type = SG_PRIMITIVETYPE_LINES;
    R.pip_line = sg_make_pipeline(&pd);
    pd.primitive_type = SG_PRIMITIVETYPE_POINTS;
    pd.shader = R.shd_pt;                     /* points are drawn as balls */
    R.pip_pt = sg_make_pipeline(&pd);
    /* the aux sets are plain vertex runs: no index buffer to build or upload */
    pd.index_type = SG_INDEXTYPE_NONE;
    R.pip_pt_ni = sg_make_pipeline(&pd);
    pd.primitive_type = SG_PRIMITIVETYPE_LINES;  pd.shader = R.shd;
    R.pip_line_ni = sg_make_pipeline(&pd);
    pd.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    R.pip_tri_ni = sg_make_pipeline(&pd);

    R.smp_lin = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR, .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE });
    R.smp_near = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST, .mag_filter = SG_FILTER_NEAREST,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE });
    cv_render_colormap(CV_CMAP_FAST, false, false);
    float zero = 0;
    make_etex(&zero, 1, 1);
}

static void clear_aux(void);

void cv_render_clear_model(void) {
    clear_aux();
    kill_buf(&R.pos); kill_buf(&R.disp); kill_buf(&R.disp2); kill_buf(&R.scal);
    kill_buf(&R.ib_tri); kill_buf(&R.ib_edge); kill_buf(&R.ib_pt);
    R.n_tri = R.n_edge = R.n_pt = 0;
    R.n_nodes = 0;
}

void cv_render_shutdown(void) {
    cv_render_clear_model();
}

void cv_render_positions(const float* xyz, uint32_t n) {
    kill_buf(&R.pos);
    R.pos = make_buf(xyz, (size_t)n * 12, false);
    R.n_nodes = n;
}

void cv_render_displacement(const float* d, uint32_t n) {
    kill_buf(&R.disp);
    R.disp = make_buf(d, (size_t)n * 12, false);
}

void cv_render_displacement2(const float* d, uint32_t n) {
    kill_buf(&R.disp2);
    R.disp2 = make_buf(d, (size_t)n * 12, false);
}

void cv_render_scalar(const float* s, uint32_t n) {
    kill_buf(&R.scal);
    R.scal = make_buf(s, (size_t)n * 4, false);
}

void cv_render_tri_values(const float* v, size_t n_tri) {
    if (!v || n_tri == 0) { float z = 0; make_etex(&z, 1, 1); return; }
    int w = ETEX_W, h = (int)((n_tri + ETEX_W - 1) / ETEX_W);
    CV_ASSERT(h <= 16384);                       /* GL's usual texture limit: 67M triangles */
    size_t cells = (size_t)w * (size_t)h;
    if (cells == n_tri) { make_etex(v, w, h); return; }
    float* pad = malloc(cells * sizeof(float));
    if (!pad) { float z = 0; make_etex(&z, 1, 1); return; }
    memcpy(pad, v, n_tri * sizeof(float));
    for (size_t i = n_tri; i < cells; i++) pad[i] = 0;
    make_etex(pad, w, h);
    free(pad);
}

void cv_render_indices(const uint32_t* tri, size_t n_tri, const uint32_t* edge, size_t n_edge,
                       const uint32_t* pt, size_t n_pt) {
    kill_buf(&R.ib_tri); kill_buf(&R.ib_edge); kill_buf(&R.ib_pt);
    R.ib_tri = make_buf(tri, n_tri * 12, true);   R.n_tri = R.ib_tri.id ? n_tri : 0;
    R.ib_edge = make_buf(edge, n_edge * 8, true); R.n_edge = R.ib_edge.id ? n_edge : 0;
    R.ib_pt = make_buf(pt, n_pt * 4, true);       R.n_pt = R.ib_pt.id ? n_pt : 0;
}

typedef struct { sg_buffer pos, disp, scal, disp2; } vset;

/* depth pull (NDC) for edges, which lie on faces; far too small to reach through a wall */
#define PULL 2e-5f

static void draw_layer(sg_pipeline pip, vset v, sg_buffer ib, int count, int mode, const float rgb[3],
                       bool shade, const cv_draw* d, float point_size, bool on_top, float pull, int first) {
    if (!v.pos.id || count <= 0) return;      /* ib is {0} for the non-indexed pipelines */
    /* no scalar buffer = nothing to colour with: fall back to solid */
    if (!v.scal.id && (mode == CV_COLOR_NODAL || mode == CV_COLOR_ELEM)) mode = CV_COLOR_SOLID;
    sg_apply_pipeline(pip);
    sg_bindings b = {
        .vertex_buffers = {
            [0] = v.pos,
            [1] = v.disp.id ? v.disp : v.pos,      /* no displacement: scale is 0 */
            [2] = v.scal.id ? v.scal : v.pos,
            [3] = v.disp2.id ? v.disp2 : v.pos,    /* no second part: its scale is 0 */
        },
        .index_buffer = ib,
        .views = { [0] = R.cmap_view, [1] = R.etex_view },
        .samplers = { [0] = R.smp_lin, [1] = R.smp_near },
    };
    sg_apply_bindings(&b);
    vs_params vs;
    memcpy(vs.mvp, d->mvp, sizeof vs.mvp);
    memcpy(vs.mv, d->mv, sizeof vs.mv);
    vs.p[0] = v.disp.id ? d->def_scale : 0.f;
    vs.p[1] = point_size;
    vs.p[2] = on_top ? 1.f : 0.f;
    vs.p[3] = pull;
    vs.q[0] = d->proj[5] * (float)d->vp_h;      /* pixels -> view units for the ball radius */
    vs.q[1] = v.disp2.id ? d->def_scale2 : 0.f;
    vs.q[2] = vs.q[3] = 0;
    sg_apply_uniforms(0, &SG_RANGE(vs));
    fs_params fs = {
        .color = { rgb[0], rgb[1], rgb[2], 1 },
        .rng = { d->rmin, d->rmax, (float)d->bands, (float)mode },
        .flags = { d->grey_out_of_range ? 1.f : 0.f, shade ? 1.f : 0.f, on_top ? 1.f : 0.f, 0 },
        .pz = { d->proj[10], d->proj[14], d->proj[11], d->proj[15] },
        .clip = { d->clip ? d->clip_n[0] : 0, d->clip ? d->clip_n[1] : 0, d->clip ? d->clip_n[2] : 0, d->clip ? d->clip_d : 1e30f },
    };
    sg_apply_uniforms(1, &SG_RANGE(fs));
    sg_draw(first, count, 1);
}

/* ---- auxiliary vertex sets: Gauss points, highlights, cgx geometry ------------ */

static struct { vset v; uint32_t n; } A[CV_AUX_N];
static const sg_buffer NO_IB = {0};

void cv_render_aux(int which, const float* pos, const float* disp, const float* scal, uint32_t n) {
    cv_render_aux2(which, pos, disp, NULL, scal, n);
}

void cv_render_aux2(int which, const float* pos, const float* disp, const float* disp2, const float* scal, uint32_t n) {
    if (which < 0 || which >= CV_AUX_N) return;
    kill_buf(&A[which].v.pos); kill_buf(&A[which].v.disp); kill_buf(&A[which].v.scal); kill_buf(&A[which].v.disp2);
    A[which].n = 0;
    if (!pos || n == 0) return;
    A[which].v.pos = make_buf(pos, (size_t)n * 12, false);
    A[which].v.disp = make_buf(disp, (size_t)n * 12, false);
    A[which].v.disp2 = make_buf(disp2, (size_t)n * 12, false);
    A[which].v.scal = make_buf(scal, (size_t)n * 4, false);
    A[which].n = A[which].v.pos.id ? n : 0;
}

void cv_render_draw(const cv_draw* d) {
    if (d->vp_w <= 0 || d->vp_h <= 0) return;
    sg_apply_viewport(d->vp_x, d->vp_y, d->vp_w, d->vp_h, true);
    sg_apply_scissor_rect(d->vp_x, d->vp_y, d->vp_w, d->vp_h, true);
    vset mesh = { R.pos, R.disp, R.scal, R.disp2 };
    if (d->faces)
    {
        if (d->faces_color == CV_COLOR_GROUP && R.ngroups) {
            /* one solid-colour run per group: no per-triangle lookup at all */
            for (int g = 0; g < R.ngroups; g++) {
                int first = (int)R.grp_first[g], n = (int)(R.grp_first[g + 1] - R.grp_first[g]);
                if (n > 0)
                    draw_layer(R.pip_tri, mesh, R.ib_grp, n * 3, CV_COLOR_SOLID, R.grp_rgb + 3 * g,
                               d->shade, d, 1, false, 0.f, first * 3);
            }
        } else if (d->faces_color == CV_COLOR_ELEM && A[CV_AUX_ELEMTRI].n) {
            /* flat per-element colours from the expanded triangle stream */
            draw_layer(R.pip_tri_ni, A[CV_AUX_ELEMTRI].v, NO_IB, (int)A[CV_AUX_ELEMTRI].n,
                       CV_COLOR_NODAL, d->face_rgb, d->shade, d, 1, false, 0.f, 0);
        } else {
            draw_layer(d->faces_color == CV_COLOR_ELEM ? R.pip_tri_prim : R.pip_tri, mesh, R.ib_tri,
                       (int)(R.n_tri * 3), d->faces_color == CV_COLOR_GROUP ? CV_COLOR_SOLID : d->faces_color,
                       d->face_rgb, d->shade, d, 1, false, 0.f, 0);
        }
    }
    if (d->faces && d->clip && A[CV_AUX_CAPTRI].n) {   /* the cut filled, coloured like the faces */
        int m = d->faces_color == CV_COLOR_NODAL || d->faces_color == CV_COLOR_ELEM ? CV_COLOR_NODAL : CV_COLOR_SOLID;
        draw_layer(R.pip_tri_ni, A[CV_AUX_CAPTRI].v, NO_IB, (int)A[CV_AUX_CAPTRI].n, m, d->face_rgb, d->shade, d, 1, false, 0.f, 0);
    }
    if (d->ghost && d->def_scale != 0.f) {   /* the shape before deformation, faint */
        static const float ghost_rgb[3] = { 0.55f, 0.55f, 0.55f };
        cv_draw g = *d;
        g.def_scale = g.def_scale2 = 0.f;
        vset undeformed = { R.pos, {0}, {0}, {0} };
        draw_layer(R.pip_line, undeformed, R.ib_edge, (int)(R.n_edge * 2), CV_COLOR_SOLID, ghost_rgb, false, &g, 1, false, PULL, 0);
    }
    if (d->edges) {
        int m = d->edges_color == CV_COLOR_ELEM ? CV_COLOR_NODAL : d->edges_color;
        draw_layer(R.pip_line, mesh, R.ib_edge, (int)(R.n_edge * 2), m, d->edge_rgb, false, d, 1, false, PULL, 0);
    }
    if (d->points) {
        int m = d->points_color == CV_COLOR_ELEM ? CV_COLOR_NODAL : d->points_color;
        draw_layer(R.pip_pt, mesh, R.ib_pt, (int)R.n_pt, m, d->point_rgb, false, d, d->point_size, false, 0.f, 0);
    }
    if (d->path && A[CV_AUX_PATHLN].n) {     /* the plot line, on top */
        static const float path_rgb[3] = { 1.0f, 0.55f, 0.1f };
        draw_layer(R.pip_line_ni, A[CV_AUX_PATHLN].v, NO_IB, (int)A[CV_AUX_PATHLN].n, CV_COLOR_SOLID, path_rgb, false, d, 1, true, 0.f, 0);
    }
    if (d->markers && A[CV_AUX_MARK].n)      /* min (first) and max (second), coloured by the field */
        draw_layer(R.pip_pt_ni, A[CV_AUX_MARK].v, NO_IB, (int)A[CV_AUX_MARK].n,
                   CV_COLOR_NODAL, d->point_rgb, false, d, d->marker_size, true, 0.f, 0);
    if (d->gauss_points && A[CV_AUX_GP].n)
        draw_layer(R.pip_pt_ni, A[CV_AUX_GP].v, NO_IB, (int)A[CV_AUX_GP].n,
                   d->gauss_points_color, d->gauss_rgb, false, d, d->gauss_size, d->gauss_on_top, 0.f, 0);
    if (d->highlights) {
        /* surfaces: their faces redrawn a hair in front; node sets: bright balls */
        static const float surf_rgb[3] = { 0.93f, 0.25f, 0.75f }, nset_rgb[3] = { 1.0f, 0.55f, 0.10f };
        if (A[CV_AUX_HLTRI].n)
            draw_layer(R.pip_tri_ni, A[CV_AUX_HLTRI].v, NO_IB, (int)A[CV_AUX_HLTRI].n,
                       CV_COLOR_SOLID, surf_rgb, d->shade, d, 1, false, PULL, 0);
        if (A[CV_AUX_HLPT].n)
            draw_layer(R.pip_pt_ni, A[CV_AUX_HLPT].v, NO_IB, (int)A[CV_AUX_HLPT].n,
                       CV_COLOR_SOLID, nset_rgb, false, d, d->hl_size, false, 0.f, 0);
    }
    {
        /* supports, loads, links and springs from the deck: line glyphs with their
           true depth, so faces in front hide them; pulled a hair toward the eye
           so a glyph lying on a face wins against it */
        static const float bc_rgb[3] = { 0.15f, 0.85f, 0.85f }, ld_rgb[3] = { 1.0f, 0.78f, 0.10f };
        if (d->supports && A[CV_AUX_BCLN].n)
            draw_layer(R.pip_line_ni, A[CV_AUX_BCLN].v, NO_IB, (int)A[CV_AUX_BCLN].n,
                       CV_COLOR_SOLID, bc_rgb, false, d, 1, false, 4 * PULL, 0);
        if (d->loads && A[CV_AUX_LDLN].n)
            draw_layer(R.pip_line_ni, A[CV_AUX_LDLN].v, NO_IB, (int)A[CV_AUX_LDLN].n,
                       CV_COLOR_SOLID, ld_rgb, false, d, 1, false, 4 * PULL, 0);
        static const float vec_rgb[3] = { 0.95f, 0.95f, 0.95f };
        if (d->vectors && A[CV_AUX_VECLN].n)
            draw_layer(R.pip_line_ni, A[CV_AUX_VECLN].v, NO_IB, (int)A[CV_AUX_VECLN].n,
                       d->vectors_color, vec_rgb, false, d, 1, false, 4 * PULL, 0);
        static const float link_rgb[3] = { 0.55f, 0.95f, 0.45f };
        if (d->links && A[CV_AUX_LINKLN].n)
            draw_layer(R.pip_line_ni, A[CV_AUX_LINKLN].v, NO_IB, (int)A[CV_AUX_LINKLN].n,
                       CV_COLOR_SOLID, link_rgb, false, d, 1, false, 4 * PULL, 0);
        static const float disc_rgb[3] = { 0.80f, 0.45f, 0.95f };
        if (d->discrete && A[CV_AUX_DISCLN].n)
            draw_layer(R.pip_line_ni, A[CV_AUX_DISCLN].v, NO_IB, (int)A[CV_AUX_DISCLN].n,
                       CV_COLOR_SOLID, disc_rgb, false, d, 1, false, 4 * PULL, 0);
    }
    {
        /* cgx geometry: surfaces as patches, curves pulled onto them, points as balls */
        static const float srf_rgb[3] = { 0.60f, 0.70f, 0.84f }, crv_rgb[3] = { 0.08f, 0.16f, 0.48f },
                           pnt_rgb[3] = { 0.85f, 0.18f, 0.12f };
        if (d->geo_surfaces && A[CV_AUX_GEOTRI].n)
            draw_layer(R.pip_tri_ni, A[CV_AUX_GEOTRI].v, NO_IB, (int)A[CV_AUX_GEOTRI].n,
                       CV_COLOR_SOLID, srf_rgb, d->shade, d, 1, false, 0.f, 0);
        if (d->geo_curves && A[CV_AUX_GEOLN].n)
            draw_layer(R.pip_line_ni, A[CV_AUX_GEOLN].v, NO_IB, (int)A[CV_AUX_GEOLN].n,
                       CV_COLOR_SOLID, crv_rgb, false, d, 1, false, 4 * PULL, 0);
        if (d->geo_points && A[CV_AUX_GEOPT].n)
            draw_layer(R.pip_pt_ni, A[CV_AUX_GEOPT].v, NO_IB, (int)A[CV_AUX_GEOPT].n,
                       CV_COLOR_SOLID, pnt_rgb, false, d, d->geo_size, false, 0.f, 0);
    }
}

static void clear_aux(void) {
    for (int i = 0; i < CV_AUX_N; i++) cv_render_aux(i, NULL, NULL, NULL, 0);
}
