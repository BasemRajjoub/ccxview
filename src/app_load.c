/* app_load.c -- background loading (.frd / .inp / .fbd / .dat / .sta), the skin
   job after group changes, taking a finished job into G, and the open dialog. */
#include "app_int.h"
#include "calc.h"
#include "web.h"
#include "cgx.h"
#include "filedlg.h"
#include "log.h"
#include "cfg.h"
#include "app_fail.h"
#include "app_mesh.h"
#include "sokol_app.h"
#include <math.h>
#include <strings.h>

/* ---- background jobs ------------------------------------------------------------ */

static void job_finish(cv_job* j, double t0) {
    j->seconds = cv_now() - t0;
    cv_mutex_lock(&j->lock);
    j->done = true;
    cv_mutex_unlock(&j->lock);
}

/* A .fbd: its geometry directly, or -- a script, when asked -- evaluated by cgx
   in a temporary copy of its folder, with the mesh if the script makes one.
   Without a mesh from cgx, the geometry's own ELTY assignments are meshed here. */
static void fbd_eval_cgx(cv_job* j);

static void worker_fbd(cv_job* j) {
    cv_map m;
    if (!cv_map_open(&m, j->path)) { snprintf(j->err, sizeof j->err, "cannot open %s", j->path); return; }
    bool ok = cv_fbd_parse(&j->fbd, m.data, m.size);
    cv_map_close(&m);
    if (!ok) { snprintf(j->err, sizeof j->err, "out of memory reading %s", j->path); return; }
    j->has_fbd = true;
    if (j->fbd.needs_cgx && j->eval_cgx) fbd_eval_cgx(j);
    if (!j->has_deck && j->fbd.msh) {
        if (cv_inp_parse(&j->deck, j->fbd.msh, j->fbd.msh_n, NULL, NULL) && j->deck.mesh.n_nodes) {
            geo_sets_into_deck(&j->fbd, &j->deck);
            snprintf(j->deck_path, sizeof j->deck_path, "%s", j->path);
            j->has_deck = true;
        } else {
            cv_inp_free(&j->deck); free(j->deck.msgs.a); memset(&j->deck, 0, sizeof j->deck);
        }
    }
    free(j->fbd.msh); j->fbd.msh = NULL; j->fbd.msh_n = 0;
}

static void fbd_eval_cgx(cv_job* j) {
    char cgx[1024];
    if (!cv_cgx_find(cgx, sizeof cgx)) {
        snprintf(j->cgx_log, sizeof j->cgx_log, "cgx not found: put it on PATH or set CCXVIEW_CGX to it");
        return;
    }
    cv_cgx_result r;
    if (!cv_cgx_eval(cgx, j->path, &r)) {
        snprintf(j->cgx_log, sizeof j->cgx_log, "%s\n%s", r.err, r.log);
        cv_cgx_result_free(&r);
        return;
    }
    cv_fbd e;
    if (cv_fbd_parse(&e, r.fbd, r.fbd_n)) {
        cv_fbd_free(&j->fbd); free(j->fbd.msgs.a);
        j->fbd = e;
        j->fbd_evaluated = true;
    } else {
        cv_fbd_free(&e); free(e.msgs.a);
    }
    if (r.msh && cv_inp_parse(&j->deck, r.msh, r.msh_n, NULL, NULL) && j->deck.mesh.n_nodes) {
        geo_sets_into_deck(&j->fbd, &j->deck);
        snprintf(j->deck_path, sizeof j->deck_path, "%s", j->path);
        j->has_deck = true;
    } else {
        cv_inp_free(&j->deck); free(j->deck.msgs.a); memset(&j->deck, 0, sizeof j->deck);
    }
    cv_cgx_result_free(&r);
}

/* .inp and .frd open each other: whichever is opened, the other one beside it
   (same name) joins in. Geometry and results come from the .frd when there is
   one; the deck contributes its sets, surfaces and material names. */
static void worker_load(void* p) {
    cv_job* j = p;
    double t0 = cv_now();
    cv_log_set_context(j->path);
    cv_logf("opening %s", j->path);
    j->ok = false;
    char frd_path[1024] = "";
    j->has_deck = false;
    j->deck_geometry = false;
    j->has_fbd = false;
    j->fbd_evaluated = false;
    j->cgx_log[0] = 0;
    if (cv_ends_with_ci(j->path, ".fbd")) {
        worker_fbd(j);
        if (!j->has_fbd) { job_finish(j, t0); return; }
    } else if (cv_ends_with_ci(j->path, ".inp")) {
        snprintf(j->deck_path, sizeof j->deck_path, "%s", j->path);
        if (!deck_read(j->path, &j->deck)) {
            snprintf(j->err, sizeof j->err, "cannot read %s", j->path);
            job_finish(j, t0);
            return;
        }
        j->has_deck = true;
        if (!deck_sibling(j->path, ".frd", frd_path, sizeof frd_path)) frd_path[0] = 0;
    } else {
        snprintf(frd_path, sizeof frd_path, "%s", j->path);
        if (deck_sibling(j->path, ".inp", j->deck_path, sizeof j->deck_path))
            j->has_deck = deck_read(j->deck_path, &j->deck);
    }

    if (frd_path[0]) {
        if (!cv_map_open(&j->map, frd_path)) {
            snprintf(j->err, sizeof j->err, "cannot open %s", frd_path);
        } else if (!cv_frd_parse(&j->frd_out, j->map.data, j->map.size)) {
            snprintf(j->err, sizeof j->err, "out of memory reading %s", frd_path);
        } else {
            j->ok = true;
        }
    } else if (j->has_deck) {                  /* unsolved deck: its own mesh */
        j->frd_out = j->deck.mesh;
        memset(&j->deck.mesh, 0, sizeof j->deck.mesh);
        j->deck_geometry = true;
        j->ok = true;
    } else if (j->has_fbd) {                   /* geometry only, no mesh */
        j->ok = true;
    }
    if (j->ok && (!cv_groups_build(&j->groups, &j->frd_out) || !cv_skin_build_opt(&j->skin, &j->frd_out, NULL, j->crease, j->mid))) {
        snprintf(j->err, sizeof j->err, "out of memory building the surface");
        j->ok = false;
    }
    if (j->ok && frd_path[0]) {
        cv_map dm;
        if (gp_sibling(frd_path, j->dat_path, sizeof j->dat_path) && cv_map_open(&dm, j->dat_path)) {
            j->has_dat = cv_dat_parse(&j->dat, dm.data, dm.size) && j->dat.n > 0;
            cv_map_close(&dm);
        }
        char sp[1024];                          /* the run's convergence history */
        if (deck_sibling(frd_path, ".sta", sp, sizeof sp) && cv_map_open(&dm, sp)) { cv_sta_parse(&j->sta, dm.data, dm.size); cv_map_close(&dm); }
        if (deck_sibling(frd_path, ".cvg", sp, sizeof sp) && cv_map_open(&dm, sp)) { cv_cvg_parse(&j->sta, dm.data, dm.size); cv_map_close(&dm); }
    }
    job_finish(j, t0);
}

