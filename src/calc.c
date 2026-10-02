/* calc.c -- calculated fields (calc.h). The user's names are rewritten to v0, v1, ...
   before TinyExpr sees the text, so any field or component name works, whatever its
   case, and a name TinyExpr would take for a builtin (E, PI) stays the user's. */
#include "calc.h"
#include "field.h"
#include <ctype.h>
#include <math.h>

/* TinyExpr itself, compiled here: -a^2 is -(a^2) and 2^3^2 is 2^(3^2), as on paper;
   log is the natural logarithm (log10 is there too) */
#define TE_POW_FROM_RIGHT
#define TE_NAT_LOG
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#pragma GCC diagnostic ignored "-Wpedantic"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4996 4090)
#endif
#include "tinyexpr.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

enum { K_FIELD, K_X, K_Y, K_Z, K_TIME };
enum { MAX_SYM = 64 };

typedef struct {
    int  kind;
    char field[16];          /* K_FIELD: the field, as the file names it */
    char comp[12];           /* a stored component by name, or "" for a derived value */
    int  code;               /* derived: CV_COMP_MAG, _MISES, _P1.._P3 (frd shear order) */
} sym;

struct cv_calc {
    te_expr* expr;
    int      nsym;
    sym      s[MAX_SYM];
    double   slot[MAX_SYM];  /* TinyExpr reads the variables from here */
    float*   buf;            /* nsym * cap values of the current evaluation */
    uint32_t cap;
    char     missing[40];
};

/* ---- functions TinyExpr lacks ---- */
static double f_min(double a, double b) { return a < b || a != a ? a : b; }   /* NaN in, NaN out */
static double f_max(double a, double b) { return a > b || a != a ? a : b; }
static double f_clamp(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }
static double f_sign(double x) { return x > 0 ? 1 : x < 0 ? -1 : x; }   /* NaN stays */
static double f_if(double c, double a, double b) { return c != c ? c : c != 0 ? a : b; }

static const struct { const char* name; void* fn; int type; } kFuncs[] = {
    { "min", (void*)f_min, TE_FUNCTION2 }, { "max", (void*)f_max, TE_FUNCTION2 },
    { "clamp", (void*)f_clamp, TE_FUNCTION3 }, { "sign", (void*)f_sign, TE_FUNCTION1 },
    { "if", (void*)f_if, TE_FUNCTION3 },
};
static const char* const kBuiltin[] = {
    "abs", "acos", "asin", "atan", "atan2", "ceil", "cos", "cosh", "exp", "fac", "floor",
    "ln", "log", "log10", "ncr", "npr", "pow", "sin", "sinh", "sqrt", "tan", "tanh",
};

static bool ieq(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i]) || !b[i]) return false;
    return b[n] == 0;
}

/* the first description of a field called name (any case) in any step */
static const cv_field_desc* find_desc(const cv_frd* f, const char* name, size_t n) {
    for (int s = 0; s < f->n_steps; s++)
        for (int i = 0; i < f->steps[s].nfields; i++)
            if (ieq(name, f->steps[s].fields[i].name, n)) return &f->steps[s].fields[i];
    return NULL;
}

static int comp_index(const cv_field_desc* d, const char* name, size_t n) {
    for (int c = 0; c < d->ncomp; c++) if (d->comp[c][0] && ieq(name, d->comp[c], n)) return c;
    return -1;
}

/* a derived value of d by suffix: MAG, MISES, P1..P3 (a CV_COMP_ code), else 0 */
static int derived(const cv_field_desc* d, const char* suf, size_t n) {
    if (d->ncomp >= 2 && d->ncomp <= 3 && ieq(suf, "MAG", n)) return CV_COMP_MAG;
    if (!cv_tensor_order(d)) return 0;
    if (ieq(suf, "MISES", n)) return CV_COMP_MISES;
    if (n == 2 && toupper((unsigned char)suf[0]) == 'P' && suf[1] >= '1' && suf[1] <= '3')
        return CV_COMP_P1 - (suf[1] - '1');
    return 0;
}

