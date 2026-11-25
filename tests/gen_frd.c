/* gen_frd.c -- synthetic structured Hex8 block with ~N elements, for scale tests.
   usage: gen_frd N out.frd [--binary] [--steps S] [--quad]
   Material changes every quarter of the height so the group tree has entries. */
#include "frd_write.h"
#include <math.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: gen_frd N out.frd [--binary] [--steps S]\n"); return 2; }
    double want = atof(argv[1]);
    int binary = 0, steps = 2, quad = 0;      /* --quad: C3D20 instead of C3D8 */
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--binary")) binary = 1;
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--quad")) quad = 1;
    }
    /* a long bar: nx = 4*ny = 4*nz */
    int ny = (int)ceil(cbrt(want / 4.0));
    if (ny < 1) ny = 1;
    int nz = ny, nx = 4 * ny;
    /* quadratic: a node grid twice as fine; an element takes its corners and the
       middles of its edges from it (the nodes in between stay unused) */
    int g = quad ? 2 : 1, gx = g * nx, gy = g * ny, gz = g * nz, per = quad ? 20 : 8;
    uint32_t nn = (uint32_t)(gx + 1) * (gy + 1) * (gz + 1);
    uint32_t ne = (uint32_t)nx * ny * nz;
    fprintf(stderr, "grid %dx%dx%d: %u nodes, %u elements\n", nx, ny, nz, nn, ne);

    FILE* o = fopen(argv[2], "wb");
    if (!o) { perror(argv[2]); return 1; }
    setvbuf(o, NULL, _IOFBF, 1 << 22);

    uint32_t* id = malloc(sizeof(uint32_t) * nn);
    double* xyz = malloc(sizeof(double) * 3 * nn);
    uint32_t* eid = malloc(sizeof(uint32_t) * ne);
    uint32_t* mat = malloc(sizeof(uint32_t) * ne);
    uint32_t* conn = malloc(sizeof(uint32_t) * (size_t)per * (size_t)ne);
    float* disp = malloc(sizeof(float) * 3 * nn);
    float* stress = malloc(sizeof(float) * 6 * nn);
    if (!id || !xyz || !eid || !mat || !conn || !disp || !stress) { fprintf(stderr, "out of memory\n"); return 1; }

#define NID(i, j, k) ((uint32_t)(((k) * (gy + 1) + (j)) * (gx + 1) + (i)) + 1)
    double h = 1.0 / gy;
    for (int k = 0; k <= gz; k++)
        for (int j = 0; j <= gy; j++)
            for (int i = 0; i <= gx; i++) {
                uint32_t n = NID(i, j, k) - 1;
                id[n] = n + 1;
                xyz[3 * n] = i * h; xyz[3 * n + 1] = j * h; xyz[3 * n + 2] = k * h;
            }
    size_t e = 0;
    for (int k = 0; k < nz; k++)
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++, e++) {
                uint32_t* c = conn + (size_t)per * e;
                int a = g * i, b = g * j, d = g * k;
                c[0] = NID(a, b, d);         c[1] = NID(a + g, b, d);
                c[2] = NID(a + g, b + g, d); c[3] = NID(a, b + g, d);
                c[4] = NID(a, b, d + g);     c[5] = NID(a + g, b, d + g);
                c[6] = NID(a + g, b + g, d + g); c[7] = NID(a, b + g, d + g);
                if (quad) {                  /* .frd order: bottom, upright, top */
                    c[8] = NID(a + 1, b, d);      c[9] = NID(a + 2, b + 1, d);  c[10] = NID(a + 1, b + 2, d);     c[11] = NID(a, b + 1, d);
                    c[12] = NID(a, b, d + 1);     c[13] = NID(a + 2, b, d + 1); c[14] = NID(a + 2, b + 2, d + 1); c[15] = NID(a, b + 2, d + 1);
                    c[16] = NID(a + 1, b, d + 2); c[17] = NID(a + 2, b + 1, d + 2); c[18] = NID(a + 1, b + 2, d + 2); c[19] = NID(a, b + 1, d + 2);
                }
                eid[e] = (uint32_t)e + 1;
                mat[e] = 1 + (uint32_t)(4 * k / nz);
            }

    fprintf(o, "    1C\n    1UUSER\n");
    fw_nodes(o, nn, id, xyz, binary);
    fw_elems(o, ne, eid, quad ? 4 : 1, per, conn, mat, binary);
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
