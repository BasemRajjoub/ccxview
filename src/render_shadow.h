/* render_shadow.h -- the targets and passes of the Studio look's shadows (render.c draws the
   faces into them; this file owns the textures and the fullscreen passes).

   Contact shadow: the faces seen from below, orthographically, into a small depth map
   covering the floor's area (it does not depend on the camera: render.c keeps it until the
   shape changes); a pass then turns that depth into the shadow on the floor, once: a
   tight ring of taps weighted by how near the floor the surface is (what touches the floor
   is dark under it) and a wide ring, fainter, for what is higher. The floor shader just
   reads the result. */
#ifndef CV_RENDER_SHADOW_H
#define CV_RENDER_SHADOW_H

#include "sokol_gfx.h"

enum { CV_CONTACT_SIZE = 512 };

/* the contact map's depth attachment (made on first use) */
sg_view cv_sh_contact_target(void);
/* after the faces were drawn into it: the shadow, from the depth. diag-relative: hc the map's
   half side, hmax the height its depth spans (1 = hmax above the floor), both in model units */
void    cv_sh_contact_run(float diag, float hc, float hmax);
sg_view cv_sh_contact_view(void);        /* the shadow, one channel, 1 = dark; {0} before the first run */
sg_sampler cv_sh_linear(void);           /* clamped, bilinear: for reading it */
void    cv_sh_free(void);

/* Key-light shadow map. The key light is attached to the camera (view space), so the map is
   drawn every frame: an orthographic depth pass of the faces from the light, fitted to the
   model's bounding sphere. The shaders sample it with a comparison sampler: a slope-scaled
   bias, a normal offset and eight rotated taps. */
enum { CV_SHADOW_SIZE = 2048, CV_SHADOW_SOFT = 1024 };
/* the map's depth attachment, size x size; size 0: none this frame (a dummy is kept for
   the shaders to bind). Call outside a pass. */
sg_view cv_sh_key_target(int size);
sg_view cv_sh_key_view(void);            /* the map as a texture (the dummy before the first) */
sg_view cv_sh_key_dummy(void);           /* a 1 x 1 map that is never lit-or-dark: bound while drawing into the map */
sg_sampler cv_sh_key_cmp(void);          /* comparison, bilinear: a 2 x 2 filter per tap */
/* lm: view space -> the light's clip space, for a sphere of radius r around c (view space), the
   light's direction (towards it) jittered by j. Fills dir with that direction. */
void    cv_sh_key_matrix(float lm[16], float dir[3], const float c[3], float r, const float j[2]);
/* the key light's direction in view space, jittered: the shaders' kl */
void    cv_sh_key_dir(float dir[3], const float j[2]);

/* the shaders' side: the key shadow's uniforms (u_shm view -> light clip, u_sh strength / texel
   in view units / jitter x y, SH_INV 1 / size, u_shadow) and shadow(p, n, l): 1 lit .. 0 in shadow */
#define SHADOW_GLSL \
    "float shadow(vec3 p, vec3 nf, vec3 l) {\n" \
    "  if (u_sh.x <= 0.0) return 1.0;\n" \
    "  float nl = clamp(dot(nf, l), 0.0, 1.0);\n" \
    "  vec4 q = u_shm * vec4(p + nf * u_sh.y * (1.0 - nl), 1.0);\n" \
    "  vec3 s = q.xyz * 0.5 + 0.5;\n" \
    "  if (s.x < 0.0 || s.x > 1.0 || s.y < 0.0 || s.y > 1.0 || s.z > 1.0) return 1.0;\n" \
    "  float tn = sqrt(max(1.0 - nl * nl, 0.0)) / max(nl, 0.1);\n" \
    "  float ref = s.z - SH_INV * (1.5 + 1.5 * min(tn, 4.0));\n" \
    "  float rot = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n" \
    "  float sum = 0.0;\n" \
    "  for (int i = 0; i < 8; i++) {\n" \
    "    float r = sqrt((float(i) + 0.5) / 8.0), a = float(i) * 2.3999632 + rot;\n" \
    "    sum += textureLod(u_shadow, vec3(s.xy + vec2(cos(a), sin(a)) * r * 2.5 * SH_INV, ref), 0.0);\n" \
    "  }\n" \
    "  return mix(1.0, sum / 8.0, u_sh.x);\n" \
    "}\n"

#endif
