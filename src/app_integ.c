/* app_integ.c -- integrals of the field shown over a region, at every step: the
   volume integral and average over elements (the homogenised value of an RVE: <S>,
   <E> per component), the surface integral over element faces with the force a
   tensor carries through them (int S n dA) or a pressure puts on them, and sums
   over nodes (reaction forces and their moment). The maths is integ.c; here the
   region is found, each step's field decoded (as the history does) and the rows
   kept for the Integrals window and its CSV. */
#include "app_int.h"
#include "integ.h"
#include "calc.h"
#include "export.h"     /* cv_fprintf */
#include <math.h>
#include <ctype.h>

app_integ_t GI;

/* the region, taken when the window opens */
static struct {
    uint32_t* el;  uint8_t* fc;  uint32_t n;     /* elements, and their faces for a surface */
    uint32_t* nodes;  uint32_t nn;
    char key[700];                               /* what the rows were made for */
} R;

const char* app_integ_kind(int k) {
    static const char* n[CV_IK_N] = { "volume", "surface", "nodes" };
    return k >= 0 && k < CV_IK_N ? n[k] : "?";
}

static void rows_free(void) {
    free(GI.step); free(GI.t); free(GI.size); free(GI.val);
    GI.step = NULL; GI.t = NULL; GI.size = NULL; GI.val = NULL;
    GI.n = GI.nq = 0;
}

static void region_free(void) {
    free(R.el); free(R.fc); free(R.nodes);
    R.el = NULL; R.fc = NULL; R.nodes = NULL; R.n = R.nn = 0;
}

void app_integ_close(void) {
    rows_free();
    region_free();
    GI.open = false;
    R.key[0] = 0;
}

/* ---- the region ------------------------------------------------------------------ */

static int deck_set_ix(const char* name, int want_elem) {   /* want_elem -1: either kind */
    const cv_inp* d = deck_get();
    for (int i = 0; d && i < d->nsets; i++)
        if (!strcasecmp(d->sets[i].name, name) && (want_elem < 0 || d->sets[i].is_elem == want_elem)) return i;
    return -1;
}
static int deck_surf_ix(const char* name) {
    const cv_inp* d = deck_get();
    for (int i = 0; d && i < d->nsurfs; i++) if (!strcasecmp(d->surfs[i].name, name)) return i;
    return -1;
}

/* the elements of a deck element set (shown indices) into R.el */
static bool set_elems(int si) {
    const cv_set* s = &deck_get()->sets[si];
    R.el = malloc((size_t)CV_MAX(s->n, 1) * sizeof *R.el);
    if (!R.el) return false;
    for (uint32_t j = 0; j < s->n; j++) {
        uint32_t e = deck_elem(&G.frd, s->ids[j]);
        if (e != UINT32_MAX) R.el[R.n++] = e;
    }
    return R.n > 0;
}

/* the outer faces of the elements marked in vis (NULL: every one), as the skin finds them */
static bool outer_faces(const uint8_t* vis, const cv_skin* have) {
    cv_skin sk = { 0 };
    const cv_skin* s = have;
    if (!s) { if (!cv_skin_build_opt(&sk, &G.frd, vis, CV_CREASE_DEG, false)) return false; s = &sk; }
    R.el = malloc(CV_MAX(s->n_face, 1) * sizeof *R.el);
    R.fc = malloc(CV_MAX(s->n_face, 1));
    for (size_t i = 0; R.el && R.fc && i < s->n_face; i++) {
        uint32_t e = s->face[i] >> 3;
        if (vis && !vis[e]) continue;
        R.el[R.n] = e; R.fc[R.n++] = (uint8_t)(s->face[i] & 7);
    }
    if (s == &sk) cv_skin_free(&sk);
    return R.n > 0;
}

/* the nodes marked into R.nodes */
static bool marked_nodes(const uint8_t* m) {
    R.nodes = malloc((size_t)CV_MAX(G.frd.n_nodes, 1) * sizeof *R.nodes);
    for (uint32_t i = 0; R.nodes && i < G.frd.n_nodes; i++) if (m[i]) R.nodes[R.nn++] = i;
    return R.nn > 0;
}
static void mark_elem_nodes(uint8_t* m, uint32_t e) {
    for (uint32_t j = G.frd.eoff[e]; j < G.frd.eoff[e + 1]; j++) m[G.frd.conn[j]] = 1;
}