static void worker_skin(void* p) {
    cv_job* j = p;
    double t0 = cv_now();
    if (j->crop) cv_crop_mask(j->frd, j->crop_lo, j->crop_hi, j->vis);
    if (j->eye_node) cv_node_mask(j->frd, j->eye_node, j->vis);
    j->ok = cv_skin_build_opt(&j->skin, j->frd, j->vis, j->crease, j->mid);
    if (!j->ok) snprintf(j->err, sizeof j->err, "out of memory building the surface");
    job_finish(j, t0);
}

bool app_busy(void) { return G.job.kind != JOB_NONE; }

static bool job_start(int kind, void (*fn)(void*)) {
    cv_job* j = &G.job;
    j->kind = kind;
    j->done = false;
    j->err[0] = 0;
    j->started = cv_now();
    if (!cv_thread_start(&j->thread, fn, j)) {
        j->kind = JOB_NONE;
        cv_msg_add(&G.msgs, 0, false, "could not start a worker thread");
        return false;
    }
    return true;
}

static bool job_done(void) {
    cv_mutex_lock(&G.job.lock);
    bool d = G.job.done;
    cv_mutex_unlock(&G.job.lock);
    return d;
}

void unload(void) {
    app_path_clear();
    app_hist_close();
    app_lin_close();
    app_compare_close();
    cache_clear();
    cv_calc_free(G.calc); G.calc = NULL;    /* its formula stays, for the next file */
    gp_clear();
    deck_clear();
    fail_clear();
    mesh_clear();
    cv_skin_free(&G.skin);
    cv_groups_free(&G.groups);
    cv_frd_free(&G.frd);
    free(G.frd.msgs.a);
    memset(&G.frd.msgs, 0, sizeof G.frd.msgs);
    cv_map_close(&G.map);
    free(G.scalar); free(G.elem_val); free(G.tri_val); free(G.disp); free(G.disp2);
    G.scalar = G.elem_val = G.tri_val = G.disp = G.disp2 = NULL;
    G.harmonic = false;
    for (int a = 0; a < CV_AXIS_N; a++) { free(G.axis_rgb[a]); G.axis_rgb[a] = NULL; }
    geo_clear();
    cv_render_clear_model();
    cv_sta_free(&G.sta);
    G.loaded = false;
    G.has_field = false;
    G.probe_on = false;
    app_sel_clear();
    G.playing = false;
    G.skin_dirty = false;
    G.field_name[0] = G.field_label[0] = 0;
    for (int k = 0; k < 3; k++) G.legend_lines[k][0] = 0;
}

/* Default: von Mises if there is stress, else |DISP|, else the first field. */
static void pick_default_field(void) {
    G.field_name[0] = 0;
    const cv_step* s = &G.frd.steps[G.step];
    const char* pref[] = { "STRESS", "DISP", "NDTEMP" };
    for (int p = 0; p < 3 && !G.field_name[0]; p++)
        for (int i = 0; i < s->nfields; i++)
            if (strcmp(s->fields[i].name, pref[p]) == 0) {
                cv_scalar_opt o[CV_MAX_OPTS];
                cv_field_options(&s->fields[i], o, CV_MAX_OPTS);
                snprintf(G.field_name, sizeof G.field_name, "%s", pref[p]);
                G.comp = o[0].comp;
                break;
            }
    if (!G.field_name[0] && s->nfields) {
        cv_scalar_opt o[CV_MAX_OPTS];
        cv_field_options(&s->fields[0], o, CV_MAX_OPTS);
        snprintf(G.field_name, sizeof G.field_name, "%s", s->fields[0].name);
        G.comp = o[0].comp;
    }
}

