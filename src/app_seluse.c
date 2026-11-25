/* app_seluse.c -- what is done with the selection (app.h): the deck sets it
   overlaps, named selections kept with the model (its .ccxview, app_sidecar.c),
   its ids as *ELSET / *NSET lines (to paste, or a small <model>_<name>.inp to
   *INCLUDE: the solver's own files are never touched), and the crop box or the
   clip plane put on it. */
#include "app_int.h"
#include <ctype.h>
#include <stdio.h>
#include <strings.h>

/* ---- the deck sets it overlaps ----------------------------------------------------- */

static uint32_t count_in(const cv_set* st, const uint32_t* ix, uint32_t n, bool nodes) {
    uint32_t c = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t id = nodes ? G.frd.node_id[ix[k]] : G.frd.elem_id[ix[k]], lo = 0, hi = st->n;
        while (lo < hi) { uint32_t m = (lo + hi) / 2; if (st->ids[m] < id) lo = m + 1; else hi = m; }
        c += lo < st->n && st->ids[lo] == id;
    }
    return c;
}

void app_sel_sets_overlap(char* out, size_t n) {
    const cv_inp* d = deck_get();
    size_t o = 0;
    if (n) out[0] = 0;
    for (int i = 0; d && i < d->nsets && o + 40 < n; i++) {
        const cv_set* st = &d->sets[i];
        uint32_t c = st->is_elem ? count_in(st, G.sel, G.sel_n, false) : count_in(st, G.seln, G.seln_n, true);
        if (c) o += (size_t)snprintf(out + o, n - o, "%s%s %u/%u", o ? ", " : "", st->name, c, st->n);
    }
}

/* ---- named selections ---------------------------------------------------------------- */

static cv_namedsel* ns;
static int nns;

int app_nsel_count(void) { return nns; }
const cv_namedsel* app_nsel_at(int i) { return i >= 0 && i < nns ? &ns[i] : NULL; }

void app_nsel_clear(void) {
    for (int i = 0; i < nns; i++) { free(ns[i].eid); free(ns[i].nid); }
    free(ns); ns = NULL; nns = 0;
}

/* a name a deck takes: letters, digits, _ and -, no blanks; "" stays "" */
static void clean_name(char* out, size_t n, const char* name) {
    size_t o = 0;
    for (const char* p = name; *p && o + 1 < n; p++) {
        if (*p == ' ' && o == 0) continue;
        out[o++] = isalnum((unsigned char)*p) || *p == '_' || *p == '-' ? *p : '_';
    }
    while (o && out[o - 1] == '_') o--;
    out[o] = 0;
}

static int find(const char* name) {
    for (int i = 0; i < nns; i++) if (!strcasecmp(ns[i].name, name)) return i;
    return -1;
}

static uint32_t* ids_of(const uint32_t* ix, uint32_t n, bool nodes) {
    uint32_t* r = malloc((size_t)CV_MAX(n, 1) * sizeof *r);
    if (!r) return NULL;
    for (uint32_t k = 0; k < n; k++) r[k] = nodes ? G.frd.node_id[ix[k]] : G.frd.elem_id[ix[k]];
    qsort(r, n, sizeof *r, cv_cmp_u32);
    return r;
}

bool app_nsel_add_ids(const char* name, const uint32_t* eid, uint32_t ne, const uint32_t* nid, uint32_t nn) {
    char nm[sizeof ns[0].name];
    clean_name(nm, sizeof nm, name);
    if (!nm[0]) return false;
    uint32_t* e = malloc((size_t)CV_MAX(ne, 1) * sizeof *e);
    uint32_t* v = malloc((size_t)CV_MAX(nn, 1) * sizeof *v);
    int i = find(nm);
    if (i < 0) {
        cv_namedsel* g = realloc(ns, (size_t)(nns + 1) * sizeof *ns);
        if (g) ns = g; else { free(e); free(v); return false; }
    }
    if (!e || !v) { free(e); free(v); return false; }
    if (ne) memcpy(e, eid, ne * sizeof *e);
    if (nn) memcpy(v, nid, nn * sizeof *v);
    if (i < 0) i = nns++;
    else { free(ns[i].eid); free(ns[i].nid); }
    snprintf(ns[i].name, sizeof ns[i].name, "%s", nm);
    ns[i].eid = e; ns[i].ne = ne; ns[i].nid = v; ns[i].nn = nn;
    return true;
}

