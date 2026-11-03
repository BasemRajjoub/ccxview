/* app_nrec.c -- a .dat field as nodal values recovered from its integration points
   (nodalrec.h), optionally averaged at the nodes by Abaqus' threshold rule.

   nrec_compute (field, step or component changed): per element with records, the
   components extrapolated to its nodes, the scalar (Mises, a component ...) taken
   at each element-node: raw[slot], a slot being a position in conn[]. Synchronous:
   about 0.3 s for a million C3D8 on one core, per change of field, step or component.
   nrec_apply (vis, threshold changed): the averaging, out[slot]. A million elements
   take ~0.1 s, so the slider applies on release there (nrec_big). */
#include "app.h"
#include "app_int.h"
#include "nodalrec.h"
#include <math.h>

static struct {
    cv_nr_topo topo;
    const uint32_t* conn;              /* the mesh the topology was built for */
    uint32_t        n_elems;
    float *raw, *out;                  /* per slot */
    size_t n;
} R;

enum { BIG_SLOTS = 2000000 };

void nrec_clear(void) {
    cv_nr_topo_free(&R.topo);
    free(R.raw); free(R.out);
    memset(&R, 0, sizeof R);
}

bool app_nrec_active(void) { return G.nodal_recovery && G.field_src == 1 && G.has_field && R.out; }
bool nrec_big(void) { return R.n > BIG_SLOTS; }
void nrec_values(const float** v, size_t* n) { *v = R.out; *n = R.out ? R.n : 0; }

float nrec_value(uint32_t e, uint32_t node) {
    if (!R.out || e >= G.frd.n_elems) return NAN;
    for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) if (G.frd.conn[j] == node) return R.out[j];
    return NAN;
}

static struct { int t, nn, nip; bool rule; float M[CV_NR_NN * CV_NR_NIP]; } MC[24];
static int nmc;
static const float* matrix(int t, int nn, int nip) {
    for (int i = 0; i < nmc; i++) if (MC[i].t == t && MC[i].nn == nn && MC[i].nip == nip) return MC[i].M;
    int i = nmc < 24 ? nmc++ : 23;
    MC[i].t = t; MC[i].nn = nn; MC[i].nip = nip;
    MC[i].rule = cv_nr_matrix(t, nn, nip, MC[i].M);
    return MC[i].M;
}

bool nrec_compute(void) {
    const cv_dat_block* b = gp_block();
    const uint32_t E = G.frd.n_elems;
    free(R.raw); free(R.out);
    R.raw = R.out = NULL; R.n = 0;
    if (!b || !G.nodal_recovery || !E || b->ncomp > CV_MAX_COMP) return false;
    if (R.conn != G.frd.conn || R.n_elems != E) {
        cv_nr_topo_free(&R.topo);
        if (!cv_nr_topo_build(&R.topo, G.frd.n_nodes, E, G.frd.eoff, G.frd.conn)) return false;
        R.conn = G.frd.conn; R.n_elems = E;
    }
    const size_t ns = G.frd.eoff[E];
    R.raw = malloc(ns * sizeof(float));
    R.out = malloc(ns * sizeof(float));
    uint32_t* start = calloc((size_t)E + 2, sizeof(uint32_t));      /* the records of each element, counting-sorted */
    uint32_t* rec = malloc((size_t)CV_MAX(b->n, 1) * sizeof(uint32_t));
    if (!R.raw || !R.out || !start || !rec) { free(start); free(rec); free(R.raw); free(R.out); R.raw = R.out = NULL; return false; }
    for (size_t i = 0; i < ns; i++) R.raw[i] = NAN;
    R.n = ns;
    uint32_t* ei = malloc((size_t)CV_MAX(b->n, 1) * sizeof(uint32_t));
    if (!ei) { free(start); free(rec); nrec_clear(); return false; }
    for (uint32_t r = 0; r < b->n; r++) {
        ei[r] = cv_frd_elem_index(&G.frd, b->elem[r]);
        if (ei[r] != UINT32_MAX) start[ei[r] + 2]++;
    }
    for (uint32_t e = 0; e < E; e++) start[e + 2] += start[e + 1];
    for (uint32_t r = 0; r < b->n; r++) if (ei[r] != UINT32_MAX) rec[start[ei[r] + 1]++] = r;
    const int nc = b->ncomp;
    for (uint32_t e = 0; e < E; e++) {
        uint32_t a = start[e], z = start[e + 1];
        if (z == a) continue;
        const int t = G.frd.etype[e], nn = (int)(G.frd.eoff[e + 1] - G.frd.eoff[e]);
        int nip = 0;
        for (uint32_t k = a; k < z; k++) nip = CV_MAX(nip, b->ip[rec[k]]);
        if (nn > CV_NR_NN || nip > CV_NR_NIP || (uint32_t)nip != z - a) continue;      /* a gap among the points: no values */
        float v[CV_NR_NIP][CV_DAT_MAX_COMP];
        bool ok = true;
        for (uint32_t k = a; k < z && ok; k++) {
            int ip = b->ip[rec[k]] - 1;
            ok = ip >= 0;
            if (ok) { memcpy(v[ip], b->vals + (size_t)rec[k] * nc, (size_t)nc * sizeof(float)); units_apply(G.field_name, nc, v[ip], 1); }
        }
        if (!ok) continue;
        const float* M = matrix(t, nn, nip);
        float tmp[CV_NR_NN * CV_MAX_COMP], sc[CV_NR_NN];
        for (int i = 0; i < nn; i++)
            for (int c = 0; c < nc; c++) {
                float s = 0;
                for (int k = 0; k < nip; k++) s += M[i * nip + k] * v[k][c];
                tmp[i * nc + c] = s;
            }
        cv_field_scalar(tmp, nc, (uint32_t)nn, G.comp, sc);
        memcpy(R.raw + G.frd.eoff[e], sc, (size_t)nn * sizeof(float));
    }
    free(start); free(rec); free(ei);
    nrec_apply();
    return true;
}

void nrec_apply(void) {
    if (!R.raw) return;
    cv_nr_average(&R.topo, R.raw, G.vis, G.avg_across_materials ? NULL : G.frd.emat, G.avg_pct, R.out);
}