static void apply_load(cv_job* j) {
    unload();
    if (!j->ok) {
        cv_msg_add(&G.msgs, 0, false, j->err);
        if (j->has_deck) { cv_inp_free(&j->deck); free(j->deck.msgs.a); memset(&j->deck, 0, sizeof j->deck); j->has_deck = false; }
        G.frd.msgs = j->frd_out.msgs;          /* keep what the parser had to say */
        memset(&j->frd_out.msgs, 0, sizeof j->frd_out.msgs);
        cv_frd_free(&j->frd_out);
        cv_groups_free(&j->groups);
        cv_skin_free(&j->skin);
        cv_map_close(&j->map);
        if (j->has_fbd) { cv_fbd_free(&j->fbd); free(j->fbd.msgs.a); memset(&j->fbd, 0, sizeof j->fbd); j->has_fbd = false; }
        G.show_msgs = true;
        return;
    }
    if (j->has_deck) {
        /* the deck's own mesh (if it was the geometry) now lives in frd_out */
        /* with a .frd the deck keeps its own mesh: its node coordinates find the
           .frd nodes of expanded shells, which CalculiX renumbers */
        deck_set(&j->deck, j->deck_path);
    }
    j->has_deck = false;
    if (j->cgx_log[0]) {                       /* cgx's own words, line by line */
        char* t = j->cgx_log;
        while (t && *t) {
            char* nl = strchr(t, '\n');
            if (nl) *nl = 0;
            if (*t) cv_msg_add(&G.msgs, 0, false, t);
            t = nl ? nl + 1 : NULL;
        }
        G.show_msgs = true;
    }
    if (j->has_fbd) {
        bool script = j->fbd.needs_cgx && !j->fbd_evaluated;
        char why[160];
        snprintf(why, sizeof why, "a cgx script (%s): Evaluate with cgx to see what it builds", j->fbd.needs_why);
        geo_set(&j->fbd, j->fbd_evaluated);
        if (script) cv_msg_add(&G.msgs, 0, false, why);
        G.show_geo_pts = G.show_geo_crv = true;
    }
    j->has_fbd = false;
    cv_sta_free(&G.sta);
    G.sta = j->sta; memset(&j->sta, 0, sizeof j->sta);
    if (j->has_dat) gp_set(&j->dat, j->dat_path);
    else { cv_dat_free(&j->dat); free(j->dat.msgs.a); memset(&j->dat, 0, sizeof j->dat); }
    j->has_dat = false;
    G.map = j->map;         memset(&j->map, 0, sizeof j->map);
    G.frd = j->frd_out;     memset(&j->frd_out, 0, sizeof j->frd_out);
    G.groups = j->groups;   memset(&j->groups, 0, sizeof j->groups);
    G.skin = j->skin;       memset(&j->skin, 0, sizeof j->skin);
    gp_localize();                             /* needs the deck and G.frd */
    snprintf(G.path, sizeof G.path, "%s", j->path);
    G.load_seconds = j->seconds;
    settings_add_recent(j->path);
    cv_logf("loaded %s: %u nodes, %u elements, %d steps, %zu skin triangles, %zu messages, %.3f s",
            cv_basename(j->path), G.frd.n_nodes, G.frd.n_elems, G.frd.n_steps, G.skin.n_tri, app_total_msgs(), j->seconds);
    cv_log_set_context(NULL);

    uint32_t N = G.frd.n_nodes;
    G.bmin = v3_make(INFINITY, INFINITY, INFINITY);
    G.bmax = v3_make(-INFINITY, -INFINITY, -INFINITY);
    for (uint32_t i = 0; i < N; i++) {
        const float* p = G.frd.xyz + 3 * i;
        G.bmin.x = fminf(G.bmin.x, p[0]); G.bmax.x = fmaxf(G.bmax.x, p[0]);
        G.bmin.y = fminf(G.bmin.y, p[1]); G.bmax.y = fmaxf(G.bmax.y, p[1]);
        G.bmin.z = fminf(G.bmin.z, p[2]); G.bmax.z = fmaxf(G.bmax.z, p[2]);
    }
    if (!geo_bounds(&G.bmin, &G.bmax) && N == 0) { G.bmin = v3_make(-1, -1, -1); G.bmax = v3_make(1, 1, 1); }
    v3 ext = v3_sub(G.bmax, G.bmin);
    G.diag = sqrtf(v3_dot(ext, ext));
    if (!(G.diag > 0)) G.diag = 1;

    G.scalar = malloc((size_t)CV_MAX(N, 1) * sizeof(float));
    G.elem_val = malloc((size_t)CV_MAX(G.frd.n_elems, 1) * sizeof(float));
    if (!G.scalar || !G.elem_val) cv_msg_add(&G.msgs, 0, false, "out of memory: fields cannot be shown");

    cv_render_positions(G.frd.xyz, N);
    cv_render_indices(G.skin.tri, G.skin.n_tri, G.skin.edge, G.skin.n_edge, G.skin.pt, G.skin.n_pt);
    cv_render_outline(G.skin.fedge, G.skin.n_fedge);
    G.loaded = true;
    G.file_bytes = G.map.size;
    G.field_src = 0;
    free(G.vis); G.vis = NULL;
    G.eye_hide_on = false;
    free(G.hide); G.hide = NULL;
    G.menu_on = false;
    G.crop_on = false;
    for (int k = 0; k < 3; k++) { G.crop_lo[k] = 0.f; G.crop_hi[k] = 1.f; }
    init_group_colors();
    if (O.faces >= 0) G.faces_mode = O.faces;
    refresh_tri_colors();

    /* mean edge length from a sample of skin edges */
    {
        double sum = 0; size_t n = 0, stride = G.skin.n_edge / 20000 + 1;
        for (size_t e = 0; e < G.skin.n_edge; e += stride, n++) {
            const float *a = G.frd.xyz + 3 * G.skin.edge[2 * e], *b = G.frd.xyz + 3 * G.skin.edge[2 * e + 1];
            float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
            sum += sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);
        }
        G.mean_edge = n ? (float)(sum / n) : 0.f;
    }
    /* Symbol size: 2.5 % of the diagonal of the meshed part (a far reference node
       or geometry would stretch the whole-model diagonal), so supports and arrows
       look the same on every model whatever its mesh; or the size set by hand. */
    {
        float lo[3] = { INFINITY, INFINITY, INFINITY }, hi[3] = { -INFINITY, -INFINITY, -INFINITY };
        for (size_t i = 0; i < G.skin.n_pt; i++) {
            const float* p = G.frd.xyz + 3 * G.skin.pt[i];
            for (int k = 0; k < 3; k++) { lo[k] = fminf(lo[k], p[k]); hi[k] = fmaxf(hi[k], p[k]); }
        }
        float d = 0;
        if (G.skin.n_pt) for (int k = 0; k < 3; k++) d += (hi[k] - lo[k]) * (hi[k] - lo[k]);
        d = d > 0 ? sqrtf(d) : G.diag;
        G.sym_auto_len = 0.025f * d;
        G.sym_len = G.sym_auto || !(G.sym_size > 0) ? G.sym_auto_len : G.sym_size;
    }

    /* Auto deformation from the last step that has DISP (usually the largest).
       The same decode catches a diverged run: a result that carries the model
       more than 10^4 of its own size away only produces an empty window, so say
       so and start undeformed; Deform can still be switched on. Only this one
       field is decoded at load; everything else waits until it is shown. */
    G.auto_scale = 1.f;
    G.deform = true;                     /* per file: a broken previous file must not stick */
    for (int s = G.frd.n_steps - 1; s >= 0; s--) {
        int fi = find_field(s, "DISP");
        if (fi < 0 || G.frd.steps[s].modal) continue;      /* mode shapes scale per step */
        const float* v = cache_get(s, fi);
        int nc = G.frd.steps[s].fields[fi].ncomp;
        if (v && nc >= 3) {
            float peak = 0, raw = units_len_raw();   /* in model units, like the mesh */
            for (uint32_t i = 0; i < N; i++) {
                const float* r = v + (size_t)i * nc;
                float m = raw * sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
                if (m == m && m > peak) peak = m;
            }
            G.auto_scale = cv_auto_deform(peak, G.diag);
            if (peak > 1e4f * G.diag) {
                char msg[160];
                snprintf(msg, sizeof msg, "displacements reach %.3g, %.0fx the model size: shown undeformed "
                         "(diverged run?); tick Deform to see them", peak, peak / G.diag);
                cv_msg_add(&G.msgs, 0, false, msg);
                G.deform = false;
            }
        }
        break;
    }
    if (G.deform_auto) G.deform_scale = G.auto_scale;

    G.vmin = G.bmin; G.vmax = G.bmax; G.vdiag = G.diag;
    for (int k = 0; k < 3; k++) {         /* symmetry belongs to a model: start without */
        if (!G.reload_keep) { G.sym[k] = O.mirror[k]; G.sym_at[k] = sym_auto(k); }
        if (!G.reload_keep) G.rep[k] = O.rep[k];    /* so do its copies; a reload keeps them */
    }
    /* springs, beams, lone nodes: no faces to draw, so show the nodes, bigger */
    if (G.skin.n_tri == 0 && N) { G.show_nodes = true; G.point_size = CV_MAX(G.point_size, 8.f); }
    G.show_geo_srf = G.skin.n_tri == 0;  /* a mesh covers its surfaces: they would only fight it */
    G.step = 0;                          /* open on the first increment */
    if (G.frd.n_steps > 0) {
        if (G.reload_keep && G.keep_field[0]) {          /* a reload: where the user was */
            snprintf(G.field_name, sizeof G.field_name, "%s", G.keep_field);
            G.comp = G.keep_comp;
            G.step = CV_MIN(G.keep_step, G.frd.n_steps - 1);
            if (!strcmp(G.keep_field, "=")) {            /* the calculated field */
                G.field_name[0] = 0;
                if (!app_calc_set(G.calc_expr)) pick_default_field();
            }
        } else {
            pick_default_field();
        }
        G.range_lock = false;
        app_set_step(G.step);
    } else {
        refresh_field();
        deck_refresh_highlight();             /* no steps: an unsolved deck shows its last step's loads */
    }
    if (G.reload_keep) { view_bounds(); G.cam = G.keep_cam; G.reload_keep = false; }
    else app_view(CV_VIEW_ISO);
    G.watch_mtime = cv_file_mtime(G.path); G.watch_size = cv_file_size(G.path); G.watch_t = cv_now();
    if (O.fly) app_set_flight(true);
    if (O.mesh_window) G.show_mesh = G.mesh_limits_open = true;
    if (O.details) G.show_details = true;
    if (O.about) G.show_about = true;
    if (O.labels && !G.reload_keep) {
        int k = app_label_find(O.labels);
        if (k < 0) cv_msg_add(&G.msgs, 0, false, "--labels: unknown kind (node, elem, value, evalue, sets, links, loads, supports, materials)");
        else G.label_kind = k;
    }
    app_label_changed();
    if (O.fly_clip) { G.fly_clip = O.fly_clip; if (O.fly_depth > 0) G.fly_clip_depth = O.fly_depth; }
    if (O.view_file && !G.reload_keep) app_view_load(O.view_file);
    if (O.compare) app_compare_open(O.compare);
    for (int q = 0; q < 2; q++) {        /* --path / --linearize A,B or A,normal|x|y|z */
        const char* s = q ? O.lin_ids : O.path_ids;
        unsigned a;
        char w[16];
        if (!s || sscanf(s, "%u,%15s", &a, w) != 2) continue;
        int dir = !strcmp(w, "normal") ? 1 : !strcmp(w, "x") ? 2 : !strcmp(w, "y") ? 3 : !strcmp(w, "z") ? 4 : 0;
        uint32_t na = cv_frd_node_index(&G.frd, a), nb = dir ? 0 : cv_frd_node_index(&G.frd, (uint32_t)strtoul(w, NULL, 10));
        if (na == UINT32_MAX || nb == UINT32_MAX || (!dir && na == nb)) { cv_msg_add(&G.msgs, 0, false, q ? "--linearize: node not in this model" : "--path: node not in this model"); continue; }
        G.path_lin = q == 1;
        if (q) G.path_surface = false;
        if (dir) app_path_ray(na, dir);
        else { app_path_start(na); app_path_end(nb); }
    }
    if (O.hist_id > 0) {
        uint32_t n = cv_frd_node_index(&G.frd, (uint32_t)O.hist_id);
        if (n != UINT32_MAX) app_hist_open(n, UINT32_MAX);
        else cv_msg_add(&G.msgs, 0, false, "--history: node not in this model");
    }
    if (O.find) {
        bool el = O.find[0] == 'e' || O.find[0] == 'E';
        if (!app_find((uint32_t)strtoul(O.find + (el ? 1 : 0), NULL, 10), el)) cv_msg_add(&G.msgs, 0, false, "--find: not in this model");
        else { G.label_probe_only = true; if (G.label_kind == CV_LABEL_NONE) G.label_kind = CV_LABEL_NODE; app_label_changed(); }
    }
    if (O.nsets && deck_loaded()) {
        const cv_inp* dk = deck_get();
        for (int k = 0; k < O.nsets; k++) {
            for (int i = 0; i < dk->nsets; i++) if (!strcasecmp(dk->sets[i].name, O.sets[k])) deck_set_flags()[i] = true;
            for (int i = 0; i < dk->nsurfs; i++) if (!strcasecmp(dk->surfs[i].name, O.sets[k])) deck_surf_flags()[i] = true;
        }
        deck_refresh_highlight();
        app_groups_changed();
    }
    if (O.step) { app_set_step(O.step < 0 ? G.frd.n_steps - 1 : O.step - 1); app_fit(); }
    if (O.export_kind == 1) app_export_png();
    else if (O.export_kind == 3) app_export_data(false);
    else if (O.export_kind == 4) app_export_data(true);
    else if (O.export_kind >= 5) {                 /* seq / mp4 / seqsteps / mp4steps */
        G.exp_video = O.export_kind == 6 || O.export_kind == 8;
        G.exp_kind = O.export_kind >= 7;
        app_export_animation();
    }
    if (O.conv) G.show_conv = true;
    if (O.look >= 0) app_view(O.look);
    if (O.target_set) G.cam.target = v3_make(O.target[0], O.target[1], O.target[2]);
    if (O.zoom > 0) G.cam.dist /= O.zoom;
    if (O.field && G.frd.n_steps > 0) {
        int fi = find_field(G.step, O.field);
        if (fi >= 0) {
            cv_scalar_opt o[CV_MAX_OPTS];
            if (cv_field_options(&G.frd.steps[G.step].fields[fi], o, CV_MAX_OPTS) > 0) app_select_src(O.field, o[0].comp, 0);
        }
    }
    if (O.calc && !G.reload_keep && !app_calc_set(O.calc)) {
        char m[200];
        snprintf(m, sizeof m, "--calc: %s", G.calc_err);
        cv_msg_add(&G.msgs, 0, false, m);
    }
    if (O.fail && !G.reload_keep) {
        static const char* const outs[CV_FO_N] = { "exposure", "rf", "fi", "mode", "angle", "fibre", "matrix" };
        char crit[32];
        const char* colon = strchr(O.fail, ':');
        size_t n = colon ? (size_t)(colon - O.fail) : strlen(O.fail);
        snprintf(crit, sizeof crit, "%.*s", (int)CV_MIN(n, sizeof crit - 1), O.fail);
        int c = -1, o = colon ? -1 : CV_FO_EXPOSURE;
        for (int k = 0; k < CV_FC_N; k++)
            if (!strcasecmp(crit, cv_fc_name(k)) || !strcasecmp(crit, cv_fc_title(k))) c = k;
        for (int k = 0; colon && k < CV_FO_N; k++)
            if (!strcasecmp(colon + 1, outs[k])) o = k;
        if (c < 0 || o < 0) {
            char m[200];
            snprintf(m, sizeof m, "--fail %s: criterion maxstress|tsaihill|tsaiwu|hashin|puck|larc03|larc05|"
                     "mises|tresca|mohr|auto, output exposure|rf|fi|mode|angle|fibre|matrix", O.fail);
            cv_msg_add(&G.msgs, 0, false, m);
        } else app_fail_set(c, o);
    }
    if (O.mesh && !G.reload_keep) {
        int q = cv_mq_find(O.mesh);
        if (q < 0) {
            char m[200];
            snprintf(m, sizeof m, "--mesh %s: size|edgemin|edgemax|aspect|sjac|jratio|skew|anglemin|anglemax|warp|shape", O.mesh);
            cv_msg_add(&G.msgs, 0, false, m);
        } else app_mesh_set(q);
    }
    if (O.gauss && gp_loaded()) {
        for (int st = 0; st < G.frd.n_steps; st++) {
            G.step = st;
            const char* names[4];
            if (gp_fields(names, 4) > 0) {
                cv_field_desc d;
                cv_scalar_opt o[CV_MAX_OPTS];
                if (gp_desc(names[0], &d) && cv_field_options(&d, o, CV_MAX_OPTS) > 0) {
                    app_set_step(st);
                    app_select_src(names[0], o[0].comp, 1);
                }
                break;
            }
        }
    }
    if (O.crop_set) {
        G.crop_on = true;
        for (int k = 0; k < 3; k++) { G.crop_lo[k] = O.crop[2 * k]; G.crop_hi[k] = O.crop[2 * k + 1]; }
        app_groups_changed();
    }
}

