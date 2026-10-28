/* ui_info.c -- everything about the probed node and element (and the selection it
   came from), the lower part of the Inspector's Inspect tab, in groups that fold
   (open or closed kept across picks); and the About window:
   who made ccxview, its licence, what it is built with. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "app_fail.h"
#include "app_mesh.h"
#include "os.h"
#include "pickinfo.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

/* the groups, each a tree node with its "on tag" box (G.tag_groups); the field's
   components, a face or edge, failure and mesh quality open at first */
enum { GR_FACE = CV_TAG_FACE, GR_EDGE = CV_TAG_EDGE, GR_COMP = CV_TAG_COMP, GR_NODE = CV_TAG_NODE, GR_ELEM = CV_TAG_ELEM,
       GR_CHECKS = CV_TAG_CHECKS, GR_ENODES = CV_TAG_ENODES, GR_SEL = CV_TAG_SEL, GR_IP = CV_TAG_IP, GR_N = CV_TAG_N };
static const char* gr_mark[GR_N] = { "#grp face", "#grp edge", "#grp components", "#grp node", "#grp element", "#grp checks",
                                     "#grp element nodes", "#grp selection", "#grp ip" };
static int gr_open[GR_N] = { NK_MAXIMIZED, NK_MAXIMIZED, NK_MAXIMIZED, NK_MINIMIZED, NK_MINIMIZED, NK_MAXIMIZED, NK_MINIMIZED, NK_MINIMIZED,
                             NK_MAXIMIZED };

enum { NL = 128, LW = 200 };
static char L[NL][LW];
static bool head[NL];                    /* a group's title */
static bool notag[NL];                   /* a line the tag leaves out */
static unsigned char grp[NL];
static int nl, cur;                      /* cur: the group lines go to */

static void line(bool h, const char* fmt, ...) {
    if (nl >= NL) return;
    va_list ap;
    va_start(ap, fmt); vsnprintf(L[nl], LW, fmt, ap); va_end(ap);
    grp[nl] = (unsigned char)cur;
    notag[nl] = false;
    head[nl++] = h;
}

/* ids, a row at a time */
static void id_rows(const char* label, const uint32_t* ids, uint32_t n) {
    char t[LW];
    size_t o = (size_t)snprintf(t, sizeof t, "%s (%u):", label, n);
    for (uint32_t i = 0; i < n; i++) {
        if (o > 48) { line(false, "%s", t); o = (size_t)snprintf(t, sizeof t, "   "); }
        o += (size_t)snprintf(t + o, sizeof t - o, " %u", ids[i]);
    }
    line(false, "%s", t);
}

static bool in_set(const cv_set* s, uint32_t id) {        /* ids sorted */
    uint32_t lo = 0, hi = s->n;
    while (lo < hi) { uint32_t m = (lo + hi) / 2; if (s->ids[m] < id) lo = m + 1; else hi = m; }
    return lo < s->n && s->ids[lo] == id;
}

