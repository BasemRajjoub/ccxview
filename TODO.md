# To do

Pre-processing stays out of scope. Work top to bottom: each step builds on the one
before it, so no step rewrites code an earlier one wrote. One commit per step.

## 1. GUI: Inspector pane (right) — then #28

Goal: a fixed pane on the right, mirror of the Scene pane on the left, replacing the
floating Probe and Details windows. Shown by default; a button at the top right of the
view (and a key, e.g. `]`; `[` for the Scene pane) hides / shows it; `H` still hides
everything. Both states kept in settings / `.ccxview`. Never opens or closes by itself
on a click (the view must not jump). Tabs at its top: **Inspect | Selection | Measure**.

1. **Refactor first — window bodies.** Split each tool window into
   `xxx_body(ctx, s, row)` (the rows) and its `nk_begin` wrapper: Selection
   (`ui_select.c`, already row helpers), Measurements (`ui_measure.c`), Probe +
   Details (`ui_windows.c`, `ui_info.c`). No behaviour change; ui-test passes as is.
   After this a body can sit in a floating window or a pane tab alike.
2. **Pane shell.** `ui_inspector.c`: fixed window at the right of `ui_frame`
   (`ui.c`, beside "Scene"), `G.vp_w` shrinks by its width, tab bar, toggle button +
   key, width resizable and saved; default width smaller than Scene's on narrow screens.
   Same for the left Scene pane: a show / hide button at its top left and a draggable
   edge to resize it (width kept with the other settings).
3. **Inspect tab = Probe + Details.** Content by what is picked:
   - nothing: model summary (counts, step / increment, field range, units);
   - node: id, coords, displacement, every field component, sets, elements around;
     history / path / measure / copy / labels buttons;
   - element: type, material, quality, failure, integration point values, node list
     (a node id click moves the Inspect tab to that node).
   Delete the Probe and Details windows; keep a one-line readout (`node 123 = 4.5e2`)
   in the status bar. Update `ui_test.c` cases that name "Probe" / "Details".
4. **Selection and Measure tabs.** The bodies from 1.1 in the pane; their floating
   windows go (`S` opens the pane on the Selection tab). Multi-selection summary in
   Inspect: count, max / min with go-to, integrate / hide / isolate / labels.
5. **#28 — pick kind.** Row at the top of Inspect: Node · Element · Face · Edge; what
   is picked is highlighted in the view (reuse the selection highlight). Face / edge
   content: element + face number, surfaces it is on, area, normal, field average.
   Face pick exists in Selection; edge is new (feature edge chain code may help).
   Reply on the issue and close it (only after the user has tested it).
6. **Extras (optional).** Pin (freeze Inspect to compare two picks); back / forward
   through recent picks.
7. **Then decide** on the other floating windows. Keep as centred dialogs: Units,
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