/* the elements shown: groups, sets, crop and those hidden by hand */
static uint8_t* shown_mask(void) {
    uint8_t* v = malloc(CV_MAX(G.frd.n_elems, 1));
    for (uint32_t e = 0; v && e < G.frd.n_elems; e++) v[e] = (!G.vis || G.vis[e]) && !(G.hide && G.hide[e]);
    return v;
}

/* finds the region; false with the reason in err */
static bool region(int kind, const char* target, char* err, size_t errn) {
    region_free();
    const cv_inp* d = deck_get();
    bool sel = !strcasecmp(target, "selection"), shown = !strcasecmp(target, "shown");
    int es = sel || shown ? -1 : deck_set_ix(target, 1), ns = sel || shown ? -1 : deck_set_ix(target, 0);
    int sf = sel || shown ? -1 : deck_surf_ix(target);
    const char* in = d ? "" : " (no deck beside the model)";
    if (kind == CV_IK_VOLUME) {
        if (shown) {
            uint8_t* v = shown_mask();
            R.el = malloc((size_t)CV_MAX(G.frd.n_elems, 1) * sizeof *R.el);
            for (uint32_t e = 0; v && R.el && e < G.frd.n_elems; e++) if (v[e]) R.el[R.n++] = e;
            free(v);
        } else if (sel) {
            if (!G.sel_n) { snprintf(err, errn, "no elements selected (a box selection with elements ticked)"); return false; }
            R.el = malloc(G.sel_n * sizeof *R.el);
            if (R.el) { memcpy(R.el, G.sel, G.sel_n * sizeof *R.el); R.n = G.sel_n; }
        } else if (es >= 0) set_elems(es);
        else { snprintf(err, errn, "no element set %s%s", target, in); return false; }
        if (!R.n) { snprintf(err, errn, "%s: no elements", target); return false; }
        return true;
    }
    if (kind == CV_IK_SURFACE) {
        if (shown || sel) {
            uint8_t* v = shown_mask();
            if (sel && v) {
                memset(v, 0, CV_MAX(G.frd.n_elems, 1));
                for (uint32_t k = 0; k < G.sel_n; k++) v[G.sel[k]] = 1;
            }
            if (sel && !G.sel_n) { free(v); snprintf(err, errn, "no elements selected (a box selection with elements ticked)"); return false; }
            /* the shown skin's faces when it is what is shown; the selection's own outer faces */
            outer_faces(v, shown && !G.eye_hide_on ? &G.skin : NULL);
            free(v);
        } else if (sf >= 0 && d->surfs[sf].n) {
            const cv_surface* s = &d->surfs[sf];
            R.el = malloc(s->n * sizeof *R.el); R.fc = malloc(s->n);
            for (uint32_t k = 0; R.el && R.fc && k < s->n; k++) {
                uint32_t e = deck_elem(&G.frd, s->elem[k]);
                if (e != UINT32_MAX) { R.el[R.n] = e; R.fc[R.n++] = s->face[k]; }
            }
        } else if (es >= 0) {                               /* an element set: its outer faces */
            uint8_t* v = calloc(CV_MAX(G.frd.n_elems, 1), 1);
            if (v && set_elems(es)) for (uint32_t k = 0; k < R.n; k++) v[R.el[k]] = 1;
            free(R.el); R.el = NULL; R.n = 0;
            if (v) outer_faces(v, NULL);
            free(v);
        } else {
            snprintf(err, errn, sf >= 0 ? "surface %s holds nodes, not element faces" : "no surface or element set %s%s", target, in);
            return false;
        }
        if (!R.n) { snprintf(err, errn, "%s: no faces", target); return false; }
        return true;
    }
    uint8_t* m = calloc(CV_MAX(G.frd.n_nodes, 1), 1);       /* nodes */
    if (!m) return false;
    if (shown) {
        uint8_t* v = shown_mask();
        for (uint32_t e = 0; v && e < G.frd.n_elems; e++) if (v[e]) mark_elem_nodes(m, e);
        free(v);
    } else if (sel) {
        for (uint32_t k = 0; k < G.seln_n; k++) m[G.seln[k]] = 1;
        for (uint32_t k = 0; !G.seln_n && k < G.sel_n; k++) mark_elem_nodes(m, G.sel[k]);
    } else if (ns >= 0) {
        const cv_set* s = &d->sets[ns];
        for (uint32_t k = 0; k < s->n; k++) { uint32_t i = cv_frd_node_index(&G.frd, s->ids[k]); if (i != UINT32_MAX) m[i] = 1; }
    } else if (es >= 0) {
        if (set_elems(es)) for (uint32_t k = 0; k < R.n; k++) mark_elem_nodes(m, R.el[k]);
        free(R.el); R.el = NULL; R.n = 0;
    } else if (sf >= 0) {
        const cv_surface* s = &d->surfs[sf];
        for (uint32_t k = 0; k < s->nn; k++) { uint32_t i = cv_frd_node_index(&G.frd, s->nodes[k]); if (i != UINT32_MAX) m[i] = 1; }
        for (uint32_t k = 0; k < s->n; k++) {
            uint32_t e = deck_elem(&G.frd, s->elem[k]), fn[8];
            int c = e != UINT32_MAX ? cv_face_nodes(&G.frd, e, s->face[k], fn) : 0;
            for (int j = 0; j < c; j++) m[fn[j]] = 1;
        }
    } else {
        free(m);
        snprintf(err, errn, "no node set, element set or surface %s%s", target, in);
        return false;
    }
    bool ok = marked_nodes(m);
    free(m);
    if (!ok) snprintf(err, errn, sel ? "nothing selected" : "%s: no nodes", target);
    return ok;
}

