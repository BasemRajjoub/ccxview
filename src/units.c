/* units.c -- unit tables and conversions of result fields (units.h). */
#include "units.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* exact definitions (NIST): everything imperial follows from these */
#define IN    0.0254
#define FT    0.3048
#define LB    0.45359237
#define GN    9.80665                  /* standard gravity */
#define LBF   (LB * GN)                /* 4.4482216152605 N */
#define BTU   1055.05585262            /* International Table BTU */
#define HOUR  3600.0

/* ---- units of each quantity --------------------------------------------------
   Where two names have the same value the first is the one a file system shows
   (cv_sys_unit picks the first match): mJ/mm³ before MJ/m³, mW/mm² before kW/m². */

static const cv_unit LEN[] = {
    { "m", 1 }, { "cm", 1e-2 }, { "mm", 1e-3 }, { "µm", 1e-6 }, { "km", 1e3 },
    { "in", IN }, { "ft", FT }, { "mil", IN * 1e-3 },
};
static const cv_unit STRESS[] = {
    { "Pa", 1 }, { "kPa", 1e3 }, { "MPa", 1e6 }, { "GPa", 1e9 }, { "N/mm²", 1e6 }, { "bar", 1e5 },
    { "dyn/cm²", 0.1 }, { "kgf/cm²", GN * 1e4 }, { "psi", LBF / (IN * IN) }, { "ksi", 1e3 * LBF / (IN * IN) },
    { "psf", LBF / (FT * FT) }, { "atm", 101325 },
};
static const cv_unit FORCE[] = {
    { "N", 1 }, { "kN", 1e3 }, { "MN", 1e6 }, { "mN", 1e-3 }, { "dyn", 1e-5 }, { "kgf", GN },
    { "lbf", LBF }, { "kip", 1e3 * LBF },
};
static const cv_unit TEMP[] = {           /* CV_TEMP_* order */
    { "°C", 1, 273.15 }, { "K", 1, 0 }, { "°F", 5.0 / 9, 273.15 - 32 * 5.0 / 9 }, { "°R", 5.0 / 9, 0 },
};
static const cv_unit STRAIN[] = {
    { "", 1 }, { "%", 1e-2 }, { "‰", 1e-3 }, { "µε", 1e-6 },
};
static const cv_unit VELO[] = {
    { "m/s", 1 }, { "mm/s", 1e-3 }, { "cm/s", 1e-2 }, { "km/h", 1 / 3.6 }, { "in/s", IN }, { "ft/s", FT },
    { "mph", 1609.344 / HOUR },
};
static const cv_unit ACC[] = {
    { "m/s²", 1 }, { "mm/s²", 1e-3 }, { "cm/s²", 1e-2 }, { "mm/ms²", 1e3 }, { "g", GN }, { "in/s²", IN },
    { "ft/s²", FT },
};
static const cv_unit ENERGY_D[] = {
    { "J/m³", 1 }, { "kJ/m³", 1e3 }, { "mJ/mm³", 1e6 }, { "MJ/m³", 1e6 }, { "J/mm³", 1e9 },
    { "erg/cm³", 0.1 }, { "in·lbf/in³", LBF / (IN * IN) }, { "ft·lbf/ft³", LBF / (FT * FT) },
};
static const cv_unit FLUX[] = {
    { "W/m²", 1 }, { "mW/mm²", 1e3 }, { "kW/m²", 1e3 }, { "W/cm²", 1e4 }, { "W/mm²", 1e6 }, { "kW/mm²", 1e9 },
    { "erg/(s·cm²)", 1e-3 }, { "BTU/(h·ft²)", BTU / HOUR / (FT * FT) },
    { "in·lbf/(s·in²)", LBF / IN }, { "ft·lbf/(s·ft²)", LBF / FT },
};
static const cv_unit POWER[] = {
    { "W", 1 }, { "mW", 1e-3 }, { "kW", 1e3 }, { "MW", 1e6 }, { "erg/s", 1e-7 }, { "BTU/h", BTU / HOUR },
    { "hp", 550 * FT * LBF }, { "in·lbf/s", IN * LBF }, { "ft·lbf/s", FT * LBF },
};
static const cv_unit ENERGY[] = {
    { "J", 1 }, { "mJ", 1e-3 }, { "kJ", 1e3 }, { "MJ", 1e6 }, { "N·mm", 1e-3 }, { "erg", 1e-7 }, { "kWh", 3.6e6 },
    { "BTU", BTU }, { "in·lbf", IN * LBF }, { "ft·lbf", FT * LBF },
};
static const cv_unit MASSFLOW[] = {
    { "kg/s", 1 }, { "g/s", 1e-3 }, { "t/s", 1e3 }, { "kg/h", 1 / HOUR }, { "t/h", 1e3 / HOUR }, { "lb/s", LB },
    { "lb/h", LB / HOUR }, { "lbf·s/in", LBF / IN }, { "slug/s", LBF / FT },
};
static const cv_unit VOLUME[] = {
    { "m³", 1 }, { "l", 1e-3 }, { "cm³", 1e-6 }, { "mm³", 1e-9 }, { "in³", IN * IN * IN }, { "ft³", FT * FT * FT },
    { "gal", 3.785411784e-3 },
};
static const cv_unit MASS[] = {
    { "kg", 1 }, { "g", 1e-3 }, { "t", 1e3 }, { "lb", LB }, { "slug", LBF / FT }, { "lbf·s²/in", LBF / IN },
};
static const cv_unit FORCE_LEN[] = {      /* N/mm before kN/m: one value, the first shown */
    { "N/m", 1 }, { "N/mm", 1e3 }, { "kN/m", 1e3 }, { "kN/mm", 1e6 }, { "MN/m", 1e6 }, { "dyn/cm", 1e-3 },
    { "lbf/in", LBF / IN }, { "lbf/ft", LBF / FT }, { "kip/in", 1e3 * LBF / IN }, { "kip/ft", 1e3 * LBF / FT },
};
static const cv_unit MOMENT_LEN[] = {     /* a moment per width is a force: named by the system's length (cv_sys_unit) */
    { "N·m/m", 1 }, { "N·mm/mm", 1 }, { "kN·m/m", 1e3 }, { "kN·mm/mm", 1e3 }, { "dyn·cm/cm", 1e-5 },
    { "lbf·in/in", LBF }, { "lbf·ft/ft", LBF }, { "kip·in/in", 1e3 * LBF }, { "kip·ft/ft", 1e3 * LBF },
};

