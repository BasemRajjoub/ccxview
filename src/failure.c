/* failure.c -- failure criteria for UD plies and isotropic materials (failure.h).

   References:
   - Tsai-Hill: Hill (1948) for an orthotropic body, transversely isotropic here.
   - Tsai, Wu, "A general theory of strength for anisotropic materials",
     J. Compos. Mater. 5 (1971) 58-80.
   - Hashin, "Failure criteria for unidirectional fiber composites",
     J. Appl. Mech. 47 (1980) 329-334.
   - Knops, "Analysis of Failure in Fiber Polymer Laminates: The Theory of Alfred
     Puck", Springer 2008; VDI 2014 Part 3.
   - Davila, Camanho, "Failure criteria for FRP laminates in plane stress",
     NASA/TM-2003-212663 (LaRC03); Davila, Camanho, Rose, J. Compos. Mater. 39
     (2005) 323-345.
   - Pinho, Darvizeh, Robinson, Schuecker, Camanho, "Material and structural
     response of polymer-matrix fibre-reinforced composites", J. Compos. Mater. 46
     (2012) 2313-2341 (LaRC05). */
#include "failure.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define DEG (PI / 180)

static double sq(double x) { return x * x; }
static double pos(double x) { return x > 0 ? x : 0; }      /* Macaulay bracket */

const char* cv_fc_name(int c) {
    static const char* n[CV_FC_N] = { "MAXSTRESS", "TSAIHILL", "TSAIWU", "HASHIN", "PUCK", "LARC03",
                                      "LARC05", "MISES", "TRESCA", "MOHR", "AUTO" };
    return c >= 0 && c < CV_FC_N ? n[c] : "";
}

const char* cv_fc_title(int c) {
    static const char* n[CV_FC_N] = { "Max stress", "Tsai-Hill", "Tsai-Wu", "Hashin (1980)",
                                      "Puck (action plane)", "LaRC03", "LaRC05",
                                      "von Mises (yield)", "Tresca (yield)", "Mohr-Coulomb (brittle)",
                                      "Auto (per material)" };
    return c >= 0 && c < CV_FC_N ? n[c] : "";
}

bool cv_fc_ud(int c) { return c < CV_FC_MISES; }

int cv_fc_auto(const cv_fmat* m) {
    static const int ud[] = { CV_FC_LARC05, CV_FC_PUCK, CV_FC_HASHIN, CV_FC_MAXSTRESS };
    if (m->kind == CV_MK_UD) {
        for (size_t i = 0; i < sizeof ud / sizeof ud[0]; i++) if (cv_fmat_valid(m, ud[i], NULL, 0)) return ud[i];
        return -1;
    }
    if (m->Sy > 0) return CV_FC_MISES;
    return m->Sut > 0 ? CV_FC_MOHR : -1;
}

const char* cv_fm_name(int mode) {
    static const char* n[CV_FM_N] = { "-", "FT", "FC", "MT", "MC", "S12", "S23", "IFF A", "IFF B", "IFF C",
                                      "FK MT", "FK MC", "Split", "Yield", "Fracture" };
    return mode >= 0 && mode < CV_FM_N ? n[mode] : "?";
}

/* ---- material data ------------------------------------------------------------- */

#define UD (1 << CV_MK_UD)
#define ISO (1 << CV_MK_ISO)
#define BOTH (UD | ISO)
#define O(f) offsetof(cv_fmat, f)
static const cv_fprop props[] = {
    { "E1", "E1", CV_FU_MPA, BOTH, O(E1), "Young's modulus along the fibres (isotropic: E)" },
    { "E2", "E2", CV_FU_MPA, UD, O(E2), "Young's modulus across the fibres" },
    { "G12", "G12", CV_FU_MPA, UD, O(G12), "in-plane shear modulus" },
    { "nu12", "nu12", CV_FU_NONE, BOTH, O(nu12), "major Poisson ratio (isotropic: nu)" },
    { "Xt", "Xt", CV_FU_MPA, UD, O(Xt), "tensile strength along the fibres" },
    { "Xc", "Xc", CV_FU_MPA, UD, O(Xc), "compressive strength along the fibres (positive)" },
    { "Yt", "Yt", CV_FU_MPA, UD, O(Yt), "tensile strength across the fibres" },
    { "Yc", "Yc", CV_FU_MPA, UD, O(Yc), "compressive strength across the fibres (positive)" },
    { "S12", "S12", CV_FU_MPA, UD, O(S12), "in-plane (longitudinal) shear strength" },
    { "S23", "S23", CV_FU_MPA, UD, O(S23), "transverse shear strength; 0: from Yc and alpha0" },
    { "f12", "F12*", CV_FU_NONE, UD, O(f12), "Tsai-Wu interaction, normalized (-0.5: von Mises like)" },
    { "p12t", "p12(+)", CV_FU_NONE, UD, O(p12t), "Puck inclination p_perp_par(+)" },
    { "p12c", "p12(-)", CV_FU_NONE, UD, O(p12c), "Puck inclination p_perp_par(-)" },
    { "p23t", "p23(+)", CV_FU_NONE, UD, O(p23t), "Puck inclination p_perp_perp(+)" },
    { "p23c", "p23(-)", CV_FU_NONE, UD, O(p23c), "Puck inclination p_perp_perp(-)" },
    { "E1f", "E1f", CV_FU_MPA, UD, O(E1f), "fibre modulus (Puck fibre fracture); 0: simple form" },
    { "nu12f", "nu12f", CV_FU_NONE, UD, O(nu12f), "fibre Poisson ratio (Puck fibre fracture)" },
    { "msf", "m_sf", CV_FU_NONE, UD, O(msf), "Puck magnification of transverse stress in the fibres" },
    { "GIc", "GIc", CV_FU_NMM, UD, O(GIc), "mode I fracture toughness (LaRC in-situ strengths)" },
    { "GIIc", "GIIc", CV_FU_NMM, UD, O(GIIc), "mode II fracture toughness (LaRC in-situ strengths)" },
    { "alpha0", "alpha0", CV_FU_DEG, UD, O(alpha0), "fracture angle under pure transverse compression" },
    { "Sy", "Sy", CV_FU_MPA, ISO, O(Sy), "yield strength" },
    { "Sut", "Sut", CV_FU_MPA, ISO, O(Sut), "ultimate tensile strength" },
    { "Suc", "Suc", CV_FU_MPA, ISO, O(Suc), "ultimate compressive strength (brittle; 0: = Sut)" },
};
#undef O

int cv_fprops(const cv_fprop** p) { *p = props; return (int)(sizeof props / sizeof props[0]); }
double* cv_fmat_val(cv_fmat* m, const cv_fprop* p) { return (double*)((char*)m + p->off); }

void cv_fmat_defaults(cv_fmat* m, int kind) {
    memset(m, 0, sizeof *m);
    m->kind = kind;
    m->f12 = -0.5;
    /* Puck, Kopp, Knops (2002): CFRP */
    m->p12t = 0.35; m->p12c = 0.30; m->p23t = 0.25; m->p23c = 0.25;
    m->msf = 1.1;
    m->alpha0 = 53;
}

void cv_fmat_format(const cv_fmat* m, char* buf, size_t n) {
    size_t k = (size_t)snprintf(buf, n, "kind=%s", m->kind == CV_MK_ISO ? "iso" : "ud");
    cv_fmat t = *m;
    for (size_t i = 0; i < sizeof props / sizeof props[0] && k < n; i++) {
        if (!(props[i].kinds & (1 << m->kind))) continue;
        k += (size_t)snprintf(buf + k, n - k, " %s=%.10g", props[i].key, *cv_fmat_val(&t, &props[i]));
    }
}

