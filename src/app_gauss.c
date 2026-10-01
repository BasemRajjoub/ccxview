/* app_gauss.c -- integration-point fields from a .dat: per-point values and
   Gauss point positions, drawn as coloured points.

   The active .dat block is the one named like the selected field whose time
   matches the current .frd increment. Everything here is rebuilt from that
   block; geometry follows the current skin, visibility and displacement. */
#include "app.h"
#include "dat.h"
#include "gauss.h"
#include <math.h>

static struct {
    cv_dat    dat;
    bool      on;
    char      path[1024];
    int       block;          /* active block, -1 none */
    uint32_t* epos;           /* per element: first point in ipv, UINT32_MAX = no data */
    uint16_t* enip;           /* per element: points printed */
    float*    ipv;            /* scalar per point, NaN if missing */
    float*    gpos;           /* 3 per point: undeformed position (NaN if unknown) */
    float*    gdisp;          /* 3 per point */
    uint32_t  n;              /* points */
} P = { .block = -1 };

static void free_arrays(void) {
    free(P.epos); free(P.enip); free(P.ipv); free(P.gpos); free(P.gdisp);
    P.epos = NULL; P.enip = NULL; P.ipv = NULL; P.gpos = NULL; P.gdisp = NULL;
    P.n = 0;
    P.block = -1;
}

void gp_clear(void) {
    free_arrays();
    cv_dat_free(&P.dat);
    free(P.dat.msgs.a);
    memset(&P.dat, 0, sizeof P.dat);
    P.on = false;
    P.path[0] = 0;
    cv_render_aux(CV_AUX_GP, NULL, NULL, NULL, 0);
}

void gp_set(cv_dat* d, const char* path) {
    gp_clear();
    if (!d) return;
    P.dat = *d;
    memset(d, 0, sizeof *d);
    P.on = P.dat.n > 0;
    snprintf(P.path, sizeof P.path, "%s", path ? path : "");
    for (size_t i = 0; i < P.dat.msgs.n; i++) cv_msg_add(&G.msgs, P.dat.msgs.a[i].where, false, P.dat.msgs.a[i].text);
}

void gp_localize(void) { if (P.on) deck_localize_dat(&P.dat, &G.frd); }

bool gp_loaded(void) { return P.on; }
const char* gp_path(void) { return P.path; }

static bool time_match(float a, float b) { return fabsf(a - b) <= 1e-5f * CV_MAX(1.f, fabsf(b)); }

static float step_time(void) {
    return (G.frd.n_steps > 0 && G.step < G.frd.n_steps) ? G.frd.steps[G.step].time : 0.f;
}

/* distinct field names printed at the current increment */
int gp_fields(const char** names, int max) {
    int n = 0;
    float t = step_time();
    for (int i = 0; i < P.dat.n && n < max; i++) {
        const cv_dat_block* b = &P.dat.b[i];
        if (b->is_coord || !time_match(b->time, t)) continue;
        bool dup = false;
        for (int k = 0; k < n; k++) dup |= strcmp(names[k], b->name) == 0;
        if (!dup) names[n++] = b->name;
    }
    return n;
}

/* A field descriptor so the usual component / magnitude / von Mises options apply. */
bool gp_desc(const char* name, cv_field_desc* d) {
    for (int i = 0; i < P.dat.n; i++) {
        const cv_dat_block* b = &P.dat.b[i];
        if (b->is_coord || strcmp(b->name, name) != 0) continue;
        memset(d, 0, sizeof *d);
        snprintf(d->name, sizeof d->name, "%s", b->name);
        d->ncomp = CV_MIN(b->ncomp, CV_MAX_COMP);
        for (int c = 0; c < d->ncomp; c++) snprintf(d->comp[c], sizeof d->comp[c], "%s", b->comp[c]);
        return true;
    }
    return false;
}

static int find_block(const char* name, bool coord) {
    float t = step_time();
    for (int i = 0; i < P.dat.n; i++) {
        const cv_dat_block* b = &P.dat.b[i];
        if (b->is_coord != coord || !time_match(b->time, t)) continue;
        if (coord || strcmp(b->name, name) == 0) return i;
    }
    return -1;
}

static void elem_point(uint32_t e, const double xi[3], float pos[3], float disp[3]) {
    const int t = G.frd.etype[e];
    const uint32_t b = G.frd.eoff[e], nn = G.frd.eoff[e + 1] - b;
    double N[20];
    pos[0] = pos[1] = pos[2] = NAN;
    disp[0] = disp[1] = disp[2] = 0;
    if (nn > 20 || !cv_shape(t, (int)nn, xi, N)) return;
    double x[3] = { 0, 0, 0 }, d[3] = { 0, 0, 0 };
    for (uint32_t i = 0; i < nn; i++) {
        uint32_t nd = G.frd.conn[b + (uint32_t)cv_frd_node_pos(t, (int)nn, (int)i)];
        for (int k = 0; k < 3; k++) {
            x[k] += N[i] * G.frd.xyz[3 * nd + k];
            if (G.disp) d[k] += N[i] * G.disp[3 * nd + k];
        }
    }
    for (int k = 0; k < 3; k++) { pos[k] = (float)x[k]; disp[k] = (float)d[k]; }
}

