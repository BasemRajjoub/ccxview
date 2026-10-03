/* app_loads.c -- what the deck applies, drawn on the model: supports, loads and
   thermal loads of the step on screen, each kind with a symbol of its own.

     supports (cyan)   held DOF: a cone per axis, a second base for a held rotation
                       prescribed displacement: an arrow with a bar across its tail
                       prescribed rotation: the moment symbol
                       held or prescribed temperature: a small cross
     loads (yellow)    force: an arrow, its head on the node
                       pressure: an arrow on the face
                       edge load (shell EDNOR, pressure on a plane element): an
                         arrow on the edge with a bar across its tail
                       gravity, body force: a block arrow leaving the body
                       centrifugal: the axis as a dashed line, a turning arc at its end
                       bolt preload: two arrows meeting in a ring on the section
     moments (magenta) a double-headed arrow, an arc turning with it round the shaft
     thermal (red)     heat into a node or a face: an arrow with a zigzag shaft
                       film (convection): a zigzag ending in a bar, the fluid
                       radiation: a stem ending in three rays
                       given temperature: a diamond; body heat: a zigzag block arrow

   A node in a *TRANSFORM has its DOFs along the local axes there, and its symbols
   follow them. Symbols are sized within their own kind by magnitude. Heads and supports
   are solid cones and every stroke a thin tube, so they read from any side; beyond
   40000 symbols they are plain lines. All in world
   units of G.sym_len; every vertex carries its node's displacement, so the symbols
   ride with the deformed shape. */
#include "app.h"
#include "inp.h"
#include <math.h>

/* One colour's symbols: lines (p, d: positions and 6 displacement values per vertex)
   and, when solid, triangles (tp, td): every stroke also as a tube of radius T, heads
   and supports as cones. Lines alone when there are too many symbols for that. */
typedef struct { cv_fvec p, d, tp, td; bool solid; float T; } layer;

static void vtx(layer* l, const float* a, const float* d) {
    if (cv_reserve(l->tp, l->tp.n + 3)) for (int k = 0; k < 3; k++) l->tp.a[l->tp.n++] = a[k];
    if (cv_reserve(l->td, l->td.n + 6)) for (int k = 0; k < 6; k++) l->td.a[l->td.n++] = d[k];
}
static void tri(layer* l, const float* a, const float* b, const float* c, const float* d) { vtx(l, a, d); vtx(l, b, d); vtx(l, c, d); }

static float norm3(float v[3]) {
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 0) for (int i = 0; i < 3; i++) v[i] /= n;
    return n;
}
static void cross3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
/* two unit vectors u, v across dir, right-handed: u x v = dir */
static void frame(const float dir[3], float u[3], float v[3]) {
    int k = fabsf(dir[0]) <= fabsf(dir[1]) ? (fabsf(dir[0]) <= fabsf(dir[2]) ? 0 : 2) : (fabsf(dir[1]) <= fabsf(dir[2]) ? 1 : 2);
    float up[3] = { 0, 0, 0 }; up[k] = 1;
    cross3(dir, up, u); norm3(u);
    cross3(dir, u, v);
}
static void along(const float p[3], const float dir[3], float t, float out[3]) {
    for (int i = 0; i < 3; i++) out[i] = p[i] + dir[i] * t;
}

