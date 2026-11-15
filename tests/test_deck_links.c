/* Exercise the actual link generation with a rebuilt visibility mask. */
#include "../src/app_deck.c"
#include <assert.h>
#include <stdio.h>

cv_app G;
static size_t segments;

float deck_stroke(void) { return 1; }
void deck_lines_inst(int which, const cv_fvec* pos, const cv_fvec* disp, float radius) {
    assert(which == CV_INST_LINK);
    assert(disp->n == 2 * pos->n);
    segments = pos->n / 6;
}

int main(void) {
    uint32_t ids[] = {1, 2, 3, 4};
    float xyz[] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
    uint32_t elem_ids[] = {1, 2}, offsets[] = {0, 2, 4}, conn[] = {0, 1, 1, 3};
    uint8_t visible[] = {1, 0, 0, 0};
    uint32_t targets[] = {1, 2};
    cv_surface surface = {.nodes = targets, .nn = 2};
    cv_link link = {.kind = CV_LINK_KINEMATIC, .ref = 3, .surf = {0, -1}};
    bool link_on = true;

    G.loaded = true;
    G.frd.n_nodes = 4;
    G.frd.node_id = ids;
    G.frd.xyz = xyz;
    G.frd.n_elems = 2;
    G.frd.elem_id = elem_ids;
    G.frd.eoff = offsets;
    G.frd.conn = conn;
    assert(cv_frd_build_maps(&G.frd, NULL, NULL));
    D.on = true;
    D.d.links = &link;
    D.d.nlinks = 1;
    D.d.surfs = &surface;
    D.d.nsurfs = 1;
    D.link_on = &link_on;

    refresh_links();
    assert(segments == 2);
    D.nvis = visible;
    mark_free_nodes(visible);                /* node 3 is in no element: shown */
    assert(visible[2] == 1 && visible[3] == 0);
    {   /* supports, loads, springs and labels at a reference node ask the same question */
        float p[3], d[6];
        assert(deck_node_pd(3, p, d));
        assert(!deck_node_pd(4, p, d));
    }
    refresh_links();
    assert(segments == 1);
    visible[0] = 0;
    refresh_links();
    assert(segments == 0);
    visible[0] = 1;
    link.ref = 4;
    refresh_links();
    assert(segments == 0);
    link.ref = 3;
    G.crop_on = true;
    G.job.crop_lo[0] = 0.5f;
    G.job.crop_hi[0] = G.job.crop_hi[1] = G.job.crop_hi[2] = 2;
    mark_free_nodes(visible);                /* node 3 at x = 0: outside the crop box */
    assert(visible[2] == 0);
    refresh_links();
    assert(segments == 0);
    G.crop_on = false;
    link_on = false;
    refresh_links();
    assert(segments == 0);

    free(G.frd.idmap.flat);
    free(G.frd.emap.flat);
    puts("deck link visibility: ok");
    return 0;
}
