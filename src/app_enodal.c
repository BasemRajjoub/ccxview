/* app_enodal.c -- element-nodal results (IRTYPE 3: STRESS / STRPOS / STRNEG of FEMaster) on
   screen. The headless rules are elemnodal.h; this decodes the entry shown, keeps its values
   per connectivity slot (one for every element at every node: the jumps between elements
   stay), averages them when asked, and gives the renderer its triangles: the solids' in
   CV_AUX_ELEMTRI, a shell's in two sets (the face its normal leaves and the reverse), each
   drawn with the faces on that side only, a hair in front of any solid face under it. */
#include "app_int.h"
#include "elemnodal.h"
#include "eclass.h"
#include <math.h>

static struct {
    bool  on;                         /* the field on screen is element-nodal and decoded */
    int   step, head;                 /* the raw values below are of this step and entry head, -1 none */
    int   nc;
    cv_en_group grp;
    float* raw[3];                    /* STRESS (or the one field), STRPOS, STRNEG: slots * nc each, units applied */
    cv_nr_topo topo;
    const uint32_t* topo_conn;        /* the mesh it was built for */
    uint32_t topo_ne;
    float* nrm;                       /* per element, shells' normals */
    const uint32_t* nrm_conn;
    uint32_t* group;                  /* per element: section and material, for the averaging */
    unsigned group_gen;
    const uint32_t* group_ids;
    float* all;                       /* 2 * slots: the front scalars, then the back */
    size_t ns;
    uint64_t key[6];                  /* what the scalars were computed for */
    bool  keyed;
    bool  warned;                     /* said that no deck means no averaging */
} E = { .step = -1, .head = -1 };

void app_en_raw_clear(void) {
    for (int i = 0; i < 3; i++) { free(E.raw[i]); E.raw[i] = NULL; }
    E.step = E.head = -1;
    E.keyed = false;
}

void app_en_clear(void) {
    app_en_raw_clear();
    cv_nr_topo_free(&E.topo);
    free(E.nrm); free(E.group); free(E.all);
    E.nrm = NULL; E.group = NULL; E.all = NULL; E.topo_conn = NULL; E.nrm_conn = NULL; E.group_ids = NULL;
    E.ns = 0; E.on = false; E.warned = false;
}

void app_en_stop(void) { E.on = false; }          /* the field on screen is another: refresh_field decides again */

bool app_en_active(void) { return G.field_src == 0 && G.has_field && E.on && E.all; }

/* ---- what the lists offer ---------------------------------------------------------------- */

bool app_en_listed(const cv_step* st, int fi) {
    if (!st->fields[fi].elnod) return true;
    if (!G.en_on) return false;
    bool folded;
    cv_en_entry(st, fi, NULL, &folded);
    return !folded;
}

const char* app_field_title(const cv_step* st, int fi) {
    return st->fields[fi].elnod && G.en_on ? cv_en_title(st, fi) : cv_field_title(st->fields[fi].name);
}

/* a field of the step that is folded into another's entry: the entry's head, else fi */
int app_en_head(const cv_step* st, int fi) {
    if (fi < 0 || !st->fields[fi].elnod || !G.en_on) return fi;
    cv_en_group g;
    bool folded;
    cv_en_entry(st, fi, &g, &folded);
    if (!folded) return fi;
    return g.stress >= 0 ? g.stress : g.pos;
}

/* the step has a shell's surface stresses to choose between (tb:sides) */
bool app_en_has_shells(void) {
    if (!G.loaded || !G.en_on || G.frd.n_steps == 0) return false;
    const cv_step* st = &G.frd.steps[G.step];
    for (int i = 0; i < st->nfields; i++)
        if (st->fields[i].elnod && (!strcmp(st->fields[i].name, "STRPOS") || !strcmp(st->fields[i].name, "STRNEG"))) return true;
    return false;
}

/* ---- decoding ---------------------------------------------------------------------------- */

