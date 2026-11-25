/* gif.c -- see gif.h. Each frame carries its own palette (a Local Color Table): the
   frame's distinct colours when there are at most 256, else a median cut of a 15-bit
   histogram whose boxes take the true mean of the 8-bit values they hold. The LZW
   codes start at (minimum code size + 1) bits and grow to 12; a full table is
   answered by a clear code. */
#include "gif.h"

struct cv_gif {
    FILE*    f;
    int      w, h, delay;
    bool     dither, bad;
    int      frames;
    uint8_t* idx;                 /* w * h palette indices of the frame being written */
    uint8_t  pal[256 * 3];
    int      npal;
};

enum { HSIZE = 8192 };

static void put16(FILE* f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

/* ---- colour ------------------------------------------------------------------------ */

static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

typedef struct { uint64_t n, r, g, b; } bin;
static int g_axis;
static int cmp_bin(const void* a, const void* b) {
    int x = *(const int*)a, y = *(const int*)b;
    int kx = (x >> (g_axis * 5)) & 31, ky = (y >> (g_axis * 5)) & 31;   /* axis 0 = b, 1 = g, 2 = r */
    return kx != ky ? kx - ky : x - y;
}

/* median cut of the histogram into at most 256 boxes -> g->pal; lut[15-bit] = nearest index */
static bool median_cut(cv_gif* g, const bin* h, uint8_t* lut) {
    int* occ = malloc(32768 * sizeof *occ);
    int  lo[256], hi[256], nb = 1, no = 0;
    if (!occ) return false;
    for (int i = 0; i < 32768; i++) if (h[i].n) occ[no++] = i;
    lo[0] = 0; hi[0] = no;
    while (nb < 256) {
        int best = -1; uint64_t bn = 0;
        for (int i = 0; i < nb; i++) {
            if (hi[i] - lo[i] < 2) continue;
            uint64_t n = 0;
            for (int j = lo[i]; j < hi[i]; j++) n += h[occ[j]].n;
            if (n > bn) { bn = n; best = i; }
        }
        if (best < 0) break;
        int mn[3] = { 31, 31, 31 }, mx[3] = { 0, 0, 0 };
        for (int j = lo[best]; j < hi[best]; j++)
            for (int a = 0; a < 3; a++) {
                int v = (occ[j] >> (a * 5)) & 31;
                if (v < mn[a]) mn[a] = v;
                if (v > mx[a]) mx[a] = v;
            }
        int ax = 0, rg = mx[0] - mn[0];
        for (int a = 1; a < 3; a++) if (mx[a] - mn[a] > rg) { rg = mx[a] - mn[a]; ax = a; }
        g_axis = ax;
        qsort(occ + lo[best], (size_t)(hi[best] - lo[best]), sizeof *occ, cmp_bin);
        uint64_t half = bn / 2, acc = 0;
        int cut = lo[best];
        while (cut < hi[best] - 1) { acc += h[occ[cut]].n; cut++; if (acc >= half) break; }
        lo[nb] = cut; hi[nb] = hi[best]; hi[best] = cut; nb++;
    }
    for (int i = 0; i < nb; i++) {
        uint64_t n = 0, r = 0, gg = 0, b = 0;
        for (int j = lo[i]; j < hi[i]; j++) { const bin* e = &h[occ[j]]; n += e->n; r += e->r; gg += e->g; b += e->b; }
        g->pal[i * 3] = (uint8_t)((r + n / 2) / n); g->pal[i * 3 + 1] = (uint8_t)((gg + n / 2) / n); g->pal[i * 3 + 2] = (uint8_t)((b + n / 2) / n);
    }
    g->npal = nb;
    free(occ);
    for (int i = 0; i < 32768; i++) {
        int r, gr, b;
        if (h[i].n) { r = (int)(h[i].r / h[i].n); gr = (int)(h[i].g / h[i].n); b = (int)(h[i].b / h[i].n); }
        else { int r5 = (i >> 10) & 31, g5 = (i >> 5) & 31, b5 = i & 31; r = r5 << 3 | r5 >> 2; gr = g5 << 3 | g5 >> 2; b = b5 << 3 | b5 >> 2; }
        int bi = 0, bd = 1 << 30;
        for (int k = 0; k < nb; k++) {
            int dr = r - g->pal[k * 3], dg = gr - g->pal[k * 3 + 1], db = b - g->pal[k * 3 + 2];
            int d = dr * dr + dg * dg + db * db;
            if (d < bd) { bd = d; bi = k; }
        }
        lut[i] = (uint8_t)bi;
    }
    return true;
}

static unsigned hash32(uint32_t k) { return (k * 2654435761u) >> 22; }      /* 10 bits */

static const uint8_t* row_of(const cv_gif* g, const uint8_t* rgba, int y, bool bottom_up) {
    return rgba + (size_t)(bottom_up ? g->h - 1 - y : y) * g->w * 4;
}

/* picture -> g->idx and g->pal / g->npal */
static bool quantise(cv_gif* g, const uint8_t* rgba, bool bottom_up) {
    uint32_t keys[1024]; uint16_t vals[1024];
    memset(keys, 0xff, sizeof keys);
    int nd = 0;
    bool exact = true;
    for (int y = 0; y < g->h && exact; y++) {
        const uint8_t* s = row_of(g, rgba, y, bottom_up);
        for (int x = 0; x < g->w; x++, s += 4) {
            uint32_t k = (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
            unsigned h = hash32(k);
            while (keys[h] != 0xffffffffu && keys[h] != k) h = (h + 1) & 1023;
            if (keys[h] == k) continue;
            if (nd == 256) { exact = false; break; }
            keys[h] = k; vals[h] = (uint16_t)nd;
            g->pal[nd * 3] = s[0]; g->pal[nd * 3 + 1] = s[1]; g->pal[nd * 3 + 2] = s[2];
            nd++;
        }
    }
    if (exact) {
        g->npal = nd;
        for (int y = 0; y < g->h; y++) {
            const uint8_t* s = row_of(g, rgba, y, bottom_up);
            uint8_t* d = g->idx + (size_t)y * g->w;
            for (int x = 0; x < g->w; x++, s += 4) {
                uint32_t k = (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
                unsigned h = hash32(k);
                while (keys[h] != k) h = (h + 1) & 1023;
                d[x] = (uint8_t)vals[h];
            }
        }
        return true;
    }
    bin* hist = calloc(32768, sizeof *hist);
    uint8_t* lut = malloc(32768);
    bool ok = hist && lut;
    if (ok) {
        for (int y = 0; y < g->h; y++) {
            const uint8_t* s = row_of(g, rgba, y, bottom_up);
            for (int x = 0; x < g->w; x++, s += 4) {
                bin* e = &hist[(s[0] >> 3) << 10 | (s[1] >> 3) << 5 | (s[2] >> 3)];
                e->n++; e->r += s[0]; e->g += s[1]; e->b += s[2];
            }
        }
        ok = median_cut(g, hist, lut);
    }
    if (ok)
        for (int y = 0; y < g->h; y++) {
            const uint8_t* s = row_of(g, rgba, y, bottom_up);
            uint8_t* d = g->idx + (size_t)y * g->w;
            for (int x = 0; x < g->w; x++, s += 4) {
                int r = s[0], gr = s[1], b = s[2];
                if (g->dither) {
                    int t = (bayer[y & 3][x & 3] - 8) / 2;           /* about +-4: half a 5-bit step */
                    r += t; gr += t; b += t;
                    r = r < 0 ? 0 : r > 255 ? 255 : r; gr = gr < 0 ? 0 : gr > 255 ? 255 : gr; b = b < 0 ? 0 : b > 255 ? 255 : b;
                }
                d[x] = lut[(r >> 3) << 10 | (gr >> 3) << 5 | (b >> 3)];
            }
        }
    free(hist); free(lut);
    return ok;
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

static bool lzw(FILE* f, const uint8_t* px, size_t n, int mcs) {
    const int CLEAR = 1 << mcs, EOI = CLEAR + 1;
    int32_t* key = malloc(HSIZE * sizeof *key);
    uint16_t* val = malloc(HSIZE * sizeof *val);
    if (!key || !val) { free(key); free(val); return false; }
    bitw b = { .f = f };
    fputc(mcs, f);
    memset(key, 0xff, HSIZE * sizeof *key);
    int next = CLEAR + 2, size = mcs + 1;
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
            next = CLEAR + 2; size = mcs + 1;
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
    fputc(0x70, f); fputc(0, f); fputc(0, f);             /* no global table: every frame has its own */
    fwrite("\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00", 1, 19, f);   /* loop for ever */
    return g;
}

void cv_gif_dither(cv_gif* g, bool on) { if (g) g->dither = on; }

bool cv_gif_frame(cv_gif* g, const uint8_t* rgba, int width, int height, bool bottom_up) {
    if (!g || g->bad || !rgba || width != g->w || height != g->h) return false;
    if (!quantise(g, rgba, bottom_up)) { g->bad = true; return false; }
    int bits = 1;
    while ((1 << bits) < g->npal) bits++;                 /* table of 2^bits entries */
    FILE* f = g->f;
    fwrite("\x21\xf9\x04\x04", 1, 4, f);                  /* graphic control: leave in place, no transparency */
    put16(f, g->delay); fputc(0, f); fputc(0, f);
    fputc(0x2c, f); put16(f, 0); put16(f, 0); put16(f, g->w); put16(f, g->h); fputc(0x80 | (bits - 1), f);
    fwrite(g->pal, 1, (size_t)g->npal * 3, f);
    for (int i = g->npal * 3; i < 3 << bits; i++) fputc(0, f);
    if (!lzw(f, g->idx, (size_t)g->w * g->h, bits < 2 ? 2 : bits) || ferror(f)) { g->bad = true; return false; }
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
