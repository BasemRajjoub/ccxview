/* app_load.c -- background loading (.frd / .inp / .fbd / .dat / .sta), the skin
   job after group changes, taking a finished job into G, and the open dialog. */
#include "app_int.h"
#include "calc.h"
#include "web.h"
#include "cgx.h"
#include "filedlg.h"
#include "log.h"
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
    app_sidecar_flush();                    /* what was set up for this model, before it goes */
    app_path_clear();
    app_hist_close();
    app_integ_close();
    app_lin_close();
    app_compare_close();
    cache_clear();
    cv_calc_free(G.calc); G.calc = NULL;    /* its formula stays, for the next file */
    gp_clear();
    deck_clear();
    fail_clear();
    mesh_clear();
    shell_clear();
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

/* Default: |DISP| when there is one (the first look is whether it moved as expected,
   #24), else von Mises, else the first field. */
static void pick_default_field(void) {
    G.field_name[0] = 0;
    const cv_step* s = &G.frd.steps[G.step];
    const char* pref[] = { "DISP", "STRESS", "NDTEMP" };
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

/* is this measurement listed already (the same kind, the same nodes in order) */
static bool measure_listed(int kind, const uint32_t ids[3]) {
    for (int i = 0; i < app_measure_count(); i++) {
        int k;
        uint32_t id[3];
        if (app_measure_get(i, &k, id) && k == kind && id[0] == ids[0] && id[1] == ids[1] && (app_measure_nodes(k) == 2 || id[2] == ids[2])) return true;
    }
    return false;
}

static void apply_load(cv_job* j) {
    bool reload = G.reload_keep;
    unload();
    if (!j->ok) {
        G.reload_keep = false;                 /* the next file opened is not this reload */
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
    shell_attach();                            /* SHELL beside STRESS: the same */
    if (strcmp(G.path, j->path)) app_stl_clear();   /* imported geometry belongs to the model: a reload keeps it */
    snprintf(G.path, sizeof G.path, "%s", j->path);
    bool new_model = app_measure_model(G.path);   /* another file: its measurements go (a sidecar may add them after) */
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
    if (!reload) {                       /* a reload keeps the crop box (the skin is rebuilt with it below) */
        G.crop_on = false;
        for (int k = 0; k < 3; k++) { G.crop_lo[k] = 0.f; G.crop_hi[k] = 1.f; }
    }
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
        if (!reload) G.range_lock = false;   /* a reload keeps a locked range */
        app_set_step(G.step);
    } else {
        refresh_field();
        deck_refresh_highlight();             /* no steps: an unsolved deck shows its last step's loads */
    }
    if (!G.reload_keep) for (int i = 0; i < O.nstl; i++) app_stl_add(O.stl[i]);   /* before the first fit, which takes them in */
    if (G.reload_keep) { view_bounds(); G.cam = G.keep_cam; G.reload_keep = false; }
    else app_view(CV_VIEW_ISO);
    app_sidecar_load(reload);            /* what was set up for this model; the command line below wins */
    if (reload && G.crop_on && !app_busy()) app_groups_changed();   /* the crop box kept */
    G.watch_mtime = cv_file_mtime(G.path); G.watch_size = cv_file_size(G.path); G.watch_t = cv_now();
    if (O.fly) app_set_flight(true);
    if (O.mesh_window) G.show_mesh = G.mesh_limits_open = true;
    if (O.measure_window) G.show_measure = true;
    if (O.details) G.show_details = true;
    if (O.about) G.show_about = true;
    if (O.deck_window) G.show_deck = true;
    if (O.range_set && !G.reload_keep) { G.range_lock = true; G.rmin = O.range[0]; G.rmax = O.range[1]; }
    if (O.labels && !G.reload_keep) {
        int m = app_label_parse(O.labels);
        if (!m) cv_msg_add(&G.msgs, 0, false, "--labels: unknown kind (node, elem, value, evalue, sets, links, loads, supports, materials, gpvalue, gpid, minmax)");
        else {
            G.label_kinds = m;
            if (m & 1 << CV_LABEL_MINMAX) G.show_markers = true;
            if (m & (1 << CV_LABEL_GPVALUE | 1 << CV_LABEL_GPID)) G.show_gp = true;
        }
    }
    for (int q = 0; !G.reload_keep && q < O.nlabel_off; q++) {   /* --label-offset KIND:ID:DX,DY */
        cv_label_off o;
        if (cv_label_off_parse_arg(O.label_off[q], &o) && app_label_key_kind(o.kind)) app_label_move(o.kind, o.id, o.dx, o.dy);
        else {
            char m[200];
            snprintf(m, sizeof m, "--label-offset %s: KIND:ID:DX,DY, the kind one of node, elem, gp, min, max, measure, set, link, load, support, material", O.label_off[q]);
            cv_msg_add(&G.msgs, 0, false, m);
        }
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
    for (int q = 0; new_model && q < O.nmeasure; q++) {   /* --measure dist:A,B | angle:A,B,C | circle:A,B,C */
        const char* s = O.measure[q];
        const char* colon = strchr(s, ':');
        int kind = -1;
        for (int k = 0; colon && k < CV_MEAS_N; k++)
            if (!strncasecmp(s, app_measure_kind_name(k), (size_t)(colon - s)) && colon > s) kind = k;   /* dist, distance, ang ... */
        unsigned id[3] = { 0, 0, 0 };
        int n = colon ? sscanf(colon + 1, "%u,%u,%u", &id[0], &id[1], &id[2]) : 0;
        uint32_t ids[3] = { id[0], id[1], id[2] };
        char m[160];
        if (kind < 0 || n != app_measure_nodes(kind)) {
            snprintf(m, sizeof m, "--measure %s: dist:A,B, angle:A,B,C or circle:A,B,C (node ids)", s);
            cv_msg_add(&G.msgs, 0, false, m);
        } else if (measure_listed(kind, ids)) {
            continue;                    /* restored from the post-processing file already */
        } else if (!app_measure_add(kind, ids)) {
            snprintf(m, sizeof m, "--measure %s: node not in this model", s);
            cv_msg_add(&G.msgs, 0, false, m);
        }
    }
    if (O.hist_id > 0) {
        uint32_t n = cv_frd_node_index(&G.frd, (uint32_t)O.hist_id);
        if (n != UINT32_MAX) app_hist_open(n, UINT32_MAX);
        else cv_msg_add(&G.msgs, 0, false, "--history: node not in this model");
    }
    if (O.find) {
        bool el = O.find[0] == 'e' || O.find[0] == 'E';
        if (!app_find((uint32_t)strtoul(O.find + (el ? 1 : 0), NULL, 10), el)) cv_msg_add(&G.msgs, 0, false, "--find: not in this model");
        else { G.label_probe_only = true; if (!G.label_kinds) G.label_kinds = 1 << CV_LABEL_NODE; app_label_changed(); }
    }
    if ((O.nsets || O.nhide_sets) && deck_loaded()) {
        const cv_inp* dk = deck_get();
        for (int k = 0; k < O.nsets; k++) {
            for (int i = 0; i < dk->nsets; i++) if (!strcasecmp(dk->sets[i].name, O.sets[k])) deck_set_flags()[i] = true;
            for (int i = 0; i < dk->nsurfs; i++) if (!strcasecmp(dk->surfs[i].name, O.sets[k])) deck_surf_flags()[i] = true;
        }
        for (int k = 0; k < O.nhide_sets; k++) {
            bool found = false;
            for (int i = 0; i < dk->nsets; i++)
                if (dk->sets[i].is_elem && !strcasecmp(dk->sets[i].name, O.hide_sets[k])) deck_set_hidden_flags()[i] = found = true;
            if (!found) cv_msg_add(&G.msgs, 0, false, "--hide-set: no element set of that name in the deck");
        }
        deck_refresh_highlight();
        app_groups_changed();
    }
    if (O.step) { app_set_step(O.step < 0 ? G.frd.n_steps - 1 : O.step - 1); app_fit(); }
    if (O.conv) G.show_conv = true;
    if (O.look >= 0) app_view(O.look);
    if (O.target_set) G.cam.target = v3_make(O.target[0], O.target[1], O.target[2]);
    if (O.zoom > 0) G.cam.dist /= O.zoom;
    if (O.field && G.frd.n_steps > 0) {     /* NAME, or NAME:COMPONENT as the Fields tree names it (SHELL:Mxx) */
        char name[64];
        const char* colon = strchr(O.field, ':');
        snprintf(name, sizeof name, "%.*s", (int)(colon ? CV_MIN((size_t)(colon - O.field), sizeof name - 1) : strlen(O.field)), O.field);
        int fi = find_field(G.step, name);
        if (fi >= 0) {
            cv_scalar_opt o[CV_MAX_OPTS];
            int n = app_field_options(&G.frd.steps[G.step].fields[fi], o, CV_MAX_OPTS), k = n ? 0 : -1;
            if (colon) {
                k = -1;
                for (int i = 0; i < n; i++) if (!strcasecmp(o[i].label, colon + 1)) k = i;
            }
            if (k >= 0) app_select_src(name, o[k].comp, 0);
            else {
                char m[160];
                snprintf(m, sizeof m, "--field %s: %s has no such component", O.field, name);
                cv_msg_add(&G.msgs, 0, false, m);
            }
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
    /* last: what --field, --calc, --fail, --mesh chose is what is exported */
    if (O.export_kind == 1) app_export_png();
    else if (O.export_kind == 3) app_export_data(false);
    else if (O.export_kind == 4) app_export_data(true);
    else if (O.export_kind >= 5) {                 /* seq / mp4 / seqsteps / mp4steps */
        G.exp_video = O.export_kind == 6 || O.export_kind == 8;
        G.exp_kind = O.export_kind >= 7;
        app_export_animation();
    }
}

static void start_skin_job(void) {
    free(G.job.vis);
    G.job.vis = malloc(CV_MAX(G.frd.n_elems, 1));
    if (!G.job.vis) { cv_msg_add(&G.msgs, 0, false, "out of memory"); return; }
    cv_groups_mask(&G.groups, G.frd.n_elems, G.job.vis);
    deck_apply_mask(&G.frd, G.job.vis);        /* ticked element sets: a display group */
    deck_apply_removed(&G.frd, G.job.vis);     /* *MODEL CHANGE: out in the step on screen */
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
    if (cv_ends_with_ci(path, ".stl")) { app_stl_add(path); return; }   /* geometry joins the open model */
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

/* --integrate-csv: the model read here and now, no window, nothing drawn: the mesh,
   the steps, the skin and the deck into G as a load would put them */
bool app_load_headless(const char* path) {
    cv_job* j = &G.job;
    memset(j, 0, sizeof *j);
    snprintf(j->path, sizeof j->path, "%s", path);
    cv_mutex_init(&j->lock);
    worker_load(j);
    if (!j->ok) { fprintf(stderr, "%s: %s\n", path, j->err[0] ? j->err : "cannot read"); return false; }
    for (int q = 0; q < CV_Q_N; q++) G.unit_in[q] = G.unit_show[q] = -1;   /* values as the file has them */
    if (j->has_deck) deck_set(&j->deck, j->deck_path);
    j->has_deck = false;
    G.map = j->map;         memset(&j->map, 0, sizeof j->map);
    G.frd = j->frd_out;     memset(&j->frd_out, 0, sizeof j->frd_out);
    G.groups = j->groups;   memset(&j->groups, 0, sizeof j->groups);
    G.skin = j->skin;       memset(&j->skin, 0, sizeof j->skin);
    snprintf(G.path, sizeof G.path, "%s", path);
    G.loaded = true;
    return true;
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

void app_dialog_done(const char* path) {
    bool cmp = G.dlg_for_compare, stl = G.dlg_for_stl;     /* a .stl picked to open joins the model too (app_open) */
    G.dlg_for_compare = G.dlg_for_stl = false;
    if (stl) app_stl_add(path);
    else if (cmp) app_compare_open(path);
    else app_open(path);
}

void poll_dialog(void) {
    if (!G.dlg_running) return;
    char path[1024];
    int st = cv_filedlg_poll(path, sizeof path);
    if (st == CV_DLG_RUNNING || st == CV_DLG_IDLE) return;
    G.dlg_running = false;
    if (st == CV_DLG_DONE) {
        app_dialog_done(path);
    } else if (st == CV_DLG_UNAVAILABLE) {      /* remember, and fall back to ours */
        G.native_dlg_missing = true;
        app_open_dialog();
    } else {
        G.dlg_for_compare = G.dlg_for_stl = false;
    }
}

