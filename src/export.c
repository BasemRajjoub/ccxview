/* export.c -- write the shown mesh and field to CSV / legacy VTK for other tools. */
#include "export.h"
#include <math.h>

/* Node-order table by FRD element type 1..12 -> legacy VTK cell type. 0 = unknown. */
static const int kVtkType[] = { 0, 12, 13, 10, 25, 26, 24, 5, 22, 9, 23, 3, 21 };

/* hex20 and wedge15 keep the .frd node in one place; only these two swap two
   quadruples/triples of mid-edge nodes against VTK's order (see gauss.h). Returns
   the .frd (== f->conn) position that belongs at VTK connectivity slot `i`. */
static int vtk_src_pos(int etype, int nn, int i) {
    if (etype == 4 && nn == 20) {
        if (i >= 12 && i < 16) return i + 4;   /* vtk top mid <- frd top mid (17-20) */
        if (i >= 16 && i < 20) return i - 4;   /* vtk vertical <- frd vertical (13-16) */
    }
    if (etype == 5 && nn == 15) {
        if (i >= 9 && i < 12) return i + 3;    /* vtk top mid <- frd top mid (13-15) */
        if (i >= 12 && i < 15) return i - 3;   /* vtk vertical <- frd vertical (10-12) */
    }
    return i;
}

/* A float printed as "nan" rather than whatever the platform's printf spells it. */
static void put_f(FILE* o, float v) {
    if (v != v) fputs("nan", o);
    else fprintf(o, "%.9g", v);
}

bool cv_export_csv(const char* path, const cv_frd* f, const float* disp, const float* scalar,
                   const char* label, const uint8_t* vis) {
    FILE* o = fopen(path, "w");
    if (!o) return false;

    /* Which nodes to write: everything, or only those used by a visible element. */
    uint8_t* used = NULL;
    if (vis && f->n_nodes) {
        used = calloc(f->n_nodes, 1);
        if (used) {
            for (uint32_t e = 0; e < f->n_elems; e++) {
                if (!vis[e]) continue;
                for (uint32_t j = f->eoff[e]; j < f->eoff[e + 1]; j++) used[f->conn[j]] = 1;
            }
        }
        /* OOM: fall back to writing every node rather than crash or fail the export. */
    }

    fprintf(o, "id,x,y,z");
    if (disp) fprintf(o, ",dx,dy,dz");
    if (scalar) fprintf(o, ",%s", label && label[0] ? label : "value");
    fputc('\n', o);

    for (uint32_t i = 0; i < f->n_nodes; i++) {
        if (used && !used[i]) continue;
        fprintf(o, "%u,", f->node_id[i]);
        put_f(o, f->xyz[3 * i]);     fputc(',', o);
        put_f(o, f->xyz[3 * i + 1]); fputc(',', o);
        put_f(o, f->xyz[3 * i + 2]);
        if (disp) {
            fputc(',', o); put_f(o, disp[3 * i]);
            fputc(',', o); put_f(o, disp[3 * i + 1]);
            fputc(',', o); put_f(o, disp[3 * i + 2]);
        }
        if (scalar) { fputc(',', o); put_f(o, scalar[i]); }
        fputc('\n', o);
    }

    free(used);
    return fclose(o) == 0;
}