static void start_skin_job(void) {
    free(G.job.vis);
    G.job.vis = malloc(CV_MAX(G.frd.n_elems, 1));
    if (!G.job.vis) { cv_msg_add(&G.msgs, 0, false, "out of memory"); return; }
    cv_groups_mask(&G.groups, G.frd.n_elems, G.job.vis);
    deck_apply_mask(&G.frd, G.job.vis);        /* ticked element sets: a display group */
    for (uint32_t e = 0; G.hide && e < G.frd.n_elems; e++) if (G.hide[e]) G.job.vis[e] = 0;   /* hidden by hand */
    G.job.crop = G.crop_on;
    const float lo[3] = { G.bmin.x, G.bmin.y, G.bmin.z }, hi[3] = { G.bmax.x, G.bmax.y, G.bmax.z };
    for (int k = 0; k < 3; k++) {           /* fractions -> world, padded so 0/1 keep the edges */
        float ext = hi[k] - lo[k], pad = ext * 1e-4f + 1e-12f;
        G.job.crop_lo[k] = G.crop_lo[k] <= 0.f ? lo[k] - pad : lo[k] + ext * G.crop_lo[k];
        G.job.crop_hi[k] = G.crop_hi[k] >= 1.f ? hi[k] + pad : lo[k] + ext * G.crop_hi[k];
    }
    free(G.job.eye_node); G.job.eye_node = NULL;   /* the nodes beyond the eye's plane, flagged here: */
    if (G.eye_hide_on && (G.job.eye_node = malloc(CV_MAX(G.frd.n_nodes, 1))))   /* the displacement may change meanwhile */
        cv_plane_nodes(&G.frd, G.disp, G.eye_f1, G.disp2, G.eye_f2, G.eye_n, G.eye_d, G.job.eye_node);
    G.job.frd = &G.frd;
    G.job.crease = G.outline_angle;
    G.job.mid = G.mid_faces;
    G.skin_dirty = false;
    job_start(JOB_SKIN, worker_skin);
}

