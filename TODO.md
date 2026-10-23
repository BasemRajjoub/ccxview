# To do

Pre-processing stays out of scope. Work top to bottom: each step builds on the one
before it, so no step rewrites code an earlier one wrote. One commit per step.

## 1. GUI: Inspector pane (right) — then #28

The pane is there (`ui_inspector.c`: under the toolbar at the right, `]` / `[` and the
chevron buttons hide and show it and the Scene pane, inner edges drag, kept in the ini;
tabs **Inspect | Selection | Measure**); Inspect holds the probe (Probe + Details, the
model's summary when nothing is probed, a selection's summary on top); Selection and
Measure hold what their floating windows did (S shows the pane on Selection); the
Inspect tab's pick row (node, element, face, edge) is #28: reply on the issue and close
it once the user has tested it.

1. **Extras (optional).** Pin (freeze Inspect to compare two picks); back / forward
   through recent picks.
2. **Then decide** on the other floating windows. Keep as centred dialogs: Units,
   Formula, Strength materials, Mesh quality, About, Messages. Keep floating: History,
   Path, Convergence, Integrals (a bottom dock could come later). Candidates for more
   pane tabs: Find, Contact settings, Legend settings, Title block.

## 2. Idle load
With a model loaded and nothing happening, GPU use stays high and the CPU busy,
because `frame_cb` (sokol_app) redraws the scene and the UI at every vsync. Redraw
only when something changes (input, animation, video, loading, a window or pane edge
being dragged, a timer such as the message fade) and otherwise drop to a low rate or
reuse the last frame, without adding latency to the first frame after input. Check
native (D3D11/GL) and the browser. After 1, so the pane's drag and tabs are covered.

## 3. Open points (no order)

### Shell forces (#19)
- Reinforcement: transverse shear (the sandwich's core) and compression steel
  are not designed; the facet method (Capra-Maury) of Code_Aster is not offered.
- The sandwich's lever arm is fixed at h - 2c; Annex F iterates the inner layer
  (about 19 % more steel in pure bending than Code_Aster SSLS134).
- Qx Qy at the mid-side nodes of quadratic shells come from two points through
  the thickness only.

### Deck (#2)
- The ramp of a load inside a static step is not drawn (the end value is).
- LOAD CASE=2 (the imaginary part of a steady state dynamics load) is left out;
  a switch to show it instead of load case 1 could follow.

### Integrals
- Surfaces defined on shell elements use the shell's face numbers, which may not
  be the expanded solid's.
- The moment point (nodes:SET@x,y,z) is in model coordinates, not in the length
  shown.

### Display
- See-through STL layers are sorted as wholes, by their box centres: one layer
  inside another's box, or two interleaved, can still blend the wrong way
  (sorting triangles would fix it, at a cost per frame).
- `--field NAME:COMP` with an unknown component falls back to the default field
  and says so only in the message window; print it to stderr too, with the
  valid labels.