static bool topo_ready(void) {
    if (E.topo.start && E.topo_conn == G.frd.conn && E.topo_ne == G.frd.n_elems) return true;
    cv_nr_topo_free(&E.topo);
    free(E.nrm); E.nrm = NULL;
    if (!cv_nr_topo_build(&E.topo, G.frd.n_nodes, G.frd.n_elems, G.frd.eoff, G.frd.conn)) return false;
    E.topo_conn = G.frd.conn; E.topo_ne = G.frd.n_elems;
    E.nrm = cv_en_normals(&G.frd);
    E.nrm_conn = G.frd.conn;
    return E.nrm != NULL;
}

/* the group of every element for the averaging: its section (the deck's) and material */
static const uint32_t* groups(void) {
    const cv_inp* d = deck_get();
    if (!d) return NULL;
    unsigned gen = deck_gen();
    if (E.group && E.group_gen == gen && E.group_ids == G.frd.elem_id) return E.group;
    free(E.group);
    E.group = malloc((size_t)CV_MAX(G.frd.n_elems, 1) * sizeof *E.group);
    if (!E.group) return NULL;
    cv_eclass c;
    bool ok = cv_eclass_sections(&c, &G.frd, d);
    for (uint32_t e = 0; e < G.frd.n_elems; e++)
        E.group[e] = ((ok ? (uint32_t)c.of_elem[e] + 1 : 0) << 16) ^ (G.frd.emat ? G.frd.emat[e] & 0xFFFF : 0);
    if (ok) cv_eclass_free(&c);
    E.group_gen = gen; E.group_ids = G.frd.elem_id;
    return E.group;
}

static bool decode(int fi, int idx) {
    const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
    size_t ns = cv_frd_slots(&G.frd);
    float* v = malloc(CV_MAX(ns, 1) * (size_t)d->ncomp * sizeof *v);
    if (!v) { cv_msg_add(&G.msgs, 0, false, "out of memory decoding a field"); return false; }
    cv_frd_read_elnod(&G.frd, d, v, &G.msgs);
    units_apply(d->name, d->ncomp, v, ns);
    E.raw[idx] = v;
    return true;
}

/* Decode the entry whose head is field fi of this step (once per step and entry), then the
   scalars. false: nothing to show. */
bool app_en_compute(int step, int fi) {
    E.on = false;
    if (!G.en_on || step < 0 || step >= G.frd.n_steps || fi < 0 || !G.frd.steps[step].fields[fi].elnod) return false;
    const cv_step* st = &G.frd.steps[step];
    if (E.step != step || E.head != fi || !E.raw[0]) {
        app_en_raw_clear();
        E.grp = (cv_en_group){ CV_EN_ONE, fi, -1, -1 };
        cv_en_group g;
        int k = cv_en_entry(st, fi, &g, NULL);
        if (k != CV_EN_ONE) E.grp = g;
        E.nc = st->fields[fi].ncomp;
        bool ok = true;
        if (E.grp.kind == CV_EN_SHELL) ok = decode(E.grp.pos, 1) && decode(E.grp.neg, 2);     /* the head is STRPOS */
        else {
            ok = decode(fi, 0);
            if (ok && E.grp.kind == CV_EN_STRESS) {
                if (E.grp.pos >= 0 && st->fields[E.grp.pos].ncomp == E.nc) ok = decode(E.grp.pos, 1);
                if (ok && E.grp.neg >= 0 && st->fields[E.grp.neg].ncomp == E.nc) ok = decode(E.grp.neg, 2);
            }
        }
        if (!ok) { app_en_raw_clear(); return false; }
        E.step = step; E.head = fi;
    }
    if (!topo_ready()) return false;
    E.ns = cv_frd_slots(&G.frd);
    float* a = realloc(E.all, CV_MAX(2 * E.ns, 1) * sizeof *a);
    if (!a) return false;
    E.all = a;
    E.keyed = false;
    E.on = true;
    app_en_apply();
    return true;
}