/* a cylinder from a to b, n sides, with or without its end discs */
static void cyl(layer* l, const float a[3], const float b[3], float r, int n, bool caps, const float d[6]) {
    float ax[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, u[3], v[3], pa[3], pb[3], qa[3], qb[3];
    if (norm3(ax) <= 0) return;
    frame(ax, u, v);
    for (int s = 0; s <= n; s++) {
        float t = 6.2831853f * s / n, c = r * cosf(t), sn = r * sinf(t);
        for (int i = 0; i < 3; i++) { qa[i] = a[i] + c * u[i] + sn * v[i]; qb[i] = b[i] + c * u[i] + sn * v[i]; }
        if (s) {
            tri(l, pa, qa, qb, d); tri(l, pa, qb, pb, d);
            if (caps) { tri(l, a, qa, pa, d); tri(l, b, pb, qb, d); }
        }
        memcpy(pa, qa, sizeof pa); memcpy(pb, qb, sizeof pb);
    }
}
/* a cone: its tip at `tip`, pointing along dir, h long, r at the base */
static void cone(layer* l, const float tip[3], const float dir[3], float h, float r, const float d[6]) {
    enum { N = 10 };
    float u[3], v[3], c[3], p[3], q[3];
    frame(dir, u, v);
    along(tip, dir, -h, c);
    for (int s = 0; s <= N; s++) {
        float t = 6.2831853f * s / N;
        for (int i = 0; i < 3; i++) q[i] = c[i] + r * (cosf(t) * u[i] + sinf(t) * v[i]);
        if (s) { tri(l, tip, p, q, d); tri(l, c, q, p, d); }
        memcpy(p, q, sizeof p);
    }
}

/* a stroke: a line, and when solid a tube round it (the line still shows from far away) */
static void seg(layer* l, const float* a, const float* b, const float* d) {
    deck_seg(&l->p, &l->d, a, b, d);
    if (l->solid) cyl(l, a, b, l->T, 5, false, d);
}
/* an arrow head at tip along dir for an arrow len long: a cone, or two strokes */
static void head(layer* l, const float tip[3], const float dir[3], float len, const float d[6]) {
    if (l->solid) { cone(l, tip, dir, 0.3f * len, fmaxf(0.11f * len, 2.5f * l->T), d); return; }
    float u[3], v[3], w[3];
    frame(dir, u, v);
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        for (int i = 0; i < 3; i++) w[i] = tip[i] - dir[i] * 0.28f * len + sgn * 0.12f * len * u[i];
        deck_seg(&l->p, &l->d, tip, w, d);
    }
}
/* an arrow len long arriving at tip along dir */
static void arrow(layer* l, const float tip[3], const float dir[3], float len, const float d[6]) {
    float t[3], b[3];
    along(tip, dir, -len, t);
    along(tip, dir, l->solid ? -0.25f * len : 0, b);
    seg(l, t, b, d);
    head(l, tip, dir, len, d);
}

/* a circle (or part of one, from angle a0 over `turn` radians) of radius r about
   the axis dir through c; the last point comes back in `end` */
static void arc(layer* l, const float c[3], const float dir[3], float r, float a0, float turn, const float d[6], float end[3]) {
    enum { NSEG = 12 };
    float u[3], v[3], a[3], b[3] = { 0, 0, 0 };
    frame(dir, u, v);
    for (int s = 0; s <= NSEG; s++) {
        float t = a0 + turn * s / NSEG;
        for (int i = 0; i < 3; i++) a[i] = c[i] + r * (cosf(t) * u[i] + sinf(t) * v[i]);
        if (s) seg(l, b, a, d);
        memcpy(b, a, sizeof b);
    }
    if (end) memcpy(end, b, sizeof b);
}
/* three quarters of a circle about dir, ending in a head that turns the right-hand way */
static void turning(layer* l, const float c[3], const float dir[3], float r, const float d[6]) {
    float u[3], v[3], e[3];
    frame(dir, u, v);
    arc(l, c, dir, r, 0, 1.5f * 3.14159265f, d, e);
    float h = 0.45f * r;                            /* at 270 degrees: at -v, moving along +u */
    if (l->solid) {
        float t[3];
        along(e, u, 0.6f * h, t);
        cone(l, t, u, 1.1f * h, fmaxf(0.45f * h, 2.5f * l->T), d);
        return;
    }
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float w[3];
        for (int i = 0; i < 3; i++) w[i] = e[i] - h * u[i] + sgn * 0.6f * h * v[i];
        seg(l, e, w, d);
    }
}

/* a moment about dir at p: the double-headed arrow, and the turning arc round its shaft */
static void moment(layer* l, const float p[3], const float dir[3], float len, const float d[6]) {
    float c[3], t[3], b[3];
    along(p, dir, -len, t);
    along(p, dir, l->solid ? -0.5f * len : 0, b);
    seg(l, t, b, d);
    head(l, p, dir, len, d);
    along(p, dir, -0.27f * len, c);                 /* the second head, behind the first */
    head(l, c, dir, len, d);
    along(p, dir, -0.8f * len, c);                  /* near the tail, clear of the heads */
    turning(l, c, dir, 0.3f * len, d);
}

/* a bar across the tail of an arrow that arrives at tip along dir */
static void tail_bar(layer* l, const float tip[3], const float dir[3], float len, const float d[6]) {
    float u[3], v[3], t[3], a[3], b[3];
    frame(dir, u, v);
    along(tip, dir, -len, t);
    along(t, u, -0.22f * len, a); along(t, u, 0.22f * len, b); seg(l, a, b, d);
    along(t, v, -0.22f * len, a); along(t, v, 0.22f * len, b); seg(l, a, b, d);
}

