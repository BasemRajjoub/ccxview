/* app_clayer.c -- the contact interface layer: over each slave face of the contact
   pairs active in the step, a prism (a hexahedron on a quadrilateral face, a wedge on a
   triangle; corners only) from the face up to its corners' feet on their master faces
   (app_cdisp.c, clayer.c), so that it is as thick as the gap at each corner and has no
   thickness where the contact is closed: an adhesive filling the gap. Only a picture
   of the contact, not an element of the model; nothing is picked or labelled on it.

   It follows the shape drawn: its bottom moves with the slave nodes, its top with the
   feet on the master faces (exaggerated with the shape); "at true scale" (cel_true)
   puts the top at the slave node less its true gap along the face's normal, so the
   thickness is the true gap on an exaggerated shape. Where a corner penetrates its
   master face the top lies inside the slave body: the prism turns inside out, and the
   faces with such a corner are outlined in the penetrating colour.

   Coloured, interpolated over its faces, by what the whole display is coloured by
   (G.cel_by): the gap (the gap colours), CPRESS (0 where open), |CSLIP|, |CSHEAR|
   (Turbo, over their own ends), or the status (its fixed colours, a patch per corner).
   A least thickness (cel_min) leaves a thin skin where it is closed. Those colours, for
   the links and the .cel's elements too, are set up here (app_cby_colours). */
#include "app.h"
#include "app_int.h"
#include "clayer.h"
#include <math.h>
#include <strings.h>

const char* const cv_cby_names[CV_CBY_N] = { "gap", "pressure", "slip", "shear", "status" };
const char* const cv_cdraw_names[CV_CDRAW_N] = { "layer", "links", "ccx" };

/* a name of choice i (several spellings each), or its number, among n */
static int parse_of(const char* s, const char* const (*names)[3], int n) {
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (end != s && !*end) return v >= 0 && v < n ? (int)v : -1;
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 3; k++) if (names[i][k] && !strcasecmp(s, names[i][k])) return i;
    return -1;
}

int app_cby_parse(const char* s) {
    static const char* const nm[CV_CBY_N][3] = { { "gap", "copen", NULL }, { "cpress", "pressure", NULL }, { "cslip", "slip", NULL },
                                                 { "cshear", "shear", NULL }, { "status", NULL, NULL } };
    return parse_of(s, nm, CV_CBY_N);
}

int app_cdraw_parse(const char* s) {
    static const char* const nm[CV_CDRAW_N][3] = { { "layer", NULL, NULL }, { "links", "link", NULL }, { "ccx", "solids", "elements" } };
    return parse_of(s, nm, CV_CDRAW_N);
}

/* Turbo without its darkest tenth, which a dark background swallows */
void app_cby_map(float t, float rgb[3]) { cv_colormap_rgb(CV_CMAP_TURBO, 0.1f + 0.9f * fminf(fmaxf(t, 0.f), 1.f), rgb); }

float app_cby_value(const cv_cslave* q, int by) {
    if (!q || q->st < 0) return NAN;
    switch (by) {
    case CV_CBY_GAP: return q->gap;
    case CV_CBY_CPRESS: return q->press == q->press ? fmaxf(q->press, 0.f) : NAN;   /* CalculiX writes it negative where open */
    case CV_CBY_CSLIP: return q->slip;
    case CV_CBY_CSHEAR: return q->shear;
    default: return (float)q->st;
    }
}

void app_cby_colours(cv_cinfo* I, float vmin, float vmax) {
    float rgb[3 * 256];
    I->lknown = vmin <= vmax;
    if (I->by == CV_CBY_GAP) {
        I->llo = I->lo; I->lhi = I->hi;
        cv_contact_gap_table(CV_CGAP_LINKS, I->lo, I->hi, I->tol, rgb, 256);
        cv_render_contact_map(rgb, 256);
    } else if (I->by == CV_CBY_STATUS) {
        I->llo = -0.5f; I->lhi = (float)CV_CST_N - 0.5f;
        cv_render_contact_map(&cv_cst_rgb[0][0], CV_CST_N);
    } else {
        I->llo = G.cel_lo > 0 ? G.cel_lo : 0.f;
        I->lhi = G.cel_hi > 0 ? G.cel_hi : I->lknown ? vmax : 1.f;
        if (!(I->lhi > I->llo)) I->lhi = I->llo + fmaxf(fabsf(I->llo) * 1e-3f, 1e-12f);
        for (int i = 0; i < 256; i++) app_cby_map((float)i / 255.f, rgb + 3 * i);
        cv_render_contact_map(rgb, 256);
    }
}