bool cv_fmat_parse(const char* s, cv_fmat* m) {
    char name[48];
    memcpy(name, m->name, sizeof name);
    const char* k = strstr(s, "kind=");
    cv_fmat_defaults(m, k && !strncmp(k + 5, "iso", 3) ? CV_MK_ISO : CV_MK_UD);
    memcpy(m->name, name, sizeof name);
    bool any = false;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
        const char* eq = strchr(s, '=');
        if (!eq) break;
        size_t kl = (size_t)(eq - s);
        char* end;
        double v = strtod(eq + 1, &end);
        for (size_t i = 0; end > eq + 1 && i < sizeof props / sizeof props[0]; i++)
            if (strlen(props[i].key) == kl && !strncmp(props[i].key, s, kl)) { *cv_fmat_val(m, &props[i]) = v; any = true; }
        s = eq + 1;
        while (*s && *s != ' ' && *s != '\t' && *s != ',') s++;
    }
    return any;
}

bool cv_fmat_valid(const cv_fmat* m, int c, char* why, size_t n) {
    const char* miss = NULL;
    if (c == CV_FC_AUTO) {
        if (cv_fc_auto(m) < 0) miss = m->kind == CV_MK_UD ? "Xt, Xc, Yt, Yc and S12" : "Sy or Sut";
    } else if (cv_fc_ud(c)) {
        if (m->kind != CV_MK_UD) miss = "a UD ply (this material is isotropic)";
        else if (!(m->Xt > 0 && m->Xc > 0 && m->Yt > 0 && m->Yc > 0 && m->S12 > 0)) miss = "Xt, Xc, Yt, Yc and S12";
        else if ((c == CV_FC_LARC03 || c == CV_FC_LARC05) && !(m->E1 > 0 && m->E2 > 0 && m->G12 > 0)) miss = "E1, E2 and G12";
        else if (c == CV_FC_LARC05 && !(m->S23 > 0 || (m->alpha0 > 45 && m->alpha0 < 90))) miss = "S23 or alpha0";
    } else if (c == CV_FC_MOHR) {
        if (!(m->Sut > 0)) miss = "Sut";
    } else if (!(m->Sy > 0 || m->Sut > 0)) miss = "Sy";
    if (miss && why) snprintf(why, n, "%s needs %s", cv_fc_title(c), miss);
    return !miss;
}

/* ---- isotropic --------------------------------------------------------------------- */

/* principal values of s (xx yy zz xy yz zx), largest first */
static void principal(const double s[6], double p[3]) {
    double xx = s[0], yy = s[1], zz = s[2], xy = s[3], yz = s[4], zx = s[5];
    double q = (xx + yy + zz) / 3, p1 = xy * xy + yz * yz + zx * zx;
    double p2 = sq(xx - q) + sq(yy - q) + sq(zz - q) + 2 * p1;
    if (p2 <= 1e-30 * (q * q + 1e-300)) { p[0] = p[1] = p[2] = q; return; }
    double pp = sqrt(p2 / 6);
    double b[6] = { (xx - q) / pp, (yy - q) / pp, (zz - q) / pp, xy / pp, yz / pp, zx / pp };
    double det = b[0] * (b[1] * b[2] - b[4] * b[4]) - b[3] * (b[3] * b[2] - b[4] * b[5]) + b[5] * (b[3] * b[4] - b[1] * b[5]);
    double r = det / 2;
    double phi = r <= -1 ? PI / 3 : r >= 1 ? 0 : acos(r) / 3;
    p[0] = q + 2 * pp * cos(phi);
    p[2] = q + 2 * pp * cos(phi + 2 * PI / 3);
    p[1] = 3 * q - p[0] - p[2];
}

static void iso(int c, const cv_fmat* m, const double s[6], cv_fres* r) {
    double sy = m->Sy > 0 ? m->Sy : m->Sut, p[3], e;
    principal(s, p);
    r->mode = CV_FM_YIELD;
    if (c == CV_FC_MISES) e = sqrt((sq(p[0] - p[1]) + sq(p[1] - p[2]) + sq(p[2] - p[0])) / 2) / sy;
    else if (c == CV_FC_TRESCA) e = (p[0] - p[2]) / sy;
    else {                                      /* Mohr-Coulomb: s1/St - s3/Sc = 1 */
        double st = m->Sut, sc = m->Suc > 0 ? m->Suc : m->Sut;
        e = pos(p[0]) / st + pos(-p[2]) / sc;
        if (p[0] > 0 && p[2] > 0) e = p[0] / st;
        else if (p[0] < 0 && p[2] < 0) e = -p[2] / sc;
        else e = p[0] / st - p[2] / sc;
        r->mode = CV_FM_FRACT;
    }
    r->fi = (float)e;
    r->rf = e > 0 ? (float)(1 / e) : INFINITY;
}

/* ---- UD: max stress, Tsai-Hill, Tsai-Wu, Hashin ------------------------------------ */

static double s23_of(const cv_fmat* m) {        /* LaRC: ST = Yc cos a0 (sin a0 + cos a0 / tan 2 a0) */
    if (m->S23 > 0) return m->S23;
    double a = (m->alpha0 > 45 && m->alpha0 < 90 ? m->alpha0 : 53) * DEG;
    return m->Yc * cos(a) * (sin(a) + cos(a) / tan(2 * a));
}

static void maxstress(const cv_fmat* m, const double s[6], cv_fres* r) {
    double S23 = s23_of(m);
    double e[6] = { s[0] > 0 ? s[0] / m->Xt : -s[0] / m->Xc, s[1] > 0 ? s[1] / m->Yt : -s[1] / m->Yc,
                    s[2] > 0 ? s[2] / m->Yt : -s[2] / m->Yc, fabs(s[3]) / m->S12, fabs(s[4]) / S23,
                    fabs(s[5]) / m->S12 };
    int k = 0;
    for (int i = 1; i < 6; i++) if (e[i] > e[k]) k = i;
    static const uint8_t md[6][2] = { { CV_FM_FT, CV_FM_FC }, { CV_FM_MT, CV_FM_MC }, { CV_FM_MT, CV_FM_MC },
                                      { CV_FM_S12, CV_FM_S12 }, { CV_FM_S23, CV_FM_S23 }, { CV_FM_S12, CV_FM_S12 } };
    r->mode = md[k][k < 3 && s[k] < 0];
    r->fi = (float)e[k];
    r->rf = e[k] > 0 ? (float)(1 / e[k]) : INFINITY;
}

/* Hill's quadratic with the strengths of the stress signs; transversely isotropic */
static void tsaihill(const cv_fmat* m, const double s[6], cv_fres* r) {
    double X = s[0] >= 0 ? m->Xt : m->Xc, Y = s[1] >= 0 ? m->Yt : m->Yc, Z = s[2] >= 0 ? m->Yt : m->Yc;
    double iX = 1 / (X * X), iY = 1 / (Y * Y), iZ = 1 / (Z * Z);
    double F = (iY + iZ - iX) / 2, Gc = (iZ + iX - iY) / 2, H = (iX + iY - iZ) / 2;
    double S23 = m->S23 > 0 ? m->S23 : 0;
    double L2 = S23 > 0 ? 1 / (S23 * S23) : 6 * F;          /* 2L; isotropic 2-3 plane: L = 3F */
    double fi = F * sq(s[1] - s[2]) + Gc * sq(s[2] - s[0]) + H * sq(s[0] - s[1])
              + L2 * s[4] * s[4] + (s[3] * s[3] + s[5] * s[5]) / (m->S12 * m->S12);
    r->fi = (float)fi;
    r->rf = fi > 0 ? (float)(1 / sqrt(fi)) : INFINITY;
    /* the mode: the largest share among fibre, matrix and shear terms */
    double f = s[0] * s[0] * iX, t = s[1] * s[1] * iY + s[2] * s[2] * iZ, h = (s[3] * s[3] + s[5] * s[5]) / (m->S12 * m->S12);
    r->mode = f >= t && f >= h ? (s[0] >= 0 ? CV_FM_FT : CV_FM_FC) : t >= h ? (s[1] + s[2] >= 0 ? CV_FM_MT : CV_FM_MC) : CV_FM_S12;
}

