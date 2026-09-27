/* web.h -- the browser build (Emscripten). Files live in an in-memory file
   system: opening means the browser's file picker or a drop writes the chosen
   files there; exporting means handing the written file to the browser as a
   download; settings are mirrored in localStorage. Native builds see only
   no-op macros. */
#ifndef CV_WEB_H
#define CV_WEB_H
#include "base.h"

#ifdef __EMSCRIPTEN__
void cv_web_init(void);                       /* before settings_load(): restore the INI, hook page unload */
void cv_web_exported(const char* path);       /* offer a file from the memory FS as a download */
void cv_web_settings_saved(const char* path); /* mirror the INI to localStorage */
void cv_web_pick_start(void);                 /* browser file picker; poll with cv_web_pick_poll */
int  cv_web_pick_poll(char* out, size_t n);   /* CV_DLG_* state, path on CV_DLG_DONE */
void cv_web_fetch_drops(void);                /* after SAPP_EVENTTYPE_FILES_DROPPED: pull the files in, open them */
#define CV_EXPORTED(path)       cv_web_exported(path)
#define CV_SETTINGS_SAVED(path) cv_web_settings_saved(path)
#else
#define CV_EXPORTED(path)       ((void)0)
#define CV_SETTINGS_SAVED(path) ((void)0)
#endif

#endif
