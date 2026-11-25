/* look.h -- the "Studio" look's surface materials (headless).

   A small catalog of physically based surface materials (base colour, metallic,
   roughness) and a guess of which one a deck material is, from its name and,
   failing that, its constants: E, Poisson's ratio and density, read in whichever
   consistent unit system makes them a known material. The guess only picks how a
   part looks; it never changes a value. */
#ifndef CV_LOOK_H
#define CV_LOOK_H

#include <stdbool.h>
#include "inp.h"

enum { CV_LOOK_DEFAULT,      /* neutral light grey, satin: nothing known */
       CV_LOOK_STEEL, CV_LOOK_STAINLESS, CV_LOOK_CAST_IRON, CV_LOOK_ALUMINIUM, CV_LOOK_TITANIUM,
       CV_LOOK_COPPER, CV_LOOK_BRASS, CV_LOOK_MAGNESIUM,
       CV_LOOK_CONCRETE, CV_LOOK_GLASS, CV_LOOK_CERAMIC, CV_LOOK_WOOD,
       CV_LOOK_CFRP, CV_LOOK_GFRP, CV_LOOK_PLASTIC, CV_LOOK_RUBBER,
       CV_LOOK_PAINTED,      /* glossy paint: a plain colour that reads well under a field */
       CV_LOOK_N };

typedef struct {
    const char* name;        /* "Steel", shown in the UI and written to the ini (case-insensitive) */
    float rgb[3];            /* base colour, linear (not sRGB) */
    float metallic;          /* 0 dielectric .. 1 metal */
    float roughness;         /* 0.05 mirror .. 1 matte (perceptual, squared in the BRDF) */
} cv_look_mat;

extern const cv_look_mat cv_look_mats[CV_LOOK_N];

/* the preset by name, case-insensitive; -1 when none */
int cv_look_find(const char* name);

/* Which preset deck material `name` (with its constants p, NULL when none) looks like.
   1. Its name: a word in it (STEEL, S235, X5CRNI, INOX, ALU, AL6061, TI6AL4V, CONCRETE,
      C30/37, BETON, GLASS, RUBBER, EPDM, WOOD, TIMBER, CFRP, CARBON, GFRP, PA6, ABS, ...).
   2. Else its constants: E (the first of ENG / ORTHO, the larger of E1 E2 for a ply),
      nu when isotropic, rho when given, read in the unit system `sys` (CV_SYS_*, units.h)
      when one is set, else in each of the usual consistent systems, the one that makes
      them closest to a known material winning (mm-t-s with MPa first when it ties).
   3. Else CV_LOOK_DEFAULT.
   why (may be NULL, size n): a short reason for the UI, "name: STEEL", "E 210 GPa,
   rho 7850 kg/m3", "nothing known". */
int cv_look_guess(const char* name, const cv_matprop* p, int sys, char* why, int n);

#endif
