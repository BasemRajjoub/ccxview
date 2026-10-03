# To do

Open GitHub issues not yet started, with what was discussed.

## #5 Store post operations beside the results
Linearization lines, cuts, mirror copies, unit changes, probes: keep them so a
model can be reopened with its post-processing. Writing them as comments into
the .frd was proposed; objection: a .frd still being written by the solver
cannot be edited. Plan: a sidecar file `name.ccxview` beside the .frd (ini,
same reader as the settings), loaded when the .frd opens, saved on change.

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

## Tensor glyphs (stress and strain as ellipsoids)
One glyph per element centroid (per Gauss point when a .dat field is selected):
an ellipsoid with semi-axes |s1|, |s2|, |s3| along the principal directions,
coloured by the selected scalar, and a second style, the principal cross
(three segments, red tension, blue compression). Drawn as ray-traced
impostors like the node balls, so a million glyphs cost nothing; auto scale
to about 1.5 element sizes with a slider; hidden by "hide when dense";
follow the deformed shape through the displacement attribute. The
eigenvectors are there (cv_principal_dirs in field.c, Jacobi, used by the
principal arrows); still needed: an ellipsoid impostor shader in render.c,
the glyph build next to the arrows in app_overlay.c, a Layers row,
`--tensor ellipsoid|cross`, settings. About a day. Shells and beams give a
disc (one zero axis), which is right.
