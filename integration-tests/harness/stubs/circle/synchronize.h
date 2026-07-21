//
// Host-build stub for <circle/synchronize.h>.
// DMA buffers are ordinary aligned arrays on the host; barriers and
// peripheral fences are no-ops.
//
#ifndef _circle_synchronize_h
#define _circle_synchronize_h

#define CACHE_ALIGN alignas(64)
#define DMA_BUFFER(type, name, num) alignas(64) type name[num]

static inline void DataSyncBarrier(void) {}
static inline void DataMemBarrier(void) {}
static inline void InstructionSyncBarrier(void) {}
static inline void PeripheralEntry(void) {}
static inline void PeripheralExit(void) {}
static inline void EnableIRQs(void) {}
static inline void DisableIRQs(void) {}

#endif
