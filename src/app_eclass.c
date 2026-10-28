/* app_eclass.c -- the faces' categorical colours, whatever they go by: element type,
   material, group (the Groups axes, G.axis_rgb), element set or section (eclass.h,
   from the deck, built when asked and again after the deck changed). Per element a
   class, a colour per class, the legend's names. */
#include "app.h"
#include "app_int.h"
#include "eclass.h"

static struct {
    cv_eclass c;
    float*    rgb;
    bool      built;
    unsigned  gen;                /* deck_gen() it was built from */
    const uint32_t* ids;          /* and the mesh */
    uint32_t  ne;
} E[2];                           /* FM_ESET, FM_SECT */

static int which(void) { return G.faces_mode == FM_ESET ? 0 : G.faces_mode == FM_SECT ? 1 : -1; }

static void build(int k) {
    unsigned g = deck_gen();
    if (E[k].built && E[k].gen == g && E[k].ids == G.frd.elem_id && E[k].ne == G.frd.n_elems) return;
    cv_eclass_free(&E[k].c);
    free(E[k].rgb); E[k].rgb = NULL;
    E[k].built = true; E[k].gen = g; E[k].ids = G.frd.elem_id; E[k].ne = G.frd.n_elems;
    const cv_inp* d = deck_get();
    cv_inp none;
    memset(&none, 0, sizeof none);
    if (!d) d = &none;
    if (!(k == 0 ? cv_eclass_sets(&E[k].c, &G.frd, d->sets, d->nsets) : cv_eclass_sections(&E[k].c, &G.frd, d))) return;
    E[k].rgb = malloc((size_t)CV_MAX(E[k].c.n, 1) * 3 * sizeof(float));
    for (int i = 0; E[k].rgb && i < E[k].c.n; i++) {
        if (E[k].c.none && i == E[k].c.n - 1) memcpy(E[k].rgb + 3 * i, cv_palette_none, 3 * sizeof(float));
        else cv_palette(i, E[k].rgb + 3 * i);
    }
}

bool app_face_classes(const uint16_t** of, int* n, const float** rgb) {
    int a = G.faces_mode - FM_TYPE, k = which();
    if (!G.loaded) return false;
    if (a >= 0 && a < CV_AXIS_N) {
        if (!G.axis_rgb[a] || !G.groups.axis[a].of_elem) return false;
        *of = G.groups.axis[a].of_elem; *n = G.groups.axis[a].n; *rgb = G.axis_rgb[a];
        return true;
    }
    if (k < 0) return false;
    build(k);
    if (!E[k].c.of_elem || !E[k].rgb) return false;
    *of = E[k].c.of_elem; *n = E[k].c.n; *rgb = E[k].rgb;
    return true;
}

const char* app_face_classes_title(void) {
    static const char* t[FM_N] = { "", "element type", "material", "group", "", "element set", "section" };
    return G.faces_mode >= 0 && G.faces_mode < FM_N ? t[G.faces_mode] : "";
}

/* class i's name for the legend; false: not listed (a Groups value unticked) */
bool app_face_class_label(int i, char* out, size_t n) {
    int a = G.faces_mode - FM_TYPE, k = which();
    if (a >= 0 && a < CV_AXIS_N) {
        const cv_axis* ax = &G.groups.axis[a];
        if (i >= ax->n || !ax->on[i]) return false;
        const char* mat = a == CV_AXIS_MAT ? deck_material_name(ax->value[i]) : NULL;   /* the names Groups shows */
        if (a == CV_AXIS_TYPE) snprintf(out, n, "%s", cv_frd_type_name((int)ax->value[i]));
        else if (mat) snprintf(out, n, "%s", mat);
        else snprintf(out, n, "%s %u", a == CV_AXIS_MAT ? "Material" : "Group", ax->value[i]);
        return true;
    }
    if (k < 0 || i >= E[k].c.n) return false;
    snprintf(out, n, "%s", E[k].c.name[i]);
    return true;
}
