/* dat.c -- integration-point blocks of a CalculiX .dat file. */
#include "dat.h"
#include "frd.h"      /* cv_parse_num */
#include <ctype.h>
#include <math.h>

typedef struct { const char* p; const char* end; uint64_t line; } dcur;

static bool dnext(dcur* c, const char** ls, const char** le) {
    if (c->p >= c->end) return false;
    const char* nl = memchr(c->p, '\n', (size_t)(c->end - c->p));
    *ls = c->p;
    *le = nl ? nl : c->end;
    c->p = nl ? nl + 1 : c->end;
    if (*le > *ls && (*le)[-1] == '\r') (*le)--;
    c->line++;
    return true;
}

static const char* find_str(const char* s, const char* e, const char* needle) {
    size_t n = strlen(needle);
    for (; s + n <= e; s++) if (memcmp(s, needle, n) == 0) return s;
    return NULL;
}

static void trim_copy(char* out, size_t n, const char* s, const char* e) {
    while (s < e && isspace((unsigned char)*s)) s++;
    while (e > s && isspace((unsigned char)e[-1])) e--;
    size_t k = (size_t)(e - s) < n - 1 ? (size_t)(e - s) : n - 1;
    memcpy(out, s, k);
    out[k] = 0;
}

/* "S T E P       3" */
static bool step_line(const char* s, const char* e, int* step) {
    const char* p = find_str(s, e, "S T E P");
    if (!p) return false;
    p += 7;
    while (p < e && *p == ' ') p++;
    int v = 0;
    while (p < e && isdigit((unsigned char)*p)) v = v * 10 + (*p++ - '0');
    *step = v;
    return true;
}

/* Integers and numbers of one data line. Returns count, or -1 if the line does
   not start with two integers (then it is not a data line). */
static int data_line(const char* s, const char* e, long long* el, long long* ip, double* v, int max) {
    const char* t[CV_DAT_MAX_COMP + 2][2];
    int n = 0;
    while (n < CV_DAT_MAX_COMP + 2) {
        while (s < e && (*s == ' ' || *s == '\t')) s++;
        if (s >= e) break;
        t[n][0] = s;
        while (s < e && *s != ' ' && *s != '\t') s++;
        t[n++][1] = s;
    }
    if (n < 2) return -1;
    for (int k = 0; k < 2; k++)
        for (const char* q = t[k][0]; q < t[k][1]; q++) if (!isdigit((unsigned char)*q)) return -1;
    *el = strtoll(t[0][0], NULL, 10);
    *ip = strtoll(t[1][0], NULL, 10);
    int m = 0;
    for (int k = 2; k < n && m < max; k++) {
        double x;
        v[m++] = cv_parse_num(t[k][0], t[k][1], &x) ? x : NAN;
    }
    return m;
}

bool cv_dat_parse(cv_dat* d, const char* data, size_t size) {
    memset(d, 0, sizeof *d);
    CV_VEC(cv_dat_block) blocks = {0};
    dcur c = { data, data + size, 0 };
    const char *ls, *le;
    int step = 0;
    char msg[120];

    while (dnext(&c, &ls, &le)) {
        if (step_line(ls, le, &step)) continue;
        const char* ip = find_str(ls, le, "integ.pnt.");
        const char* lp = ip ? find_str(ls, ip, "(") : NULL;
        if (!ip || !lp) continue;                          /* not an integration-point header */

        cv_dat_block b;
        memset(&b, 0, sizeof b);
        trim_copy(b.name, sizeof b.name, ls, lp);
        b.step = step;
        b.is_coord = find_str(b.name, b.name + strlen(b.name), "coord") != NULL;
        /* components: after "integ.pnt.," up to ")" */
        const char* rp = find_str(ip, le, ")");
        const char* q = ip + 10;
        if (q < le && *q == ',') q++;
        while (rp && q < rp && b.ncomp < CV_DAT_MAX_COMP) {
            const char* comma = find_str(q, rp, ",");
            const char* ce = comma ? comma : rp;
            trim_copy(b.comp[b.ncomp], sizeof b.comp[0], q, ce);
            if (b.comp[b.ncomp][0]) b.ncomp++;
            if (!comma) break;
            q = comma + 1;
        }
        const char* fs = find_str(ls, le, "for set");
        const char* at = find_str(ls, le, "and time");
        if (fs && at && at > fs) trim_copy(b.set, sizeof b.set, fs + 7, at);
        double t = 0;
        if (at) {
            const char* ts = at + 8;
            while (ts < le && *ts == ' ') ts++;
            const char* te = ts;
            while (te < le && *te != ' ') te++;
            if (!cv_parse_num(ts, te, &t)) t = 0;
        }
        b.time = (float)t;
        if (b.ncomp == 0) {
            snprintf(msg, sizeof msg, "%s: no components in header", b.name);
            cv_msg_add(&d->msgs, c.line, false, msg);
            b.ncomp = 1;
            snprintf(b.comp[0], sizeof b.comp[0], "value");
        }

        CV_VEC(uint32_t) el = {0};
        CV_VEC(uint16_t) ips = {0};
        CV_VEC(float) vals = {0};
        size_t bad = 0;
        bool seen_data = false;
        while (dnext(&c, &ls, &le)) {
            const char* s = ls;
            while (s < le && *s == ' ') s++;
            if (s == le) {                                  /* blank: ends the block once data began */
                if (seen_data) break;
                continue;
            }
            long long e_id, ipn;
            double v[CV_DAT_MAX_COMP];
            int m = data_line(ls, le, &e_id, &ipn, v, b.ncomp);
            if (m < 0) { c.p = ls; c.line--; break; }         /* next header */
            seen_data = true;
            if (e_id <= 0 || e_id > 0xFFFFFFFELL || ipn <= 0 || ipn > 65535) { bad++; continue; }
            if (!cv_push(el, (uint32_t)e_id) || !cv_push(ips, (uint16_t)ipn)) goto oom_block;
            for (int k = 0; k < b.ncomp; k++)
                if (!cv_push(vals, k < m ? (float)v[k] : NAN)) goto oom_block;
            if (m < b.ncomp) bad++;
        }
        if (bad) {
            snprintf(msg, sizeof msg, "%s: %zu unreadable records skipped", b.name, bad);
            cv_msg_add(&d->msgs, c.line, false, msg);
        }
        b.n = (uint32_t)el.n;
        b.elem = el.a; b.ip = ips.a; b.vals = vals.a;
        if (!cv_push(blocks, b)) { free(el.a); free(ips.a); free(vals.a); goto oom; }
        continue;
    oom_block:
        cv_free_vec(el); cv_free_vec(ips); cv_free_vec(vals);
        goto oom;
    }
    d->n = (int)blocks.n;
    d->b = blocks.a;
    return true;
oom:
    d->n = (int)blocks.n;
    d->b = blocks.a;
    cv_dat_free(d);
    cv_msg_add(&d->msgs, 0, false, "out of memory reading the .dat file");
    return false;
}

void cv_dat_free(cv_dat* d) {
    for (int i = 0; i < d->n; i++) { free(d->b[i].elem); free(d->b[i].ip); free(d->b[i].vals); }
    free(d->b);
    cv_msgs keep = d->msgs;
    memset(d, 0, sizeof *d);
    d->msgs = keep;
}
