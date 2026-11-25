/* app_cmds_lib.c -- the feature registry's functions: lookup, get / set through the same calls the
   panels make, the pins and the recent list in G, the table's self-check. The table: app_cmds.c. */
#include <string.h>
#include <math.h>
#include "app.h"
#include "app_cmds.h"

extern const cv_cmd cv_cmds[];
extern const int cv_cmds_n;

int cv_cmd_count(void) { return cv_cmds_n; }
const cv_cmd* cv_cmd_at(int i) { return i >= 0 && i < cv_cmds_n ? &cv_cmds[i] : NULL; }
const cv_cmd* cv_cmd_find(const char* id) {
    for (int i = 0; id && i < cv_cmds_n; i++) if (!strcmp(cv_cmds[i].id, id)) return &cv_cmds[i];
    return NULL;
}

int cv_cmd_geti(const cv_cmd* c) {
    if (c->get) return c->get();
    if (!c->p) return 0;
    return c->kind == CV_CMD_BOOL ? *(bool*)c->p : *(int*)c->p;
}
float cv_cmd_getf(const cv_cmd* c) { return c->p ? *(float*)c->p : 0.f; }

void cv_cmd_seti(const cv_cmd* c, int v) {
    if (c->set) { c->set(v); return; }
    if (!c->p) return;
    if (c->kind == CV_CMD_BOOL) *(bool*)c->p = v != 0; else *(int*)c->p = v;
    if (c->act) c->act();
}
void cv_cmd_setf(const cv_cmd* c, float v) {
    if (!c->p) return;
    *(float*)c->p = v;
    if (c->act) c->act();
}

void cv_cmd_path(const cv_cmd* c, char* out, size_t n) {
    const char* sep = "\xE2\x80\xBA";
    if (c->group && c->group[0]) snprintf(out, n, "%s %s %s", c->section, sep, c->group);
    else snprintf(out, n, "%s", c->section);
}

const char* const CV_PTAB_NAMES[5] = { "Model", "Results", "View", "Render", "Export" };
int cv_cmd_tab(const cv_cmd* c) {
    for (int t = 0; t < 5; t++) if (!strcmp(c->section, CV_PTAB_NAMES[t])) return t;
    return -1;
}

void cv_cmd_run(const cv_cmd* c) {
    if (c->kind == CV_CMD_ACTION && c->act) c->act();
    else if (c->kind == CV_CMD_GOTO) ui_search_jump(c);
}

/* ---- comma separated id lists in G ------------------------------------------------------ */
static bool in_list(const char* list, const char* id) {
    size_t n = strlen(id);
    for (const char* p = list; *p;) {
        const char* e = strchr(p, ',');
        size_t k = e ? (size_t)(e - p) : strlen(p);
        if (k == n && !strncmp(p, id, n)) return true;
        p += k + (e ? 1 : 0);
    }
    return false;
}
static void drop(char* list, const char* id) {
    char out[sizeof G.toolbar_pins] = "";
    size_t o = 0, n = strlen(id);
    for (const char* p = list; *p;) {
        const char* e = strchr(p, ',');
        size_t k = e ? (size_t)(e - p) : strlen(p);
        if (!(k == n && !strncmp(p, id, n)) && k && o + k + 2 < sizeof out) { o += (size_t)snprintf(out + o, sizeof out - o, "%s%.*s", o ? "," : "", (int)k, p); }
        p += k + (e ? 1 : 0);
    }
    strcpy(list, out);
}

/* the top bar: ids in order, "-" a gap, "|" the next row; the ini holds it when it is not the default */
const char CV_TB_DEFAULT[] =
    "tb:pane0,tb:deform,tb:dscale,tb:dauto,tb:d11,-,tb:animate,tb:animmode,tb:animsec,-,tb:fit,-,tb:select,-,tb:symbols,tb:pane1,|,"
    "tb:cmap,tb:bands,tb:lock,tb:field,tb:comp,tb:sides,-,tb:view,tb:ortho,-,tb:labels,-,tb:render,tb:rmenu";
