/* report.c -- model summary report (report.h) */
#include "report.h"
#include "quality.h"
#include "units.h"
#include <math.h>
#include <string.h>

#define CAP 100         /* table rows before "... and N more" */
#define P(...) fprintf(out, __VA_ARGS__)

static void esc(FILE* out, const char* s) {     /* a table cell: no pipes or line breaks */
    for (; *s; s++) {
        if (*s == '|') fputs("\\|", out);
        else if (*s == '\n' || *s == '\r') fputc(' ', out);
        else fputc(*s, out);
    }
}
static void more(FILE* out, int n, int cap) { if (n > cap) fprintf(out, "\n... and %d more\n", n - cap); }
static const char* base(const char* p) { return p ? cv_basename(p) : ""; }

/* ---- grouped load lines ---- */
typedef struct { int kind, a, b, amp; float val; uint32_t n, lo, hi; } grp;
typedef struct { grp g[256]; int n; uint32_t over; } grps;
static void gadd(grps* G, int kind, int a, int b, float val, int amp, uint32_t id) {
    for (int i = 0; i < G->n; i++) {
        grp* g = &G->g[i];
        if (g->kind == kind && g->a == a && g->b == b && g->amp == amp && g->val == val) {
            g->n++;
            if (id < g->lo) g->lo = id;
            if (id > g->hi) g->hi = id;
            return;
        }
    }
    if (G->n >= 256) { G->over++; return; }
    G->g[G->n++] = (grp){ kind, a, b, amp, val, 1, id, id };
}
static const char* ampname(const cv_inp* d, int amp) { return amp > 0 && amp <= d->namps ? d->amps[amp - 1].name : "-"; }
static void ids(FILE* out, const grp* g) { if (g->n == 1) P("%u", g->lo); else P("%u .. %u", g->lo, g->hi); }

static const char* DL[] = { "pressure", "edge load", "heat flux", "film", "radiation" };
static const char* BL[] = { "gravity", "centrifugal", "body force", "Newton gravity", "volume heat" };

static void dofs(FILE* out, int lo, int hi) { if (lo == hi) P("%d", lo); else P("%d-%d", lo, hi); }

static void step_loads(FILE* out, const cv_inp* d, int s) {
    grps* G = calloc(1, sizeof *G);
    if (!G) return;
    #define RESET() do { G->n = 0; G->over = 0; } while (0)
    #define TAIL() do { if (G->over) P("\n... and %u more groups\n", G->over); P("\n"); } while (0)
    for (uint32_t i = 0; i < d->nbcs; i++) if (d->bcs[i].step == s && d->bcs[i].node) gadd(G, 0, d->bcs[i].dof_lo, d->bcs[i].dof_hi, d->bcs[i].value, d->bcs[i].amp, d->bcs[i].node);
    if (G->n) {
        P("Constraints (*BOUNDARY)\n\n| DOFs | Value | Amplitude | Nodes | Node ids |\n|---|---|---|---|---|\n");
        for (int i = 0; i < G->n; i++) {
            grp* g = &G->g[i];
            P("| "); dofs(out, g->a, g->b); P(" | "); if (g->val == 0) P("0 (held)"); else P("%g", g->val);
            P(" | "); esc(out, ampname(d, g->amp)); P(" | %u | ", g->n); ids(out, g); P(" |\n");
        }
        TAIL();
    }
    RESET();
    for (uint32_t i = 0; i < d->ncloads; i++) if (d->cloads[i].step == s && d->cloads[i].node) gadd(G, 0, d->cloads[i].dof, d->cloads[i].dof, d->cloads[i].value, d->cloads[i].amp, d->cloads[i].node);
    if (G->n) {
        P("Concentrated loads (*CLOAD, *CFLUX)\n\n| DOF | Magnitude | Amplitude | Nodes | Total | Node ids |\n|---|---|---|---|---|---|\n");
        for (int i = 0; i < G->n; i++) {
            grp* g = &G->g[i];
            P("| %d | %g | ", g->a, g->val); esc(out, ampname(d, g->amp)); P(" | %u | %g | ", g->n, (double)g->val * g->n); ids(out, g); P(" |\n");
        }
        TAIL();
    }
    RESET();
    for (uint32_t i = 0; i < d->ndloads; i++) if (d->dloads[i].step == s && d->dloads[i].elem) gadd(G, d->dloads[i].kind, 0, 0, d->dloads[i].value, d->dloads[i].amp, d->dloads[i].elem);
    if (G->n) {
        P("Distributed loads on element faces (*DLOAD, *DSLOAD, *DFLUX, *FILM, *RADIATE)\n\n| Type | Value | Amplitude | Faces |\n|---|---|---|---|\n");
        for (int i = 0; i < G->n; i++) {
            grp* g = &G->g[i];
            P("| %s | %g | ", g->kind >= 0 && g->kind < CV_DL_N ? DL[g->kind] : "?", g->val); esc(out, ampname(d, g->amp)); P(" | %u |\n", g->n);
        }
        TAIL();
    }
    int nb = 0;
    for (uint32_t i = 0; i < d->nbody; i++) if (d->body[i].step == s && d->body[i].set != -2) nb++;
    if (nb) {
        P("Body loads (*DLOAD on elements)\n\n| Type | Elements | Value | Vector | Amplitude |\n|---|---|---|---|---|\n");
        int k = 0;
        for (uint32_t i = 0; i < d->nbody; i++) {
            const cv_body* b = &d->body[i];
            if (b->step != s || b->set == -2 || ++k > CAP) continue;
            P("| %s | ", b->kind < CV_BL_N ? BL[b->kind] : "?");
            if (b->set >= 0 && b->set < d->nsets) esc(out, d->sets[b->set].name); else P("element %u", b->elem);
            P(" | %g | %g %g %g | ", b->value, b->v[0], b->v[1], b->v[2]); esc(out, ampname(d, b->amp)); P(" |\n");
        }
        more(out, nb, CAP); P("\n");
    }
    RESET();
    for (uint32_t i = 0; i < d->ntemps; i++) if (d->temps[i].step == s) gadd(G, 0, 0, 0, d->temps[i].value, d->temps[i].amp, d->temps[i].node);
    if (G->n) {
        P("Prescribed temperatures (*TEMPERATURE)\n\n| Temperature | Amplitude | Nodes | Node ids |\n|---|---|---|---|\n");
        for (int i = 0; i < G->n; i++) { grp* g = &G->g[i]; P("| %g | ", g->val); esc(out, ampname(d, g->amp)); P(" | %u | ", g->n); ids(out, g); P(" |\n"); }
        TAIL();
    }
    free(G);
}