#define N_(a) (int)(sizeof a / sizeof a[0])
static const struct { const char* name; const cv_unit* u; int n; int dim[3]; const char* key; } Q[CV_Q_N] = {
    /* dimension: powers of length, mass, time; the key as in ccxview.ini (unit_<key>) */
    [CV_Q_LEN]      = { "Length",            LEN,      N_(LEN),      {  1, 0,  0 }, "length" },
    [CV_Q_STRESS]   = { "Stress, pressure",  STRESS,   N_(STRESS),   { -1, 1, -2 }, "stress" },
    [CV_Q_FORCE]    = { "Force",             FORCE,    N_(FORCE),    {  1, 1, -2 }, "force" },
    [CV_Q_TEMP]     = { "Temperature",       TEMP,     N_(TEMP),     {  0, 0,  0 }, "temperature" },
    [CV_Q_STRAIN]   = { "Strain",            STRAIN,   N_(STRAIN),   {  0, 0,  0 }, "strain" },
    [CV_Q_VELO]     = { "Velocity",          VELO,     N_(VELO),     {  1, 0, -1 }, "velocity" },
    [CV_Q_ACC]      = { "Acceleration",      ACC,      N_(ACC),      {  1, 0, -2 }, "acceleration" },
    [CV_Q_ENERGY_D] = { "Energy density",    ENERGY_D, N_(ENERGY_D), { -1, 1, -2 }, "energy_density" },
    [CV_Q_FLUX]     = { "Heat flux",         FLUX,     N_(FLUX),     {  0, 1, -3 }, "heat_flux" },
    [CV_Q_POWER]    = { "Heat flow, power",  POWER,    N_(POWER),    {  2, 1, -3 }, "power" },
    [CV_Q_ENERGY]   = { "Energy",            ENERGY,   N_(ENERGY),   {  2, 1, -2 }, "energy" },
    [CV_Q_MASSFLOW] = { "Mass flow",         MASSFLOW, N_(MASSFLOW), {  0, 1, -1 }, "mass_flow" },
    [CV_Q_VOLUME]   = { "Volume",            VOLUME,   N_(VOLUME),   {  3, 0,  0 }, "volume" },
    [CV_Q_MASS]     = { "Mass",              MASS,     N_(MASS),     {  0, 1,  0 }, "mass" },
    [CV_Q_FORCE_LEN]  = { "Force per width",  FORCE_LEN,  N_(FORCE_LEN),  {  0, 1, -2 }, "force_per_width" },
    [CV_Q_MOMENT_LEN] = { "Moment per width", MOMENT_LEN, N_(MOMENT_LEN), {  1, 1, -2 }, "moment_per_width" },
};

