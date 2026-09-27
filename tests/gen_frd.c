/* gen_frd.c -- synthetic structured Hex8 block with ~N elements, for scale tests.
   usage: gen_frd N out.frd [--binary] [--steps S]
   Material changes every quarter of the height so the group tree has entries. */
#include "frd_write.h"
#include <math.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: gen_frd N out.frd [--binary] [--steps S]\n"); return 2; }
    double want = atof(argv[1]);
    int binary = 0, steps = 2;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--binary")) binary = 1;
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
    }
    /* a long bar: nx = 4*ny = 4*nz */
    int ny = (int)ceil(cbrt(want / 4.0));
    if (ny < 1) ny = 1;
    int nz = ny, nx = 4 * ny;
    uint32_t nn = (uint32_t)(nx + 1) * (ny + 1) * (nz + 1);
    uint32_t ne = (uint32_t)nx * ny * nz;
    fprintf(stderr, "grid %dx%dx%d: %u nodes, %u elements\n", nx, ny, nz, nn, ne);

    FILE* o = fopen(argv[2], "wb");
    if (!o) { perror(argv[2]); return 1; }
    setvbuf(o, NULL, _IOFBF, 1 << 22);

    uint32_t* id = malloc(sizeof(uint32_t) * nn);
    double* xyz = malloc(sizeof(double) * 3 * nn);
    uint32_t* eid = malloc(sizeof(uint32_t) * ne);
    uint32_t* mat = malloc(sizeof(uint32_t) * ne);
    uint32_t* conn = malloc(sizeof(uint32_t) * 8 * (size_t)ne);
    float* disp = malloc(sizeof(float) * 3 * nn);
    float* stress = malloc(sizeof(float) * 6 * nn);
    if (!id || !xyz || !eid || !mat || !conn || !disp || !stress) { fprintf(stderr, "out of memory\n"); return 1; }

#define NID(i, j, k) ((uint32_t)(((k) * (ny + 1) + (j)) * (nx + 1) + (i)) + 1)
    double h = 1.0 / ny;
    for (int k = 0; k <= nz; k++)
        for (int j = 0; j <= ny; j++)
            for (int i = 0; i <= nx; i++) {
                uint32_t n = NID(i, j, k) - 1;
                id[n] = n + 1;
                xyz[3 * n] = i * h; xyz[3 * n + 1] = j * h; xyz[3 * n + 2] = k * h;
            }
    size_t e = 0;
    for (int k = 0; k < nz; k++)
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++, e++) {
                uint32_t* c = conn + 8 * e;
                c[0] = NID(i, j, k);         c[1] = NID(i + 1, j, k);
                c[2] = NID(i + 1, j + 1, k); c[3] = NID(i, j + 1, k);
                c[4] = NID(i, j, k + 1);     c[5] = NID(i + 1, j, k + 1);
                c[6] = NID(i + 1, j + 1, k + 1); c[7] = NID(i, j + 1, k + 1);
                eid[e] = (uint32_t)e + 1;
                mat[e] = 1 + (uint32_t)(4 * k / nz);
            }

    fprintf(o, "    1C\n    1UUSER\n");
    fw_nodes(o, nn, id, xyz, binary);
    fw_elems(o, ne, eid, 1, 8, conn, mat, binary);
    static const char* dn[] = { "D1", "D2", "D3" };
    static const char* sn[] = { "SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX" };
    for (int s = 1; s <= steps; s++) {
        double f = (double)s / steps;
        for (uint32_t n = 0; n < nn; n++) {
            double x = xyz[3 * n], z = xyz[3 * n + 2] - 0.5;
            disp[3 * n] = (float)(-0.01 * f * z * x);
            disp[3 * n + 1] = 0.f;
            disp[3 * n + 2] = (float)(0.005 * f * x * x);
            stress[6 * n] = (float)(-200.0 * f * z * (4.0 - x));
            stress[6 * n + 1] = stress[6 * n + 2] = 0.f;
            stress[6 * n + 3] = (float)(10.0 * f * (0.25 - z * z));
            stress[6 * n + 4] = stress[6 * n + 5] = 0.f;
        }
        fw_step(o, s, f, nn, binary);
        fw_field(o, "DISP", 3, dn, 1, nn, id, disp, binary);
        fw_step(o, s, f, nn, binary);
        fw_field(o, "STRESS", 6, sn, 0, nn, id, stress, binary);
    }
    fprintf(o, "9999\n");
    fclose(o);
    return 0;
}