static void set_field(sym* s, const cv_field_desc* d, int comp, int code) {
    s->kind = K_FIELD;
    snprintf(s->field, sizeof s->field, "%s", d->name);
    if (comp >= 0) snprintf(s->comp, sizeof s->comp, "%s", d->comp[comp]);
    else s->comp[0] = 0;
    s->code = code;
}

/* What the name id[0..n) stands for; false with a reason in err. */
static bool resolve(const cv_frd* f, const char* id, size_t n, sym* out, char* err, size_t errn) {
    memset(out, 0, sizeof *out);
    if (ieq(id, "X", n)) { out->kind = K_X; return true; }
    if (ieq(id, "Y", n)) { out->kind = K_Y; return true; }
    if (ieq(id, "Z", n)) { out->kind = K_Z; return true; }
    if (ieq(id, "TIME", n)) { out->kind = K_TIME; return true; }
    /* FIELD_SUFFIX, split at any underscore (a field name may hold one) */
    for (size_t k = 1; k + 1 < n; k++) {
        if (id[k] != '_') continue;
        const cv_field_desc* d = find_desc(f, id, k);
        if (!d) continue;
        const char* suf = id + k + 1;
        size_t sn = n - k - 1;
        int c = comp_index(d, suf, sn), code;
        if (c >= 0) { set_field(out, d, c, 0); return true; }
        if ((code = derived(d, suf, sn))) { set_field(out, d, -1, code); return true; }
    }
    /* a field on its own */
    const cv_field_desc* d = find_desc(f, id, n);
    if (d && d->ncomp == 1) { set_field(out, d, 0, 0); return true; }
    if (d && d->ncomp <= 3) { set_field(out, d, -1, CV_COMP_MAG); return true; }
    if (d) {
        snprintf(err, errn, "%s has %d components: write %s_%s", d->name, d->ncomp, d->name,
                 cv_tensor_order(d) ? "MISES" : d->comp[0]);
        return false;
    }
    /* a component on its own, when one field alone has it */
    const cv_field_desc* hit = NULL;
    int hc = -1;
    for (int s = 0; s < f->n_steps; s++)
        for (int i = 0; i < f->steps[s].nfields; i++) {
            const cv_field_desc* e = &f->steps[s].fields[i];
            int c = comp_index(e, id, n);
            if (c < 0 || (hit && !strcmp(hit->name, e->name))) continue;
            if (hit) {
                snprintf(err, errn, "%.*s is in %s and %s: write %s_%.*s", (int)n, id, hit->name,
                         e->name, hit->name, (int)n, id);
                return false;
            }
            hit = e; hc = c;
        }
    if (hit) { set_field(out, hit, hc, 0); return true; }
    /* the usual shorthands */
    static const struct { const char* name; const char* field; int code; } kShort[] = {
        { "MISES", "STRESS", CV_COMP_MISES },
        { "S1", "STRESS", CV_COMP_P1 }, { "S2", "STRESS", CV_COMP_P2 }, { "S3", "STRESS", CV_COMP_P3 },
        { "E1", "TOSTRAIN", CV_COMP_P1 }, { "E2", "TOSTRAIN", CV_COMP_P2 }, { "E3", "TOSTRAIN", CV_COMP_P3 },
    };
    for (size_t i = 0; i < CV_COUNT(kShort); i++) {
        if (!ieq(id, kShort[i].name, n)) continue;
        d = find_desc(f, kShort[i].field, strlen(kShort[i].field));
        if (d && cv_tensor_order(d)) { set_field(out, d, -1, kShort[i].code); return true; }
        snprintf(err, errn, "%.*s needs a %s field", (int)n, id, kShort[i].field);
        return false;
    }
    snprintf(err, errn, "unknown name %.*s", (int)n, id);
    return false;
}

static bool is_func(const char* id, size_t n) {
    for (size_t i = 0; i < CV_COUNT(kBuiltin); i++) if (ieq(id, kBuiltin[i], n)) return true;
    for (size_t i = 0; i < CV_COUNT(kFuncs); i++) if (ieq(id, kFuncs[i].name, n)) return true;
    return false;
}

