/* ui_deck.c -- the Deck window: what the input deck changes from step to step and
   ccxview does not draw as a symbol. Its steps (procedure, time, the one on screen),
   the elements *MODEL CHANGE takes out and puts back, what the global model drives
   in a submodel, and the amplitudes with their points. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include <stdarg.h>
#include <stdio.h>

enum { NL = 400, LW = 160 };
static char L[NL][LW];
static uint8_t kind[NL];                 /* 0 text, 1 a section title, 2 the step on screen, 3 dim */
static int nl;

static void line(int k, const char* fmt, ...) {
    if (nl >= NL) return;
    va_list ap;
    va_start(ap, fmt); vsnprintf(L[nl], LW, fmt, ap); va_end(ap);
    kind[nl++] = (uint8_t)k;
}

/* a *MODEL CHANGE line: what it names */
static void mchg_text(const cv_inp* d, const cv_mchange* m, char* t, size_t n) {
    if (m->link >= 0 && m->link < d->nlinks) {
        const cv_link* l = &d->links[m->link];
        snprintf(t, n, "contact pair %s / %s %s", l->surf[0] >= 0 ? d->surfs[l->surf[0]].name : "?",
                 l->surf[1] >= 0 ? d->surfs[l->surf[1]].name : "?", m->add ? "added" : "removed");
    } else if (m->set >= 0 && m->set < d->nsets) {
        snprintf(t, n, "%s %s (%u elements)", m->add ? "adds" : "removes", d->sets[m->set].name, d->sets[m->set].n);
    } else {
        snprintf(t, n, "%s element %u", m->add ? "adds" : "removes", m->elem);
    }
}

static void build(const cv_inp* d) {
    nl = 0;
    int on = deck_step();
    double ts, tt;
    deck_step_time(on, &ts, &tt);
    if (d->nsteps) line(1, "Steps");
    for (int k = 0; k < d->nsteps; k++) {
        const cv_stepinfo* si = d->stepinfo ? &d->stepinfo[k] : NULL;
        double t0 = cv_inp_step_start(d, k), per = si ? si->period : 1;
        char t[LW];
        if (per > 0) snprintf(t, sizeof t, "total time %g .. %g", t0, t0 + per); else snprintf(t, sizeof t, "takes no time");
        line(k == on ? 2 : 0, "%d  %s   %s%s", k + 1, si && si->proc[0] ? si->proc : "(no procedure)", t, k == on ? "   < on screen" : "");
        if (k == on && per > 0) line(3, "     on screen: step time %g, total time %g", ts, tt);
        for (uint32_t i = 0; i < d->nmchg; i++) {
            if (d->mchg[i].step != k) continue;
            mchg_text(d, &d->mchg[i], t, sizeof t);
            line(0, "     *MODEL CHANGE %s", t);
        }
        int nb = 0, nf = 0, g = 0;                 /* *BOUNDARY / *DSLOAD, SUBMODEL lines of this step */
        for (uint32_t i = 0; i < d->nbcs; i++) if (d->bcs[i].step == k && d->bcs[i].sub) { nb += d->bcs[i].dof_hi - d->bcs[i].dof_lo + 1; g = d->bcs[i].sub; }
        if (nb) line(0, "     %d node DOFs driven by step %d of the global model", nb, g);
        for (uint32_t i = 0; i < d->ndloads; i++) if (d->dloads[i].step == k && d->dloads[i].sub) { nf++; g = d->dloads[i].sub; }
        if (nf) line(0, "     pressure on %d faces from the stresses of global step %d", nf, g);
    }
    int64_t nrm = 0;
    deck_removed(&nrm);
    if (d->nmchg) line(3, "%lld elements out of the model in the step on screen%s", (long long)nrm, G.show_removed ? " (shown)" : "");

    if (d->nsubs) line(1, "Submodel");
    for (int i = 0; i < d->nsubs; i++) {
        const cv_submodel* sm = &d->subs[i];
        line(0, "TYPE=%s, INPUT=%s%s%s", sm->surface ? "SURFACE" : "NODE", sm->input[0] ? sm->input : "?",
             sm->gelset[0] ? ", GLOBAL ELSET=" : "", sm->gelset);
        if (sm->surface) line(3, "     %s: %d surface%s", sm->names, sm->nsurf, sm->nsurf == 1 ? "" : "s");
        else line(3, "     %s: %u nodes", sm->names, sm->nn);
    }

    if (d->namps) line(1, "Amplitudes");
    for (int i = 0; i < d->namps; i++) {
        const cv_amp* a = &d->amps[i];
        if (!a->tabular) { line(0, "%s   %s: not evaluated, its loads are shown with their own value", a->name, a->def); continue; }
        line(0, "%s   %s time, %u points", a->name, a->total ? "total" : "step", a->n);
        char t[LW];
        size_t o = 0;
        for (uint32_t k = 0; k < a->n && nl < NL - 1; k++) {
            o += (size_t)snprintf(t + o, sizeof t - o, "%s(%g, %g)", o ? "  " : "     ", a->t[k], a->v[k]);
            if (o > 48 || k + 1 == a->n) { line(3, "%s", t); o = 0; }
        }
    }
    if (!nl) line(3, "No steps, amplitudes, model changes or submodel in this deck.");
}

void window_deck(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    const cv_inp* d = deck_get();
    if (!G.show_deck || !d) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "Deck", NK_SHOWN);
    was_open = true;
    build(d);
    /* at the top left of the view, beside the panels: the model stays mostly in sight */
    float w = CV_MIN(480 * s, G.vp_w * 0.6f), h = CV_MIN(G.vp_h * 0.6f, row * (nl + 4.5f) + 20 * s);
    if (nk_begin(ctx, "Deck", nk_rect(G.vp_x + 10 * s, G.vp_y + 10 * s, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        if (d->nmchg) {
            nk_layout_row_dynamic(ctx, row, 1);
            tip(ctx, "Elements a *MODEL CHANGE took out of the model in the step on screen:\n"
                     "off, hidden (and left out of the legend's range); on, drawn as the others");
            if (nk_checkbox_label(ctx, "show removed elements", &G.show_removed)) app_groups_changed();
        }
        nk_layout_row_dynamic(ctx, row, 1);
        for (int i = 0; i < nl; i++) {
            if (kind[i] == 1 && i) { uii_hsep(ctx, s); nk_layout_row_dynamic(ctx, row, 1); }
            if (kind[i] == 1 || kind[i] == 2) nk_label_colored(ctx, L[i], NK_TEXT_LEFT, P.accent);
            else if (kind[i] == 3) nk_label_colored(ctx, L[i], NK_TEXT_LEFT, P.dim);
            else nk_label(ctx, L[i], NK_TEXT_LEFT);
        }
    }
    if (nk_window_is_hidden(ctx, "Deck")) G.show_deck = false;
    nk_end(ctx);
}