static bool step_has(const cv_inp* d, int s) {
    for (uint32_t i = 0; i < d->nbcs; i++) if (d->bcs[i].step == s && d->bcs[i].node) return true;
    for (uint32_t i = 0; i < d->ncloads; i++) if (d->cloads[i].step == s && d->cloads[i].node) return true;
    for (uint32_t i = 0; i < d->ndloads; i++) if (d->dloads[i].step == s && d->dloads[i].elem) return true;
    for (uint32_t i = 0; i < d->nbody; i++) if (d->body[i].step == s && d->body[i].set != -2) return true;
    for (uint32_t i = 0; i < d->ntemps; i++) if (d->temps[i].step == s) return true;
    return false;
}

/* ---- mesh quality ---- */
static void quality(FILE* out, const cv_frd* m) {
    cv_mq_init();
    double mn[CV_MQ_N], mx[CV_MQ_N], sum[CV_MQ_N];
    uint32_t cnt[CV_MQ_N] = {0}, poor[CV_MQ_N] = {0};
    for (int q = 0; q < CV_MQ_N; q++) { mn[q] = INFINITY; mx[q] = -INFINITY; sum[q] = 0; }
    uint32_t nmeas = 0;
    for (uint32_t e = 0; e < m->n_elems; e++) {
        int t = m->etype[e];
        uint32_t b = m->eoff[e], nn = m->eoff[e + 1] - b;
        if (!cv_mq_dim(t) || nn > 32 || !nn) continue;
        double x[32][3], v[CV_MQ_N];
        for (uint32_t i = 0; i < nn; i++) for (int a = 0; a < 3; a++) x[i][a] = m->xyz[3 * (size_t)m->conn[b + i] + a];
        cv_mq_elem(t, (int)nn, (const double (*)[3])x, v);
        nmeas++;
        for (int q = 0; q < CV_MQ_N; q++) {
            if (!isfinite(v[q])) continue;
            cnt[q]++; sum[q] += v[q];
            if (v[q] < mn[q]) mn[q] = v[q];
            if (v[q] > mx[q]) mx[q] = v[q];
            if (cv_mq_poor(q, t, v[q])) poor[q]++;
        }
    }
    if (!nmeas) return;
    P("Mesh quality\n\n%u elements measured on the undeformed mesh, corner nodes only for quadratic elements; limits as the Mesh quality window (usual limits per element type). Lengths in input units.\n\n", nmeas);
    P("| Metric | Elements | Min | Max | Mean | Past the limit |\n|---|---|---|---|---|---|\n");
    for (int q = 0; q < CV_MQ_N; q++) {
        if (!cnt[q]) continue;
        const cv_mq_info* i = cv_mq(q);
        P("| %s%s | %u | %.4g | %.4g | %.4g | %u |\n", i->name, i->deg ? " (deg)" : "", cnt[q], mn[q], mx[q], sum[q] / cnt[q], poor[q]);
    }
    P("\n");
}