int app_nsel_save(const char* name) {
    if (!G.sel_n && !G.seln_n) { snprintf(G.sel_note, sizeof G.sel_note, "nothing selected to keep"); return -1; }
    uint32_t* e = ids_of(G.sel, G.sel_n, false);
    uint32_t* v = ids_of(G.seln, G.seln_n, true);
    char nm[sizeof ns[0].name];
    clean_name(nm, sizeof nm, name);
    if (!nm[0]) snprintf(nm, sizeof nm, "SEL%d", nns + 1);
    bool ok = e && v && app_nsel_add_ids(nm, e, G.sel_n, v, G.seln_n);
    free(e); free(v);
    snprintf(G.sel_note, sizeof G.sel_note, ok ? "kept as %s" : "could not keep %s", nm);
    return ok ? find(nm) : -1;
}

bool app_nsel_take(int i, int mode) {
    const cv_namedsel* s = app_nsel_at(i);
    if (!s || !G.loaded) return false;
    uint32_t* el = malloc((size_t)CV_MAX(s->ne, 1) * sizeof *el);
    uint32_t* nd = malloc((size_t)CV_MAX(s->nn, 1) * sizeof *nd);
    uint32_t ne = 0, nn = 0;
    bool ok = false;
    if (el && nd) {
        for (uint32_t k = 0; k < s->ne; k++) { uint32_t e = cv_frd_elem_index(&G.frd, s->eid[k]); if (e != UINT32_MAX) el[ne++] = e; }
        for (uint32_t k = 0; k < s->nn; k++) { uint32_t v = cv_frd_node_index(&G.frd, s->nid[k]); if (v != UINT32_MAX) nd[nn++] = v; }
        G.sel_note[0] = 0;
        ok = app_sel_apply(el, ne, nd, nn, mode, (s->ne ? 1 : 0) | (s->nn ? 2 : 0));
    }
    free(el); free(nd);
    return ok;
}

bool app_nsel_rename(int i, const char* name) {
    char nm[sizeof ns[0].name];
    clean_name(nm, sizeof nm, name);
    if (i < 0 || i >= nns || !nm[0] || (find(nm) >= 0 && find(nm) != i)) return false;
    snprintf(ns[i].name, sizeof ns[i].name, "%s", nm);
    return true;
}

void app_nsel_delete(int i) {
    if (i < 0 || i >= nns) return;
    free(ns[i].eid); free(ns[i].nid);
    memmove(ns + i, ns + i + 1, (size_t)(nns - i - 1) * sizeof *ns);
    nns--;
}

/* ---- as deck lines --------------------------------------------------------------------- */

typedef CV_VEC(char) text;

static void put_ids(text* t, const uint32_t* ix, uint32_t n, bool nodes) {
    char b[24];
    for (uint32_t k = 0; k < n; k++) {
        int l = snprintf(b, sizeof b, "%u%s", nodes ? G.frd.node_id[ix[k]] : G.frd.elem_id[ix[k]],
                         k + 1 == n ? "\n" : k % 16 == 15 ? ",\n" : ", ");
        if (l > 0 && cv_reserve(*t, t->n + (size_t)l + 1)) { memcpy(t->a + t->n, b, (size_t)l); t->n += (size_t)l; }
    }
}

char* app_sel_inp_text(const char* name) {
    char nm[sizeof ns[0].name], line[160];
    clean_name(nm, sizeof nm, name);
    if (!nm[0]) snprintf(nm, sizeof nm, "SELECTION");
    for (char* p = nm; *p; p++) *p = (char)toupper((unsigned char)*p);
    text t = {0};
    for (int kind = 0; kind < 2; kind++) {
        uint32_t n = kind ? G.seln_n : G.sel_n;
        if (!n) continue;
        int l = snprintf(line, sizeof line, kind ? "*NSET, NSET=%s\n" : "*ELSET, ELSET=%s\n", nm);
        if (l > 0 && cv_reserve(t, t.n + (size_t)l + 1)) { memcpy(t.a + t.n, line, (size_t)l); t.n += (size_t)l; }
        put_ids(&t, kind ? G.seln : G.sel, n, kind == 1);
    }
    if (!cv_reserve(t, t.n + 1)) { cv_free_vec(t); return NULL; }
    t.a[t.n] = 0;
    return t.a;
}

