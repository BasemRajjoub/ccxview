/* export.h -- write the shown mesh and its current field to files other tools
   read: CSV (one row per node) and legacy VTK (unstructured grid, ParaView).
   Headless. */
#ifndef CV_EXPORT_H
#define CV_EXPORT_H

#include "frd.h"

/* CSV: "id,x,y,z[,dx,dy,dz][,<label>]" per node; disp / scalar may be NULL.
   vis (per element, may be NULL): only nodes of visible elements are written. */
bool cv_export_csv(const char* path, const cv_frd* f, const float* disp, const float* scalar,
                   const char* label, const uint8_t* vis);

/* Legacy VTK ASCII: POINTS, CELLS with VTK cell types (hex 12 / 25, wedge 13 / 26,
   tet 10 / 24, tri 5 / 22, quad 9 / 23, line 3 / 21), POINT_DATA with the
   displacement as VECTORS and the scalar as SCALARS, CELL_DATA with the element
   type, material and group. Quadratic elements keep their .frd mid-node order,
   reordered to VTK's where they differ (hex20, wedge15). vis as above. */
bool cv_export_vtk(const char* path, const cv_frd* f, const float* disp, const float* scalar,
                   const char* label, const uint8_t* vis);

#endif