/* a zigzag from `from` over len along dir (heat flowing) */
static void zigzag(layer* l, const float from[3], const float dir[3], float len, const float d[6]) {
    enum { N = 6 };
    float u[3], v[3], a[3], b[3];
    frame(dir, u, v);
    memcpy(b, from, sizeof b);
    for (int s = 1; s <= N; s++) {
        float side = s == N ? 0 : (s & 1 ? 0.1f : -0.1f) * len;
        for (int i = 0; i < 3; i++) a[i] = from[i] + dir[i] * len * s / N + u[i] * side;
        seg(l, b, a, d);
        memcpy(b, a, sizeof b);
    }
}
/* heat arriving at tip along dir: a zigzag shaft and an arrow head */
static void heat_arrow(layer* l, const float tip[3], const float dir[3], float len, const float d[6]) {
    float t[3];
    along(tip, dir, -len, t);
    float u[3], v[3], m[3];
    frame(dir, u, v);
    along(tip, dir, -0.3f * len, m);
    zigzag(l, t, dir, 0.7f * len, d);
    if (!l->solid) seg(l, m, tip, d);               /* a short straight end, and the head on it */
    head(l, tip, dir, len, d);
}
/* convection on a face at p (out: the outward normal): a zigzag ending in a bar, the fluid */
static void film(layer* l, const float p[3], const float out[3], float len, const float d[6]) {
    float u[3], v[3], e[3], a[3], b[3];
    frame(out, u, v);
    zigzag(l, p, out, len, d);
    along(p, out, len, e);
    along(e, u, -0.3f * len, a); along(e, u, 0.3f * len, b); seg(l, a, b, d);
    along(e, v, -0.3f * len, a); along(e, v, 0.3f * len, b); seg(l, a, b, d);
}
/* radiation from a face at p: a stem and three rays spreading from its end */
static void rays(layer* l, const float p[3], const float out[3], float len, const float d[6]) {
    float u[3], v[3], e[3], a[3];
    frame(out, u, v);
    along(p, out, 0.5f * len, e);
    seg(l, p, e, d);
    for (int k = 0; k < 3; k++) {
        float t = 2.0944f * k;
        for (int i = 0; i < 3; i++) a[i] = e[i] + 0.5f * len * (out[i] + 0.55f * (cosf(t) * u[i] + sinf(t) * v[i]));
        seg(l, e, a, d);
    }
    along(p, out, len, a);
    seg(l, e, a, d);
}
/* a given temperature at a node: a diamond */
static void diamond(layer* l, const float p[3], float r, const float d[6]) {
    float c[6][3];
    for (int k = 0; k < 6; k++) { memcpy(c[k], p, sizeof c[k]); c[k][k / 2] += k & 1 ? -r : r; }
    if (l->solid) {                                 /* the eight faces */
        for (int x = 0; x < 2; x++) for (int y = 2; y < 4; y++) for (int z = 4; z < 6; z++) tri(l, c[x], c[y], c[z], d);
        return;
    }
    for (int a = 0; a < 6; a++) for (int b = a + 1; b < 6; b++) if (a / 2 != b / 2) seg(l, c[a], c[b], d);
}

/* a load on a whole body, leaving it at `from` along dir: the outline of a broad
   arrow in two planes. zig: with a zigzag down its middle (heat). */
static void block_arrow(layer* l, const float from[3], const float dir[3], float len, const float d[6], bool zig) {
    float uv[2][3];
    frame(dir, uv[0], uv[1]);
    float w = 0.12f * len, hw = 0.26f * len, hl = 0.35f * len;
    for (int k = 0; k < 2; k++) {
        const float* u = uv[k];
        float q[7][3];
        const float along_[7] = { 0, len - hl, len - hl, len, len - hl, len - hl, 0 };
        const float side[7] = { -w, -w, -hw, 0, hw, w, w };
        for (int i = 0; i < 7; i++) for (int c = 0; c < 3; c++) q[i][c] = from[c] + dir[c] * along_[i] + u[c] * side[i];
        for (int i = 0; i < 7; i++) seg(l, q[i], q[(i + 1) % 7], d);
    }
    if (zig) zigzag(l, from, dir, len - hl, d);
}

/* supports at p from the mask of its held DOFs (bit k-1 for DOF k), along the axes
   Q (rows): per axis one cone with its tip on the node; a held rotation about the
   axis adds a plate behind its base (a rotation alone: a shorter cone). As lines:
   two crossed triangles, the plate a second base line. */