/* the node: where it is and goes, the field there, its every component, the elements on it */
static void node_lines(uint32_t n) {
    const cv_frd* f = &G.frd;
    char a[32], b[32], c[32], d[32];
    cur = GR_NODE;
    line(true, "node %u", f->node_id[n]);
    const float* x = f->xyz + 3 * n;
    line(false, "position   %.6g, %.6g, %.6g", x[0], x[1], x[2]);
    if (G.disp) {
        const float* u = G.disp + 3 * n;
        fmt_num(a, sizeof a, u[0]); fmt_num(b, sizeof b, u[1]); fmt_num(c, sizeof c, u[2]);
        fmt_num(d, sizeof d, sqrt((double)u[0] * u[0] + (double)u[1] * u[1] + (double)u[2] * u[2]));
        line(false, "displacement  %s, %s, %s", a, b, c);
        line(false, "   its size  %s", d);
        line(false, "displaced to   %.6g, %.6g, %.6g", x[0] + u[0], x[1] + u[1], x[2] + u[2]);
    }
    if (G.has_field) {
        fmt_num(a, sizeof a, G.elem_mode ? NAN : G.scalar[n]);
        line(false, "%s = %s%s", G.field_label, a, G.elem_mode ? "  (shown per element, below)" : "");
        if (G.probe_ip) { fmt_num(a, sizeof a, G.probe_value); line(false, "integration point %d: %s", G.probe_ip, a); }
    }
    char names[CV_MAX_COMP][12];
    float v[CV_MAX_COMP];
    int k = app_field_comps(n, names, v, CV_MAX_COMP);
    if (k) {
        cur = GR_COMP;
        line(true, "field components: %s (global, input units)", G.field_name);
        char t[LW];
        size_t o = 0;
        t[0] = 0;
        for (int i = 0; i < k; i++) {
            fmt_num(a, sizeof a, v[i]);
            o += (size_t)snprintf(t + o, sizeof t - o, "   %s %s", names[i], a);
            if (i % 2 == 1 || i == k - 1) { line(false, "%s", t); o = 0; t[0] = 0; }
        }
        cur = GR_NODE;
    }
    static uint32_t around_for = UINT32_MAX, around[64], na;    /* elements on the node: a scan, so kept */
    static const cv_frd* around_f;
    if (around_for != n || around_f != f) {
        around_for = n; around_f = f; na = 0;
        for (uint32_t q = 0; q < f->n_elems && na < 64; q++)
            for (uint32_t j = f->eoff[q]; j < f->eoff[q + 1]; j++) if (f->conn[j] == n) { around[na++] = f->elem_id[q]; break; }
    }
    id_rows("elements on it", around, na);

}

/* the element: type, material, sets, nodes (buttons), its value, failure, mesh quality */
static void elem_lines(uint32_t n, uint32_t e) {
    const cv_frd* f = &G.frd;
    char a[32];
    cur = GR_ELEM;
    line(true, "element %u   %s", f->elem_id[e], cv_frd_type_name(f->etype[e]));
    const char* mat = deck_material_name(f->emat[e]);
    line(false, "material %u%s%s   group %u", f->emat[e], mat ? "  " : "", mat ? mat : "", f->egrp[e]);
    const cv_inp* dk = deck_get();
    if (dk) {
        char t[LW];
        size_t o = (size_t)snprintf(t, sizeof t, "element sets:");
        int ns = 0;
        for (int i = 0; i < dk->nsets && o < sizeof t - 70; i++)
            if (dk->sets[i].is_elem && in_set(&dk->sets[i], f->elem_id[e])) { o += (size_t)snprintf(t + o, sizeof t - o, " %s", dk->sets[i].name); ns++; }
        line(false, "%s", ns ? t : "element sets: none");
    }
    uint32_t ids[32], m = CV_MIN(f->eoff[e + 1] - f->eoff[e], 32u);
    for (uint32_t j = 0; j < m; j++) ids[j] = f->node_id[f->conn[f->eoff[e] + j]];
    cur = GR_ENODES;
    line(true, "element nodes (%u): a click probes one", m);
    id_rows("nodes", ids, m);
    cur = GR_ELEM;
    if (G.has_field && G.elem_mode) { fmt_num(a, sizeof a, G.elem_val[e]); line(false, "%s (element) = %s", G.field_label, a); }
    char fl[LW], ml[LW];                              /* the checks, a group when one applies */
    bool fo = G.has_field && fail_probe_text(n, e, fl, sizeof fl), mo = G.has_field && mesh_probe_text(e, ml, sizeof ml);
    if (!fo && !mo) return;
    cur = GR_CHECKS;
    line(true, "%s", fo && mo ? "failure and mesh quality" : fo ? "failure" : "mesh quality");
    if (fo) line(false, "failure   %s", fl);
    if (mo) line(false, "mesh   %s", ml);

}

