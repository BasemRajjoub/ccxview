/* sokol_impl.c -- the single translation unit holding the sokol + Nuklear
   implementations (compiled as Objective-C on macOS). */
#define SOKOL_IMPL
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#define NK_IMPLEMENTATION
#include "nk.h"
#include "sokol_nuklear.h"

/* Nuklear's overlay command buffer: what is drawn into it lies over every window
   and popup, and takes no input (ui.c draws the tooltips there). Begin after the
   last window of the frame has ended, since the buffer must be the frame's last. */
struct nk_command_buffer* cv_nk_overlay_begin(struct nk_context* ctx) {
    nk_command_buffer_init(&ctx->overlay, &ctx->memory, NK_CLIPPING_ON);
    nk_start_buffer(ctx, &ctx->overlay);
    nk_push_scissor(&ctx->overlay, nk_null_rect);
    return &ctx->overlay;
}
void cv_nk_overlay_end(struct nk_context* ctx) { nk_finish_buffer(ctx, &ctx->overlay); }

/* GL_PROGRAM_POINT_SIZE: lets the vertex shader size points. sokol_gfx does not
   touch it, so enabling it once after sg_setup() is enough. */
void cv_gl_enable_point_size(void) {
#ifndef __EMSCRIPTEN__
    glEnable(0x8642);                          /* always on in GLES / WebGL */
#endif
}

/* PNG deflate by miniz instead of stb's own compressor: smaller files (level 9
   with a real match finder), still one translation unit. Deflate only. */
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_INFLATE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.c"

/* stb_image_write's hook: zlib-wrapped deflate of data into a malloc'd buffer */
static unsigned char* cv_png_deflate(unsigned char* data, int len, int* out_len, int quality) {
    (void)quality;
    mz_ulong n = mz_compressBound((mz_ulong)len);
    unsigned char* out = malloc(n);
    if (!out) return NULL;
    if (mz_compress2(out, &n, data, (mz_ulong)len, MZ_BEST_COMPRESSION) != MZ_OK) { free(out); return NULL; }
    *out_len = (int)n;
    return out;
}
#define STBIW_ZLIB_COMPRESS cv_png_deflate
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(_WIN32)
/* sokol's WGL loader does not fetch glReadPixels; GL 1.1 is exported by opengl32. */
__declspec(dllimport) void __stdcall glReadPixels(int, int, int, int, unsigned, unsigned, void*);
#endif

/* Save the default framebuffer as PNG (--shot). Returns false on failure. */
bool cv_save_png(const char* path, int w, int h) {
    unsigned char* px = malloc((size_t)w * h * 4);
    if (!px) return false;
    glPixelStorei(0x0D05 /* GL_PACK_ALIGNMENT */, 1);
    glReadPixels(0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, px);
    stbi_flip_vertically_on_write(1);
    int ok = stbi_write_png(path, w, h, 4, px, w * 4);
    free(px);
    return ok != 0;
}

/* A window-space rectangle (top-left origin) as RGBA rows, bottom row first
   (GL order), alpha forced opaque. Caller frees. */
unsigned char* cv_read_pixels_region(int x, int y, int w, int h, int fb_h) {
    if (w <= 0 || h <= 0) return NULL;
    unsigned char* px = malloc((size_t)w * h * 4);
    if (!px) return NULL;
    glPixelStorei(0x0D05 /* GL_PACK_ALIGNMENT */, 1);
    glReadPixels(x, fb_h - (y + h), w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, px);
    for (size_t i = 0; i < (size_t)w * h; i++) px[4 * i + 3] = 255;
    return px;
}

/* Save a window-space rectangle (top-left origin, framebuffer pixels) as PNG.
   Alpha is forced opaque so the image looks the same in every viewer. */
/* bg: the colour the frame was cleared to with alpha 0, for a transparent picture; the
   panels' text (legend, gizmo) is drawn without alpha, so a pixel that left the clear
   colour counts as opaque. NULL: an opaque picture */
bool cv_save_png_region(const char* path, int x, int y, int w, int h, int fb_h, const float* bg) {
    if (w <= 0 || h <= 0) return false;
    unsigned char* px = malloc((size_t)w * h * 4);
    if (!px) return false;
    glPixelStorei(0x0D05 /* GL_PACK_ALIGNMENT */, 1);
    glReadPixels(x, fb_h - (y + h), w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, px);
    if (!bg) for (size_t i = 0; i < (size_t)w * h; i++) px[4 * i + 3] = 255;
    else {
        int b0 = (int)(bg[0] * 255.f + 0.5f), b1 = (int)(bg[1] * 255.f + 0.5f), b2 = (int)(bg[2] * 255.f + 0.5f);
        for (size_t i = 0; i < (size_t)w * h; i++) {
            unsigned char* q = px + 4 * i;
            if (q[3] == 0 && (abs(q[0] - b0) > 2 || abs(q[1] - b1) > 2 || abs(q[2] - b2) > 2)) q[3] = 255;
        }
    }
    stbi_flip_vertically_on_write(1);
    int ok = stbi_write_png(path, w, h, 4, px, w * 4);
    free(px);
    return ok != 0;
}

/* ccxview bakes its own font, so sokol_nuklear's atlas is never initialised;
   snk_shutdown() clears it and asserts on an uninitialised one. */
void cv_snk_before_shutdown(void) { nk_font_atlas_init_default(&_snuklear.atlas); }
