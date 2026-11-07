/* t_gif.h -- the GIF writer: header, frame count by walking the blocks, and the
   LZW data of one frame decoded again. Included by test_main.c. */
#include "../src/gif.h"

/* decode GIF image data (sub-blocks joined in d, n bytes) of minimum code size 8 into out */
static size_t gif_lzw_decode(const uint8_t* d, size_t n, uint8_t* out, size_t cap, int mcs) {
    static uint16_t pre[4096]; static uint8_t suf[4096], stack[4096];
    const int CL = 1 << mcs, FIRST = CL + 2;
    int size = mcs + 1, next = FIRST, old = -1;
    size_t o = 0, pos = 0;
    uint32_t acc = 0; int nb = 0;
    uint8_t first = 0;
    for (;;) {
        while (nb < size && pos < n) { acc |= (uint32_t)d[pos++] << nb; nb += 8; }
        if (nb < size) return o;
        int code = (int)(acc & ((1u << size) - 1));
        acc >>= size; nb -= size;
        if (code == CL) { size = mcs + 1; next = FIRST; old = -1; continue; }
        if (code == CL + 1) return o;
        int sp = 0, c = code;
        if (old < 0) { if (o < cap) out[o++] = (uint8_t)code; old = code; first = (uint8_t)code; continue; }
        if (code >= next) { stack[sp++] = first; c = old; }          /* the KwKwK case */
        while (c >= FIRST) { stack[sp++] = suf[c]; c = pre[c]; }
        first = (uint8_t)c;
        stack[sp++] = first;
        while (sp) if (o < cap) out[o++] = stack[--sp]; else return o;
        if (next < 4096) { pre[next] = (uint16_t)old; suf[next] = first; next++; if (next == (1 << size) && size < 12) size++; }
        old = code;
    }
}

/* one frame parsed back: q at the 0x21 0xf9 control block. Fills pal (RGB), returns pixels decoded */
static size_t gif_read_frame(const uint8_t** qp, uint8_t* pal, int* npal, uint8_t* dec, size_t cap) {
    const uint8_t* q = *qp + 8;                                    /* past the control block */
    q += 10 - 1;                                                   /* descriptor, up to its flags */
    int fl = *q++;
    *npal = 0;
    if (fl & 0x80) { *npal = 2 << (fl & 7); memcpy(pal, q, (size_t)*npal * 3); q += *npal * 3; }
    int mcs = *q++;
    uint8_t* joined = malloc(1 << 22); size_t jn = 0;
    while (*q) { memcpy(joined + jn, q + 1, *q); jn += *q; q += 1 + *q; }
    q++;
    size_t got = gif_lzw_decode(joined, jn, dec, cap, mcs);
    free(joined);
    *qp = q;
    return got;
}

static uint8_t* gif_slurp(const char* path, long* sz) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* b = malloc((size_t)*sz);
    if (fread(b, 1, (size_t)*sz, f) != (size_t)*sz) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static const uint8_t gif_grey[3] = { 82, 82, 82 }, gif_white[3] = { 255, 255, 255 };
static const uint8_t gif_band[12][3] = { {255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},{255,0,255},
                                         {10,20,30},{200,100,50},{30,160,90},{250,200,10},{120,60,200},{1,2,3} };
static const uint8_t* gif_want(int x, int y, int k, int H) {
    if (x < 40) return gif_band[(y * 12 / H + k) % 12];
    return ((x + y) % 11) == 0 ? gif_white : gif_grey;
}