/* the smallest positive lambda with a lambda^2 + b lambda = 1, INFINITY none */
static double root_q(double a, double b) {
    double d = b * b + 4 * a;                   /* a < 0: rises to a peak, then falls */
    if (d < 0) return INFINITY;
    double q = b + sqrt(d);                     /* 2 / q: the root, without cancellation */
    return q > 0 ? 2 / q : INFINITY;
}

static void tsaiwu(const cv_fmat* m, const double s[6], cv_fres* r) {
    double F1 = 1 / m->Xt - 1 / m->Xc, F2 = 1 / m->Yt - 1 / m->Yc;
    double F11 = 1 / (m->Xt * m->Xc), F22 = 1 / (m->Yt * m->Yc), F66 = 1 / (m->S12 * m->S12);
    double F12 = m->f12 * sqrt(F11 * F22), F23, F44;
    if (m->S23 > 0) { F44 = 1 / (m->S23 * m->S23); F23 = F22 - F44 / 2; }
    else { F23 = m->f12 * F22; F44 = 2 * (F22 - F23); }       /* isotropic 2-3 plane */
    double b = F1 * s[0] + F2 * (s[1] + s[2]);
    double a = F11 * s[0] * s[0] + F22 * (s[1] * s[1] + s[2] * s[2]) + F44 * s[4] * s[4]
             + F66 * (s[3] * s[3] + s[5] * s[5]) + 2 * F12 * s[0] * (s[1] + s[2]) + 2 * F23 * s[1] * s[2];
    r->fi = (float)(a + b);
    r->rf = (float)root_q(a, b);
    double f = F11 * s[0] * s[0] + fabs(F1 * s[0]), t = F22 * (s[1] * s[1] + s[2] * s[2]) + fabs(F2 * (s[1] + s[2])),
           h = F66 * (s[3] * s[3] + s[5] * s[5]) + F44 * s[4] * s[4];
    r->mode = f >= t && f >= h ? (s[0] >= 0 ? CV_FM_FT : CV_FM_FC) : t >= h ? (s[1] + s[2] >= 0 ? CV_FM_MT : CV_FM_MC) : CV_FM_S12;
}

/* Hashin 1980, eqs. (21)-(24) with the transverse shear strength tau_T = S23 */
static void hashin(const cv_fmat* m, const double s[6], cv_fres* r) {
    double ST = s23_of(m), SL2 = m->S12 * m->S12, sh = (s[3] * s[3] + s[5] * s[5]) / SL2;
    double ff, fl, fq, mq, ml;                  /* fibre / matrix: index, its quadratic and linear part */
    uint8_t fm, mm;
    if (s[0] > 0) { fq = sq(s[0] / m->Xt) + sh; fl = 0; fm = CV_FM_FT; }   /* eq. 10, for s11 > 0 only */
    else { fq = 0; fl = -s[0] / m->Xc; fm = CV_FM_FC; }
    ff = fq + fl;
    double t = s[1] + s[2], q23 = (s[4] * s[4] - s[1] * s[2]) / (ST * ST);
    if (t >= 0) { mq = sq(t / m->Yt) + q23 + sh; ml = 0; mm = CV_FM_MT; }
    else {
        mq = sq(t / (2 * ST)) + q23 + sh;
        ml = (sq(m->Yc / (2 * ST)) - 1) * t / m->Yc;
        mm = CV_FM_MC;
    }
    double fi_m = mq + ml;
    double rf_f = fl > 0 ? 1 / fl : fq > 0 ? 1 / sqrt(fq) : INFINITY, rf_m = root_q(mq, ml);
    r->fe_f = (float)(1 / rf_f);
    r->fe_m = (float)(1 / rf_m);
    /* the governing mode: the one nearer failure along the load (the matrix index
       under compression can be negative, so not the larger index) */
    if (rf_f <= rf_m) { r->fi = (float)ff; r->mode = fm; } else { r->fi = (float)fi_m; r->mode = mm; }
    r->rf = (float)(rf_f < rf_m ? rf_f : rf_m);
    if (!(r->rf < INFINITY)) r->mode = CV_FM_NONE;
}

/* ---- reserve factors of criteria not linear in the load ----------------------------- */

/* index of a stress state scaled by lam; fine: the exact plane search, else the grid */
typedef double (*fi_fn)(const void* c, double lam, bool fine);

/* Illinois (regula falsi) on f(lam) = 1 between lo (f < 1) and hi (f >= 1) */
static double illinois(fi_fn f, const void* c, bool fine, double lo, double glo, double hi, double ghi) {
    int side = 0;
    for (int it = 0; it < 100 && hi - lo > 1e-9 * hi; it++) {
        double x = ghi > 100 ? (lo + hi) / 2 : (lo * ghi - hi * glo) / (ghi - glo);
        if (!(x > lo && x < hi)) x = (lo + hi) / 2;
        double g = f(c, x, fine) - 1;
        if (g >= 0) { hi = x; ghi = g; if (side == 1) glo /= 2; side = 1; }
        else { lo = x; glo = g; if (side == -1) ghi /= 2; side = -1; }
    }
    return hi;
}

/* the load factor lam with f(lam) = 1, f growing from f(0) = 0: on the grid, then
   polished with the exact search (which can only raise f, so the root moves down) */
static double solve_rf(fi_fn f, const void* c, double f1, bool polish) {
    if (!(f1 == f1)) return NAN;
    double lo = 0, glo = -1, hi, ghi, x = f1 > 0 ? 1 / sqrt(f1) : 1, g;
    if (f1 >= 1) { hi = 1; ghi = f1 - 1; } else { lo = 1; glo = f1 - 1; hi = INFINITY; ghi = 0; }
    g = f(c, x, false) - 1;
    if (g >= 0 && x < hi) { hi = x; ghi = g; } else if (g < 0 && x > lo) { lo = x; glo = g; }
    for (int i = 0; hi == INFINITY; i++) {                 /* expand upwards */
        if (i > 60) return INFINITY;
        x = lo * 2;
        g = f(c, x, false) - 1;
        if (g >= 0) { hi = x; ghi = g; } else { lo = x; glo = g; }
    }
    for (int i = 0; lo == 0 && hi > 1e-12; i++) {          /* and downwards */
        x = hi / 2;
        g = f(c, x, false) - 1;
        if (g >= 0) { hi = x; ghi = g; } else { lo = x; glo = g; }
        if (i > 60) break;
    }
    double r = illinois(f, c, false, lo, glo, hi, ghi);
    if (!polish) return r;
    hi = r; ghi = f(c, hi, true) - 1;
    if (ghi < 0) return r;
    lo = hi;
    for (int i = 0; i < 60; i++) {
        lo *= 0.99;
        glo = f(c, lo, true) - 1;
        if (glo < 0) break;
        hi = lo; ghi = glo;
    }
    return glo < 0 ? illinois(f, c, true, lo, glo, hi, ghi) : hi;
}