const char* cv_quantity_name(int q) { return q >= 0 && q < CV_Q_N ? Q[q].name : ""; }
const char* cv_quantity_key(int q) { return q >= 0 && q < CV_Q_N ? Q[q].key : ""; }
int cv_unit_count(int q) { return q >= 0 && q < CV_Q_N ? Q[q].n : 0; }
const cv_unit* cv_unit_get(int q, int i) { return i >= 0 && i < cv_unit_count(q) ? &Q[q].u[i] : NULL; }

int cv_unit_find(int q, const char* name) {
    for (int i = 0; i < cv_unit_count(q); i++) if (!strcmp(Q[q].u[i].name, name)) return i;
    return -1;
}

/* ---- file systems: base units of length, mass, time in SI ---------------------- */

/* named as the display presets are, so "SI (mm, MPa)" in reads as "SI (mm, MPa)" shown */
static const struct { const char* name; const char* base; double L, M, T; int temp; } SYS[CV_SYS_N] = {
    [CV_SYS_NONE]      = { "Not set",           "",                                  0, 0, 0, CV_TEMP_C },
    [CV_SYS_MM_T_S]    = { "SI (mm, MPa)",      "mm, t, s  ->  N, MPa",            1e-3, 1e3,  1, CV_TEMP_C },
    [CV_SYS_M_KG_S]    = { "SI (m, Pa)",        "m, kg, s  ->  N, Pa",                1,   1,  1, CV_TEMP_C },
    [CV_SYS_IN_LBF_S]  = { "US (in, psi)",      "in, lbf·s²/in, s  ->  lbf, psi",    IN, LBF / IN, 1, CV_TEMP_F },
    [CV_SYS_MM_KG_MS]  = { "SI (mm, GPa, ms)",  "mm, kg, ms  ->  kN, GPa",         1e-3,   1, 1e-3, CV_TEMP_C },
    [CV_SYS_MM_G_MS]   = { "SI (mm, MPa, ms)",  "mm, g, ms  ->  N, MPa",           1e-3, 1e-3, 1e-3, CV_TEMP_C },
    [CV_SYS_CM_G_S]    = { "CGS (cm, dyn/cm²)", "cm, g, s  ->  dyn, dyn/cm²",      1e-2, 1e-3, 1, CV_TEMP_C },
    [CV_SYS_FT_SLUG_S] = { "US (ft, psf)",      "ft, slug, s  ->  lbf, psf",         FT, LBF / FT, 1, CV_TEMP_F },
};

const char* cv_sys_name(int sys) { return sys >= 0 && sys < CV_SYS_N ? SYS[sys].name : ""; }
const char* cv_sys_base(int sys) { return sys >= 0 && sys < CV_SYS_N ? SYS[sys].base : ""; }
int cv_sys_temp(int sys) { return sys > 0 && sys < CV_SYS_N ? SYS[sys].temp : CV_TEMP_C; }

double cv_sys_si(int sys, int q) {
    if (sys <= 0 || sys >= CV_SYS_N || q < 0 || q >= CV_Q_N) return 0;
    const int* d = Q[q].dim;
    return pow(SYS[sys].L, d[0]) * pow(SYS[sys].M, d[1]) * pow(SYS[sys].T, d[2]);
}

