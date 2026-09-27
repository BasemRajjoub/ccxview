/* video.c -- see video.h. The two libraries' implementations live in
   video_h264.c and video_mp4.c (they share internal names); here only their
   declarations are used. */
#include "video.h"
#include "video_impl.h"

struct cv_video {
    FILE*             f;
    MP4E_mux_t*       mux;
    mp4_h26x_writer_t wr;
    H264E_persist_t*  enc;
    H264E_scratch_t*  scratch;
    void*             enc_mem;
    void*             scratch_mem;
    uint8_t*          yuv;              /* Y then U then V planes, 64-byte aligned */
    void*             yuv_mem;
    int               ys, cs;           /* luma / chroma strides: multiples of 8 for the SIMD paths */
    int               w, h, fps, quality;
    int               frames;
};

static int write_cb(int64_t offset, const void* buffer, size_t size, void* token) {
    FILE* f = token;
    if (fseek(f, (long)offset, SEEK_SET) != 0) return 1;
    return fwrite(buffer, 1, size, f) == size ? 0 : 1;
}

/* the encoder wants its memory 64-byte aligned: over-allocate and round up */
static void* aligned_alloc64(size_t n, void** raw) {
    *raw = malloc(n + 64);
    if (!*raw) return NULL;
    return (void*)(((uintptr_t)*raw + 63) & ~(uintptr_t)63);
}

cv_video* cv_video_open(const char* path, int width, int height, int fps, int quality) {
    int w = width & ~1, h = height & ~1;          /* the encoder needs even sizes */
    if (w < 16 || h < 16 || fps < 1) return NULL;
    cv_video* v = calloc(1, sizeof *v);
    if (!v) return NULL;
    v->w = w; v->h = h; v->fps = fps;
    v->quality = quality < 0 ? 0 : quality > 10 ? 10 : quality;

    H264E_create_param_t cp;
    memset(&cp, 0, sizeof cp);
    cp.width = w; cp.height = h;
    cp.gop = fps * 2;                             /* a key frame every two seconds: seekable */
    cp.vbv_size_bytes = 1500000;                  /* room for large intra frames */
    cp.const_input_flag = 1;                      /* we keep our planes; any even size is fine then */
    cp.max_long_term_reference_frames = 0;
    int sp = 0, ss = 0;
    if (H264E_sizeof(&cp, &sp, &ss) != H264E_STATUS_SUCCESS) { free(v); return NULL; }
    v->enc = aligned_alloc64((size_t)sp, &v->enc_mem);
    v->scratch = aligned_alloc64((size_t)ss, &v->scratch_mem);
    v->ys = (w + 7) & ~7; v->cs = (w / 2 + 7) & ~7;
    v->yuv = aligned_alloc64((size_t)v->ys * h + 2 * (size_t)v->cs * (h / 2), &v->yuv_mem);
    v->f = fopen(path, "wb");
    if (!v->enc || !v->scratch || !v->yuv || !v->f || H264E_init(v->enc, &cp) != H264E_STATUS_SUCCESS) {
        cv_video_close(v);
        return NULL;
    }
    v->mux = MP4E_open(0, 0, v->f, write_cb);
    if (!v->mux || mp4_h26x_write_init(&v->wr, v->mux, w, h, 0) != MP4E_STATUS_OK) { cv_video_close(v); return NULL; }
    return v;
}

int cv_video_width(const cv_video* v) { return v ? v->w : 0; }
int cv_video_height(const cv_video* v) { return v ? v->h : 0; }

/* RGB -> BT.601 YUV 4:2:0, chroma averaged over 2x2 */
static void to_yuv(cv_video* v, const uint8_t* rgba, int width, int height, bool bottom_up) {
    int w = v->w, h = v->h, ys = v->ys, cs = v->cs;
    uint8_t* Y = v->yuv; uint8_t* U = Y + (size_t)ys * h; uint8_t* V = U + (size_t)cs * (h / 2);
    for (int y = 0; y < h; y++) {
        const uint8_t* row = rgba + (size_t)(bottom_up ? height - 1 - y : y) * width * 4;
        for (int x = 0; x < w; x++) {
            int r = row[4 * x], g = row[4 * x + 1], b = row[4 * x + 2];
            Y[(size_t)y * ys + x] = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
        }
    }
    for (int y = 0; y < h; y += 2)
        for (int x = 0; x < w; x += 2) {
            int r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 2; dy++) {
                const uint8_t* row = rgba + (size_t)(bottom_up ? height - 1 - (y + dy) : y + dy) * width * 4;
                for (int dx = 0; dx < 2; dx++) { r += row[4 * (x + dx)]; g += row[4 * (x + dx) + 1]; b += row[4 * (x + dx) + 2]; }
            }
            r /= 4; g /= 4; b /= 4;
            size_t i = (size_t)(y / 2) * cs + x / 2;
            U[i] = (uint8_t)(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            V[i] = (uint8_t)(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
}

bool cv_video_frame(cv_video* v, const uint8_t* rgba, int width, int height, bool bottom_up) {
    if (!v || !rgba || width < v->w || height < v->h) return false;
    to_yuv(v, rgba, width, height, bottom_up);
    H264E_io_yuv_t in;
    in.yuv[0] = v->yuv; in.stride[0] = v->ys;
    in.yuv[1] = v->yuv + (size_t)v->ys * v->h; in.stride[1] = v->cs;
    in.yuv[2] = in.yuv[1] + (size_t)v->cs * (v->h / 2); in.stride[2] = v->cs;
    H264E_run_param_t rp;
    memset(&rp, 0, sizeof rp);
    rp.encode_speed = v->quality;
    rp.desired_frame_bytes = 1500000 / v->fps;     /* about 12 Mbit/s: crisp contour edges */
    /* the rate control starts coarse and settles over a few frames; a clip
       that opens blurred looks broken, so the opening frames may be large
       and are quantised finely */
    if (v->frames < 4) rp.desired_frame_bytes *= 4;
    rp.qp_min = 10; rp.qp_max = v->frames < 4 ? 20 : 32;
    unsigned char* data = NULL; int n = 0;
    if (H264E_encode(v->enc, v->scratch, &rp, &in, &data, &n) != H264E_STATUS_SUCCESS) return false;
    if (mp4_h26x_write_nal(&v->wr, data, n, 90000u / (unsigned)v->fps) != MP4E_STATUS_OK) return false;
    v->frames++;
    return true;
}

bool cv_video_close(cv_video* v) {
    if (!v) return false;
    bool ok = true;
    if (v->mux) {
        mp4_h26x_write_close(&v->wr);
        ok = MP4E_close(v->mux) == MP4E_STATUS_OK;
    }
    if (v->f && fclose(v->f) != 0) ok = false;
    free(v->enc_mem); free(v->scratch_mem); free(v->yuv_mem);
    free(v);
    return ok;
}

static int read_cb(int64_t offset, void* buffer, size_t size, void* token) {
    FILE* f = token;
    if (fseek(f, (long)offset, SEEK_SET) != 0) return 1;
    return fread(buffer, 1, size, f) == size ? 0 : 1;
}

int cv_video_count_frames(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    MP4D_demux_t d;
    int n = -1;
    if (size > 0 && MP4D_open(&d, read_cb, f, size)) {
        for (unsigned t = 0; t < d.track_count; t++)
            if (d.track[t].handler_type == MP4D_HANDLER_TYPE_VIDE) { n = (int)d.track[t].sample_count; break; }
        MP4D_close(&d);
    }
    fclose(f);
    return n;
}
