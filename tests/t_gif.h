/* t_gif.h -- the GIF writer: header, frame count by walking the blocks, and the
   LZW data of one frame decoded again. Included by test_main.c. */
#include "../src/gif.h"

/* decode GIF image data (sub-blocks joined in d, n bytes) of minimum code size 8 into out */
static size_t gif_lzw_decode(const uint8_t* d, size_t n, uint8_t* out, size_t cap) {
    static uint16_t pre[4096]; static uint8_t suf[4096], stack[4096];
    int size = 9, next = 258, old = -1;
    size_t o = 0, pos = 0;
    uint32_t acc = 0; int nb = 0;
    uint8_t first = 0;
    for (;;) {
        while (nb < size && pos < n) { acc |= (uint32_t)d[pos++] << nb; nb += 8; }
        if (nb < size) return o;
        int code = (int)(acc & ((1u << size) - 1));
        acc >>= size; nb -= size;
        if (code == 256) { size = 9; next = 258; old = -1; continue; }
        if (code == 257) return o;
        int sp = 0, c = code;
        if (old < 0) { if (o < cap) out[o++] = (uint8_t)code; old = code; first = (uint8_t)code; continue; }
        if (code >= next) { stack[sp++] = first; c = old; }          /* the KwKwK case */
        while (c >= 258) { stack[sp++] = suf[c]; c = pre[c]; }
        first = (uint8_t)c;
        stack[sp++] = first;
        while (sp) if (o < cap) out[o++] = stack[--sp]; else return o;
        if (next < 4096) { pre[next] = (uint16_t)old; suf[next] = first; next++; if (next == (1 << size) && size < 12) size++; }
        old = code;
    }
}