static bool same(double a, double b) { return fabs(a - b) <= 1e-9 * fabs(b); }

int cv_sys_unit(int sys, int temp, int q) {
    if (q == CV_Q_STRAIN) return 0;                     /* a ratio needs no system */
    if (sys <= 0 || sys >= CV_SYS_N || q < 0 || q >= CV_Q_N) return -1;
    if (q == CV_Q_TEMP) return temp >= 0 && temp < CV_TEMP_N ? temp : -1;
    double si = cv_sys_si(sys, q);
    if (q == CV_Q_MOMENT_LEN) {                         /* N·m/m and N·mm/mm are one value: the system's length says */
        const cv_unit* l = cv_unit_get(CV_Q_LEN, cv_sys_unit(sys, temp, CV_Q_LEN));
        char per[24];
        snprintf(per, sizeof per, "·%s/%s", l ? l->name : "", l ? l->name : "");
        for (int i = 0; i < Q[q].n; i++) {
            const char* p = strstr(Q[q].u[i].name, per);
            if (same(Q[q].u[i].si, si) && p && !p[strlen(per)]) return i;
        }
    }
    for (int i = 0; i < Q[q].n; i++) if (same(Q[q].u[i].si, si)) return i;
    return -1;
}

/* ---- display presets ------------------------------------------------------------ */

static const struct { const char* name; const char* u[CV_Q_N]; } SHOW[CV_SHOW_N] = {
    [CV_SHOW_FILE]  = { "As input" },
    [CV_SHOW_SI_M]  = { "SI (m, Pa)", { "m", "Pa", "N", "°C", "", "m/s", "m/s²", "J/m³", "W/m²", "W", "J", "kg/s", "m³", "kg", "N/m", "N·m/m" } },
    [CV_SHOW_SI_MM] = { "SI (mm, MPa)", { "mm", "MPa", "N", "°C", "", "mm/s", "mm/s²", "mJ/mm³", "mW/mm²", "mW", "mJ", "t/s", "mm³", "t", "N/mm", "N·mm/mm" } },
    [CV_SHOW_US_IN] = { "US (in, psi)", { "in", "psi", "lbf", "°F", "", "in/s", "in/s²", "in·lbf/in³", "BTU/(h·ft²)", "BTU/h", "in·lbf", "lb/s", "in³", "lb", "lbf/in", "lbf·in/in" } },
    [CV_SHOW_US_FT] = { "US (ft, psf)", { "ft", "psf", "lbf", "°F", "", "ft/s", "ft/s²", "ft·lbf/ft³", "BTU/(h·ft²)", "BTU/h", "ft·lbf", "lb/s", "ft³", "lb", "lbf/ft", "lbf·ft/ft" } },
};

const char* cv_show_name(int p) { return p >= 0 && p < CV_SHOW_N ? SHOW[p].name : "Custom"; }

int cv_show_unit(int p, int q) {
    if (p <= CV_SHOW_FILE || p >= CV_SHOW_N || q < 0 || q >= CV_Q_N) return -1;
    return cv_unit_find(q, SHOW[p].u[q]);
}

/* ---- conversion ----------------------------------------------------------------- */

int cv_unit_input(int sys, int temp, int in, int q) {
    return in >= 0 && in < cv_unit_count(q) ? in : cv_sys_unit(sys, temp, q);
}

int cv_unit_shown(int sys, int temp, int in, int q, int show) {
    int from = cv_unit_input(sys, temp, in, q);
    if (from < 0) return -1;                             /* nothing known to convert from */
    return show >= 0 && show < cv_unit_count(q) ? show : from;
}

bool cv_unit_conv(int sys, int temp, int in, int q, int show, double* k, double* off) {
    *k = 1; *off = 0;
    int from = cv_unit_input(sys, temp, in, q);
    if (from < 0 || show < 0 || show >= cv_unit_count(q) || show == from) return false;
    const cv_unit *a = &Q[q].u[from], *b = &Q[q].u[show];
    bool own = in >= 0 && in < cv_unit_count(q);         /* chosen by hand: its listed value */
    double si = own || q == CV_Q_TEMP || q == CV_Q_STRAIN ? a->si : cv_sys_si(sys, q);   /* else exact */
    *k = si / b->si;
    *off = (a->off - b->off) / b->si;
    return true;
}