/* the face picked: which, the surfaces it is on, area, normal, the field over it */
static void face_lines(uint32_t e, int face) {
    const cv_frd* f = &G.frd;
    char a[32];
    uint32_t r[8];
    int n = app_pick_nodes_of(CV_PK_FACE, r);
    cur = GR_FACE;
    line(true, "face S%d of element %u", face + 1, f->elem_id[e]);
    const cv_inp* dk = deck_get();
    if (dk) {
        char t[LW];
        size_t o = (size_t)snprintf(t, sizeof t, "surfaces:");
        int ns = 0;
        for (int i = 0; i < dk->nsurfs && o < sizeof t - 70; i++) {
            const cv_surface* sf = &dk->surfs[i];
            bool on = false;
            for (uint32_t k = 0; k < sf->n && !on; k++) on = sf->elem[k] == f->elem_id[e] && sf->face[k] == face;
            if (on) { o += (size_t)snprintf(t + o, sizeof t - o, " %s", sf->name); ns++; }
        }
        line(false, "%s", ns ? t : "surfaces: none");
    }
    float p[24], nrm[3], fc[3] = { 0, 0, 0 }, ec[3] = { 0, 0, 0 };
    for (int i = 0; i < n; i++) for (int k = 0; k < 3; k++) { p[3 * i + k] = f->xyz[3 * r[i] + k]; fc[k] += p[3 * i + k] / n; }
    fmt_num(a, sizeof a, cv_poly_area(p, n, nrm));
    line(false, "area %s (undeformed)", a);
    bool solid = cv_elem_nfaces(f, e) > 1;            /* a solid's face: the normal out of it */
    uint32_t ne = f->eoff[e + 1] - f->eoff[e];
    for (uint32_t j = 0; j < ne; j++) for (int k = 0; k < 3; k++) ec[k] += f->xyz[3 * f->conn[f->eoff[e] + j] + k] / ne;
    if (solid && (fc[0] - ec[0]) * nrm[0] + (fc[1] - ec[1]) * nrm[1] + (fc[2] - ec[2]) * nrm[2] < 0)
        for (int k = 0; k < 3; k++) nrm[k] = -nrm[k];
    for (int k = 0; k < 3; k++) if (fabsf(nrm[k]) < 5e-4f) nrm[k] = 0.f;     /* no "-0.000" */
    line(false, "%s  %.3f, %.3f, %.3f", solid ? "outward normal" : "normal", nrm[0], nrm[1], nrm[2]);
    if (G.has_field) {
        fmt_num(a, sizeof a, app_pick_average_of(CV_PK_FACE));
        line(false, "%s average %s", G.field_label, a);
        line(false, "   %s", G.elem_mode ? "the element's value" : "over the face's nodes");
    }
    uint32_t ids[8];
    for (int i = 0; i < n; i++) ids[i] = f->node_id[r[i]];
    id_rows("its nodes", ids, (uint32_t)n);
}

/* the edge picked: its nodes, length, the field at its ends and along it */
static void edge_lines(void) {
    const cv_frd* f = &G.frd;
    char a[32], b[32];
    uint32_t r[8];
    int n = app_pick_nodes_of(CV_PK_EDGE, r);
    cur = GR_EDGE;
    if (n == 3) line(true, "edge %u - %u - %u", f->node_id[r[0]], f->node_id[r[1]], f->node_id[r[2]]);
    else line(true, "edge %u - %u", f->node_id[r[0]], f->node_id[r[1]]);
    double len = 0;
    for (int i = 0; i + 1 < n; i++) {
        double s = 0;
        for (int k = 0; k < 3; k++) { double d = (double)f->xyz[3 * r[i + 1] + k] - f->xyz[3 * r[i] + k]; s += d * d; }
        len += sqrt(s);
    }
    fmt_num(a, sizeof a, len);
    line(false, "length %s (undeformed%s)", a, n == 3 ? ", through its mid node" : "");
    if (G.has_field && G.scalar && !G.elem_mode) {
        fmt_num(a, sizeof a, G.scalar[r[0]]); fmt_num(b, sizeof b, G.scalar[r[n - 1]]);
        line(false, "%s at the ends %s, %s", G.field_label, a, b);
    }
    if (G.has_field) { fmt_num(a, sizeof a, app_pick_average_of(CV_PK_EDGE)); line(false, "average %s%s", a, G.elem_mode ? " (the element's value)" : ""); }
}

/* the selection's totals, two lines a kind selected: how many and their size (undeformed),
   the field's min, max and average over them (weighted by the size) */