static void test_gif(void) {
    const int W = 61, H = 47, N = 5;
    const char* path = "build/t_gif.gif";
    remove(path);
    cv_gif* g = cv_gif_open(path, W, H, 25);
    CHECK(g != NULL);
    if (!g) return;
    uint8_t* px = malloc((size_t)W * H * 4);
    uint8_t* first = malloc((size_t)W * H);
    for (int k = 0; k < N; k++) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t* p = px + ((size_t)y * W + x) * 4;
                unsigned r = (unsigned)(x * 255) / W, gg = (unsigned)(y * 255) / H;
                p[0] = (uint8_t)r; p[1] = (uint8_t)gg; p[2] = (uint8_t)(k * 50 + ((x ^ y) & 7) * 9); p[3] = 255;
                if (k == 0) {           /* the palette index this must become: 6 x 7 x 6 levels */
                    first[(size_t)y * W + x] = (uint8_t)(((r * 5 + 127) / 255) * 42 + ((gg * 6 + 127) / 255) * 6 + ((p[2] * 5 + 127u) / 255));
                }
            }
        CHECK(cv_gif_frame(g, px, W, H, false));
    }
    CHECK(!cv_gif_frame(g, px, W + 1, H, false));                  /* wrong size */
    CHECK(cv_gif_close(g));

    FILE* f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t* b = malloc((size_t)sz);
        CHECK_EQ((long)fread(b, 1, (size_t)sz, f), sz);
        fclose(f);
        CHECK(memcmp(b, "GIF89a", 6) == 0);
        CHECK_EQ(b[6] | b[7] << 8, W); CHECK_EQ(b[8] | b[9] << 8, H);
        CHECK_EQ(b[10], 0xf7);
        CHECK(memcmp(b + 13 + 768, "\x21\xff\x0bNETSCAPE2.0", 14) == 0);
        CHECK_EQ(b[sz - 1], 0x3b);
        /* the first frame's image data: after the Netscape block (19) and the control block (8), the descriptor (10) */
        const uint8_t* q = b + 13 + 768 + 19;
        CHECK(q[0] == 0x21 && q[1] == 0xf9);
        CHECK_EQ(q[4] | q[5] << 8, 4);                             /* 25 fps: 4/100 s */
        q += 8;
        CHECK_EQ(q[0], 0x2c);
        q += 10;
        CHECK_EQ(q[0], 8);                                         /* minimum code size */
        q++;
        uint8_t* joined = malloc((size_t)sz);
        size_t jn = 0;
        while (*q) { memcpy(joined + jn, q + 1, *q); jn += *q; q += 1 + *q; }
        uint8_t* dec = malloc((size_t)W * H);
        size_t got = gif_lzw_decode(joined, jn, dec, (size_t)W * H);
        CHECK_EQ(got, (size_t)W * H);
        CHECK(memcmp(dec, first, (size_t)W * H) == 0);             /* the LZW round trip, and the quantiser */
        free(b); free(joined); free(dec);
    }
    CHECK_EQ(cv_gif_count_frames(path), N);
    CHECK_EQ(cv_gif_count_frames("build/does-not-exist.gif"), -1);

    /* a noisy picture fills the code table (clears, 12-bit codes) and still decodes; dithering and bottom-up */
    srand(7);
    for (size_t i = 0; i < (size_t)W * H * 4; i++) px[i] = (uint8_t)(rand() >> 4);
    for (size_t i = 3; i < (size_t)W * H * 4; i += 4) px[i] = 255;
    g = cv_gif_open("build/t_gif2.gif", W, H, 100);
    CHECK(g != NULL);
    if (g) {
        cv_gif_dither(g, true);
        CHECK(cv_gif_frame(g, px, W, H, true));
        CHECK(cv_gif_frame(g, px, W, H, false));
        CHECK(cv_gif_close(g));
        CHECK_EQ(cv_gif_count_frames("build/t_gif2.gif"), 2);
    }
    /* 300 x 300 of noise: well over 4096 table entries */
    {
        const int BW = 300, BH = 300;
        uint8_t* big = malloc((size_t)BW * BH * 4);
        for (size_t i = 0; i < (size_t)BW * BH * 4; i++) big[i] = (uint8_t)(rand() >> 4);
        cv_gif* g3 = cv_gif_open("build/t_gif3.gif", BW, BH, 10);
        CHECK(g3 && cv_gif_frame(g3, big, BW, BH, false) && cv_gif_close(g3));
        FILE* f3 = fopen("build/t_gif3.gif", "rb");
        if (f3) {
            fseek(f3, 0, SEEK_END); long sz = ftell(f3); fseek(f3, 0, SEEK_SET);
            uint8_t* b = malloc((size_t)sz);
            fread(b, 1, (size_t)sz, f3); fclose(f3);
            const uint8_t* q = b + 13 + 768 + 19 + 8 + 10 + 1;
            uint8_t* joined = malloc((size_t)sz); size_t jn = 0;
            while (*q) { memcpy(joined + jn, q + 1, *q); jn += *q; q += 1 + *q; }
            uint8_t* dec = malloc((size_t)BW * BH);
            CHECK_EQ(gif_lzw_decode(joined, jn, dec, (size_t)BW * BH), (size_t)BW * BH);
            int bad = 0;
            for (int i = 0; i < BW * BH; i++) {                   /* each pixel decodes to its own nearest palette entry */
                const uint8_t* p = big + (size_t)i * 4;
                int want = ((p[0] * 5 + 127) / 255) * 42 + ((p[1] * 6 + 127) / 255) * 6 + ((p[2] * 5 + 127) / 255);
                bad += dec[i] != want;
            }
            CHECK_EQ(bad, 0);
            free(b); free(joined); free(dec);
        }
        free(big);
    }
    remove("build/t_gif2.gif"); remove("build/t_gif3.gif");
    CHECK(cv_gif_open("build/t_gif4.gif", 0, 5, 10) == NULL);
    CHECK(!cv_gif_frame(NULL, NULL, 0, 0, false));
    free(px); free(first);
    remove(path);
}
