# Using ccxview

Open a file from the command line, with Open... (Ctrl+O), by dropping it on the
window, or by typing a path in the box. A `.stl` dropped on the window (or picked
with Import STL... in Groups > Imported geometry or View > File) is added to the
open model as imported geometry instead.

## Command line

```sh
ccxview model.frd
ccxview model.frd --shot out.png            # render, save, quit
ccxview model.frd --export mp4              # animation video
ccxview model.frd --field DISP --vectors    # displacement arrows
ccxview model.frd --gp                      # Gauss points
ccxview model.frd --field STRESS --tensor ellipsoid   # stress glyphs: ellipsoid, superquadric, cross,
                                                      #   schultz-kindlmann, reynolds, hwy
ccxview model.frd --field STRESS --trajectories both  # principal stress trajectories: s1, s3, both
ccxview model.frd --field DISP --history 17 # node 17 over all steps
ccxview model.frd --field STRESS:SXX        # a field and its component, as the Fields tree names it
ccxview plate.frd --field SHELL:Mxx         # shell section forces (with the deck): Nxx Nyy Nxy Mxx Myy Mxy Qx Qy
ccxview model.frd --calc "S1 - S3"          # a calculated field (Tresca)
ccxview model.frd --fail auto               # failure, per material; built-in presets where none assigned
ccxview model.frd --fail larc05:rf          # a failure criterion (strength materials in ccxview.ini)
ccxview model.frd --mesh quality            # mesh quality: scores quality hmqi ansys abaqus; measures size
                                            #   edgemin edgemax aspect sjac jratio skew anglemin anglemax warp shape
ccxview model.frd --linearize 38,54         # ASME stress linearization, node 38 to 54
ccxview model.frd --measure dist:16,71 --measure circle:16,419,1171 --measure-window
                                            # measurements by node id, repeatable: dist:A,B  angle:A,B,C (at B)
                                            #   circle:A,B,C; --opt meas_show=0|1|2: their labels give
                                            #   undeformed -> deformed, the undeformed, the deformed value
ccxview model.frd --field STRESS --integrate volume:EALL   # Integrals window: KIND volume (element set), surface
                                            #   (surface or a set's outer faces), nodes (a sum); TARGET a set or
                                            #   surface name, selection or shown; nodes:NSET@x,y,z: moment about x,y,z
ccxview model.frd --field FORC --integrate nodes:NLEFT --integrate-csv rf.csv   # headless: every step's row
                                            #   to the CSV, no window (field: --field, else STRESS / FORC for nodes)
ccxview run2.frd --compare run1.frd         # difference of two runs
ccxview model.frd --stl pin.stl --stl alt.stl --stl-alpha 0.4   # geometry that was not analysed, shown with
                                            #   the results (repeatable), see-through
ccxview model.frd --step 3 --look +z        # a step, a view direction
ccxview model.frd --fly-clip 0.01           # free flight, the model cut 1% of its size ahead of the eye
ccxview model.frd --fly-hide 0.01           # ... or whole elements there hidden
ccxview model.frd --box 0.5,0.2,0.9,0.8     # box selection (fractions of the view; x0 > x1: crossing)
ccxview model.frd --find 120 --details      # probe node 120, its Details window open
ccxview --about                             # who made it, its licence, the libraries it uses
ccxview model.frd --menu 0.5,0.5            # the context menu at that point of the view (fractions)
ccxview model.frd --range 50,150 --opt oor_above=3  # the legend locked to 50 .. 150; values above it hidden (oor_above / oor_below: 0 the map's end colour, 1 grey, 2 a colour, 3 hidden)
ccxview model.frd --title-block --opt "title_text1=Bracket rev B"  # the title block, a project line (title_label1..3 / title_text1..3)
ccxview model.frd --title-block --opt title_user=0 --opt title_file_date=1   # lines off by key: title_heading file solver analysis step scale units user date
ccxview model.frd --labels node,value,loads # labels on the model, any mix of: node elem value evalue sets links loads supports materials gpvalue gpid minmax
ccxview model.inp --opt sym_thin=1          # crowded supports and loads thinned to a pattern
ccxview model.frd --mesh-window --opt mq_lim_aspect=5 --opt mesh_warn_pct=2   # mesh report, own limits
ccxview model.frd --opt key=value           # any setting of ccxview.ini for this run
ccxview model.frd --software                # CPU rendering
ccxview --check model.frd                   # headless parse for CI
ccxview --version
```

