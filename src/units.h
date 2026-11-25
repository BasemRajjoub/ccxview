/* units.h -- physical units of result fields. Headless.
   CalculiX has no units: a model is built in one consistent set (mm t s, m kg s,
   in lbf·s²/in s, ...) and every result comes out in that set. The file system
   says which set; each quantity can then be shown in any unit of its kind.
   Every unit is known by its SI value: si_value = value * si + off (only
   temperatures have an offset). A file unit is derived from the system's base
   units (length, mass, time) and the quantity's dimension. */
#ifndef CV_UNITS_H
#define CV_UNITS_H

#include <stdbool.h>

/* the kinds of quantity a result field can be */
enum {
    CV_Q_LEN, CV_Q_STRESS, CV_Q_FORCE, CV_Q_TEMP, CV_Q_STRAIN, CV_Q_VELO, CV_Q_ACC,
    CV_Q_ENERGY_D, CV_Q_FLUX, CV_Q_POWER, CV_Q_ENERGY, CV_Q_MASSFLOW, CV_Q_VOLUME, CV_Q_MASS,
    CV_Q_FORCE_LEN, CV_Q_MOMENT_LEN,      /* per unit width: shell section forces and moments */
    CV_Q_AREA_LEN,                        /* an area per width: reinforcement (rebar.h) */
    CV_Q_N
};

/* the consistent unit systems a model can be built in (the first four are the
   values the old "units" setting had) */
enum {
    CV_SYS_NONE, CV_SYS_MM_T_S, CV_SYS_M_KG_S, CV_SYS_IN_LBF_S, CV_SYS_MM_KG_MS,
    CV_SYS_MM_G_MS, CV_SYS_CM_G_S, CV_SYS_FT_SLUG_S, CV_SYS_N
};

/* temperature scales (a file's temperature unit is chosen apart from its system) */
enum { CV_TEMP_C, CV_TEMP_K, CV_TEMP_F, CV_TEMP_R, CV_TEMP_N };

/* display presets: index -1 in a quantity means "as input" */
enum { CV_SHOW_FILE, CV_SHOW_SI_M, CV_SHOW_SI_MM, CV_SHOW_US_IN, CV_SHOW_US_FT, CV_SHOW_N };

typedef struct { const char* name; double si, off; } cv_unit;

const char*    cv_quantity_name(int q);          /* "Length", "Stress / pressure", ... */
const char*    cv_quantity_key(int q);           /* "length", "stress", "heat_flux", ...: for files */
int            cv_unit_count(int q);
const cv_unit* cv_unit_get(int q, int i);         /* NULL when out of range */
int            cv_unit_find(int q, const char* name);   /* index, -1 when unknown */

const char* cv_sys_name(int sys);                 /* "SI (mm, MPa)", as the display presets */
const char* cv_sys_base(int sys);                 /* "mm, t, s  ->  N, MPa": base units, then force and stress */
int         cv_sys_temp(int sys);                 /* the usual temperature scale */
double      cv_sys_si(int sys, int q);            /* SI value of one file unit of q (0: no system) */
/* the unit of q in the file (index into q's list), -1 without a system or a match;
   temperature follows temp, a strain is always a plain ratio */
int         cv_sys_unit(int sys, int temp, int q);

const char* cv_show_name(int preset);             /* "As input", "SI (m, Pa)", ... */
int         cv_show_unit(int preset, int q);      /* unit index of q in a preset, -1: as in the file */

/* the unit q is in, in the file: `in` when set (>= 0, the user's choice for this
   quantity), else the system's unit (cv_sys_unit); -1 when neither says */
int  cv_unit_input(int sys, int temp, int in, int q);
/* shown = file * k + off for quantity q in input unit `in` (-1: the system's) shown
   in unit `show` (-1: as input). false (k 1, off 0) when there is nothing to convert
   or nothing known to convert from. */
bool cv_unit_conv(int sys, int temp, int in, int q, int show, double* k, double* off);
/* the unit q is shown in: index into q's list, -1 when there is none to name */
int  cv_unit_shown(int sys, int temp, int in, int q, int show);

/* the quantity of component comp (-1: an invariant) of a .frd field ("STRESS")
   or .dat block ("stresses"), or of the shell section forces ("SHELL", shell.h:
   Nxx .. Mxy .. Qy) and reinforcement ("REBAR", rebar.h: the areas, then two
   ratios); -1 when it has none (SDV, ERROR, ...) */
int cv_field_quantity(const char* field, int comp);

#endif
