/* app_fail.h -- failure criteria as a field (field_src 3): the strength materials,
   which deck material uses which, and the evaluation over the .frd stresses. */
#ifndef CV_APP_FAIL_H
#define CV_APP_FAIL_H

#include "failure.h"
#include "cfg.h"
#include "inp.h"

/* what the failure field shows (G.fail_out) */
enum { CV_FO_EXPOSURE, CV_FO_RF, CV_FO_FI, CV_FO_MODE, CV_FO_ANGLE, CV_FO_FIBRE, CV_FO_MATRIX, CV_FO_N };
const char* fail_out_name(int o);
const char* fail_out_tip(int o);

/* the strength materials: the user's own, kept in the settings file */
int      fail_count(void);
cv_fmat* fail_mat(int i);
int      fail_find(const char* name);         /* index, -1 none */
int      fail_add(const cv_fmat* m);          /* a copy, its name made unique; index, -1 when full */
void     fail_remove(int i);
bool     fail_rename(int i, const char* name);   /* false: empty or taken; assignments follow */
void     fail_changed(void);                  /* a material or an assignment edited: shown again */

/* deck material name (or "*": every element without one) -> strength material */
const char* fail_assigned(const char* deck_mat);   /* name, NULL none */
void        fail_assign(const char* deck_mat, const char* fmat);   /* NULL or "": none */

/* A deck material with none assigned (and no "*"): the nearest template, by its
   name ("S355", "IM7/8552", "PA66", "steel"...) or else its *ELASTIC data, with
   the deck's elastic constants and *PLASTIC yield stress in place of the
   template's. how[]: "S235 (name)", "PA66 (E)". false: no match. */
bool fail_auto(const cv_inp* d, int k, cv_fmat* m, char* how, size_t n);

/* the field: fills G.scalar and G.elem_val for this step; false with why[] saying
   why not. title: "LaRC05", kind: "exposure" */
bool fail_eval_field(char* why, size_t n);
/* the governing criterion, mode and plane at a node (or element in per-element mode) */
bool fail_probe_text(uint32_t node, uint32_t elem, char* out, size_t n);
void fail_clear(void);                       /* results freed (unload) */

void fail_cfg_load(const cv_cfg* c);
void fail_cfg_save(cv_cfg* c);
bool fail_cfg_key(const char* key);          /* fmatN, fassignN */

#endif
