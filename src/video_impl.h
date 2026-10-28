/* video_impl.h -- the vendored codec headers with the configuration every
   translation unit must share. */
#ifndef CV_VIDEO_IMPL_H
#define CV_VIDEO_IMPL_H
#define H264E_SVC_API 0
#define H264E_MAX_THREADS 0
#define MINIH264_NO_NEON   /* its NEON path fails to encode on Apple arm64 (and is untested elsewhere): plain C */
#include "minih264e.h"
#include "minimp4.h"
#endif
