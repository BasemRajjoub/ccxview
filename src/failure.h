/* failure.h -- failure criteria for unidirectional plies and isotropic materials,
   and the strength data they need. Headless: stresses in, failure measures out.

   Stresses are in the material axes, in MPa: s[6] = s11 s22 s33 t12 t23 t13 (1 the
   fibre direction, 2 and 3 transverse; .frd order xy yz zx). A UD ply is taken as
   transversely isotropic about the fibre: Z strengths equal Y strengths.

   Every criterion gives
     fi    its own failure index, as its authors define it (quadratic for Hashin,
           Tsai-Wu, LaRC matrix tension; linear for max stress, Puck's exposure);
     rf    the reserve factor: the factor on the whole stress state that brings it
           to failure (1 / exposure). Linear in load, so comparable across criteria;
     mode  the governing failure mode (CV_FM_*);
     angle the fracture plane angle in degrees where the criterion has one (Puck,
           LaRC matrix modes), else NaN.
   LaRC03 is a plane stress criterion: it reads s11, s22, t12 only. */
#ifndef CV_FAILURE_H
#define CV_FAILURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { CV_MK_UD, CV_MK_ISO };   /* material kind */

typedef struct {
    char   name[48];
    int    kind;                    /* CV_MK_ */
    /* elastic: UD 1 = fibre, 2 = 3 transverse; isotropic: E1 = E, nu12 = nu */
    double E1, E2, G12, nu12;
    /* UD strengths, all positive */
    double Xt, Xc, Yt, Yc, S12;     /* fibre tension / compression, transverse, in-plane shear */
    double S23;                     /* transverse shear; 0: from Yc and the fracture angle */
    double f12;                     /* Tsai-Wu interaction F12* (normalized), usually -0.5 */
    /* Puck: inclination parameters and fibre data for fibre fracture */
    double p12t, p12c, p23t, p23c;  /* p_perp_par(+), p_perp_par(-), p_perp_perp(+), p_perp_perp(-) */
    double E1f, nu12f, msf;         /* fibre modulus, fibre Poisson ratio, magnification factor */
    /* LaRC: fracture toughness, fracture angle under pure transverse compression */
    double GIc, GIIc, alpha0;       /* N/mm (kJ/m^2), N/mm, degrees */
    /* isotropic */
    double Sy, Sut, Suc;            /* yield, ultimate tensile, ultimate compressive */
} cv_fmat;

/* Where a ply sits, for LaRC's in-situ strengths: thickness (mm, 0 unknown) and
   whether it is embedded between plies of other orientations or an outer ply.
   CV_PLY_UD: a unidirectional laminate, no in-situ effect (the measured strengths). */
enum { CV_PLY_UD, CV_PLY_EMBEDDED, CV_PLY_OUTER };
typedef struct { double t; int pos; } cv_fply;

enum {
    CV_FC_MAXSTRESS, CV_FC_TSAIHILL, CV_FC_TSAIWU, CV_FC_HASHIN, CV_FC_PUCK, CV_FC_LARC03,
    CV_FC_LARC05, CV_FC_MISES, CV_FC_TRESCA, CV_FC_MOHR,
    CV_FC_AUTO,                     /* per material: cv_fc_auto */
    CV_FC_N
};

enum {
    CV_FM_NONE,
    CV_FM_FT, CV_FM_FC,             /* fibre tension, fibre compression (kinking) */
    CV_FM_MT, CV_FM_MC,             /* matrix (inter-fibre) tension, compression */
    CV_FM_S12, CV_FM_S23,           /* shear (max stress) */
    CV_FM_IFFA, CV_FM_IFFB, CV_FM_IFFC,     /* Puck inter-fibre fracture modes */
    CV_FM_FKT, CV_FM_FKC,           /* LaRC fibre kinking with matrix tension / compression */
    CV_FM_SPLIT,                    /* LaRC05 fibre splitting (compression below Xc/2) */
    CV_FM_YIELD, CV_FM_FRACT,       /* isotropic: yield, brittle fracture */
    CV_FM_N
};

/* fe_f, fe_m: the exposures (1 / reserve factor) of the fibre and the matrix
   (inter-fibre) modes alone, where the criterion separates them, else NaN */
typedef struct { float fi, rf, angle, fe_f, fe_m; uint8_t mode; } cv_fres;

/* criterion info: short name (field component), long name, true for UD criteria */
const char* cv_fc_name(int c);
const char* cv_fc_title(int c);
bool        cv_fc_ud(int c);
const char* cv_fm_name(int mode);   /* "FT", "MC", "IFF A", ... */

void cv_fail_eval(int crit, const cv_fmat* m, const cv_fply* ply, const double s[6], cv_fres* r);

/* The strengths LaRC uses for a ply: in-situ YT, SL (thin or thick ply, embedded or
   outer), and ST, the transverse shear strength from Yc and alpha0. */
void cv_larc_insitu(const cv_fmat* m, const cv_fply* ply, double* yt, double* sl, double* st);

/* ---- material data ---- */

/* the criterion CV_FC_AUTO uses for a material: LaRC05 for a UD ply (Puck, Hashin,
   max stress when it lacks the data), von Mises for a ductile isotropic material
   (Sy given), Mohr-Coulomb for a brittle one (Sut only); -1 when none can be used */
int  cv_fc_auto(const cv_fmat* m);

void cv_fmat_defaults(cv_fmat* m, int kind);    /* zero strengths, typical CFRP slopes */
bool cv_fmat_valid(const cv_fmat* m, int crit, char* why, size_t n);   /* has what crit needs */

/* the editable properties, for the editor and the settings file */
enum { CV_FU_NONE, CV_FU_MPA, CV_FU_DEG, CV_FU_NMM, CV_FU_N };
typedef struct {
    const char* key;                /* settings key, also the short label */
    const char* label;
    int         unit;               /* CV_FU_ */
    int         kinds;              /* bit (1 << CV_MK_) for the kinds it applies to */
    size_t      off;                /* offsetof(cv_fmat, ...) */
    const char* tip;
} cv_fprop;
int  cv_fprops(const cv_fprop** p);
double* cv_fmat_val(cv_fmat* m, const cv_fprop* p);

/* "kind=ud E1=135000 ..." and back; parsing starts from the defaults of the kind */
void cv_fmat_format(const cv_fmat* m, char* buf, size_t n);
bool cv_fmat_parse(const char* s, cv_fmat* m);

/* built-in templates: well documented ply and metal data, with their sources */
typedef struct { cv_fmat m; const char* source; } cv_ftemplate;
int cv_ftemplates(const cv_ftemplate** t);
/* The template a material name points to ("S355 plate", "IM7-8552", "PA66 GF0"):
   upper case letters and digits only, matched against known grade names; kind
   CV_MK_ or -1 for either. -1 none. */
int cv_ftemplate_by_name(const char* name, int kind);
/* The template nearest to elastic data, all in MPa: UD by E1 and E2; isotropic
   by E and nu, among plastics (E < 10 GPa) or metals, and by the yield stress
   where sy > 0. -1 none. */
int cv_ftemplate_nearest(int kind, double E1, double E2, double nu, double sy);

#endif
