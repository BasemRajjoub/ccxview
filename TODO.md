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
*TRANSFORM is handled (results in local systems turned back to global).
Still to do: *STEP tracking of loads and supports (today every step's are
collected, last wins), *DSLOAD, *TEMPERATURE as a drawable load, and a
documented list of the result fields ccxview understands (docs/keywords.md
has the deck side).
