//
// fatfs_host.h
//
// Fault injection for the FatFs seam in fatfs_host.cpp. The host filesystem will
// not run out of room on demand, so a full card is simulated instead.
//
#ifndef _harness_fatfs_host_h
#define _harness_fatfs_host_h

#include <stddef.h>

// Writes past nBytes return FR_OK with a short count, which is how FatFs reports
// a full volume: not an error return, so FRESULT-only callers miss it.
void FatFsHostSetWriteLimit(size_t nBytes);

// Make every f_sync() report FR_DISK_ERR.
void FatFsHostFailSync(bool bFail);

// Back to a healthy card; the state is process-wide, so injectors must reset it.
void FatFsHostClearFaults(void);

// Fast-seek link-map requests; process-wide, so reset before each load.
void FatFsHostResetLinkmapCount(void);
unsigned FatFsHostLinkmapCount(void);

#endif