/* the maximum of fn over [a, b] (golden section), with the ends counted */
typedef double (*ang_fn)(const void* c, double lam, double a);
static double golden_max(ang_fn fn, const void* c, double lam, double a, double b, double* at) {
    const double R = 0.6180339887498949;
    double x1 = b - R * (b - a), x2 = a + R * (b - a), f1 = fn(c, lam, x1), f2 = fn(c, lam, x2);
    for (int i = 0; i < 40 && b - a > 1e-7; i++) {
        if (f1 < f2) { a = x1; x1 = x2; f1 = f2; x2 = a + R * (b - a); f2 = fn(c, lam, x2); }
        else { b = x2; x2 = x1; f2 = f1; x1 = b - R * (b - a); f1 = fn(c, lam, x1); }
    }
    double best = f1 > f2 ? f1 : f2;
    *at = f1 > f2 ? x1 : x2;
    return best;
}

/* ---- LaRC in-situ strengths --------------------------------------------------------- */

/* Davila, Camanho, Rose (NASA/TM-2003-212663 eqs. 41-47) and Camanho et al. (2004,
   thin outer ply): thick embedded plies sqrt(2) and 1.12 sqrt(2); thin plies from the
   toughness. An embedded ply takes the larger of thick and thin, an outer ply the larger
   of thin outer and the measured. thin: the toughness strengths were used (g = GIc/GIIc). */
static void insitu(const cv_fmat* m, const cv_fply* ply, double* yt, double* sl, bool* thin) {
    *yt = m->Yt; *sl = m->S12; *thin = false;
    if (ply->pos == CV_PLY_UD) return;
    double nu21 = m->nu12 * m->E2 / m->E1;
    double L22 = 2 * (1 / m->E2 - nu21 * nu21 / m->E1), L44 = 1 / m->G12;
    bool tough = ply->t > 0 && m->GIc > 0 && m->GIIc > 0 && L22 > 0;
    double ytn = 0, sln = 0;
    if (tough && ply->pos == CV_PLY_EMBEDDED) {
        ytn = sqrt(8 * m->GIc / (PI * ply->t * L22));
        sln = sqrt(8 * m->GIIc / (PI * ply->t * L44));
    } else if (tough) {
        ytn = 1.79 * sqrt(m->GIc / (PI * ply->t * L22));
        sln = 2 * sqrt(m->GIIc / (PI * ply->t * L44));
    }
    if (ply->pos == CV_PLY_EMBEDDED) { *yt = 1.12 * sqrt(2.) * m->Yt; *sl = sqrt(2.) * m->S12; }
    if (ytn > *yt) { *yt = ytn; *thin = true; }
    if (sln > *sl) { *sl = sln; *thin = true; }
}

static double alpha0_of(const cv_fmat* m) { return (m->alpha0 > 45 && m->alpha0 < 90 ? m->alpha0 : 53) * DEG; }

void cv_larc_insitu(const cv_fmat* m, const cv_fply* ply, double* yt, double* sl, double* st) {
    static const cv_fply none = { 0, CV_PLY_UD };
    bool thin;
    insitu(m, ply ? ply : &none, yt, sl, &thin);
    if (st) *st = s23_of(m);
}

/* ---- Puck ----------------------------------------------------------------------------- */

typedef struct {
    double s2, s3, t23, t31, t21;
    double Rt, Rpl, RA, p12t, p12c, p23t, p23c;
} puck_ctx;

/* Knops (2008) eqs. 1-4 and fig. 42: the stress exposure of the action plane at th */
static double puck_fe_cs(const puck_ctx* k, double c, double sn_) {
    double sn = k->s2 * c * c + k->s3 * sn_ * sn_ + 2 * k->t23 * sn_ * c;
    double tnt = (k->s3 - k->s2) * sn_ * c + k->t23 * (c * c - sn_ * sn_);
    double tn1 = k->t31 * sn_ + k->t21 * c;
    double sh = tnt * tnt + tn1 * tn1;
    if (sh <= 1e-24 * (sn * sn + 1e-300)) return sn > 0 ? sn / k->Rt : 0;
    double c2 = tnt * tnt / sh, s2 = tn1 * tn1 / sh;
    if (sn >= 0) {
        double pR = k->p23t / k->RA * c2 + k->p12t / k->Rpl * s2;
        return sqrt(sq((1 / k->Rt - pR) * sn) + sq(tnt / k->RA) + sq(tn1 / k->Rpl)) + pR * sn;
    }
    double pR = k->p23c / k->RA * c2 + k->p12c / k->Rpl * s2;
    return sqrt(sq(tnt / k->RA) + sq(tn1 / k->Rpl) + sq(pR * sn)) + pR * sn;
}

static double puck_fe(const void* cp, double lam, double th) {
    (void)lam;
    return puck_fe_cs(cp, cos(th), sin(th));
}

/* the largest exposure over the planes: a 1 degree grid over (-90, 90], then golden
   section about the best (Knops p. 65-66; fE can have close local maxima) */
static double puck_iff(const puck_ctx* k, double* th) {
    double best = -1, at = 0, c = cos(-89 * DEG), sn = sin(-89 * DEG), cd = cos(DEG), sd = sin(DEG);
    for (int i = -89; i <= 90; i++) {
        double f = puck_fe_cs(k, c, sn), t = c * cd - sn * sd;
        if (f > best) { best = f; at = i * DEG; }
        sn = sn * cd + c * sd; c = t;                       /* on by one degree */
    }
    double a, f = golden_max(puck_fe, k, 1, at - DEG, at + DEG, &a);
    if (f > best) { best = f; at = a; }
    if (at > PI / 2) at -= PI;
    if (at <= -PI / 2) at += PI;
    *th = at;
    return best;
}

/* Puck: fibre fracture (Knops eq. 21 with the fibre data, eq. 14/15 without), inter-
   fibre fracture on the action plane, weakened by s1 (eq. 79-82, s = m = 0.5).
   Both exposures are linear in the load, so RF = 1 / fE. */
static void puck(const cv_fmat* m, const double s[6], cv_fres* r) {
    puck_ctx k = { s[1], s[2], s[4], s[5], s[3], m->Yt, m->S12, m->Yc / (2 * (1 + m->p23c)),
                   m->p12t, m->p12c, m->p23t, m->p23c };
    double s1 = s[0];
    if (m->E1f > 0 && m->E1 > 0) s1 -= (m->nu12 - m->nu12f * m->msf * m->E1 / m->E1f) * (s[1] + s[2]);
    double fff = s1 >= 0 ? s1 / m->Xt : -s1 / m->Xc;
    double ffs = s[0] >= 0 ? s[0] / m->Xt : -s[0] / m->Xc;     /* simple, for the weakening */
    double th, fe0 = puck_iff(&k, &th), eta = 1;
    if (ffs > 0 && fe0 > 0) {
        double c = fe0 / ffs;
        if (c < 0.5) c = 0.5;
        if (c < 2) eta = c * (sqrt(c * c + 12) + 3) / (2 * (c * c + 3));
    }
    double fiff = fe0 / eta;
    double c = cos(th), sn = k.s2 * c * c + k.s3 * sin(th) * sin(th) + 2 * k.t23 * sin(th) * c;
    r->fe_f = (float)fff;
    r->fe_m = (float)fiff;
    r->angle = (float)(th / DEG);
    if (fff >= fiff) {
        r->fi = (float)fff;
        r->mode = s1 >= 0 ? CV_FM_FT : CV_FM_FC;
    } else {
        r->fi = (float)fiff;
        r->mode = sn >= 0 ? CV_FM_IFFA : fabs(th) < DEG ? CV_FM_IFFB : CV_FM_IFFC;
    }
    r->rf = r->fi > 0 ? 1 / r->fi : INFINITY;
    if (!(r->fi > 0)) r->mode = CV_FM_NONE;
}

