/* render.c -- sokol_gfx pipelines, buffers and colormaps (GL 4.1 core). */
#include "render.h"
#include "sokol_gfx.h"
#include "gpu.h"
#include <math.h>

void cv_gl_enable_point_size(void);   /* sokol_impl.c: glEnable(GL_PROGRAM_POINT_SIZE) */

/* ---- colormaps ---------------------------------------------------------------
   Fast and CoolWarm: Moreland's published tables (kennethmoreland.com/color-advice).
   Viridis, Inferno and Turbo: the usual polynomial fits. Rainbow and Jet last, on
   purpose; later maps are appended so saved indices keep their meaning. */

const char* const cv_cmap_names[CV_CMAP_N] = { "Fast", "Cool-warm", "Viridis", "Turbo", "Heat", "Rainbow", "Jet", "Inferno",
                                               "Rainbow desat.", "Cividis", "Plasma", "Black body", "Kindlmann", "Warm", "Cool" };

/* a map from a few colours evenly spaced over 0 .. 1, linear between them */
static void map_pts(const float (*r)[3], int n, float t, float o[3]) {
    float s = t * (float)(n - 1);
    int i = (int)s; if (i > n - 2) i = n - 2;
    float f = s - (float)i;
    for (int k = 0; k < 3; k++) o[k] = r[i][k] + (r[i + 1][k] - r[i][k]) * f;
}

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
        case CV_CMAP_CIVIDIS: {                 /* Nunez, Anderton, Renslow 2018: viridis for colour-blind readers */
            static const float r[11][3] = {
                {0.000f,0.133f,0.306f}, {0.071f,0.208f,0.439f}, {0.231f,0.286f,0.424f}, {0.341f,0.365f,0.427f},
                {0.439f,0.443f,0.451f}, {0.541f,0.525f,0.471f}, {0.647f,0.612f,0.455f}, {0.765f,0.702f,0.412f},
                {0.882f,0.800f,0.333f}, {0.996f,0.910f,0.220f}, {1.000f,0.925f,0.220f},
            };
            map_pts(r, 11, t, o); return;
        }
        case CV_CMAP_PLASMA: {                  /* matplotlib's */
            static const float r[11][3] = {
                {0.051f,0.031f,0.529f}, {0.255f,0.016f,0.616f}, {0.416f,0.000f,0.659f}, {0.561f,0.051f,0.643f},
                {0.694f,0.165f,0.565f}, {0.800f,0.278f,0.471f}, {0.882f,0.392f,0.384f}, {0.949f,0.518f,0.294f},
                {0.988f,0.651f,0.212f}, {0.988f,0.808f,0.145f}, {0.941f,0.976f,0.131f},
            };
            map_pts(r, 11, t, o); return;
        }
        case CV_CMAP_BLACKBODY: {               /* black - red - yellow - white, the radiation of a hot body */
            static const float r[4][3] = { {0,0,0}, {0.9f,0,0}, {0.9f,0.9f,0}, {1,1,1} };
            map_pts(r, 4, t, o); return;
        }
        case CV_CMAP_KINDLMANN: {               /* Kindlmann, Reinhard, Creem 2002: luminance climbs, the hue turns */
            static const float r[8][3] = {
                {0,0,0}, {0.16f,0.02f,0.48f}, {0.02f,0.33f,0.57f}, {0.01f,0.49f,0.37f},
                {0.06f,0.63f,0.07f}, {0.57f,0.73f,0.06f}, {0.98f,0.74f,0.73f}, {1,1,1},
            };
            map_pts(r, 8, t, o); return;
        }
        case CV_CMAP_WARM: cv_colormap_rgb(CV_CMAP_COOLWARM, 0.5f + 0.5f * t, o); return;   /* the halves of cool-warm */
        case CV_CMAP_COOL: cv_colormap_rgb(CV_CMAP_COOLWARM, 0.5f - 0.5f * t, o); return;
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

/* what both vertex shaders share: the uniforms and outputs, and from the deformed
   model-space point p to the clip position */
/* u_p: x deform scale, y point size (symbols: the least radius in pixels), z on top, w pull.
   u_q: x P[1][1] * viewport height (px -> view units), y scale of the second displacement,
        z proj[10], w proj[11] (-1 perspective, 0 parallel).
   v_r: a point's sphere radius in view units. v_wpos: the deformed model-space position,
        for the clip plane. */
#define VS_UNIFORMS \
    "uniform mat4 u_mvp;\n" \
    "uniform mat4 u_mv;\n" \
    "uniform vec4 u_p;\n" \
    "uniform vec4 u_q;\n" \
    "out vec3 v_vpos;\n" \
    "out float v_s;\n" \
    "out float v_r;\n" \
    "out vec3 v_n;\n" \
    "out vec3 v_wpos;\n"
/* From the deformed point p to the clip position.
   "On top": depth squeezed into the front 2% of the range, so these points pass in
   front of the faces yet still hide one another near-to-far.
   Pull: edges lie ON faces, so they are moved toward the eye by u_p.w view units to
   win against them. In view space, not in clip depth: a clip-depth pull is a
   different distance at every depth and, with the eye close (near plane far in front
   of the model), reaches from the back wall through the front one. Never past the
   eye: a vertex nearer than the pull would come out behind it (w < 0) and its
   triangle would cover the screen; at most half its distance, none behind the eye. */
#define VS_TAIL \
    "  gl_Position = u_mvp * vec4(p, 1.0);\n" \
    "  if (u_p.z > 0.5) gl_Position.z = -gl_Position.w + (gl_Position.z + gl_Position.w) * 0.02;\n" \
    "  if (u_q.w != 0.0) {\n"                /* perspective: w = -z_view */ \
    "    float pl = min(u_p.w, 0.5 * max(gl_Position.w, 0.0));\n" \
    "    float w2 = gl_Position.w - pl;\n" \
    "    if (gl_Position.w > 0.0) gl_Position.xy *= w2 / gl_Position.w;\n"    /* same screen point */ \
    "    gl_Position.z += u_q.z * pl;\n" \
    "    gl_Position.w = w2;\n" \
    "  } else gl_Position.z += u_q.z * u_p.w;\n" \
    "  v_r = u_p.y * gl_Position.w / max(u_q.x, 1e-6);\n" \
    "  v_vpos = (u_mv * vec4(p, 1.0)).xyz;\n" \
    "  v_wpos = p;\n" \
    "  v_n = vec3(0.0);\n" \
    "  gl_PointSize = u_p.y;\n" \
    ""

static const char* kVS =
    GLSL_HDR
    VS_UNIFORMS
    "in vec3 a_pos;\n"
    "in vec3 a_disp;\n"
    "in float a_scal;\n"
    "in vec3 a_disp2;\n"
    "void main() {\n"
    "  vec3 p = a_pos + a_disp * u_p.x + a_disp2 * u_q.y;\n"
    "  v_s = a_scal;\n"
    VS_TAIL
    "}\n";

/* The instanced symbol body: a_m is a vertex of the unit body (cos, sin round the
   axis, t along it: 0 at a, 1 at b; -1 / 2 for the end discs at a / b); the
   instance gives the two ends, their radii and how the body moves. The radius
   is at least u_p.y pixels where it is not 0 (a cone keeps its tip). The normal
   is the true one of the cone (or tube), so 12 sides shade round. */