static int ori_of(const cv_inp* d, uint32_t id) {
    uint32_t lo = 0, hi = d->nelem_ori;
    while (lo < hi) { uint32_t mid = (lo + hi) / 2; if (d->elem_ori[mid].id < id) lo = mid + 1; else hi = mid; }
    return lo < d->nelem_ori && d->elem_ori[lo].id == id ? d->elem_ori[lo].ix : -2;
}

bool cv_report_md(FILE* out, const cv_frd* f, const cv_inp* d, const cv_report_opts* o) {
    const cv_frd* m = d ? &d->mesh : f;
    if (!out || !m) return false;
    cv_report_opts z = {0};
    if (!o) o = &z;

    /* 1. general */
    P("# Model summary\n\nThe model definition only, as ccxview reads it from the files; numbers are in the units of the input.\n\n## 1. General\n\n| | |\n|---|---|\n");
    if (o->frd_path) { P("| Result file | "); esc(out, base(o->frd_path)); P(" |\n"); }
    if (o->inp_path) { P("| Input deck | "); esc(out, base(o->inp_path)); P(" |\n"); }
    if (d && d->heading[0]) { P("| Heading | "); esc(out, d->heading); P(" |\n"); }
    if (f) {
        const cv_frd_head* h = &f->head;
        if (h->code[0]) { P("| Solver | "); esc(out, h->code); P(" |\n"); }
        if (h->pgm[0] || h->version[0]) { P("| Program | "); esc(out, h->pgm); P(" "); esc(out, h->version); P(" |\n"); }
        if (h->date[0]) { P("| Date | "); esc(out, h->date); P(" |\n"); }
        if (f->n_steps) {
            int ns = 0, last = -1;
            for (int i = 0; i < f->n_steps; i++) if (f->steps[i].step != last) { ns++; last = f->steps[i].step; }
            P("| Result increments | %d in %d step%s |\n", f->n_steps, ns, ns == 1 ? "" : "s");
        }
    }
    if (o->units > 0 && o->units < CV_SYS_N) { P("| Units | "); esc(out, cv_sys_name(o->units)); P(": "); esc(out, cv_sys_base(o->units)); P(" |\n"); }
    else P("| Units | not set (CalculiX has none; set in the Units window) |\n");
    P("\n");
    if (d && d->nsteps > 0) {
        P("Analysis steps\n\n| Step | Procedure | Time period |\n|---|---|---|\n");
        for (int s = 0; s < d->nsteps && s < CAP; s++) {
            const char* pn = d->proc ? cv_inp_proc_name(d->proc[s]) : "";
            P("| %d | %s | ", s + 1, pn[0] ? pn : "-");
            if (d->stepinfo && d->stepinfo[s].proc[0] && d->stepinfo[s].period > 0) P("%g", d->stepinfo[s].period); else P("-");
            P(" |\n");
        }
        more(out, d->nsteps, CAP);
        P("\nNonlinear geometry (NLGEOM), time increments and solver controls, output requests: not read.\n\n");
    }

    /* 2. mesh */
    P("## 2. Mesh\n\n");
    if (m->n_nodes) {
        P("| | |\n|---|---|\n| Nodes | %u |\n| Elements | %u |\n", m->n_nodes, m->n_elems);
        double lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
        for (uint32_t i = 0; i < m->n_nodes; i++) for (int a = 0; a < 3; a++) {
            double v = m->xyz[3 * (size_t)i + a];
            if (v < lo[a]) lo[a] = v;
            if (v > hi[a]) hi[a] = v;
        }
        P("| Bounding box min | %g, %g, %g |\n| Bounding box max | %g, %g, %g |\n| Bounding box size | %g, %g, %g |\n",
          lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
        if (d && f && f->n_nodes && (f->n_nodes != m->n_nodes || f->n_elems != m->n_elems))
            P("| Result file | %u nodes, %u elements (CalculiX expands shells and beams into solids) |\n", f->n_nodes, f->n_elems);
        P("\n");
        uint32_t per[16] = {0}, dim[4] = {0};
        for (uint32_t e = 0; e < m->n_elems; e++) { int t = m->etype[e]; if (t > 0 && t < 16) per[t]++; dim[cv_mq_dim(t)]++; }
        P("Elements by type\n\n| Type | Dimension | Elements |\n|---|---|---|\n");
        for (int t = 1; t < 16; t++) if (per[t]) {
            int dm = cv_mq_dim(t);
            P("| %s | %s | %u |\n", cv_frd_type_name(t), dm == 3 ? "3D" : dm == 2 ? "2D" : dm == 1 ? "1D" : "other", per[t]);
        }
        P("\nBy dimension: 3D %u, 2D %u, 1D %u, other %u\n\n", dim[3], dim[2], dim[1], dim[0]);
    }
    if (d) for (int pass = 1; pass >= 0; pass--) {
        int n = 0;
        for (int i = 0; i < d->nsets; i++) n += d->sets[i].is_elem == pass;
        if (!n) continue;
        P("%s sets\n\n| Set | %s |\n|---|---|\n", pass ? "Element" : "Node", pass ? "Elements" : "Nodes");
        int k = 0;
        for (int i = 0; i < d->nsets; i++) if (d->sets[i].is_elem == pass && ++k <= CAP) { P("| "); esc(out, d->sets[i].name); P(" | %u |\n", d->sets[i].n); }
        more(out, n, CAP); P("\n");
    }
    quality(out, m);

    if (d) {
        /* 3. materials */
        if (d->nmats) {
            uint32_t* use = calloc(d->nmats, sizeof *use);
            for (uint32_t e = 0; use && e < m->n_elems; e++) if (m->emat[e] && m->emat[e] <= (uint32_t)d->nmats) use[m->emat[e] - 1]++;
            P("## 3. Materials\n\n| Material | Elastic model | Constants | First yield stress (*PLASTIC) | Elements |\n|---|---|---|---|---|\n");
            static const char* EN[] = { "-", "isotropic", "engineering constants", "orthotropic", "anisotropic" };
            for (int i = 0; i < d->nmats && i < CAP; i++) {
                const cv_matprop* p = d->mprop ? &d->mprop[i] : NULL;
                P("| "); esc(out, d->mats[i]); P(" | %s | ", p ? EN[p->el] : "-");
                if (p && p->el == CV_EL_ISO) P("E = %g, nu = %g", p->c[0], p->c[1]);
                else if (p && p->el == CV_EL_ENG) P("E1 %g, E2 %g, E3 %g, nu12 %g, nu13 %g, nu23 %g, G12 %g, G13 %g, G23 %g", p->c[0], p->c[1], p->c[2], p->c[3], p->c[4], p->c[5], p->c[6], p->c[7], p->c[8]);
                else if (p && p->el >= CV_EL_ORTHO) P("first 9 terms: %g %g %g %g %g %g %g %g %g", p->c[0], p->c[1], p->c[2], p->c[3], p->c[4], p->c[5], p->c[6], p->c[7], p->c[8]);
                else P("-");
                P(" | "); if (p && p->sy > 0) P("%g", p->sy); else P("none"); P(" | %u |\n", use ? use[i] : 0);
            }
            more(out, d->nmats, CAP);
            P("\nNot read: density, thermal conductivity, expansion and specific heat, plasticity beyond the first yield stress, hardening, creep and other constitutive keywords.\n\n");
            free(use);
        }
        /* 4. sections */
        if (d->nsects || d->norients) {
            P("## 4. Sections and orientations\n\n");
            if (d->nsects) {
                P("| Type | Element set | Material | Elements | Orientation |\n|---|---|---|---|---|\n");
                for (int i = 0; i < d->nsects && i < CAP; i++) {
                    const cv_section* s = &d->sects[i];
                    const cv_set* set = cv_inp_set(d, s->elset, true);
                    P("| "); esc(out, s->kind); P(" | "); esc(out, s->elset); P(" | "); esc(out, s->mat); P(" | %u | ", set ? set->n : 0);
                    int seen[3] = {-3, -3, -3}, ns = 0; bool cannot = false;
                    for (uint32_t k = 0; set && k < set->n && d->nelem_ori; k++) {
                        int ix = ori_of(d, set->ids[k]);
                        if (ix == -1) cannot = true;
                        if (ix < 0) continue;
                        bool dup = false;
                        for (int j = 0; j < ns; j++) dup |= seen[j] == ix;
                        if (!dup && ns < 3) seen[ns++] = ix;
                    }
                    if (!ns && !cannot) P("-");
                    for (int j = 0; j < ns; j++) { P("%s", j ? ", " : ""); if (d->orient_names && seen[j] < d->norients) esc(out, d->orient_names[seen[j]]); }
                    if (cannot) P("%s(distribution / composite)", ns ? ", " : "");
                    P(" |\n");
                }
                more(out, d->nsects, CAP);
                P("\nShell thicknesses and beam cross-section dimensions: not read.\n\n");
            }
            if (d->norients) P("Orientations (*ORIENTATION): %d. ", d->norients);
            if (d->ntransforms) P("Transforms (*TRANSFORM): %d systems on %u nodes.", d->ntransforms, d->nnode_tr);
            if (d->norients || d->ntransforms) P("\n\n");
        }
        /* 5. loads per step */
        {
            bool any = false;
            for (int s = -1; s < d->nsteps; s++) any |= step_has(d, s);
            if (any) {
                P("## 5. Boundary conditions and loads\n\nValues as given in the deck, per step (a step keeps those of the steps before unless OP=NEW).\n\n");
                for (int s = -1; s < d->nsteps; s++) {
                    if (!step_has(d, s)) continue;
                    if (s < 0) P("### Before the first step\n\n"); else P("### Step %d\n\n", s + 1);
                    step_loads(out, d, s);
                }
            }
            if (d->namps) {
                P("### Amplitudes\n\n| Name | Definition | Points | Time |\n|---|---|---|---|\n");
                for (int i = 0; i < d->namps && i < CAP; i++) {
                    P("| "); esc(out, d->amps[i].name); P(" | "); esc(out, d->amps[i].def);
                    P(" | %u | %s |\n", d->amps[i].n, d->amps[i].total ? "total" : "step");
                }
                more(out, d->namps, CAP); P("\n");
            }
        }
        /* 6. constraints and interactions */
        if (d->nlinks || d->nsurfs || d->ndisc || d->npret || d->nsubs || d->cyc_n) {
            P("## 6. Constraints and interactions\n\n");
            if (d->nlinks) {
                static const char* LN[] = { "rigid body", "kinematic coupling", "distributing coupling", "equation / MPC", "tie", "contact pair" };
                P("| Kind | Name | Reference node | Nodes | Slave surface | Master surface | Friction |\n|---|---|---|---|---|---|---|\n");
                for (int i = 0; i < d->nlinks && i < CAP; i++) {
                    const cv_link* l = &d->links[i];
                    P("| %s | ", l->kind < 6 ? LN[l->kind] : "?"); esc(out, l->name); P(" | ");
                    if (l->ref) P("%u", l->ref); else P("-");
                    P(" | %u | ", l->n);
                    for (int k = 0; k < 2; k++) {
                        if (l->surf[k] >= 0 && l->surf[k] < d->nsurfs) { const cv_surface* s = &d->surfs[l->surf[k]]; esc(out, s->name); P(" (%u %s)", s->nn ? s->nn : s->n, s->nn ? "nodes" : "faces"); } else P("-");
                        P(" | ");
                    }
                    if (l->kind == CV_LINK_CONTACT && l->mu > 0) P("mu = %g", l->mu); else P("-");
                    P(" |\n");
                }
                more(out, d->nlinks, CAP);
                P("\nContact formulation, normal behaviour and friction beyond the coefficient: not read.\n\n");
            }
            if (d->nsurfs) {
                P("Surfaces\n\n| Surface | Type | Faces | Nodes |\n|---|---|---|---|\n");
                for (int i = 0; i < d->nsurfs && i < CAP; i++) { P("| "); esc(out, d->surfs[i].name); P(" | %s | %u | %u |\n", d->surfs[i].nn ? "node" : "element", d->surfs[i].n, d->surfs[i].nn); }
                more(out, d->nsurfs, CAP); P("\n");
            }
            if (d->ndisc) {
                uint32_t c[5] = {0};
                for (uint32_t i = 0; i < d->ndisc; i++) if (d->disc[i].kind < 5) c[d->disc[i].kind]++;
                P("Discrete elements: %u springs, %u dashpots, %u masses, %u gaps, %u coupling.\n\n", c[0], c[1], c[2], c[3], c[4]);
            }
            if (d->npret) P("Pre-tension sections: %u.\n\n", d->npret);
            if (d->nsubs) P("Submodels: %d.\n\n", d->nsubs);
            if (d->cyc_n) P("Cyclic symmetry: %d sectors.\n\n", d->cyc_n);
        }
        if (d->nmchg) P("Model changes (*MODEL CHANGE): %u.\n\n", d->nmchg);
    }
    fflush(out);
    return !ferror(out);
}