/* ---- the rows: every step with the field ------------------------------------------ */

/* the .frd field shown, as in the current step (any step when not there) */
static const cv_field_desc* shown_desc(void) {
    if (G.field_src != 0 || !G.field_name[0]) return NULL;
    for (int k = 0; k < G.frd.n_steps; k++) {
        int s = (G.step + k) % G.frd.n_steps, fi = find_field(s, G.field_name);
        if (fi >= 0) return &G.frd.steps[s].fields[fi];
    }
    return NULL;
}

static void add_q(const char* name, int how) {
    if (GI.nq >= CV_IQ_MAX) return;
    snprintf(GI.name[GI.nq], sizeof GI.name[0], "%s", name);
    GI.how[GI.nq++] = how;
}

void refresh_integ(void) {
    if (!GI.open || !G.loaded) return;
    char key[700];
    snprintf(key, sizeof key, "%s|%d|%d|%g|%g|%g|%d|%d|%s|%d|%s|%g|%g|%g", G.field_name, G.comp, G.csys, G.csys_o[0], G.csys_o[1],
             G.csys_o[2], G.field_src, G.frd.n_steps, G.field_src == 2 ? G.calc_expr : "", GI.kind, GI.target,
             GI.about[0], GI.about[1], GI.about[2]);
    if (!strcmp(key, R.key)) return;
    snprintf(R.key, sizeof R.key, "%s", key);
    rows_free();
    GI.note[0] = 0;
    const cv_field_desc* d = shown_desc();
    bool calc = G.field_src == 2 && G.calc;
    int ncomp = d ? CV_MIN(d->ncomp, CV_INTEG_MAXC - 1) : 0;
    bool cyl = d && G.csys > 0 && cv_cyl_applies(d);
    bool derived = d && G.comp < 0 && GI.kind != CV_IK_NODES;   /* von Mises, magnitude, principal: one more */
    bool tensor = d && cv_tensor_order(d) == 1;
    int nv = calc ? 1 : ncomp + derived;                     /* values per node handed to integ.c */
    for (int c = 0; c < ncomp; c++) {
        char nm[12];
        if (cyl) cv_cyl_comp_name(d, c, nm); else snprintf(nm, sizeof nm, "%s", d->comp[c]);
        add_q(nm, GI.kind == CV_IK_NODES ? CV_IQ_SUM : CV_IQ_INTEG);
    }
    if (derived) {
        cv_scalar_opt o[CV_MAX_OPTS];
        int no = app_field_options(d, o, CV_MAX_OPTS);
        const char* lab = "shown";
        for (int k = 0; k < no; k++) if (o[k].comp == G.comp) lab = o[k].label;
        add_q(lab, CV_IQ_INTEG);
    }
    if (calc) add_q("formula", GI.kind == CV_IK_NODES ? CV_IQ_SUM : CV_IQ_INTEG);
    int extra = GI.nq;                                        /* forces and moments from here on */
    static const char* const fs[3][3] = { { "Fx = S.n", "Fy = S.n", "Fz = S.n" }, { "Fx pressure", "Fy pressure", "Fz pressure" },
                                          { "Mx", "My", "Mz" } };
    int f = GI.kind == CV_IK_SURFACE && tensor ? 0 : GI.kind == CV_IK_SURFACE && nv == 1 ? 1 : GI.kind == CV_IK_NODES && ncomp >= 3 ? 2 : -1;
    for (int k = 0; f >= 0 && k < 3; k++) add_q(fs[f][k], CV_IQ_FORCE);
    if (!d && !calc && G.field_src != 0)
        snprintf(GI.note, sizeof GI.note, "the field shown is not integrated (an .frd field or a formula is): %s alone",
                 GI.kind == CV_IK_VOLUME ? "the volume" : GI.kind == CV_IK_SURFACE ? "the area" : "the count");

    int ns = CV_MAX(G.frd.n_steps, 1);
    GI.step = malloc(ns * sizeof *GI.step); GI.t = malloc(ns * sizeof *GI.t);
    GI.size = malloc(ns * sizeof *GI.size); GI.val = malloc((size_t)ns * CV_MAX(GI.nq, 1) * sizeof *GI.val);
    size_t N = CV_MAX(G.frd.n_nodes, 1);
    float* P = nv ? malloc(N * nv * sizeof *P) : NULL;
    float* r = cyl ? malloc(N * ncomp * sizeof *r) : NULL;
    if (!GI.step || !GI.t || !GI.size || !GI.val || (nv && !P) || (cyl && !r)) { free(P); free(r); rows_free(); return; }
    step_scratch scr = { NULL, 0 };
    uint32_t skipped = 0, missing = 0;
    for (int s = 0; s < ns; s++) {
        const float* vals = NULL;
        if (d) {
            int fi = find_field(s, G.field_name);
            if (fi < 0 || G.frd.steps[s].fields[fi].ncomp != d->ncomp) continue;
            const float* v = step_field_get(&scr, s, fi);
            if (!v) continue;
            if (cyl) {
                for (size_t i = 0; i < N; i++) memcpy(r + i * ncomp, v + i * d->ncomp, ncomp * sizeof *r);
                cv_cyl_values(d, G.frd.xyz, G.frd.n_nodes, G.csys - 1, G.csys_o, r);
            }
            for (size_t i = 0; i < G.frd.n_nodes; i++) {
                const float* q = cyl ? r + i * ncomp : v + i * d->ncomp;
                memcpy(P + i * nv, q, ncomp * sizeof *P);
                if (derived) cv_field_scalar(v + i * d->ncomp, d->ncomp, 1, G.comp, P + i * nv + ncomp);
            }
            vals = P;
        } else if (calc) {
            if (s >= G.frd.n_steps || !cv_calc_eval(G.calc, &G.frd, s, step_field_get, &scr, NULL, G.frd.n_nodes, P)) continue;
            vals = P;
        } else if (G.frd.n_steps && s != G.step) continue;             /* the size alone: one row */
        double* row = GI.val + (size_t)GI.n * GI.nq;
        if (GI.kind == CV_IK_NODES) {
            cv_nsum o;
            cv_integ_nodes(&G.frd, R.nodes, R.nn, vals, vals ? nv : 0, GI.about, &o);
            GI.size[GI.n] = o.n;
            for (int q = 0; q < extra; q++) row[q] = o.sum[q];
            for (int q = extra; q < GI.nq; q++) row[q] = o.moment[q - extra];
            missing = CV_MAX(missing, o.missing);
        } else {
            cv_integ o;
            if (GI.kind == CV_IK_VOLUME) cv_integ_volume(&G.frd, R.el, R.n, vals, vals ? nv : 0, &o);
            else cv_integ_faces(&G.frd, R.el, R.fc, R.n, vals, vals ? nv : 0, &o);
            GI.size[GI.n] = o.size;
            for (int q = 0; q < extra; q++) row[q] = o.integ[q];
            for (int q = extra; q < GI.nq; q++) row[q] = tensor ? o.traction[q - extra] : o.push[q - extra];
            skipped = CV_MAX(skipped, o.skipped);
            missing = CV_MAX(missing, o.missing);
        }
        GI.step[GI.n] = s;
        GI.t[GI.n] = s < G.frd.n_steps ? G.frd.steps[s].time : 0;
        GI.n++;
    }
    free(scr.buf); free(P); free(r);
    size_t l = strlen(GI.note);
    const char* unit = GI.kind == CV_IK_NODES ? "nodes" : GI.kind == CV_IK_SURFACE ? "faces" : "elements";
    if (skipped)
        l += snprintf(GI.note + l, sizeof GI.note - l, "%s%u %s left out: not solid (shells and beams count as the solids CalculiX expanded them to)",
                      l ? "; " : "", skipped, unit);
    if (missing && l < sizeof GI.note)
        snprintf(GI.note + l, sizeof GI.note - l, "%s%u %s left out: a node without a value", l ? "; " : "", missing, unit);
}

