/* app_cdisp.c -- the contact display: what each slave node is, drawn so the gap reads.

   The slave nodes: those of the .cel set drawn (paired by CalculiX with a master face)
   and those of the slave surfaces of the deck's contact pairs ticked and active in the step
   (paired here with the nearest face of the pair's master surface). Each is projected
   onto its master face on the true deformed shape (coordinates + DISP, whatever the
   deformation scale on screen), giving the foot, the face's normal there and the
   measured gap; COPEN, where the .frd has it, is the gap used (CalculiX's own, right
   also where it adjusted an initial overclosure). With CPRESS and CSHEAR and the
   pair's *FRICTION the status follows (contact.h): far or near open, sliding,
   sticking, penetrating.

   Drawn one way (G.cel_draw, CV_CDRAW_*):
     layer   a prism over each slave face up to its corners' feet on the master
             (app_clayer.c): an adhesive whose thickness is the gap.
     links   a line from each open node to its foot on the face. With the shape
             exaggerated the screen shows the initial gap plus S times the motion; "at
             true scale" puts the node at the foot plus the true gap along the normal.
     ccx     the contact elements of the .cel as CalculiX wrote them: a pyramid (or
             tetrahedron) from the slave node to its master face, a prism between the
             faces for surface to surface, see-through.
   Each coloured by one value (G.cel_by, CV_CBY_*): the gap (red in, green 0, blue
   open), CPRESS, |CSLIP|, |CSHEAR| or the status; the slave nodes drawn on top as balls
   in the same colours, filled when closed, a ring when open, as ticked. */
#include "app.h"
#include "app_int.h"
#include "contact.h"
#include "cel.h"
#include "log.h"
#include <math.h>
#include <strings.h>

typedef char cinfo_cats_fit[sizeof(((cv_cinfo*)0)->cat) / sizeof(int) == CV_CST_N ? 1 : -1];

typedef cv_cslave cnode;

static struct {
    CV_VEC(cnode) n;
    uint32_t* at; uint32_t nat;         /* per model node: its cnode, UINT32_MAX none */
    CV_VEC(uint32_t) sf;                /* the slave faces: 4 corners each (a triangle: the 4th UINT32_MAX) */
    cv_cinfo info;
} D;

const cv_cinfo* app_cdisp_info(void) { return &D.info; }

bool app_cdisp_parse_mode(const char* s, int* draw, int* by) {
    static const char* const names[5] = { "links", "status", "gap", "solids", "layer" };
    char* end = NULL;
    long v = strtol(s, &end, 10);
    int m = 0;
    if (end != s && !*end) { if (v < 0 || v > 31) return false; m = (int)v; }
    else while (*s) {
        while (*s == ',' || *s == '+' || *s == ' ') s++;
        if (!*s) break;
        size_t l = strcspn(s, ",+ ");
        int k = -1;
        for (int i = 0; i < 5; i++) if (strlen(names[i]) == l && !strncasecmp(s, names[i], l)) k = i;
        if (l == 4 && !strncasecmp(s, "none", 4)) k = 9;
        if (k < 0) return false;
        if (k < 5) m |= 1 << k;
        s += l;
    }
    /* status and gap on the slave faces are the layer coloured by them now */
    *draw = (m & 16) || (m & 6) ? CV_CDRAW_LAYER : (m & 8) ? CV_CDRAW_CCX : CV_CDRAW_LINKS;
    if (m & 2) *by = CV_CBY_STATUS;
    else if (m & 13) *by = CV_CBY_GAP;
    return true;
}

/* ---- the CONTACT field and what the display is coloured by ------------------------ */

static int comp_named(const cv_field_desc* d, const char* name) {
    for (int c = 0; c < d->ncomp; c++) if (!strcmp(d->comp[c], name)) return c;
    return -1;
}