static void kind_totals(void) {
    static const char* what[CV_PK_N] = { "nodes", "elements", "faces", "edges" };
    static const char* size[CV_PK_N] = { "", "volume", "area", "length" };
    static const char* by[CV_PK_N] = { "", " by volume", " by area", " by length" };
    const cv_selstat* st = app_selstats();
    char a[32], b[32], c[32];
    for (int k = 0; k < CV_PK_N; k++) {
        if (!st[k].n) continue;
        fmt_num(a, sizeof a, st[k].size);
        if (k == CV_PK_NODE) line(false, "%u nodes", st[k].n);
        else line(false, "%u %s, %s %s (undeformed)", st[k].n, what[k], size[k], a);
        if (!st[k].nval) continue;
        fmt_num(a, sizeof a, st[k].vmin); fmt_num(b, sizeof b, st[k].vmax); fmt_num(c, sizeof c, st[k].avg);
        line(false, "   min %s, max %s, average%s %s", a, b, by[k], c);
    }
}

/* the selection the probe came from */
static void sel_lines(void) {
    const cv_frd* f = &G.frd;
    char a[32], b[32];
    cur = GR_SEL;
    if (app_sel_any()) {
        if (G.self_n || G.sele_n) line(true, "selection: %u el, %u nodes, %u faces, %u edges", G.sel_n, G.seln_n, G.self_n, G.sele_n);
        else line(true, "selection: %u elements, %u nodes", G.sel_n, G.seln_n);
        if (G.sel_inside) {
            line(false, "a box, %s", G.sel_crossing ? "crossing: elements with any node inside" : "window: elements wholly inside");
            notag[nl - 1] = true;
        }
        kind_totals();
        if (G.boxq.on && G.boxq.gen == G.field_gen) {
            const uint32_t* id = G.boxq.elem ? f->elem_id : f->node_id;
            const char* what = G.boxq.elem ? "element" : "node";
            fmt_num(a, sizeof a, G.boxq.vmax); fmt_num(b, sizeof b, G.boxq.vmin);
            line(false, "max %s at %s %u", a, what, id[G.boxq.max_at]);
            line(false, "min %s at %s %u", b, what, id[G.boxq.min_at]);
            line(false, "over %u %s", G.boxq.n, G.boxq.elem ? "elements" : "nodes");
        }
    }
}

/* the probed element's integration points from the .dat (a table in the tab, lines for
   the tag and the clipboard): every component of the .dat field shown, else of the
   first, von Mises for a stress; the point nearest the click marked */
static cv_datip TB;
static const cv_dat_block* TBb;

static int ip_mark(void) { return G.probe_dip ? G.probe_dip : G.probe_ip; }

static void ip_lines(uint32_t e) {
    TBb = gp_table(e, &TB);
    if (!TBb) return;
    char t[LW], a[32];
    cur = GR_IP;
    line(true, "integration points (.dat): %s", TBb->name);
    const char* u = app_unit(TBb->name, 0);
    size_t o = (size_t)snprintf(t, sizeof t, "  ip");
    for (int c = 0; c < TB.ncomp; c++) o += (size_t)snprintf(t + o, sizeof t - o, " %10s", TBb->comp[c]);
    if (TB.mises) o += (size_t)snprintf(t + o, sizeof t - o, " %10s", "mises");
    if (*u) snprintf(t + o, sizeof t - o, "  [%s]", u);
    line(false, "%s", t);
    for (int r = 0; r < TB.n; r++) {
        o = (size_t)snprintf(t, sizeof t, "%s%3d", TB.ip[r] == ip_mark() ? "*" : " ", TB.ip[r]);
        for (int c = 0; c < TB.ncomp + TB.mises; c++) {
            fmt_num(a, sizeof a, c < TB.ncomp ? TB.v[r][c] : TB.vm[r]);
            o += (size_t)snprintf(t + o, sizeof t - o, " %10s", a);
        }
        line(false, "%s", t);
    }
    if (ip_mark()) { line(false, "* nearest the click"); notag[nl - 1] = true; }
}

/* the lines, by the kinds a click picks: a face, an edge, the node before the element
   when nodes are picked */
static void build(void) {
    const cv_pick* p = &G.probe;
    nl = 0;
    if (app_pk(CV_PK_FACE) && G.probe_face != UINT32_MAX) face_lines(G.probe_face >> 3, (int)(G.probe_face & 7));
    if (app_pk(CV_PK_EDGE) && G.probe_edge[0] != UINT32_MAX) edge_lines();
    if (app_pk(CV_PK_NODE)) { node_lines(p->node); elem_lines(p->node, p->elem); }
    else { elem_lines(p->node, p->elem); node_lines(p->node); }
    ip_lines(p->elem);
    sel_lines();
}