cv_calc* cv_calc_compile(const cv_frd* f, const char* expr, char* err, size_t errn) {
    char dummy[8];
    if (!err || !errn) { err = dummy; errn = sizeof dummy; }
    err[0] = 0;
    size_t len = strlen(expr);
    cv_calc* c = calloc(1, sizeof *c);
    char* text = malloc(len * 2 + 8);           /* a name becomes at most v63 */
    size_t* from = malloc((len * 2 + 8) * sizeof(size_t));   /* text offset -> expr offset */
    if (!c || !text || !from) { free(c); free(text); free(from); snprintf(err, errn, "out of memory"); return NULL; }
    size_t o = 0, i = 0;
    bool any = false;
    while (i < len) {
        unsigned char ch = (unsigned char)expr[i];
        if (isdigit(ch) || (ch == '.' && isdigit((unsigned char)expr[i + 1]))) {
            /* a number, exponent included, so 1e5 is not the name e5 */
            size_t b = i;
            while (isdigit((unsigned char)expr[i]) || expr[i] == '.') i++;
            if ((expr[i] == 'e' || expr[i] == 'E') &&
                (isdigit((unsigned char)expr[i + 1]) ||
                 ((expr[i + 1] == '+' || expr[i + 1] == '-') && isdigit((unsigned char)expr[i + 2])))) {
                i += 2;
                while (isdigit((unsigned char)expr[i])) i++;
            }
            for (; b < i; b++) { from[o] = b; text[o++] = expr[b]; }
            any = true;
        } else if (isalpha(ch) || ch == '_') {
            size_t b = i;
            while (isalnum((unsigned char)expr[i]) || expr[i] == '_') i++;
            size_t n = i - b, p = i;
            while (expr[p] == ' ' || expr[p] == '\t') p++;
            if (expr[p] == '(' && is_func(expr + b, n)) {          /* a function: lower case */
                for (size_t k = 0; k < n; k++) { from[o] = b + k; text[o++] = (char)tolower((unsigned char)expr[b + k]); }
            } else if (expr[p] == '(') {
                snprintf(err, errn, "unknown function %.*s at %d", (int)n, expr + b, (int)b + 1);
                goto fail;
            } else {
                sym s;
                char why[96];
                if (!resolve(f, expr + b, n, &s, why, sizeof why)) {
                    if (ieq(expr + b, "PI", n) || ieq(expr + b, "E", n)) {   /* the constants */
                        for (size_t k = 0; k < n; k++) { from[o] = b + k; text[o++] = (char)tolower((unsigned char)expr[b + k]); }
                        any = true;
                        continue;
                    }
                    snprintf(err, errn, "%s at %d", why, (int)b + 1);
                    goto fail;
                }
                int k = 0;
                while (k < c->nsym && memcmp(&c->s[k], &s, sizeof s)) k++;
                if (k == c->nsym) {
                    if (c->nsym == MAX_SYM) { snprintf(err, errn, "more than %d names", MAX_SYM); goto fail; }
                    c->s[c->nsym++] = s;
                }
                int w = snprintf(text + o, 4, "v%d", k);
                for (int q = 0; q < w; q++) from[o + q] = b;
                o += (size_t)w;
            }
            any = true;
        } else {
            from[o] = i; text[o++] = expr[i++];
        }
    }
    text[o] = 0;
    from[o] = len;
    if (!any) { snprintf(err, errn, "empty formula"); goto fail; }

    te_variable vars[MAX_SYM + CV_COUNT(kFuncs)];
    char names[MAX_SYM][4];                     /* read only while compiling */
    int nv = 0;
    for (int k = 0; k < c->nsym; k++) {
        snprintf(names[k], sizeof names[k], "v%d", k);
        vars[nv++] = (te_variable){ names[k], &c->slot[k], TE_VARIABLE, NULL };
    }
    for (size_t k = 0; k < CV_COUNT(kFuncs); k++)
        vars[nv++] = (te_variable){ kFuncs[k].name, kFuncs[k].fn, kFuncs[k].type | TE_FLAG_PURE, NULL };
    int at = 0;
    c->expr = te_compile(text, vars, nv, &at);
    if (!c->expr) {
        size_t p = at > 0 ? (size_t)at - 1 : 0;
        if (p > o) p = o;
        snprintf(err, errn, "syntax error at %d", (int)from[p] + 1);
        goto fail;
    }
    free(text); free(from);
    return c;
fail:
    free(text); free(from);
    cv_calc_free(c);
    return NULL;
}

