/* rebar.h -- reinforcement of concrete shells from their section forces (shell.h).
   Headless: Nxx Nyy Nxy Mxx Myy Mxy per width and the thickness in; the steel
   areas per width each face needs in the x and y directions out.

   The three-layer sandwich model (Eurocode 2 Annex F, fib Model Code; Marti,
   "Design of concrete slabs for transverse shear", ACI SJ 1990) at the ultimate
   limit state, with the Wood-Armer / Nielsen rule in each layer: a standard
   alternative to the facet method (Capra-Maury) of Code_Aster's CALC_FERRAILLAGE.
   Two outer layers, centred on the bars of each face at a cover c from it,
   z = h - 2c apart, carry the forces (the core would carry the transverse shear,
   not checked here):
     top (+e3):    nx = Nxx / 2 + Mxx / z    ny = Nyy / 2 + Myy / z    nxy = Nxy / 2 + Mxy / z
     bottom (-e3): nx = Nxx / 2 - Mxx / z    ny = Nyy / 2 - Myy / z    nxy = Nxy / 2 - Mxy / z
   (Mxx positive when the +e3 side pulls, as shell.h). A layer, tension positive,
   cracked at 45 degrees (Annex F.1, the least steel in total):
     nx >= -|nxy| and ny >= -|nxy|:  Fx = nx + |nxy|, Fy = ny + |nxy|, concrete 2 |nxy|
     nx <  -|nxy|:                    Fx = 0, Fy = ny + nxy^2 / |nx|, concrete |nx| + nxy^2 / |nx|
     ny <  -|nxy|:                    the same, x and y swapped
     both in compression (nx ny >= nxy^2, nx, ny < 0): no steel, concrete the larger
                                      principal compression
   As = F / fyd. The concrete force is taken by the layer, 2c thick (h / 2 at most):
   the concrete crushes when F / t > fcd (give nu fcd for cracked concrete if the
   code asks for it); the steel is still given there. Compression steel is not
   designed. Units are the model's: with N and mm, fcd and fyd in MPa, c in mm,
   As in mm^2/mm (x 1000 for mm^2/m). */
#ifndef CV_REBAR_H
#define CV_REBAR_H

#include <stdbool.h>
#include <stddef.h>

enum { CV_RB_XT, CV_RB_XB, CV_RB_YT, CV_RB_YB, CV_RB_MAX, CV_RB_SUM, CV_RB_CONC, CV_RB_CRUSH, CV_RB_N };

const char* cv_rebar_comp(int c);   /* "As_x_top" .. "Crushing" */

typedef struct {
    double fcd;     /* design compressive strength of the concrete (> 0) */
    double fyd;     /* design yield strength of the steel (> 0) */
    double cover;   /* face to the centroid of the bars (> 0, < h / 2) */
} cv_rebar_par;

/* One membrane layer: nx ny nxy per width, tension positive, into the steel forces
   per width *fx *fy (>= 0) and the concrete's compression per width *fc (>= 0). */
void cv_rebar_layer(double nx, double ny, double nxy, double* fx, double* fy, double* fc);

/* One point: sf the section forces Nxx Nyy Nxy Mxx Myy Mxy (about the mid-surface),
   h the thickness. out[CV_RB_N]: the areas per width As_x_top As_x_bot As_y_top
   As_y_bot, their largest and their sum, the concrete's largest stress over fcd,
   1 where it is above 1 (crushing) else 0. False and NaN everywhere when a value
   is missing or the parameters do not fit h. */
bool cv_rebar_point(const double sf[6], double h, const cv_rebar_par* p, float out[CV_RB_N]);

/* Every node: sf CV_SF_N values per node (shell.h's order), h one per node; out
   CV_RB_N per node, NaN where there is no shell. */
void cv_rebar_field(const float* sf, const float* h, size_t n, const cv_rebar_par* p, float* out);

#endif