/* ---- the quantity of a field ---------------------------------------------------- */

int cv_field_quantity(const char* f, int comp) {
    static const struct { const char* name; int q; } T[] = {
        { "DISP", CV_Q_LEN }, { "DISPI", CV_Q_LEN }, { "MDISP", CV_Q_LEN }, { "MAXU", CV_Q_LEN },
        { "STRESS", CV_Q_STRESS }, { "STRESSI", CV_Q_STRESS }, { "ZZSTR", CV_Q_STRESS },
        { "ZZSTRI", CV_Q_STRESS }, { "MAXS", CV_Q_STRESS }, { "PRESS", CV_Q_STRESS },
        { "PS3DF", CV_Q_STRESS }, { "PT3DF", CV_Q_STRESS },
        { "FORC", CV_Q_FORCE }, { "FORCI", CV_Q_FORCE }, { "RF", CV_Q_FORCE },
        { "NDTEMP", CV_Q_TEMP }, { "NT", CV_Q_TEMP }, { "TS3DF", CV_Q_TEMP }, { "TT3DF", CV_Q_TEMP },
        { "TOSTRAIN", CV_Q_STRAIN }, { "MESTRAIN", CV_Q_STRAIN }, { "TOSTRAII", CV_Q_STRAIN },
        { "MESTRAII", CV_Q_STRAIN }, { "PE", CV_Q_STRAIN }, { "PEEQ", CV_Q_STRAIN },
        { "VELO", CV_Q_VELO }, { "V3DF", CV_Q_VELO }, { "ACC", CV_Q_ACC },
        { "ENER", CV_Q_ENERGY_D }, { "HFL", CV_Q_FLUX }, { "FLUX", CV_Q_FLUX },   /* *NODE FILE HFL writes FLUX */ { "RFL", CV_Q_POWER }, { "MF", CV_Q_MASSFLOW },
        /* .dat phrases (longer first: the match is on a prefix) */
        { "displacements", CV_Q_LEN }, { "stresses", CV_Q_STRESS }, { "forces", CV_Q_FORCE },
        { "total force", CV_Q_FORCE }, { "temperatures", CV_Q_TEMP }, { "velocities", CV_Q_VELO },
        { "heat flux", CV_Q_FLUX }, { "heat generation", CV_Q_POWER },
        { "internal energy density", CV_Q_ENERGY_D }, { "internal energy", CV_Q_ENERGY },
        { "total internal energy", CV_Q_ENERGY }, { "kinetic energy", CV_Q_ENERGY },
        { "strains", CV_Q_STRAIN }, { "mechanical strains", CV_Q_STRAIN },
        { "equivalent plastic strain", CV_Q_STRAIN }, { "volume", CV_Q_VOLUME }, { "mass", CV_Q_MASS },
    };
    if (!f) return -1;
    if (!strcmp(f, "SHELL")) return comp >= 3 && comp <= 5 ? CV_Q_MOMENT_LEN : comp >= 0 && comp < 8 ? CV_Q_FORCE_LEN : -1;
    /* complex results: amplitudes then phases (MAG1 .. MAG3 PHA1 .. PHA3; MAGXX .. MAGZX
       PHAXX .. PHAZX); a phase is an angle in degrees, no unit of the system */
    if (!strncmp(f, "PDISP", 5) && (f[5] == 0 || f[5] == ' ')) return comp >= 3 ? -1 : CV_Q_LEN;
    if (!strncmp(f, "PSTRESS", 7) && (f[7] == 0 || f[7] == ' ')) return comp >= 6 ? -1 : CV_Q_STRESS;
    if (!strcmp(f, "CONTACT")) return comp >= 0 && comp < 3 ? CV_Q_LEN : CV_Q_STRESS;   /* COPEN CSLIP1 CSLIP2 | CPRESS CSHEAR1 CSHEAR2 */
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        size_t n = strlen(T[i].name);
        if (!strncmp(f, T[i].name, n) && (f[n] == 0 || f[n] == ' ' || f[n] == '(')) return T[i].q;
    }
    return -1;
}
