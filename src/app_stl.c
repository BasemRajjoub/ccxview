/* app_stl.c -- imported geometry: STL files shown beside the results (a pin, a
   clamp, the housing around the part), parts of the assembly that were not
   analysed. Mesh only: no ids, no results, never deformed, not probed, not on the
   mirror copies; the view's point under the cursor (turn about it, zoom to it,
   centre on it) finds them as it finds the model. They belong to the model: a
   reload keeps them, another model clears them. Each is a layer with its own
   colour, opacity and unit scale. An STL opened with no model is not a layer but
   the model (app_load.c). */
#include "app_int.h"
#include "stl.h"
#include "log.h"
#include <math.h>

typedef struct {
    cv_stl_layer l;
    char     name[256];
    cv_stl   mesh;             /* as read, welded, the file's units */
    uint32_t* edge;            /* its outline: vertex pairs */
    uint32_t n_edge;
} layer;

static layer L[CV_MESH_N];
static int nl;

/* the layer's vertices scaled, to the GPU */
static void upload(int i) {
    const cv_stl* m = &L[i].mesh;
    float* p = malloc((size_t)m->n_vert * 3 * sizeof *p);
    if (!p) { cv_render_mesh(i, NULL, 0, NULL, 0, NULL, 0); return; }
    for (size_t k = 0; k < (size_t)m->n_vert * 3; k++) p[k] = m->xyz[k] * L[i].l.scale;
    cv_render_mesh(i, p, m->n_vert, m->tri, m->n_tri, L[i].edge, L[i].n_edge);
    free(p);
}

static void changed(void) {
    if (G.loaded) view_bounds();     /* the depth range takes them in */
}

/* why an import failed: in the status bar and the Messages window, never nothing */
static void say(const char* msg) {
    cv_msg_add(&G.msgs, 0, false, msg);
    snprintf(G.note, sizeof G.note, "%s", msg);
    G.note_t = cv_now();
    G.show_msgs = true;
    cv_logf("%s", msg);
}

bool app_stl_add(const char* path) {
    if (!path || !path[0]) return false;
    char err[256], msg[400];
    if (!G.loaded) { snprintf(msg, sizeof msg, "import STL %s: no model open to add it to", cv_basename(path)); say(msg); return false; }
    char abs[1024];
    if (!cv_abs_path(path, abs, sizeof abs)) snprintf(abs, sizeof abs, "%s", path);
    for (int i = 0; i < nl; i++) if (!strcmp(L[i].l.path, abs)) return true;   /* listed already */
    if (nl >= CV_MESH_N) {
        snprintf(msg, sizeof msg, "import STL: %d files at most; %s left out", CV_MESH_N, cv_basename(path));
        say(msg);
        return false;
    }
    layer* y = &L[nl];
    memset(y, 0, sizeof *y);
    if (!cv_stl_read(&y->mesh, abs, true, err, sizeof err)) {
        snprintf(msg, sizeof msg, "import STL %s: %s", cv_basename(path), err);
        say(msg);
        return false;
    }
    if (y->mesh.skipped) {
        snprintf(msg, sizeof msg, "import STL %s: %u triangles with a coordinate that is not a number left out",
                 cv_basename(path), y->mesh.skipped);
        cv_msg_add(&G.msgs, 0, false, msg);
    }
    if (!cv_stl_edges(&y->mesh, G.outline_angle, &y->edge, &y->n_edge)) y->n_edge = 0;   /* the model's crease angle */
    snprintf(y->l.path, sizeof y->l.path, "%s", abs);
    snprintf(y->name, sizeof y->name, "%s", cv_basename(abs));
    y->l.visible = true;
    y->l.alpha = O.stl_alpha >= 0 ? CV_MIN(O.stl_alpha, 1.f) : 1.f;
    y->l.rgb[0] = 0.55f; y->l.rgb[1] = 0.62f; y->l.rgb[2] = 0.72f;    /* a neutral grey-blue, apart from the model's grey */
    y->l.scale = 1.f;
    nl++;
    upload(nl - 1);
    changed();
    cv_logf("imported %s: %u triangles, %u vertices, %s", y->name, y->mesh.n_tri, y->mesh.n_vert, y->mesh.binary ? "binary" : "ASCII");
    snprintf(G.note, sizeof G.note, "imported %s: %u triangles (Groups > Imported geometry)", y->name, y->mesh.n_tri);
    G.note_t = cv_now();
    return true;
}

