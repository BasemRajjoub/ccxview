# To do

Open points. Pre-processing stays out of scope.

## Interface
- Left panel: a button at the top left to show or hide it, and a draggable edge
  to resize it (width kept with the other settings).
- Idle load: with a model loaded and nothing happening, GPU use stays high and
  the CPU busy, because `frame_cb` (sokol_app) redraws the scene and the UI at
  every vsync. Redraw only when something changes (input, animation, video,
  loading, a window being dragged, a timer such as the message fade) and
  otherwise drop to a low rate or reuse the last frame, without adding latency
  to the first frame after input. Check native (D3D11/GL) and the browser.

## Shell forces (#19)
- Reinforcement: transverse shear (the sandwich's core) and compression steel
  are not designed; the facet method (Capra-Maury) of Code_Aster is not offered.
- The sandwich's lever arm is fixed at h - 2c; Annex F iterates the inner layer
  (about 19 % more steel in pure bending than Code_Aster SSLS134).
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
- `--field NAME:COMP` with an unknown component falls back to the default field
  and says so only in the message window; print it to stderr too, with the
  valid labels.