static uint64_t vis_hash(void) {
    uint64_t h = 1469598103934665603ull;
    if (!G.vis) return h;
    for (uint32_t e = 0; e < G.frd.n_elems; e++) h = (h ^ G.vis[e]) * 1099511628211ull;
    return h;
}

/* The scalars of the shown component, side and averaging for the elements shown. Cheap to ask
   again: nothing is done when none of that has changed. */
void app_en_apply(void) {
    if (!E.on || !E.all) return;
    int avg = G.en_avg;
    const uint32_t* grp = NULL;
    if (avg != CV_EN_AVG_NONE) {
        grp = groups();
        if (!deck_get()) {
            avg = CV_EN_AVG_NONE;
            if (!E.warned) {
                E.warned = true;
                cv_msg_add(&G.msgs, 0, false, "element-nodal results: not averaged, there is no .inp beside the .frd to say which elements share a section and material");
            }
        }
    }
    uint64_t key[6] = { (uint64_t)G.step, (uint64_t)(G.comp + 100), (uint64_t)G.en_side, (uint64_t)avg, vis_hash(), (uint64_t)(uintptr_t)E.raw[0] ^ (uint64_t)(uintptr_t)E.raw[1] };
    if (E.keyed && !memcmp(key, E.key, sizeof key)) return;
    cv_en_src src = { E.grp.kind, E.raw[0], E.raw[1], E.raw[2] };
    if (!cv_en_scalars(&G.frd, &E.topo, &src, E.nc, G.comp, G.en_side, avg, G.vis, grp, E.nrm, E.all, E.all + E.ns)) {
        cv_msg_add(&G.msgs, 0, false, "out of memory: no element-nodal values");
        E.on = false;
        return;
    }
    memcpy(E.key, key, sizeof key);
    E.keyed = true;
    if (G.elem_val)                                /* an element's mean (of its own values) for what asks per element */
        for (uint32_t e = 0; e < G.frd.n_elems; e++) {
            double s = 0; int n = 0;
            for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) if (E.all[j] == E.all[j] && !isinf(E.all[j])) { s += E.all[j]; n++; }
            G.elem_val[e] = n ? (float)(s / n) : NAN;
        }
}

/* ---- what the rest of the program asks ----------------------------------------------------- */

void app_en_values(const float** v, size_t* n) { *v = E.all; *n = E.all ? 2 * E.ns : 0; }
uint32_t app_en_elem_of(size_t i) { return E.ns && i < 2 * E.ns ? E.topo.elem[i % E.ns] : UINT32_MAX; }

/* element e's own value at node `node`: face 0 the one on the shell's positive-normal side (a solid's, the same) */
float app_en_value(uint32_t e, uint32_t node, int face) {
    if (!app_en_active() || e >= G.frd.n_elems) return NAN;
    for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++)
        if (G.frd.conn[j] == node) return E.all[(face ? E.ns : 0) + j];
    return NAN;
}

/* the element's own values at the node, as the file has them (not averaged): 1 a single value in *top (a solid, or
   a field of one surface); 2 a shell with *top (STRPOS) and *bottom (STRNEG); 0 none to give */
int app_en_own(uint32_t e, uint32_t node, float* top, float* bottom) {
    if (!app_en_active() || e >= G.frd.n_elems) return 0;
    uint32_t j = UINT32_MAX;
    for (uint32_t k = G.frd.eoff[e]; k < G.frd.eoff[e + 1] && j == UINT32_MAX; k++) if (G.frd.conn[k] == node) j = k;
    if (j == UINT32_MAX) return 0;
    bool sh = cv_en_is_shell(G.frd.etype[e]);
    float v[2] = { NAN, NAN };
    int n = 1;
    for (int i = 0; i < 2; i++) {
        int idx = sh && E.grp.kind != CV_EN_ONE ? 1 + i : 0;
        if (i && !(sh && E.grp.kind != CV_EN_ONE)) break;
        if (E.raw[idx]) cv_field_scalar(E.raw[idx] + (size_t)j * E.nc, E.nc, 1, G.comp, &v[i]);
        n = i + 1;
    }
    *top = v[0];
    if (bottom) *bottom = v[1];
    return n;
}

