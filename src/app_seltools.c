/* app_seltools.c -- the ways to select besides the box (app.h): a click on an
   element or node, inverting, elements to nodes and back, deck sets, surfaces,
   element types and materials by name, an id list, and --select. Each goes in
   through app_sel_apply (app_select.c), which does the rest. */
#include "app_int.h"
#include "idlist.h"
#include <math.h>
#include <stdio.h>
#include <strings.h>

static bool shown(uint32_t e) { return !G.vis || G.vis[e]; }

static void say(const char* fmt, const char* what) {
    snprintf(G.sel_note, sizeof G.sel_note, fmt, what ? what : "");
}

/* a candidate given both ways (either may be NULL): only what is shown, then the
   kinds in which by the mode. el / nd are not taken over. */
static bool take_which(const uint32_t* el, uint32_t ne, const uint32_t* nd, uint32_t nn, int mode, int which) {
    uint8_t* sn = cv_sel_shown_nodes(&G.frd, G.vis);
    uint32_t* e2 = malloc((size_t)CV_MAX(ne, 1) * sizeof *e2);
    uint32_t* n2 = malloc((size_t)CV_MAX(nn, 1) * sizeof *n2);
    if (!sn || !e2 || !n2) { free(sn); free(e2); free(n2); return false; }
    uint32_t a = 0, b = 0;
    for (uint32_t k = 0; k < ne; k++) if (el[k] < G.frd.n_elems && shown(el[k])) e2[a++] = el[k];
    for (uint32_t k = 0; k < nn; k++) if (nd[k] < G.frd.n_nodes && sn[nd[k]]) n2[b++] = nd[k];
    G.sel_note[0] = 0;
    if (!a && !b && (ne || nn)) say("none of them is shown%s", "");
    bool ok = app_sel_apply(e2, a, n2, b, mode, which);
    free(sn); free(e2); free(n2);
    return ok;
}

static bool take_both(const uint32_t* el, uint32_t ne, const uint32_t* nd, uint32_t nn, int mode) {
    return take_which(el, ne, nd, nn, mode, app_sel_which());
}

bool app_sel_take_elems(const uint32_t* el, uint32_t ne, int mode) {
    uint32_t nn = 0;
    uint32_t* nd = G.sel_nodes ? cv_sel_elem_nodes(&G.frd, el, ne, &nn) : NULL;
    bool ok = take_both(el, ne, nd, nn, mode);
    free(nd);
    return ok;
}

bool app_sel_take_nodes(const uint32_t* nd, uint32_t nn, int mode) {
    return take_which(NULL, 0, nd, nn, mode, 2);
}

static bool in_list(const uint32_t* a, uint32_t n, uint32_t v) {
    uint32_t lo = 0, hi = n;                        /* the lists are sorted */
    while (lo < hi) { uint32_t m = (lo + hi) / 2; if (a[m] < v) lo = m + 1; else hi = m; }
    return lo < n && a[lo] == v;
}

/* the click tool: the element and node under the pixel. NEW toggles it: a click on
   something selected takes it out, on anything else adds it */
static bool click_one(const cv_pick* p) {
    int mode = G.sel_mode, which = app_sel_which();
    if (mode == CV_SEL_NEW) {
        bool in = (which & 1) ? in_list(G.sel, G.sel_n, p->elem) : in_list(G.seln, G.seln_n, p->node);
        mode = in ? CV_SEL_REMOVE : CV_SEL_ADD;
    }
    G.sel_note[0] = 0;
    return app_sel_apply(&p->elem, 1, &p->node, 1, mode, which) || mode == CV_SEL_REMOVE;
}

bool app_sel_click(float px, float py) {
    if (!G.loaded || G.sel_tool == CV_ST_NONE) return false;
    cv_pick p;
    float o[3], d[3];
    if (!app_pick(px, py, &p, o, d) || !p.hit) { say("nothing under the cursor%s", ""); return false; }
    switch (G.sel_tool) {
    case CV_ST_CLICK: return click_one(&p);
    default: return false;
    }
}

bool app_sel_invert(void) {
    if (!G.loaded) return false;
    int which = app_sel_which();
    if (G.sel_n) which |= 1;                         /* what is selected turns over too */
    if (G.seln_n) which |= 2;
    uint32_t ne = 0, nn = 0;
    uint32_t* el = (which & 1) ? cv_sel_invert_elems(&G.frd, G.vis, G.sel, G.sel_n, &ne) : NULL;
    uint32_t* nd = (which & 2) ? cv_sel_invert_nodes(&G.frd, G.vis, G.seln, G.seln_n, &nn) : NULL;
    G.sel_note[0] = 0;
    bool ok = app_sel_apply(el, ne, nd, nn, CV_SEL_NEW, which);
    free(el); free(nd);
    return ok;
}

