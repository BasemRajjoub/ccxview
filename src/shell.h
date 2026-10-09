/* shell.h -- section forces of shells from the stresses of their expanded solids.
   Headless: the expanded mesh, which shell each solid is part of, its axes and the
   nodal stresses in; forces and moments per unit width out.

   CalculiX expands a shell into a solid (S3 -> C3D6, S4 -> C3D8, S6 -> C3D15,
   S8 -> C3D20) and a composite into one such solid per layer, so the .frd holds
   stresses where a structural program shows section forces. They are integrated
   back through the thickness, in the shell's axes (rows e1 e2 e3, e3 its normal):
     Nxx Nyy Nxy = the integral of sxx syy sxy dz       force per width
     Mxx Myy Mxy = the integral of sxx syy sxy z dz     moment per width
     Qx  Qy      = the integral of sxz syz dz           force per width
   z runs along the normal from the middle of the whole section (all its layers),
   so Mxx is positive when the fibres on the +e3 side pull: the moment of sxx, not
   the one turning about x. Each line of nodes through the thickness (two for a
   linear solid; three at the corners of a quadratic one, top, middle, bottom) is
   integrated on its own: the stress linear between two nodes, quadratic through
   three (Simpson for N and Q; M is exact either way, it sees only the linear part).
   A composite adds its layers. A node takes the mean over the shells around it,
   every node of the line the same, so the contour lies on the whole expanded
   shell. Qx and Qy are as good as the solid's transverse shears: rough. */
#ifndef CV_SHELL_H
#define CV_SHELL_H

#include "frd.h"

enum { CV_SF_NXX, CV_SF_NYY, CV_SF_NXY, CV_SF_MXX, CV_SF_MYY, CV_SF_MXY, CV_SF_QX, CV_SF_QY, CV_SF_N };

const char* cv_shell_comp(int c);   /* "Nxx" .. "Qy" */

/* The section forces at every node of f: out[node * CV_SF_N + c], NaN at nodes of
   no shell (and where a stress is missing). shell: per element of f, the shell it
   is part of (0 .. nshell - 1; the layers of a composite share one), UINT32_MAX
   not a shell. q: per element, 9 floats, the shell's axes (rows e1 e2 e3 in
   global). s: per node nc >= 6 values, xx yy zz xy yz zx, global. Lengths as f's
   coordinates, forces as the stresses times them. false: out of memory. */
bool cv_shell_forces(const cv_frd* f, const uint32_t* shell, const float* q, uint32_t nshell,
                     const float* s, int nc, float* out);

/* The same, with the moments about each shell's reference surface and the
   thickness. off: per shell, its *SHELL SECTION OFFSET (NULL: all 0): CalculiX puts
   the reference surface, the one through the shell's nodes, off * t from the
   middle along e3 (0.5: the nodes on the +e3 face), so M = M(middle) - off t N.
   thick: NULL, or per node the section's thickness (NaN at nodes of no shell). */
bool cv_shell_forces_ref(const cv_frd* f, const uint32_t* shell, const float* q, uint32_t nshell,
                         const float* off, const float* s, int nc, float* out, float* thick);

#endif
