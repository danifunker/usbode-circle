//
// Host-build stub for <circle/types.h>.
// Minimal type aliases so USBODE sources compile unmodified on a PC.
//
#ifndef _circle_types_h
#define _circle_types_h

#include <stddef.h>
#include <stdint.h>

// On the device the mem/str/printf functions arrive transitively through
// Circle headers; provide them the same way here so firmware sources
// compile with both libc++ (macOS) and libstdc++ (Linux).
#include <stdio.h>
#include <string.h>

// Use the host libc's htons/htonl and tell usbcdgadget.h not to define its
// own fallback inlines. (An audit suggested dropping this so the firmware's
// own byte-swap inlines run instead, but on macOS the system htonl is a macro
// that leaks into this translation unit and collides with the firmware's
// function definition, breaking the local build. The firmware inlines are a
// trivial, obviously-correct byte swap, so using the host's here costs no
// meaningful coverage while keeping the build portable across macOS and the
// Linux CI runner.)
#include <arpa/inet.h>
#ifndef HAVE_ARPA_INET_H
#define HAVE_ARPA_INET_H 1
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef bool boolean;
#ifndef TRUE
#define TRUE true
#endif
#ifndef FALSE
#define FALSE false
#endif

#endif