/* ---- LaRC03 (plane stress) --------------------------------------------------------- */

typedef struct {
    const cv_fmat* m;
    double s11, s22, t12;                   /* at lam = 1 */
    double yt, sl, st, g, etaT, etaL, a0, phic;
    /* out, at the last evaluation */
    int mode; double angle;
} larc_ctx;

/* Davila, Camanho, Rose eq. 33-34 with the shear's sign (LaRC04 eq. 149): the fibre
   misalignment under load, and the stresses rotated into it */
static bool misalign(const larc_ctx* k, double s11, double s22, double t12, double* s22m, double* t12m) {
    double den = k->m->G12 + s11 - s22;
    if (den <= 0) return false;
    double phi = (t12 >= 0 ? 1 : -1) * (fabs(t12) + (k->m->G12 - k->m->Xc) * k->phic) / den;
    double c = cos(phi), sn = sin(phi);
    *s22m = sn * sn * s11 + c * c * s22 - 2 * sn * c * t12;
    *t12m = -sn * c * s11 + sn * c * s22 + (c * c - sn * sn) * t12;
    return true;
}

static double l3_mt(const larc_ctx* k, double s22, double t12) {          /* criterion #1 */
    double x = s22 / k->yt;
    return (1 - k->g) * x + k->g * x * x + sq(t12 / k->sl);
}

typedef struct { const larc_ctx* k; double s22, t12; } l3_plane;
static double l3_mc_cs(const l3_plane* p, double c, double sn) {           /* criterion #2 at a */
    double tT = pos(-p->s22 * c * (sn - p->k->etaT * c));
    double tL = pos(c * (fabs(p->t12) + p->k->etaL * p->s22 * c));
    return sq(tT / p->k->st) + sq(tL / p->k->sl);
}

static double l3_mc_at(const void* cp, double lam, double a) {
    (void)lam;
    return l3_mc_cs(cp, cos(a), sin(a));
}

static double l3_mc(const larc_ctx* k, double s22, double t12, bool fine, double* angle) {
    l3_plane p = { k, s22, t12 };
    int n = (int)ceil(k->a0 / DEG);
    double h = k->a0 / n, best = -1, at = 0, c = 1, sn = 0, cd = cos(h), sd = sin(h);
    for (int i = 0; i <= n; i++) {
        double f = l3_mc_cs(&p, c, sn), t = c * cd - sn * sd;
        if (f > best) { best = f; at = i * h; }
        sn = sn * cd + c * sd; c = t;
    }
    if (fine) {
        double a, f = golden_max(l3_mc_at, &p, 1, at > h ? at - h : 0, at + h < k->a0 ? at + h : k->a0, &a);
        if (f > best) { best = f; at = a; }
    }
    *angle = at;
    return best;
}

static double l3_matrix(const void* cp, double lam, bool fine) {
    larc_ctx* k = (larc_ctx*)cp;
    double s11 = lam * k->s11, s22 = lam * k->s22, t12 = lam * k->t12, a = 0;
    k->mode = s22 >= 0 ? CV_FM_MT : CV_FM_MC;
    k->angle = 0;
    if (s22 >= 0) return l3_mt(k, s22, t12);
    if (s11 >= -k->m->Yc) { double f = l3_mc(k, s22, t12, fine, &a); k->angle = a; return f; }
    double s22m, t12m;                                                      /* criterion #6 */
    if (!misalign(k, s11, s22, t12, &s22m, &t12m)) return 1e30;
    if (s22m >= 0) { k->mode = CV_FM_MT; return l3_mt(k, s22m, t12m); }
    double f = l3_mc(k, s22m, t12m, fine, &a);
    k->angle = a;
    return f;
}

static double l3_fibre(const void* cp, double lam, bool fine) {
    larc_ctx* k = (larc_ctx*)cp;
    double s11 = lam * k->s11, s22 = lam * k->s22, t12 = lam * k->t12, s22m, t12m;
    (void)fine;
    if (s11 >= 0) { k->mode = CV_FM_FT; return (s11 - k->m->nu12 * s22) / k->m->Xt; }  /* #3: e11 / e1t */
    if (!misalign(k, s11, s22, t12, &s22m, &t12m)) { k->mode = CV_FM_FKC; return 1e30; }
    if (s22m < 0) { k->mode = CV_FM_FKC; return pos(fabs(t12m) + k->etaL * s22m) / k->sl; }   /* #4 */
    k->mode = CV_FM_FKT;                                                     /* #5 */
    return l3_mt(k, s22m, t12m);
}

/* the parameters of eqs. 10-12, 19 and 32. LaRC03 takes phiC from the in-situ SL (its
   appendix), LaRC05 from the measured, and etaL = etaT SL / ST (LaRC04 eq. 138; the
   same as eq. 11 when ST is the default) */
static void larc_setup(larc_ctx* k, const cv_fmat* m, const cv_fply* ply, bool l05) {
    bool thin;
    k->m = m;
    insitu(m, ply, &k->yt, &k->sl, &thin);
    k->a0 = alpha0_of(m);
    k->st = s23_of(m);
    k->etaT = -1 / tan(2 * k->a0);
    k->etaL = l05 ? k->etaT * m->S12 / k->st : -m->S12 * cos(2 * k->a0) / (m->Yc * sq(cos(k->a0)));
    double nu21 = m->nu12 * m->E2 / m->E1;
    double L22 = 2 * (1 / m->E2 - nu21 * nu21 / m->E1), L44 = 1 / m->G12;
    k->g = thin && m->GIc > 0 && m->GIIc > 0 ? m->GIc / m->GIIc : L22 / L44 * sq(k->yt / k->sl);
    double q = (l05 ? m->S12 : k->sl) / m->Xc, e = q + k->etaL, d = 1 - 4 * e * q;
    k->phic = atan((1 - sqrt(d > 0 ? d : 0)) / (2 * e));
}

static void larc03(const cv_fmat* m, const cv_fply* ply, const double s[6], cv_fres* r) {
    larc_ctx k = { 0 };
    larc_setup(&k, m, ply, false);
    k.s11 = s[0]; k.s22 = s[1]; k.t12 = s[3];
    double fm = l3_matrix(&k, 1, true), ang = k.angle;
    int mm = k.mode;
    double ff = l3_fibre(&k, 1, true);
    int fmode = k.mode;
    double rm, rfib;
    if (k.s22 >= 0) {                                       /* #1: a quadratic in the load */
        double x = k.s22 / k.yt, t = k.t12 / k.sl;
        rm = root_q(k.g * x * x + t * t, (1 - k.g) * x);
    } else {                                                /* #2 is of degree 2 while s11 >= -Yc */
        double a, f2 = l3_mc(&k, k.s22, k.t12, true, &a);
        rm = f2 > 0 ? 1 / sqrt(f2) : INFINITY;
        if (k.s11 < 0 && rm * -k.s11 > m->Yc) rm = solve_rf(l3_matrix, &k, fm, true);
    }
    if (k.s11 >= 0) rfib = ff > 0 ? 1 / ff : INFINITY;
    else rfib = solve_rf(l3_fibre, &k, ff, false);
    r->fe_m = (float)(1 / rm);
    r->fe_f = (float)(1 / rfib);
    r->rf = (float)(rm < rfib ? rm : rfib);
    if (ff >= fm) { r->fi = (float)ff; r->mode = (uint8_t)fmode; }
    else { r->fi = (float)fm; r->mode = (uint8_t)mm; r->angle = (float)(ang / DEG); }
    if (!(r->fi > 0)) r->mode = CV_FM_NONE;
}