/* the probe's node moved to another of its element's nodes (index into conn) */
static void probe_node(uint32_t n) {
    G.probe.node = n; G.probe_ip = 0;
    G.probe_value = !G.has_field ? NAN : G.elem_mode || G.field_src == 1 ? G.elem_val[G.probe.elem] : G.scalar[n];
}

/* the groups in their order: what is picked first */
static int order(int out[GR_N]) {
    int n = 0;
    bool nd = app_pk(CV_PK_NODE);
    out[n++] = GR_FACE; out[n++] = GR_EDGE;           /* when ticked and picked: build() has their lines */
    out[n++] = GR_COMP;
    if (nd) out[n++] = GR_NODE;
    out[n++] = GR_ELEM; out[n++] = GR_CHECKS; out[n++] = GR_ENODES; out[n++] = GR_IP;
    if (!nd) out[n++] = GR_NODE;
    out[n++] = GR_SEL;
    return n;
}

/* the element's nodes as buttons, a click probes that node */
static void node_buttons(struct nk_context* ctx, float row) {
    const cv_frd* f = &G.frd;
    uint32_t e = G.probe.elem, a = f->eoff[e], m = f->eoff[e + 1] - a;
    char t[24], mk[24];
    nk_layout_row_dynamic(ctx, row, 5);
    for (uint32_t j = 0; j < m; j++) {
        uint32_t n = f->conn[a + j];
        snprintf(t, sizeof t, "%u", f->node_id[n]);
        snprintf(mk, sizeof mk, "#elem node %u", j + 1);
        uii_test_mark(ctx, mk);
        bool here = n == G.probe.node;
        if (here) nk_style_push_color(ctx, &ctx->style.button.text_normal, P.accent);
        if (nk_button_label(ctx, t)) probe_node(n);
        if (here) nk_style_pop_color(ctx);
    }
}

/* a number short: "-1.866e9" for "-1.866e+09" */
static void short_num(char* out, size_t n, double v) {
    char t[32], *e;
    fmt_num(t, sizeof t, v);
    if ((e = strchr(t, 'e'))) {
        char* q = e + 1;
        bool neg = *q == '-';
        if (*q == '+' || *q == '-') q++;
        while (*q == '0' && q[1]) q++;
        snprintf(e + 1, sizeof t - (size_t)(e + 1 - t), "%s%s", neg ? "-" : "", q);
    }
    snprintf(out, n, "%s", t);
}

/* the table: as many component columns as fit the tab, the rest below; then a button
   that shows the field on the model */