static void support(layer* l, const float p[3], const float d[6], unsigned mask, float L, const float Q[3][3]) {
    for (int k = 0; k < 3; k++) {
        bool tr = mask & (1u << k), rot = mask & (8u << k);
        if (!tr && !rot) continue;
        float h = tr ? L : 0.7f * L, r = l->solid ? 0.26f * L : 0.35f * L;
        if (l->solid) {
            float c[3], a[3], b[3];
            along(p, Q[k], -h, c);
            deck_seg(&l->p, &l->d, p, c, d);        /* its axis: what is left of it from far away */
            cone(l, p, Q[k], h, r, d);
            if (rot) {
                along(p, Q[k], -1.12f * h, a); along(p, Q[k], -1.22f * h, b);
                cyl(l, a, b, 1.25f * r, 12, true, d);
            }
            continue;
        }
        for (int m = 1; m <= 2; m++) {             /* the two triangles, across the other axes */
            const float* ej = Q[(k + m) % 3];
            float c[3], b0[3], b1[3];
            along(p, Q[k], -h, c);
            along(c, ej, -r, b0); along(c, ej, r, b1);
            seg(l, p, b0, d); seg(l, p, b1, d); seg(l, b0, b1, d);
            if (rot) {                              /* the second base, a little further out */
                along(b0, Q[k], -0.2f * h, b0); along(b1, Q[k], -0.2f * h, b1);
                seg(l, b0, b1, d);
            }
        }
    }
}
static void cross(layer* l, const float p[3], float r, const float d[6]) {
    for (int a = 0; a < 3; a++) {
        float e0[3], e1[3];
        memcpy(e0, p, sizeof e0); memcpy(e1, p, sizeof e1);
        e0[a] -= r; e1[a] += r;
        seg(l, e0, e1, d);
    }
}

/* ---- where things are ------------------------------------------------------------ */

static const float IDENT[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

/* the axes the DOFs of a deck node run along: its *TRANSFORM at the node's own
   (undeformed) place, else the global ones */
static void node_axes(const cv_inp* dk, uint32_t id, const float p[3], float Q[3][3]) {
    memcpy(Q, IDENT, sizeof IDENT);
    int t = cv_inp_node_transform(dk, id);
    if (t < 0 || t >= dk->ntransforms) return;
    double q[3][3];
    cv_csys_axes(&dk->transforms[t], p, q);
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) Q[i][j] = (float)q[i][j];
}

/* the deck's own element type (FRD code) of an element id, 0 unknown, and whether it is a shell */
static int deck_etype(const cv_inp* dk, uint32_t id, bool* shell) {
    const cv_frd* m = dk->mesh.n_elems ? &dk->mesh : &G.frd;
    uint32_t e = cv_frd_elem_index(m, id);
    *shell = dk->nshells && bsearch(&id, dk->shells, dk->nshells, sizeof(uint32_t), cv_cmp_u32);
    return e == UINT32_MAX ? 0 : m->etype[e];
}

static void mean_node(const uint32_t* c, int k, float cen[3], float cd[6]) {
    memset(cen, 0, 3 * sizeof(float)); memset(cd, 0, 6 * sizeof(float));
    for (int j = 0; j < k; j++) {
        float d[6];
        app_node_disp6(c[j], d);
        for (int m = 0; m < 3; m++) cen[m] += G.frd.xyz[3 * (size_t)c[j] + m] / k;
        for (int m = 0; m < 6; m++) cd[m] += d[m] / k;
    }
}

/* The middle of face `face` (or of edge `face`, edge true) of a deck element, its
   outward unit normal (an edge: outward in the element's plane) and its
   displacement. false when the element is not shown. An edge of a shell or plane
   element is a side face of the solid CalculiX expands it into (edge n: face n + 2),
   or a real edge when the results kept it flat. */
static bool face_at(uint32_t elem_id, int face, bool edge, float cen[3], float out[3], float cd[6]) {
    const cv_frd* f = &G.frd;
    uint32_t e = deck_elem(f, elem_id);
    if (e == UINT32_MAX || (G.vis && !G.vis[e])) return false;
    int ty = f->etype[e];
    const uint32_t* cn = f->conn + f->eoff[e];
    if (edge && ty >= 7 && ty <= 10) {              /* flat: the edge between two corners */
        int nc = ty <= 8 ? 3 : 4;
        uint32_t c[2] = { cn[face % nc], cn[(face + 1) % nc] };
        const float *p0 = f->xyz + 3 * cn[0], *p1 = f->xyz + 3 * cn[1], *p2 = f->xyz + 3 * cn[2];
        const float *a = f->xyz + 3 * c[0], *b = f->xyz + 3 * c[1];
        float e1[3], e2[3], n[3], t[3];
        for (int m = 0; m < 3; m++) { e1[m] = p1[m] - p0[m]; e2[m] = p2[m] - p0[m]; t[m] = b[m] - a[m]; }
        cross3(e1, e2, n);
        cross3(t, n, out);
        mean_node(c, 2, cen, cd);
        return norm3(out) > 0;
    }
    if (edge) face += 2;
    uint32_t c[4];
    int k = cv_elem_face_corners(f, e, face, c);
    if (k < 3) return false;
    const float *p0 = f->xyz + 3 * c[0], *p1 = f->xyz + 3 * c[1], *p2 = f->xyz + 3 * c[k - 1];
    float a[3], b[3];
    for (int m = 0; m < 3; m++) { a[m] = p1[m] - p0[m]; b[m] = p2[m] - p0[m]; }
    cross3(a, b, out);
    if (ty >= 1 && ty <= 6) for (int m = 0; m < 3; m++) out[m] = -out[m];    /* a solid's faces are listed turning inward */
    mean_node(c, k, cen, cd);
    return norm3(out) > 0;
}

