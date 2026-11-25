/* app_cmds.h -- the feature registry: one table of everything the panels, the top bar and the
   settings let a user change or do, for the search box (ui_search.c) and the pinned top bar.
   A value entry points into G (or has get / set callbacks); setting it through cv_cmd_set* has the
   same side effects as the panel's own control (the same app_* calls). */
#ifndef CV_APP_CMDS_H
#define CV_APP_CMDS_H
#include <stdio.h>
#include <stdbool.h>

typedef enum { CV_CMD_BOOL, CV_CMD_ENUM, CV_CMD_INT, CV_CMD_FLOAT, CV_CMD_ACTION, CV_CMD_GOTO } cv_cmd_kind;

typedef struct cv_cmd {
    const char* id;                    /* stable, kept in the ini (pins, recent): never rename */
    const char* label;                 /* as the UI shows it */
    const char* keywords;              /* synonyms a user might type */
    const char* help;                  /* the tooltip */
    cv_cmd_kind kind;
    void*       p;                     /* bool* / int* / float* into G (BOOL, ENUM, INT, FLOAT) */
    const char* const* items;          /* ENUM: the names, n of them */
    int         n;
    float       lo, hi, step;          /* INT, FLOAT */
    void      (*act)(void);            /* ACTION: what it does; a value: called after it changed */
    int       (*get)(void);            /* BOOL, ENUM, INT: instead of p */
    void      (*set)(int);             /* the same: instead of writing p (then act is not called) */
    const char* section, *group;       /* where the panel has it, as the panel names them (group may be NULL) */
    int         tree, sub;             /* CV_TREE_* of the section and of the sub-section (-1: none), for the jump; tree -1: not in the panel */
} cv_cmd;

int  cv_cmd_count(void);
const cv_cmd* cv_cmd_at(int i);
const cv_cmd* cv_cmd_find(const char* id);                 /* NULL when unknown */
int   cv_cmd_geti(const cv_cmd* c);                        /* BOOL, ENUM, INT */
float cv_cmd_getf(const cv_cmd* c);
void  cv_cmd_seti(const cv_cmd* c, int v);
void  cv_cmd_setf(const cv_cmd* c, float v);
void  cv_cmd_run(const cv_cmd* c);                         /* ACTION: do it; GOTO: jump to it (ui_search.c) */
void  cv_cmd_path(const cv_cmd* c, char* out, size_t n);   /* "View > Clip & crop" (the arrow is U+203A) */

extern const char CV_TB_DEFAULT[];                         /* the top bar's ids as shipped */
const char* cv_toolbar(void);                              /* the bar's ids in use (G.toolbar_pins, or the default when empty) */
void cv_toolbar_set(const char* ids);                      /* "": all removed; the default is stored as empty */

/* the string lists in G: pins and recent (ids, comma separated) */
bool cv_cmd_pinned(const char* id);
void cv_cmd_pin(const char* id, bool on);
void cv_cmd_used(const char* id);                          /* newest first, 8 kept */
void cv_cmd_used_ptr(const void* p);                       /* the entry whose value is *p, when there is one */
int  cv_cmd_recent(const cv_cmd** out, int max);

/* problems in the table (duplicate ids, a missing label, help or location) printed to f; their count */
int  cv_cmds_check(FILE* f);

extern const char* const CV_PTAB_NAMES[5];                /* the left panel's tabs: the section of a panel location is one of them */
int  cv_cmd_tab(const cv_cmd* c);                          /* its tab (CV_PTAB_*), -1 when it is not in a tab (Settings, Top bar, Keys) */

extern const char* const cv_faces_mode_names[];            /* the Layers list of colourings */

/* ui_search.c */
void ui_search_jump(const cv_cmd* c);                      /* open its section and flash its header */
#endif