void app_integ_refresh(void) { refresh_integ(); }

/* ---- opening, the command line, the CSV --------------------------------------------- */

bool app_integ_open(int kind, const char* target_in) {
    char err[200], target[64];
    snprintf(target, sizeof target, "%s", target_in);        /* it may be GI.target itself */
    if (!G.loaded || kind < 0 || kind >= CV_IK_N) return false;
    if (!region(kind, target, err, sizeof err)) {
        char e2[8];
        region_free();
        if (GI.open && !region(GI.kind, GI.target, e2, sizeof e2)) app_integ_close();   /* the window keeps what it had */
        snprintf(G.note, sizeof G.note, "integrals: %s", err); G.note_t = cv_now();
        return false;
    }
    rows_free();
    GI.open = true;
    GI.kind = kind;
    snprintf(GI.target, sizeof GI.target, "%s", target);
    bool gen = !strcasecmp(target, "selection") || !strcasecmp(target, "shown");
    bool surf = !gen && deck_surf_ix(target) >= 0 && (kind == CV_IK_SURFACE || deck_set_ix(target, -1) < 0);
    const char* of = gen ? "the " : surf ? "surface " : "set ";
    if (kind == CV_IK_NODES) snprintf(GI.what, sizeof GI.what, "sum over %s%s: %u nodes", of, target, R.nn);
    else if (kind == CV_IK_VOLUME) snprintf(GI.what, sizeof GI.what, "volume of %s%s: %u elements", of, target, R.n);
    else if (surf) snprintf(GI.what, sizeof GI.what, "surface %s: %u faces", target, R.n);
    else snprintf(GI.what, sizeof GI.what, "outer faces of %s%s: %u", of, target, R.n);
    R.key[0] = 0;
    refresh_integ();
    return true;
}