void app_groups_changed(void) {
    if (!G.loaded) return;
    if (app_busy()) { G.skin_dirty = true; return; }
    G.job.eye_only = false;
    start_skin_job();
}

/* a new skin when the plane moved enough to matter (and none is being built): the
   hidden set follows the eye at the pace the skin can be built */
void app_eye_hide(bool on, const float n[3], float d, float f1, float f2) {
    if (!G.loaded) return;
    if (!on) { if (G.eye_hide_on) { G.eye_hide_on = false; app_groups_changed(); } return; }
    if (app_busy()) return;
    if (G.eye_hide_on && fabsf(d - G.eye_d) < 2e-3f * G.diag && f1 == G.eye_f1 && f2 == G.eye_f2 &&
        n[0] * G.eye_n[0] + n[1] * G.eye_n[1] + n[2] * G.eye_n[2] > 0.9998f) return;
    G.eye_hide_on = true;
    memcpy(G.eye_n, n, sizeof G.eye_n); G.eye_d = d; G.eye_f1 = f1; G.eye_f2 = f2;
    G.job.eye_only = true;
    start_skin_job();
}

void poll_job(void) {
    if (!app_busy() || !job_done()) return;
    cv_job* j = &G.job;
    cv_thread_join(&j->thread);
    int kind = j->kind;
    j->kind = JOB_NONE;
    if (kind == JOB_LOAD) {
        apply_load(j);
    } else if (kind == JOB_SKIN) {
        if (j->ok) {
            cv_skin_free(&G.skin);
            G.skin = j->skin;
            memset(&j->skin, 0, sizeof j->skin);
            cv_render_indices(G.skin.tri, G.skin.n_tri, G.skin.edge, G.skin.n_edge, G.skin.pt, G.skin.n_pt);
            cv_render_outline(G.skin.fedge, G.skin.n_fedge);
            free(G.vis);
            G.vis = j->vis;                       /* the mask this skin was built from */
            j->vis = NULL;
            refresh_tri_values();
            refresh_tri_colors();
            refresh_gauss();
            refresh_vectors();
            refresh_tensors();
            refresh_traj();
            if (!j->eye_only) {                   /* flying through: the legend and the probe hold */
                app_refresh_range();              /* the legend covers what is shown */
                G.probe_on = false;
                app_sel_clear();                  /* it may hold elements now hidden */
            }
            deck_refresh_highlight();
            app_label_changed();                  /* other nodes and faces to label */
        } else {
            cv_msg_add(&G.msgs, 0, false, j->err);
        }
        free(j->eye_node); j->eye_node = NULL;
        if (G.skin_dirty) { j->eye_only = false; start_skin_job(); }
    }
}

/* A .dat on its own: attach it to the open model, or open the .frd beside it. */
static void open_dat(const char* path) {
    if (!G.loaded) {
        char frd[1024];
        snprintf(frd, sizeof frd, "%s", path);
        memcpy(frd + strlen(frd) - 4, frd[strlen(frd) - 3] == 'D' ? ".FRD" : ".frd", 4);
        if (cv_file_size(frd) > 0) app_open(frd);
        else cv_msg_add(&G.msgs, 0, false, "open the .frd first; a .dat has no geometry");
        return;
    }
    cv_map m;
    cv_dat d;
    if (!cv_map_open(&m, path)) { cv_msg_add(&G.msgs, 0, false, "cannot open the .dat file"); return; }
    cv_dat_parse(&d, m.data, m.size);
    cv_map_close(&m);
    if (d.n == 0) {
        cv_msg_add(&G.msgs, 0, false, "the .dat has no integration-point output (*EL PRINT)");
        cv_dat_free(&d); free(d.msgs.a);
        return;
    }
    gp_set(&d, path);
    gp_localize();
    if (G.field_src == 1) refresh_field();
}

