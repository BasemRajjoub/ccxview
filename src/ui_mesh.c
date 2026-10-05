/* ui_mesh.c -- the mesh quality field in the Fields panel, and the Mesh quality
   window: what the mesh is made of, and every quality measure over it with its
   range, spread, worst element and how many elements pass the usual limit. */
#include "app.h"
#include "ui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "nk.h"
#include "sokol_nuklear.h"
#include "ui_int.h"
#include "app_mesh.h"
#include <math.h>

/* the overall scores first, then the measures they come from */
static int order(int i) {
    int ns = CV_MQ_N - CV_MQ_SCORE0;
    return i < ns ? CV_MQ_SCORE0 + i : i - ns;
}

/* ---- the Fields panel: which measure, the window -------------------------------------- */

void section_mesh(struct nk_context* ctx, float s, float row) {
    (void)s;
    bool active = G.field_src == 4;
    if (!nk_tree_push_id(ctx, NK_TREE_NODE, "Mesh quality", active ? NK_MAXIMIZED : NK_MINIMIZED, 401)) return;
    nk_layout_row_dynamic(ctx, row, 1);
    for (int i = 0; i < CV_MQ_N; i++) {
        int q = order(i);
        if (i == 0 || i == CV_MQ_N - CV_MQ_SCORE0)
            nk_label_colored(ctx, i ? "Measures" : "Overall scores", NK_TEXT_LEFT, P.dim);
        bool sel = active && G.mesh_q == q;
        tip(ctx, cv_mq(q)->tip);
        if (nk_option_label(ctx, cv_mq(q)->name, sel) && !sel) app_mesh_set(q);
    }
    if (active && !G.has_field) nk_label_colored(ctx, G.legend_lines[1], NK_TEXT_LEFT, P.warn);
    tip(ctx, "The mesh at a glance: element types, size, and each measure's range,\n"
             "spread and worst element, against the usual limits");
    if (nk_button_label(ctx, "Mesh quality...")) G.show_mesh = !G.show_mesh;
    nk_tree_pop(ctx);
}

/* ---- the window ------------------------------------------------------------------------- */

static void num(char* out, size_t n, double v) {
    if (v != v) snprintf(out, n, "-");
    else if (isinf(v)) snprintf(out, n, v > 0 ? "inf" : "-inf");
    else fmt_num(out, n, v);
}

/* ten bars, the poor end of the range in the warning colour */
static void histogram(struct nk_context* ctx, const mesh_stat* m, int q, int t) {
    struct nk_rect r;
    if (!nk_widget(&r, ctx)) return;
    struct nk_command_buffer* cv = nk_window_get_canvas(ctx);
    uint32_t top = 1;
    for (int b = 0; b < 10; b++) if (m->hist[b] > top) top = m->hist[b];
    double lim = cv_mq_limit(q, t), w = m->max - m->min;
    for (int b = 0; b < 10; b++) {
        if (!m->hist[b]) continue;
        float h = (r.h - 2) * (float)(0.15 + 0.85 * log1p(m->hist[b]) / log1p(top));
        double mid = m->min + w * (b + 0.5) / 10;
        bool poor = lim == lim && (cv_mq(q)->high_bad ? mid > lim : mid < lim);
        nk_fill_rect(cv, nk_rect(r.x + r.w * b / 10 + 1, r.y + r.h - 1 - h, r.w / 10 - 2, h), 0, poor ? P.warn : P.accent);
    }
}