bool app_sel_to_nodes(void) {
    if (!G.sel_n) { say("no elements selected%s", ""); return false; }
    uint32_t nn = 0;
    uint32_t* nd = cv_sel_elem_nodes(&G.frd, G.sel, G.sel_n, &nn);
    G.sel_elems = false; G.sel_nodes = true;          /* the tools take nodes now */
    G.sel_note[0] = 0;
    bool ok = app_sel_apply(NULL, 0, nd, nn, CV_SEL_NEW, 2);
    free(nd);
    return ok;
}

bool app_sel_to_elems(bool any) {
    if (!G.seln_n) { say("no nodes selected%s", ""); return false; }
    uint32_t ne = 0;
    uint32_t* el = cv_sel_node_elems(&G.frd, G.vis, G.seln, G.seln_n, any, &ne);
    G.sel_elems = true; G.sel_nodes = false;
    G.sel_note[0] = 0;
    bool ok = app_sel_apply(el, ne, NULL, 0, CV_SEL_NEW, 1);
    if (!ok) say(any ? "no shown element has a selected node%s" : "no shown element has every node selected%s", "");
    free(el);
    return ok;
}

/* ---- by name ------------------------------------------------------------------------ */

/* deck ids to indices, those not in the model dropped */
static uint32_t* ids_to_index(const uint32_t* ids, uint32_t n, bool nodes, uint32_t* m) {
    uint32_t* r = malloc((size_t)CV_MAX(n, 1) * sizeof *r);
    *m = 0;
    if (!r) return NULL;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = nodes ? cv_frd_node_index(&G.frd, ids[k]) : cv_frd_elem_index(&G.frd, ids[k]);
        if (i != UINT32_MAX) r[(*m)++] = i;
    }
    return r;
}

static bool by_set(const cv_set* s, int mode) {
    uint32_t m = 0;
    uint32_t* ix = ids_to_index(s->ids, s->n, !s->is_elem, &m);
    if (!ix) return false;
    bool ok = s->is_elem ? app_sel_take_elems(ix, m, mode) : app_sel_take_nodes(ix, m, mode);
    free(ix);
    return ok;
}

/* an element face surface: its elements, and the nodes of its faces; a node surface: its nodes */
static bool by_surface(const cv_surface* s, int mode) {
    if (!s->n) {
        uint32_t m = 0;
        uint32_t* ix = ids_to_index(s->nodes, s->nn, true, &m);
        bool ok = ix && app_sel_take_nodes(ix, m, mode);
        free(ix);
        return ok;
    }
    uint32_t* el = malloc((size_t)s->n * sizeof *el);
    uint32_t* nd = malloc((size_t)s->n * 8 * sizeof *nd);
    if (!el || !nd) { free(el); free(nd); return false; }
    uint32_t ne = 0, nn = 0;
    for (uint32_t k = 0; k < s->n; k++) {
        uint32_t e = cv_frd_elem_index(&G.frd, s->elem[k]);
        if (e == UINT32_MAX) continue;
        el[ne++] = e;
        nn += (uint32_t)cv_elem_face_nodes(&G.frd, e, s->face[k], nd + nn);
    }
    bool ok = take_both(el, ne, nd, nn, mode);
    free(el); free(nd);
    return ok;
}

/* the elements of one value of a group axis (type, material) */
static bool by_axis(int axis, int v, int mode) {
    uint32_t* el = malloc((size_t)CV_MAX(G.frd.n_elems, 1) * sizeof *el), ne = 0;
    if (!el) return false;
    for (uint32_t e = 0; e < G.frd.n_elems; e++) if (G.groups.axis[axis].of_elem[e] == v) el[ne++] = e;
    bool ok = app_sel_take_elems(el, ne, mode);
    free(el);
    return ok;
}

static int axis_find(int axis, const char* name) {
    const cv_axis* ax = &G.groups.axis[axis];
    char* end;
    unsigned long num = strtoul(name, &end, 10);
    for (int i = 0; i < ax->n; i++) {
        uint32_t v = ax->value[i];
        if (axis == CV_AXIS_TYPE && !strcasecmp(cv_frd_type_name((int)v), name)) return i;
        if (axis == CV_AXIS_MAT) {
            const char* mn = deck_material_name(v);
            if (mn && !strcasecmp(mn, name)) return i;
        }
        if (*name && !*end && num == v) return i;
    }
    return -1;
}

