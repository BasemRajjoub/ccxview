/* video.h -- MP4 (H.264) writer for the animation export, on two vendored
   public-domain headers: minih264e (encoder) and minimp4 (muxer). Frames come
   in as RGBA rows; the size is trimmed to the multiple of 16 the encoder
   wants. Headless. */
#ifndef CV_VIDEO_H
#define CV_VIDEO_H

#include "base.h"

typedef struct cv_video cv_video;

/* fps: frames per second; quality 0..10 (0 best, 10 fastest). NULL on failure. */
cv_video* cv_video_open(const char* path, int width, int height, int fps, int quality);
int       cv_video_width(const cv_video* v);      /* the trimmed size actually encoded */
int       cv_video_height(const cv_video* v);
/* one frame of width x height RGBA pixels (as given to open); bottom_up for
   GL readbacks. false on an encoder error. */
bool      cv_video_frame(cv_video* v, const uint8_t* rgba, int width, int height, bool bottom_up);
bool      cv_video_close(cv_video* v);           /* finishes the file; frees v */

/* how many video frames an .mp4 holds (its first video track), -1 if unreadable */
int       cv_video_count_frames(const char* path);

#endif
