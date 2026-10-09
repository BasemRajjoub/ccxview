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
ccxview slab.frd --field REBAR:As_x_bot --opt rebar_fcd=20 --opt rebar_fyd=435 --opt rebar_cover=40 --rebar-window
                                            # concrete shell reinforcement per width: As_x_top As_x_bot As_y_top
                                            #   As_y_bot As_max As_total Conc_ratio Crushing (model units)
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
                                            #   to the CSV, no window (field: --field, else STRESS / FORC for nodes);
                                            #   in the units the window would show: ccxview.ini, --opt units=1
                                            #   --opt unit_length=0 (an index: m) ..., then the model's .ccxview
ccxview run2.frd --compare run1.frd         # difference of two runs
ccxview model.frd --stl pin.stl --stl alt.stl --stl-alpha 0.4   # geometry that was not analysed, shown with
                                            #   the results (repeatable), see-through
ccxview model.frd --step 3 --look +z        # a step, a view direction
ccxview model.frd --fly-clip 0.01           # free flight, the model cut 1% of its size ahead of the eye
ccxview model.frd --fly-hide 0.01           # ... or whole elements there hidden
ccxview model.frd --box 0.5,0.2,0.9,0.8     # box selection (fractions of the view; x0 > x1: crossing)
ccxview model.frd --select set:EHOLE --select add:ids:1-20 --selection-window
                                            # selection steps, in order (see Selection below);
                                            #   --opt sel_filters=1: the window's filter rows open;
                                            #   sel_open_pick / _names / _use / _named=0|1: its other parts
ccxview model.frd --find 120 --details      # probe node 120, its Details window open
ccxview --about                             # who made it, its licence, the libraries it uses
ccxview model.frd --menu 0.5,0.5            # the context menu at that point of the view (fractions)
ccxview model.frd --range 50,150 --opt oor_above=3  # the legend locked to 50 .. 150; values above it hidden (oor_above / oor_below: 0 the map's end colour, 1 grey, 2 a colour, 3 hidden)
ccxview model.frd --hide-set EHOLE         # hide a deck element set, the rest stays (repeatable; --set NAME ticks one)
ccxview model.frd --title-block --opt "title_text1=Bracket rev B"  # the title block, a project line (title_label1..3 / title_text1..3)
ccxview model.frd --title-block --opt title_user=0 --opt title_file_date=1   # lines off by key: title_heading file solver analysis step scale units user date
ccxview model.frd --labels node,value,loads # labels on the model, any mix of: node elem value evalue sets links loads supports materials gpvalue gpid minmax
ccxview model.frd --labels node,minmax --label-offset max:1:80,-60 --label-offset node:940:-40,30
                                            # a label moved by hand: KIND:ID:DX,DY px (x right, y down), repeatable;
                                            #   kinds node elem gp min max measure set link load support material
ccxview model.inp --opt sym_thin=1          # crowded supports and loads thinned to a pattern
ccxview model.frd --mesh-window --opt mq_lim_aspect=5 --opt mesh_warn_pct=2   # mesh report, own limits
ccxview model.frd --deck-window --step 2    # the Deck window: steps, model changes, submodel, amplitudes
ccxview model.frd --opt show_removed=1      # elements removed by *MODEL CHANGE drawn all the same
ccxview model.frd --no-sidecar              # neither read nor write model.ccxview this run
ccxview model.frd --opt key=value           # any setting of ccxview.ini for this run
ccxview model.frd --software                # CPU rendering
ccxview --check model.frd                   # headless parse for CI
ccxview --version
```

`--opt` takes every key of `ccxview.ini`, for example `--opt bands=0`,
`--opt clip_on=1 --opt clip_axis=0 --opt clip_pos=0.4`, `--opt ui_theme="Catppuccin Latte"`,
`--opt sym_auto=0 --opt sym_size=5`, `--opt mid_faces=0`.

## The model's .ccxview

`model.ccxview` beside the model holds what was set up for it (see
[features.md](features.md)): an INI like `ccxview.ini`, written by ccxview when
something changes, in parts with a `# part` line before each. A key that does not
fit the model is skipped; a missing key leaves that setting as it is.

