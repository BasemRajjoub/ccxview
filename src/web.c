/* web.c -- see web.h. Compiled only in the Emscripten build. */
#include "web.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "app.h"
#include "filedlg.h"
#include "cfg.h"
#include <sys/stat.h>
#include <strings.h>
#include "sokol_app.h"

EM_JS(void, web_settings_load, (const char* path), {
    try {
        var s = localStorage.getItem('ccxview.ini');
        if (!s) return;
        var p = UTF8ToString(path);
        FS.mkdirTree(p.substring(0, p.lastIndexOf('/')));
        FS.writeFile(p, s);
    } catch (e) {}
});
EM_JS(void, web_settings_store, (const char* path), {
    try { localStorage.setItem('ccxview.ini', FS.readFile(UTF8ToString(path), { encoding: 'utf8' })); } catch (e) {}
});
EM_JS(void, web_hook_unload, (void), {
    window.addEventListener('pagehide', function() { Module._cv_web_unload(); });
});
EM_JS(void, web_download, (const char* path), {
    var p = UTF8ToString(path), data;
    try { data = FS.readFile(p); } catch (e) { return; }
    var a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([data]));
    a.download = p.substring(p.lastIndexOf('/') + 1);
    a.click();
    setTimeout(function() { URL.revokeObjectURL(a.href); }, 10000);
});
/* The picker takes several files at once, so a .frd can come with its .dat /
   .inp / .sta siblings. Module.cvPick: 1 running, 2 done, 0 cancelled. */
EM_JS(void, web_pick_start, (void), {
    var inp = document.getElementById('cv_open');
    if (!inp) {
        inp = document.createElement('input');
        inp.type = 'file'; inp.multiple = true; inp.id = 'cv_open';
        inp.accept = '.frd,.inp,.dat,.fbd,.sta,.cvg';
        inp.style.display = 'none';
        document.body.appendChild(inp);
        inp.addEventListener('cancel', function() { Module.cvPick = 0; });
        inp.addEventListener('change', async function() {
            var files = Array.from(inp.files);
            inp.value = "";
            if (!files.length) { Module.cvPick = 0; return; }
            try { FS.mkdir('/open'); } catch (e) {}
            var rank = { frd: 0, inp: 1, fbd: 2, dat: 3 }, best = null, bestRank = 9;
            for (var f of files) {
                var buf = new Uint8Array(await f.arrayBuffer());
                FS.writeFile('/open/' + f.name, buf);
                var ext = f.name.split('.').pop().toLowerCase();
                if (ext in rank && rank[ext] < bestRank) { best = '/open/' + f.name; bestRank = rank[ext]; }
            }
            Module.cvPickPath = best;
            Module.cvPick = best ? 2 : 0;
        });
    }
    Module.cvPick = 1;
    inp.click();
});
EM_JS(int, web_pick_state, (void), { return Module.cvPick === undefined ? -1 : Module.cvPick; });
EM_JS(int, web_pick_path, (char* out, int n), {
    if (!Module.cvPickPath) return 0;
    stringToUTF8(Module.cvPickPath, out, n);
    Module.cvPick = -1;
    return 1;
});

void cv_web_init(void) {
    char path[1024];
    if (cv_cfg_default_path(path, sizeof path)) web_settings_load(path);
    web_hook_unload();
}

EMSCRIPTEN_KEEPALIVE void cv_web_unload(void) { settings_save(0, 0); }

void cv_web_exported(const char* path) { web_download(path); }
void cv_web_settings_saved(const char* path) { web_settings_store(path); }

void cv_web_pick_start(void) { web_pick_start(); }

int cv_web_pick_poll(char* out, size_t n) {
    int s = web_pick_state();
    if (s == 1) return CV_DLG_RUNNING;
    if (s == 2) return web_pick_path(out, (int)n) ? CV_DLG_DONE : CV_DLG_CANCELLED;
    if (s == 0) return CV_DLG_CANCELLED;
    return CV_DLG_IDLE;
}

/* Dropped files arrive one asynchronous fetch each; the main model opens once
   the last one is in, so its siblings are already there. */
static struct { int pending; char main[1024]; int main_rank; } drop;

static void drop_fetched(const sapp_html5_fetch_response* r) {
    const char* name = (const char*)r->user_data;
    if (r->succeeded) {
        char path[1024];
        snprintf(path, sizeof path, "/drop/%s", name);
        FILE* f = fopen(path, "wb");
        if (f) { fwrite(r->data.ptr, 1, r->data.size, f); fclose(f); }
        const char* ext = strrchr(name, '.');
        static const char* order[] = { ".frd", ".inp", ".fbd", ".dat" };
        for (int k = 0; ext && k < 4; k++)
            if (!strcasecmp(ext, order[k]) && k < drop.main_rank) { drop.main_rank = k; snprintf(drop.main, sizeof drop.main, "%s", path); }
    }
    free((void*)r->buffer.ptr);
    free((void*)name);
    if (--drop.pending == 0 && drop.main[0]) app_open(drop.main);
}

void cv_web_fetch_drops(void) {
    int n = sapp_get_num_dropped_files();
    if (n <= 0 || drop.pending) return;
    mkdir("/drop", 0777);
    drop.main[0] = 0; drop.main_rank = 9; drop.pending = n;
    for (int i = 0; i < n; i++) {
        uint32_t size = sapp_html5_get_dropped_file_size(i);
        void* buf = malloc(size ? size : 1);
        const char* p = sapp_get_dropped_file_path(i);
        const char* base = strrchr(p, '/');
        char* name = strdup(base ? base + 1 : p);
        if (!buf || !name) { free(buf); free(name); drop.pending--; continue; }
        sapp_html5_fetch_dropped_file(&(sapp_html5_fetch_request){
            .dropped_file_index = i, .callback = drop_fetched, .buffer = { .ptr = buf, .size = size }, .user_data = name });
    }
}
#endif