/* ---- LaRC05 ---------------------------------------------------------------------------- */

typedef struct {
    larc_ctx k;
    double s[6];
    double sti;                             /* ST in-situ */
    double sn[180], tT[180], tL[180];       /* the plane tractions on the grid, lam = 1 */
    int mode; double angle;
} l5_ctx;

/* Pinho et al. (2012): the stress-based index of a plane */
static double l5_index(const l5_ctx* c, double sn, double tT, double tL) {
    double n = sn < 0 ? sn : 0;
    return sq(tT / (c->sti - c->k.etaT * n)) + sq(tL / (c->k.sl - c->k.etaL * n)) + sq(pos(sn) / c->k.yt);
}

static double l5_plane(const void* cp, double lam, double a) {
    const l5_ctx* c = cp;
    const double* s = c->s;
    double c2 = cos(2 * a), s2 = sin(2 * a);
    double sn = (s[1] + s[2]) / 2 + (s[1] - s[2]) / 2 * c2 + s[4] * s2;
    double tT = -(s[1] - s[2]) / 2 * s2 + s[4] * c2;
    double tL = s[3] * cos(a) + s[5] * sin(a);
    return l5_index(c, lam * sn, lam * tT, lam * tL);
}

static double l5_matrix(l5_ctx* c, double lam, bool fine) {
    double best = -1;
    int at = 0;
    for (int i = 0; i < 180; i++) {
        double f = l5_index(c, lam * c->sn[i], lam * c->tT[i], lam * c->tL[i]);
        if (f > best) { best = f; at = i; }
    }
    double a = at * DEG;
    if (fine) {
        double x, f = golden_max(l5_plane, c, lam, a - DEG, a + DEG, &x);
        if (f > best) { best = f; a = x; }
    }
    if (a < 0) a += PI;
    if (a >= PI) a -= PI;
    const double* s = c->s;
    double sn = (s[1] + s[2]) / 2 + (s[1] - s[2]) / 2 * cos(2 * a) + s[4] * sin(2 * a);
    c->mode = sn >= 0 ? CV_FM_MT : CV_FM_MC;
    c->angle = a;
    return best;
}

/* kinking and splitting: the kink plane psi (analytic, and a 30 degree sample), the
   misalignment phi in it, the index on the misaligned frame (Pinho 2012; LaRC05 code) */
static double l5_kink(const void* cp, double lam, bool fine) {
    l5_ctx* c = (l5_ctx*)cp;
    const cv_fmat* m = c->k.m;
    double s1 = lam * c->s[0], s2 = lam * c->s[1], s3 = lam * c->s[2], t12 = lam * c->s[3],
           t23 = lam * c->s[4], t31 = lam * c->s[5];
    double best = 0, psi0 = 0.5 * atan2(2 * t23, s2 - s3);
    (void)fine;
    for (int i = -2; i < 6; i++) {
        double psi = i == -2 ? psi0 : i == -1 ? psi0 + PI / 2 : i * 30 * DEG;
        double c2 = cos(2 * psi), sn2 = sin(2 * psi);
        double s2p = (s2 + s3) / 2 + (s2 - s3) / 2 * c2 + t23 * sn2;
        double t12p = t12 * cos(psi) + t31 * sin(psi);
        double t23p = -(s2 - s3) / 2 * sn2 + t23 * c2;
        double t31p = t31 * cos(psi) - t12 * sin(psi);
        double den = m->G12 + s1 - s2p;
        if (den <= 0) return 1e30;
        double phi = (t12p >= 0 ? 1 : -1) * (fabs(t12p) + (m->G12 - m->Xc) * c->k.phic) / den;
        double cp_ = cos(phi), sp = sin(phi);
        double s2m = sp * sp * s1 + cp_ * cp_ * s2p - 2 * sp * cp_ * t12p;
        double t12m = -sp * cp_ * s1 + sp * cp_ * s2p + (cp_ * cp_ - sp * sp) * t12p;
        double t23m = cp_ * t23p - sp * t31p;
        double f = l5_index(c, s2m, t23m, t12m);
        if (f > best) best = f;
    }
    c->mode = s1 < -m->Xc / 2 ? CV_FM_FC : CV_FM_SPLIT;
    return best;
}

/* the load factor that brings one plane to failure. Normalized, the index is
   u^2 = (b1 l / (1 + k1 l))^2 + (b2 l / (1 + k2 l))^2 (+ (b3 l)^2 in tension), u concave
   and rising in l: Newton from below converges from the left. Friction can keep u
   below 1 for good (INFINITY). */
static double l5_plane_rf(const l5_ctx* c, double sn, double tT, double tL) {
    double b1 = fabs(tT) / c->sti, b2 = fabs(tL) / c->k.sl, b3 = pos(sn) / c->k.yt;
    double g = b1 * b1 + b2 * b2 + b3 * b3;
    if (!(g > 0)) return INFINITY;
    if (sn >= 0) return 1 / sqrt(g);
    double k1 = c->k.etaT * -sn / c->sti, k2 = c->k.etaL * -sn / c->k.sl;
    double ui = (k1 > 0 ? sq(b1 / k1) : b1 > 0 ? INFINITY : 0) + (k2 > 0 ? sq(b2 / k2) : b2 > 0 ? INFINITY : 0);
    if (ui <= 1) return INFINITY;
    double x = 1 / sqrt(g);
    for (int it = 0; it < 60; it++) {
        double d1 = 1 + k1 * x, d2 = 1 + k2 * x, P = b1 * x / d1, Q = b2 * x / d2, u = sqrt(P * P + Q * Q);
        if (!(u > 0)) return INFINITY;
        double du = (P * b1 / (d1 * d1) + Q * b2 / (d2 * d2)) / u, dx = (1 - u) / du;
        x += dx;
        if (fabs(dx) < 1e-12 * x) break;
    }
    return x;
}

static double l5_rf_at(const l5_ctx* c, double a) {
    const double* s = c->s;
    double c2 = cos(2 * a), s2 = sin(2 * a);
    return l5_plane_rf(c, (s[1] + s[2]) / 2 + (s[1] - s[2]) / 2 * c2 + s[4] * s2,
                       -(s[1] - s[2]) / 2 * s2 + s[4] * c2, s[3] * cos(a) + s[5] * sin(a));
}

/* the matrix reserve factor: the least over the planes. A plane cannot fail below the
   load of its frictionless index, 1 / sqrt(g), so only planes under the best are solved;
   then a parabola through the best grid plane and its neighbours */
static double l5_matrix_rf(const l5_ctx* c) {
    double lb[180], best = INFINITY;
    int k = -1, i0 = 0;
    for (int i = 0; i < 180; i++) {
        double g = sq(c->tT[i] / c->sti) + sq(c->tL[i] / c->k.sl) + sq(pos(c->sn[i]) / c->k.yt);
        lb[i] = g > 0 ? 1 / sqrt(g) : INFINITY;
        if (lb[i] < lb[i0]) i0 = i;
    }
    for (int j = -1; j < 180; j++) {
        int i = j < 0 ? i0 : j;
        if (!(lb[i] < best) || (j == i0)) continue;
        double f = l5_plane_rf(c, c->sn[i], c->tT[i], c->tL[i]);
        if (f < best) { best = f; k = i; }
    }
    if (k < 0) return best;
    double a = k * DEG, rl = l5_rf_at(c, a - DEG), rh = l5_rf_at(c, a + DEG), den = rl - 2 * best + rh;
    if (rl >= best && rh >= best && den > 0 && rl < INFINITY && rh < INFINITY) {
        double d = DEG * (rl - rh) / (2 * den), f = l5_rf_at(c, a + d);
        if (f < best) best = f;
    }
    return best;
}

