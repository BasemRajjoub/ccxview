/* bench.c -- headless: map + parse + groups + skin + decode, timed.
   usage: bench [--quiet] file | bench --fuzz N file */
#include <math.h>
#include "../src/frd.h"
#include "../src/mesh.h"
#include "../src/cap.h"
#include "../src/field.h"
#include "../src/os.h"
#include "../src/dat.h"
#include "../src/inp.h"
#include "../src/fbd.h"
#include "../src/sta.h"

/* --fuzz N file: parse N byte-flipped copies with the reader matching the
   extension (and truncated prefixes). Only crashes and sanitizer reports count. */
static int fuzz(const char* path, int iters) {
    cv_map m;
    if (!cv_map_open(&m, path)) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    size_t pl = strlen(path), L = m.size;
    const char* ext = pl > 4 ? path + pl - 4 : "";
    char* w = malloc(L + 1);
    if (!w) { cv_map_close(&m); return 1; }
    uint32_t rng = 12345;
    for (int it = 0; it < iters; it++) {
        memcpy(w, m.data, L);
        int flips = 1 + (int)(rng % 16);
        for (int k = 0; k < flips && L; k++) {
            rng = rng * 1664525u + 1013904223u;
            w[(rng >> 8) % L] = (char)(rng >> 20);
        }
        rng = rng * 1664525u + 1013904223u;
        size_t n = (it % 4 == 3) ? (rng >> 4) % (L + 1) : L;      /* every 4th: a truncated copy */
        if (!strcmp(ext, ".inp")) { cv_inp d; cv_inp_parse(&d, w, n, NULL, NULL); cv_skin k; if (cv_skin_build(&k, &d.mesh, NULL)) cv_skin_free(&k); cv_inp_free(&d); free(d.msgs.a); }
        else if (!strcmp(ext, ".dat")) { cv_dat d; cv_dat_parse(&d, w, n); cv_dat_free(&d); free(d.msgs.a); }
        else if (!strcmp(ext, ".fbd")) { cv_fbd g; cv_fbd_parse(&g, w, n); cv_fbd_free(&g); free(g.msgs.a); }
        else if (!strcmp(ext, ".sta")) { cv_sta t = {0}; cv_sta_parse(&t, w, n); cv_sta_free(&t); }
        else if (!strcmp(ext, ".cvg")) { cv_sta t = {0}; cv_cvg_parse(&t, w, n); cv_sta_free(&t); }
        else {
            cv_frd f;
            cv_frd_parse(&f, w, n);
            for (int st = 0; st < f.n_steps; st++)
                for (int k = 0; k < f.steps[st].nfields; k++) {
                    float* v = malloc(sizeof(float) * CV_MAX(1, f.n_nodes) * f.steps[st].fields[k].ncomp);
                    if (v) { cv_frd_read_field(&f, &f.steps[st].fields[k], v, &f.msgs); free(v); }
                }
            cv_skin sk; if (cv_skin_build(&sk, &f, NULL)) cv_skin_free(&sk);
            cv_groups g; if (cv_groups_build(&g, &f)) cv_groups_free(&g);
            cv_frd_free(&f); free(f.msgs.a);
        }
    }
    free(w);
    cv_map_close(&m);
    printf("%-50s %d fuzzed copies: ok\n", path, iters);
    return 0;
}

/* *INCLUDE reader: paths relative to the deck's folder */
static bool file_reader(void* user, const char* path, char** data, size_t* size) {
    char full[2048];
    if (path[0] == '/') snprintf(full, sizeof full, "%s", path);
    else snprintf(full, sizeof full, "%s/%s", (const char*)user, path);
    cv_map m;
    if (!cv_map_open(&m, full)) return false;
    *data = malloc(m.size + 1);
    if (!*data) { cv_map_close(&m); return false; }
    memcpy(*data, m.data, m.size);
    *size = m.size;
    cv_map_close(&m);
    return true;
}

