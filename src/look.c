/* look.c -- the Studio look's surface materials and the guess of which one a deck
   material is: from its name, else from E / nu / rho read in each plausible unit
   system (see look.h). Headless. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "look.h"
#include "units.h"
#include "inp.h"

const cv_look_mat cv_look_mats[CV_LOOK_N] = {
    [CV_LOOK_DEFAULT]   = { "Default",         { 0.70f, 0.70f, 0.70f }, 0, 0.50f },
    [CV_LOOK_STEEL]     = { "Steel",           { 0.56f, 0.57f, 0.58f }, 1, 0.35f },
    [CV_LOOK_STAINLESS] = { "Stainless steel", { 0.65f, 0.66f, 0.67f }, 1, 0.25f },
    [CV_LOOK_CAST_IRON] = { "Cast iron",       { 0.42f, 0.42f, 0.42f }, 1, 0.60f },
    [CV_LOOK_ALUMINIUM] = { "Aluminium",       { 0.91f, 0.92f, 0.92f }, 1, 0.30f },
    [CV_LOOK_TITANIUM]  = { "Titanium",        { 0.54f, 0.50f, 0.45f }, 1, 0.35f },
    [CV_LOOK_COPPER]    = { "Copper",          { 0.95f, 0.64f, 0.54f }, 1, 0.30f },
    [CV_LOOK_BRASS]     = { "Brass",           { 0.91f, 0.78f, 0.42f }, 1, 0.30f },
    [CV_LOOK_MAGNESIUM] = { "Magnesium",       { 0.78f, 0.79f, 0.80f }, 1, 0.40f },
    [CV_LOOK_CONCRETE]  = { "Concrete",        { 0.50f, 0.49f, 0.46f }, 0, 0.90f },
    [CV_LOOK_GLASS]     = { "Glass",           { 0.80f, 0.88f, 0.88f }, 0, 0.05f },
    [CV_LOOK_CERAMIC]   = { "Ceramic",         { 0.88f, 0.86f, 0.80f }, 0, 0.30f },
    [CV_LOOK_WOOD]      = { "Wood",            { 0.45f, 0.30f, 0.17f }, 0, 0.70f },
    [CV_LOOK_CFRP]      = { "CFRP",            { 0.05f, 0.05f, 0.05f }, 0, 0.35f },
    [CV_LOOK_GFRP]      = { "GFRP",            { 0.72f, 0.78f, 0.55f }, 0, 0.50f },
    [CV_LOOK_PLASTIC]   = { "Plastic",         { 0.45f, 0.50f, 0.58f }, 0, 0.45f },
    [CV_LOOK_RUBBER]    = { "Rubber",          { 0.03f, 0.03f, 0.03f }, 0, 0.85f },
    [CV_LOOK_PAINTED]   = { "Painted",         { 0.55f, 0.55f, 0.55f }, 0, 0.35f },
};

/* a name with spaces, '_' and '-' dropped, upper case, into out */
static void squash(const char* s, char* out, size_t n) {
    size_t k = 0;
    for (; *s && k + 1 < n; s++) if (isalnum((unsigned char)*s)) out[k++] = (char)toupper((unsigned char)*s);
    out[k] = 0;
}

int cv_look_find(const char* name) {
    if (!name || !*name) return -1;
    char a[64]; squash(name, a, sizeof a);
    for (int i = 0; i < CV_LOOK_N; i++) {
        char b[64]; squash(cv_look_mats[i].name, b, sizeof b);
        if (!strcmp(a, b)) return i;
    }
    return -1;
}

/* ---- name tokens ---- */
static bool eq(const char* t, const char* w)  { return !strcmp(t, w); }
static bool pre(const char* t, const char* w) { return !strncmp(t, w, strlen(w)); }
static bool has(const char* t, const char* w) { return strstr(t, w) != NULL; }
/* the word w alone or followed only by digits ("PE", "PE100") */
static bool wd(const char* t, const char* w) {
    if (!pre(t, w)) return false;
    for (const char* c = t + strlen(w); *c; c++) if (!isdigit((unsigned char)*c)) return false;
    return true;
}
static bool dig(const char* c, int n) {
    for (int i = 0; i < n; i++) if (!isdigit((unsigned char)c[i])) return false;
    return true;
}