void app_cdisp_field_chosen(const char* field, int comp) {
    if (strcmp(field, "CONTACT") || G.field_src != 0 || !G.frd.n_steps) return;
    int fi = find_field(G.step, "CONTACT"), by = -1;
    if (comp == CV_COMP_STATUS) by = CV_CBY_STATUS;
    else if (fi >= 0 && comp >= 0 && comp < G.frd.steps[G.step].fields[fi].ncomp) {
        const char* n = G.frd.steps[G.step].fields[fi].comp[comp];
        by = !strcmp(n, "COPEN") ? CV_CBY_GAP : !strcmp(n, "CPRESS") ? CV_CBY_CPRESS
           : !strncmp(n, "CSLIP", 5) ? CV_CBY_CSLIP : !strncmp(n, "CSHEAR", 6) ? CV_CBY_CSHEAR : -1;
    }
    if (by >= 0 && by != G.cel_by) { G.cel_by = by; app_contact_refresh(); }
}

int app_cdisp_by_comp(int by) {
    static const char* const comp[CV_CBY_N] = { "COPEN", "CPRESS", "CSLIP1", "CSHEAR1", NULL };
    int fi = G.frd.n_steps ? find_field(G.step, "CONTACT") : -1;
    if (fi < 0 || by < 0 || by >= CV_CBY_N) return -100;
    if (by == CV_CBY_STATUS) return CV_COMP_STATUS;
    int c = comp_named(&G.frd.steps[G.step].fields[fi], comp[by]);
    return c >= 0 ? c : -100;
}

/* ---- the slave nodes and their state ----------------------------------------------- */

/* node i on the true deformed shape */
static void true_pt(uint32_t i, float x[3]) {
    for (int k = 0; k < 3; k++) x[k] = G.frd.xyz[3 * (size_t)i + k] + (G.disp ? G.disp[3 * (size_t)i + k] : 0.f);
}

static cnode* node_add(uint32_t i, int link) {
    if (D.at[i] != UINT32_MAX) return &D.n.a[D.at[i]];
    cnode q;
    memset(&q, 0, sizeof q);
    q.node = i; q.link = link; q.st = -1;
    q.gap = q.press = q.shear = NAN; q.dist = INFINITY;
    if (!cv_push(D.n, q)) return NULL;
    D.at[i] = (uint32_t)(D.n.n - 1);
    return &D.n.a[D.n.n - 1];
}

/* q paired with the face of corners m (shown indices) when nearer than its pairing so far */
static void pair_with(cnode* q, const uint32_t* m, int nm) {
    float p[3], c[4][3];
    cv_cproj o;
    true_pt(q->node, p);
    for (int k = 0; k < nm; k++) true_pt(m[k], c[k]);
    if (!cv_contact_project(p, (const float (*)[3])c, nm, &o)) return;
    float d = sqrtf((p[0] - o.foot[0]) * (p[0] - o.foot[0]) + (p[1] - o.foot[1]) * (p[1] - o.foot[1]) + (p[2] - o.foot[2]) * (p[2] - o.foot[2]));
    if (q->nm && !(d < q->dist)) return;
    q->pj = o; q->dist = d; q->nm = (uint8_t)nm;
    memcpy(q->m, m, (size_t)nm * sizeof(uint32_t));
}

static bool to_ix(const uint32_t* id, int n, uint32_t* ix) {
    for (int j = 0; j < n; j++) if ((ix[j] = cv_frd_node_index(&G.frd, id[j])) == UINT32_MAX) return false;
    return true;
}

/* the corners c[0..n) of a face of element e turned so that their normal (right-hand
   rule) points out of the element, as CalculiX orders a master face */
static void outward(uint32_t e, uint32_t* c, int n) {
    double ec[3] = { 0, 0, 0 }, fc[3] = { 0, 0, 0 }, nn[3] = { 0, 0, 0 };
    uint32_t b = G.frd.eoff[e], m = G.frd.eoff[e + 1] - b;
    for (uint32_t j = 0; j < m; j++) for (int i = 0; i < 3; i++) ec[i] += G.frd.xyz[3 * (size_t)G.frd.conn[b + j] + i] / (double)m;
    for (int k = 0; k < n; k++) {                   /* Newell's normal */
        const float* p = G.frd.xyz + 3 * (size_t)c[k]; const float* q = G.frd.xyz + 3 * (size_t)c[(k + 1) % n];
        nn[0] += (double)(p[1] - q[1]) * (p[2] + q[2]);
        nn[1] += (double)(p[2] - q[2]) * (p[0] + q[0]);
        nn[2] += (double)(p[0] - q[0]) * (p[1] + q[1]);
        for (int i = 0; i < 3; i++) fc[i] += p[i] / (double)n;
    }
    if (nn[0] * (fc[0] - ec[0]) + nn[1] * (fc[1] - ec[1]) + nn[2] * (fc[2] - ec[2]) >= 0) return;
    uint32_t t = c[1]; c[1] = c[n - 1]; c[n - 1] = t;
}

