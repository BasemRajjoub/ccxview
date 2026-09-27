/* t_video.h -- the MP4 writer: a small moving gradient in, a playable file with
   the right frame count out. Included by test_main.c. */
#include "../src/video.h"

static void test_video(void) {
    const int W = 70, H = 46, N = 12;               /* odd-ish size: the writer trims to even */
    const char* path = "build/t_video.mp4";
    remove(path);
    cv_video* v = cv_video_open(path, W, H, 24, 8);
    CHECK(v != NULL);
    if (!v) return;
    CHECK_EQ(cv_video_width(v), 70);
    CHECK_EQ(cv_video_height(v), 46);
    uint8_t* px = malloc((size_t)W * H * 4);
    for (int k = 0; k < N; k++) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t* p = px + ((size_t)y * W + x) * 4;
                p[0] = (uint8_t)((x * 255) / W); p[1] = (uint8_t)((y * 255) / H); p[2] = (uint8_t)(k * 20); p[3] = 255;
            }
        CHECK(cv_video_frame(v, px, W, H, k & 1));
    }
    free(px);
    CHECK(cv_video_close(v));
    FILE* f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        char head[12] = {0};
        fread(head, 1, 12, f);
        fseek(f, 0, SEEK_END);
        CHECK(ftell(f) > 500);
        CHECK(memcmp(head + 4, "ftyp", 4) == 0);      /* an MP4 box structure */
        fclose(f);
    }
    CHECK_EQ(cv_video_count_frames(path), N);
    CHECK_EQ(cv_video_count_frames("build/does-not-exist.mp4"), -1);
    /* refusals */
    CHECK(cv_video_open("build/t_video2.mp4", 8, 8, 24, 5) == NULL);
    CHECK(!cv_video_frame(NULL, NULL, 0, 0, false));
}
