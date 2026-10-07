# To do

Open GitHub issues not yet started, with what was discussed.

## #5 Store post operations beside the results
Linearization lines, cuts, mirror copies, unit changes, probes: keep them so a
model can be reopened with its post-processing. Writing them as comments into
the .frd was proposed; objection: a .frd still being written by the solver
cannot be edited. Plan: a sidecar file `name.ccxview` beside the .frd (ini,
same reader as the settings), loaded when the .frd opens, saved on change.

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
