/* app_elemnodal.c -- element-nodal (IRTYPE 3) results, FEMaster's per element values:
   the shown scalar per connectivity slot, a top and a bottom surface for shells
   (STRESS with STRPOS / STRNEG), optionally averaged by section (field.h), the
   comparison run's values subtracted, and what Inspect reads back. */
#include "app.h"
#include "app_int.h"
#include "eclass.h"

static size_t nslots_of(const cv_frd* f) { return f->n_elems ? f->eoff[f->n_elems] : 0; }
static bool is_shell(const cv_frd* f, uint32_t e) { return f->etype[e] >= 7 && f->etype[e] <= 10; }
static bool averaged(const cv_field_desc* d) { return cv_field_is_stress(d->name) && cv_tensor_order(d); }

/* the deck's section of every element, built when first asked and again after the
   deck or the model changed; NULL without a deck (or memory) */
static struct {
    int32_t*        v;
    unsigned        gen;          /* deck_gen() it was built from */
    const uint32_t* ids;          /* and the mesh */
    uint32_t        ne;
} S;

static const int32_t* elem_sections(void) {
    const cv_inp* deck = deck_get();
    if (!deck) return NULL;
    if (S.v && S.gen == deck_gen() && S.ids == G.frd.elem_id && S.ne == G.frd.n_elems) return S.v;
    free(S.v);
    S.v = malloc((size_t)CV_MAX(G.frd.n_elems, 1) * sizeof *S.v);
    if (!S.v) {
        cv_msg_add(&G.msgs, 0, false, "out of memory mapping stress averaging sections; values are not averaged");
        return NULL;
    }
    cv_elem_sections(&G.frd, deck, S.v);
    S.gen = deck_gen(); S.ids = G.frd.elem_id; S.ne = G.frd.n_elems;
    return S.v;
}

/* tensor values per slot of f into the chosen cylindrical system, in place;
   invariants (comp < 0) need no turning. false on OOM */
static bool turn_cyl(const cv_frd* f, const cv_field_desc* d, float* values, int comp) {
    if (G.csys <= 0 || comp < 0 || !cv_cyl_applies(d)) return true;
    size_t ns = nslots_of(f);
    float* xyz = malloc(CV_MAX(ns * 3, 1) * sizeof(float));
    if (!xyz) return false;
    for (size_t j = 0; j < ns; j++)
        memcpy(xyz + 3 * j, f->xyz + 3 * (size_t)f->conn[j], 3 * sizeof(float));
    cv_cyl_values(d, xyz, ns, G.csys - 1, G.csys_o, values);
    free(xyz);
    return true;
}

/* The comparison run's values of field d on this model's slots (ncomp each, turned
   like ours): matched by element id and node id, NaN where a pair has no match.
   NULL, with a message, when it has no such field or memory ran out. */
static float* cmp_elem_values(const cv_field_desc* d, int comp) {
    int step = G.cmp.n_steps ? CV_MIN(G.step, G.cmp.n_steps - 1) : -1;
    const cv_field_desc* other = NULL;
    for (int i = 0; step >= 0 && i < G.cmp.steps[step].nfields; i++)
        if (!strcmp(G.cmp.steps[step].fields[i].name, d->name)) other = &G.cmp.steps[step].fields[i];
    bool ok = other && other->irtype == 3 && other->ncomp == d->ncomp;
    for (int c = 0; ok && c < d->ncomp; c++) ok = !strcmp(d->comp[c], other->comp[c]);
    if (!ok) {
        cv_msg_add(&G.msgs, 0, false, "compare: matching element-nodal field is missing or incompatible");
        return NULL;
    }
    int nc = d->ncomp;
    size_t ons = nslots_of(&G.cmp), ns = nslots_of(&G.frd);
    float* raw = malloc(CV_MAX(ons * (size_t)nc, 1) * sizeof(float));
    float* out = malloc(CV_MAX(ns * (size_t)nc, 1) * sizeof(float));
    if (raw && out) {
        cv_frd_read_elem_field(&G.cmp, other, raw, &G.msgs);
        deck_compare_localize_elem(&G.cmp, step, other, raw);
        units_apply(other->name, nc, raw, ons);
    }
    if (!raw || !out || !turn_cyl(&G.cmp, other, raw, comp)) {
        cv_msg_add(&G.msgs, 0, false, "compare: out of memory decoding element-nodal field");
        free(raw); free(out);
        return NULL;
    }
    size_t unmatched = 0;
    for (uint32_t e = 0; e < G.frd.n_elems; e++) {
        uint32_t oe = cv_frd_elem_index(&G.cmp, G.frd.elem_id[e]);
        for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) {
            uint32_t match = UINT32_MAX;
            for (uint32_t k = oe != UINT32_MAX ? G.cmp.eoff[oe] : 0; oe != UINT32_MAX && k < G.cmp.eoff[oe + 1]; k++)
                if (G.cmp.node_id[G.cmp.conn[k]] == G.frd.node_id[G.frd.conn[j]]) { match = k; break; }
            if (match == UINT32_MAX) unmatched++;
            for (int c = 0; c < nc; c++) out[(size_t)j * nc + c] = match == UINT32_MAX ? NAN : raw[(size_t)match * nc + c];
        }
    }
    free(raw);
    if (unmatched) cv_msg_add(&G.msgs, 0, false, "compare: unmatched element-node pairs have no data");
    return out;
}