static void larc05(const cv_fmat* m, const cv_fply* ply, const double s[6], cv_fres* r) {
    l5_ctx* c = calloc(1, sizeof *c);
    if (!c) return;
    larc_setup(&c->k, m, ply, true);
    c->sti = c->k.st * c->k.sl / m->S12;
    memcpy(c->s, s, sizeof c->s);
    double ca = 1, sa = 0, cd = cos(DEG), sd = sin(DEG);    /* the planes by rotation: no trig */
    for (int i = 0; i < 180; i++) {
        double c2 = ca * ca - sa * sa, s2 = 2 * sa * ca, t = ca * cd - sa * sd;
        c->sn[i] = (s[1] + s[2]) / 2 + (s[1] - s[2]) / 2 * c2 + s[4] * s2;
        c->tT[i] = -(s[1] - s[2]) / 2 * s2 + s[4] * c2;
        c->tL[i] = s[3] * ca + s[5] * sa;
        sa = sa * cd + ca * sd; ca = t;
    }
    double fm = l5_matrix(c, 1, true), ang = c->angle;
    int mm = c->mode;
    double rm = l5_matrix_rf(c), ff, rfib;
    int fmode;
    if (s[0] >= 0) { ff = s[0] / m->Xt; fmode = CV_FM_FT; rfib = ff > 0 ? 1 / ff : INFINITY; }
    else { ff = l5_kink(c, 1, true); fmode = c->mode; rfib = solve_rf(l5_kink, c, ff, false); }
    r->fe_m = (float)(1 / rm);
    r->fe_f = (float)(1 / rfib);
    r->rf = (float)(rm < rfib ? rm : rfib);
    if (ff >= fm) { r->fi = (float)ff; r->mode = (uint8_t)fmode; }
    else { r->fi = (float)fm; r->mode = (uint8_t)mm; r->angle = (float)(ang / DEG); }
    if (!(r->fi > 0)) r->mode = CV_FM_NONE;
    free(c);
}

void cv_fail_eval(int c, const cv_fmat* m, const cv_fply* ply, const double s[6], cv_fres* r) {
    static const cv_fply none = { 0, CV_PLY_UD };
    r->fi = r->rf = r->angle = r->fe_f = r->fe_m = NAN;
    r->mode = CV_FM_NONE;
    for (int i = 0; i < 6; i++) if (s[i] != s[i]) return;
    if (!cv_fmat_valid(m, c, NULL, 0)) return;
    if (!ply) ply = &none;
    if (c == CV_FC_AUTO) c = cv_fc_auto(m);
    switch (c) {
        case CV_FC_MAXSTRESS: maxstress(m, s, r); break;
        case CV_FC_TSAIHILL:  tsaihill(m, s, r); break;
        case CV_FC_TSAIWU:    tsaiwu(m, s, r); break;
        case CV_FC_HASHIN:    hashin(m, s, r); break;
        case CV_FC_PUCK:      puck(m, s, r); break;
        case CV_FC_LARC03:    larc03(m, ply, s, r); break;
        case CV_FC_LARC05:    larc05(m, ply, s, r); break;
        default:              iso(c, m, s, r); break;
    }
}

/* ---- templates -------------------------------------------------------------------- */

/* Sources:
   WWFE-I   Soden, Hinton, Kaddour; data as tabulated in NASA TM-107331 (1996) T.II/III
   WWFE-II  Kaddour, Hinton; data as in Deuschle, PhD thesis Stuttgart (2010) App. B.2
   Puck     p and m_sigf by fibre class: Knops (2008) T.1, Deuschle (2010) T.4.1
   Metals   handbook minimum values from secondary sources: check the standard.
   Plastics typical datasheet values, unfilled, dry as moulded, 23 C, ISO 527:
            the yield stress as Sy and Sut. A starting point, not allowables. */
static cv_ftemplate tpl[48];
static int ntpl;

static cv_fmat* tpl_ud(const char* name, const char* src, double E1, double E2, double G12, double nu12,
                   double Xt, double Xc, double Yt, double Yc, double S12, double S23, double E1f, bool glass) {
    cv_ftemplate* t = &tpl[ntpl++];
    cv_fmat* m = &t->m;
    cv_fmat_defaults(m, CV_MK_UD);
    snprintf(m->name, sizeof m->name, "%s", name);
    t->source = src;
    m->E1 = E1; m->E2 = E2; m->G12 = G12; m->nu12 = nu12;
    m->Xt = Xt; m->Xc = Xc; m->Yt = Yt; m->Yc = Yc; m->S12 = S12; m->S23 = S23;
    m->E1f = E1f; m->nu12f = E1f > 0 ? 0.2 : 0;
    if (glass) { m->p12t = 0.30; m->p12c = 0.25; m->p23t = m->p23c = 0.25; m->msf = 1.3; }
    return m;
}

static cv_fmat* tpl_iso(const char* name, const char* src, double E, double nu, double Sy, double Sut, double Suc) {
    cv_ftemplate* t = &tpl[ntpl++];
    cv_fmat* m = &t->m;
    cv_fmat_defaults(m, CV_MK_ISO);
    snprintf(m->name, sizeof m->name, "%s", name);
    t->source = src;
    m->E1 = E; m->nu12 = nu; m->Sy = Sy; m->Sut = Sut; m->Suc = Suc;
    return m;
}