/* ---- geometry ------------------------------------------------------------------- */

void gp_refresh_geometry(void) {
    if (!P.on || P.block < 0 || !P.epos) {
        cv_render_aux(CV_AUX_GP, NULL, NULL, NULL, 0);
        return;
    }
    /* per-point displacement follows the current increment */
    for (uint32_t e = 0; e < G.frd.n_elems; e++) {
        if (P.epos[e] == UINT32_MAX) continue;
        for (int k = 0; k < P.enip[e]; k++) {
            uint32_t i = P.epos[e] + (uint32_t)k;
            double xi[3];
            float x[3], d[3];
            if (!cv_ip_param(G.frd.etype[e], P.enip[e], k, xi)) continue;
            elem_point(e, xi, x, d);
            memcpy(P.gdisp + 3 * i, d, sizeof d);
        }
    }
    /* Gauss points of visible elements */
    CV_VEC(float) gp = {0}, gd = {0}, gv = {0};
    for (uint32_t e = 0; e < G.frd.n_elems; e++) {
        if (P.epos[e] == UINT32_MAX || (G.vis && !G.vis[e])) continue;
        for (int k = 0; k < P.enip[e]; k++) {
            uint32_t i = P.epos[e] + (uint32_t)k;
            if (P.gpos[3 * i] != P.gpos[3 * i]) continue;       /* no position */
            for (int j = 0; j < 3; j++) { cv_push(gp, P.gpos[3 * i + j]); cv_push(gd, P.gdisp[3 * i + j]); }
            cv_push(gv, P.ipv[i]);
        }
    }
    cv_render_aux(CV_AUX_GP, gp.a, gd.a, gv.a, (uint32_t)gv.n);
    cv_free_vec(gp); cv_free_vec(gd); cv_free_vec(gv);

}

/* ---- nodal fields -----------------------------------------------------------------
   Without a .dat the layer still works: every visible solid element gets its usual
   integration points, and the value there is the element's nodal field interpolated
   with its shape functions. */

static int default_nip(int t) {
    switch (t) {
        case 1: case 4: return 8;      /* C3D8 / C3D20R */
        case 3: return 1;              /* C3D4 */
        case 6: return 4;              /* C3D10 */
        case 2: return 2;              /* C3D6 */
        case 5: return 9;              /* C3D15 */
        case 7: return 1;              /* 3-node triangle */
        case 8: return 3;              /* 6-node triangle */
        case 9: case 10: return 4;     /* 4/8-node quad, 2x2 */
        default: return 0;
    }
}

void gp_build_nodal(void) {
    if (!G.loaded || !G.show_gp) { cv_render_aux(CV_AUX_GP, NULL, NULL, NULL, 0); return; }
    const uint32_t E = G.frd.n_elems;
    size_t total = 0;
    for (uint32_t e = 0; e < E; e++) if (!G.vis || G.vis[e]) total += (size_t)default_nip(G.frd.etype[e]);
    bool one = total > 8000000;          /* huge models: one point per element keeps it drawable */
    CV_VEC(float) gp = {0}, gd = {0}, gv = {0};
    const bool val = G.has_field && G.scalar;
    for (uint32_t e = 0; e < E; e++) {
        if (G.vis && !G.vis[e]) continue;
        const int t = G.frd.etype[e];
        int nip = one ? 1 : default_nip(t);
        if (!nip) continue;
        const uint32_t b = G.frd.eoff[e], nn = G.frd.eoff[e + 1] - b;
        for (int k = 0; k < nip; k++) {
            double xi[3], N[20];
            if (!cv_ip_param(t, nip, k, xi) || nn > 20 || !cv_shape(t, (int)nn, xi, N)) continue;
            double x[3] = { 0, 0, 0 }, d[3] = { 0, 0, 0 }, v = 0;
            for (uint32_t i = 0; i < nn; i++) {
                uint32_t nd = G.frd.conn[b + (uint32_t)cv_frd_node_pos(t, (int)nn, (int)i)];
                for (int j = 0; j < 3; j++) {
                    x[j] += N[i] * G.frd.xyz[3 * nd + j];
                    if (G.disp) d[j] += N[i] * G.disp[3 * nd + j];
                }
                if (val) v += N[i] * G.scalar[nd];
            }
            if (!cv_reserve(gp, gp.n + 3) || !cv_reserve(gd, gd.n + 3) || !cv_push(gv, val ? (float)v : NAN)) goto done;
            for (int j = 0; j < 3; j++) { gp.a[gp.n++] = (float)x[j]; gd.a[gd.n++] = (float)d[j]; }
        }
    }
done:
    cv_render_aux(CV_AUX_GP, gp.a, gd.a, gv.a, (uint32_t)gv.n);
    cv_free_vec(gp); cv_free_vec(gd); cv_free_vec(gv);
}

/* ---- values -------------------------------------------------------------------- */