void app_open(const char* path) {
    if (!path || !path[0]) return;
    if (cv_ends_with_ci(path, ".dat")) { open_dat(path); return; }
    if (app_busy()) {               /* finish whatever runs; a load supersedes it */
        cv_thread_join(&G.job.thread);
        if (G.job.kind == JOB_LOAD) {
            cv_fbd_free(&G.job.fbd); free(G.job.fbd.msgs.a); memset(&G.job.fbd, 0, sizeof G.job.fbd);
            cv_frd_free(&G.job.frd_out); free(G.job.frd_out.msgs.a);
            cv_groups_free(&G.job.groups); cv_map_close(&G.job.map);
            cv_inp_free(&G.job.deck); free(G.job.deck.msgs.a);
            cv_dat_free(&G.job.dat); free(G.job.dat.msgs.a);
            cv_sta_free(&G.job.sta);
        }
        cv_skin_free(&G.job.skin);
        G.job.kind = JOB_NONE;
    }
    free(G.msgs.a);
    memset(&G.msgs, 0, sizeof G.msgs);
    snprintf(G.job.path, sizeof G.job.path, "%s", path);
    snprintf(G.open_buf, sizeof G.open_buf, "%s", path);
    G.open_len = (int)strlen(G.open_buf);
    memset(&G.job.frd_out, 0, sizeof G.job.frd_out);
    memset(&G.job.dat, 0, sizeof G.job.dat);
    G.job.has_dat = false;
    cv_sta_free(&G.job.sta);
    memset(&G.job.deck, 0, sizeof G.job.deck);
    G.job.has_deck = false;
    G.job.crease = G.outline_angle;
    G.job.mid = G.mid_faces;
    G.job.eval_cgx = O.cgx || O.eval_next;
    O.eval_next = false;
    job_start(JOB_LOAD, worker_load);
}

void app_reload(void) {
    if (!G.loaded || app_busy()) return;
    G.reload_keep = true;
    G.keep_step = G.step;
    G.keep_comp = G.comp;
    snprintf(G.keep_field, sizeof G.keep_field, "%s", G.field_src == 0 ? G.field_name : G.field_src == 2 ? "=" : "");
    G.keep_cam = G.cam;
    char p[1024];
    snprintf(p, sizeof p, "%s", G.path);
    app_open(p);
}

/* ---- view state: camera, step, field and the layer toggles, as a small INI
   beside the model, so a picture can be reproduced later or by someone else ---- */
bool app_view_save(const char* path) {
    if (!G.loaded) return false;
    cv_cfg c;
    cv_cfg_load(&c, path);
    cv_cfg_set_float(&c, "cam_yaw", G.cam.yaw); cv_cfg_set_float(&c, "cam_pitch", G.cam.pitch);
    cv_cfg_set_float(&c, "cam_dist", G.cam.dist); cv_cfg_set_bool(&c, "cam_ortho", G.cam.ortho);
    cv_cfg_set_bool(&c, "cam_up_z", G.up_z);           /* yaw and pitch are about this axis */
    cv_cfg_set_bool(&c, "cam_free", G.orbit_free);     /* free orbit: the direction and up below rule */
    cv_cfg_set_float(&c, "cam_dx", G.cam.fdir.x); cv_cfg_set_float(&c, "cam_dy", G.cam.fdir.y); cv_cfg_set_float(&c, "cam_dz", G.cam.fdir.z);
    cv_cfg_set_float(&c, "cam_ux", G.cam.fup.x); cv_cfg_set_float(&c, "cam_uy", G.cam.fup.y); cv_cfg_set_float(&c, "cam_uz", G.cam.fup.z);
    cv_cfg_set_float(&c, "cam_x", G.cam.target.x); cv_cfg_set_float(&c, "cam_y", G.cam.target.y); cv_cfg_set_float(&c, "cam_z", G.cam.target.z);
    cv_cfg_set_int(&c, "step", G.step + 1);
    cv_cfg_set(&c, "field", G.field_src == 0 ? G.field_name : "");
    cv_cfg_set(&c, "calc", G.field_src == 2 ? G.calc_expr : "");
    cv_cfg_set_int(&c, "comp", G.comp);
    cv_cfg_set_bool(&c, "elem_mode", G.elem_mode);
    cv_cfg_set_int(&c, "csys", G.csys);
    cv_cfg_set_float(&c, "csys_x", G.csys_o[0]); cv_cfg_set_float(&c, "csys_y", G.csys_o[1]); cv_cfg_set_float(&c, "csys_z", G.csys_o[2]);
    cv_cfg_set_bool(&c, "deform", G.deform); cv_cfg_set_float(&c, "deform_scale", G.deform_scale);
    cv_cfg_set_bool(&c, "range_lock", G.range_lock); cv_cfg_set_float(&c, "rmin", G.rmin); cv_cfg_set_float(&c, "rmax", G.rmax);
    cv_cfg_set_bool(&c, "clip_on", G.clip_on); cv_cfg_set_int(&c, "clip_axis", G.clip_axis);
    cv_cfg_set_bool(&c, "clip_flip", G.clip_flip); cv_cfg_set_float(&c, "clip_pos", G.clip_pos);
    cv_cfg_set_bool(&c, "clip_cap", G.clip_cap);
    cv_cfg_set_bool(&c, "crop_on", G.crop_on);
    for (int k = 0; k < 3; k++) {
        char key[16];
        snprintf(key, sizeof key, "crop_lo%d", k); cv_cfg_set_float(&c, key, G.crop_lo[k]);
        snprintf(key, sizeof key, "crop_hi%d", k); cv_cfg_set_float(&c, key, G.crop_hi[k]);
        snprintf(key, sizeof key, "mirror%d", k); cv_cfg_set_bool(&c, key, G.sym[k]);
        snprintf(key, sizeof key, "mirror_at%d", k); cv_cfg_set_int(&c, key, G.sym_at[k]);
        snprintf(key, sizeof key, "rep%d", k); cv_cfg_set_bool(&c, key, G.rep[k]);
        snprintf(key, sizeof key, "rep_n%d", k); cv_cfg_set_int(&c, key, G.rep_n[k]);
        snprintf(key, sizeof key, "rep_gap%d", k); cv_cfg_set_float(&c, key, G.rep_gap[k]);
        if (k == 0) cv_cfg_set_bool(&c, "rep_follow", G.rep_follow);
        snprintf(key, sizeof key, "cyc_o%d", k); cv_cfg_set_float(&c, key, G.cyc_o[k]);
    }
    cv_cfg_set_bool(&c, "cyc_on", G.cyc_on); cv_cfg_set_int(&c, "cyc_n", G.cyc_n);
    cv_cfg_set_int(&c, "cyc_show", G.cyc_show); cv_cfg_set_int(&c, "cyc_axis", G.cyc_axis);
    const char* layer_keys[] = { "show_faces", "show_edges", "show_nodes", "show_gp", "show_vec", "show_tensor", "show_traj", "show_markers", "show_ghost", "shading" };
    const bool  layer_vals[] = { G.show_faces, G.show_edges, G.show_nodes, G.show_gp, G.show_vec, G.show_tensor, G.show_traj, G.show_markers, G.show_ghost, G.shading };
    for (size_t i = 0; i < CV_COUNT(layer_keys); i++) cv_cfg_set_bool(&c, layer_keys[i], layer_vals[i]);
    cv_cfg_set_int(&c, "tensor_style", G.tensor_style); cv_cfg_set_int(&c, "traj_which", G.traj_which);
    cv_cfg_set_int(&c, "faces_mode", G.faces_mode); cv_cfg_set_int(&c, "cmap", G.cmap); cv_cfg_set_int(&c, "bands", G.bands);
    bool ok = cv_cfg_save(&c);
    cv_cfg_free(&c);
    if (ok) CV_EXPORTED(path);
    return ok;
}