static const char* kVSI =
    GLSL_HDR
    VS_UNIFORMS
    "in vec3 a_m;\n"
    "in vec3 i_a;\n"
    "in vec3 i_b;\n"
    "in vec3 i_r;\n"                          /* radius at a, at b, scalar */
    "in vec3 i_disp;\n"
    "in vec3 i_disp2;\n"
    "in vec3 i_dispb;\n"
    "in vec3 i_disp2b;\n"
    "void main() {\n"
    "  vec3 A = i_a + i_disp * u_p.x + i_disp2 * u_q.y;\n"
    "  vec3 B = i_b + i_dispb * u_p.x + i_disp2b * u_q.y, ax = B - A;\n"
    "  float L = length(ax);\n"
    "  vec3 w = L > 0.0 ? ax / L : vec3(0.0, 0.0, 1.0);\n"
    "  vec3 aw = abs(w);\n"
    "  vec3 up = aw.x <= aw.y ? (aw.x <= aw.z ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0))\n"
    "                         : (aw.y <= aw.z ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0));\n"
    "  vec3 u = normalize(cross(w, up));\n"
    "  vec3 v = cross(w, u);\n"
    "  float t = clamp(a_m.z, 0.0, 1.0);\n"
    "  vec3 c = mix(A, B, t);\n"
    "  float r = mix(i_r.x, i_r.y, t);\n"
    "  float wc = (u_mvp * vec4(c, 1.0)).w;\n"                       /* 1 in a parallel projection */
    "  if (r > 0.0 && wc > 0.0) r = max(r, u_p.y * 2.0 * wc / max(u_q.x, 1e-6));\n"
    "  vec3 rad = a_m.x * u + a_m.y * v;\n"
    "  vec3 p = c + r * rad;\n"
    "  v_s = i_r.z;\n"
    VS_TAIL
    "  vec3 nm = a_m.z < 0.0 ? -w : a_m.z > 1.0 ? w : rad * L - w * (i_r.y - i_r.x);\n"
    "  v_n = mat3(u_mv) * nm;\n"
    "}\n";

/* Labels: a quad per glyph or box, anchored to a model point, sized in pixels, so it
   never scales with the zoom; its depth is the anchor's pulled toward the eye, so the
   model hides labels on its far side. u_p: deform scales, viewport w, h.
   u_q: x the pull in view units per unit of clip w (parallel: in view units), y on top
   (then the point is tested against the model's depth: hidden points get no label),
   z proj[10], w proj[11] (-1 perspective, 0 parallel). */
static const char* kVSL =
    GLSL_HDR
    "uniform mat4 u_mvp;\n"
    "uniform vec4 u_p;\n"
    "uniform vec4 u_q;\n"
    "uniform highp sampler2DShadow u_depth;\n"
    "in vec2 a_q;\n"
    "in vec3 i_pos;\n"
    "in vec3 i_disp;\n"
    "in vec3 i_disp2;\n"
    "in vec2 i_off;\n"
    "in vec2 i_size;\n"
    "in vec2 i_uv0;\n"
    "in vec2 i_uv1;\n"
    "in float i_pull;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  vec3 p = i_pos + i_disp * u_p.x + i_disp2 * u_p.y;\n"
    "  vec4 c = u_mvp * vec4(p, 1.0);\n"
    /* the point a few label heights nearer the eye, compared with the depth drawn at the
       point's pixel: behind the surface there, the whole label leaves the screen */
    "  float pv = i_pull > 0.0 ? i_pull : u_q.x * (u_q.w != 0.0 ? c.w : 1.0);\n"   /* its own, or a few pixels' worth */
    "  float pl0 = min(pv, 0.5 * max(c.w, 0.0));\n"
    "  vec4 r = c;\n"
    "  if (u_q.w != 0.0) { r.z += u_q.z * pl0; r.w -= pl0; } else r.z += u_q.z * pv;\n"
    "  if (u_q.y > 0.5 && c.w > 0.0) {\n"
    "    vec2 uv = c.xy / c.w * 0.5 + 0.5;\n"
    "    float zr = r.z / r.w * 0.5 + 0.5;\n"
    "    if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 && textureLod(u_depth, vec3(uv, zr), 0.0) < 0.5) {\n"
    "      gl_Position = vec4(2.0, 2.0, 2.0, 1.0); v_uv = vec2(0.0); return;\n"
    "    }\n"
    "  }\n"
    "  vec2 px = i_off + a_q * i_size;\n"
    "  c.xy += vec2(px.x * 2.0 / u_p.z, -px.y * 2.0 / u_p.w) * c.w;\n"
    /* toward the eye by a few label heights in view units (as VS_TAIL does for the symbols), so
       the quad, which lies in the screen plane, is not cut by the oblique face it sits on;
       on top: the front 2 % of the depth range, as the markers */
    "  if (u_q.y > 0.5) c.z = -c.w + (c.z + c.w) * 0.02;\n"
    "  else if (u_q.w != 0.0) {\n"
    "    float pl = min(u_q.x * c.w, 0.5 * max(c.w, 0.0));\n"
    "    float w2 = c.w - pl;\n"
    "    if (c.w > 0.0) c.xy *= w2 / c.w;\n"
    "    c.z += u_q.z * pl;\n"
    "    c.w = w2;\n"
    "  } else c.z += u_q.z * u_q.x;\n"
    "  gl_Position = c;\n"
    "  v_uv = mix(i_uv0, i_uv1, a_q);\n"
    "}\n";

static const char* kFSL =
    GLSL_HDR
    "uniform vec4 u_color;\n"
    "uniform sampler2D u_atlas;\n"
    "in vec2 v_uv;\n"
    "out vec4 frag;\n"
    "void main() { frag = vec4(u_color.rgb, u_color.a * texture(u_atlas, v_uv).r); }\n";

/* The tensor glyph: a_m is (theta, phi) on the unit sphere's grid. The instance
   gives the centre and scalar, the base axes (direction times length) with the
   superquadric exponents alpha, beta, cee in their w, the signed eigenvalue along
   each axis and the kind (i_l.w: 0 superquadric, 1 Reynolds, 2 HWY; + 4 coloured
   by the normal stress in each direction), and how the glyph moves.
   Superquadric: glyph.h cv_superquad_point (keep in step). Reynolds: radius
   |n.S.n| along n; HWY: the shear |S.n - (n.S.n) n|. */