/* the element faces of deck surface si as corners, turned outward: to v (4 each), their count */
static void surf_corners(const cv_inp* dk, int si, uint32_t** fc, uint32_t* nf) {
    CV_VEC(uint32_t) v = {0};
    const cv_surface* s = si >= 0 && si < dk->nsurfs ? &dk->surfs[si] : NULL;
    for (uint32_t j = 0; s && j < s->n; j++) {
        uint32_t e = deck_elem(&G.frd, s->elem[j]), c[4];
        int k = e == UINT32_MAX ? 0 : cv_elem_face_corners(&G.frd, e, s->face[j], c);
        if (k < 3) continue;
        outward(e, c, k);
        if (k == 3) c[3] = UINT32_MAX;
        for (int i = 0; i < 4; i++) cv_push(v, c[i]);
    }
    *fc = v.a; *nf = (uint32_t)(v.n / 4);
}

/* the nodes of link li's slave surface not paired yet: with the nearest face of its master */
static void pair_by_search(const cv_inp* dk, int li) {
    uint32_t* fc = NULL, nf = 0;
    surf_corners(dk, dk->links[li].surf[1], &fc, &nf);
    float* xyz = nf ? malloc((size_t)nf * 12 * sizeof(float)) : NULL;
    uint8_t* nc = nf ? malloc(nf) : NULL;
    if (xyz && nc) {
        for (uint32_t f = 0; f < nf; f++) {
            nc[f] = fc[4 * f + 3] == UINT32_MAX ? 3 : 4;
            for (int k = 0; k < 4; k++) true_pt(fc[4 * f + (k < nc[f] ? k : 2)], xyz + 12 * (size_t)f + 3 * k);
        }
        cv_cgrid* g = cv_cgrid_build(xyz, nc, nf);
        for (size_t j = 0; g && j < D.n.n; j++) {
            cnode* q = &D.n.a[j];
            if (q->link != li || q->nm) continue;
            float p[3];
            cv_cproj o;
            true_pt(q->node, p);
            uint32_t f = cv_cgrid_nearest(g, p, 1e30f, &o);
            if (f == UINT32_MAX) continue;
            pair_with(q, fc + 4 * (size_t)f, nc[f]);
        }
        cv_cgrid_free(g);
    }
    free(xyz); free(nc); free(fc);
}

