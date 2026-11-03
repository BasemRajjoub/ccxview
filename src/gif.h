/* gif.h -- animated GIF89a writer for the animation export, a sibling of the
   MP4 writer (video.h). Our own small encoder: a fixed 256-colour palette
   (6 x 7 x 6 levels of R, G, B), LZW, a Netscape loop block, every frame whole.
   Frames come in as RGBA rows. Headless. */
#ifndef CV_GIF_H
#define CV_GIF_H

#include "base.h"

typedef struct cv_gif cv_gif;

/* fps: frames per second (the delay is in 1/100 s, so 1..100 are exact enough). NULL on failure. */
cv_gif* cv_gif_open(const char* path, int width, int height, int fps);
void    cv_gif_dither(cv_gif* g, bool on);       /* ordered (Bayer 4x4) dithering; off by default */
/* one frame of width x height RGBA pixels (as given to open); bottom_up for
   GL readbacks. false on a size mismatch or a write error. */
bool    cv_gif_frame(cv_gif* g, const uint8_t* rgba, int width, int height, bool bottom_up);
bool    cv_gif_close(cv_gif* g);                 /* finishes the file; frees g */

/* how many frames a .gif holds (walking its blocks), -1 if unreadable */
int     cv_gif_count_frames(const char* path);

#endif
