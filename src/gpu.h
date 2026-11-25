/* gpu.h -- what renders, how busy it is, and the software fallback.

   Stats for the title bar: the renderer name (GL_RENDERER), whether it is a
   software rasteriser, and the utilisation only where the system itself
   measures it, so it reads as the system's own monitor does:
     Windows: this process, its busiest GPU engine (Task Manager's GPU column),
     Linux:   the whole GPU (AMD sysfs, NVIDIA NVML),
     else:    unknown. GL timer queries are no measure of this: some drivers
              (Intel on Windows) report nearly the whole frame as busy.

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

void cv_gpu_init(void);                    /* after sg_setup(): name, utilisation source */

const char* cv_gpu_name(void);             /* short renderer name, "" if unknown */
bool        cv_gpu_is_software(void);      /* llvmpipe / softpipe / SwiftShader / GDI */
/* percent, -1 if unknown; *whole_gpu: the load of the GPU from every process, not ours */
float       cv_gpu_percent(bool* whole_gpu);

#endif