/* Field fi's scalar per slot (comp as cv_field_scalar), turned, less the comparison
   run's when on; NULL on OOM or when nothing is decoded. With sections, stress
   tensors are averaged component-wise first (tensor) or the scalar after (scalar). */
static float* elem_scalar(int fi, const int32_t* sections, int comp) {
    if (fi < 0 || fi >= G.frd.steps[G.step].nfields) return NULL;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
    const float* raw = cache_get_elem(G.step, fi);
    if (!raw) return NULL;
    int nc = d->ncomp;
    size_t ns = nslots_of(&G.frd), nv = ns * (size_t)nc;
    int avg = sections && averaged(d) ? G.elem_nodal_avg : CV_EN_AVG_NONE;
    bool diff = G.diff_mode && G.cmp_on;
    float* a = malloc(CV_MAX(nv, 1) * sizeof(float));
    float* out = malloc(CV_MAX(ns, 1) * sizeof(float));
    float* b = NULL;
    float* bs = NULL;
    if (!a || !out) goto oom;
    memcpy(a, raw, nv * sizeof(float));
    if (!turn_cyl(&G.frd, d, a, comp)) goto oom;
    if (diff) {
        b = cmp_elem_values(d, comp);
        if (!b) {                                  /* no difference to show */
            for (size_t j = 0; j < ns; j++) out[j] = NAN;
            free(a);
            return out;
        }
        if (!(bs = malloc(CV_MAX(ns, 1) * sizeof(float)))) goto oom;
        if (avg == CV_EN_AVG_TENSOR)               /* both average over the same element-nodes */
            for (size_t j = 0; j < ns; j++) {
                bool valid = true;
                for (int c = 0; valid && c < nc; c++) valid = isfinite(a[j * nc + c]) && isfinite(b[j * nc + c]);
                for (int c = 0; !valid && c < nc; c++) a[j * nc + c] = b[j * nc + c] = NAN;
            }
    }
    if (avg == CV_EN_AVG_TENSOR &&
        (!cv_elem_nodal_average(&G.frd, G.vis, sections, a, nc) ||
         (b && !cv_elem_nodal_average(&G.frd, G.vis, sections, b, nc)))) goto oom;
    cv_field_scalar(a, nc, ns, comp, out);
    if (b) {
        cv_field_scalar(b, nc, ns, comp, bs);
        for (size_t j = 0; j < ns; j++) out[j] -= bs[j];
    }
    if (avg == CV_EN_AVG_SCALAR && !cv_elem_nodal_average(&G.frd, G.vis, sections, out, 1)) goto oom;
    free(a); free(b); free(bs);
    return out;
oom:
    cv_msg_add(&G.msgs, 0, false, "out of memory preparing element-nodal values");
    free(a); free(b); free(bs); free(out);
    return NULL;
}

void clear_elem_surfaces(void) {
    free(G.elem_top); free(G.elem_bottom);
    G.elem_top = G.elem_bottom = NULL;
    G.elem_nslots = 0;
    G.field_elem_nodal = false;
}

/* the fields of the two shell surfaces of field fi: the STRESS group's STRPOS and
   STRNEG, or the pair itself; false when fi has no sides */
