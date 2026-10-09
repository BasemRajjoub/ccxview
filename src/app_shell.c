/* app_shell.c -- shell section forces (shell.h) as a field of their own.

   With a deck beside the .frd that has shells, every step with STRESS gets one
   more field, SHELL: Nxx Nyy Nxy Mxx Myy Mxy Qx Qy per unit width, in the shell's
   axes (its *ORIENTATION, else the global x on it, as CalculiX's). It is listed,
   picked, plotted over the steps, exported, labelled and used in formulas
   (SHELL_MXX) as a field the .frd holds; only its decoding differs: the step's
   STRESS, turned global with the deck, integrated through the thickness. Its
   units are a force and a moment per width, converted as the units window says.
   The moments are about each shell's reference surface (*SHELL SECTION OFFSET).

   Beside it REBAR: the reinforcement a concrete shell needs (rebar.h), from the
   forces about the middle of the section and its thickness, with the design
   values of G (the Reinforcement window); As_x_top .. As_total an area per width,
   Conc_ratio and Crushing plain numbers. */
#include "app_int.h"
#include "shell.h"
#include "rebar.h"
#include <math.h>

enum { SHELL_FMT = 99, REBAR_FMT = 98 };   /* cv_field_desc.fmt of the fields: no bytes in the file */

static struct {
    uint32_t* shell;                /* per .frd element: its shell (deck element index), UINT32_MAX none */
    float*    q;                    /* per .frd element: the shell's axes */
    float*    off;                  /* per shell: its OFFSET in thicknesses, NULL none */
    uint32_t  nshell;
} S;

void shell_clear(void) {
    free(S.shell); free(S.q); free(S.off);
    memset(&S, 0, sizeof S);
}

bool shell_field(const cv_field_desc* d) { return d && (d->fmt == SHELL_FMT || d->fmt == REBAR_FMT); }

void shell_attach(void) {
    shell_clear();
    const cv_inp* d = deck_get();
    const cv_frd* f = &G.frd;
    if (!d || !d->nshells || !f->n_steps || !f->n_elems) return;
    const cv_elemmap* m = deck_elemmap(f);
    if (!m) return;
    S.shell = malloc(f->n_elems * sizeof(uint32_t));
    S.q = malloc((size_t)f->n_elems * 9 * sizeof(float));
    if (!S.shell || !S.q) { shell_clear(); return; }
    uint32_t any = 0;
    for (uint32_t e = 0; e < f->n_elems; e++) {
        double Q[3][3];
        S.shell[e] = UINT32_MAX;
        int t = f->etype[e];
        if ((t == 1 || t == 2 || t == 4 || t == 5) && cv_elemmap_shell(m, d, e, &S.shell[e], Q)) {
            for (int k = 0; k < 9; k++) S.q[9 * (size_t)e + k] = (float)Q[k / 3][k % 3];
            any++;
        } else S.shell[e] = UINT32_MAX;
    }
    S.nshell = d->mesh.n_elems;
    if (!any) { shell_clear(); return; }
    if (d->shell_off && (S.off = malloc(S.nshell * sizeof(float))))
        memcpy(S.off, d->shell_off, S.nshell * sizeof(float));
    cv_field_desc sf, rf;
    memset(&sf, 0, sizeof sf);
    snprintf(sf.name, sizeof sf.name, "SHELL");
    sf.ncomp = CV_SF_N;
    sf.fmt = SHELL_FMT;
    for (int c = 0; c < CV_SF_N; c++) snprintf(sf.comp[c], sizeof sf.comp[c], "%s", cv_shell_comp(c));
    rf = sf;
    snprintf(rf.name, sizeof rf.name, "REBAR");
    rf.ncomp = CV_RB_N;
    rf.fmt = REBAR_FMT;
    for (int c = 0; c < CV_RB_N; c++) snprintf(rf.comp[c], sizeof rf.comp[c], "%s", cv_rebar_comp(c));
    for (int s = 0; s < f->n_steps; s++) {
        cv_step* st = &G.frd.steps[s];
        int fi = find_field(s, "STRESS");
        if (fi < 0 || st->fields[fi].ncomp < 6 || find_field(s, "SHELL") >= 0) continue;
        cv_field_desc* nf = realloc(st->fields, (size_t)(st->nfields + 2) * sizeof *nf);
        if (!nf) break;
        st->fields = nf;
        nf[st->nfields++] = sf;
        nf[st->nfields++] = rf;
    }
}

/* the section forces (SHELL) or the reinforcement (REBAR) of step `step`, in file
   units, from its STRESS turned global */
void shell_read(int step, const cv_field_desc* d, float* out, cv_msgs* msgs) {
    const cv_frd* f = &G.frd;
    bool rebar = d->fmt == REBAR_FMT;
    size_t nn = CV_MAX(f->n_nodes, 1), n = (size_t)f->n_nodes * (rebar ? CV_RB_N : CV_SF_N);
    int fi = find_field(step, "STRESS");
    const cv_field_desc* sd = fi >= 0 ? &f->steps[step].fields[fi] : NULL;
    float* s = sd && S.shell ? malloc(nn * (size_t)sd->ncomp * sizeof(float)) : NULL;
    float* sf = rebar ? malloc(nn * (CV_SF_N + 1) * sizeof(float)) : out;   /* the forces, then the thickness */
    bool ok = s && sf;
    if (ok) {
        cv_frd_read_field(f, sd, s, msgs);
        deck_localize(step, sd, s);
        ok = cv_shell_forces_ref(f, S.shell, S.q, S.nshell, rebar ? NULL : S.off, s, sd->ncomp, sf, rebar ? sf + nn * CV_SF_N : NULL);
        if (!ok && msgs) cv_msg_add(msgs, 0, false, "out of memory: no shell forces");
    }
    if (ok && rebar) {
        const cv_rebar_par p = { G.rebar_fcd, G.rebar_fyd, G.rebar_cover };
        cv_rebar_field(sf, sf + nn * CV_SF_N, f->n_nodes, &p, out);
    }
    if (!ok) for (size_t i = 0; i < n; i++) out[i] = NAN;
    free(s);
    if (rebar) free(sf);
}

void app_rebar_changed(void) {
    if (!G.loaded) return;
    cache_clear();
    G.hist_key[0] = 0;
    app_set_step(G.step);                  /* decodes again: field, plots */
}