/* the box of the shown nodes of an element set (or one element), and their mean displacement */
static bool body_box(const cv_inp* dk, const cv_body* b, float lo[3], float hi[3], float cd[6]) {
    const cv_frd* f = &G.frd;
    uint32_t one = b->elem, n = 1, cnt = 0;
    const uint32_t* ids = &one;
    if (b->set >= 0 && b->set < dk->nsets) { ids = dk->sets[b->set].ids; n = dk->sets[b->set].n; }
    for (int m = 0; m < 3; m++) { lo[m] = 1e30f; hi[m] = -1e30f; }
    memset(cd, 0, 6 * sizeof(float));
    for (uint32_t i = 0; i < n; i++) {
        uint32_t e = deck_elem(f, ids[i]);
        if (e == UINT32_MAX || (G.vis && !G.vis[e])) continue;
        for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) {
            const float* p = f->xyz + 3 * (size_t)f->conn[j];
            float d[6];
            app_node_disp6(f->conn[j], d);
            for (int m = 0; m < 3; m++) { lo[m] = fminf(lo[m], p[m]); hi[m] = fmaxf(hi[m], p[m]); }
            for (int m = 0; m < 6; m++) cd[m] += d[m];
            cnt++;
        }
    }
    for (int m = 0; m < 6 && cnt; m++) cd[m] /= cnt;
    return cnt > 0;
}
/* where a ray from the middle of a box along dir leaves it */
static void box_exit(const float lo[3], const float hi[3], const float dir[3], float out[3]) {
    float t = 1e30f, c[3];
    for (int m = 0; m < 3; m++) {
        c[m] = 0.5f * (lo[m] + hi[m]);
        if (fabsf(dir[m]) > 1e-6f) t = fminf(t, 0.5f * (hi[m] - lo[m]) / fabsf(dir[m]));
    }
    along(c, dir, t < 1e30f ? t : 0, out);
}

/* ---- the step on screen ---------------------------------------------------------- */

static struct { cv_applied a; const cv_inp* deck; int step; bool ok; } S;

/* the deck's step of the result on screen (0-based); without results the last one */
static int shown_step(const cv_inp* dk) {
    if (G.frd.n_steps && G.step >= 0 && G.step < G.frd.n_steps && G.frd.steps[G.step].step > 0)
        return CV_MIN(G.frd.steps[G.step].step - 1, CV_MAX(dk->nsteps - 1, 0));
    return CV_MAX(dk->nsteps - 1, 0);
}
static const cv_applied* applied(const cv_inp* dk) {
    int st = dk ? shown_step(dk) : -1;
    if (S.ok && S.deck == dk && S.step == st) return &S.a;
    if (S.ok) cv_applied_free(&S.a);
    S.ok = dk && cv_inp_applied(dk, st, &S.a);
    S.deck = dk; S.step = st;
    return S.ok ? &S.a : NULL;
}

static float rel(float v, float big) { return big > 0 ? 0.5f + 0.5f * fabsf(v) / big : 1.f; }

typedef struct { uint32_t node, mask; } bc_mask;
static int bc_mask_cmp(const void* a, const void* b) {
    uint32_t x = ((const bc_mask*)a)->node, y = ((const bc_mask*)b)->node;
    return x < y ? -1 : x > y;
}
static bool is_bolt_ref(const cv_inp* dk, uint32_t node) {
    for (uint32_t i = 0; i < dk->npret; i++) if (dk->pret[i].ref == node) return true;
    return false;
}

/* a bolt's cut: the middle of its section, the axis of the preload, how it moves, and
   the radius that holds the section (0 for a beam) */