bool cv_export_vtk(const char* path, const cv_frd* f, const float* disp, const float* scalar,
                   const char* label, const uint8_t* vis) {
    FILE* o = fopen(path, "w");
    if (!o) return false;

    fprintf(o, "# vtk DataFile Version 3.0\nccxview\nASCII\nDATASET UNSTRUCTURED_GRID\n");

    /* Points are always written in full: cells reference them by their existing
       dense index, so a vis mask only ever needs to drop whole cells, never
       renumber points. */
    fprintf(o, "POINTS %u float\n", f->n_nodes);
    for (uint32_t i = 0; i < f->n_nodes; i++) {
        put_f(o, f->xyz[3 * i]);     fputc(' ', o);
        put_f(o, f->xyz[3 * i + 1]); fputc(' ', o);
        put_f(o, f->xyz[3 * i + 2]); fputc('\n', o);
    }

    uint32_t n_cell = 0;
    uint64_t total = 0;
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (vis && !vis[e]) continue;
        int nn = cv_frd_type_nodes(f->etype[e]);
        if (nn == 0 || f->etype[e] >= (uint8_t)CV_COUNT(kVtkType) || kVtkType[f->etype[e]] == 0) continue;
        n_cell++;
        total += (uint64_t)nn + 1;
    }

    fprintf(o, "CELLS %u %llu\n", n_cell, (unsigned long long)total);
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (vis && !vis[e]) continue;
        int t = f->etype[e];
        int nn = cv_frd_type_nodes(t);
        if (nn == 0 || t >= (int)CV_COUNT(kVtkType) || kVtkType[t] == 0) continue;
        fprintf(o, "%d", nn);
        uint32_t base = f->eoff[e];
        for (int i = 0; i < nn; i++)
            fprintf(o, " %u", f->conn[base + (uint32_t)vtk_src_pos(t, nn, i)]);
        fputc('\n', o);
    }

    fprintf(o, "CELL_TYPES %u\n", n_cell);
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (vis && !vis[e]) continue;
        int t = f->etype[e];
        if (cv_frd_type_nodes(t) == 0 || t >= (int)CV_COUNT(kVtkType) || kVtkType[t] == 0) continue;
        fprintf(o, "%d\n", kVtkType[t]);
    }

    if (disp || scalar) {
        fprintf(o, "POINT_DATA %u\n", f->n_nodes);
        if (disp) {
            fprintf(o, "VECTORS displacement float\n");
            for (uint32_t i = 0; i < f->n_nodes; i++) {
                put_f(o, disp[3 * i]);     fputc(' ', o);
                put_f(o, disp[3 * i + 1]); fputc(' ', o);
                put_f(o, disp[3 * i + 2]); fputc('\n', o);
            }
        }
        if (scalar) {
            char name[64];
            snprintf(name, sizeof name, "%s", label && label[0] ? label : "value");
            for (char* p = name; *p; p++) if (*p == ' ') *p = '_';
            fprintf(o, "SCALARS %s float 1\nLOOKUP_TABLE default\n", name);
            for (uint32_t i = 0; i < f->n_nodes; i++) { put_f(o, scalar[i]); fputc('\n', o); }
        }
    }

    fprintf(o, "CELL_DATA %u\n", n_cell);
    fprintf(o, "SCALARS elem_type int 1\nLOOKUP_TABLE default\n");
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (vis && !vis[e]) continue;
        int t = f->etype[e];
        if (cv_frd_type_nodes(t) == 0 || t >= (int)CV_COUNT(kVtkType) || kVtkType[t] == 0) continue;
        fprintf(o, "%d\n", t);
    }
    fprintf(o, "SCALARS material int 1\nLOOKUP_TABLE default\n");
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (vis && !vis[e]) continue;
        int t = f->etype[e];
        if (cv_frd_type_nodes(t) == 0 || t >= (int)CV_COUNT(kVtkType) || kVtkType[t] == 0) continue;
        fprintf(o, "%u\n", f->emat[e]);
    }
    fprintf(o, "SCALARS group int 1\nLOOKUP_TABLE default\n");
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (vis && !vis[e]) continue;
        int t = f->etype[e];
        if (cv_frd_type_nodes(t) == 0 || t >= (int)CV_COUNT(kVtkType) || kVtkType[t] == 0) continue;
        fprintf(o, "%u\n", f->egrp[e]);
    }
    fprintf(o, "SCALARS elem_id int 1\nLOOKUP_TABLE default\n");
    for (uint32_t e = 0; e < f->n_elems; e++) {
        if (vis && !vis[e]) continue;
        int t = f->etype[e];
        if (cv_frd_type_nodes(t) == 0 || t >= (int)CV_COUNT(kVtkType) || kVtkType[t] == 0) continue;
        fprintf(o, "%u\n", f->elem_id[e]);
    }

    return fclose(o) == 0;
}
