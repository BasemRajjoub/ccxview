# To do

Open points left after the 9 Oct 2026 batch (sidecar #5, hide sets #25, title block
#15, keywords #2, shell forces #19, STL layers #23, measurements, integrals, movable
labels, the Selection window #16) and the fixes after it. Pre-processing stays out
of scope.

## Shell forces (#19)
- Reinforcement: transverse shear (the sandwich's core) and compression steel
  are not designed; the facet method (Capra-Maury) of Code_Aster is not offered.
- Qx Qy at the mid-side nodes of quadratic shells come from two points through
  the thickness only.

## Deck (#2)
- The ramp of a load inside a static step is not drawn (the end value is).
- LOAD CASE=2 (the imaginary part of a steady state dynamics load) is left out;
  a switch to show it instead of load case 1 could follow.

## Integrals
- Surfaces defined on shell elements use the shell's face numbers, which may not
  be the expanded solid's.
- The moment point (nodes:SET@x,y,z) is in model coordinates, not in the length
  shown.

## Display
- See-through STL layers are sorted as wholes, by their box centres: one layer
  inside another's box, or two interleaved, can still blend the wrong way
  (sorting triangles would fix it, at a cost per frame).
- The History plot was reported to show the file's numbers under the shown unit:
  not reproduced (DISP in m, by --opt and by the .ccxview, plots and writes the
  converted values). The Integrals window did mix units (fixed): if it comes back,
  note the field, the units and how the history was opened.
