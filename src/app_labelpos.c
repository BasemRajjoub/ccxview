/* app_labelpos.c -- labels moved by hand: a label dragged on the model to where it
   reads best keeps a pixel offset from its point, by its key (kind and id, see
   app_label.c), so it stays by its point through zoom and turn. A moved label is
   pinned: never thinned away, the others keep clear of it, a leader runs from its
   point. Kept per model in the post-processing file; a key that no label has any
   more (after a renumbering) is kept and does nothing. */
#include "app_int.h"
#include "ui.h"

enum { MOVED_MAX = 4000 };              /* the anchors hold the entry's number in 16 bits */
static struct { CV_VEC(cv_label_off) a; unsigned gen; } MV;
static struct { bool on; cv_label_off key; } DR;           /* the label being dragged */
static struct { bool on; cv_label_off key; } MK;           /* the moved label the context menu was opened on */

unsigned label_moved_gen(void) { return MV.gen; }
int label_moved_n(void) { return (int)MV.a.n; }
const cv_label_off* label_moved(int j) { return &MV.a.a[j]; }

int app_label_moved_count(void) { return (int)MV.a.n; }

bool app_label_moved_get(int i, cv_label_off* o) {
    if (i < 0 || (size_t)i >= MV.a.n) return false;
    *o = MV.a.a[i];
    return true;
}

static int find(const char* kind, const char* id) {
    for (size_t j = 0; j < MV.a.n; j++) if (!strcmp(MV.a.a[j].kind, kind) && !strcmp(MV.a.a[j].id, id)) return (int)j;
    return -1;
}

bool app_label_move(const char* kind, const char* id, float dx, float dy) {
    if (!kind[0] || !id[0] || dx != dx || dy != dy) return false;
    dx = CV_MAX(-10000.f, CV_MIN(dx, 10000.f)); dy = CV_MAX(-10000.f, CV_MIN(dy, 10000.f));
    int j = find(kind, id);
    if (j < 0) {
        if (MV.a.n >= MOVED_MAX) return false;
        cv_label_off o = {0};
        snprintf(o.kind, sizeof o.kind, "%s", kind); snprintf(o.id, sizeof o.id, "%s", id);
        if (!cv_push(MV.a, o)) return false;
        j = (int)MV.a.n - 1;
    }
    MV.a.a[j].dx = dx; MV.a.a[j].dy = dy;
    MV.gen++;
    return true;
}

bool app_label_unmove(const char* kind, const char* id) {
    int j = find(kind, id);
    if (j < 0) return false;
    memmove(MV.a.a + j, MV.a.a + j + 1, (MV.a.n - (size_t)j - 1) * sizeof *MV.a.a);
    MV.a.n--;
    MV.gen++;
    return true;
}

void app_label_moved_clear(void) {
    if (MV.a.n) MV.gen++;
    MV.a.n = 0;
    DR.on = MK.on = false;
}

/* ---- the drag: a press on a label wins over the camera -------------------------------- */

bool app_label_at(float x, float y, cv_label_off* key) { return label_hit(x, y, key); }

bool app_label_grab(float x, float y) {
    DR.on = label_hit(x, y, &DR.key);
    return DR.on;
}

void app_label_drag(float dx, float dy) {
    if (!DR.on) return;
    float s = CV_MAX(ui_scale(), 0.1f);          /* kept before the interface scale, as the label size */
    DR.key.dx += dx / s; DR.key.dy += dy / s;
    app_label_move(DR.key.kind, DR.key.id, DR.key.dx, DR.key.dy);
}

void app_label_release(void) { DR.on = false; }
bool app_label_dragging(void) { return DR.on; }

/* ---- the context menu's "Reset label" ------------------------------------------------- */

void app_label_menu_at(float x, float y) {
    MK.on = label_hit(x, y, &MK.key) && find(MK.key.kind, MK.key.id) >= 0;
}
bool app_label_menu_moved(void) { return MK.on && find(MK.key.kind, MK.key.id) >= 0; }
void app_label_menu_reset(void) {
    if (MK.on) app_label_unmove(MK.key.kind, MK.key.id);
    MK.on = false;
}
