/* pickinfo.c -- see pickinfo.h. */
#include "pickinfo.h"
#include "mesh.h"
#include <math.h>

int cv_face_ring(const cv_frd* f, uint32_t e, int face, uint32_t out[8]) {
    uint32_t nd[8];
    int n = cv_elem_face_nodes(f, e, face, nd);
    if (n != 6 && n != 8) { for (int i = 0; i < n; i++) out[i] = nd[i]; return n; }
    int m = n / 2;                                   /* corners, then the mid-side nodes in step */
    for (int i = 0; i < m; i++) { out[2 * i] = nd[i]; out[2 * i + 1] = nd[m + i]; }
    return n;
}

float cv_poly_area(const float* p, int n, float nrm[3]) {
    double s[3] = { 0, 0, 0 };
    for (int i = 0; i < n; i++) {
        const float* a = p + 3 * i;
        const float* b = p + 3 * ((i + 1) % n);
        s[0] += ((double)a[1] - b[1]) * ((double)a[2] + b[2]);
        s[1] += ((double)a[2] - b[2]) * ((double)a[0] + b[0]);
        s[2] += ((double)a[0] - b[0]) * ((double)a[1] + b[1]);
    }
    double l = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (nrm) for (int k = 0; k < 3; k++) nrm[k] = l > 0 ? (float)(s[k] / l) : 0.f;
    return (float)(0.5 * l);
}

int cv_elem_edges(const cv_frd* f, uint32_t e, uint32_t out[][3], int max) {
    int k = 0, nf = cv_elem_nfaces(f, e);
    for (int face = 0; face < nf; face++) {
        uint32_t nd[8];
        int n = cv_elem_face_nodes(f, e, face, nd), m = n == 6 || n == 8 ? n / 2 : n;
        for (int j = 0; j < m && k < max; j++) {
            uint32_t a = nd[j], b = nd[(j + 1) % m], mid = m < n ? nd[m + j] : UINT32_MAX;
            bool seen = false;
            for (int q = 0; q < k && !seen; q++)
                seen = (out[q][0] == a && out[q][1] == b) || (out[q][0] == b && out[q][1] == a);
            if (!seen) { out[k][0] = a; out[k][1] = b; out[k][2] = mid; k++; }
        }
    }
    uint32_t nn = f->eoff[e + 1] - f->eoff[e];
    if (!nf && nn >= 2 && max > 0) {                 /* a line: end to end, the middle node between */
        const uint32_t* c = f->conn + f->eoff[e];
        out[0][0] = c[0]; out[0][1] = c[nn == 3 ? 2 : 1]; out[0][2] = nn == 3 ? c[1] : UINT32_MAX;
        k = 1;
    }
    return k;
}