/* how the overall scores are made: words, then each measure's part in them */
static void explain(struct nk_context* ctx, float s, float row, int t) {
    if (!nk_tree_push_id(ctx, NK_TREE_TAB, "How the overall scores are made", NK_MINIMIZED, 402)) return;
    static const char* text[] = {
        "ccxview quality, 0 to 1: every measure below scored from 1 at its ideal to 0 at its",
        "  limit (ratios on a log scale), the element taking the weakest. 0: past a limit.",
        "  Angles are left to the skewness, which is made of them.",
        "HyperMesh QI: each measure a penalty, 0 up to \"good\", 1 at \"fail\", 1 to 10 up to",
        "  \"worst\"; the element's index the mean of passing penalties plus the sum of failing",
        "  ones. 0 ideal, 1 or more fails. HyperMesh's scheme on ccxview's measures and limits.",
        "ANSYS element quality: C V / sqrt((sum e^2)^3), shells C A / sum e^2, over the",
        "  corner edges; 1 for a cube, square, regular tet or triangle, 0 flat or inverted.",
        "Abaqus checks failed: of aspect, smallest and largest face angle, shape factor",
        "  (triangles, tets) at the Verify Mesh defaults, and inside out. 0 passes.",
    };
    nk_layout_row_dynamic(ctx, row * 0.9f, 1);
    for (size_t i = 0; i < CV_COUNT(text); i++) nk_label(ctx, text[i], NK_TEXT_LEFT);
    char h[64];
    snprintf(h, sizeof h, "For %s elements:", t ? cv_frd_type_name(t) : "the");
    nk_layout_row_dynamic(ctx, row, 1);
    nk_label_colored(ctx, h, NK_TEXT_LEFT, P.accent);
    const float cols[] = { 0.28f, 0.18f, 0.18f, 0.18f, 0.18f };
    nk_layout_row(ctx, NK_DYNAMIC, row, 5, cols);
    static const char* heads[] = { "measure", "ccxview 1 .. 0", "HM good", "HM fail", "HM worst" };
    static const char* tips[] = { "", "The ideal and the limit: the ccxview score runs from 1 to 0 between them",
        "HyperMesh penalty 0 up to here", "Penalty 1 here: the usual limit", "Penalty 10 from here" };
    for (int k = 0; k < 5; k++) { tip(ctx, tips[k]); nk_label_colored(ctx, heads[k], k ? NK_TEXT_RIGHT : NK_TEXT_LEFT, P.dim); }
    static const int ideal_q[] = { CV_MQ_ASPECT, CV_MQ_SJAC, CV_MQ_JRATIO, CV_MQ_SKEW, CV_MQ_ANGLE_MIN, CV_MQ_ANGLE_MAX, CV_MQ_WARP, CV_MQ_SHAPE };
    static const double ideal[] = { 1, 1, 1, 0, NAN, NAN, 0, 1 };
    for (size_t i = 0; i < CV_COUNT(ideal_q); i++) {
        int q = ideal_q[i];
        double lim = cv_mq_limit(q, t), g = cv_mq_hm_good(q, t), w = cv_mq_hm_worst(q, t);
        if (lim != lim) continue;
        char a[32], b[32];
        nk_layout_row(ctx, NK_DYNAMIC, row, 5, cols);
        nk_label(ctx, cv_mq(q)->name, NK_TEXT_LEFT);
        if (ideal[i] == ideal[i]) { fmt_num(a, sizeof a, ideal[i]); fmt_num(b, sizeof b, lim); snprintf(h, sizeof h, "%s .. %s", a, b); }
        else snprintf(h, sizeof h, "-");
        nk_label(ctx, h, NK_TEXT_RIGHT);
        const double lv[3] = { g, lim, w };
        for (int k = 0; k < 3; k++) {
            if (lv[k] == lv[k]) fmt_num(a, sizeof a, lv[k]); else snprintf(a, sizeof a, "-");
            nk_label(ctx, a, NK_TEXT_RIGHT);
        }
    }
    (void)s;
    nk_tree_pop(ctx);
}

