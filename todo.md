# To do

Requests from the forum thread
[ccxview: results viewer for CalculiX](https://calculix.discourse.group/t/ccxview-results-viewer-for-calculix/4134),
not done yet.

## Display

- [x] **Out-of-range colours**: with the legend range locked, values above the max
  and below the min get their own colours (e.g. grey / magenta) instead of the end
  colours of the map. *(linth)* Above light grey, below darker grey, both shown
  in the legend.
- [x] **More colour maps**, rainbow / jet among them. *(Calc_em)* Jet, Inferno and Rainbow desaturated
  (xyont) added; Rainbow,
  Turbo, Viridis, Cool-warm, Heat were there already (legend grey = greyscale).
- [ ] **Filled clip caps**: fill the cut where the clip plane goes through solid
  elements, instead of showing the open inside. *(Calc_em)*
- [x] **Separate glyph sizes**: restraints smaller than loads. *(SergioP)* Now a mesh-based size with
  sliders under View > Symbol sizes (`bc_scale`, `load_scale`).
- [ ] **Windows icon**: a better application icon. *(SergioP)*

## Post-processing

- [x] **Principal stresses and strains**: S1, S2, S3 (and E1..E3) as derived
  components of STRESS / TOSTRAIN, maybe with principal direction arrows. *(linth)* Values done (S1..S3,
  E1..E3, also on .dat Gauss points); with S1..S3 of a .frd field chosen, Layers > Directions draws
  arrow pairs along the principal direction at the nodes (out: tension, in: compression).
- [x] **Coordinate transformation**: results in a cylindrical (or user) system,
  e.g. radial / hoop / axial stress. *(linth)* Fields > coordinates: cylindrical about
  X, Y or Z through a point; vectors (Dr Dt Da) and tensors (Srr Stt Saa Srt Sta Sar) of the
  .frd turned per node, invariants unchanged; saved in the view state.
- [ ] **Cyclic symmetry expansion** beyond the NGRAPH sectors written by ccx. *(linth)*
- [ ] **ASME stress linearization** along a path through the thickness: membrane,
  bending, peak. Could build on the existing path plot. *(SergioP)*
- [ ] **Time / increment plots**: a value at a node or element against time or
  increment, over all steps. *(SergioP)*
- [x] **Unit systems**: the user picks a unit set, results show units in the
  legend and probe. *(SergioP)* Legend settings > units (t-mm-s, kg-m-s, in-lbf-s);
  the unit follows the field (length, stress, force, temperature, energy density, ...)
  in the legend, probe, path plot and CSV header.

## Modelling

- [ ] **Build models from .fbd**: mesh the cgx geometry (not only show it), i.e.
  run the cgx script far enough to get nodes and elements. *(xyont)*
