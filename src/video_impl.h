/* video_impl.h -- the vendored codec headers with the configuration every
   translation unit must share. */
#ifndef CV_VIDEO_IMPL_H
#define CV_VIDEO_IMPL_H
#define H264E_SVC_API 0
#define H264E_MAX_THREADS 0
#if defined(__aarch64__) || defined(__arm__)
#define MINIH264_PLAIN_C   /* its NEON path does not build or encode on Apple arm64: plain C, as fast as needed */
#endif
#define MINIMP4_TRANSCODE_SPS_ID 0   /* one stream, one SPS / PPS: nothing to renumber; the renumbering dropped every frame on macOS */
#include "minih264e.h"
#include "minimp4.h"
#endif
