# To do

Open GitHub issues not yet started, with what was discussed.

## #5 Store post operations beside the results
Linearization lines, cuts, mirror copies, unit changes, probes: keep them so a
model can be reopened with its post-processing. Writing them as comments into
the .frd was proposed; objection: a .frd still being written by the solver
cannot be edited. Plan: a sidecar file `name.ccxview` beside the .frd (ini,
same reader as the settings), loaded when the .frd opens, saved on change.

## From PrePoMax, agreed 7 Oct 2026 (in this order)

1. Deformation scale presets: a list beside the scale box: true, auto, auto x0.25,
   x0.5, x2, x5, user.
2. Colour maps not yet in the list: Cividis, Plasma, Black body, Kindlmann,
   Rainbow desaturated, Warm, Cool (tables for make_cmap).
3. Legend background and border (none by default; white for pictures on a page).
4. Transparent background in the PNG (a checkbox in the export; the swapchain
   cleared to alpha 0 and the PNG written with alpha). White background preset
   exists already.
5. Measurements as labels (the label system): distance between two nodes with
   dx dy dz, angle through three nodes, circle radius and centre through three;
   deformed and undeformed values.
6. Integrals of a field as history: surface integral over a set of faces (total
   force from a pressure or a reaction), volume integral and volume average over
   an element set. The volume average is the homogenised value of an RVE
   (<s> = 1/V sum s_e V_e, <e> likewise), so a set's homogenised stress and strain
   per step come out of the same code; a CSV of them.

Dropped: animation once, solid undeformed ghost, set area / volume / mass summary.

## #19 Shell section forces
A Fields subgroup "Shell forces": Nxx Nyy Nxy, Mxx Myy Mxy, Qx Qy per unit width,
from the nodal stresses of the expanded shell elements (cv_elemmap gives the
shell, layer, thickness and local axes of each): stresses turned into the
shell's local system, integrated through the thickness (two points for linear,
three for quadratic shells; N = int s dz, M = int s z dz, Q = int tau dz),
layers summed, averaged at the shell nodes, written on bottom and top nodes so
the contour sits on the expanded shell. Headless shell.c + t_shell.h (a
cantilever plate with a known root moment), --field key, units N/mm and
N mm/mm. Later: reinforcement design from them (Wood-Armer / Capra-Maury, as
Code_Aster's CALC_FERRAILLAGE). Plan posted in the issue, 8 Oct 2026.

## Movable labels
Drag a label on the model to where it reads best. Hit test on the laid-out
boxes (screen space, kept per frame); the press wins over the camera as a
window's does. Per label a pixel offset from its point, keyed (kind, id):
("node", 940), ("max", 2), ("set", "EHOLE"); pixels, so it stays by its point
through zoom and turn. A moved label is pinned (never thinned, the others keep
clear of it) with a leader line from the point: both exist since the min / max
labels. Right-click on a label: "reset label"; the panel: "reset moved labels".
Saved per model in the `.ccxview` sidecar (#5), lines like `label node 940 12
-30`; stale keys after a renumbering are harmless. Do it once the sidecar exists.

## #23 Geometry that was not analysed, beside the results
Sergio: parts of the assembly that were not simulated, for the picture. STL is
simple (binary and ASCII, a triangle list; no ids, no results): a mesh-only
layer with show / hide and transparency, listed in Groups, remembered by the
sidecar (#5). Answered: doable, but held while labels and the sidecar land.

## #6 Mesh quality report and field
Aspect ratio, skewness, Jacobian ratio, volume ratio, warpage per element;
a summary report and a per-element field to colour the model. User-set
limits good / warning / bad, and a tolerated percentage of bad elements
(bad elements away from hot spots are a warning, not a failure). Reference:
https://github.com/eigemx/neatmesh. Headless module (quality.c + t_quality.h),
a Fields entry "Mesh quality", a report window.

## #7 Command line like cgx
xyont asks for cgx-style commands driving the view; Sergio notes it would
duplicate the UI; xyont settles for a simple text editor for the .fbd. Park:
at most a text box to run one cgx command on the loaded .fbd.

## #2 Keywords and results coverage
Supports and loads follow the step on screen, with a symbol per kind
(docs/keywords.md, samples/symbols). Still to do: *SUBMODEL (mark the driven
nodes), *MODEL CHANGE (hide removed elements per step), amplitudes, and a
documented list of the result fields ccxview understands.