static bool bolt_at(const cv_inp* dk, const cv_pretension* t, float cen[3], float axis[3], float cd[6], float* rad) {
    float n[3] = { 0, 0, 0 }, c[3], o[3], d[6];
    int cnt = 0;
    memset(cen, 0, 3 * sizeof(float)); memset(cd, 0, 6 * sizeof(float));
    if (t->surf >= 0 && t->surf < dk->nsurfs) {
        const cv_surface* s = &dk->surfs[t->surf];
        for (uint32_t i = 0; i < s->n; i++) {
            if (!face_at(s->elem[i], s->face[i], false, c, o, d)) continue;
            for (int m = 0; m < 3; m++) { cen[m] += c[m]; n[m] += o[m]; }
            for (int m = 0; m < 6; m++) cd[m] += d[m];
            cnt++;
        }
    } else if (t->elem) {                           /* a beam: between its end nodes */
        uint32_t e = deck_elem(&G.frd, t->elem);
        if (e != UINT32_MAX && G.frd.eoff[e + 1] - G.frd.eoff[e] >= 2) {
            const uint32_t* cn = G.frd.conn + G.frd.eoff[e];
            mean_node(cn, 2, cen, cd);
            for (int m = 0; m < 3; m++) n[m] = G.frd.xyz[3 * (size_t)cn[1] + m] - G.frd.xyz[3 * (size_t)cn[0] + m];
            cnt = 1;
            for (int m = 0; m < 3; m++) cen[m] *= cnt;
        }
    }
    if (!cnt) return false;
    for (int m = 0; m < 3; m++) cen[m] /= cnt;
    for (int m = 0; m < 6; m++) cd[m] /= cnt;
    memcpy(axis, t->has_dir ? t->dir : n, 3 * sizeof(float));
    if (norm3(axis) <= 0) return false;
    *rad = 0;
    for (uint32_t i = 0; t->surf >= 0 && i < dk->surfs[t->surf].n; i++) {      /* the corner furthest from the axis */
        const cv_surface* s = &dk->surfs[t->surf];
        uint32_t e = deck_elem(&G.frd, s->elem[i]), cn[4];
        int k = e == UINT32_MAX ? 0 : cv_elem_face_corners(&G.frd, e, s->face[i], cn);
        for (int j = 0; j < k; j++) {
            float r[3], al = 0;
            for (int m = 0; m < 3; m++) { r[m] = G.frd.xyz[3 * (size_t)cn[j] + m] - cen[m]; al += r[m] * axis[m]; }
            for (int m = 0; m < 3; m++) r[m] -= al * axis[m];
            *rad = fmaxf(*rad, sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]));
        }
    }
    return true;
}