const char* cv_toolbar(void) { return G.toolbar_pins[0] ? G.toolbar_pins : CV_TB_DEFAULT; }
void cv_toolbar_set(const char* l) {
    snprintf(G.toolbar_pins, sizeof G.toolbar_pins, "%s", l && strcmp(l, CV_TB_DEFAULT) ? l : "");
    if (l && !l[0]) strcpy(G.toolbar_pins, "-");                /* all removed: not the default again */
}
bool cv_cmd_pinned(const char* id) { return in_list(cv_toolbar(), id); }
void cv_cmd_pin(const char* id, bool on) {
    bool was = in_list(cv_toolbar(), id);
    if (was == on || (!strncmp(id, "tb:", 3) ? false : !cv_cmd_find(id))) return;
    char buf[sizeof G.toolbar_pins];
    snprintf(buf, sizeof buf, "%s", cv_toolbar());
    if (!on) drop(buf, id);
    else {
        size_t l = strlen(buf);
        if (l + strlen(id) + 2 >= sizeof buf) return;
        snprintf(buf + l, sizeof buf - l, "%s%s", l ? "," : "", id);
    }
    cv_toolbar_set(buf);
}

void cv_cmd_used(const char* id) {
    if (!id || !cv_cmd_find(id)) return;
    char old[256];
    snprintf(old, sizeof old, "%s", G.cmd_recent);
    drop(old, id);
    char keep[256] = "";
    int k = 0;                                   /* the newest 8 of what was kept */
    for (const char* p = old; *p && k < 7; k++) {
        const char* e = strchr(p, ',');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        size_t o = strlen(keep);
        snprintf(keep + o, sizeof keep - o, ",%.*s", (int)n, p);
        p += n + (e ? 1 : 0);
    }
    snprintf(G.cmd_recent, sizeof G.cmd_recent, "%s%s", id, keep);
}

void cv_cmd_used_ptr(const void* p) {
    if (!p) return;
    for (int i = 0; i < cv_cmds_n; i++) if (cv_cmds[i].p == p) { cv_cmd_used(cv_cmds[i].id); return; }
}

int cv_cmd_recent(const cv_cmd** out, int max) {
    int n = 0;
    for (const char* p = G.cmd_recent; *p && n < max;) {
        const char* e = strchr(p, ',');
        size_t k = e ? (size_t)(e - p) : strlen(p);
        char id[64];
        snprintf(id, sizeof id, "%.*s", (int)CV_MIN(k, sizeof id - 1), p);
        const cv_cmd* c = cv_cmd_find(id);
        if (c) out[n++] = c;
        p += k + (e ? 1 : 0);
    }
    return n;
}

int cv_cmds_check(FILE* f) {
    int bad = 0;
    for (int i = 0; i < cv_cmds_n; i++) {
        const cv_cmd* c = &cv_cmds[i];
        const char* id = c->id ? c->id : "(no id)";
        if (!c->id || !c->id[0] || strchr(c->id, ',') || strchr(c->id, ' ')) { fprintf(f, "entry %d: bad id\n", i); bad++; }
        for (int j = 0; j < i; j++) if (c->id && !strcmp(c->id, cv_cmds[j].id)) { fprintf(f, "%s: id used twice\n", id); bad++; }
        if (!c->label || !c->label[0]) { fprintf(f, "%s: no label\n", id); bad++; }
        if (!c->help || !c->help[0]) { fprintf(f, "%s: no help\n", id); bad++; }
        if (!c->section || !c->section[0]) { fprintf(f, "%s: no location\n", id); bad++; }
        if (!c->keywords) { fprintf(f, "%s: no keywords\n", id); bad++; }
        if ((c->kind == CV_CMD_BOOL || c->kind == CV_CMD_ENUM || c->kind == CV_CMD_INT) && !c->p && !(c->get && c->set)) { fprintf(f, "%s: no value\n", id); bad++; }
        if (c->kind == CV_CMD_FLOAT && (!c->p || !(c->lo < c->hi))) { fprintf(f, "%s: no float or range\n", id); bad++; }
        if (c->kind == CV_CMD_INT && !(c->lo < c->hi)) { fprintf(f, "%s: no range\n", id); bad++; }
        if (c->kind == CV_CMD_ENUM && (!c->items || c->n < 2)) { fprintf(f, "%s: no items\n", id); bad++; }
        if (c->kind == CV_CMD_ACTION && !c->act) { fprintf(f, "%s: no action\n", id); bad++; }
        if (c->kind == CV_CMD_GOTO && c->tree < 0 && cv_cmd_tab(c) < 0 && strcmp(c->section, "Settings")) { fprintf(f, "%s: goto without a section\n", id); bad++; }
    }
    return bad;
}