static void build(void) {
    D.n.n = 0; D.sf.n = 0;
    memset(&D.info, 0, sizeof D.info);
    D.info.draw = -1;
    if (!G.loaded || !G.frd.n_nodes || G.stl_model) return;
    if (D.nat != G.frd.n_nodes) {
        free(D.at);
        D.at = malloc((size_t)G.frd.n_nodes * sizeof(uint32_t));
        D.nat = D.at ? G.frd.n_nodes : 0;
    }
    if (!D.at) return;
    memset(D.at, 0xff, (size_t)D.nat * sizeof(uint32_t));
    /* the deck's contact pairs active now: their slave nodes and faces */
    const cv_inp* dk = deck_get();
    const bool* lon = deck_link_flags();
    int ds = deck_step(), npair = 0, one = -1;
    for (int i = 0; dk && i < dk->nlinks; i++) {
        const cv_link* l = &dk->links[i];
        if (l->kind != CV_LINK_CONTACT || !cv_inp_link_active(dk, ds, i) || (lon && !lon[i])) continue;
        npair++; one = i;
        uint32_t n = 0;
        uint32_t* ix = app_contact_surf_nodes(l->surf[0], &n);
        for (uint32_t j = 0; j < n; j++) node_add(ix[j], i);
        free(ix);
        uint32_t* fc = NULL, nf = 0;
        surf_corners(dk, l->surf[0], &fc, &nf);
        for (uint32_t j = 0; j < 4 * nf; j++) cv_push(D.sf, fc[j]);
        free(fc);
    }
    /* the .cel's pairs */
    const cv_cel* c = app_contact_cel();
    uint32_t nu = 0;
    const uint32_t* u = c ? app_contact_cel_uniq(&nu) : NULL;
    for (uint32_t j = 0; j < nu; j++) {
        const cv_celem* e = &c->elem[u[j]];
        uint32_t mi[4], si[4];
        if (!to_ix(e->m, e->nm, mi) || !to_ix(e->s, e->ns, si)) continue;
        for (int k = 0; k < e->ns; k++) {
            cnode* q = node_add(si[k], -1);
            if (!q) continue;
            q->elem = true;
            pair_with(q, mi, e->nm);
        }
    }
    for (int i = 0; dk && i < dk->nlinks; i++)
        if (dk->links[i].kind == CV_LINK_CONTACT && cv_inp_link_active(dk, ds, i) && !(lon && !lon[i])) pair_by_search(dk, i);
    if (!D.n.n) return;
    /* the CONTACT block of the increment on screen */
    int fi = find_field(G.step, "CONTACT");
    const float* v = fi >= 0 ? cache_get(G.step, fi) : NULL;
    int nc = 0, co = -1, cp = -1, c1 = -1, c2 = -1, s1 = -1, s2 = -1;
    if (v) {
        const cv_field_desc* d = &G.frd.steps[G.step].fields[fi];
        nc = d->ncomp;
        co = comp_named(d, "COPEN"); cp = comp_named(d, "CPRESS"); c1 = comp_named(d, "CSHEAR1"); c2 = comp_named(d, "CSHEAR2");
        s1 = comp_named(d, "CSLIP1"); s2 = comp_named(d, "CSLIP2");
    }
    const float raw = units_len_raw();
    /* the size of the faces met: the automatic tolerance and near distance */
    double el = 0;
    int ne = 0;
    for (size_t j = 0; j < D.n.n; j++) {
        cnode* q = &D.n.a[j];
        for (int k = 0; k < q->nm && j < 4000; k++) {
            float a[3], b[3];
            true_pt(q->m[k], a); true_pt(q->m[(k + 1) % q->nm], b);
            el += sqrt((double)(a[0] - b[0]) * (a[0] - b[0]) + (double)(a[1] - b[1]) * (a[1] - b[1]) + (double)(a[2] - b[2]) * (a[2] - b[2]));
            ne++;
        }
    }
    float h = ne ? (float)(el / ne) : G.mean_edge;
    float tol = G.cel_tol > 0 ? G.cel_tol : 0.005f * h, near = G.cel_near > 0 ? G.cel_near : 0.1f * h;
    cv_cinfo* I = &D.info;
    I->tol = tol; I->near = near;
    I->gmin = INFINITY; I->gmax = -INFINITY;
    bool any_copen = false;
    for (size_t j = 0; j < D.n.n; j++) {
        cnode* q = &D.n.a[j];
        const float* r = v ? v + (size_t)q->node * nc : NULL;
        float copen = r && co >= 0 ? r[co] * raw : NAN;
        if (r && cp >= 0) q->press = r[cp] == r[cp] ? r[cp] : 0.f;   /* not in the block: CalculiX has it open */
        if (r && c1 >= 0 && c2 >= 0 && r[c1] == r[c1] && r[c2] == r[c2]) q->shear = sqrtf(r[c1] * r[c1] + r[c2] * r[c2]);
        if (r && s1 >= 0 && s2 >= 0 && r[s1] == r[s1] && r[s2] == r[s2]) q->slip = sqrtf(r[s1] * r[s1] + r[s2] * r[s2]) * raw;
        if (copen == copen) { q->gap = copen; any_copen = true; }
        else if (q->nm) { q->gap = q->pj.gap; I->measured = true; }
        cv_cin in = { q->gap, q->press, q->shear, 0.f, tol, near, q->elem };
        int li = q->link >= 0 ? q->link : npair == 1 ? one : -1;
        if (li >= 0) in.mu = dk->links[li].mu;
        q->st = (int8_t)cv_contact_status(&in);
        if (q->st < 0) continue;
        I->nodes++;
        I->cat[q->st]++;
        if (q->gap == q->gap) { I->gmin = fminf(I->gmin, q->gap); I->gmax = fmaxf(I->gmax, q->gap); }
    }
    I->copen = any_copen;
    I->open = I->cat[CV_CST_FAR] + I->cat[CV_CST_NEAR];
    I->closed = I->nodes - I->open;
    I->pen = I->cat[CV_CST_PEN];
    if (!(I->gmin <= I->gmax)) I->gmin = I->gmax = 0;
    I->hi = G.cel_gap_max > 0 ? G.cel_gap_max : fmaxf(I->gmax, tol);
    I->lo = -(G.cel_pen_max > 0 ? G.cel_pen_max : fmaxf(-I->gmin, tol));
    if (!(I->hi > 0)) I->hi = 1e-6f;
    if (!(I->lo < 0)) I->lo = -1e-6f;
    if (cv_log_verbose())                       /* --verbose: each slave node, to check against the files */
        for (size_t j = 0; j < D.n.n; j++) {
            const cnode* q = &D.n.a[j];
            cv_logf("contact: node %u gap %.6g measured %.6g xi %.3f eta %.3f inside %d press %.5g shear %.5g status %s",
                    G.frd.node_id[q->node], q->gap, q->nm ? q->pj.gap : NAN, q->pj.xi, q->pj.eta, q->pj.inside,
                    q->press, q->shear, q->st >= 0 ? cv_cst_names[q->st] : "-");
        }
}