bool gp_refresh(void) {
    free_arrays();
    if (!P.on) { gp_refresh_geometry(); return false; }
    int bi = find_block(G.field_name, false);
    if (bi < 0 || !G.elem_val) { gp_refresh_geometry(); return false; }
    const cv_dat_block* b = &P.dat.b[bi];
    const uint32_t E = G.frd.n_elems;
    P.epos = malloc((size_t)CV_MAX(E, 1) * sizeof *P.epos);
    P.enip = calloc(CV_MAX(E, 1), sizeof *P.enip);
    float* rec = malloc((size_t)CV_MAX(b->n, 1) * sizeof(float));
    if (!P.epos || !P.enip || !rec) { free(rec); free_arrays(); return false; }

    cv_field_scalar(b->vals, b->ncomp, b->n, G.comp, rec);
    size_t unknown = 0;
    for (uint32_t r = 0; r < b->n; r++) {
        uint32_t e = cv_frd_elem_index(&G.frd, b->elem[r]);
        if (e == UINT32_MAX) { unknown++; continue; }
        if (b->ip[r] > P.enip[e]) P.enip[e] = b->ip[r];
    }
    uint32_t n = 0;
    for (uint32_t e = 0; e < E; e++) {
        P.epos[e] = P.enip[e] ? n : UINT32_MAX;
        n += P.enip[e];
    }
    P.n = n;
    P.ipv = malloc((size_t)CV_MAX(n, 1) * sizeof(float));
    P.gpos = malloc((size_t)CV_MAX(n, 1) * 3 * sizeof(float));
    P.gdisp = calloc((size_t)CV_MAX(n, 1) * 3, sizeof(float));
    if (!P.ipv || !P.gpos || !P.gdisp) { free(rec); free_arrays(); return false; }
    for (uint32_t i = 0; i < n; i++) P.ipv[i] = NAN;
    for (uint32_t i = 0; i < 3 * n; i++) P.gpos[i] = NAN;
    for (uint32_t r = 0; r < b->n; r++) {
        uint32_t e = cv_frd_elem_index(&G.frd, b->elem[r]);
        if (e != UINT32_MAX) P.ipv[P.epos[e] + b->ip[r] - 1] = rec[r];
    }
    free(rec);
    if (unknown) {
        char msg[120];
        snprintf(msg, sizeof msg, "%s: %zu records for elements not in the .frd (ignored)", b->name, unknown);
        cv_msg_add(&G.msgs, 0, false, msg);
    }

    /* element means (the probe's fallback) */
    for (uint32_t e = 0; e < E; e++) {
        double s = 0; int k = 0;
        if (P.epos[e] != UINT32_MAX)
            for (int j = 0; j < P.enip[e]; j++) {
                float v = P.ipv[P.epos[e] + (uint32_t)j];
                if (v == v) { s += v; k++; }
            }
        G.elem_val[e] = k ? (float)(s / k) : NAN;
    }

    /* positions: printed COORD wins, else computed from the element shape */
    int cb_i = find_block(NULL, true);
    for (uint32_t e = 0; e < E; e++) {
        if (P.epos[e] == UINT32_MAX) continue;
        for (int k = 0; k < P.enip[e]; k++) {
            double xi[3];
            float x[3], d[3];
            if (!cv_ip_param(G.frd.etype[e], P.enip[e], k, xi)) continue;
            elem_point(e, xi, x, d);
            memcpy(P.gpos + 3 * (P.epos[e] + (uint32_t)k), x, sizeof x);
        }
    }
    if (cb_i >= 0) {
        const cv_dat_block* c = &P.dat.b[cb_i];
        for (uint32_t r = 0; r < c->n && c->ncomp >= 3; r++) {
            uint32_t e = cv_frd_elem_index(&G.frd, c->elem[r]);
            if (e == UINT32_MAX || P.epos[e] == UINT32_MAX || c->ip[r] > P.enip[e]) continue;
            memcpy(P.gpos + 3 * (P.epos[e] + c->ip[r] - 1), c->vals + (size_t)r * c->ncomp, 3 * sizeof(float));
        }
    }
    P.block = bi;
    gp_refresh_geometry();
    return true;
}

void gp_values(const float** v, size_t* n) {
    *v = P.ipv;
    *n = P.ipv ? P.n : 0;
}

/* nearest integration point of element e to a (deformed-space) hit point */
bool gp_probe(uint32_t e, const float hit[3], float scale, int* ip, float* value) {
    if (!P.epos || e >= G.frd.n_elems || P.epos[e] == UINT32_MAX) return false;
    float best = INFINITY;
    for (int k = 0; k < P.enip[e]; k++) {
        uint32_t i = P.epos[e] + (uint32_t)k;
        float d2 = 0;
        for (int j = 0; j < 3; j++) {
            float x = P.gpos[3 * i + j] + P.gdisp[3 * i + j] * scale - hit[j];
            d2 += x * x;
        }
        if (d2 < best) { best = d2; *ip = k + 1; *value = P.ipv[i]; }
    }
    return best < INFINITY;
}

/* sibling .dat of a .frd path, if it exists: "model.frd" -> "model.dat" */
bool gp_sibling(const char* frd_path, char* out, size_t n) {
    return deck_sibling(frd_path, ".dat", out, n);
}
