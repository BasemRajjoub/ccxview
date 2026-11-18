#include "../src/app_int.h"
#include <assert.h>
#include <stdio.h>

cv_app G;

bool app_nrec_active(void) { return false; }
void nrec_values(const float** v, size_t* n) { (void)v; (void)n; }
void gp_values(const float** v, size_t* n) { (void)v; (void)n; }
void app_label_changed(void) {}
void cv_render_aux(int which, const float* pos, const float* disp, const float* scal, uint32_t n) {
    (void)which; (void)pos; (void)disp; (void)scal; (void)n;
}

int main(void) {
    uint32_t offsets[] = {0, 2, 4}, conn[] = {0, 1, 2, 4};
    uint32_t skin_nodes[] = {2, 3, 4};
    float xyz[15] = {0};
    float scalar[] = {100, 100, 2, -3, 9}, elem[] = {2, 20};
    float top[] = {1, 3, 9, 13}, bottom[] = {-5, 2, 7, 30};
    uint8_t visible[] = {1, 1};
    G.loaded = G.has_field = G.field_elem_nodal = true;
    G.frd.n_nodes = 5; G.frd.n_elems = 2;
    G.frd.eoff = offsets; G.frd.conn = conn; G.frd.xyz = xyz;
    G.skin.pt = skin_nodes; G.skin.n_pt = 3;
    G.scalar = scalar; G.elem_val = elem;
    G.elem_top = top; G.elem_bottom = bottom; G.vis = visible;
    G.elem_nslots = 4;
    G.minmax_n = 1;

    assert(app_elem_node_value(0, 1, 1) == 3 && app_elem_node_value(0, 1, 2) == 2);
    assert(app_elem_node_value(1, 4, 1) == 13 && app_elem_node_value(1, 4, 2) == 30);
    assert(isnan(app_elem_node_value(0, 4, 1)));
    assert(app_elem_mean_value(0, 1) == 2 && app_elem_mean_value(0, 2) == -1.5f);
    top[0] = NAN;
    assert(isnan(app_elem_node_value(0, 0, 1)) && app_elem_mean_value(0, 1) == 3);
    top[0] = 1;
    G.elem_bottom = NULL;
    assert(isnan(app_elem_node_value(0, 1, 2)) && isnan(app_elem_mean_value(0, 2)));
    G.elem_bottom = bottom;

    app_refresh_range();
    assert(G.data_min == -5 && G.data_max == 30);
    assert(G.min_at == 0 && G.max_at == 1);
    assert(G.ext_n == 1 && G.ext_lo[0] == 0 && G.ext_hi[0] == 1);

    visible[1] = 0;
    app_refresh_range();
    assert(G.data_min == -5 && G.data_max == 3);
    assert(G.min_at == 0 && G.max_at == 0);
    assert(G.ext_n == 1 && G.ext_lo[0] == 0 && G.ext_hi[0] == 0);

    G.shell_side = 1;
    app_refresh_range();
    assert(G.data_min == 1 && G.data_max == 3);
    G.shell_side = 2;
    app_refresh_range();
    assert(G.data_min == -5 && G.data_max == 2);

    visible[1] = 1;
    G.field_elem_nodal = false;
    app_refresh_range();
    assert(G.data_min == -3 && G.data_max == 9);
    assert(G.min_at == 3 && G.max_at == 4);

    G.elem_mode = true;
    app_refresh_range();
    assert(G.data_min == 2 && G.data_max == 20);
    assert(G.min_at == 0 && G.max_at == 1);

    free(G.ext_lo); free(G.ext_hi);
    puts("field range: ok");
    return 0;
}