/* KIND:TARGET[@x,y,z] */
bool app_integ_open_spec(const char* spec) {
    char k[16], t[64];
    const char* colon = strchr(spec, ':');
    if (!colon) { snprintf(G.note, sizeof G.note, "--integrate %s: KIND:TARGET, KIND volume, surface or nodes", spec); G.note_t = cv_now(); return false; }
    snprintf(k, sizeof k, "%.*s", (int)CV_MIN((size_t)(colon - spec), sizeof k - 1), spec);
    snprintf(t, sizeof t, "%s", colon + 1);
    char* at = strchr(t, '@');
    double about[3] = { 0, 0, 0 };
    if (at) { *at = 0; sscanf(at + 1, "%lf,%lf,%lf", &about[0], &about[1], &about[2]); }
    int kind = -1;
    for (int i = 0; i < CV_IK_N; i++) if (!strcasecmp(k, app_integ_kind(i))) kind = i;
    if (kind < 0) { snprintf(G.note, sizeof G.note, "--integrate %s: KIND is volume, surface or nodes", spec); G.note_t = cv_now(); return false; }
    memcpy(GI.about, about, sizeof about);
    return app_integ_open(kind, t);
}

void app_integ_csv_path(char* out, size_t n) {
    const char* dot = strrchr(G.path, '.'), *sep = strrchr(G.path, cv_path_sep());
    int base = dot && dot > (sep ? sep : G.path) ? (int)(dot - G.path) : (int)strlen(G.path);
    char what[96];
    bool gen = !strcasecmp(GI.target, "selection") || !strcasecmp(GI.target, "shown");
    snprintf(what, sizeof what, gen ? "%s_%s" : "%s", GI.target, app_integ_kind(GI.kind));
    for (char* c = what; *c; c++) if (!isalnum((unsigned char)*c) && *c != '-' && *c != '_') *c = '_';
    snprintf(out, n, "%.*s_integral_%s.csv", base, G.path, what);
}

