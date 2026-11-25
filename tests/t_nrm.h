/* t_nrm.h -- smooth skin normals */
#include "../src/nrm.h"

static void test_nrm(void) {
    /* two triangles of the z = 0 plane, one bent up along x = 1 (a 90 degree crease) */
    const float xyz[] = { 0,0,0,  1,0,0,  1,1,0,  0,1,0,  1,0,1,  1,1,1 };
    const uint32_t tri[] = { 0,1,2,  0,2,3,  1,4,5,  1,5,2 };
    int8_t n[6 * 4];
    CHECK(cv_nrm_skin(xyz, NULL, 0, 6, tri, 2, n));
    for (int i = 0; i < 4; i++) CHECK(n[4 * i + 2] == 127 && n[4 * i] == 0 && n[4 * i + 1] == 0);
    CHECK(n[4 * 4 + 0] == 0 && n[4 * 4 + 2] == 0);                 /* untouched node: 0 */
    CHECK(cv_nrm_skin(xyz, NULL, 0, 6, tri, 4, n));
    CHECK(n[4 * 4 + 0] == -127 && n[4 * 4 + 2] == 0);              /* the wall's normal -x */
    CHECK(n[4 * 1 + 0] == 0 && n[4 * 1 + 2] == 0);                 /* on the 90 degree crease: none, flat faces up to it */
    CHECK(n[4 * 0 + 2] == 127 && n[4 * 3 + 2] == 127);             /* off it: the plane's */
    /* a gentle fold (20 degrees) stays smooth: between the two */
    const float fx[] = { 0,0,0,  1,0,0,  1,1,0,  0,1,0,  2,0,0.364f,  2,1,0.364f };
    CHECK(cv_nrm_skin(fx, NULL, 0, 6, tri, 4, n));
    CHECK(n[4 * 1 + 0] < -5 && n[4 * 1 + 2] > 100);
    /* both sides of a shell on the same nodes cancel: 0 */
    const uint32_t two[] = { 0,1,2,  0,2,1 };
    CHECK(cv_nrm_skin(xyz, NULL, 0, 6, two, 2, n));
    CHECK(n[0] == 0 && n[1] == 0 && n[2] == 0);
    /* the deformed shape: lifting node 2 by disp tilts the normal of node 0 */
    const float disp[] = { 0,0,0,  0,0,0,  0,0,1,  0,0,0,  0,0,0,  0,0,0 };
    CHECK(cv_nrm_skin(xyz, disp, 1.f, 6, tri, 2, n));
    CHECK(n[4 * 1 + 0] == 0 && n[4 * 1 + 1] < -80 && n[4 * 1 + 2] > 80);   /* node 1: (0, -1, 1) / sqrt 2 */
    CHECK(cv_nrm_skin(xyz, NULL, 0, 6, tri, 0, n) && n[2] == 0);   /* no triangles */
}
