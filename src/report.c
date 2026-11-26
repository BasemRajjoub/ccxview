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
typedef struct { int kind, a, b, amp; float val; uint32_t n, lo, hi; const char* nm; bool surf; } grp;   /* nm: the set or surface a deck line named, NULL ids */
typedef struct { grp g[256]; int n; uint32_t over; } grps;
static void gadd(grps* G, int kind, int a, int b, float val, int amp, uint32_t id, const char* nm, uint32_t cnt) {
    for (int i = 0; i < G->n; i++) {
        grp* g = &G->g[i];
        if (g->kind == kind && g->a == a && g->b == b && g->amp == amp && g->val == val && (g->nm == nm || (g->nm && nm && !strcmp(g->nm, nm)))) {
            g->n += cnt;
            if (id < g->lo) g->lo = id;
            if (id > g->hi) g->hi = id;
            return;
        }
    }
    if (G->n >= 256) { G->over++; return; }
    G->g[G->n++] = (grp){ kind, a, b, amp, val, cnt, id, id, nm, false };
}
static const char* ampname(const cv_inp* d, int amp) { return amp > 0 && amp <= d->namps ? d->amps[amp - 1].name : "-"; }
static void ids(FILE* out, const grp* g) {
    if (g->nm) { P(g->surf ? "surface " : "set "); esc(out, g->nm); }
    else if (g->n == 1) P("%u", g->lo);
    else P("%u .. %u", g->lo, g->hi);
}
/* which entries of bcs / cloads / dloads / temps a named deck line made (CV_LS_) */
typedef struct { uint8_t* m[4]; } cover;
static void cover_init(cover* c, const cv_inp* d) {
    uint32_t n[4] = { d->nbcs, d->ncloads, d->ndloads, d->ntemps };
    for (int k = 0; k < 4; k++) c->m[k] = calloc(n[k] ? n[k] : 1, 1);
    for (uint32_t i = 0; i < d->nlsrc; i++) {
        const cv_loadsrc* s = &d->lsrc[i];
        if (s->kind < 4 && c->m[s->kind] && s->first + (uint64_t)s->n <= n[s->kind]) memset(c->m[s->kind] + s->first, 1, s->n);
    }
}
static void cover_free(cover* c) { for (int k = 0; k < 4; k++) free(c->m[k]); }

static const char* DL[] = { "pressure", "edge load", "heat flux", "film", "radiation" };
static const char* BL[] = { "gravity", "centrifugal", "body force", "Newton gravity", "volume heat" };

static void dofs(FILE* out, int lo, int hi) { if (lo == hi) P("%d", lo); else P("%d-%d", lo, hi); }