bool app_view_load(const char* path) {
    if (!G.loaded) return false;
    cv_cfg c;
    if (!cv_cfg_load(&c, path) || !c.n) { cv_cfg_free(&c); return false; }
    int step = cv_cfg_get_int(&c, "step", G.step + 1) - 1;
    if (step >= 0 && step < G.frd.n_steps) G.step = step;
    const char* f = cv_cfg_get(&c, "field", "");
    if (f[0] && find_field(G.step, f) >= 0) { snprintf(G.field_name, sizeof G.field_name, "%s", f); G.comp = cv_cfg_get_int(&c, "comp", G.comp); G.field_src = 0; }
    const char* calc = cv_cfg_get(&c, "calc", "");
    if (calc[0]) app_calc_set(calc);
    G.elem_mode = cv_cfg_get_bool(&c, "elem_mode", G.elem_mode);
    G.csys = CV_MAX(0, CV_MIN(cv_cfg_get_int(&c, "csys", G.csys), 3));
    G.csys_o[0] = cv_cfg_get_float(&c, "csys_x", G.csys_o[0]); G.csys_o[1] = cv_cfg_get_float(&c, "csys_y", G.csys_o[1]);
    G.csys_o[2] = cv_cfg_get_float(&c, "csys_z", G.csys_o[2]);
    G.deform = cv_cfg_get_bool(&c, "deform", G.deform);
    G.deform_scale = cv_cfg_get_float(&c, "deform_scale", G.deform_scale); G.deform_auto = false;
    G.clip_on = cv_cfg_get_bool(&c, "clip_on", G.clip_on); G.clip_axis = cv_cfg_get_int(&c, "clip_axis", G.clip_axis) % 3;
    G.clip_flip = cv_cfg_get_bool(&c, "clip_flip", G.clip_flip); G.clip_pos = cv_cfg_get_float(&c, "clip_pos", G.clip_pos);
    G.clip_cap = cv_cfg_get_bool(&c, "clip_cap", G.clip_cap);
    G.crop_on = cv_cfg_get_bool(&c, "crop_on", G.crop_on);
    for (int k = 0; k < 3; k++) {
        char key[16];
        snprintf(key, sizeof key, "crop_lo%d", k); G.crop_lo[k] = cv_cfg_get_float(&c, key, G.crop_lo[k]);
        snprintf(key, sizeof key, "crop_hi%d", k); G.crop_hi[k] = cv_cfg_get_float(&c, key, G.crop_hi[k]);
        snprintf(key, sizeof key, "mirror%d", k); G.sym[k] = cv_cfg_get_bool(&c, key, G.sym[k]);
        snprintf(key, sizeof key, "mirror_at%d", k); G.sym_at[k] = cv_cfg_get_int(&c, key, sym_auto(k));
        if (G.sym_at[k] < 0 || G.sym_at[k] >= CV_SYM_N) G.sym_at[k] = sym_auto(k);
        snprintf(key, sizeof key, "rep%d", k); G.rep[k] = cv_cfg_get_bool(&c, key, G.rep[k]);
        snprintf(key, sizeof key, "rep_n%d", k); G.rep_n[k] = CV_MAX(2, CV_MIN(cv_cfg_get_int(&c, key, G.rep_n[k]), 100));
        snprintf(key, sizeof key, "rep_gap%d", k); G.rep_gap[k] = cv_cfg_get_float(&c, key, G.rep_gap[k]);
        if (k == 0) G.rep_follow = cv_cfg_get_bool(&c, "rep_follow", G.rep_follow);
        snprintf(key, sizeof key, "cyc_o%d", k); G.cyc_o[k] = cv_cfg_get_float(&c, key, G.cyc_o[k]);
    }
    G.cyc_on = cv_cfg_get_bool(&c, "cyc_on", G.cyc_on);
    G.cyc_n = CV_MAX(1, CV_MIN(cv_cfg_get_int(&c, "cyc_n", G.cyc_n), 720));
    G.cyc_show = CV_MAX(1, CV_MIN(cv_cfg_get_int(&c, "cyc_show", G.cyc_show), G.cyc_n));
    G.cyc_axis = CV_MAX(0, CV_MIN(cv_cfg_get_int(&c, "cyc_axis", G.cyc_axis), 2));
    G.show_faces = cv_cfg_get_bool(&c, "show_faces", G.show_faces); G.show_edges = cv_cfg_get_bool(&c, "show_edges", G.show_edges);
    G.show_nodes = cv_cfg_get_bool(&c, "show_nodes", G.show_nodes); G.show_gp = cv_cfg_get_bool(&c, "show_gp", G.show_gp);
    G.show_vec = cv_cfg_get_bool(&c, "show_vec", G.show_vec); G.show_markers = cv_cfg_get_bool(&c, "show_markers", G.show_markers);
    G.show_tensor = cv_cfg_get_bool(&c, "show_tensor", G.show_tensor);
    G.show_traj = cv_cfg_get_bool(&c, "show_traj", G.show_traj);
    G.traj_which = CV_MAX(0, CV_MIN(cv_cfg_get_int(&c, "traj_which", G.traj_which), 2));
    G.tensor_style = CV_MAX(0, CV_MIN(cv_cfg_get_int(&c, "tensor_style", G.tensor_style), CV_GLYPH_N - 1));
    G.show_ghost = cv_cfg_get_bool(&c, "show_ghost", G.show_ghost); G.shading = cv_cfg_get_bool(&c, "shading", G.shading);
    G.faces_mode = cv_cfg_get_int(&c, "faces_mode", G.faces_mode) % FM_N;
    app_colormap(cv_cfg_get_int(&c, "cmap", G.cmap) % CV_CMAP_N);
    G.bands = cv_cfg_get_int(&c, "bands", G.bands);
    app_set_step(G.step);
    app_set_faces_mode(G.faces_mode);
    app_groups_changed();
    G.range_lock = cv_cfg_get_bool(&c, "range_lock", false);
    if (G.range_lock) { G.rmin = cv_cfg_get_float(&c, "rmin", G.rmin); G.rmax = cv_cfg_get_float(&c, "rmax", G.rmax); }
    G.up_z = cv_cfg_get_bool(&c, "cam_up_z", false);   /* older views: Y up */
    G.cam.yaw = cv_cfg_get_float(&c, "cam_yaw", G.cam.yaw); G.cam.pitch = cv_cfg_get_float(&c, "cam_pitch", G.cam.pitch);
    G.cam.dist = cv_cfg_get_float(&c, "cam_dist", G.cam.dist); G.cam.ortho = cv_cfg_get_bool(&c, "cam_ortho", G.cam.ortho);
    G.orbit_free = false;                               /* the angles as saved, then the free orientation over them */
    app_set_orbit_free(true);
    G.orbit_free = cv_cfg_get_bool(&c, "cam_free", false);
    if (G.orbit_free) {
        v3 d = v3_make(cv_cfg_get_float(&c, "cam_dx", G.cam.fdir.x), cv_cfg_get_float(&c, "cam_dy", G.cam.fdir.y), cv_cfg_get_float(&c, "cam_dz", G.cam.fdir.z));
        v3 u = v3_make(cv_cfg_get_float(&c, "cam_ux", G.cam.fup.x), cv_cfg_get_float(&c, "cam_uy", G.cam.fup.y), cv_cfg_get_float(&c, "cam_uz", G.cam.fup.z));
        if (v3_dot(d, d) > 0.5f && v3_dot(u, u) > 0.5f) { G.cam.fdir = v3_norm(d); G.cam.fup = v3_norm(u); }
    }
    G.cam.target = v3_make(cv_cfg_get_float(&c, "cam_x", G.cam.target.x), cv_cfg_get_float(&c, "cam_y", G.cam.target.y), cv_cfg_get_float(&c, "cam_z", G.cam.target.z));
    view_bounds();
    cv_cfg_free(&c);
    return true;
}