/* ---- files waiting for the model being loaded -------------------------------------- */
static char queue[CV_MESH_N][1024];
static int nq;

void app_stl_queue(const char* path) {
    for (int i = 0; i < nq; i++) if (!strcmp(queue[i], path)) return;
    if (nq < CV_MESH_N) snprintf(queue[nq++], sizeof queue[0], "%s", path);
}

void app_stl_queue_add(void) {
    int n = nq;
    nq = 0;
    for (int i = 0; i < n; i++) app_stl_add(queue[i]);
}

void app_stl_queue_drop(void) {
    char msg[1100];
    for (int i = 0; i < nq; i++) {
        snprintf(msg, sizeof msg, "import STL %s: the model did not open, so it was not added", cv_basename(queue[i]));
        cv_msg_add(&G.msgs, 0, false, msg);
    }
    nq = 0;
}

void app_stl_import(const char* path) {
    if (!path || !path[0]) return;
    if (G.job.kind == JOB_LOAD) app_stl_queue(path);
    else if (G.loaded) app_stl_add(path);
    else app_open(path);                    /* nothing to add it to: it is the model */
}

/* a file's rank as the model among several dropped: .frd first; -1 an STL, 9 neither */
static int model_rank(const char* p) {
    static const char* const ext[] = { ".frd", ".inp", ".fbd", ".dat" };
    for (int k = 0; k < 4; k++) if (cv_ends_with_ci(p, ext[k])) return k;
    return cv_ends_with_ci(p, ".stl") ? -1 : 9;
}

void app_open_files(const char* const* paths, int n) {
    int best = -1;
    for (int i = 0; i < n; i++) {
        int r = model_rank(paths[i]);
        if (r >= 0 && (r < 9 || n == 1) && (best < 0 || r < model_rank(paths[best]))) best = i;   /* anything else alone: app_open says what */
    }
    if (best >= 0) app_open(paths[best]);
    for (int i = 0; i < n; i++)
        if (model_rank(paths[i]) < 0) app_open(paths[i]);   /* a load running: they wait for it; else they join, or the first is the model */
}

/* ---- the point under the cursor --------------------------------------------------- */
float app_stl_ray(const float o[3], const float d[3], int* layer) {
    float best = INFINITY;
    if (layer) *layer = -1;
    for (int i = 0; i < nl; i++) {
        if (!L[i].l.visible) continue;
        const cv_stl* m = &L[i].mesh;
        double s = L[i].l.scale;
        /* the ray in the file's units: t comes out divided by the scale */
        double oo[3] = { o[0] / s, o[1] / s, o[2] / s }, dd[3] = { d[0], d[1], d[2] };
        double t0 = 0, t1 = INFINITY;                        /* the bounding box first: most rays miss most layers */
        for (int k = 0; k < 3 && t0 <= t1; k++) {
            double pad = 1e-6 * ((double)m->hi[k] - m->lo[k]) + 1e-12;
            if (fabs(dd[k]) < 1e-30) { if (oo[k] < m->lo[k] - pad || oo[k] > m->hi[k] + pad) t1 = -1; continue; }
            double a = (m->lo[k] - pad - oo[k]) / dd[k], b = (m->hi[k] + pad - oo[k]) / dd[k];
            if (a > b) { double c = a; a = b; b = c; }
            t0 = fmax(t0, a); t1 = fmin(t1, b);
        }
        if (t0 > t1 || t1 < 0 || t0 * s >= best) continue;
        for (uint32_t t = 0; t < m->n_tri; t++) {            /* Moller-Trumbore, either side */
            const float* p0 = m->xyz + 3 * (size_t)m->tri[3 * (size_t)t];
            const float* p1 = m->xyz + 3 * (size_t)m->tri[3 * (size_t)t + 1];
            const float* p2 = m->xyz + 3 * (size_t)m->tri[3 * (size_t)t + 2];
            double e1[3], e2[3], q[3], r[3], h[3];
            for (int k = 0; k < 3; k++) { e1[k] = (double)p1[k] - p0[k]; e2[k] = (double)p2[k] - p0[k]; r[k] = oo[k] - p0[k]; }
            h[0] = dd[1] * e2[2] - dd[2] * e2[1]; h[1] = dd[2] * e2[0] - dd[0] * e2[2]; h[2] = dd[0] * e2[1] - dd[1] * e2[0];
            double det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
            if (fabs(det) < 1e-30) continue;
            double u = (r[0] * h[0] + r[1] * h[1] + r[2] * h[2]) / det;
            if (u < 0 || u > 1) continue;
            q[0] = r[1] * e1[2] - r[2] * e1[1]; q[1] = r[2] * e1[0] - r[0] * e1[2]; q[2] = r[0] * e1[1] - r[1] * e1[0];
            double v = (dd[0] * q[0] + dd[1] * q[1] + dd[2] * q[2]) / det;
            if (v < 0 || u + v > 1) continue;
            double tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det * s;
            if (tt > 0 && tt < best) { best = (float)tt; if (layer) *layer = i; }
        }
    }
    return best;
}