static const char* kVSG =
    GLSL_HDR
    VS_UNIFORMS
    "in vec3 a_m;\n"
    "in vec4 i_c;\n"
    "in vec4 i_e0;\n"
    "in vec4 i_e1;\n"
    "in vec4 i_e2;\n"
    "in vec4 i_l;\n"
    "in vec3 i_disp;\n"
    "in vec3 i_disp2;\n"
    "float spw(float x, float e) { return x < 0.0 ? -pow(-x, e) : pow(x, e); }\n"
    /* the point at (th, ph) in base units; nd: its direction in the eigenframe */
    "vec3 surf(int kind, float th, float ph, out vec3 nd) {\n"
    "  float ct = cos(th), st = sin(th), cf = cos(ph), sf = sin(ph);\n"
    "  if (kind == 0) {\n"
    "    float al = i_e0.w, be = i_e1.w, ce = i_e2.w, sm = spw(sf, be);\n"
    "    vec3 q = vec3(spw(ct, al) * sm, spw(st, al) * sm, spw(cf, be));\n"
    "    if (ce != be && sm != 0.0) q.y *= spw(sin(acos(clamp(spw(q.z, 1.0 / ce), -1.0, 1.0))), ce) / sm;\n"
    "    nd = q * vec3(length(i_e0.xyz), length(i_e1.xyz), length(i_e2.xyz));\n"
    "    return q;\n"
    "  }\n"
    "  vec3 n = vec3(ct * sf, st * sf, cf);\n"
    "  float qn = dot(i_l.xyz, n * n);\n"
    "  nd = n;\n"
    "  return n * (kind == 1 ? abs(qn) : sqrt(max(dot(i_l.xyz * i_l.xyz, n * n) - qn * qn, 0.0)));\n"
    "}\n"
    "vec3 world(vec3 q) { return q.x * i_e0.xyz + q.y * i_e1.xyz + q.z * i_e2.xyz; }\n"
    "void main() {\n"
    "  int kind = int(i_l.w + 0.5);\n"
    "  bool normal = kind >= 4;\n"
    "  if (normal) kind -= 4;\n"
    "  vec3 nd, dn;\n"
    "  vec3 o = world(surf(kind, a_m.x, a_m.y, nd));\n"
    "  vec3 p = i_c.xyz + i_disp * u_p.x + i_disp2 * u_q.y + o;\n"
    "  float nn = dot(nd, nd);\n"
    "  v_s = normal ? (nn > 0.0 ? dot(i_l.xyz, nd * nd) / nn : 0.0) : i_c.w;\n"
    VS_TAIL
    /* the surface normal: an ellipsoid's in closed form (the base axes scaled by
       1 / length^2); else by differences along theta and phi, a little off the poles
       (where theta does not move the point). Smooth shading at any grid. */
    "  if (kind == 0 && i_e0.w == 1.0 && i_e1.w == 1.0 && i_e2.w == 1.0) {\n"
    "    vec3 q = surf(0, a_m.x, a_m.y, dn);\n"
    "    v_n = mat3(u_mv) * (q.x * i_e0.xyz / dot(i_e0.xyz, i_e0.xyz) + q.y * i_e1.xyz / dot(i_e1.xyz, i_e1.xyz)\n"
    "                        + q.z * i_e2.xyz / dot(i_e2.xyz, i_e2.xyz));\n"
    "    return;\n"
    "  }\n"
    "  const float e = 0.01;\n"
    "  float ph = clamp(a_m.y, 3.0 * e, 3.14159265 - 3.0 * e);\n"
    "  vec3 o0 = world(surf(kind, a_m.x, ph, dn));\n"
    "  vec3 dt = world(surf(kind, a_m.x + e, ph, dn)) - o0;\n"
    "  vec3 dp = world(surf(kind, a_m.x, ph + e, dn)) - o0;\n"
    "  vec3 nm = cross(dt, dp);\n"
    "  v_n = mat3(u_mv) * (dot(nm, nm) > 0.0 ? nm : o);\n"
    "}\n";

static const char* kFS =
    GLSL_HDR
    "uniform vec4 u_color;\n"                /* solid colour */
    "uniform vec4 u_rng;\n"                  /* min, max, bands, mode */
    "uniform vec4 u_flags;\n"                /* x: unused, y: shade, z: on top, w: selected (tinted) */
    "uniform vec4 u_oora;\n"                 /* values above the locked range: rgb, w: 0 the map's end, 1 the rgb, 3 hidden */
    "uniform vec4 u_oorb;\n"                 /* below */
    "uniform vec4 u_pz;\n"                   /* projection: z_clip = x*z + y, w_clip = z*z + w */
    "uniform vec4 u_clip;\n"                 /* clip plane normal, d; w = 1e30 when off */
    "uniform sampler2D u_cmap;\n"
    "uniform sampler2D u_etex;\n"
    "in vec3 v_vpos;\n"
    "in float v_s;\n"
    "in float v_r;\n"
    "in vec3 v_wpos;\n"
    "in vec3 v_n;\n"
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
    "      if (t0 > 1.0001) { if (u_oora.w > 2.5) discard; if (u_oora.w > 0.5) c = u_oora.rgb; }\n"
    "      if (t0 < -1e-4) { if (u_oorb.w > 2.5) discard; if (u_oorb.w > 0.5) c = u_oorb.rgb; }\n"
    "    }\n"
    "  }\n"
    "  if (u_flags.w > 0.5) c = mix(c, vec3(1.0, 0.9, 0.2), 0.5);\n"   /* the box selection: its colours toned toward yellow */
    "  if (u_flags.y > 0.5) {\n"
    /* symbols and glyphs bring their own normal: smooth, with a soft highlight */
    "    bool sm = dot(v_n, v_n) > 0.0;\n"
    "    vec3 n = sm ? normalize(v_n) : normalize(cross(dFdx(v_vpos), dFdy(v_vpos)));\n"
    "    c *= 0.30 + 0.70 * abs(n.z);\n"
    "    if (sm) c += 0.18 * pow(abs(n.z), 28.0);\n"
    "  }\n"
    "#ifdef SPHERE\n"
    "  c *= 0.72 + 0.28 * nz;\n"               /* a touch of rim darkening: reads as a ball */
    "#endif\n"
    "  frag = vec4(c, 1.0);\n"
    "}\n";

typedef struct { float mvp[16]; float mv[16]; float p[4]; float q[4]; } vs_params;
static float g_tint;                         /* the next layers are the selection: toned toward yellow (u_flags.w) */
typedef struct { float color[4]; float rng[4]; float flags[4]; float pz[4]; float clip[4]; float oora[4]; float oorb[4]; } fs_params;

/* ---- state ------------------------------------------------------------------- */

enum { ETEX_W = 4096 };

