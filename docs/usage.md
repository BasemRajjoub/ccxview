# Using ccxview

Open a file from the command line, with Open... (Ctrl+O), by dropping it on the
window, or by typing a path in the box.

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
ccxview model.frd --calc "S1 - S3"          # a calculated field (Tresca)
ccxview model.frd --fail auto               # failure, per material; built-in presets where none assigned
ccxview model.frd --fail larc05:rf          # a failure criterion (strength materials in ccxview.ini)
ccxview model.frd --mesh quality            # mesh quality: scores quality hmqi ansys abaqus; measures size
                                            #   edgemin edgemax aspect sjac jratio skew anglemin anglemax warp shape
ccxview model.frd --linearize 38,54         # ASME stress linearization, node 38 to 54
ccxview run2.frd --compare run1.frd         # difference of two runs
ccxview model.frd --step 3 --look +z        # a step, a view direction
ccxview model.frd --fly-clip 0.01           # free flight, the model cut 1% of its size ahead of the eye
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
| Alt+drag, Alt+← → | roll about the line of sight |
| middle click, C | centre the view on the point under the cursor (new rotation centre) |
| N | look normal to the face under the cursor |
| Ctrl+← → ↑ ↓ | turn the view 15° (with Shift 90°) |
| Ctrl+Z / Ctrl+Y | view back / forward |
| click / double-click | probe / zoom to element |
| F, R, 1..6 | fit, reset, look from ±X ±Y ±Z |
| space, ← → | play, step |
| G | free flight (WASD, Esc to leave); View > Camera: "Clip at eye" cuts what lies just ahead, to fly through walls |
| H | view only, for screenshots |
| click the legend's unit / right-click the legend | units / legend settings |
| Ctrl+O / Ctrl+E / Ctrl+F | open / export PNG / find |

## Where things are

- **Layers** (left panel): faces, edges, outline, nodes, Gauss points, vectors,
  tensor glyphs, stress trajectories, supports, loads, springs.
- **Groups**: element types, materials, sets and surfaces of the deck.
- **Fields**: the results of the step, their components and invariants,
  calculated fields, failure criteria (Strength materials... for the data),
  mesh quality (Mesh quality... for the summary and worst elements).
- **View**: camera, colours and legend, symbol sizes, mirror, replicate, cyclic
  symmetry, clip and crop.
- **Toolbar**: deformation scale, animation, colour map, bands.
- **Status bar**: units, messages.
