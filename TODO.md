# To do

Open points left after the 9 Oct 2026 batch (sidecar #5, hide sets #25, title block
#15, keywords #2, shell forces #19, STL layers #23, measurements, integrals, movable
labels, the Selection window #16). Pre-processing stays out of scope.

## Shell forces (#19)
- Reinforcement: transverse shear (the sandwich's core) and compression steel
  are not designed; the facet method (Capra-Maury) of Code_Aster is not offered.
- Qx Qy at the mid-side nodes of quadratic shells come from two points through
  the thickness only.

## Deck (#2)
- TIME DELAY= and LOAD CASE= on load cards are ignored.
- *CLOAD, SUBMODEL and *TEMPERATURE, SUBMODEL are not read.
- The ramp of a load inside a static step is not drawn (the end value is).
- PDISP components are all converted as lengths; the help text for ZZS does not
  match the block name ZZSTR.

## Integrals
- The headless `--integrate-csv` ignores the unit settings.
- Surfaces defined on shell elements use the shell's face numbers, which may not
  be the expanded solid's.

## Display
- See-through STL layers are not sorted against each other.
- The History plot labels the display unit but shows the file's numbers.
- The Selection window is tall with every section shown: some sections could fold.
- Crop to the selection does not refit the view (the skin rebuilds in the
  background): press F.