/* --check FILE: the loader, synchronously and without a window. Prints every
   message; exit code 1 when the file could not be read at all, 2 when it read
   with complaints, 0 when clean. */
int app_check(const char* path) {
    cv_job* j = &G.job;
    memset(j, 0, sizeof *j);
    snprintf(j->path, sizeof j->path, "%s", path);
    cv_mutex_init(&j->lock);
    worker_load(j);
    const cv_msgs* lists[4] = { &j->frd_out.msgs, &j->deck.msgs, &j->dat.msgs, &j->fbd.msgs };
    size_t n = 0;
    for (int l = 0; l < 4; l++)
        for (size_t i = 0; i < lists[l]->n; i++, n++) {
            const cv_msg* m = &lists[l]->a[i];
            if (m->where) printf("%s %llu: %s\n", m->is_offset ? "byte" : "line", (unsigned long long)m->where, m->text);
            else printf("%s\n", m->text);
        }
    if (!j->ok) { printf("%s: %s\n", path, j->err[0] ? j->err : "cannot read"); return 1; }
    printf("%s: %u nodes, %u elements, %d steps, %zu skin triangles, %zu message%s, %.3f s\n", path,
           j->frd_out.n_nodes, j->frd_out.n_elems, j->frd_out.n_steps, j->skin.n_tri, n, n == 1 ? "" : "s", j->seconds);
    return n ? 2 : 0;
}

void app_eval_cgx(void) {
    if (!G.loaded || !cv_ends_with_ci(G.path, ".fbd")) return;
    char p[1024];
    snprintf(p, sizeof p, "%s", G.path);
    O.eval_next = true;
    app_open(p);
}

/* ---- open dialog ---------------------------------------------------------------- */

/* Where a dialog starts: the open file's folder, else samples/ beside the
   executable, else the executable's folder. */
void app_start_dir(char* out, size_t n) {
    out[0] = 0;
    if (G.loaded && G.path[0]) {
        snprintf(out, n, "%s", G.path);
        char* s = strrchr(out, cv_path_sep());
        if (s) { *s = 0; if (cv_is_dir(out)) return; }
    }
    if (settings_last_dir()[0] && cv_is_dir(settings_last_dir())) { snprintf(out, n, "%s", settings_last_dir()); return; }
    char sp[1100];                          /* samples/ beside the binary, or beside its bin/ folder */
    snprintf(sp, sizeof sp, "%s%csamples", G.exe_dir, cv_path_sep());
    if (G.exe_dir[0] && cv_is_dir(sp)) { snprintf(out, n, "%s", sp); return; }
    snprintf(sp, sizeof sp, "%s%c..%csamples", G.exe_dir, cv_path_sep(), cv_path_sep());
    if (G.exe_dir[0] && cv_is_dir(sp)) { snprintf(out, n, "%s", sp); return; }
    snprintf(out, n, "%s", G.exe_dir[0] ? G.exe_dir : ".");
}

void app_open_dialog(void) {
    if (G.dlg_running) return;
    char dir[1024];
    app_start_dir(dir, sizeof dir);
    if (G.native_dlg_missing) {
        snprintf(G.browse_dir, sizeof G.browse_dir, "%s", dir);
        G.browser_open = true;
        return;
    }
    char parent[64] = "";
#if defined(__linux__)
    uintptr_t xid = (uintptr_t)sapp_x11_get_window();
    if (xid) snprintf(parent, sizeof parent, "x11:%lx", (unsigned long)xid);
#endif
    cv_filedlg_start(parent, dir);
    G.dlg_running = true;
}

void poll_dialog(void) {
    if (!G.dlg_running) return;
    char path[1024];
    int st = cv_filedlg_poll(path, sizeof path);
    if (st == CV_DLG_RUNNING || st == CV_DLG_IDLE) return;
    G.dlg_running = false;
    if (st == CV_DLG_DONE) {
        if (G.dlg_for_compare) app_compare_open(path); else app_open(path);
        G.dlg_for_compare = false;
    } else if (st == CV_DLG_UNAVAILABLE) {      /* remember, and fall back to ours */
        G.native_dlg_missing = true;
        app_open_dialog();
    } else {
        G.dlg_for_compare = false;
    }
}