static void ip_table(struct nk_context* ctx, float s, float row) {
    const struct nk_user_font* f = ctx->style.font;
    int nc = TB.ncomp + TB.mises, mk = ip_mark();
    char a[32];
    float cw = 0;
    for (int c = 0; c < nc; c++) {
        const char* h = c < TB.ncomp ? TBb->comp[c] : "mises";
        cw = CV_MAX(cw, f->width(f->userdata, f->height, h, (int)strlen(h)));
        for (int r = 0; r < TB.n; r++) {
            short_num(a, sizeof a, c < TB.ncomp ? TB.v[r][c] : TB.vm[r]);
            cw = CV_MAX(cw, f->width(f->userdata, f->height, a, (int)strlen(a)));
        }
    }
    cw += 10 * s;
    float iw = f->width(f->userdata, f->height, "ip 88", 5) + 4 * s;
    float avail = ctx->current->layout->bounds.w - 2 * s;
    int per = CV_MAX(1, (int)((avail - iw) / cw));
    for (int c0 = 0; c0 < nc; c0 += per) {
        int k = CV_MIN(per, nc - c0);
        nk_layout_row_begin(ctx, NK_STATIC, row, k + 1);
        nk_layout_row_push(ctx, iw);
        nk_label_colored(ctx, "ip", NK_TEXT_LEFT, P.dim);
        for (int c = c0; c < c0 + k; c++) {
            nk_layout_row_push(ctx, cw);
            nk_label_colored(ctx, c < TB.ncomp ? TBb->comp[c] : "mises", NK_TEXT_RIGHT, P.dim);
        }
        nk_layout_row_end(ctx);
        for (int r = 0; r < TB.n; r++) {
            struct nk_color col = TB.ip[r] == mk ? P.accent : ctx->style.text.color;
            nk_layout_row_begin(ctx, NK_STATIC, row, k + 1);
            nk_layout_row_push(ctx, iw);
            snprintf(a, sizeof a, "%s%d", TB.ip[r] == mk ? "* " : "", TB.ip[r]);
            nk_label_colored(ctx, a, NK_TEXT_LEFT, col);
            for (int c = c0; c < c0 + k; c++) {
                nk_layout_row_push(ctx, cw);
                short_num(a, sizeof a, c < TB.ncomp ? TB.v[r][c] : TB.vm[r]);
                nk_label_colored(ctx, a, NK_TEXT_RIGHT, col);
            }
            nk_layout_row_end(ctx);
        }
    }
    const char* u = app_unit(TBb->name, 0);
    char t[96];
    snprintf(t, sizeof t, "%s%s%s%s", *u ? "in " : "", u, *u && mk ? ";  " : "", mk ? "* nearest the click" : "");
    nk_layout_row_dynamic(ctx, row, 2);
    nk_label_colored(ctx, t, NK_TEXT_LEFT, P.dim);
    tip(ctx, "Colour the Gauss points by this .dat field (Fields > .dat), its von Mises for a stress");
    if (nk_button_label(ctx, "show on model")) {
        bool on = G.field_src == 1 && !strcmp(G.field_name, TBb->name);
        app_pick_field(TBb->name, on ? G.comp : TB.mises ? CV_COMP_MISES : 0, 1);
        G.show_gp = true;
        app_gauss_changed();
    }
}

/* a tree node a group, its title the group's first line; the element's nodes as buttons */
void uii_details_body(struct nk_context* ctx, float s, float row) {
    (void)s;
    build();
    int ord[GR_N], no = order(ord);
    for (int o = 0; o < no; o++) {
        int g = ord[o], h = -1;
        for (int i = 0; i < nl && h < 0; i++) if (grp[i] == g && head[i]) h = i;
        if (h < 0) continue;
        uii_test_mark_row(ctx, gr_mark[g]);
        bool on = G.tag_groups >> g & 1;
        bool open = uii_node_check(ctx, L[h], &gr_open[g], &on, "on tag", "Show this group's lines on the tag in the view");
        G.tag_groups = on ? G.tag_groups | 1 << g : G.tag_groups & ~(1 << g);
        if (!open) continue;
        if (g == GR_ENODES) node_buttons(ctx, row);
        else if (g == GR_IP) ip_table(ctx, s, row);
        else {
            nk_layout_row_dynamic(ctx, row, 1);
            for (int i = 0; i < nl; i++) if (grp[i] == g && !head[i]) nk_label(ctx, L[i], NK_TEXT_LEFT);
        }
        uii_node_pop(ctx);
    }
}

/* the tag's lines below its head: the groups ticked "on tag", in the tab's order,
   each under its title (hd) but a face's, an edge's (the tag's head says it), the
   checks' and the element nodes' (their lines do); max of them */
int uii_details_tag(const char* out[], bool hd[], int max) {
    if (!G.tag_groups || max <= 0) return 0;
    build();
    int ord[GR_N], no = order(ord), k = 0;
    for (int o = 0; o < no && k < max; o++) {
        int g = ord[o];
        if (!(G.tag_groups >> g & 1)) continue;
        for (int i = 0; i < nl && k < max; i++) {
            if (grp[i] != g || notag[i] || (head[i] && (g == GR_FACE || g == GR_EDGE || g == GR_CHECKS || g == GR_ENODES))) continue;
            hd[k] = head[i];
            out[k++] = L[i];
        }
    }
    return k;
}

/* every line to the clipboard, folded or not, a group at a time */
void uii_details_copy(void) {
    static char txt[NL * LW];
    size_t o = 0;
    build();
    int ord[GR_N], no = order(ord);
    for (int k = 0; k < no; k++)
        for (int i = 0; i < nl && o < sizeof txt - LW - 2; i++)
            if (grp[i] == ord[k] && !(ord[k] == GR_ENODES && head[i])) o += (size_t)snprintf(txt + o, sizeof txt - o, "%s\n", L[i]);
    sapp_set_clipboard_string(txt);
}