/* ---- drawing ----------------------------------------------------------------------- */

typedef struct { cv_fvec p, d; CV_VEC(float) s; } vb;

static void vb_add(vb* b, const float p[3], const float d[6], float s) {
    if (!cv_reserve(b->p, b->p.n + 3) || !cv_reserve(b->d, b->d.n + 6) || !cv_push(b->s, s)) return;
    for (int k = 0; k < 3; k++) b->p.a[b->p.n++] = p[k];
    for (int k = 0; k < 6; k++) b->d.a[b->d.n++] = d[k];
}
static void vb_up(vb* b, int which) {
    app_aux_upload(which, &b->p, &b->d, b->s.n ? b->s.a : NULL);
    cv_free_vec(b->p); cv_free_vec(b->d); cv_free_vec(b->s);
}

void app_cdisp_wpt(const uint32_t* m, int n, const float* w, float p[3], float d[6]) {
    memset(p, 0, 3 * sizeof(float)); memset(d, 0, 6 * sizeof(float));
    for (int k = 0; k < n; k++) {
        if (w[k] == 0) continue;
        float dk[6];
        app_node_disp6(m[k], dk);
        for (int i = 0; i < 3; i++) p[i] += w[k] * G.frd.xyz[3 * (size_t)m[k] + i];
        for (int i = 0; i < 6; i++) d[i] += w[k] * dk[i];
    }
}

static bool exaggerated(void) {
    float s = G.deform ? G.deform_scale : 0.f;
    return G.disp && fabsf(s - 1.f) > 1e-4f;
}

/* where slave node q is drawn: the node itself, or (at true scale, the shape
   exaggerated) its foot on the face plus the true gap along the face's normal */
static void slave_at(const cnode* q, float p[3], float d[6]) {
    if (G.cel_true && q->nm && q->gap == q->gap && exaggerated()) {
        app_cdisp_wpt(q->m, q->nm, q->pj.w, p, d);
        for (int i = 0; i < 3; i++) p[i] += q->gap * q->pj.n[i];
        return;
    }
    memcpy(p, G.frd.xyz + 3 * (size_t)q->node, 3 * sizeof(float));
    app_node_disp6(q->node, d);
}

static bool is_closed(int st) { return st >= CV_CST_SLIDE; }

/* the slave nodes as balls (closed filled, open a ring) as ticked, and in links the line
   from each to its foot on the face, by the value the display is coloured by */