static bool surface_fields(int fi, int* base, int* top, int* bottom) {
    const cv_step* st = &G.frd.steps[G.step];
    int stress, t, b;
    *base = fi;
    if (cv_stress_group(st, &stress, &t, &b) && (fi == stress || fi == t || fi == b)) {
        *base = stress; *top = t; *bottom = b;
        return true;
    }
    if (cv_shell_pair(st, &t, &b) && (fi == t || fi == b)) { *top = t; *bottom = b; return true; }
    return false;
}

/* the deck's sections when stress d is to be averaged; NULL (with a message why) else */
static const int32_t* averaging_sections(const cv_field_desc* d) {
    if (G.elem_nodal_avg == CV_EN_AVG_NONE || !averaged(d)) return NULL;
    if (!deck_loaded()) {
        cv_msg_add(&G.msgs, 0, false, "Stress averaging skipped: the companion INP model is missing or could not be read.");
        cv_msg_add(&G.msgs, 0, false, "Place an INP with the same base name as the FRD, plus its include files, "
                   "in the FRD folder, then reload the FRD.");
        return NULL;
    }
    const int32_t* sections = elem_sections();
    for (uint32_t e = 0; sections && e < G.frd.n_elems; e++)
        if ((!G.vis || G.vis[e]) && sections[e] < 0 && G.frd.etype[e] >= 1 && G.frd.etype[e] <= 10) {
            cv_msg_add(&G.msgs, 0, false, "stress averaging: elements without an INP section keep their unaveraged values");
            break;
        }
    return sections;
}

bool refresh_elem_surfaces(int fi, const cv_field_desc* d) {
    clear_elem_surfaces();
    if (d->irtype != 3 || G.field_src != 0) return false;
    size_t ns = nslots_of(&G.frd);
    G.elem_top = malloc(CV_MAX(ns, 1) * sizeof(float));
    G.elem_bottom = malloc(CV_MAX(ns, 1) * sizeof(float));
    if (!G.elem_top || !G.elem_bottom) {
        clear_elem_surfaces();
        cv_msg_add(&G.msgs, 0, false, "out of memory preparing element-nodal shell display");
        return false;
    }
    G.field_elem_nodal = true;
    G.elem_nslots = ns;
    int base, top_fi = -1, bot_fi = -1;
    bool sided = surface_fields(fi, &base, &top_fi, &bot_fi);
    const int32_t* sections = averaging_sections(d);
    float* bv = elem_scalar(base, sections, G.comp);
    float* tv = sided ? elem_scalar(top_fi, sections, G.comp) : NULL;
    float* btv = sided ? elem_scalar(bot_fi, sections, G.comp) : NULL;
    if (sided && (top_fi < 0 || bot_fi < 0))
        cv_msg_add(&G.msgs, 0, false, "shell stress: missing or incompatible top or bottom field; that surface has no data");
    for (uint32_t e = 0; e < G.frd.n_elems; e++) {
        bool two = sided && is_shell(&G.frd, e);
        for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) {
            G.elem_top[j] = two ? (tv ? tv[j] : NAN) : (bv ? bv[j] : NAN);
            G.elem_bottom[j] = two ? (btv ? btv[j] : NAN) : (bv ? bv[j] : NAN);
        }
    }
    free(bv); free(tv); free(btv);

    for (uint32_t e = 0; e < G.frd.n_elems; e++) {         /* the element's value: its shown slots' mean */
        double sum = 0;
        uint32_t count = 0;
        for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++)
            for (int side = 0; side < (G.shell_side == 0 ? 2 : 1); side++) {
                float x = G.shell_side == 2 || side ? G.elem_bottom[j] : G.elem_top[j];
                if (isfinite(x)) { sum += x; count++; }
            }
        G.elem_val[e] = count ? (float)(sum / count) : NAN;
    }
    return true;
}

void refresh_elem_visibility(void) {
    if (!G.field_elem_nodal || G.elem_nodal_avg == CV_EN_AVG_NONE || G.field_src != 0) return;
    int fi = find_field(G.step, G.field_name);
    if (fi >= 0 && averaged(&G.frd.steps[G.step].fields[fi])) {
        refresh_elem_surfaces(fi, &G.frd.steps[G.step].fields[fi]);
        G.field_gen++;
        app_sel_field_changed();
    }
}

uint32_t elem_node_slot(uint32_t elem, uint32_t node) {
    if (elem >= G.frd.n_elems || node >= G.frd.n_nodes) return UINT32_MAX;
    for (uint32_t j = G.frd.eoff[elem]; j < G.frd.eoff[elem + 1]; j++)
        if (G.frd.conn[j] == node) return j;
    return UINT32_MAX;
}