bool app_sel_by_name(const char* name, int mode) {
    if (!G.loaded) return false;
    const cv_inp* d = deck_get();
    bool set = !strncasecmp(name, "set:", 4), surf = !strncasecmp(name, "surf:", 5);
    if (!strncasecmp(name, "type:", 5) || !strncasecmp(name, "mat:", 4)) {
        int axis = name[0] == 't' || name[0] == 'T' ? CV_AXIS_TYPE : CV_AXIS_MAT;
        const char* v = strchr(name, ':') + 1;
        int i = axis_find(axis, v);
        if (i < 0) { say(axis == CV_AXIS_TYPE ? "no element of type %s" : "no material %s", v); return false; }
        return by_axis(axis, i, mode);
    }
    const char* nm = set ? name + 4 : surf ? name + 5 : name;
    for (int i = 0; d && !surf && i < d->nsets; i++)
        if (!strcasecmp(d->sets[i].name, nm)) return by_set(&d->sets[i], mode);
    for (int i = 0; d && !set && i < d->nsurfs; i++)
        if (!strcasecmp(d->surfs[i].name, nm)) return by_surface(&d->surfs[i], mode);
    say(d ? "no set or surface %s in the deck" : "no deck: no set %s", nm);
    return false;
}

bool app_sel_by_ids(const char* text, bool nodes, int mode) {
    if (!G.loaded) return false;
    cv_idlist l;
    if (!cv_idlist_parse(text, &l)) { say("too many ids%s", ""); return false; }
    uint32_t m = 0;
    uint32_t* ix = ids_to_index(l.ids, (uint32_t)l.n, nodes, &m);
    bool ok = false;
    if (ix) ok = nodes ? app_sel_take_nodes(ix, m, mode) : app_sel_take_elems(ix, m, mode);
    char t[sizeof G.sel_note];
    t[0] = 0;
    size_t miss = l.n - m;
    if (l.bad) snprintf(t, sizeof t, "not ids: %s", l.bad_text);
    if (miss) snprintf(t + strlen(t), sizeof t - strlen(t), "%s%zu %s not in the model", t[0] ? "; " : "", miss, nodes ? "nodes" : "elements");
    if (t[0]) snprintf(G.sel_note, sizeof G.sel_note, "%s", t);
    free(ix);
    cv_idlist_free(&l);
    return ok;
}

/* ---- --select ------------------------------------------------------------------------ */

bool app_sel_spec(const char* spec) {
    static const struct { const char* key; int mode; } modes[] = {
        { "new:", CV_SEL_NEW }, { "add:", CV_SEL_ADD }, { "remove:", CV_SEL_REMOVE }, { "sub:", CV_SEL_REMOVE },
        { "and:", CV_SEL_AND }, { "intersect:", CV_SEL_AND },
    };
    int mode = CV_SEL_NEW;
    for (size_t k = 0; k < CV_COUNT(modes); k++)
        if (!strncasecmp(spec, modes[k].key, strlen(modes[k].key))) { mode = modes[k].mode; spec += strlen(modes[k].key); break; }
    G.sel_note[0] = 0;
    if (!strncasecmp(spec, "ids:", 4)) return app_sel_by_ids(spec + 4, false, mode);
    if (!strncasecmp(spec, "nids:", 5)) return app_sel_by_ids(spec + 5, true, mode);
    if (!strcasecmp(spec, "invert")) return app_sel_invert();
    if (!strcasecmp(spec, "nodes")) return app_sel_to_nodes();
    if (!strcasecmp(spec, "elements") || !strcasecmp(spec, "elems")) return app_sel_to_elems(false);
    if (!strcasecmp(spec, "elements-any") || !strcasecmp(spec, "elems-any")) return app_sel_to_elems(true);
    if (!strcasecmp(spec, "clear")) { app_sel_clear(); return true; }
    if (!strncasecmp(spec, "takes:", 6)) {               /* what the next ones take */
        const char* w = spec + 6;
        G.sel_elems = !strcasecmp(w, "elements") || !strcasecmp(w, "both");
        G.sel_nodes = !strcasecmp(w, "nodes") || !strcasecmp(w, "both");
        if (!G.sel_elems && !G.sel_nodes) { G.sel_elems = true; say("takes: elements, nodes or both, not %s", w); return false; }
        return true;
    }
    if (!strncasecmp(spec, "facing:", 7)) { G.sel_visible = !strcasecmp(spec + 7, "on"); return true; }
    return app_sel_by_name(spec, mode);
}