int app_stl_count(void) { return nl; }

bool app_stl_get(int i, cv_stl_layer* out) {
    if (i < 0 || i >= nl) return false;
    *out = L[i].l;
    return true;
}

void app_stl_set(int i, const cv_stl_layer* in) {
    if (i < 0 || i >= nl) return;
    cv_stl_layer* l = &L[i].l;
    float old = l->scale;
    l->visible = in->visible;
    l->alpha = in->alpha >= 0 && in->alpha <= 1 ? in->alpha : 1.f;
    for (int k = 0; k < 3; k++) l->rgb[k] = in->rgb[k] >= 0 && in->rgb[k] <= 1 ? in->rgb[k] : 0.5f;
    l->scale = in->scale > 0 && isfinite(in->scale) ? in->scale : 1.f;
    if (l->scale != old) upload(i);
    changed();
}

const char* app_stl_name(int i) { return i >= 0 && i < nl ? L[i].name : ""; }
uint32_t app_stl_tris(int i) { return i >= 0 && i < nl ? L[i].mesh.n_tri : 0; }

void app_stl_remove(int i) {
    if (i < 0 || i >= nl) return;
    cv_stl_free(&L[i].mesh);
    free(L[i].edge);
    memmove(L + i, L + i + 1, (size_t)(nl - i - 1) * sizeof *L);
    nl--;
    memset(&L[nl], 0, sizeof L[nl]);
    for (int k = i; k < nl; k++) upload(k);      /* the slots follow the list */
    cv_render_mesh(nl, NULL, 0, NULL, 0, NULL, 0);
    changed();
}

void app_stl_clear(void) {
    while (nl > 0) app_stl_remove(nl - 1);
}

bool app_stl_bounds(v3* lo, v3* hi) {
    bool any = false;
    for (int i = 0; i < nl; i++) {
        if (!L[i].l.visible) continue;
        float s = L[i].l.scale;
        v3 a = v3_make(L[i].mesh.lo[0] * s, L[i].mesh.lo[1] * s, L[i].mesh.lo[2] * s);
        v3 b = v3_make(L[i].mesh.hi[0] * s, L[i].mesh.hi[1] * s, L[i].mesh.hi[2] * s);
        if (!any) { *lo = a; *hi = b; any = true; continue; }
        lo->x = fminf(lo->x, a.x); lo->y = fminf(lo->y, a.y); lo->z = fminf(lo->z, a.z);
        hi->x = fmaxf(hi->x, b.x); hi->y = fmaxf(hi->y, b.y); hi->z = fmaxf(hi->z, b.z);
    }
    return any;
}

void app_stl_draw(cv_draw* d) {
    for (int i = 0; i < CV_MESH_N; i++) {
        d->mesh[i].on = i < nl && L[i].l.visible;
        if (!d->mesh[i].on) continue;
        memcpy(d->mesh[i].rgb, L[i].l.rgb, sizeof d->mesh[i].rgb);
        d->mesh[i].alpha = L[i].l.alpha;
    }
    /* the see-through layers back to front, so a near one blends over a far one */
    float lo[3 * CV_MESH_N], hi[3 * CV_MESH_N], eye[3];
    v3 e, f, r, u;
    cam_basis(&G.cam, &e, &f, &r, &u);
    eye[0] = e.x; eye[1] = e.y; eye[2] = e.z;
    for (int i = 0; i < nl; i++)
        for (int k = 0; k < 3; k++) { lo[3 * i + k] = L[i].mesh.lo[k] * L[i].l.scale; hi[3 * i + k] = L[i].mesh.hi[k] * L[i].l.scale; }
    cv_stl_order_far(lo, hi, nl, eye, d->mesh_order);
    d->mesh_n_order = nl;
}