/* the field shown on side (1 top, 2 bottom) of an element */
static int elem_surface_field(uint32_t elem, int side) {
    if (!G.field_elem_nodal || G.field_src != 0 || elem >= G.frd.n_elems) return -1;
    int fi = find_field(G.step, G.field_name), base, top, bottom;
    if (fi < 0 || !is_shell(&G.frd, elem) || !surface_fields(fi, &base, &top, &bottom)) return fi;
    return side == 2 ? bottom : top;
}

bool app_elem_has_sides(uint32_t elem) {
    return elem_surface_field(elem, 1) != elem_surface_field(elem, 2);
}

float app_elem_node_value(uint32_t elem, uint32_t node, int side) {
    uint32_t j = elem_node_slot(elem, node);
    const float* v = side == 2 ? G.elem_bottom : G.elem_top;
    return G.field_elem_nodal && v && j < G.elem_nslots ? v[j] : NAN;
}

float app_elem_mean_value(uint32_t elem, int side) {
    const float* v = side == 2 ? G.elem_bottom : G.elem_top;
    if (!G.field_elem_nodal || !v || elem >= G.frd.n_elems) return NAN;
    double sum = 0;
    uint32_t count = 0;
    for (uint32_t j = G.frd.eoff[elem]; j < G.frd.eoff[elem + 1] && j < G.elem_nslots; j++)
        if (isfinite(v[j])) { sum += v[j]; count++; }
    return count ? (float)(sum / count) : NAN;
}

int elem_field_comps(uint32_t elem, uint32_t node, int side, char names[][12], float* vals, int max) {
    uint32_t slot = elem_node_slot(elem, node);
    if (slot == UINT32_MAX) return 0;
    int fi = elem_surface_field(elem, side), fallback = find_field(G.step, G.field_name);
    if (fallback < 0) return 0;
    const cv_field_desc* d = &G.frd.steps[G.step].fields[fi >= 0 ? fi : fallback];
    static struct {
        bool valid;
        uint64_t gen;
        const float* xyz;
        uint32_t elem, node;
        int field, n;
        char names[CV_MAX_COMP][12];
        float vals[CV_MAX_COMP];
    } cache[2];
    int k = side == 2;
    if (!cache[k].valid || cache[k].gen != G.field_gen || cache[k].xyz != G.frd.xyz ||
        cache[k].elem != elem || cache[k].node != node || cache[k].field != fi) {
        cache[k].valid = false;
        const int32_t* sections = fi >= 0 && G.elem_nodal_avg != CV_EN_AVG_NONE && averaged(d) ? elem_sections() : NULL;
        cache[k].n = d->ncomp;
        for (int c = 0; c < d->ncomp; c++) {
            memcpy(cache[k].names[c], d->comp[c], 12);
            if (G.csys > 0 && cv_cyl_applies(d)) cv_cyl_comp_name(d, c, cache[k].names[c]);
            float* values = fi >= 0 ? elem_scalar(fi, sections, c) : NULL;
            if (fi >= 0 && !values) return 0;
            cache[k].vals[c] = values ? values[slot] : NAN;
            free(values);
        }
        cache[k].gen = G.field_gen; cache[k].xyz = G.frd.xyz;
        cache[k].elem = elem; cache[k].node = node; cache[k].field = fi;
        cache[k].valid = true;
    }
    int n = CV_MIN(cache[k].n, max);
    for (int c = 0; c < n; c++) { memcpy(names[c], cache[k].names[c], 12); vals[c] = cache[k].vals[c]; }
    return n;
}

void app_set_shell_side(int side) {
    G.shell_side = CV_MAX(0, CV_MIN(side, 2));
    if (G.loaded && G.field_elem_nodal) refresh_field();
}

void app_set_elem_nodal_avg(int mode) {
    G.elem_nodal_avg = CV_MAX(0, CV_MIN(mode, CV_EN_AVG_N - 1));
    if (G.loaded && G.field_elem_nodal) {
        refresh_field();
        int fi = find_field(G.step, G.field_name);
        if (G.elem_nodal_avg != CV_EN_AVG_NONE && G.field_src == 0 && !deck_loaded() && fi >= 0 &&
            averaged(&G.frd.steps[G.step].fields[fi]))
            G.show_msgs = true;
    }
}