`--opt` takes every key of `ccxview.ini`, for example `--opt bands=0`,
`--opt clip_on=1 --opt clip_axis=0 --opt clip_pos=0.4`, `--opt ui_theme="Catppuccin Latte"`,
`--opt sym_auto=0 --opt sym_size=5`, `--opt mid_faces=0`.

## Mouse and keys

| Input | Action |
|---|---|
| left drag / right or middle drag / wheel | orbit / pan / zoom, about the point under the cursor (View panel: up axis Y or Z, turntable or free rotation, cursor pivot on/off, rotation centre mark) |
| X / Y / Z held + drag | orbit about that world axis only |
| Ctrl+drag / Ctrl+right drag | box zoom / zoom by dragging up and down |
| Ctrl+Shift+drag | box selection, as in CAD: left to right takes the elements wholly inside (blue), right to left the ones it touches (green); selected elements are toned yellow and outlined, selected nodes are magenta dots, the probe goes to the field's max over them, the min beside it (also a button in Colours & legend). "details..." in the Probe shows all about the node, element and selection |
| Alt+drag, Alt+← → | roll about the line of sight |
| middle click, C | centre the view on the point under the cursor (new rotation centre) |
| right click (no drag) | context menu: on the model probe, details, centre, look at the face, zoom to the element, history, path, through the wall, measure (distance, angle, circle from this node), clip here, copy, hide this element / material / type / set or show only it, integrate over its set; with a box selection hide or show only it, go to its max / min, copy its ids, save it as CSV, integrate over it; always fit, look from, show all, and on empty space reset, view back / forward, orthographic, free flight, save a picture |
| N | look normal to the face under the cursor |
| Ctrl+← → ↑ ↓ | turn the view 15° (with Shift 90°) |
| Ctrl+Z / Ctrl+Y | view back / forward |
| click / double-click | probe / zoom to element |
| F, R, 1..6 | fit, reset, look from ±X ±Y ±Z |
| space, ← → | play, step |
| G | free flight (WASD, Esc to leave); View > Camera: the "eye" list cuts what lies just ahead, or hides whole elements there, to fly through walls |
| H | view only, for screenshots |
| click the legend's unit / right-click the legend | units / legend settings |
| right-click the title block | its lines, date and free text (drag it to move it, like the legend) |
| Ctrl+O / Ctrl+E / Ctrl+F | open / export PNG / find |
| Esc | cancel a pending pick (path end, measurement nodes), close the menu, clear the selection |

## Where things are

- **Layers** (left panel): faces, edges, outline, nodes, Gauss points, vectors,
  tensor glyphs, stress trajectories, supports, loads, springs.
- **Groups**: element types, materials, sets and surfaces of the deck (right
  click a set or surface: integrate over it); Imported geometry: STL files shown with the results, each with show / hide, colour, opacity and unit scale.
- **Fields**: the results of the step, their components and invariants,
  the shell section forces (SHELL, with the deck), integrate... (the field's integrals over a set at every step),
  calculated fields, failure criteria (Strength materials... for the data),
  mesh quality (Mesh quality... for the summary and worst elements).
- **View**: camera, colours and legend, symbol sizes, mirror, replicate, cyclic
  symmetry, clip and crop; the Measurements window.
- **Toolbar**: deformation scale, animation, colour map, bands.
- **Status bar**: units, messages.