/* <model>_<name>.inp beside the model */
void app_sel_inp_path(const char* name, char* out, size_t n) {
    char base[1024], nm[sizeof ns[0].name];
    snprintf(base, sizeof base, "%s", G.path);
    char* dot = strrchr(base, '.');
    char* sep = strrchr(base, cv_path_sep());
    if (dot && (!sep || dot > sep)) *dot = 0;
    clean_name(nm, sizeof nm, name);
    snprintf(out, n, "%s_%s.inp", base, nm[0] ? nm : "selection");
}

bool app_sel_inp_save(const char* name) {
    if (!G.loaded || (!G.sel_n && !G.seln_n)) return false;
    char path[1200];
    app_sel_inp_path(name, path, sizeof path);
    char* t = app_sel_inp_text(name);
    FILE* fp = t ? fopen(path, "w") : NULL;
    bool ok = fp && fprintf(fp, "** the selection %s, written by ccxview from %s (*INCLUDE, INPUT=...)\n%s", name, G.path, t) > 0;
    if (fp && fclose(fp) != 0) ok = false;
    free(t);
    snprintf(G.sel_note, sizeof G.sel_note, ok ? "saved %s" : "could not write %s", path);
    cv_msg_add(&G.msgs, 0, false, G.sel_note);
    return ok;
}

/* ---- the crop box and the clip plane on it ----------------------------------------------- */

/* the undeformed box round the selection's nodes (and its elements' nodes) */
static bool sel_box(float lo[3], float hi[3]) {
    for (int k = 0; k < 3; k++) { lo[k] = INFINITY; hi[k] = -INFINITY; }
    for (int kind = 0; kind < 2; kind++)
        for (uint32_t i = 0; i < (kind ? G.seln_n : G.sel_n); i++) {
            uint32_t a = kind ? i : G.frd.eoff[G.sel[i]], b = kind ? i + 1 : G.frd.eoff[G.sel[i] + 1];
            for (uint32_t j = a; j < b; j++) {
                const float* p = G.frd.xyz + 3 * (kind ? G.seln[j] : G.frd.conn[j]);
                for (int k = 0; k < 3; k++) { lo[k] = CV_MIN(lo[k], p[k]); hi[k] = CV_MAX(hi[k], p[k]); }
            }
        }
    return lo[0] <= hi[0];
}

bool app_sel_crop(void) {
    float lo[3], hi[3];
    if (!G.loaded || !sel_box(lo, hi)) return false;
    const float b0[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, b1[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
    for (int k = 0; k < 3; k++) {
        float ext = b1[k] - b0[k];
        G.crop_lo[k] = ext > 0 ? CV_MAX((lo[k] - b0[k]) / ext, 0.f) : 0.f;
        G.crop_hi[k] = ext > 0 ? CV_MIN((hi[k] - b0[k]) / ext, 1.f) : 1.f;
    }
    G.crop_on = true;
    app_view_push();                     /* the view before, for Ctrl+Z */
    G.fit_pending = true;                /* framed once the cropped skin is built */
    app_groups_changed();
    return true;
}

bool app_sel_clip(void) {
    float lo[3], hi[3];
    if (!G.loaded || !sel_box(lo, hi)) return false;
    int k = G.clip_axis < 0 || G.clip_axis > 2 ? 0 : G.clip_axis;
    const float b0[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, b1[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
    float c = 0.5f * (lo[k] + hi[k]), ext = b1[k] - b0[k];
    G.clip_pos = ext > 0 ? CV_MIN(CV_MAX((c - b0[k]) / ext, 0.f), 1.f) : 0.5f;
    G.clip_on = true;
    return true;
}
