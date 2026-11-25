/* fsearch.c -- fuzzy matching for the feature search box. */
#include "fsearch.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define LBL 128
#define KWL 256
#define HLP 512
#define MAXW 16

static void lower_copy(char* d, size_t cap, const char* s) {
    size_t i = 0;
    if (s) for (; s[i] && i + 1 < cap; i++) d[i] = (char)tolower((unsigned char)s[i]);
    d[i] = 0;
}

/* split into alnum words in place (writes NULs); returns count */
static int split_words(char* s, char** w, int maxw) {
    int n = 0;
    while (*s && n < maxw) {
        while (*s && !isalnum((unsigned char)*s)) *s++ = 0;
        if (!*s) break;
        w[n++] = s;
        while (isalnum((unsigned char)*s)) s++;
    }
    if (*s && n >= maxw) *s = 0;
    return n;
}

/* Damerau-Levenshtein distance <= 1 (a: la chars, b: lb chars) */
static int within_one(const char* a, int la, const char* b, int lb) {
    if (la - lb > 1 || lb - la > 1) return 0;
    int i = 0;
    while (i < la && i < lb && a[i] == b[i]) i++;
    if (i == la && i == lb) return 1;
    if (la == lb) {
        if (memcmp(a + i + 1, b + i + 1, (size_t)(la - i - 1)) == 0) return 1;
        if (i + 1 < la && a[i] == b[i + 1] && a[i + 1] == b[i]
            && memcmp(a + i + 2, b + i + 2, (size_t)(la - i - 2)) == 0) return 1;
        return 0;
    }
    if (la > lb) return memcmp(a + i + 1, b + i, (size_t)(la - i - 1)) == 0;
    return memcmp(a + i, b + i + 1, (size_t)(la - i)) == 0;
}

/* q is one typo from the word, or from a same-ish-length prefix of it */
static int typo_word(const char* q, int lq, const char* w) {
    int lw = (int)strlen(w);
    if (within_one(q, lq, w, lw)) return 1;
    for (int d = -1; d <= 1; d++) {
        int l = lq + d;
        if (l > 0 && l < lw && within_one(q, lq, w, l)) return 1;
    }
    return 0;
}

static int is_subseq(const char* q, const char* s) {
    for (; *s && *q; s++) if (*s == *q) q++;
    return *q == 0;
}

/* best word-level score of q against a word list: exact, prefix, typo */
static int words_score(const char* q, int lq, char** w, int nw, int ex, int pre, int ty) {
    int best = 0;
    for (int i = 0; i < nw; i++) {
        int s = 0;
        size_t lw = strlen(w[i]);
        if (lw == (size_t)lq && !strcmp(w[i], q)) s = ex;
        else if (lw > (size_t)lq && !strncmp(w[i], q, (size_t)lq)) s = pre;
        else if (lq >= 4 && typo_word(q, lq, w[i])) s = ty;
        if (s > best) best = s;
    }
    return best;
}

typedef struct {
    char lab[LBL], kw[KWL], hlp[HLP], lw_buf[LBL], kw_buf[KWL];
    char* lw[MAXW]; char* kww[MAXW * 2];
    int nlw, nkw;
    char ini[MAXW + 1];
} fs_ent;

static int score_word(const char* q, const fs_ent* e) {
    int lq = (int)strlen(q), best = 0, s;
    s = words_score(q, lq, (char**)e->lw, e->nlw, 100, 80, 50);
    if (s > best) best = s;
    if (best < 60 && strstr(e->lab, q)) best = 60;
    if (best < 40) {
        s = words_score(q, lq, (char**)e->kww, e->nkw, 45, 40, 25);
        if (s > best) best = s;
    }
    if (best < 30 && lq >= 3 && strstr(e->kw, q)) best = 30;
    if (best < 15 && strstr(e->hlp, q)) best = 15;
    if (best < 10 && lq >= 2 && lq <= 3) {
        if (is_subseq(q, e->ini)) s = 12;
        else if (is_subseq(q, e->lab)) s = 8;
        else if (is_subseq(q, e->kw)) s = 5;
        else s = 0;
        if (s > best) best = s;
    }
    return best;
}

int cv_fs_score(const char* query, const char* label, const char* keywords, const char* help) {
    char qb[LBL];
    char* qw[MAXW];
    lower_copy(qb, sizeof qb, query);
    int nq = split_words(qb, qw, MAXW);
    if (nq == 0) return 1;

    fs_ent e;
    lower_copy(e.lab, sizeof e.lab, label);
    lower_copy(e.kw, sizeof e.kw, keywords);
    lower_copy(e.hlp, sizeof e.hlp, help);
    memcpy(e.lw_buf, e.lab, sizeof e.lab);
    memcpy(e.kw_buf, e.kw, sizeof e.kw);
    e.nlw = split_words(e.lw_buf, e.lw, MAXW);
    e.nkw = split_words(e.kw_buf, e.kww, MAXW * 2);
    for (int i = 0; i < e.nlw && i < MAXW; i++) e.ini[i] = e.lw[i][0];
    e.ini[e.nlw < MAXW ? e.nlw : MAXW] = 0;

    int total = 0;
    for (int i = 0; i < nq; i++) {
        int w = score_word(qw[i], &e);
        if (!w) return 0;
        total += w;
    }
    return total;
}

int cv_fs_rank(const char* query, int n, const char* const* label, const char* const* kw,
               const char* const* help, int* order, int max) {
    if (n <= 0 || max <= 0 || !order) return 0;
    int* sc = (int*)malloc((size_t)n * sizeof *sc);
    if (!sc) return 0;
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        sc[i] = cv_fs_score(query, label[i], kw ? kw[i] : NULL, help ? help[i] : NULL);
        if (!sc[i]) continue;
        /* insertion into the sorted top list; ties keep the earlier index first */
        int p = cnt < max ? cnt : max;
        while (p > 0 && sc[order[p - 1]] < sc[i]) p--;
        if (p >= max) continue;
        int last = cnt < max ? cnt : max - 1;
        for (int k = last; k > p; k--) order[k] = order[k - 1];
        order[p] = i;
        if (cnt < max) cnt++;
    }
    free(sc);
    return cnt;
}
