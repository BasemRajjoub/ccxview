/* gpu.h -- what renders, how busy it is, and the software fallback.

   Stats for the title bar:
     renderer name (GL_RENDERER), whether it is a software rasteriser,
     busy %: GPU time of our frames over wall time (GL timer queries, so it
             works with every driver, llvmpipe included),
     load %: the whole GPU's utilisation where the system tells it
             (NVIDIA via NVML, AMD via sysfs), -1 elsewhere.

   Fallback: when no OpenGL 4.1 context can be made (no driver, a VM, a
   remote desktop) the process restarts itself in software mode: Mesa's
   llvmpipe on Linux (LIBGL_ALWAYS_SOFTWARE), a Mesa opengl32.dll from
   <exe dir>/mesa on Windows. --software asks for it directly. */
#ifndef CV_GPU_H
#define CV_GPU_H

#include "base.h"

void cv_gpu_remember_args(int argc, char** argv);   /* before anything else in main */
void cv_gpu_software_mode(void);                    /* set up the software driver for THIS run (before the window) */
bool cv_gpu_is_software_run(void);                  /* this process was started in software mode */
/* sokol failed to create the GL context: restart in software mode. Returns
   only when that is impossible (then the caller lets sokol abort). */
void cv_gpu_fallback(void);

void cv_gpu_init(void);                    /* after sg_setup(): name, queries, load source */
void cv_gpu_frame_begin(void);             /* around the frame's GL work */
void cv_gpu_frame_end(void);

const char* cv_gpu_name(void);             /* short renderer name, "" if unknown */
bool        cv_gpu_is_software(void);      /* llvmpipe / softpipe / SwiftShader / GDI */
float       cv_gpu_busy_percent(void);     /* our frames: GPU time / wall time, -1 if no timer queries */
float       cv_gpu_load_percent(void);     /* system-wide utilisation, -1 if unknown */

#endif