void app_clayer_clear(void) {
    static const int w[] = { CV_AUX_CLAY, CV_AUX_CLAYN, CV_AUX_CLAYP };
    for (size_t i = 0; i < CV_COUNT(w); i++) cv_render_aux(w[i], NULL, NULL, NULL, 0);
}

static void push(cv_fvec* p, cv_fvec* d, const cv_lpt* x) {
    if (!cv_reserve(*p, p->n + 3) || !cv_reserve(*d, d->n + 6)) return;
    for (int k = 0; k < 3; k++) p->a[p->n++] = x->p[k];
    for (int k = 0; k < 6; k++) d->a[d->n++] = x->d[k];
}

void app_clayer_draw(const uint8_t* shown, cv_cinfo* I) {
    size_t nf = 0;
    const uint32_t* sf = app_cdisp_faces(&nf);
    int by = I->by;
    bool cat = by == CV_CBY_STATUS;
    int place = G.cel_true ? CV_CLAY_TRUE : CV_CLAY_FOOT;
    float minth = G.cel_min > 0 ? G.cel_min : 0.f;
    cv_fvec tp = {0}, td = {0}, ep = {0}, ed = {0}, pp = {0}, pd = {0};
    CV_VEC(float) ts = {0};
    int npen = 0;
    for (size_t f = 0; f < nf; f++) {
        const uint32_t* c = sf + 4 * f;
        int n = c[3] == UINT32_MAX ? 3 : 4;
        bool vis = true, any = false, pen = false;
        const cv_cslave* q[4];
        for (int k = 0; k < n; k++) {
            vis = vis && (!shown || shown[c[k]]);
            q[k] = app_cdisp_slave(c[k]);
            any |= q[k] && q[k]->st >= 0;
        }
        if (!vis || !any) continue;
        cv_lpt b[4], t[4], tri[3 * CV_CLAY_MAXTRI], seg[24];
        float v[4], val[3 * CV_CLAY_MAXTRI];
        for (int k = 0; k < n; k++) {
            memcpy(b[k].p, G.frd.xyz + 3 * (size_t)c[k], sizeof b[k].p);
            app_node_disp6(c[k], b[k].d);
            const cv_cslave* s = q[k];
            bool paired = s && s->nm && s->st >= 0;
            cv_lpt foot;
            if (paired) app_cdisp_wpt(s->m, s->nm, s->pj.w, foot.p, foot.d);
            cv_clay_top(&b[k], paired ? &foot : NULL, paired ? s->pj.n : b[k].p, paired ? s->gap : NAN, place, minth, &t[k]);
            pen |= s && s->st == CV_CST_PEN;
            v[k] = app_cby_value(s, by);
        }
        int nt = cv_clay_prism(n, b, t, v, cat, tri, val);
        for (int i = 0; i < 3 * nt; i++) { push(&tp, &td, &tri[i]); cv_push(ts, val[i]); }
        if (G.cel_edges || pen) {
            int ns = cv_clay_edges(n, b, t, seg);
            for (int i = 0; i < 2 * ns; i++) push(pen ? &pp : &ep, pen ? &pd : &ed, &seg[i]);
        }
        npen += pen;
    }
    app_aux_upload(CV_AUX_CLAY, &tp, &td, ts.n ? ts.a : NULL);
    app_aux_upload(CV_AUX_CLAYN, &ep, &ed, NULL);
    app_aux_upload(CV_AUX_CLAYP, &pp, &pd, NULL);
    cv_free_vec(tp); cv_free_vec(td); cv_free_vec(ts); cv_free_vec(ep); cv_free_vec(ed); cv_free_vec(pp); cv_free_vec(pd);
    I->lpen = npen;
}