int cv_ftemplates(const cv_ftemplate** t) {
    if (!ntpl) {
        cv_fmat* m;
        m = tpl_ud("AS4/3501-6", "WWFE-I (NASA TM-107331 T.III); p23 Puck, Kopp, Knops 2002",
               126000, 11000, 6600, 0.28, 1950, 1480, 48, 200, 79, 0, 225000, false);
        m->p23t = m->p23c = 0.275;
        tpl_ud("T300/BSL914C", "WWFE-I (NASA TM-107331 T.III); S23 Puck R_perp_perp^A (Deuschle 2010)",
           138000, 11000, 5500, 0.28, 1500, 900, 27, 200, 80, 41, 230000, false);
        tpl_ud("E-glass/LY556", "WWFE-I (NASA TM-107331 T.III); S23 Puck R_perp_perp^A (Deuschle 2010)",
           53480, 17700, 5830, 0.278, 1140, 570, 35, 114, 72, 40, 80000, true);
        tpl_ud("E-glass/MY750", "WWFE-I/II (NASA TM-107331 T.III, Deuschle 2010 App. B.2)",
           45600, 16200, 5830, 0.278, 1280, 800, 40, 145, 73, 50, 74000, true);
        tpl_ud("IM7/8551-7", "WWFE-II (Deuschle 2010 App. B.2)",
           165000, 8400, 5600, 0.34, 2560, 1590, 73, 185, 90, 57, 276000, false);
        tpl_ud("T300/PR-319", "WWFE-II (Deuschle 2010 App. B.2)",
           129000, 5600, 1330, 0.318, 1378, 950, 40, 125, 97, 45, 230000, false);
        tpl_ud("A-S/Epoxy 1", "WWFE-II (Deuschle 2010 App. B.2)",
           140000, 10000, 6000, 0.3, 1990, 1500, 38, 150, 70, 50, 0, false);
        tpl_ud("S2-glass/Epoxy 2", "WWFE-II (Deuschle 2010 App. B.2)",
           52000, 19000, 6700, 0.3, 1700, 1150, 63, 180, 72, 40, 87000, true);
        m = tpl_ud("IM7/8552", "Camanho, Maimi, Davila, Compos Sci Technol 67 (2007) 2715, T.2/T.3",
               171420, 9080, 5290, 0.32, 2326.2, 1200.1, 62.3, 199.8, 92.3, 75.3, 276000, false);
        m->GIc = 0.2774; m->GIIc = 0.7879;
        m = tpl_ud("T800S/M21", "Guillamet et al., arXiv:2301.05552 (2023) T.1; Yc after Furtado",
               138400, 8540, 4290, 0.311, 2854, 1109, 56.6, 250, 93.7, 0, 0, false);
        m->GIc = 0.308; m->GIIc = 0.828;
        tpl_ud("AS4/PEEK (APC-2)", "NASA CR-181805 (1989) T.9; Xc, Yc, S23 secondary (Deuschle 2010)",
           142000, 9600, 6000, 0.25, 2070, 954.6, 79, 205.9, 115, 72.7, 225000, false);
        tpl_iso("S235", "EN 1993-1-1 T.3.1, t <= 40 mm (secondary source)", 210000, 0.3, 235, 360, 0);
        tpl_iso("S355", "EN 1993-1-1 T.3.1, t <= 40 mm (secondary source)", 210000, 0.3, 355, 490, 0);
        tpl_iso("Al 6061-T6", "minimum values (secondary source)", 69000, 0.33, 240, 290, 0);
        tpl_iso("Al 7075-T6", "minimum values (secondary source)", 71700, 0.33, 430, 510, 0);
        tpl_iso("Ti-6Al-4V", "grade 5 annealed, minimum values (secondary source)", 110000, 0.31, 880, 900, 0);
        tpl_iso("EN-GJL-250", "grey cast iron, brittle: use Mohr (secondary source)", 110000, 0.25, 0, 250, 950);
        tpl_iso("1.4301 (304)", "EN 10088-2 cold rolled strip, Rp0.2 and Rm minimum (secondary source)", 200000, 0.3, 230, 540, 0);
        tpl_iso("42CrMo4 QT", "EN 10083-3 quenched and tempered, 16 < d <= 40 mm, minimum (secondary source)", 210000, 0.3, 750, 1000, 0);
        tpl_iso("PEEK", "plastic: Victrex 450G datasheet, typical", 3700, 0.4, 98, 98, 0);
        tpl_iso("PA66", "plastic: unfilled, dry as moulded, typical (e.g. Ultramid A3K)", 3000, 0.39, 85, 85, 0);
        tpl_iso("PC", "plastic: unfilled, typical (e.g. Makrolon 2405)", 2400, 0.38, 65, 65, 0);
        tpl_iso("POM", "plastic: copolymer, typical (e.g. Hostaform C 9021)", 2850, 0.37, 64, 64, 0);
        tpl_iso("ABS", "plastic: general purpose, typical", 2300, 0.38, 45, 45, 0);
        tpl_iso("PP", "plastic: homopolymer, typical", 1500, 0.4, 33, 33, 0);
    }
    *t = tpl;
    return ntpl;
}

/* upper case letters and digits */
static void norm(const char* s, char* o, size_t n) {
    size_t k = 0;
    for (; s && *s && k + 1 < n; s++) {
        char c = *s;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) o[k++] = c;
    }
    o[k] = 0;
}

int cv_ftemplate_by_name(const char* name, int kind) {
    /* the longer, more particular keys first: "AS4PEEK" before "PEEK", "PA66" before "PA6" */
    static const struct { const char* key; const char* tpl; } keys[] = {
        { "IM785517", "IM7/8551-7" }, { "IM78552", "IM7/8552" }, { "8552", "IM7/8552" },
        { "T800", "T800S/M21" }, { "M21", "T800S/M21" }, { "AS43501", "AS4/3501-6" }, { "3501", "AS4/3501-6" },
        { "BSL914", "T300/BSL914C" }, { "914C", "T300/BSL914C" }, { "PR319", "T300/PR-319" },
        { "LY556", "E-glass/LY556" }, { "MY750", "E-glass/MY750" }, { "S2GLASS", "S2-glass/Epoxy 2" },
        { "APC2", "AS4/PEEK (APC-2)" }, { "AS4PEEK", "AS4/PEEK (APC-2)" }, { "CFPEEK", "AS4/PEEK (APC-2)" },
        { "CFRP", "IM7/8552" }, { "CARBON", "IM7/8552" }, { "GFRP", "E-glass/MY750" }, { "GLASS", "E-glass/MY750" },
        { "S235", "S235" }, { "S355", "S355" }, { "14301", "1.4301 (304)" }, { "AISI304", "1.4301 (304)" },
        { "SS304", "1.4301 (304)" }, { "X5CRNI", "1.4301 (304)" }, { "STAINLESS", "1.4301 (304)" }, { "INOX", "1.4301 (304)" },
        { "42CRMO4", "42CrMo4 QT" }, { "STEEL", "S235" }, { "STAHL", "S235" },
        { "6061", "Al 6061-T6" }, { "7075", "Al 7075-T6" }, { "ALU", "Al 6061-T6" },
        { "TI6AL4V", "Ti-6Al-4V" }, { "TI64", "Ti-6Al-4V" }, { "TITAN", "Ti-6Al-4V" },
        { "GJL", "EN-GJL-250" }, { "GREYIRON", "EN-GJL-250" }, { "GRAYIRON", "EN-GJL-250" }, { "CASTIRON", "EN-GJL-250" },
        { "PEEK", "PEEK" }, { "PA66", "PA66" }, { "PA6", "PA66" }, { "NYLON", "PA66" }, { "POLYAMID", "PA66" },
        { "POLYCARB", "PC" }, { "MAKROLON", "PC" }, { "LEXAN", "PC" },
        { "POM", "POM" }, { "ACETAL", "POM" }, { "DELRIN", "POM" }, { "HOSTAFORM", "POM" },
        { "ABS", "ABS" }, { "POLYPROP", "PP" },
    };
    char n[96];
    norm(name, n, sizeof n);
    if (!n[0]) return -1;
    const cv_ftemplate* t;
    int nt = cv_ftemplates(&t);
    for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) {
        if (!strstr(n, keys[k].key)) continue;
        for (int i = 0; i < nt; i++)
            if (!strcmp(t[i].m.name, keys[k].tpl) && (kind < 0 || t[i].m.kind == kind)) return i;
    }
    return -1;
}

int cv_ftemplate_nearest(int kind, double E1, double E2, double nu, double sy) {
    if (!(E1 > 0)) return -1;
    const cv_ftemplate* t;
    int nt = cv_ftemplates(&t), best = -1;
    double bd = INFINITY;
    for (int i = 0; i < nt; i++) {
        const cv_fmat* m = &t[i].m;
        if (m->kind != kind) continue;
        double d;
        if (kind == CV_MK_UD) d = fabs(log(E1 / m->E1)) + (E2 > 0 ? fabs(log(E2 / m->E2)) : 0);
        else {
            if ((E1 < 10000) != (m->E1 < 10000)) continue;          /* a plastic, or a metal */
            d = fabs(log(E1 / m->E1)) + 2 * fabs(nu - m->nu12);
            if (sy > 0) d += m->Sy > 0 ? 0.5 * fabs(log(sy / m->Sy)) : 1;   /* it yields: not the brittle one */
        }
        if (d < bd - 1e-12) { bd = d; best = i; }
    }
    return best;
}
