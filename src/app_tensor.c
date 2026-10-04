/* app_tensor.c -- the tensor glyph layer: a stress or strain field drawn as one
   glyph per shown element (glyph.h), at the element's centre, from the mean of its
   node values. Ellipsoids and superquadrics go to cv_render_glyphs, coloured by the
   element's value of the selected scalar (the legend applies); the principal cross
   goes to the symbol layers CV_INST_TENS / COMP, red for tension, blue for
   compression: plain, or (coloured) each bar by its value on a cool-warm scale
   to +-peak, pale near zero. The sign-aware glyphs (Schultz-Kindlmann, Reynolds,
   HWY) take the same scale, per direction: the normal stress there. The glyph of the 98th-percentile magnitude is tensor_scale times
   the mean element size (times the spacing of a sample on huge models); the
   few above it are capped there, their colour still telling. */
#include "app_int.h"
#include "glyph.h"
#include <math.h>

bool app_field_is_tensor(void) {
    if (!G.loaded || G.field_src != 0) return false;
    int fi = find_field(G.step, G.field_name);
    return fi >= 0 && cv_tensor_order(&G.frd.steps[G.step].fields[fi]) != 0;
}

static void clear_layers(void) {
    cv_render_glyphs(NULL, 0);
    cv_render_inst(CV_INST_TENS, NULL, 0);
    cv_render_inst(CV_INST_COMP, NULL, 0);
}

/* centre, mean tensor and displacement of element e; false if a node has no value */
static bool elem_tensor(uint32_t e, const float* v, float c[3], float s[6], float d[6], float* size) {
    uint32_t a = G.frd.eoff[e], b = G.frd.eoff[e + 1];
    if (b <= a) return false;
    double cs[3] = { 0 }, ss[6] = { 0 }, ds[6] = { 0 };
    float lo[3] = { INFINITY, INFINITY, INFINITY }, hi[3] = { -INFINITY, -INFINITY, -INFINITY };
    for (uint32_t k = a; k < b; k++) {
        uint32_t i = G.frd.conn[k];
        const float* r = v + 6 * (size_t)i, *p = G.frd.xyz + 3 * (size_t)i;
        float di[6];
        app_node_disp6(i, di);
        for (int j = 0; j < 6; j++) {
            if (r[j] != r[j]) return false;
            ss[j] += r[j];
            ds[j] += di[j];
        }
        for (int j = 0; j < 3; j++) {
            cs[j] += p[j];
            lo[j] = CV_MIN(lo[j], p[j]); hi[j] = CV_MAX(hi[j], p[j]);
        }
    }
    double n = b - a;
    for (int j = 0; j < 6; j++) { s[j] = (float)(ss[j] / n); d[j] = (float)(ds[j] / n); }
    for (int j = 0; j < 3; j++) c[j] = (float)(cs[j] / n);
    /* the edge of a cube with the box's diagonal: a fair element size for any shape */
    float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    *size = sqrtf(dx * dx + dy * dy + dz * dz) * 0.57735f;
    return true;
}

/* one bar of the cross: through c along dir, half-length h, heads out for tension, in for compression */
/* the 98th percentile of the largest principal magnitude over the shown elements'
   centres (a sample of 40k on huge models), kept until the field or the mask changes */
float app_tensor_ref(void) {
    static uint32_t gen = UINT32_MAX; static const uint8_t* vis; static float ref;
    if (gen == G.field_gen && vis == G.vis) return ref;
    gen = G.field_gen; vis = G.vis; ref = 0;
    if (!G.has_field || !app_field_is_tensor()) return ref;
    int fi = find_field(G.step, G.field_name);
    const cv_field_desc* fd = &G.frd.steps[G.step].fields[fi];
    const float* v = cache_get(G.step, fi);
    if (!v || fd->ncomp < 6) return ref;
    bool xz = cv_tensor_order(fd) == 2;
    uint32_t n = G.frd.n_elems, shown = 0;
    for (uint32_t e = 0; e < n; e++) shown += !G.vis || G.vis[e];
    uint32_t stride = shown / 40000 + 1;
    CV_VEC(float) mag = {0};
    for (uint32_t e = 0, k = 0; e < n; e++) {
        if (G.vis && !G.vis[e]) continue;
        if (k++ % stride) continue;
        float c[3], s6[6], d[6], sz, val[3];
        if (!elem_tensor(e, v, c, s6, d, &sz)) continue;
        cv_principal(s6, xz, val);
        float m = CV_MAX(fabsf(val[0]), fabsf(val[2]));
        if (m > 0 && !cv_push(mag, m)) break;
    }
    ref = cv_glyph_ref(mag.a, mag.n, 0.98f);
    cv_free_vec(mag);
    return ref;
}

/* the scale of the layers coloured by sign, while one of them is coloured */
float app_tensor_cross_lim(void) {
    bool on = G.tensor_colored && ((G.show_tensor && (G.tensor_style == CV_GLYPH_CROSS || cv_glyph_signed(G.tensor_style)))
                                   || G.show_traj);
    return on ? app_tensor_ref() : 0.f;
}