/* The look one upper-case token names, -1 none. next: the character after the token. */
static int tok_look(const char* t, char next, char after) {
    if (wd(t, "GFRP") || eq(t, "GFK") || has(t, "GLASSFIB") || eq(t, "EGLASS")) return CV_LOOK_GFRP;
    if (wd(t, "CFRP") || eq(t, "CFK") || pre(t, "CARBON") || eq(t, "T300") || eq(t, "T700") || eq(t, "IM7")) return CV_LOOK_CFRP;
    if (has(t, "AL2O3") || pre(t, "ALUMINA") || pre(t, "CERAMIC") || wd(t, "SIC") || wd(t, "ZRO2")) return CV_LOOK_CERAMIC;
    if (has(t, "INOX") || has(t, "STAINLESS") || (t[0] == 'X' && isdigit((unsigned char)t[1]) && strstr(t, "CR")) ||
        eq(t, "304") || eq(t, "304L") || eq(t, "316") || eq(t, "316L") || pre(t, "AISI3")) return CV_LOOK_STAINLESS;
    if (wd(t, "GG") || pre(t, "GJL") || pre(t, "GJS") || has(t, "CASTIRON") || has(t, "GUSS")) return CV_LOOK_CAST_IRON;
    if (has(t, "STEEL") || has(t, "STAHL") || (t[0] == 'S' && dig(t + 1, 3) && !isdigit((unsigned char)t[4]))) return CV_LOOK_STEEL;
    if (has(t, "TITAN") || wd(t, "TI") || (t[0] == 'T' && t[1] == 'I' && (isdigit((unsigned char)t[2]) || pre(t + 2, "AL")))) return CV_LOOK_TITANIUM;
    if (pre(t, "CUZN") || has(t, "BRASS") || has(t, "MESSING")) return CV_LOOK_BRASS;
    if (wd(t, "CU") || has(t, "COPPER") || has(t, "KUPFER")) return CV_LOOK_COPPER;
    if (pre(t, "ALU") || wd(t, "AL") || pre(t, "ALMG") || eq(t, "6061") || eq(t, "7075") || eq(t, "2024")) return CV_LOOK_ALUMINIUM;
    if (wd(t, "MG") || pre(t, "MAGNES") || pre(t, "AZ31") || pre(t, "AZ91")) return CV_LOOK_MAGNESIUM;
    if (has(t, "CONCRETE") || pre(t, "BETON") ||
        (t[0] == 'C' && isdigit((unsigned char)t[1]) && strspn(t + 1, "0123456789") == strlen(t + 1) && next == '/' && isdigit((unsigned char)after)))
        return CV_LOOK_CONCRETE;
    if (pre(t, "GLAS")) return CV_LOOK_GLASS;
    if (pre(t, "WOOD") || pre(t, "TIMBER") || pre(t, "HOLZ") || pre(t, "PINE") || pre(t, "SPRUCE") || pre(t, "OAK") ||
        (t[0] == 'G' && t[1] == 'L' && dig(t + 2, 2) && !t[4])) return CV_LOOK_WOOD;
    if (pre(t, "PLASTIC") || pre(t, "NYLON") || eq(t, "PEEK") || eq(t, "PMMA") || wd(t, "PA") || wd(t, "ABS") || wd(t, "PP") ||
        wd(t, "PE") || wd(t, "PVC") || wd(t, "POM") || wd(t, "PC")) return CV_LOOK_PLASTIC;
    if (has(t, "RUBBER") || pre(t, "EPDM") || wd(t, "NBR") || pre(t, "NEOPRENE") || pre(t, "SILICONE") ||
        pre(t, "ELASTOMER") || pre(t, "GUMMI")) return CV_LOOK_RUBBER;
    return -1;
}

/* the look the name says (specific before general: the lowest id in `rank` wins), -1 none; word: what hit */
static int name_look(const char* name, char* word, size_t wn) {
    static const int rank[CV_LOOK_N] = { 99, 6, 4, 5, 10, 7, 9, 8, 11, 12, 13, 3, 14, 1, 0, 15, 16, 17 };
    int best = -1;
    char c[64]; squash(name, c, sizeof c);
    if (strstr(c, "GLASSFIB") || strstr(c, "EGLASS")) { snprintf(word, wn, "%s", name); return CV_LOOK_GFRP; }
    if (strstr(c, "CASTIRON")) { snprintf(word, wn, "%s", name); return CV_LOOK_CAST_IRON; }
    for (const char* s = name; *s;) {
        while (*s && !isalnum((unsigned char)*s)) s++;
        char t[64]; size_t k = 0;
        while (isalnum((unsigned char)*s)) { if (k + 1 < sizeof t) t[k++] = (char)toupper((unsigned char)*s); s++; }
        t[k] = 0;
        if (!k) break;
        int id = tok_look(t, s[0], s[0] ? s[1] : 0);
        if (id >= 0 && (best < 0 || rank[id] < rank[best])) { best = id; snprintf(word, wn, "%s", t); }
    }
    return best;
}

