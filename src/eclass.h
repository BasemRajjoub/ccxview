/* eclass.h -- elements sorted into classes for colouring the faces: by element set
   (the smallest set holding each, so parts show rather than EALL), by section (the
   last naming it); and the categorical colours every such colouring uses. Headless. */
#ifndef CV_ECLASS_H
#define CV_ECLASS_H

#include "inp.h"

typedef struct {
    int       n;              /* classes; with none, the last holds the elements in none */
    bool      none;
    uint16_t* of_elem;        /* per element (mesh order): its class */
    char    (*name)[96];      /* per class: "EHOLE", "solid: EALL (STEEL)", "(no set)" */
} cv_eclass;

bool cv_eclass_sets(cv_eclass* c, const cv_frd* f, const cv_set* sets, int nsets);   /* false on OOM */
bool cv_eclass_sections(cv_eclass* c, const cv_frd* f, const cv_inp* d);
void cv_eclass_free(cv_eclass* c);

/* class i's colour: 12 clearly distinct, then golden-angle hues; the grey of none apart */
void cv_palette(int i, float rgb[3]);
extern const float cv_palette_none[3];

#endif