/* the one number for the probe: the element's own value, STRPOS's unless only the bottom is shown */
float app_en_probe(uint32_t e, uint32_t node) {
    float a, b;
    int n = app_en_own(e, node, &a, &b);
    return n == 2 && G.en_side == CV_EN_BOTTOM ? b : n ? a : NAN;
}

bool app_en_shell(uint32_t e) { return e < G.frd.n_elems && cv_en_is_shell(G.frd.etype[e]); }

/* ---- triangles for the renderer ------------------------------------------------------------- */

static uint32_t slot_of(uint32_t e, uint32_t node) {
    for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) if (G.frd.conn[j] == node) return j;
    return UINT32_MAX;
}

void app_en_streams(void) {
    cv_render_aux(CV_AUX_ELEMTRI, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_SHELLP, NULL, NULL, NULL, 0);
    cv_render_aux(CV_AUX_SHELLN, NULL, NULL, NULL, 0);
    if (!app_en_active() || !G.skin.n_tri) return;
    size_t nt = G.skin.n_tri, nsh = 0;
    for (size_t t = 0; t < nt; t++) nsh += cv_en_is_shell(G.frd.etype[G.skin.tri_elem[t]]);
    size_t nso = nt - nsh;
    cv_fvec pos[3] = {{0}}, disp[3] = {{0}}, val[3] = {{0}};      /* solids, shell front, shell back */
    size_t want[3] = { nso, nsh, nsh };
    bool ok = true;
    for (int s = 0; s < 3 && ok; s++)
        ok = cv_reserve(pos[s], want[s] * 9) && cv_reserve(val[s], want[s] * 3) && (!G.disp || cv_reserve(disp[s], want[s] * 9));
    for (size_t t = 0; ok && t < nt; t++) {
        uint32_t e = G.skin.tri_elem[t];
        CV_ASSERT(e < G.frd.n_elems);
        bool sh = cv_en_is_shell(G.frd.etype[e]);
        for (int side = 0; side < (sh ? 2 : 1); side++) {
            int s = sh ? 1 + side : 0;
            for (int k = 0; k < 3; k++) {
                uint32_t n = G.skin.tri[3 * t + k], j = slot_of(e, n);
                CV_ASSERT(n < G.frd.n_nodes);
                for (int q = 0; q < 3; q++) pos[s].a[pos[s].n++] = G.frd.xyz[3 * n + q];
                if (G.disp) for (int q = 0; q < 3; q++) disp[s].a[disp[s].n++] = G.disp[3 * n + q];
                val[s].a[val[s].n++] = j == UINT32_MAX ? NAN : E.all[(side ? E.ns : 0) + j];
            }
        }
    }
    if (ok) {
        cv_render_aux(CV_AUX_ELEMTRI, pos[0].n ? pos[0].a : NULL, G.disp ? disp[0].a : NULL, val[0].a, (uint32_t)val[0].n);
        cv_render_aux(CV_AUX_SHELLP, pos[1].n ? pos[1].a : NULL, G.disp ? disp[1].a : NULL, val[1].a, (uint32_t)val[1].n);
        cv_render_aux(CV_AUX_SHELLN, pos[2].n ? pos[2].a : NULL, G.disp ? disp[2].a : NULL, val[2].a, (uint32_t)val[2].n);
    }
    for (int s = 0; s < 3; s++) { cv_free_vec(pos[s]); cv_free_vec(disp[s]); cv_free_vec(val[s]); }
}