| Part | Keys |
|---|---|
| sets | `elsets_on`, `elsets_hidden`, `nsets_on`, `surfaces_on`: set names, `, ` between |
| groups | `off_type`, `off_material`, `off_group`: the values switched off, as ranges (`1-4, 9`) |
| hidden | `hidden_elems`: element ids hidden by hand, as ranges |
| plain | symbols (`show_bc`, `show_loads`, `bc_scale` ...), legend look (`legend_fmt` ...), labels (`label_kinds`, `label_px` ...), `lin_asme`, `lin_q` |
| units | `units` (the system), `unit_in_<quantity>` and `unit_<quantity>`: unit names (`MPa`), empty for the system's / as input |
| view | the keys of a view state file: `cam_*`, `step`, `field`, `comp`, `calc`, `gauss_field`, `fail_field`, `mesh_field`, `elem_mode`, `csys*`, `deform*`, `range_lock`, `rmin`, `rmax`, `clip_*`, `crop_*`, `mirror*`, `rep*`, `cyc_*`, the layers, `faces_mode`, `cmap`, `bands` |
| compare | `compare` (the other run's path), `compare_diff` |
| paths | `path` (`38, 54`: two node ids, or `38, normal` / `x` / `y` / `z`), `path_surface`, `path_open`, `path_lin`, `history`, `history_elem` |
| scl | `scl1`, `scl2` ...: `name; ax,ay,az; bx,by,bz; node; node or normal / x / y / z` (the nodes rebuild the line, the points are for reading) |
| stl | `stl1`, `stl2` ...: `shown (1 / 0); opacity; r,g,b; scale; path`, the path relative to the model's folder when the file lies in it or below it, else absolute; a file not found is left out with a message |
| measure | `measure1`, `measure2` ...: `distance 12 40`, `angle 1 2 3`, `circle 1 2 3` (file node ids) |
| labels | `label1`, `label2` ...: labels moved by hand, `node 940 12 -30`: the key's kind and id (a name may hold spaces), the offset in px from where the label would be |
| selections | `selection1`, `selection2` ...: a kept selection's name; `selection1_elems`, `selection1_nodes`: its element and node ids as ranges |

A long value continues in `key_2`, `key_3` ... .

## Selection

`--select SPEC` is repeatable and runs in order once the model has loaded (after
`--box`, before `--integrate`, so `--integrate volume:selection` works on it). A
SPEC may start with a mode, `new:` (the default), `add:`, `remove:` or `and:`
(intersect), then:

| SPEC | What it selects |
|---|---|
| `ids:1-100,205` / `nids:1-100` | element ids / node ids |
| `set:NAME`, `surf:NAME`, `NAME` | a deck set or surface (a bare name: a set, else a surface) |
| `type:C3D20R`, `mat:STEEL`, `mat:2` | every element of a type, of a material (by name or number) |
| `invert` | what is shown and not selected |
| `nodes`, `elements`, `elements-any` | the selected elements' nodes; the elements with every (any) node selected |
| `grow`, `shrink`, `boundary` | one layer more / less; the boundary's nodes and the elements on it |
| `lasso:x,y,x,y,...` | a lasso through those points (fractions of the view), the elements wholly inside |
| `part:EID` | every element connected to element EID |
| `face:EID:S3`, `face:EID` | the outer faces from that face of the element (any of its outer faces) up to the feature edges |
| `chain:NID` | the nodes along the feature edges through node NID, up to the corners |
| `takes:elements\|nodes\|both`, `facing:on\|off` | what the next steps take |
| `filter:x>10`, `filter:10<y<20`, `filter:r<5`, `filter:theta>30`, `filter:axial<2` | keep what lies there (of the selection, or of everything shown when nothing is) |
| `axis:z@x,y,z` | the axis r, theta and axial are about (through 0,0,0 when no point is given) |
| `field>100`, `field<5`, `top:5` | keep what the field shown puts above, below 100 / 5, in its top 5 % |
| `filter:type:C3D20R`, `filter:mat:STEEL`, `facing` | keep one element type, one material, the side facing the camera |
| `keep:NAME`, `named:NAME` | keep the selection under NAME (in model.ccxview); take a kept one (by the mode) |
| `hide`, `isolate`, `crop`, `clip`, `labels` | hide it, show only it, the crop box round it, the clip plane through its centre, labels on it only |
| `csv`, `inp:NAME` | `<model>_selection.csv`; `<model>_NAME.inp` with its `*ELSET` / `*NSET` lines named NAME |
| `clear` | nothing |

It prints what it selected and the field's max and min over it.

## Mouse and keys

| Input | Action |
|---|---|
| left drag / right or middle drag / wheel | orbit / pan / zoom, about the point under the cursor (View panel: up axis Y or Z, turntable or free rotation, cursor pivot on/off, rotation centre mark) |
| X / Y / Z held + drag | orbit about that world axis only |
| drag a label | move it (the camera stays); right-click it: Reset label |
| Ctrl+drag / Ctrl+right drag | box zoom / zoom by dragging up and down |
| Ctrl+Shift+drag | box selection, as in CAD: left to right takes the elements wholly inside (blue), right to left the ones it touches (green), by the Selection window's mode; selected elements are toned yellow and outlined, selected nodes are magenta dots, the probe goes to the field's max over them, the min beside it (also a button in Colours & legend). "details..." in the Probe shows all about the node, element and selection |
| S | the Selection window: mode, box, click, lasso, faces, edge chain and part tools, invert, conversions, grow, shrink, boundary, by name, by id |
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
| Esc | cancel a pending pick (path end, measurement nodes), close the menu, put a selection tool down, clear the selection |

## Where things are

- **Layers** (left panel): faces, edges, outline, nodes, Gauss points, vectors,
  tensor glyphs, stress trajectories, supports, loads, springs.
- **Groups**: element types, materials, sets and surfaces of the deck (right
  click a set or surface: integrate over it); the eye before an element set hides
  it, the tick shows only the ticked sets; Imported geometry: STL files shown with
  the results, each with show / hide, colour, opacity and unit scale.
- **Fields**: the results of the step, their components and invariants,
  the shell section forces (SHELL, with the deck) and the reinforcement of
  concrete shells from them (REBAR; Reinforcement... for fcd, fyd, cover), integrate... (the field's integrals over a set at every step),
  calculated fields, failure criteria (Strength materials... for the data),
  mesh quality (Mesh quality... for the summary and worst elements).
- **View**: camera, colours and legend, symbol sizes, mirror, replicate, cyclic
  symmetry, clip and crop; the Measurements window.
- **Toolbar**: deformation scale, animation, colour map, bands.
- **Status bar**: units, messages.