bool app_integ_csv(const char* path) {
    if (!GI.open || !GI.n) return false;
    FILE* o = fopen(path, "wb");
    if (!o) return false;
    cv_fprintf(o, "step,time,%s", GI.kind == CV_IK_NODES ? "nodes" : GI.kind == CV_IK_SURFACE ? "area" : "volume");
    for (int q = 0; q < GI.nq; q++) {
        if (GI.how[q] == CV_IQ_INTEG) cv_fprintf(o, ",integral %s,average %s", GI.name[q], GI.name[q]);
        else cv_fprintf(o, GI.how[q] == CV_IQ_SUM ? ",sum %s" : ",%s", GI.name[q]);
    }
    cv_fprintf(o, "\n");
    for (int i = 0; i < GI.n; i++) {
        cv_fprintf(o, "%d,%.9g,%.9g", GI.step[i] + 1, GI.t[i], GI.size[i]);
        for (int q = 0; q < GI.nq; q++) {
            double v = GI.val[(size_t)i * GI.nq + q];
            cv_fprintf(o, ",%.9g", v);
            if (GI.how[q] == CV_IQ_INTEG) cv_fprintf(o, ",%.9g", GI.size[i] > 0 ? v / GI.size[i] : NAN);
        }
        cv_fprintf(o, "\n");
    }
    return fclose(o) == 0;
}

/* --integrate-csv: no window. The field: --field, else STRESS (FORC for nodes), else
   the first of the last step; its first option (von Mises, the magnitude) */
int app_integ_headless(const char* model, const char* spec, const char* field, const char* out) {
    if (!app_load_headless(model)) return 1;
    const char* want = field ? field : strncasecmp(spec, "nodes", 5) ? "STRESS" : "FORC";
    G.field_src = 0;
    G.step = CV_MAX(G.frd.n_steps - 1, 0);
    for (int s = 0; s < G.frd.n_steps && !G.field_name[0]; s++)
        if (find_field(s, want) >= 0) { snprintf(G.field_name, sizeof G.field_name, "%s", want); G.step = s; }
    if (!G.field_name[0] && field) { fprintf(stderr, "--integrate-csv: no field %s in %s\n", field, model); return 1; }
    if (!G.field_name[0] && G.frd.n_steps && G.frd.steps[G.step].nfields)
        snprintf(G.field_name, sizeof G.field_name, "%s", G.frd.steps[G.step].fields[0].name);
    int fi = find_field(G.step, G.field_name);
    cv_scalar_opt o[CV_MAX_OPTS];
    G.comp = fi >= 0 && cv_field_options(&G.frd.steps[G.step].fields[fi], o, CV_MAX_OPTS) > 0 ? o[0].comp : 0;
    if (!app_integ_open_spec(spec)) { fprintf(stderr, "%s\n", G.note); return 1; }
    if (!app_integ_csv(out)) { fprintf(stderr, "--integrate-csv: cannot write %s\n", out); return 1; }
    printf("%s: %s, %s, %d step%s -> %s\n", model, GI.what, G.field_name[0] ? G.field_name : "no field", GI.n, GI.n == 1 ? "" : "s", out);
    if (GI.note[0]) printf("note: %s\n", GI.note);
    return 0;
}