bool cv_calc_uses_fields(const cv_calc* c) {
    for (int k = 0; k < c->nsym; k++) if (c->s[k].kind == K_FIELD) return true;
    return false;
}

const char* cv_calc_missing(const cv_calc* c) { return c->missing; }

static int step_field(const cv_step* st, const char* name) {
    for (int i = 0; i < st->nfields; i++) if (!strcmp(st->fields[i].name, name)) return i;
    return -1;
}

bool cv_calc_eval(cv_calc* c, const cv_frd* f, int step, cv_calc_get_fn get, void* ud,
                  const uint32_t* nodes, uint32_t n, float* out) {
    c->missing[0] = 0;
    if (!nodes) n = f->n_nodes;
    bool ok = step >= 0 && step < f->n_steps;
    if (ok && n > c->cap) {
        float* b = realloc(c->buf, (size_t)CV_MAX(c->nsym, 1) * n * sizeof(float));
        if (b) { c->buf = b; c->cap = n; } else ok = false;
    }
    const cv_step* st = ok ? &f->steps[step] : NULL;
    for (int k = 0; ok && k < c->nsym; k++) {
        const sym* s = &c->s[k];
        float* b = c->buf + (size_t)k * n;
        if (s->kind != K_FIELD) {
            for (uint32_t i = 0; i < n; i++) {
                uint32_t j = nodes ? nodes[i] : i;
                b[i] = s->kind == K_TIME ? st->time : f->xyz[3 * (size_t)j + (s->kind - K_X)];
            }
            continue;
        }
        int fi = step_field(st, s->field);
        const cv_field_desc* d = fi >= 0 ? &st->fields[fi] : NULL;
        int code = s->code;
        if (d && s->comp[0]) code = comp_index(d, s->comp, strlen(s->comp));
        else if (d && code <= CV_COMP_P1 && code >= CV_COMP_P3 && cv_tensor_order(d) == 2) code += CV_COMP_P1_XZ - CV_COMP_P1;
        if (!d || (s->comp[0] && code < 0)) {
            snprintf(c->missing, sizeof c->missing, "%s%s%s", s->field, s->comp[0] ? "_" : "", s->comp);
            ok = false;
            break;
        }
        const float* v = get(ud, step, fi);
        if (!v) { ok = false; break; }
        if (!nodes) cv_field_scalar(v, d->ncomp, n, code, b);
        else for (uint32_t i = 0; i < n; i++) cv_field_scalar(v + (size_t)nodes[i] * d->ncomp, d->ncomp, 1, code, b + i);
    }
    if (!ok) {
        for (uint32_t i = 0; i < n; i++) out[i] = NAN;
        return false;
    }
    for (uint32_t i = 0; i < n; i++) {
        for (int k = 0; k < c->nsym; k++) c->slot[k] = c->buf[(size_t)k * n + i];
        out[i] = (float)te_eval(c->expr);
    }
    return true;
}

void cv_calc_free(cv_calc* c) {
    if (!c) return;
    te_free(c->expr);
    free(c->buf);
    free(c);
}

size_t cv_calc_names(const cv_frd* f, char* out, size_t cap) {
    size_t o = 0;
    if (cap) out[0] = 0;
#define PUT(...) do { if (o < cap) { int w_ = snprintf(out + o, cap - o, __VA_ARGS__); if (w_ > 0) o += (size_t)w_; } } while (0)
    for (int s = 0; s < f->n_steps; s++)
        for (int i = 0; i < f->steps[s].nfields; i++) {
            const cv_field_desc* d = &f->steps[s].fields[i];
            if (find_desc(f, d->name, strlen(d->name)) != d) continue;    /* listed once */
            PUT("%s%s:", o ? "\n" : "", d->name);
            for (int k = 0; k < d->ncomp; k++) PUT(" %s", d->comp[k]);
            if (d->ncomp >= 2 && d->ncomp <= 3) PUT(" MAG");
            if (cv_tensor_order(d)) PUT(" MISES P1 P2 P3");
        }
    PUT("%sX Y Z TIME", o ? "\n" : "");
#undef PUT
    return CV_MIN(o, cap ? cap - 1 : 0);
}
