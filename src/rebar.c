/* rebar.c -- reinforcement of concrete shells, the sandwich model (rebar.h). */
#include "rebar.h"
#include "shell.h"
#include <math.h>

const char* cv_rebar_comp(int c) {
    static const char* const n[CV_RB_N] = { "As_x_top", "As_x_bot", "As_y_top", "As_y_bot", "As_max", "As_total", "Conc_ratio", "Crushing" };
    return c >= 0 && c < CV_RB_N ? n[c] : "?";
}

void cv_rebar_layer(double nx, double ny, double nxy, double* fx, double* fy, double* fc) {
    double t = fabs(nxy);
    if (nx >= -t && ny >= -t) { *fx = nx + t; *fy = ny + t; *fc = 2 * t; return; }
    /* one direction compressed beyond the shear: no steel that way, the crack turns */
    if (nx < -t && ny + t * t / -nx > 0) { *fx = 0; *fy = ny + t * t / -nx; *fc = -nx + t * t / -nx; return; }
    if (ny < -t && nx + t * t / -ny > 0) { *fy = 0; *fx = nx + t * t / -ny; *fc = -ny + t * t / -ny; return; }
    /* compression both ways: the concrete alone, its larger principal compression */
    *fx = *fy = 0;
    *fc = -((nx + ny) / 2 - sqrt((nx - ny) * (nx - ny) / 4 + t * t));
}

bool cv_rebar_point(const double sf[6], double h, const cv_rebar_par* p, float out[CV_RB_N]) {
    bool ok = h > 0 && p->fcd > 0 && p->fyd > 0 && p->cover > 0 && 2 * p->cover < h;
    for (int c = 0; c < 6; c++) ok = ok && isfinite(sf[c]);
    if (!ok) { for (int c = 0; c < CV_RB_N; c++) out[c] = NAN; return false; }
    double z = h - 2 * p->cover, t = fmin(2 * p->cover, h / 2), as[4], conc = 0;
    for (int side = 0; side < 2; side++) {            /* 0 top (+e3), 1 bottom */
        double sg = side ? -1 : 1, fx, fy, fc;
        cv_rebar_layer(sf[CV_SF_NXX] / 2 + sg * sf[CV_SF_MXX] / z, sf[CV_SF_NYY] / 2 + sg * sf[CV_SF_MYY] / z,
                       sf[CV_SF_NXY] / 2 + sg * sf[CV_SF_MXY] / z, &fx, &fy, &fc);
        as[side] = fx / p->fyd;
        as[2 + side] = fy / p->fyd;
        conc = fmax(conc, fc / t / p->fcd);
    }
    double mx = 0, sum = 0;
    for (int k = 0; k < 4; k++) { out[k] = (float)as[k]; mx = fmax(mx, as[k]); sum += as[k]; }
    out[CV_RB_MAX] = (float)mx;
    out[CV_RB_SUM] = (float)sum;
    out[CV_RB_CONC] = (float)conc;
    out[CV_RB_CRUSH] = conc > 1 ? 1.f : 0.f;
    return true;
}

void cv_rebar_field(const float* sf, const float* h, size_t n, const cv_rebar_par* p, float* out) {
    for (size_t i = 0; i < n; i++) {
        double v[6];
        for (int c = 0; c < 6; c++) v[c] = sf[i * CV_SF_N + c];
        cv_rebar_point(v, h[i], p, out + i * CV_RB_N);
    }
}