static void draw_nodes(const uint8_t* shown, bool links, int by) {
    vb fb = {0}, ob = {0};
    cv_fvec in = {0};
    float r = 1e-6f * G.diag;
    for (size_t j = 0; j < D.n.n; j++) {
        const cnode* q = &D.n.a[j];
        if (q->st < 0 || (shown && !shown[q->node])) continue;
        float a[3], da[6], v = app_cby_value(q, by);
        slave_at(q, a, da);
        bool closed = is_closed(q->st);
        if (closed ? G.cel_closed : G.cel_open) vb_add(closed ? &fb : &ob, a, da, v);
        if (!links || !q->nm || q->gap != q->gap) continue;
        float b[3], db[6];
        app_cdisp_wpt(q->m, q->nm, q->pj.w, b, db);
        deck_inst2(&in, a, b, r, r, v, da, db);
    }
    D.info.balls = fb.p.n || ob.p.n;
    vb_up(&fb, CV_AUX_CSLV); vb_up(&ob, CV_AUX_CSLVO);
    cv_render_inst(CV_INST_CLINK, in.a, (uint32_t)(in.n / CV_INST_FLOATS));
    cv_free_vec(in);
}

static const cnode* at_node(uint32_t i) { return i != UINT32_MAX && i < D.nat && D.at[i] != UINT32_MAX ? &D.n.a[D.at[i]] : NULL; }
const cv_cslave* app_cdisp_slave(uint32_t node) { return D.at ? at_node(node) : NULL; }
const uint32_t* app_cdisp_faces(size_t* nf) { *nf = D.sf.n / 4; return D.sf.a; }

/* the contact elements of the .cel as solids between slave and master, by the value
   (the mean of their slave nodes'; the status: their first's) */
