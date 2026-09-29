/* cfg.h -- the settings file: a flat "key = value" INI kept beside the
   executable (ccxview.ini), so a copied folder takes its settings along; the
   web build keeps it in localStorage. Comments (#, ;)
   and unknown keys survive a save. Values are strings; typed getters parse
   them. Headless, never fails on bad input. */
#ifndef CV_CFG_H
#define CV_CFG_H

#include "base.h"

enum { CV_CFG_RECENT = 10 };          /* recent files kept, newest first */

typedef struct { char key[64]; char val[1024]; } cv_cfg_item;

typedef struct {
    cv_cfg_item* a;  int n;            /* every line that is key = value, in file order */
    char*  raw;                        /* the file as read (comments etc.), NULL if none */
    size_t raw_n;
    char   path[1024];                 /* where it was loaded from / will be saved */
} cv_cfg;

bool cv_cfg_default_path(char* out, size_t n);       /* <exe dir>/ccxview.ini; false if unknown */
bool cv_cfg_load(cv_cfg* c, const char* path);       /* missing file: empty config, true */
bool cv_cfg_save(const cv_cfg* c);                   /* to c->path; creates the folder */
void cv_cfg_free(cv_cfg* c);

const char* cv_cfg_get(const cv_cfg* c, const char* key, const char* dflt);
int   cv_cfg_get_int(const cv_cfg* c, const char* key, int dflt);
float cv_cfg_get_float(const cv_cfg* c, const char* key, float dflt);
bool  cv_cfg_get_bool(const cv_cfg* c, const char* key, bool dflt);   /* 1/0, true/false, yes/no, on/off */
void  cv_cfg_set(cv_cfg* c, const char* key, const char* val);       /* adds or replaces */
void  cv_cfg_set_int(cv_cfg* c, const char* key, int v);
void  cv_cfg_set_float(cv_cfg* c, const char* key, float v);
void  cv_cfg_set_bool(cv_cfg* c, const char* key, bool v);

/* recent files: keys recent0..recent9, newest first; adding an existing path moves it to the front */
void        cv_cfg_add_recent(cv_cfg* c, const char* path);
int         cv_cfg_recent(const cv_cfg* c, const char** out, int max);   /* pointers into c, count */

#endif