int main(int argc, char** argv) {
    if (argc == 4 && strcmp(argv[1], "--fuzz") == 0) return fuzz(argv[3], atoi(argv[2]));
    int quiet = argc > 1 && strcmp(argv[1], "--quiet") == 0;
    if (argc < 2 + quiet) { fprintf(stderr, "usage: bench [--quiet] file | bench --fuzz N file\n"); return 2; }
    const char* path = argv[1 + quiet];

    cv_map m;
    if (!cv_map_open(&m, path)) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    size_t pl = strlen(path);
    if (pl > 4 && !strcmp(path + pl - 4, ".fbd")) {             /* .fbd: cgx geometry */
        cv_fbd g;
        double t0 = cv_now();
        bool ok = cv_fbd_parse(&g, m.data, m.size);
        printf("%s: points=%u curves=%u surfaces=%u triangles=%u sets=%d%s%s (%.3fs)\n", path, g.npts, g.ncrv,
               g.nsrf, g.ntri, g.nsets, g.needs_cgx ? "  NEEDS CGX: " : "", g.needs_cgx ? g.needs_why : "", cv_now() - t0);
        for (size_t i = 0; i < g.msgs.n; i++) printf("  %s\n", g.msgs.a[i].text);
        cv_fbd_free(&g);
        free(g.msgs.a);
        cv_map_close(&m);
        return ok ? 0 : 1;
    }
    if (pl > 4 && !strcmp(path + pl - 4, ".inp")) {             /* .inp: mesh, sets, surfaces */
        char dir[2048];
        snprintf(dir, sizeof dir, "%s", path);
        char* sl = strrchr(dir, '/');
        if (sl) *sl = 0; else strcpy(dir, ".");
        cv_inp d;
        double t0 = cv_now();
        bool ok = cv_inp_parse(&d, m.data, m.size, file_reader, dir);
        double t1 = cv_now();
        cv_skin sk = {0};
        ok = ok && cv_skin_build(&sk, &d.mesh, NULL);
        int ns = 0, es = 0;
        for (int i = 0; i < d.nsets; i++) { if (d.sets[i].is_elem) es++; else ns++; }
        printf("%-60s nodes=%u elems=%u nsets=%d elsets=%d surfs=%d mats=%d tris=%zu msgs=%zu parse=%.3fs %s\n",
               path, d.mesh.n_nodes, d.mesh.n_elems, ns, es, d.nsurfs, d.nmats, sk.n_tri, d.msgs.n, t1 - t0,
               ok ? "" : "FAILED");
        if (!quiet) for (size_t i = 0; i < d.msgs.n; i++) printf("  line %llu: %s\n", (unsigned long long)d.msgs.a[i].where, d.msgs.a[i].text);
        cv_skin_free(&sk);
        cv_inp_free(&d);
        free(d.msgs.a);
        cv_map_close(&m);
        return ok ? 0 : 1;
    }
    if (pl > 4 && !strcmp(path + pl - 4, ".dat")) {             /* .dat: integration-point blocks */
        cv_dat d;
        double t0 = cv_now();
        cv_dat_parse(&d, m.data, m.size);
        printf("%s: %d integration-point blocks in %.3f s\n", path, d.n, cv_now() - t0);
        for (int i = 0; i < d.n && (i < 4 || i == d.n - 1); i++) {
            const cv_dat_block* b = &d.b[i];
            int maxip = 0;
            for (uint32_t r = 0; r < b->n; r++) if (b->ip[r] > maxip) maxip = b->ip[r];
            printf("  [%d] %-28s set %-8s step %d t=%g  %u records, %d comps (%s..), max ip %d\n", i, b->name,
                   b->set, b->step, b->time, b->n, b->ncomp, b->comp[0], maxip);
        }
        for (size_t i = 0; i < d.msgs.n; i++) printf("  msg: %s\n", d.msgs.a[i].text);
        cv_dat_free(&d);
        cv_map_close(&m);
        return 0;
    }
    double t0 = cv_now();
    cv_frd f;
    bool ok = cv_frd_parse(&f, m.data, m.size);
    double t1 = cv_now();
    cv_groups g;
    ok = ok && cv_groups_build(&g, &f);
    cv_skin s = {0};
    ok = ok && cv_skin_build(&s, &f, NULL);
    double t2 = cv_now();

    size_t nfields = 0, nvals = 0;
    float* buf = NULL;
    for (int st = 0; ok && st < f.n_steps; st++) {
        if (st != 0 && st != f.n_steps - 1) continue;          /* first + last step */
        for (int k = 0; k < f.steps[st].nfields; k++) {
            const cv_field_desc* d = &f.steps[st].fields[k];
            float* nb = realloc(buf, sizeof(float) * CV_MAX(1, f.n_nodes) * d->ncomp);
            if (!nb) { ok = false; break; }
            buf = nb;
            cv_frd_read_field(&f, d, buf, &f.msgs);
            nfields++;
            nvals += (size_t)f.n_nodes * d->ncomp;
        }
    }
    double t3 = cv_now();

    size_t total_fields = 0;
    for (int st = 0; st < f.n_steps; st++) total_fields += f.steps[st].nfields;
    if (quiet) {
        printf("%-60s nodes=%u elems=%u steps=%d fields=%zu tris=%zu edges=%zu msgs=%zu parse=%.3fs skin=%.3fs %s\n",
               path, f.n_nodes, f.n_elems, f.n_steps, total_fields, s.n_tri, s.n_edge, f.msgs.n,
               t1 - t0, t2 - t1, ok ? "" : "FAILED");
    } else {
        printf("file      %s (%.1f MB)\n", path, m.size / 1048576.0);
        printf("nodes     %u\nelements  %u\nsteps     %d (%zu fields)\n", f.n_nodes, f.n_elems,
               f.n_steps, total_fields);
        for (int a = 0; ok && a < CV_AXIS_N; a++) printf("%-9s %d groups\n", cv_axis_name(a), g.axis[a].n);
        printf("skin      %zu triangles, %zu edges, %zu points\n", s.n_tri, s.n_edge, s.n_pt);
        if (ok && f.n_elems) {                         /* the clip cut: prepared once, then moved */
            float lo = INFINITY, hi = -INFINITY;
            for (uint32_t i = 0; i < f.n_nodes; i++) { lo = fminf(lo, f.xyz[3 * (size_t)i]); hi = fmaxf(hi, f.xyz[3 * (size_t)i]); }
            cv_cap_model cm = { .f = &f, .n = { 1, 0, 0 }, .mid = true };
            cv_cap_prep cp;
            double c0 = cv_now();
            if (cv_cap_prepare(&cp, &cm)) {
                double c1 = cv_now();
                size_t tris = 0;
                for (int i = 0; i < 20; i++) {
                    cv_cap_out co = {0};
                    cv_cap_cut(&cm, &cp, lo + (hi - lo) * (i + 0.5f) / 20, 0, NULL, NULL, &co);
                    tris += co.pos.n / 9;
                    cv_cap_out_free(&co);
                }
                printf("clip cut  prepare %.3f s, then %.4f s per position (%zu triangles each)\n", c1 - c0,
                       (cv_now() - c1) / 20, tris / 20);
                cv_cap_prep_free(&cp);
            }
        }
        printf("parse     %.3f s\ngroups+skin %.3f s\ndecode    %.3f s (%zu fields, %.1f M values)\n",
               t1 - t0, t2 - t1, t3 - t2, nfields, nvals / 1e6);
        if (f.n_steps) {
            const cv_step* ls = &f.steps[f.n_steps - 1];
            for (int k = 0; k < ls->nfields; k++) {
                const cv_field_desc* d = &ls->fields[k];
                float* v = malloc(sizeof(float) * CV_MAX(1, f.n_nodes) * d->ncomp);
                float* sc = malloc(sizeof(float) * CV_MAX(1, f.n_nodes));
                if (!v || !sc) { free(v); free(sc); break; }
                cv_frd_read_field(&f, d, v, NULL);
                printf("  last step %-8s", d->name);
                for (int c = 0; c < d->ncomp && c < 6; c++) {
                    float mn, mx;
                    cv_field_scalar(v, d->ncomp, f.n_nodes, c, sc);
                    cv_range(sc, f.n_nodes, &mn, &mx);
                    printf(" %s[%.4g,%.4g]", d->comp[c], mn, mx);
                }
                printf("\n");
                free(v); free(sc);
            }
        }
        for (size_t i = 0; i < f.msgs.n; i++)
            printf("  %s %llu: %s\n", f.msgs.a[i].is_offset ? "byte" : "line",
                   (unsigned long long)f.msgs.a[i].where, f.msgs.a[i].text);
    }
    free(buf);
    cv_skin_free(&s);
    if (ok) cv_groups_free(&g);
    cv_frd_free(&f);
    free(f.msgs.a);
    cv_map_close(&m);
    return ok ? 0 : 1;
}