static void step_loads(FILE* out, const cv_inp* d, const cover* cv, int s) {
    grps* G = calloc(1, sizeof *G);
    if (!G) return;
    #define RESET() do { G->n = 0; G->over = 0; } while (0)
    #define TAIL() do { if (G->over) P("\n... and %u more groups\n", G->over); P("\n"); } while (0)
    for (uint32_t i = 0; i < d->nbcs; i++) if (d->bcs[i].step == s && d->bcs[i].node && !(cv->m[0] && cv->m[0][i])) gadd(G, 0, d->bcs[i].dof_lo, d->bcs[i].dof_hi, d->bcs[i].value, d->bcs[i].amp, d->bcs[i].node, NULL, 1);
    for (uint32_t i = 0; i < d->nlsrc; i++) if (d->lsrc[i].kind == CV_LS_BC && d->bcs[d->lsrc[i].first].step == s) { const cv_bc* b = &d->bcs[d->lsrc[i].first]; gadd(G, 0, b->dof_lo, b->dof_hi, b->value, b->amp, 0, d->lsrc[i].name, d->lsrc[i].n); }
    if (G->n) {
        P("Constraints (*BOUNDARY)\n\n| DOFs | Value | Amplitude | Nodes | Node set / node ids |\n|---|---|---|---|---|\n");
        for (int i = 0; i < G->n; i++) {
            grp* g = &G->g[i];
            P("| "); dofs(out, g->a, g->b); P(" | "); if (g->val == 0) P("0 (held)"); else P("%g", g->val);
            P(" | "); esc(out, ampname(d, g->amp)); P(" | %u | ", g->n); ids(out, g); P(" |\n");
        }
        TAIL();
    }
    RESET();
    for (uint32_t i = 0; i < d->ncloads; i++) if (d->cloads[i].step == s && d->cloads[i].node && !(cv->m[1] && cv->m[1][i])) gadd(G, 0, d->cloads[i].dof, d->cloads[i].dof, d->cloads[i].value, d->cloads[i].amp, d->cloads[i].node, NULL, 1);
    for (uint32_t i = 0; i < d->nlsrc; i++) if (d->lsrc[i].kind == CV_LS_CLOAD && d->cloads[d->lsrc[i].first].step == s) { const cv_cload* b = &d->cloads[d->lsrc[i].first]; gadd(G, 0, b->dof, b->dof, b->value, b->amp, 0, d->lsrc[i].name, d->lsrc[i].n); }
    if (G->n) {
        P("Concentrated loads (*CLOAD, *CFLUX)\n\n| DOF | Magnitude | Amplitude | Nodes | Total | Node set / node ids |\n|---|---|---|---|---|---|\n");
        for (int i = 0; i < G->n; i++) {
            grp* g = &G->g[i];
            P("| %d | %g | ", g->a, g->val); esc(out, ampname(d, g->amp)); P(" | %u | %g | ", g->n, (double)g->val * g->n); ids(out, g); P(" |\n");
        }
        TAIL();
    }
    RESET();
    for (uint32_t i = 0; i < d->ndloads; i++) if (d->dloads[i].step == s && d->dloads[i].elem && !(cv->m[2] && cv->m[2][i])) gadd(G, d->dloads[i].kind, 0, 0, d->dloads[i].value, d->dloads[i].amp, d->dloads[i].elem, NULL, 1);
    for (uint32_t i = 0; i < d->nlsrc; i++) if (d->lsrc[i].kind == CV_LS_DLOAD && d->dloads[d->lsrc[i].first].step == s) { const cv_dload* b = &d->dloads[d->lsrc[i].first]; gadd(G, b->kind, 0, 0, b->value, b->amp, 0, d->lsrc[i].name, d->lsrc[i].n); }
    for (int i = 0; i < G->n; i++) for (int k = 0; G->g[i].nm && k < d->nsurfs; k++) if (!strcmp(d->surfs[k].name, G->g[i].nm) && !cv_inp_set(d, G->g[i].nm, true)) G->g[i].surf = true;
    if (G->n) {
        P("Distributed loads on element faces (*DLOAD, *DSLOAD, *DFLUX, *FILM, *RADIATE)\n\n| Type | Value | Amplitude | Faces | Element set / surface / element ids |\n|---|---|---|---|---|\n");
        for (int i = 0; i < G->n; i++) {
            grp* g = &G->g[i];
            P("| %s | %g | ", g->kind >= 0 && g->kind < CV_DL_N ? DL[g->kind] : "?", g->val); esc(out, ampname(d, g->amp)); P(" | %u | ", g->n); ids(out, g); P(" |\n");
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
    for (uint32_t i = 0; i < d->ntemps; i++) if (d->temps[i].step == s && !(cv->m[3] && cv->m[3][i])) gadd(G, 0, 0, 0, d->temps[i].value, d->temps[i].amp, d->temps[i].node, NULL, 1);
    for (uint32_t i = 0; i < d->nlsrc; i++) if (d->lsrc[i].kind == CV_LS_TEMP && d->temps[d->lsrc[i].first].step == s) { const cv_ntemp* b = &d->temps[d->lsrc[i].first]; gadd(G, 0, 0, 0, b->value, b->amp, 0, d->lsrc[i].name, d->lsrc[i].n); }
    if (G->n) {
        P("Prescribed temperatures (*TEMPERATURE)\n\n| Temperature | Amplitude | Nodes | Node set / node ids |\n|---|---|---|---|\n");
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
        uint32_t nn = m->eoff[e + 1] - m->eoff[e], pyr[5];
        const uint32_t* cn = m->conn + m->eoff[e];
        if (t == 1 && nn == 8 && cv_hex_pyramid(cn, pyr)) { t = CV_PYR5; cn = pyr; nn = 5; }
        if (!cv_mq_dim(t) || nn > 32 || !nn) continue;
        double x[32][3], v[CV_MQ_N];
        for (uint32_t i = 0; i < nn; i++) for (int a = 0; a < 3; a++) x[i][a] = m->xyz[3 * (size_t)cn[i] + a];
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

const char* cv_report_part_name(int i) {
    static const char* N[CV_RP_N] = { "General", "Analysis steps", "Mesh", "Node and element sets", "Mesh quality",
                                      "Materials", "Sections and orientations", "Boundary conditions and loads", "Constraints and interactions" };
    return i >= 0 && i < CV_RP_N ? N[i] : "";
}

bool cv_report_md(FILE* out, const cv_frd* f, const cv_inp* d, const cv_report_opts* o) {
    const cv_frd* m = d ? &d->mesh : f;
    if (!out || !m) return false;
    cv_report_opts z = {0};
    if (!o) o = &z;
    unsigned on = ~o->skip;
    int sec = 0;                                         /* section numbers as written: none skipped */

    P("# Model summary\n\nThe model definition only, as ccxview reads it from the files; numbers are in the units of the input.\n\n");
    /* general */
    if (on & CV_RP_GENERAL) {
    P("## %d. General\n\n| | |\n|---|---|\n", ++sec);
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
    }
    if (d && d->nsteps > 0 && (on & CV_RP_STEPS)) {
        if (on & CV_RP_GENERAL) P("Analysis steps\n\n"); else P("## %d. Analysis steps\n\n", ++sec);
        P("| Step | Procedure | Time period | NLGEOM | Initial increment | Min increment | Max increment |\n|---|---|---|---|---|---|---|\n");
        for (int s = 0; s < d->nsteps && s < CAP; s++) {
            const char* pn = d->proc ? cv_inp_proc_name(d->proc[s]) : "";
            const cv_stepinfo* si = d->stepinfo ? &d->stepinfo[s] : NULL;
            P("| %d | %s | ", s + 1, pn[0] ? pn : "-");
            if (si && si->proc[0] && si->period > 0) P("%g", si->period); else P("-");
            P(" | %s", si && si->nlgeom ? "yes" : "no");
            float tv[3] = { si ? si->inc : 0, si ? si->tmin : 0, si ? si->tmax : 0 };
            for (int k = 0; k < 3; k++) { P(" | "); if (tv[k] > 0) P("%g", tv[k]); else P("-"); }
            P(" |\n");
        }
        more(out, d->nsteps, CAP);
        P("\nSolver controls (*CONTROLS, *SOLVER), output requests, contact and other step options: not read.\n\n");
    }

    /* pictures */
    if (o->npics > 0 && o->pics) {
        P("## %d. Pictures\n\n", ++sec);
        if (o->shown && o->shown[0]) P("%s\n\n", o->shown);
        for (int i = 0; i < o->npics; i++) {
            const char* c = o->pics[i].caption ? o->pics[i].caption : "";
            P("![");
            for (; *c; c++) if (*c != ']' && *c != '\n') fputc(*c, out);
            P("](%s)\n\n", o->pics[i].file ? o->pics[i].file : "");
        }
    }

    /* mesh */
    bool sets = false, qual = false;
    sets = d && (on & CV_RP_SETS) && d->nsets > 0;
    qual = (on & CV_RP_QUALITY) && m->n_elems;
    if (((on & CV_RP_MESH) && m->n_nodes) || sets || qual) P("## %d. Mesh\n\n", ++sec);
    if (m->n_nodes && (on & CV_RP_MESH)) {
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
        for (uint32_t e = 0; e < m->n_elems; e++) { int t = cv_elem_type(m, e); if (t > 0 && t < 16) per[t]++; dim[cv_mq_dim(t)]++; }
        P("Elements by type\n\n| Type | Dimension | Elements |\n|---|---|---|\n");
        for (int t = 1; t < 16; t++) if (per[t]) {
            int dm = cv_mq_dim(t);
            P("| %s | %s | %u |\n", cv_frd_type_name(t), dm == 3 ? "3D" : dm == 2 ? "2D" : dm == 1 ? "1D" : "other", per[t]);
        }
        P("\nBy dimension: 3D %u, 2D %u, 1D %u, other %u\n\n", dim[3], dim[2], dim[1], dim[0]);
    }
    if (sets) for (int pass = 1; pass >= 0; pass--) {
        int n = 0;
        for (int i = 0; i < d->nsets; i++) n += d->sets[i].is_elem == pass;
        if (!n) continue;
        P("%s sets\n\n| Set | %s |\n|---|---|\n", pass ? "Element" : "Node", pass ? "Elements" : "Nodes");
        int k = 0;
        for (int i = 0; i < d->nsets; i++) if (d->sets[i].is_elem == pass && ++k <= CAP) { P("| "); esc(out, d->sets[i].name); P(" | %u |\n", d->sets[i].n); }
        more(out, n, CAP); P("\n");
    }
    if (qual) quality(out, m);

    if (d) {
        /* materials */
        if (d->nmats && (on & CV_RP_MATS)) {
            uint32_t* use = calloc(d->nmats, sizeof *use);
            for (uint32_t e = 0; use && e < m->n_elems; e++) if (m->emat[e] && m->emat[e] <= (uint32_t)d->nmats) use[m->emat[e] - 1]++;
            P("## %d. Materials\n\n| Material | Elastic model | Constants | First yield stress (*PLASTIC) | Elements |\n|---|---|---|---|---|\n", ++sec);
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
            P("\n");
            bool anyp = false;
            for (int i = 0; d->mprop && i < d->nmats; i++) anyp |= d->mprop[i].has != 0;
            if (anyp) {
                P("Other properties (first row of each table; \"table\": more than one row, temperature dependent)\n\n| Material | Density | Conductivity | Expansion | Expansion zero | Specific heat |\n|---|---|---|---|---|---|\n");
                for (int i = 0; i < d->nmats && i < CAP; i++) {
                    const cv_matprop* p = &d->mprop[i];
                    P("| "); esc(out, d->mats[i]); P(" |");
                    const struct { uint16_t bit; float v; } col[] = { { CV_MP_RHO, p->rho }, { CV_MP_COND, p->cond }, { CV_MP_EXP, p->alpha } };
                    for (int k = 0; k < 3; k++) {
                        if (p->has & col[k].bit) P(" %g%s |", col[k].v, p->has & (col[k].bit << 1) ? " (table)" : ""); else P(" - |");
                    }
                    if (p->has & CV_MP_ZERO) P(" %g |", p->alpha0); else P(" - |");
                    if (p->has & CV_MP_CP) P(" %g%s |", p->cp, p->has & CV_MP_CP_T ? " (table)" : ""); else P(" - |");
                    P("\n");
                }
                more(out, d->nmats, CAP); P("\n");
            }
            P("Not read: plasticity beyond the first yield stress, hardening, creep, other constitutive keywords, and conductivity / expansion terms past the first value of orthotropic tables.\n\n");
            free(use);
        }
        /* sections */
        if ((d->nsects || d->nsecinfo || d->norients) && (on & CV_RP_SECTS)) {
            P("## %d. Sections and orientations\n\n", ++sec);
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
                P("\n");
            }
            if (d->nsecinfo) {
                P("Shell, membrane and beam sections\n\n| Type | Element set | Material | Shape | Thickness | Offset | Dimensions | Direction |\n|---|---|---|---|---|---|---|---|\n");
                for (int i = 0; i < d->nsecinfo && i < CAP; i++) {
                    const cv_secinfo* q = &d->secinfo[i];
                    bool beam = q->kind[0] == 'b';
                    P("| %s | ", !strcmp(q->kind, "beamgeneral") ? "beam general" : q->kind); esc(out, q->elset); P(" | ");
                    if (q->mat[0]) esc(out, q->mat); else P("-");
                    P(" | "); if (q->shape[0]) esc(out, q->shape); else P("-");
                    P(" | ");
                    if (q->composite) P("composite: %u layers, total %g", q->nlay, q->ttot);
                    else if (!beam && q->t > 0) P("%g", q->t); else P("-");
                    P(" | "); if (q->has_off) P("%g", q->off); else P("-");
                    P(" | ");
                    if (beam && q->ndim) for (int k = 0; k < q->ndim; k++) P("%s%g", k ? ", " : "", q->dim[k]); else P("-");
                    P(" | "); if (q->has_dir) P("%g, %g, %g", q->dir[0], q->dir[1], q->dir[2]); else P("-");
                    P(" |\n");
                }
                more(out, d->nsecinfo, CAP);
                P("\nShell offset is in thicknesses. Beam dimensions are the first data line as given (RECT: a, b; CIRC: r; PIPE: r, t; BOX: a, b, t1..t4; I: l, h, b1, b2, t1, t2, t3; GENERAL: A, I11, I12, I22, It); the direction is the second line (the approximate 1-direction n1). Integration points, composite layer details, other section keywords: not read.\n\n");
            }
            if (d->norients) P("Orientations (*ORIENTATION): %d. ", d->norients);
            if (d->ntransforms) P("Transforms (*TRANSFORM): %d systems on %u nodes.", d->ntransforms, d->nnode_tr);
            if (d->norients || d->ntransforms) P("\n\n");
        }
        /* loads per step */
        if (on & CV_RP_LOADS) {
            bool any = false;
            cover cv;
            cover_init(&cv, d);
            for (int s = -1; s < d->nsteps; s++) any |= step_has(d, s);
            if (any) {
                P("## %d. Boundary conditions and loads\n\nValues as given in the deck, per step (a step keeps those of the steps before unless OP=NEW).\n\n", ++sec);
                for (int s = -1; s < d->nsteps; s++) {
                    if (!step_has(d, s)) continue;
                    if (s < 0) P("### Before the first step\n\n"); else P("### Step %d\n\n", s + 1);
                    step_loads(out, d, &cv, s);
                }
            }
            cover_free(&cv);
            if (d->namps) {
                P("### Amplitudes\n\n| Name | Definition | Points | Time |\n|---|---|---|---|\n");
                for (int i = 0; i < d->namps && i < CAP; i++) {
                    P("| "); esc(out, d->amps[i].name); P(" | "); esc(out, d->amps[i].def);
                    P(" | %u | %s |\n", d->amps[i].n, d->amps[i].total ? "total" : "step");
                }
                more(out, d->namps, CAP); P("\n");
            }
        }
        /* constraints and interactions */
        if ((d->nlinks || d->nsurfs || d->ndisc || d->npret || d->nsubs || d->cyc_n) && (on & CV_RP_CONSTR)) {
            P("## %d. Constraints and interactions\n\n", ++sec);
            if (d->nlinks) {
                static const char* LN[] = { "rigid body", "kinematic coupling", "distributing coupling", "equation / MPC", "tie", "contact pair" };
                P("| Kind | Name | Reference node | Nodes | Slave surface | Master surface | Friction |\n|---|---|---|---|---|---|---|\n");
                for (int i = 0; i < d->nlinks && i < CAP; i++) {
                    const cv_link* l = &d->links[i];
                    P("| %s | ", l->kind < 6 ? LN[l->kind] : "?"); esc(out, l->name); P(" | ");
                    if (l->ref) P("%u", l->ref); else P("-");
                    if (!l->n && l->surf[0] >= 0) P(" | via surface | ");   /* its nodes are the surface's: no count of their own */
                    else P(" | %u | ", l->n);
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
        if (d->nmchg && (on & CV_RP_CONSTR)) P("Model changes (*MODEL CHANGE): %u.\n\n", d->nmchg);
    }
    fflush(out);
    return !ferror(out);
}
