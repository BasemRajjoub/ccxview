/* glibc_old.h -- force-included (make PORTABLE=1) so a Linux binary built on a
   recent glibc still runs on older ones. A binary carries the newest symbol
   version its build host offers; these directives ask for the old versions,
   which every glibc since 2.2.5 (2002) has kept. The stat family had no
   exported symbol before 2.33 (it was __xstat), so those go through the old
   entry points. What remains is __libc_start_main@2.34 from the start-up
   code, so the floor lands at glibc 2.34 (2021: Ubuntu 22.04, Debian 12,
   RHEL 9). Building on an old glibc (scripts/build-portable.sh) goes lower. */
#ifndef CV_GLIBC_OLD_H
#define CV_GLIBC_OLD_H
#if defined(__linux__) && defined(__x86_64__)
#include <features.h>
#endif
#if defined(__linux__) && defined(__x86_64__) && defined(__GLIBC__)

#include <sys/stat.h>
#include <dlfcn.h>
#include <pthread.h>
#include <math.h>

__asm__(".symver fmod,fmod@GLIBC_2.2.5");
__asm__(".symver pow,pow@GLIBC_2.2.5");
__asm__(".symver powf,powf@GLIBC_2.2.5");
__asm__(".symver exp,exp@GLIBC_2.2.5");
__asm__(".symver log,log@GLIBC_2.2.5");
__asm__(".symver dlopen,dlopen@GLIBC_2.2.5");
__asm__(".symver dlsym,dlsym@GLIBC_2.2.5");
__asm__(".symver dlclose,dlclose@GLIBC_2.2.5");
__asm__(".symver dlerror,dlerror@GLIBC_2.2.5");
__asm__(".symver pthread_create,pthread_create@GLIBC_2.2.5");
__asm__(".symver pthread_join,pthread_join@GLIBC_2.2.5");

extern int __xstat(int ver, const char* path, struct stat* buf);
extern int __fxstat(int ver, int fd, struct stat* buf);
extern int __lxstat(int ver, const char* path, struct stat* buf);
static inline int cv_old_stat(const char* p, struct stat* b)  { return __xstat(1, p, b); }
static inline int cv_old_fstat(int fd, struct stat* b)         { return __fxstat(1, fd, b); }
static inline int cv_old_lstat(const char* p, struct stat* b) { return __lxstat(1, p, b); }
#define stat(p, b)  cv_old_stat(p, b)
#define fstat(f, b) cv_old_fstat(f, b)
#define lstat(p, b) cv_old_lstat(p, b)

#endif
#endif