/* ---- constants ---- */
typedef struct { int id; double E, rho, nu; bool ortho; } ref;   /* ortho: fine for a ply / grain; nu 0: none */
static const ref refs[] = {
    { CV_LOOK_STEEL, 205e9, 7850, 0.29, false },    { CV_LOOK_CAST_IRON, 120e9, 7150, 0.26, false },
    { CV_LOOK_ALUMINIUM, 70e9, 2700, 0.33, false }, { CV_LOOK_TITANIUM, 113e9, 4430, 0.34, false },
    { CV_LOOK_COPPER, 120e9, 8940, 0.34, false },   { CV_LOOK_BRASS, 100e9, 8500, 0.34, false },
    { CV_LOOK_MAGNESIUM, 45e9, 1770, 0.35, false }, { CV_LOOK_CONCRETE, 32e9, 2400, 0.2, false },
    { CV_LOOK_GLASS, 70e9, 2500, 0.22, false },     { CV_LOOK_CERAMIC, 350e9, 3900, 0.22, false },
    { CV_LOOK_WOOD, 11e9, 500, 0, true },           { CV_LOOK_CFRP, 135e9, 1550, 0, true },
    { CV_LOOK_GFRP, 40e9, 1950, 0, true },          { CV_LOOK_PLASTIC, 2.5e9, 1150, 0.38, false },
    { CV_LOOK_RUBBER, 5e6, 1100, 0.49, false },
};
#define NREF ((int)(sizeof refs / sizeof refs[0]))

/* E alone is only trusted for these */
static bool alone_ok(int id) {
    return id == CV_LOOK_STEEL || id == CV_LOOK_ALUMINIUM || id == CV_LOOK_TITANIUM ||
           id == CV_LOOK_CONCRETE || id == CV_LOOK_PLASTIC || id == CV_LOOK_RUBBER;
}

static double sys_rho(int sys) {   /* kg/m3 per file density unit */
    double v = cv_sys_si(sys, CV_Q_VOLUME);
    return v > 0 ? cv_sys_si(sys, CV_Q_MASS) / v : 0;
}

#define ACCEPT 0.6    /* worst score taken: log-distance sum, see tok/refs above */

static void fmt_e(double E, char* o, size_t n) {
    if (E >= 1e9) snprintf(o, n, "%.3g GPa", E / 1e9);
    else if (E >= 1e6) snprintf(o, n, "%.3g MPa", E / 1e6);
    else snprintf(o, n, "%.3g Pa", E);
}

int cv_look_guess(const char* name, const cv_matprop* p, int sys, char* why, int n) {
    char w[64] = "";
    int r = name ? name_look(name, w, sizeof w) : -1;
    if (r >= 0) { if (why && n > 0) snprintf(why, (size_t)n, "name: %s", w); return r; }
    if (!p || p->el == CV_EL_NONE) goto none;

    bool iso = p->el == CV_EL_ISO;
    double E = p->c[0];
    if (p->el == CV_EL_ENG) E = fmax(p->c[0], p->c[1]);
    else if (p->el >= CV_EL_ORTHO) E = fmax(p->c[0], p->c[2]);
    double nu = iso ? p->c[1] : 0, rho = (p->has & CV_MP_RHO) ? p->rho : 0;
    if (!(E > 0)) goto none;
    bool alone = rho <= 0 && !(nu > 0);

    double best = 1e9; int bid = -1, bsys = 0;
    for (int s = 1; s < CV_SYS_N; s++) {
        if (sys > CV_SYS_NONE && s != sys) continue;
        double kE = cv_sys_si(s, CV_Q_STRESS), kR = sys_rho(s);
        if (!(kE > 0)) continue;
        if (alone && sys == CV_SYS_NONE && s != CV_SYS_MM_T_S && s != CV_SYS_M_KG_S) continue;
        double prior = s == CV_SYS_MM_T_S ? 0 : s == CV_SYS_M_KG_S ? 0.01 : 0.02;
        for (int i = 0; i < NREF; i++) {
            const ref* m = &refs[i];
            double d = fabs(log(E * kE / m->E));
            if (alone && (!alone_ok(m->id) || d > 0.14)) continue;   /* within ~15% */
            double sc = d + prior;
            if (rho > 0 && kR > 0) sc += 1.5 * fabs(log(rho * kR / m->rho));
            if (nu > 0 && m->nu > 0) sc += 6 * fabs(nu - m->nu);
            if (!iso && !m->ortho) sc += 0.7;       /* a ply or grain is not a metal */
            if (iso && m->ortho) sc += 0.3;
            if (sc < best) { best = sc; bid = m->id; bsys = s; }
        }
    }
    if (bid >= 0 && best < ACCEPT) {
        if (why && n > 0) {
            char e[32], rest[64] = "";
            fmt_e(E * cv_sys_si(bsys, CV_Q_STRESS), e, sizeof e);
            if (nu > 0) snprintf(rest, sizeof rest, ", nu %.3g", nu);
            if (rho > 0) {
                size_t l = strlen(rest);
                snprintf(rest + l, sizeof rest - l, ", rho %.4g kg/m3", rho * sys_rho(bsys));
            }
            snprintf(why, (size_t)n, "E %s%s (%s)", e, rest, cv_sys_name(bsys));
        }
        return bid;
    }
none:
    if (why && n > 0) snprintf(why, (size_t)n, "nothing known");
    return CV_LOOK_DEFAULT;
}
