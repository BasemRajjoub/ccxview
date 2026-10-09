/* app_shell.c -- shell section forces (shell.h) as a field of their own.

   With a deck beside the .frd that has shells, every step with STRESS gets one
   more field, SHELL: Nxx Nyy Nxy Mxx Myy Mxy Qx Qy per unit width, in the shell's
   axes (its *ORIENTATION, else the global x on it, as CalculiX's). It is listed,
   picked, plotted over the steps, exported, labelled and used in formulas
   (SHELL_MXX) as a field the .frd holds; only its decoding differs: the step's
   STRESS, turned global with the deck, integrated through the thickness. Its
   units are a force and a moment per width, converted as the units window says. */
#include "app_int.h"
#include "shell.h"
#include <math.h>

enum { SHELL_FMT = 99 };            /* cv_field_desc.fmt of the field: no bytes in the file */

static struct {
    uint32_t* shell;                /* per .frd element: its shell (deck element index), UINT32_MAX none */
    float*    q;                    /* per .frd element: the shell's axes */
    uint32_t  nshell;
} S;

void shell_clear(void) {
    free(S.shell); free(S.q);
    memset(&S, 0, sizeof S);
}

bool shell_field(const cv_field_desc* d) { return d && d->fmt == SHELL_FMT; }

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
    cv_field_desc sf;
    memset(&sf, 0, sizeof sf);
    snprintf(sf.name, sizeof sf.name, "SHELL");
    sf.ncomp = CV_SF_N;
    sf.fmt = SHELL_FMT;
    for (int c = 0; c < CV_SF_N; c++) snprintf(sf.comp[c], sizeof sf.comp[c], "%s", cv_shell_comp(c));
    for (int s = 0; s < f->n_steps; s++) {
        cv_step* st = &G.frd.steps[s];
        int fi = find_field(s, "STRESS");
        if (fi < 0 || st->fields[fi].ncomp < 6 || find_field(s, "SHELL") >= 0) continue;
        cv_field_desc* nf = realloc(st->fields, (size_t)(st->nfields + 1) * sizeof *nf);
        if (!nf) break;
        st->fields = nf;
        nf[st->nfields++] = sf;
    }
}

/* the section forces of step `step`, in file units, from its STRESS turned global */
void shell_read(int step, float* out, cv_msgs* msgs) {
    const cv_frd* f = &G.frd;
    size_t n = (size_t)f->n_nodes * CV_SF_N;
    int fi = find_field(step, "STRESS");
    const cv_field_desc* sd = fi >= 0 ? &f->steps[step].fields[fi] : NULL;
    float* s = sd && S.shell ? malloc((size_t)CV_MAX(f->n_nodes, 1) * (size_t)sd->ncomp * sizeof(float)) : NULL;
    if (!s) { for (size_t i = 0; i < n; i++) out[i] = NAN; return; }
    cv_frd_read_field(f, sd, s, msgs);
    deck_localize(step, sd, s);
    if (!cv_shell_forces(f, S.shell, S.q, S.nshell, s, sd->ncomp, out)) {
        for (size_t i = 0; i < n; i++) out[i] = NAN;
        if (msgs) cv_msg_add(msgs, 0, false, "out of memory: no shell forces");
    }
    free(s);
}
