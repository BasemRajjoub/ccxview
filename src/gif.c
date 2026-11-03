/* gif.c -- see gif.h. The palette index of (r, g, b) is r6 * 42 + g7 * 6 + b6; the
   last four entries are black. The LZW codes start at 9 bits (minimum code size
   8) and grow to 12; a full table is answered by a clear code. */
#include "gif.h"

struct cv_gif {
    FILE*    f;
    int      w, h, delay;
    bool     dither, bad;
    int      frames;
    uint8_t* idx;                 /* w * h palette indices of the frame being written */
};

enum { CLEAR = 256, EOI = 257, HSIZE = 8192 };

static void put16(FILE* f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

/* ---- colour ------------------------------------------------------------------------ */

static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

/* v 0..255 to level 0..n-1 (n-1 steps); d: -0.5..0.5 of a step added first when dithering */
static int level(int v, int n, int d16, bool dither) {
    int steps = n - 1;
    int x = v * steps * 16 + (dither ? (d16 - 8) * 255 : 0);       /* v/255*steps, in 16ths * 255 */
    int l = (x + 8 * 255) / (16 * 255);
    return l < 0 ? 0 : l > steps ? steps : l;
}

static void quantise(cv_gif* g, const uint8_t* rgba, bool bottom_up) {
    for (int y = 0; y < g->h; y++) {
        const uint8_t* s = rgba + (size_t)(bottom_up ? g->h - 1 - y : y) * g->w * 4;
        uint8_t* d = g->idx + (size_t)y * g->w;
        for (int x = 0; x < g->w; x++, s += 4) {
            int t = bayer[y & 3][x & 3];
            d[x] = (uint8_t)(level(s[0], 6, t, g->dither) * 42 + level(s[1], 7, t, g->dither) * 6 + level(s[2], 6, t, g->dither));
        }
    }
}

/* ---- LZW --------------------------------------------------------------------------- */

typedef struct {
    FILE*    f;
    uint32_t acc;
    int      nbits, blen;
    uint8_t  blk[255];
    bool     err;
} bitw;

static void blk_flush(bitw* b) {
    if (!b->blen) return;
    fputc(b->blen, b->f);
    if (fwrite(b->blk, 1, (size_t)b->blen, b->f) != (size_t)b->blen) b->err = true;
    b->blen = 0;
}

static void emit(bitw* b, int code, int size) {
    b->acc |= (uint32_t)code << b->nbits;
    b->nbits += size;
    while (b->nbits >= 8) {
        b->blk[b->blen++] = (uint8_t)(b->acc & 255);
        b->acc >>= 8; b->nbits -= 8;
        if (b->blen == 255) blk_flush(b);
    }
}

static bool lzw(FILE* f, const uint8_t* px, size_t n) {
    int32_t* key = malloc(HSIZE * sizeof *key);
    uint16_t* val = malloc(HSIZE * sizeof *val);
    if (!key || !val) { free(key); free(val); return false; }
    bitw b = { .f = f };
    fputc(8, f);                                          /* minimum code size */
    memset(key, 0xff, HSIZE * sizeof *key);
    int next = 258, size = 9;
    emit(&b, CLEAR, size);
    int prefix = n ? px[0] : EOI;
    for (size_t i = 1; i < n; i++) {
        int c = px[i];
        int32_t k = (prefix << 8) | c;
        unsigned h = ((unsigned)k * 2654435761u) >> 19;   /* 13 bits */
        while (key[h] != -1 && key[h] != k) h = (h + 1) & (HSIZE - 1);
        if (key[h] == k) { prefix = val[h]; continue; }
        emit(&b, prefix, size);
        if (next > (1 << size) - 1 && size < 12) size++;  /* the entries made so far need one more bit */
        key[h] = k; val[h] = (uint16_t)next++;
        if (next == 4096) {                               /* full: clear, start over */
            emit(&b, CLEAR, size);
            memset(key, 0xff, HSIZE * sizeof *key);
            next = 258; size = 9;
        }
        prefix = c;
    }
    if (n) emit(&b, prefix, size);
    if (next > (1 << size) - 1 && size < 12 && n) size++;
    emit(&b, EOI, size);
    if (b.nbits) emit(&b, 0, 8 - b.nbits);                /* the last bits to a byte */
    blk_flush(&b);
    fputc(0, f);                                          /* end of the image data */
    free(key); free(val);
    return !b.err;
}

/* ---- the file ---------------------------------------------------------------------- */

cv_gif* cv_gif_open(const char* path, int width, int height, int fps) {
    if (width < 1 || height < 1 || width > 65535 || height > 65535 || fps < 1) return NULL;
    cv_gif* g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->w = width; g->h = height;
    g->delay = (100 + fps / 2) / fps;
    if (g->delay < 2) g->delay = 2;                       /* browsers show 0 / 1 as 10 */
    g->idx = malloc((size_t)width * height);
    g->f = fopen(path, "wb");
    if (!g->idx || !g->f) { if (g->f) fclose(g->f); free(g->idx); free(g); return NULL; }
    FILE* f = g->f;
    fwrite("GIF89a", 1, 6, f);
    put16(f, width); put16(f, height);
    fputc(0xf7, f); fputc(0, f); fputc(0, f);             /* global table of 256, 8 bits */
    for (int r = 0; r < 6; r++) for (int gr = 0; gr < 7; gr++) for (int b = 0; b < 6; b++) {
        fputc(r * 255 / 5, f); fputc(gr * 255 / 6, f); fputc(b * 255 / 5, f);
    }
    for (int i = 0; i < 4; i++) { fputc(0, f); fputc(0, f); fputc(0, f); }
    fwrite("\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00", 1, 19, f);   /* loop for ever */
    return g;
}

void cv_gif_dither(cv_gif* g, bool on) { if (g) g->dither = on; }

bool cv_gif_frame(cv_gif* g, const uint8_t* rgba, int width, int height, bool bottom_up) {
    if (!g || g->bad || !rgba || width != g->w || height != g->h) return false;
    quantise(g, rgba, bottom_up);
    FILE* f = g->f;
    fwrite("\x21\xf9\x04\x04", 1, 4, f);                  /* graphic control: leave in place, no transparency */
    put16(f, g->delay); fputc(0, f); fputc(0, f);
    fputc(0x2c, f); put16(f, 0); put16(f, 0); put16(f, g->w); put16(f, g->h); fputc(0, f);
    if (!lzw(f, g->idx, (size_t)g->w * g->h) || ferror(f)) { g->bad = true; return false; }
    g->frames++;
    return true;
}

bool cv_gif_close(cv_gif* g) {
    if (!g) return false;
    fputc(0x3b, g->f);
    bool ok = !g->bad && !ferror(g->f);
    if (fclose(g->f) != 0) ok = false;
    free(g->idx); free(g);
    return ok;
}

/* ---- reading back the frame count -------------------------------------------------- */

static int skip_blocks(FILE* f) {
    for (;;) {
        int n = fgetc(f);
        if (n < 0) return -1;
        if (!n) return 0;
        if (fseek(f, n, SEEK_CUR) != 0) return -1;
    }
}

int cv_gif_count_frames(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t h[13];
    int frames = -1;
    if (fread(h, 1, 13, f) == 13 && !memcmp(h, "GIF8", 4) && h[5] == 'a') {
        if (h[10] & 0x80 && fseek(f, 3L << ((h[10] & 7) + 1), SEEK_CUR) != 0) goto done;
        frames = 0;
        for (;;) {
            int c = fgetc(f);
            if (c == 0x3b) break;
            if (c == 0x21) { fgetc(f); if (skip_blocks(f)) { frames = -1; break; } }
            else if (c == 0x2c) {
                uint8_t d[9];
                if (fread(d, 1, 9, f) != 9) { frames = -1; break; }
                if (d[8] & 0x80 && fseek(f, 3L << ((d[8] & 7) + 1), SEEK_CUR) != 0) { frames = -1; break; }
                fgetc(f);                                     /* minimum code size */
                if (skip_blocks(f)) { frames = -1; break; }
                frames++;
            } else { frames = -1; break; }
        }
    }
done:
    fclose(f);
    return frames;
}
