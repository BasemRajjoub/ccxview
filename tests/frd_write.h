/* frd_write.h -- minimal .frd writer (ASCII + binary) for tests and the generator.
   Mirrors the column layout CalculiX's frd.c uses. */
#ifndef CV_FRD_WRITE_H
#define CV_FRD_WRITE_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Header line with a format flag at a fixed 0-based column. */
static void fw_header(FILE* o, const char* head, int flag_col, char flag) {
    char line[128];
    int n = snprintf(line, sizeof line, "%s", head);
    while (n < flag_col) line[n++] = ' ';
    line[n++] = flag;
    line[n] = 0;
    fprintf(o, "%s\n", line);
}

static void fw_nodes(FILE* o, uint32_t n, const uint32_t* id, const double* xyz, int binary) {
    char h[64];
    snprintf(h, sizeof h, "    2C%18u", n);
    fw_header(o, h, 73, binary ? '3' : '1');
    for (uint32_t i = 0; i < n; i++) {
        if (binary) {
            int32_t x = (int32_t)id[i];
            fwrite(&x, 4, 1, o);
            fwrite(&xyz[3 * i], 8, 3, o);
        } else {
            fprintf(o, " -1%10u%12.5E%12.5E%12.5E\n", id[i], xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]);
        }
    }
    fprintf(o, " -3\n");
}

/* Elements: conn holds node ids, nn nodes each. */
static void fw_elems(FILE* o, uint32_t n, const uint32_t* id, int type, int nn,
                     const uint32_t* conn, const uint32_t* mat, int binary) {
    char h[64];
    snprintf(h, sizeof h, "    3C%18u", n);
    fw_header(o, h, 73, binary ? '2' : '1');
    for (uint32_t e = 0; e < n; e++) {
        const uint32_t* c = conn + (size_t)e * nn;
        if (binary) {
            int32_t hd[4] = { (int32_t)id[e], type, 0, (int32_t)(mat ? mat[e] : 1) };
            fwrite(hd, 4, 4, o);
            for (int j = 0; j < nn; j++) { int32_t x = (int32_t)c[j]; fwrite(&x, 4, 1, o); }
        } else {
            fprintf(o, " -1%10u%5d%5d%5u\n", id[e], type, 0, mat ? mat[e] : 1u);
            for (int j = 0; j < nn; j++) {
                if (j % 10 == 0) fprintf(o, " -2");
                fprintf(o, "%10u", c[j]);
                if (j % 10 == 9 || j == nn - 1) fprintf(o, "\n");
            }
        }
    }
    fprintf(o, " -3\n");
}

static void fw_step(FILE* o, int numstp, double time, uint32_t nrec, int binary) {
    fprintf(o, "    1PSTEP%25d%12d%12d\n", numstp, numstp, 1);
    char h[96];
    snprintf(h, sizeof h, "  100CL%5d%12.5E%12u                     0%5d",
             100 + numstp, time, nrec, numstp);
    /* the last column (index 74) is the format flag: 1 ASCII, 2 float, 3 double */
    char line[128];
    int n = snprintf(line, sizeof line, "%s", h);
    if (n > 74) n = 74;
    while (n < 74) line[n++] = ' ';
    line[n++] = binary ? '2' : '1';
    line[n] = 0;
    fprintf(o, "%s\n", line);
}

/* One field; vals[node*ncomp + c]. with_all adds CalculiX's pseudo "ALL" entry. */
static void fw_field(FILE* o, const char* name, int ncomp, const char* const* comps,
                     int with_all, uint32_t n, const uint32_t* id, const float* vals, int binary) {
    fprintf(o, " -4  %-8s%4d%5d\n", name, ncomp + (with_all ? 1 : 0), 1);
    for (int c = 0; c < ncomp; c++) fprintf(o, " -5  %-8s%4d%5d%5d%5d\n", comps[c], 1, 2, c + 1, 0);
    if (with_all) fprintf(o, " -5  ALL     %4d%5d%5d%5d%5d%s\n", 1, 2, 0, 0, 1, "ALL");
    for (int c0 = 0; c0 < ncomp; c0 += 6) {
        int k = ncomp - c0 < 6 ? ncomp - c0 : 6;
        for (uint32_t i = 0; i < n; i++) {
            const float* v = vals + (size_t)i * ncomp + c0;
            if (binary) {
                int32_t x = (int32_t)id[i];
                fwrite(&x, 4, 1, o);
                fwrite(v, 4, (size_t)k, o);
            } else if (c0 == 0) {
                fprintf(o, " -1%10u", id[i]);
                for (int c = 0; c < k; c++) fprintf(o, "%12.5E", v[c]);
                fprintf(o, "\n");
                for (int c1 = 6; c1 < ncomp; c1 += 6) {       /* ASCII continuation */
                    fprintf(o, " -2          ");
                    int k2 = ncomp - c1 < 6 ? ncomp - c1 : 6;
                    for (int c = 0; c < k2; c++) fprintf(o, "%12.5E", vals[(size_t)i * ncomp + c1 + c]);
                    fprintf(o, "\n");
                }
            }
        }
        if (!binary) break;                                   /* ASCII wrote all at once */
    }
    fprintf(o, " -3\n");
}

#endif