void window_mesh(struct nk_context* ctx, float s, float row, int fw, int fh) {
    static bool was_open;
    static mesh_stat st[CV_MQ_N];
    static mesh_info info;
    static unsigned key_gen;
    static double key_len;
    static bool ok;
    if (!G.show_mesh || !G.loaded) { was_open = false; return; }
    if (!was_open) { nk_window_show(ctx, "Mesh quality", NK_SHOWN); key_gen = 0; }
    was_open = true;
    if (key_gen != mesh_gen() || key_len != mesh_len_scale()) {     /* worked out again only when it changed */
        ok = mesh_stats(st, &info);
        key_gen = mesh_gen();
        key_len = mesh_len_scale();
    }
    float w = CV_MIN(760 * s, fw * 0.95f), h = CV_MIN(fh * 0.85f, row * 32);
    if (nk_begin(ctx, "Mesh quality", nk_rect((fw - w) / 2, (fh - h) / 2, w, h),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE | NK_WINDOW_BORDER)) {
        char t[256], a[32], b[32], c[32];
        if (!ok) {
            nk_layout_row_dynamic(ctx, row, 1);
            nk_label(ctx, "No mesh.", NK_TEXT_LEFT);
            nk_end(ctx);
            return;
        }
        /* what the mesh is */
        int main_t = 0;                                  /* the commonest type: its limits shown */
        size_t o = 0;
        t[0] = 0;
        for (int k = 1; k < 16; k++) {
            if (!info.per_type[k]) continue;
            if (!main_t || info.per_type[k] > info.per_type[main_t]) main_t = k;
            o += snprintf(t + o, sizeof t - o, "%s%s %u", o ? ",  " : "", cv_frd_type_name(k), info.per_type[k]);
            if (o >= sizeof t) break;
        }
        nk_layout_row_dynamic(ctx, row, 1);
        nk_label_colored(ctx, "Mesh", NK_TEXT_LEFT, P.accent);
        nk_layout_row_dynamic(ctx, row, 2);
        snprintf(b, sizeof b, "elements %u   nodes %u", info.elems, info.used_nodes);
        tip(ctx, info.used_nodes < info.nodes ? "Nodes used by elements (the file has more: free nodes)" : "Nodes used by elements");
        nk_label(ctx, b, NK_TEXT_LEFT);
        snprintf(b, sizeof b, "materials %u   groups %u", info.mats, info.groups);
        nk_label(ctx, b, NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, row, 1);
        tip(ctx, "Elements of each .frd type");
        nk_label(ctx, t, NK_TEXT_LEFT);
        const char* u = mesh_unit(CV_MQ_EDGE_MIN);
        o = 0;
        t[0] = 0;
        const double sums[3] = { info.volume, info.area, info.length };
        static const char* sum_names[3] = { "volume", "area", "length" }, *pows[3] = { "^3", "^2", "" };
        for (int k = 0; k < 3 && o < sizeof t; k++) {
            if (sums[k] == 0) continue;
            num(a, sizeof a, sums[k]);
            o += snprintf(t + o, sizeof t - o, "%s%s %s%s%s%s", o ? "   " : "", sum_names[k], a, u[0] ? " " : "", u, u[0] ? pows[k] : "");
        }
        tip(ctx, "Summed over the solid, shell and plane, and beam elements");
        nk_label(ctx, t, NK_TEXT_LEFT);
        num(a, sizeof a, info.box[3] - info.box[0]);
        num(b, sizeof b, info.box[4] - info.box[1]);
        num(c, sizeof c, info.box[5] - info.box[2]);
        snprintf(t, sizeof t, "extent %s x %s x %s%s%s", a, b, c, u[0] ? " " : "", u);
        tip(ctx, "The bounding box of the nodes in use");
        nk_label(ctx, t, NK_TEXT_LEFT);
        snprintf(t, sizeof t, "%u elements (%.2f %%) past at least one limit", info.poor_any,
                 info.elems ? 100.0 * info.poor_any / info.elems : 0);
        tip(ctx, "Elements past the usual limit of any measure below");
        nk_label_colored(ctx, t, NK_TEXT_LEFT, info.poor_any ? P.warn : P.text);
        uii_hsep(ctx, s);

        /* each measure */
        const float cols[] = { 0.2f, 0.11f, 0.11f, 0.11f, 0.1f, 0.12f, 0.11f, 0.14f };
        nk_layout_row(ctx, NK_DYNAMIC, row, 8, cols);
        static const char* heads[] = { "measure", "min", "mean", "max", "limit", "poor", "worst", "spread" };
        static const char* head_tips[] = {
            "Click one to show it on the model, per element",
            "The least value over the elements it applies to", "The mean", "The largest value",
            "The usual limit, for the commonest element type here", "Elements past their type's limit",
            "The worst element: click to find it", "How the elements spread from min to max\n(log scale; poor end in the warning colour)" };
        for (int k = 0; k < 8; k++) { tip(ctx, head_tips[k]); nk_label_colored(ctx, heads[k], k ? NK_TEXT_RIGHT : NK_TEXT_LEFT, P.dim); }
        for (int i = 0; i < CV_MQ_N; i++) {
            int q = order(i);
            const mesh_stat* m = &st[q];
            if (i == CV_MQ_N - CV_MQ_SCORE0) uii_hsep(ctx, s);
            const cv_mq_info* in = cv_mq(q);
            nk_layout_row(ctx, NK_DYNAMIC, row, 8, cols);
            tip(ctx, in->tip);
            bool on = G.field_src == 4 && G.mesh_q == q;
            if (nk_select_label(ctx, in->name, NK_TEXT_LEFT, on) && !on) app_mesh_set(q);
            if (!m->n) {
                nk_label_colored(ctx, "does not apply", NK_TEXT_RIGHT, P.dim);
                for (int k = 0; k < 6; k++) nk_label(ctx, "", NK_TEXT_LEFT);
                continue;
            }
            num(a, sizeof a, m->min); nk_label(ctx, a, NK_TEXT_RIGHT);
            num(a, sizeof a, m->mean); nk_label(ctx, a, NK_TEXT_RIGHT);
            num(a, sizeof a, m->max); nk_label(ctx, a, NK_TEXT_RIGHT);
            double lim = cv_mq_limit(q, main_t);
            if (lim == lim) { num(b, sizeof b, lim); snprintf(a, sizeof a, "%s %s", in->high_bad ? (in->incl ? ">=" : ">") : (in->incl ? "<=" : "<"), b); }
            else snprintf(a, sizeof a, "-");
            nk_label_colored(ctx, a, NK_TEXT_RIGHT, P.dim);
            if (m->poor) snprintf(a, sizeof a, "%u (%.1f%%)", m->poor, 100.0 * m->poor / m->n);
            else snprintf(a, sizeof a, "%s", lim == lim ? "0" : "-");
            nk_label_colored(ctx, a, NK_TEXT_RIGHT, m->poor ? P.warn : P.text);
            if (m->worst != UINT32_MAX) {
                snprintf(a, sizeof a, "%u", G.frd.elem_id[m->worst]);
                tip(ctx, "Find this element: probe it and centre the view on it");
                if (nk_button_label(ctx, a)) {
                    if (!(G.field_src == 4 && G.mesh_q == q)) app_mesh_set(q);
                    app_find(G.frd.elem_id[m->worst], true);
                }
            } else nk_label(ctx, "", NK_TEXT_LEFT);
            histogram(ctx, m, q, main_t);
        }
        explain(ctx, s, row, main_t);
        nk_layout_row_dynamic(ctx, row, 1);
        snprintf(t, sizeof t, "Lengths in %s, angles in degrees. Undeformed mesh; quadratic elements on their corners", u[0] ? u : "model units");
        tip(ctx, "Except the Jacobian ratio, which sees curved edges through the mid-side nodes.\n"
                 "Shells and beams CalculiX expanded to solids show as the .frd has them");
        nk_label_colored(ctx, t, NK_TEXT_LEFT, P.dim);
        nk_label_colored(ctx, "Limits: Abaqus Verify Mesh, ANSYS shape checking, Verdict (Sandia) scaled Jacobian;", NK_TEXT_LEFT, P.dim);
        nk_label_colored(ctx, "scores: ANSYS Element Quality, HyperMesh quality index, Abaqus Verify Mesh", NK_TEXT_LEFT, P.dim);
    }
    if (nk_window_is_hidden(ctx, "Mesh quality")) G.show_mesh = false;
    nk_end(ctx);
}