static void draw_solids(const uint8_t* shown, int by) {
    const cv_cel* c = app_contact_cel();
    uint32_t nu = 0;
    const uint32_t* u = c ? app_contact_cel_uniq(&nu) : NULL;
    vb t = {0}, l = {0};
    for (uint32_t j = 0; j < nu; j++) {
        const cv_celem* e = &c->elem[u[j]];
        uint32_t mi[4], si[4];
        if (!to_ix(e->m, e->nm, mi) || !to_ix(e->s, e->ns, si) || (shown && !shown[si[0]])) continue;
        float mp[4][3], md[4][6], sp[4][3], sd[4][6], g = 0, first = NAN;
        int ng = 0;
        for (int k = 0; k < e->nm; k++) { memcpy(mp[k], G.frd.xyz + 3 * (size_t)mi[k], sizeof mp[k]); app_node_disp6(mi[k], md[k]); }
        for (int k = 0; k < e->ns; k++) {
            const cnode* q = at_node(si[k]);
            float v = app_cby_value(q, by);
            if (k == 0) first = v;
            if (q) { slave_at(q, sp[k], sd[k]); if (v == v) { g += v; ng++; } }
            else { memcpy(sp[k], G.frd.xyz + 3 * (size_t)si[k], sizeof sp[k]); app_node_disp6(si[k], sd[k]); }
        }
        float s = by == CV_CBY_STATUS ? first : ng ? g / (float)ng : NAN;
        int nm = e->nm, ns = e->ns;
        /* the master face, the slave face (or apex), the sides */
        for (int k = 1; k + 1 < nm; k++) { vb_add(&t, mp[0], md[0], s); vb_add(&t, mp[k], md[k], s); vb_add(&t, mp[k + 1], md[k + 1], s); }
        for (int k = 0; k < nm; k++) { vb_add(&l, mp[k], md[k], 0); vb_add(&l, mp[(k + 1) % nm], md[(k + 1) % nm], 0); }
        if (ns == 1) {
            for (int k = 0; k < nm; k++) {
                int kn = (k + 1) % nm;
                vb_add(&t, sp[0], sd[0], s); vb_add(&t, mp[k], md[k], s); vb_add(&t, mp[kn], md[kn], s);
                vb_add(&l, sp[0], sd[0], 0); vb_add(&l, mp[k], md[k], 0);
            }
        } else {
            for (int k = 1; k + 1 < ns; k++) { vb_add(&t, sp[0], sd[0], s); vb_add(&t, sp[k], sd[k], s); vb_add(&t, sp[k + 1], sd[k + 1], s); }
            for (int k = 0; k < ns; k++) { vb_add(&l, sp[k], sd[k], 0); vb_add(&l, sp[(k + 1) % ns], sd[(k + 1) % ns], 0); }
            if (ns == nm) {
                /* the slave face runs the other way round: pair each master corner with
                   the slave corner nearest it */
                int best = 0, dir = 1;
                float bd = INFINITY;
                for (int o = 0; o < ns; o++)
                    for (int dr = -1; dr <= 1; dr += 2) {
                        float dsum = 0;
                        for (int k = 0; k < nm; k++) {
                            const float* a = mp[k]; const float* b = sp[((o + dr * k) % ns + ns) % ns];
                            dsum += (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
                        }
                        if (dsum < bd) { bd = dsum; best = o; dir = dr; }
                    }
                for (int k = 0; k < nm; k++) {
                    int kn = (k + 1) % nm, a = ((best + dir * k) % ns + ns) % ns, b = ((best + dir * kn) % ns + ns) % ns;
                    vb_add(&t, mp[k], md[k], s); vb_add(&t, mp[kn], md[kn], s); vb_add(&t, sp[b], sd[b], s);
                    vb_add(&t, mp[k], md[k], s); vb_add(&t, sp[b], sd[b], s); vb_add(&t, sp[a], sd[a], s);
                    vb_add(&l, mp[k], md[k], 0); vb_add(&l, sp[a], sd[a], 0);
                }
            }
        }
    }
    vb_up(&t, CV_AUX_CSOL); vb_up(&l, CV_AUX_CSOLN);
}

void app_cdisp_clear(void) {
    static const int w[] = { CV_AUX_CSLV, CV_AUX_CSLVO, CV_AUX_CSOL, CV_AUX_CSOLN };
    for (size_t i = 0; i < CV_COUNT(w); i++) cv_render_aux(w[i], NULL, NULL, NULL, 0);
    cv_render_inst(CV_INST_CLINK, NULL, 0);
    app_clayer_clear();
    D.info.draw = -1; D.info.balls = false;
}

void app_cdisp_refresh(void) {
    app_cdisp_clear();
    build();
    if (!G.cel_show || !D.n.n) return;
    uint8_t* shown = NULL;
    if (G.vis && G.frd.n_nodes && (shown = calloc(G.frd.n_nodes, 1)))
        for (size_t i = 0; i < G.skin.n_pt; i++) shown[G.skin.pt[i]] = 1;
    cv_cinfo* I = &D.info;
    int draw = G.cel_draw >= 0 && G.cel_draw < CV_CDRAW_N ? G.cel_draw : CV_CDRAW_LAYER;
    I->by = G.cel_by >= 0 && G.cel_by < CV_CBY_N ? G.cel_by : CV_CBY_GAP;
    float vmin = INFINITY, vmax = -INFINITY;     /* the colours' range: over every slave node */
    for (size_t j = 0; j < D.n.n; j++) {
        float v = app_cby_value(&D.n.a[j], I->by);
        if (v == v) { vmin = fminf(vmin, v); vmax = fmaxf(vmax, v); }
    }
    app_cby_colours(I, vmin, vmax);
    draw_nodes(shown, draw == CV_CDRAW_LINKS, I->by);
    if (draw == CV_CDRAW_LAYER) app_clayer_draw(shown, I);
    if (draw == CV_CDRAW_CCX) draw_solids(shown, I->by);
    I->draw = draw;
    free(shown);
}

/* ---- the STATUS of the CONTACT field --------------------------------------------- */

bool app_cdisp_status_field(float* out) {
    cv_cinfo was = D.info;                      /* what is on screen stays so */
    build();
    D.info.draw = was.draw; D.info.balls = was.balls; D.info.by = was.by;
    D.info.llo = was.llo; D.info.lhi = was.lhi; D.info.lknown = was.lknown; D.info.lpen = was.lpen;
    if (!D.n.n || !D.info.nodes) return false;
    for (uint32_t i = 0; i < G.frd.n_nodes; i++) out[i] = NAN;
    for (size_t j = 0; j < D.n.n; j++) if (D.n.a[j].st >= 0) out[D.n.a[j].node] = (float)D.n.a[j].st;
    return true;
}

const char* app_cdisp_status_name(double v) {
    if (!(v == v) || v < -0.5 || v > CV_CST_N - 0.5) return NULL;
    return cv_cst_names[(int)floor(v + 0.5)];
}