static void test_gif(void) {
    const int W = 61, H = 47, N = 5;
    const char* path = "build/t_gif.gif";
    remove(path);
    cv_gif* g = cv_gif_open(path, W, H, 25);
    CHECK(g != NULL);
    if (!g) return;
    uint8_t* px = malloc((size_t)W * H * 4);
    /* 12 flat bands, a grey 82, white "text" pixels: must come back exactly */
    for (int k = 0; k < N; k++) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t* p = px + ((size_t)y * W + x) * 4;
                const uint8_t* c = gif_want(x, y, k, H);
                p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
            }
        CHECK(cv_gif_frame(g, px, W, H, false));
    }
    CHECK(!cv_gif_frame(g, px, W + 1, H, false));                  /* wrong size */
    CHECK(cv_gif_close(g));

    long sz = 0;
    uint8_t* b = gif_slurp(path, &sz);
    CHECK(b != NULL);
    if (b) {
        CHECK(memcmp(b, "GIF89a", 6) == 0);
        CHECK_EQ(b[6] | b[7] << 8, W); CHECK_EQ(b[8] | b[9] << 8, H);
        CHECK((b[10] & 0x80) == 0);                                /* no global table */
        CHECK(memcmp(b + 13, "\x21\xff\x0bNETSCAPE2.0", 14) == 0);
        CHECK_EQ(b[sz - 1], 0x3b);
        const uint8_t* q = b + 13 + 19;
        CHECK(q[0] == 0x21 && q[1] == 0xf9);
        CHECK_EQ(q[4] | q[5] << 8, 4);                             /* 25 fps: 4/100 s */
        CHECK_EQ(q[8], 0x2c);
        for (int k = 0; k < N; k++) {
            uint8_t pal[768], *dec = malloc((size_t)W * H);
            int np;
            size_t got = gif_read_frame(&q, pal, &np, dec, (size_t)W * H);
            CHECK_EQ(got, (size_t)W * H);
            CHECK_EQ(np, 16);                                      /* 12 + grey + white = 14 colours -> 16 */
            int bad = 0;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    const uint8_t* c = gif_want(x, y, k, H);
                    const uint8_t* e = pal + dec[y * W + x] * 3;
                    bad += e[0] != c[0] || e[1] != c[1] || e[2] != c[2];
                }
            CHECK_EQ(bad, 0);
            free(dec);
        }
        CHECK_EQ(q[0], 0x3b);
        free(b);
    }
    CHECK_EQ(cv_gif_count_frames(path), N);
    CHECK_EQ(cv_gif_count_frames("build/does-not-exist.gif"), -1);
    /* PIL reads it (skipped quietly when python3 / PIL is missing) */
    {
        char cmd[256];
        snprintf(cmd, sizeof cmd, "python3 -I -c \"from PIL import Image; im=Image.open('%s'); assert im.n_frames==%d and im.size==(%d,%d); im.seek(%d); im.convert('RGB')\" 2>/dev/null", path, N, W, H, N - 1);
        int rc = system(cmd);
        if (rc == 0) CHECK(true);
        else printf("  (gif: PIL check skipped or failed, rc %d)\n", rc);
    }

    /* noise: more than 256 colours -> median cut. Max error stated: 24 per channel on a 5-bit grid cut of 256 boxes
       (noise of 0..4095>>4 per channel, 61 x 47 px); mean error is checked too. Dithering and bottom-up both used. */
    srand(7);
    for (size_t i = 0; i < (size_t)W * H * 4; i++) px[i] = (uint8_t)(rand() >> 4);
    for (size_t i = 3; i < (size_t)W * H * 4; i += 4) px[i] = 255;
    g = cv_gif_open("build/t_gif2.gif", W, H, 100);
    CHECK(g != NULL);
    if (g) {
        CHECK(cv_gif_frame(g, px, W, H, false));
        cv_gif_dither(g, true);
        CHECK(cv_gif_frame(g, px, W, H, true));
        CHECK(cv_gif_close(g));
        CHECK_EQ(cv_gif_count_frames("build/t_gif2.gif"), 2);
        b = gif_slurp("build/t_gif2.gif", &sz);
        if (b) {
            const uint8_t* q = b + 13 + 19;
            uint8_t pal[768], *dec = malloc((size_t)W * H);
            int np;
            CHECK_EQ(gif_read_frame(&q, pal, &np, dec, (size_t)W * H), (size_t)W * H);
            CHECK_EQ(np, 256);
            int mx = 0; long sum = 0;
            for (int i = 0; i < W * H; i++)
                for (int c = 0; c < 3; c++) {
                    int e = abs(pal[dec[i] * 3 + c] - px[i * 4 + c]);
                    if (e > mx) mx = e;
                    sum += e;
                }
            CHECK(mx <= 64);                                       /* undithered, worst channel */
            CHECK(sum / (3L * W * H) <= 20);
            free(dec); free(b);
        }
    }
    /* 300 x 300 of noise: well over 4096 table entries (clears, 12-bit codes) and still decodes */
    {
        const int BW = 300, BH = 300;
        uint8_t* big = malloc((size_t)BW * BH * 4);
        for (size_t i = 0; i < (size_t)BW * BH * 4; i++) big[i] = (uint8_t)(rand() >> 4);
        cv_gif* g3 = cv_gif_open("build/t_gif3.gif", BW, BH, 10);
        CHECK(g3 && cv_gif_frame(g3, big, BW, BH, false) && cv_gif_close(g3));
        b = gif_slurp("build/t_gif3.gif", &sz);
        if (b) {
            const uint8_t* q = b + 13 + 19;
            uint8_t pal[768], *dec = malloc((size_t)BW * BH);
            int np;
            CHECK_EQ(gif_read_frame(&q, pal, &np, dec, (size_t)BW * BH), (size_t)BW * BH);
            int mx = 0;
            for (int i = 0; i < BW * BH; i++)
                for (int c = 0; c < 3; c++) { int e = abs(pal[dec[i] * 3 + c] - big[i * 4 + c]); if (e > mx) mx = e; }
            CHECK(mx <= 64);
            free(dec); free(b);
        }
        free(big);
    }
    remove("build/t_gif2.gif"); remove("build/t_gif3.gif");
    CHECK(cv_gif_open("build/t_gif4.gif", 0, 5, 10) == NULL);
    CHECK(!cv_gif_frame(NULL, NULL, 0, 0, false));
    free(px);
    remove(path);
}
