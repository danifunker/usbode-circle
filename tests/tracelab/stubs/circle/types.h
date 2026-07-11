// Minimal stand-in for Circle's circle/types.h, used only to host-compile
// addon/tracelab sources for unit testing. Not built into the firmware.
#ifndef _test_stub_circle_types_h
#define _test_stub_circle_types_h

#include <cstdint>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int boolean;
#define TRUE 1
#define FALSE 0

#endif