void loads_refresh(void) {
    layer bc = {{0}}, ld = {{0}}, mo = {{0}}, ht = {{0}};
    layer* all[4] = { &bc, &ld, &mo, &ht };
    const cv_inp* dk = G.loaded ? deck_get() : NULL;
    const cv_applied* a = applied(dk);
    float L = CV_MAX(G.bc_scale, 0.01f) * G.sym_len, LL = 1.5f * CV_MAX(G.load_scale, 0.01f) * G.sym_len;
    if (a) {
        float p[3], d[6], Q[3][3], dir[3];
        /* solid symbols while there are not too many of them (a cone is 20 triangles) */
        bool solid = (uint64_t)a->nbcs + a->ncloads + a->ndloads + a->ntemps <= 40000;
        for (int k = 0; k < 4; k++) { all[k]->solid = solid; all[k]->T = 0.045f * G.sym_len * CV_MIN(CV_MAX(G.sym_thick, 0.1f), 10.f); }

        /* sized within each kind: their units differ */
        float fmax = 0, mmax = 0, qmax = 0, umax = 0, rmax = 0, dmax[CV_DL_N] = { 0 }, bmax[CV_BL_N] = { 0 };
        for (uint32_t i = 0; i < a->ncloads; i++) {
            float v = fabsf(a->cloads[i].value);
            if (is_bolt_ref(dk, a->cloads[i].node)) continue;
            if (a->cloads[i].dof == 11) qmax = fmaxf(qmax, v); else if (a->cloads[i].dof > 3) mmax = fmaxf(mmax, v); else fmax = fmaxf(fmax, v);
        }
        for (uint32_t i = 0; i < a->nbcs; i++) {
            float v = fabsf(a->bcs[i].value);
            if (a->bcs[i].dof_lo <= 3) umax = fmaxf(umax, v); else if (a->bcs[i].dof_lo <= 6) rmax = fmaxf(rmax, v);
        }
        for (uint32_t i = 0; i < a->ndloads; i++) dmax[a->dloads[i].kind] = fmaxf(dmax[a->dloads[i].kind], fabsf(a->dloads[i].value));
        for (uint32_t i = 0; i < a->nbody; i++) bmax[a->body[i].kind] = fmaxf(bmax[a->body[i].kind], fabsf(a->body[i].value));

        /* supports: one symbol per node for its held DOFs; a prescribed value on its own */
        bc_mask* m = a->nbcs ? malloc(a->nbcs * sizeof *m) : NULL;
        uint32_t nm = 0;
        for (uint32_t i = 0; m && i < a->nbcs; i++) {
            const cv_bc* b = &a->bcs[i];
            int dof = b->dof_lo;
            if (is_bolt_ref(dk, b->node)) continue;         /* a bolt tightened by a displacement: drawn at its cut */
            if (dof == 11) m[nm++] = (bc_mask){ b->node, 64 };
            else if (dof >= 1 && dof <= 6 && b->value == 0) m[nm++] = (bc_mask){ b->node, 1u << (dof - 1) };
            else if (dof >= 1 && dof <= 6 && deck_node_pd(b->node, p, d)) {
                node_axes(dk, b->node, p, Q);
                for (int k = 0; k < 3; k++) dir[k] = Q[(dof - 1) % 3][k] * (b->value < 0 ? -1.f : 1.f);
                float len = LL * rel(b->value, dof > 3 ? rmax : umax);
                if (dof > 3) moment(&bc, p, dir, len, d);
                else { arrow(&bc, p, dir, len, d); tail_bar(&bc, p, dir, len, d); }
            }
        }
        if (nm) qsort(m, nm, sizeof *m, bc_mask_cmp);
        for (uint32_t i = 0; i < nm;) {
            uint32_t node = m[i].node, bits = 0;
            for (; i < nm && m[i].node == node; i++) bits |= m[i].mask;
            if (!deck_node_pd(node, p, d)) continue;
            node_axes(dk, node, p, Q);
            if (bits & 64) cross(&bc, p, 0.25f * L, d);
            support(&bc, p, d, bits, L, Q);
        }
        free(m);

        /* point loads: forces, moments, heat */
        for (uint32_t i = 0; i < a->ncloads; i++) {
            const cv_cload* c = &a->cloads[i];
            if (c->value == 0 || is_bolt_ref(dk, c->node) || !deck_node_pd(c->node, p, d)) continue;
            if (c->dof == 11) {                     /* no direction of its own: from outside the model */
                for (int k = 0; k < 3; k++) dir[k] = 0.5f * ((&G.bmin.x)[k] + (&G.bmax.x)[k]) - p[k];
                if (norm3(dir) <= 0) { dir[0] = 0; dir[1] = 0; dir[2] = -1; }
                float len = LL * rel(c->value, qmax);
                if (c->value > 0) heat_arrow(&ht, p, dir, len, d);
                else { float t[3], o[3] = { -dir[0], -dir[1], -dir[2] }; along(p, o, len, t); heat_arrow(&ht, t, o, len, d); }
                continue;
            }
            node_axes(dk, c->node, p, Q);
            for (int k = 0; k < 3; k++) dir[k] = Q[(c->dof - 1) % 3][k] * (c->value < 0 ? -1.f : 1.f);
            if (c->dof > 3) moment(&mo, p, dir, LL * rel(c->value, mmax), d);
            else arrow(&ld, p, dir, LL * rel(c->value, fmax), d);
        }

        /* on faces and edges */
        for (uint32_t i = 0; i < a->ndloads; i++) {
            const cv_dload* q = &a->dloads[i];
            float cen[3], out[3], cd[6], in[3];
            bool shell;
            int ty = deck_etype(dk, q->elem, &shell);
            bool edge = q->kind == CV_DL_EDGE || (ty >= 7 && ty <= 10 && !shell);
            if (q->value == 0 || !face_at(q->elem, q->face, edge, cen, out, cd)) continue;
            float len = LL * rel(q->value, dmax[q->kind]);
            float sgn = q->value < 0 ? 1.f : -1.f;              /* positive: into the face */
            for (int k = 0; k < 3; k++) in[k] = out[k] * sgn;
            switch (q->kind) {
                case CV_DL_P:    arrow(&ld, cen, in, len, cd); if (edge) tail_bar(&ld, cen, in, len, cd); break;
                case CV_DL_EDGE: arrow(&ld, cen, in, len, cd); tail_bar(&ld, cen, in, len, cd); break;
                case CV_DL_FLUX:
                    if (q->value > 0) heat_arrow(&ht, cen, in, len, cd);
                    else { float t[3]; along(cen, out, len, t); heat_arrow(&ht, t, out, len, cd); }
                    break;
                case CV_DL_FILM: film(&ht, cen, out, len, cd); break;
                case CV_DL_RAD:  rays(&ht, cen, out, len, cd); break;
            }
        }

        /* on whole bodies */
        for (uint32_t i = 0; i < a->nbody; i++) {
            const cv_body* b = &a->body[i];
            float lo[3], hi[3], cd[6], from[3], c[3];
            if (b->kind == CV_BL_NEWTON || !body_box(dk, b, lo, hi, cd)) continue;
            for (int k = 0; k < 3; k++) c[k] = 0.5f * (lo[k] + hi[k]);
            float len = 1.3f * LL * rel(b->value, bmax[b->kind]);
            if (b->kind == CV_BL_CENTRIF) {         /* the axis through the body, a turning arc at its end */
                float ax[3] = { b->v[3], b->v[4], b->v[5] }, e0[3], e1[3];
                if (norm3(ax) <= 0) continue;
                float t = 0, half = 0;
                for (int k = 0; k < 3; k++) { t += (c[k] - b->v[k]) * ax[k]; half += 0.5f * (hi[k] - lo[k]) * fabsf(ax[k]); }
                half += 1.5f * LL;
                for (int s = 0; s < 9; s += 2) {    /* dashed */
                    along(b->v, ax, t - half + 2 * half * s / 9, e0);
                    along(b->v, ax, t - half + 2 * half * (s + 1) / 9, e1);
                    seg(&ld, e0, e1, cd);
                }
                turning(&ld, e1, ax, 0.6f * LL, cd);
                continue;
            }
            if (b->kind == CV_BL_HEAT) { dir[0] = 0; dir[1] = 0; dir[2] = 1; }
            else { memcpy(dir, b->v, sizeof dir); if (b->value < 0) for (int k = 0; k < 3; k++) dir[k] = -dir[k]; }
            if (norm3(dir) <= 0) continue;
            box_exit(lo, hi, dir, from);
            block_arrow(b->kind == CV_BL_HEAT ? &ht : &ld, from, dir, len, cd, b->kind == CV_BL_HEAT);
        }

        /* bolts: at the cut, sized by the preload on the reference node */
        float tmax = 0;
        for (uint32_t i = 0; i < a->ncloads; i++) if (is_bolt_ref(dk, a->cloads[i].node)) tmax = fmaxf(tmax, fabsf(a->cloads[i].value));
        for (uint32_t i = 0; i < dk->npret; i++) {
            const cv_pretension* t = &dk->pret[i];
            float cen[3], ax[3], cd[6], back[3], u[3], w[3], rad = 0, v = 0;
            bool given = false;
            for (uint32_t j = 0; j < a->ncloads; j++) if (a->cloads[j].node == t->ref) { v = a->cloads[j].value; given = true; }
            for (uint32_t j = 0; j < a->nbcs && !given; j++) if (a->bcs[j].node == t->ref) given = true;
            if (!given || !bolt_at(dk, t, cen, ax, cd, &rad)) continue;
            /* a ring round the section, outside the body, and on it pairs of arrows along
               the axis: meeting at the cut when tightened, parting when loosened */
            float len = LL * rel(v, tmax), r = rad > 0 ? 1.12f * rad : 0.45f * len;
            for (int k = 0; k < 3; k++) back[k] = -ax[k];
            frame(ax, u, w);
            arc(&ld, cen, ax, r, 0, 6.2831853f, cd, NULL);
            for (int q = 0; q < 4; q++) {
                float at[3], tip[3];
                const float* e = q & 1 ? w : u;
                along(cen, e, q & 2 ? -r : r, at);
                if (v < 0) {
                    along(at, ax, len, tip); arrow(&ld, tip, ax, len, cd);
                    along(at, back, len, tip); arrow(&ld, tip, back, len, cd);
                } else {
                    arrow(&ld, at, ax, len, cd);
                    arrow(&ld, at, back, len, cd);
                }
            }
        }

        /* given temperatures */
        for (uint32_t i = 0; i < a->ntemps; i++)
            if (deck_node_pd(a->temps[i].node, p, d)) diamond(&ht, p, 0.3f * L, d);
    }
    static const int ln[4] = { CV_AUX_BCLN, CV_AUX_LDLN, CV_AUX_MOMLN, CV_AUX_HEATLN };
    static const int tr[4] = { CV_AUX_BCTRI, CV_AUX_LDTRI, CV_AUX_MOMTRI, CV_AUX_HEATTRI };
    for (int k = 0; k < 4; k++) {
        app_aux_upload(ln[k], &all[k]->p, &all[k]->d, NULL);
        app_aux_upload(tr[k], &all[k]->tp, &all[k]->td, NULL);
        cv_free_vec(all[k]->p); cv_free_vec(all[k]->d); cv_free_vec(all[k]->tp); cv_free_vec(all[k]->td);
    }
}
