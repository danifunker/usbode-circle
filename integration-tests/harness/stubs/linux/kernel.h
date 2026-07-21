//
// Host-build stub for <linux/kernel.h>.
// The disc-image readers include this transitively but use only offsetof/
// container_of and the printf family, all provided by the host libc.
//
#ifndef _linux_kernel_h
#define _linux_kernel_h

#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))
#endif

#endif