static void cross_bar(cv_fvec* in, const float c[3], const float dir[3], float h, float r, const float d[6], float val) {
    bool tension = val >= 0;
    if (h < 3 * r) {                              /* too short for heads: a plain bar, still there */
        float a[3], b[3];
        for (int k = 0; k < 3; k++) { a[k] = c[k] - dir[k] * h; b[k] = c[k] + dir[k] * h; }
        deck_inst(in, a, b, r, r, val, d);
        return;
    }
    for (int sg = -1; sg <= 1; sg += 2) {
        float tip[3], neck[3];
        for (int k = 0; k < 3; k++) { tip[k] = c[k] + sg * dir[k] * h; neck[k] = c[k] + sg * dir[k] * 0.7f * h; }
        deck_inst(in, c, neck, r, r, val, d);
        if (tension) deck_inst(in, neck, tip, 2.5f * r, 0, val, d);
        else deck_inst(in, tip, neck, 2.5f * r, 0, val, d);
    }
}

void refresh_tensors(void) {
    clear_layers();
    if (!G.show_tensor || !G.has_field || !app_field_is_tensor()) return;
    int fi = find_field(G.step, G.field_name);
    const cv_field_desc* fd = &G.frd.steps[G.step].fields[fi];
    const float* v = cache_get(G.step, fi);
    if (!v || fd->ncomp < 6) return;
    bool xz = cv_tensor_order(fd) == 2;
    int style = G.tensor_style >= 0 && G.tensor_style < CV_GLYPH_N ? G.tensor_style : CV_GLYPH_ELLIPSOID;

    /* first pass: the elements drawn, the largest principal magnitude, the mean size */
    uint32_t n = G.frd.n_elems, shown = 0;
    for (uint32_t e = 0; e < n; e++) shown += !G.vis || G.vis[e];
    uint32_t stride = shown / 40000 + 1;                        /* huge models: a sample (denser is clutter) */
    typedef struct { uint32_t e; float c[3], s[6], d[6]; } item;
    CV_VEC(item) it = {0};
    CV_VEC(float) mag = {0};
    double size_sum = 0;
    for (uint32_t e = 0, k = 0; e < n; e++) {
        if (G.vis && !G.vis[e]) continue;
        if (k++ % stride) continue;
        item x = { .e = e };
        float sz, val[3];
        if (!elem_tensor(e, v, x.c, x.s, x.d, &sz)) continue;
        cv_principal(x.s, xz, val);
        float m = CV_MAX(fabsf(val[0]), fabsf(val[2]));
        if (!(m > 0)) continue;
        if (!cv_reserve(mag, it.n + 1) || !cv_push(it, x)) break;
        mag.a[mag.n++] = m;
        size_sum += sz;
    }
    /* sized to the 98th percentile: the few above it (singular corners) are capped */
    float peak = cv_glyph_ref(mag.a, mag.n, 0.98f);
    cv_free_vec(mag);
    if (!it.n || !(peak > 0)) { cv_free_vec(it); return; }
    float h = (float)(size_sum / (double)it.n) * cbrtf((float)stride);   /* a sample: the spacing of the glyphs drawn */
    float k = 0.5f * CV_MAX(G.tensor_scale, 0.01f) * h / peak;   /* model length per unit of the tensor */

    cv_fvec gl = {0}, ten = {0}, cmp = {0};
    bool signed_col = cv_glyph_signed(style) && G.tensor_colored;
    float r = 0.04f * k * peak;                                   /* the cross's bar radius */
    for (size_t j = 0; j < it.n; j++) {
        const item* x = &it.a[j];
        cv_glyph g;
        if (!cv_glyph_make(x->s, xz, style, k, 0.12f, &g)) continue;   /* rods and discs keep some body */
        float f = CV_MIN(1.f, k * peak / cv_glyph_extent(&g));   /* the cap, kept for the cross too */
        cv_glyph_cap(&g, k * peak);
        if (style == CV_GLYPH_CROSS) {
            /* every principal value gets its bar, down to 1 % of the reference: a uniaxial
               state (a beam in bending) shows one, pure shear two, a general state three */
            for (int a = 0; a < 3; a++) {
                float hl = fabsf(g.val[a]) * k * f;
                if (hl > 0.25f * r)
                    cross_bar(g.val[a] >= 0 ? &ten : &cmp, x->c, g.axis[a], hl, r, x->d, g.val[a]);
            }
            continue;
        }
        if (!cv_reserve(gl, gl.n + CV_GLYPH_FLOATS)) break;
        float* o = gl.a + gl.n;
        float sv = G.elem_val ? G.elem_val[x->e] : NAN;
        o[0] = x->c[0]; o[1] = x->c[1]; o[2] = x->c[2]; o[3] = sv;
        for (int a = 0; a < 3; a++) {
            for (int q = 0; q < 3; q++) o[4 + 4 * a + q] = g.base[a][q] * g.len[a];
            o[16 + a] = g.lam[a];
        }
        o[7] = g.alpha; o[11] = g.beta; o[15] = g.cee;
        o[19] = (float)g.shape + (signed_col ? 4.f : 0.f);
        memcpy(o + 20, x->d, 6 * sizeof(float));
        gl.n += CV_GLYPH_FLOATS;
    }
    cv_render_glyphs(gl.a, (uint32_t)(gl.n / CV_GLYPH_FLOATS));
    cv_render_inst(CV_INST_TENS, ten.a, (uint32_t)(ten.n / CV_INST_FLOATS));
    cv_render_inst(CV_INST_COMP, cmp.a, (uint32_t)(cmp.n / CV_INST_FLOATS));
    cv_free_vec(gl); cv_free_vec(ten); cv_free_vec(cmp); cv_free_vec(it);
}

void app_tensors_changed(void) { refresh_tensors(); }
void app_traj_changed(void) { refresh_traj(); }