static struct {
    sg_shader   shd, shd_prim, shd_pt;
    sg_pipeline pip_tri, pip_tri_prim, pip_line, pip_pt;
    sg_pipeline pip_tri_ni, pip_line_ni, pip_pt_ni;   /* non-indexed: the aux vertex sets */
    sg_shader   shd_inst;
    sg_pipeline pip_inst;             /* the instanced symbol bodies */
    sg_shader   shd_glyph;
    sg_pipeline pip_glyph;            /* the tensor glyphs */
    sg_shader   shd_label;
    sg_pipeline pip_label;            /* labels: a unit quad per instance */
    sg_buffer   label_quad;
    sg_image    label_img;  sg_view label_view;   /* the font atlas, alpha */
    /* the model's depth alone, drawn before the frame while labels are on: a label whose
       point lies behind the surface at its pixel is not drawn (cv_render_label_depth) */
    sg_pipeline pip_depth;
    sg_image    dpt_img;    sg_view dpt_att, dpt_tex;  int dpt_w, dpt_h;
    sg_sampler  smp_cmp;              /* the comparison: a point's depth against the surface's */
    sg_buffer   gmesh[3];  int gmesh_n[3];              /* the unit glyph: (theta, phi) grids, fine to coarse */
    sg_buffer   body[2];  int body_n[2];                /* the unit body: round with both end discs; light, for great numbers */
    sg_image    cmap_img;  sg_view cmap_view;
    sg_image    div_img;   sg_view div_view;   /* cool-warm, fixed: the principal cross by value */
    sg_image    etex_img;  sg_view etex_view;
    sg_buffer   ib_grp;               /* skin triangles ordered by group */
    uint32_t*   grp_first;            /* ngroups + 1 */
    float*      grp_rgb;              /* 3 per group */
    int         ngroups;
    sg_sampler  smp_lin, smp_near;
    sg_buffer   pos, disp, disp2, scal;
    sg_buffer   ib_tri, ib_edge, ib_pt, ib_fedge;
    size_t      n_tri, n_edge, n_pt, n_fedge;
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

static void make_cmap(int cm, bool reverse, bool grey, sg_image* img, sg_view* view) {
    uint8_t px[256 * 4];
    for (int i = 0; i < 256; i++) {
        float c[3];
        cv_colormap_rgb(cm, reverse ? 1.f - (float)i / 255.f : (float)i / 255.f, c);
        if (grey) c[0] = c[1] = c[2] = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
        for (int k = 0; k < 3; k++) px[4 * i + k] = (uint8_t)(c[k] * 255.f + 0.5f);
        px[4 * i + 3] = 255;
    }
    if (view->id) sg_destroy_view(*view);
    if (img->id) sg_destroy_image(*img);
    *img = sg_make_image(&(sg_image_desc){
        .width = 256, .height = 1, .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = { px, sizeof px },
    });
    *view = sg_make_view(&(sg_view_desc){ .texture.image = *img });
}

void cv_render_colormap(int cm, bool reverse, bool grey) { make_cmap(cm, reverse, grey, &R.cmap_img, &R.cmap_view); }

static sg_shader make_shader_vs(const char* vs_src, const char* const* attr, const char* fs_src) {
    return sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = vs_src,
        .fragment_func.source = fs_src,
        .attrs = {
            [0] = { .glsl_name = attr[0] }, [1] = { .glsl_name = attr[1] }, [2] = { .glsl_name = attr[2] },
            [3] = { .glsl_name = attr[3] }, [4] = { .glsl_name = attr[4] }, [5] = { .glsl_name = attr[5] },
            [6] = { .glsl_name = attr[6] }, [7] = { .glsl_name = attr[7] },
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
                [5] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_oora" },
                [6] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_oorb" },
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

static sg_shader make_shader(const char* fs_src) {
    static const char* const attr[8] = { "a_pos", "a_disp", "a_scal", "a_disp2", NULL, NULL, NULL, NULL };
    return make_shader_vs(kVS, attr, fs_src);
}

/* the unit body of the instanced symbols as plain triangles: n sides, an end disc
   at a, and one at b when cap_b. Returns the vertex count. */
static int unit_body(float* v, int n, bool cap_b) {
    int k = 0;
#define MV(c, s_, t) (v[k++] = (c), v[k++] = (s_), v[k++] = (t))
    for (int i = 0; i < n; i++) {
        float t0 = 6.2831853f * i / n, t1 = 6.2831853f * (i + 1) / n;
        float c0 = cosf(t0), s0 = sinf(t0), c1 = cosf(t1), s1 = sinf(t1);
        MV(c0, s0, 0); MV(c1, s1, 0); MV(c1, s1, 1);
        MV(c0, s0, 0); MV(c1, s1, 1); MV(c0, s0, 1);
        MV(0, 0, -1); MV(c1, s1, -1); MV(c0, s0, -1);          /* -1 / 2: the end discs (their normal) */
        if (cap_b) { MV(0, 0, 2); MV(c0, s0, 2); MV(c1, s1, 2); }
    }
#undef MV
    return k / 3;
}

/* the unit glyph as plain triangles over an nt x np grid of (theta, phi) */
static int unit_glyph(float* v, int nt, int np) {
    int k = 0;
    for (int i = 0; i < nt; i++)
        for (int j = 0; j < np; j++) {
            float t0 = 6.2831853f * i / nt, t1 = 6.2831853f * (i + 1) / nt;
            float f0 = 3.1415927f * j / np, f1 = 3.1415927f * (j + 1) / np;
            const float q[6][2] = { { t0, f0 }, { t0, f1 }, { t1, f1 }, { t0, f0 }, { t1, f1 }, { t1, f0 } };
            for (int c = 0; c < 6; c++) { v[k++] = q[c][0]; v[k++] = q[c][1]; v[k++] = 0; }
        }
    return k / 3;
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

    {   /* instanced symbols: the unit body per vertex, 15 floats per instance */
        static const char* const attr[8] = { "a_m", "i_a", "i_b", "i_r", "i_disp", "i_disp2", "i_dispb", "i_disp2b" };
        R.shd_inst = make_shader_vs(kVSI, attr, kFS);
        sg_pipeline_desc pi = {
            .shader = R.shd_inst,
            .layout = {
                .buffers = { [0] = { .stride = 12 },
                             [1] = { .stride = CV_INST_FLOATS * 4, .step_func = SG_VERTEXSTEP_PER_INSTANCE } },
                .attrs = {
                    [0] = { .buffer_index = 0, .format = SG_VERTEXFORMAT_FLOAT3 },
                    [1] = { .buffer_index = 1, .offset = 0,  .format = SG_VERTEXFORMAT_FLOAT3 },
                    [2] = { .buffer_index = 1, .offset = 12, .format = SG_VERTEXFORMAT_FLOAT3 },
                    [3] = { .buffer_index = 1, .offset = 24, .format = SG_VERTEXFORMAT_FLOAT3 },
                    [4] = { .buffer_index = 1, .offset = 36, .format = SG_VERTEXFORMAT_FLOAT3 },
                    [5] = { .buffer_index = 1, .offset = 48, .format = SG_VERTEXFORMAT_FLOAT3 },
                    [6] = { .buffer_index = 1, .offset = 60, .format = SG_VERTEXFORMAT_FLOAT3 },
                    [7] = { .buffer_index = 1, .offset = 72, .format = SG_VERTEXFORMAT_FLOAT3 },
                },
            },
            .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .index_type = SG_INDEXTYPE_NONE,
            .depth = { .compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true },
        };
        R.pip_inst = sg_make_pipeline(&pi);
        float v[12 * 12 * 3];
        R.body_n[0] = unit_body(v, 12, true);
        R.body[0] = make_buf(v, (size_t)R.body_n[0] * 12, false);
        R.body_n[1] = unit_body(v, 6, false);
        R.body[1] = make_buf(v, (size_t)R.body_n[1] * 12, false);
    }
    {   /* labels: a unit quad per instance, CV_LABEL_FLOATS per instance, blended, no depth write */
        typedef struct { float mvp[16]; float p[4]; float q[4]; } vs_label;
        R.shd_label = sg_make_shader(&(sg_shader_desc){
            .vertex_func.source = kVSL, .fragment_func.source = kFSL,
            .attrs = { [0] = { .glsl_name = "a_q" }, [1] = { .glsl_name = "i_pos" }, [2] = { .glsl_name = "i_disp" },
                       [3] = { .glsl_name = "i_disp2" }, [4] = { .glsl_name = "i_off" }, [5] = { .glsl_name = "i_size" },
                       [6] = { .glsl_name = "i_uv0" }, [7] = { .glsl_name = "i_uv1" }, [8] = { .glsl_name = "i_pull" } },
            .uniform_blocks[0] = { .stage = SG_SHADERSTAGE_VERTEX, .size = sizeof(vs_label),
                .glsl_uniforms = { [0] = { .type = SG_UNIFORMTYPE_MAT4, .glsl_name = "u_mvp" },
                                   [1] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_p" },
                                   [2] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_q" } } },
            .uniform_blocks[1] = { .stage = SG_SHADERSTAGE_FRAGMENT, .size = 16,
                .glsl_uniforms = { [0] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_color" } } },
            .views = { [0].texture = { .stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D, .sample_type = SG_IMAGESAMPLETYPE_FLOAT },
                       [1].texture = { .stage = SG_SHADERSTAGE_VERTEX, .image_type = SG_IMAGETYPE_2D, .sample_type = SG_IMAGESAMPLETYPE_DEPTH } },
            .samplers = { [0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .sampler_type = SG_SAMPLERTYPE_FILTERING },
                          [1] = { .stage = SG_SHADERSTAGE_VERTEX, .sampler_type = SG_SAMPLERTYPE_COMPARISON } },
            .texture_sampler_pairs = { [0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .view_slot = 0, .sampler_slot = 0, .glsl_name = "u_atlas" },
                                       [1] = { .stage = SG_SHADERSTAGE_VERTEX, .view_slot = 1, .sampler_slot = 1, .glsl_name = "u_depth" } },
        });
        R.smp_cmp = sg_make_sampler(&(sg_sampler_desc){ .min_filter = SG_FILTER_NEAREST, .mag_filter = SG_FILTER_NEAREST,
                                                       .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
                                                       .compare = SG_COMPAREFUNC_LESS_EQUAL });
        /* the faces' depth alone: the mesh shader into a depth-only pass */
        R.pip_depth = sg_make_pipeline(&(sg_pipeline_desc){
            .shader = R.shd, .layout = layout, .index_type = SG_INDEXTYPE_UINT32, .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .colors[0].pixel_format = SG_PIXELFORMAT_NONE,   /* depth only: no colour attachment */
            .depth = { .pixel_format = SG_PIXELFORMAT_DEPTH, .compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true },
            .sample_count = 1,
        });
        R.pip_label = sg_make_pipeline(&(sg_pipeline_desc){
            .shader = R.shd_label,
            .layout = {
                .buffers = { [0] = { .stride = 8 }, [1] = { .stride = CV_LABEL_FLOATS * 4, .step_func = SG_VERTEXSTEP_PER_INSTANCE } },
                .attrs = {
                    [0] = { .buffer_index = 0, .format = SG_VERTEXFORMAT_FLOAT2 },
                    [1] = { .buffer_index = 1, .offset = 0,  .format = SG_VERTEXFORMAT_FLOAT3 },
                    [2] = { .buffer_index = 1, .offset = 12, .format = SG_VERTEXFORMAT_FLOAT3 },
                    [3] = { .buffer_index = 1, .offset = 24, .format = SG_VERTEXFORMAT_FLOAT3 },
                    [4] = { .buffer_index = 1, .offset = 36, .format = SG_VERTEXFORMAT_FLOAT2 },
                    [5] = { .buffer_index = 1, .offset = 44, .format = SG_VERTEXFORMAT_FLOAT2 },
                    [6] = { .buffer_index = 1, .offset = 52, .format = SG_VERTEXFORMAT_FLOAT2 },
                    [7] = { .buffer_index = 1, .offset = 60, .format = SG_VERTEXFORMAT_FLOAT2 },
                    [8] = { .buffer_index = 1, .offset = 68, .format = SG_VERTEXFORMAT_FLOAT },
                },
            },
            .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .index_type = SG_INDEXTYPE_NONE,
            .depth = { .compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = false },
            .colors[0].blend = { .enabled = true, .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA, .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                 .src_factor_alpha = SG_BLENDFACTOR_ONE, .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA },
        });
        static const float q[12] = { 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1 };
        R.label_quad = make_buf(q, sizeof q, false);
    }

    {   /* tensor glyphs: the unit grid per vertex, CV_GLYPH_FLOATS per instance */
        static const char* const attr[8] = { "a_m", "i_c", "i_e0", "i_e1", "i_e2", "i_l", "i_disp", "i_disp2" };
        R.shd_glyph = make_shader_vs(kVSG, attr, kFS);
        sg_pipeline_desc pg = {
            .shader = R.shd_glyph,
            .layout = {
                .buffers = { [0] = { .stride = 12 },
                             [1] = { .stride = CV_GLYPH_FLOATS * 4, .step_func = SG_VERTEXSTEP_PER_INSTANCE } },
                .attrs = {
                    [0] = { .buffer_index = 0, .format = SG_VERTEXFORMAT_FLOAT3 },
                    [1] = { .buffer_index = 1, .offset = 0,  .format = SG_VERTEXFORMAT_FLOAT4 },
                    [2] = { .buffer_index = 1, .offset = 16, .format = SG_VERTEXFORMAT_FLOAT4 },
                    [3] = { .buffer_index = 1, .offset = 32, .format = SG_VERTEXFORMAT_FLOAT4 },
                    [4] = { .buffer_index = 1, .offset = 48, .format = SG_VERTEXFORMAT_FLOAT4 },
                    [5] = { .buffer_index = 1, .offset = 64, .format = SG_VERTEXFORMAT_FLOAT4 },
                    [6] = { .buffer_index = 1, .offset = 80, .format = SG_VERTEXFORMAT_FLOAT3 },
                    [7] = { .buffer_index = 1, .offset = 92, .format = SG_VERTEXFORMAT_FLOAT3 },
                },
            },
            .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .index_type = SG_INDEXTYPE_NONE,
            .depth = { .compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true },
        };
        R.pip_glyph = sg_make_pipeline(&pg);
        static float v[32 * 16 * 18];
        /* fine for a few thousand (pinched shapes need it), coarser as they multiply:
           at most ~15M vertices a frame; the smooth normals hide the facets */
        static const int grid[3][2] = { { 32, 16 }, { 18, 9 }, { 12, 6 } };
        for (int i = 0; i < 3; i++) {
            R.gmesh_n[i] = unit_glyph(v, grid[i][0], grid[i][1]);
            R.gmesh[i] = make_buf(v, (size_t)R.gmesh_n[i] * 12, false);
        }
    }

    R.smp_lin = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR, .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE });
    R.smp_near = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST, .mag_filter = SG_FILTER_NEAREST,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE });
    cv_render_colormap(CV_CMAP_FAST, false, false);
    make_cmap(CV_CMAP_COOLWARM, false, false, &R.div_img, &R.div_view);
    float zero = 0;
    make_etex(&zero, 1, 1);
}

static void clear_aux(void);

void cv_render_clear_model(void) {
    clear_aux();
    kill_buf(&R.pos); kill_buf(&R.disp); kill_buf(&R.disp2); kill_buf(&R.scal);
    kill_buf(&R.ib_tri); kill_buf(&R.ib_edge); kill_buf(&R.ib_pt); kill_buf(&R.ib_fedge);
    R.n_tri = R.n_edge = R.n_pt = R.n_fedge = 0;
    R.n_nodes = 0;
    cv_render_labels(NULL, 0, NULL, 0);
}

static void depth_free(void);

void cv_render_shutdown(void) {
    depth_free();
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

void cv_render_outline(const uint32_t* fedge, size_t n_fedge) {
    kill_buf(&R.ib_fedge);
    R.ib_fedge = make_buf(fedge, n_fedge * 8, true); R.n_fedge = R.ib_fedge.id ? n_fedge : 0;
}

typedef struct { sg_buffer pos, disp, scal, disp2; } vset;

/* depth pull for edges, which lie on faces, as a fraction of the model size: above the
   depth buffer's step with the eye close to the model, far below any wall thickness */
#define PULL 3e-4f

static void uniforms(const cv_draw* d, bool has_disp, bool has_disp2, int mode, const float rgb[3], bool shade,
                     float point_size, bool on_top, float pull) {
    vs_params vs;
    memcpy(vs.mvp, d->mvp, sizeof vs.mvp);
    memcpy(vs.mv, d->mv, sizeof vs.mv);
    vs.p[0] = has_disp ? d->def_scale : 0.f;
    vs.p[1] = point_size;
    vs.p[2] = on_top ? 1.f : 0.f;
    vs.p[3] = pull * d->diag;
    vs.q[0] = d->proj[5] * (float)d->vp_h;      /* pixels -> view units for the ball radius */
    vs.q[1] = has_disp2 ? d->def_scale2 : 0.f;
    vs.q[2] = d->proj[10]; vs.q[3] = d->proj[11];
    sg_apply_uniforms(0, &SG_RANGE(vs));
    fs_params fs = {
        .color = { rgb[0], rgb[1], rgb[2], 1 },
        .rng = { d->rmin, d->rmax, (float)d->bands, (float)mode },
        .flags = { 0.f, shade ? 1.f : 0.f, on_top ? 1.f : 0.f, g_tint },
        .pz = { d->proj[10], d->proj[14], d->proj[11], d->proj[15] },
        .clip = { d->clip ? d->clip_n[0] : 0, d->clip ? d->clip_n[1] : 0, d->clip ? d->clip_n[2] : 0, d->clip ? d->clip_d : 1e30f },
        .oora = { d->oor[0][0], d->oor[0][1], d->oor[0][2], d->oor[0][3] },
        .oorb = { d->oor[1][0], d->oor[1][1], d->oor[1][2], d->oor[1][3] },
    };
    sg_apply_uniforms(1, &SG_RANGE(fs));
}

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
    uniforms(d, v.disp.id != 0, v.disp2.id != 0, mode, rgb, shade, point_size, on_top, pull);
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

static struct { sg_buffer buf; uint32_t n; } I[CV_INST_N];
static struct { sg_buffer box, gly; uint32_t nb, ng; } LB;   /* the labels: boxes, glyphs */

void cv_render_label_atlas(const unsigned char* a8, int w, int h) {
    if (R.label_view.id) { sg_destroy_view(R.label_view); R.label_view.id = 0; }
    if (R.label_img.id) { sg_destroy_image(R.label_img); R.label_img.id = 0; }
    if (!a8 || w <= 0 || h <= 0) return;
    R.label_img = sg_make_image(&(sg_image_desc){ .width = w, .height = h, .pixel_format = SG_PIXELFORMAT_R8,
                                                  .data.mip_levels[0] = { a8, (size_t)w * (size_t)h } });
    R.label_view = sg_make_view(&(sg_view_desc){ .texture.image = R.label_img });
}

void cv_render_labels(const float* box, uint32_t nb, const float* gly, uint32_t ng) {
    kill_buf(&LB.box); kill_buf(&LB.gly); LB.nb = LB.ng = 0;
    if (box && nb) { LB.box = make_buf(box, (size_t)nb * CV_LABEL_FLOATS * 4, false); LB.nb = nb; }
    if (gly && ng) { LB.gly = make_buf(gly, (size_t)ng * CV_LABEL_FLOATS * 4, false); LB.ng = ng; }
}

static void depth_free(void) {
    if (R.dpt_tex.id) sg_destroy_view(R.dpt_tex);
    if (R.dpt_att.id) sg_destroy_view(R.dpt_att);
    if (R.dpt_img.id) sg_destroy_image(R.dpt_img);
    R.dpt_tex.id = R.dpt_att.id = R.dpt_img.id = 0; R.dpt_w = R.dpt_h = 0;
}

void cv_render_label_depth(const cv_draw* d) {
    if (d->vp_w <= 0 || d->vp_h <= 0 || (!LB.nb && !LB.ng)) return;
    if (R.dpt_w != d->vp_w || R.dpt_h != d->vp_h) {
        depth_free();
        R.dpt_img = sg_make_image(&(sg_image_desc){ .usage.depth_stencil_attachment = true, .width = d->vp_w, .height = d->vp_h,
                                                    .pixel_format = SG_PIXELFORMAT_DEPTH, .sample_count = 1 });
        R.dpt_att = sg_make_view(&(sg_view_desc){ .depth_stencil_attachment.image = R.dpt_img });
        R.dpt_tex = sg_make_view(&(sg_view_desc){ .texture.image = R.dpt_img });
        R.dpt_w = d->vp_w; R.dpt_h = d->vp_h;
    }
    sg_begin_pass(&(sg_pass){ .action.depth = { .load_action = SG_LOADACTION_CLEAR, .clear_value = 1.f },
                              .attachments.depth_stencil = R.dpt_att });
    if (d->faces && R.n_tri) {
        vset mesh = { R.pos, R.disp, R.scal, R.disp2 };
        draw_layer(R.pip_depth, mesh, R.ib_tri, (int)(R.n_tri * 3), CV_COLOR_SOLID, d->face_rgb, false, d, 1, false, 0.f, 0);
    }
    sg_end_pass();
}

/* the two label layers: the boxes, then the glyphs over them */
static void draw_labels(const cv_draw* d) {
    if (!R.label_view.id || !R.dpt_tex.id || (!LB.nb && !LB.ng)) return;
    typedef struct { float mvp[16]; float p[4]; float q[4]; } vs_label;
    vs_label vs;
    memcpy(vs.mvp, d->mvp, sizeof vs.mvp);
    vs.p[0] = d->def_scale; vs.p[1] = d->def_scale2; vs.p[2] = (float)d->vp_w; vs.p[3] = (float)d->vp_h;
    /* by the labels' reach, so a face tilted up to 45 degrees cannot cut into them; pixels to
       view units per clip w are 2 / (P11 * vp_h) */
    float per_px = 2.f / CV_MAX(d->proj[5] * (float)d->vp_h, 1e-6f);
    /* in front: the pull is only the visibility test's tolerance, two pixels (the depth was
       drawn with this very projection); depth tested: the labels' reach, so a face cannot cut in */
    vs.q[0] = (d->labels_on_top ? 2.f : CV_MAX(d->label_px, 1.f)) * per_px;
    vs.q[1] = d->labels_on_top ? 1.f : 0.f; vs.q[2] = d->proj[10]; vs.q[3] = d->proj[11];
    sg_apply_pipeline(R.pip_label);
    for (int pass = 0; pass < 2; pass++) {
        uint32_t n = pass ? LB.ng : LB.nb;
        if (!n) continue;
        sg_bindings b = { .vertex_buffers = { [0] = R.label_quad, [1] = pass ? LB.gly : LB.box },
                          .views = { [0] = R.label_view, [1] = R.dpt_tex }, .samplers = { [0] = R.smp_lin, [1] = R.smp_cmp } };
        sg_apply_bindings(&b);
        sg_apply_uniforms(0, &SG_RANGE(vs));
        float col[4];
        if (pass) { col[0] = d->label_rgb[0]; col[1] = d->label_rgb[1]; col[2] = d->label_rgb[2]; col[3] = 1; }
        else memcpy(col, d->label_box_rgba, sizeof col);
        sg_apply_uniforms(1, &SG_RANGE(col));
        sg_draw(0, 6, (int)n);
    }
}

void cv_render_inst(int which, const float* inst, uint32_t n) {
    if (which < 0 || which >= CV_INST_N) return;
    kill_buf(&I[which].buf);
    I[which].n = 0;
    if (!inst || n == 0) return;
    I[which].buf = make_buf(inst, (size_t)n * CV_INST_FLOATS * 4, false);
    I[which].n = I[which].buf.id ? n : 0;
}

static struct { sg_buffer buf; uint32_t n; } GLY;   /* the tensor glyphs */

void cv_render_glyphs(const float* inst, uint32_t n) {
    kill_buf(&GLY.buf);
    GLY.n = 0;
    if (!inst || n == 0) return;
    GLY.buf = make_buf(inst, (size_t)n * CV_GLYPH_FLOATS * 4, false);
    GLY.n = GLY.buf.id ? n : 0;
}

static void draw_glyphs(int mode, const float rgb[3], const cv_draw* d, sg_view cmap) {
    if (!GLY.n) return;
    int lod = GLY.n > 15000 ? 2 : GLY.n > 4000 ? 1 : 0;   /* great numbers: a coarser grid */
    if (cv_gpu_is_software() && lod < 2) lod++;           /* a software rasteriser: one step coarser */
    sg_apply_pipeline(R.pip_glyph);
    sg_bindings b = {
        .vertex_buffers = { [0] = R.gmesh[lod], [1] = GLY.buf },
        .views = { [0] = cmap, [1] = R.etex_view },
        .samplers = { [0] = R.smp_lin, [1] = R.smp_near },
    };
    sg_apply_bindings(&b);
    uniforms(d, true, true, mode, rgb, true, 0.f, false, 0.f);
    sg_draw(0, R.gmesh_n[lod], (int)GLY.n);
}

/* a layer of symbols, lit; min_px: the least radius on screen; cmap: the colour map texture */
static void draw_inst_map(int which, int mode, const float rgb[3], const cv_draw* d, float min_px, sg_view cmap, bool on_top) {
    if (!I[which].n) return;
    int lod = I[which].n > 30000 ? 1 : 0;       /* great numbers: six sides, one end disc */
    sg_apply_pipeline(R.pip_inst);
    sg_bindings b = {
        .vertex_buffers = { [0] = R.body[lod], [1] = I[which].buf },
        .views = { [0] = cmap, [1] = R.etex_view },
        .samplers = { [0] = R.smp_lin, [1] = R.smp_near },
    };
    sg_apply_bindings(&b);
    uniforms(d, true, true, mode, rgb, true, min_px, on_top, 0.f);
    sg_draw(0, R.body_n[lod], (int)I[which].n);
}

static void draw_inst(int which, int mode, const float rgb[3], const cv_draw* d, float min_px) {
    draw_inst_map(which, mode, rgb, d, min_px, R.cmap_view, false);
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
    if (d->faces && A[CV_AUX_SELTRI].n) {    /* the box selection's faces again, a hair in front, toned */
        int m = d->faces_color == CV_COLOR_NODAL || d->faces_color == CV_COLOR_ELEM ? CV_COLOR_NODAL : CV_COLOR_SOLID;
        g_tint = 1.f;
        draw_layer(R.pip_tri_ni, A[CV_AUX_SELTRI].v, NO_IB, (int)A[CV_AUX_SELTRI].n, m, d->face_rgb, d->shade, d, 1, false, PULL, 0);
        g_tint = 0.f;
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
    if (A[CV_AUX_SELLN].n) {   /* the box selection: its outer face edges, bright */
        static const float sel_rgb[3] = { 1.0f, 0.92f, 0.15f };
        draw_layer(R.pip_line_ni, A[CV_AUX_SELLN].v, NO_IB, (int)A[CV_AUX_SELLN].n, CV_COLOR_SOLID, sel_rgb, false, d, 1, false, PULL, 0);
    }
    if (d->outline) {   /* after the edges and darker, so it reads over coloured ones; GL core lines have no width */
        const float rgb[3] = { d->edge_rgb[0] * 0.4f, d->edge_rgb[1] * 0.4f, d->edge_rgb[2] * 0.4f };
        draw_layer(R.pip_line, mesh, R.ib_fedge, (int)(R.n_fedge * 2), CV_COLOR_SOLID, rgb, false, d, 1, false, PULL, 0);
    }
    if (d->points) {
        int m = d->points_color == CV_COLOR_ELEM ? CV_COLOR_NODAL : d->points_color;
        draw_layer(R.pip_pt, mesh, R.ib_pt, (int)R.n_pt, m, d->point_rgb, false, d, d->point_size, false, 0.f, 0);
    }
    if (d->path && A[CV_AUX_RAYLN].n) {      /* the path's line on through the model, on top */
        static const float ray_rgb[3] = { 0.2f, 0.85f, 0.3f };
        draw_layer(R.pip_line_ni, A[CV_AUX_RAYLN].v, NO_IB, (int)A[CV_AUX_RAYLN].n, CV_COLOR_SOLID, ray_rgb, false, d, 1, true, 0.f, 0);
    }
    if (d->path && A[CV_AUX_PATHLN].n) {     /* the plot line, on top */
        static const float path_rgb[3] = { 1.0f, 0.55f, 0.1f };
        draw_layer(R.pip_line_ni, A[CV_AUX_PATHLN].v, NO_IB, (int)A[CV_AUX_PATHLN].n, CV_COLOR_SOLID, path_rgb, false, d, 1, true, 0.f, 0);
        draw_layer(R.pip_pt_ni, A[CV_AUX_PATHLN].v, NO_IB, (int)A[CV_AUX_PATHLN].n, CV_COLOR_SOLID, path_rgb, false, d, d->marker_size * 0.4f, true, 0.f, 0);
    }
    if (A[CV_AUX_PICKPT].n) {                /* the picked nodes, on top */
        static const float pick_rgb[3] = { 1.0f, 0.84f, 0.0f };
        draw_layer(R.pip_pt_ni, A[CV_AUX_PICKPT].v, NO_IB, (int)A[CV_AUX_PICKPT].n, CV_COLOR_SOLID, pick_rgb, false, d, d->marker_size, true, 0.f, 0);
    }
    if (A[CV_AUX_MEASLN].n) {                /* the measurements' lines, on top: dots along them make them read thicker */
        static const float meas_rgb[3] = { 0.1f, 0.85f, 1.0f };
        draw_layer(R.pip_line_ni, A[CV_AUX_MEASLN].v, NO_IB, (int)A[CV_AUX_MEASLN].n, CV_COLOR_SOLID, meas_rgb, false, d, 1, true, 0.f, 0);
        draw_layer(R.pip_pt_ni, A[CV_AUX_MEASLN].v, NO_IB, (int)A[CV_AUX_MEASLN].n, CV_COLOR_SOLID, meas_rgb, false, d, d->marker_size * 0.25f, true, 0.f, 0);
    }
    if (A[CV_AUX_MEASPT].n) {                /* the measured nodes, and those picked so far */
        static const float meas_rgb[3] = { 0.1f, 0.85f, 1.0f };
        draw_layer(R.pip_pt_ni, A[CV_AUX_MEASPT].v, NO_IB, (int)A[CV_AUX_MEASPT].n, CV_COLOR_SOLID, meas_rgb, false, d, d->marker_size * 0.7f, true, 0.f, 0);
    }
    if (A[CV_AUX_SELPT].n) {                 /* the selected nodes: small magenta dots (balls: a dark rim on any colour) */
        static const float node_rgb[3] = { 0.95f, 0.2f, 0.85f };
        draw_layer(R.pip_pt_ni, A[CV_AUX_SELPT].v, NO_IB, (int)A[CV_AUX_SELPT].n, CV_COLOR_SOLID, node_rgb, false, d, d->marker_size * 0.5f, false, 0.f, 0);
    }
    {   /* the selection's max (red) and min (blue) on top, each on a white ball a size larger
           drawn at its true depth, so the coloured one wins where they meet */
        static const float white[3] = { 1, 1, 1 }, red[3] = { 0.9f, 0.08f, 0.08f }, blue[3] = { 0.1f, 0.3f, 0.95f };
        const int w[2] = { CV_AUX_SELMAX, CV_AUX_SELMIN };
        for (int k = 0; k < 2; k++) {
            if (!A[w[k]].n) continue;
            draw_layer(R.pip_pt_ni, A[w[k]].v, NO_IB, (int)A[w[k]].n, CV_COLOR_SOLID, white, false, d, d->marker_size * 1.45f, false, 0.f, 0);
            draw_layer(R.pip_pt_ni, A[w[k]].v, NO_IB, (int)A[w[k]].n, CV_COLOR_SOLID, k ? blue : red, false, d, d->marker_size, true, 0.f, 0);
        }
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
        static const float bc_rgb[3] = { 0.15f, 0.85f, 0.85f }, ld_rgb[3] = { 1.0f, 0.78f, 0.10f },
                           mom_rgb[3] = { 1.0f, 0.35f, 0.85f }, heat_rgb[3] = { 1.0f, 0.32f, 0.18f };
        /* forces and pressures yellow, moments magenta, heat red */
        static const float link_rgb[3] = { 0.55f, 0.95f, 0.45f }, disc_rgb[3] = { 0.80f, 0.45f, 0.95f },
                           vec_rgb[3] = { 0.95f, 0.95f, 0.95f };
        const float px = 0.6f;                    /* the least radius of a stroke on screen */
        if (d->supports) {
            draw_inst(CV_INST_BC, CV_COLOR_SOLID, bc_rgb, d, px);
            draw_inst_map(CV_INST_BOLTBC, CV_COLOR_SOLID, bc_rgb, d, px, R.cmap_view, true);   /* inside the bolt: in front */
        }
        if (d->loads) {
            draw_inst(CV_INST_LD, CV_COLOR_SOLID, ld_rgb, d, px);
            draw_inst(CV_INST_MOM, CV_COLOR_SOLID, mom_rgb, d, px);
            draw_inst(CV_INST_HEAT, CV_COLOR_SOLID, heat_rgb, d, px);
            draw_inst_map(CV_INST_BOLTLD, CV_COLOR_SOLID, ld_rgb, d, px, R.cmap_view, true);
        }
        if (d->vectors) draw_inst(CV_INST_VEC, d->vectors_color, vec_rgb, d, px);
        {   /* tensor glyphs, the cross and the trajectories. Coloured by sign: blue -lim, pale 0, red +lim */
            static const float glyph_rgb[3] = { 0.85f, 0.85f, 0.85f }, ten_rgb[3] = { 0.90f, 0.15f, 0.12f },
                               cmp_rgb[3] = { 0.15f, 0.35f, 0.95f };
            cv_draw c = *d;
            c.rmin = -d->sign_lim; c.rmax = d->sign_lim; c.bands = 0; memset(c.oor, 0, sizeof c.oor);
            bool sg = d->sign_lim > 0;
            if (d->tensors) {
                if (d->glyph_signed && sg) draw_glyphs(CV_COLOR_NODAL, glyph_rgb, &c, R.div_view);
                else draw_glyphs(d->glyph_signed ? CV_COLOR_SOLID : d->tensors_color, glyph_rgb, d, R.cmap_view);
                draw_inst_map(CV_INST_TENS, sg ? CV_COLOR_NODAL : CV_COLOR_SOLID, ten_rgb, sg ? &c : d, px, R.div_view, false);
                draw_inst_map(CV_INST_COMP, sg ? CV_COLOR_NODAL : CV_COLOR_SOLID, cmp_rgb, sg ? &c : d, px, R.div_view, false);
            }
            if (d->traj) {
                draw_inst_map(CV_INST_TRAJ1, sg ? CV_COLOR_NODAL : CV_COLOR_SOLID, ten_rgb, sg ? &c : d, px, R.div_view, false);
                draw_inst_map(CV_INST_TRAJ3, sg ? CV_COLOR_NODAL : CV_COLOR_SOLID, cmp_rgb, sg ? &c : d, px, R.div_view, false);
            }
        }
        if (d->links) draw_inst(CV_INST_LINK, CV_COLOR_SOLID, link_rgb, d, px);
        if (d->discrete) draw_inst(CV_INST_DISC, CV_COLOR_SOLID, disc_rgb, d, px);
        if (d->labels) draw_labels(d);
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
    for (int i = 0; i < CV_INST_N; i++) cv_render_inst(i, NULL, 0);
    cv_render_glyphs(NULL, 0);
    for (int i = 0; i < CV_AUX_N; i++) cv_render_aux(i, NULL, NULL, NULL, 0);
}
