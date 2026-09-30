# To do

Requests from the forum thread
[ccxview: results viewer for CalculiX](https://calculix.discourse.group/t/ccxview-results-viewer-for-calculix/4134),
not done yet.

## Display

- [x] **Out-of-range colours**: with the legend range locked, values above the max
  and below the min get their own colours (e.g. grey / magenta) instead of the end
  colours of the map. *(linth)* Above light grey, below darker grey, both shown
  in the legend.
- [x] **More colour maps**, rainbow / jet among them. *(Calc_em)* Jet and Inferno added; Rainbow,
  Turbo, Viridis, Cool-warm, Heat were there already (legend grey = greyscale).
- [ ] **Filled clip caps**: fill the cut where the clip plane goes through solid
  elements, instead of showing the open inside. *(Calc_em)*
- [x] **Separate glyph sizes**: restraints smaller than loads. *(SergioP)* Now `BC %`
  and `load %` (`bc_pct`, `load_pct`; an old `glyph_pct` sets both).
- [ ] **Windows icon**: a better application icon. *(SergioP)*

## Post-processing

- [ ] **Principal stresses and strains**: S1, S2, S3 (and E1..E3) as derived
  components of STRESS / TOSTRAIN, maybe with principal direction arrows. *(linth)*
- [ ] **Coordinate transformation**: results in a cylindrical (or user) system,
  e.g. radial / hoop / axial stress. *(linth)*
- [ ] **Cyclic symmetry expansion** beyond the NGRAPH sectors written by ccx. *(linth)*
- [ ] **ASME stress linearization** along a path through the thickness: membrane,
  bending, peak. Could build on the existing path plot. *(SergioP)*
- [ ] **Time / increment plots**: a value at a node or element against time or
  increment, over all steps. *(SergioP)*
- [ ] **Unit systems**: the user picks a unit set, results show units in the
  legend and probe. *(SergioP)*

## Modelling

- [ ] **Build models from .fbd**: mesh the cgx geometry (not only show it), i.e.
  run the cgx script far enough to get nodes and elements. *(xyont)*