/* ---- About ------------------------------------------------------------------------- */

static const struct { const char *name, *by, *licence, *url; } credits[] = {
    { "sokol",            "Andre Weissflog",                 "zlib",        "https://github.com/floooh/sokol" },
    { "Nuklear",          "Micha Mettke and contributors",   "MIT / PD",    "https://github.com/Immediate-Mode-UI/Nuklear" },
    { "stb_image_write, stb_sprintf", "Sean Barrett",        "MIT / PD",    "https://github.com/nothings/stb" },
    { "tinyfiledialogs",  "Guillaume Vareille",              "zlib",        "https://sourceforge.net/projects/tinyfiledialogs/" },
    { "minih264, minimp4", "Lieff",                          "CC0",         "https://github.com/lieff/minih264" },
    { "TinyExpr",         "Lewis Van Winkle",                "zlib",        "https://github.com/codeplea/tinyexpr" },
    { "miniz",            "Rich Geldreich and contributors", "MIT",         "https://github.com/richgel999/miniz" },
    { "Inter (font)",     "Rasmus Andersson",                "OFL 1.1",     "https://github.com/rsms/inter" },
    { "Noto Sans Math (font)", "Google, Noto project",       "OFL 1.1",     "https://github.com/notofonts/math" },
    { "Lucide (icons)",   "Lucide contributors, Feather",    "ISC",         "https://github.com/lucide-icons/lucide" },
    { "ProggyClean (font)", "Tristan Grimmer",               "MIT",         "https://github.com/bluescan/proggyfonts" },
};

static void link_button(struct nk_context* ctx, const char* label, const char* url) {
    tip(ctx, url);
    if (nk_button_label(ctx, label) && !cv_open_url(url))
        cv_msg_add(&G.msgs, 0, false, "could not open the browser");
}

void window_about(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    if (!G.show_about) { was_open = false; return; }
    if (!was_open) nk_window_show(ctx, "About", NK_SHOWN);
    was_open = true;
    float w = CV_MIN(560 * s, fw * 0.9f), h = CV_MIN(fh * 0.85f, row * 24);
    if (nk_begin(ctx, "About", nk_rect((fw - w) / 2, (fh - h) / 2, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        char t[160];
        nk_layout_row_dynamic(ctx, row, 1);
        snprintf(t, sizeof t, "ccxview %s", app_version());
        nk_label_colored(ctx, t, NK_TEXT_LEFT, P.accent);
        nk_label(ctx, "A fast viewer for CalculiX results and models.", NK_TEXT_LEFT);
        nk_label_colored(ctx, "Free software under the GPL, version 3 or later.", NK_TEXT_LEFT, P.dim);
        uii_hsep(ctx, s);
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label(ctx, "Made by Basem Rajjoub, structural engineer and researcher", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, row, 3);
        link_button(ctx, "website", "https://basemrajjoub.com/");
        link_button(ctx, "LinkedIn", "https://www.linkedin.com/in/rajjoub/");
        link_button(ctx, "GitHub", "https://github.com/BasemRajjoub");
        nk_layout_row_dynamic(ctx, row, 2);
        link_button(ctx, "ccxview on GitHub", "https://github.com/BasemRajjoub/ccxview");
        link_button(ctx, "report a problem", "https://github.com/BasemRajjoub/ccxview/issues");
        uii_hsep(ctx, s);
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "Built with, and thanks to their authors:", NK_TEXT_LEFT, P.dim);
        const float cols[] = { 0.38f, 0.42f, 0.2f };
        for (size_t i = 0; i < CV_COUNT(credits); i++) {
            nk_layout_row(ctx, NK_DYNAMIC, row, 3, cols);
            link_button(ctx, credits[i].name, credits[i].url);
            nk_label(ctx, credits[i].by, NK_TEXT_LEFT);
            nk_label_colored(ctx, credits[i].licence, NK_TEXT_LEFT, P.dim);
        }
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "Their licence texts ship in the licenses folder.", NK_TEXT_LEFT, P.dim);
    }
    if (nk_window_is_hidden(ctx, "About")) G.show_about = false;
    nk_end(ctx);
}
