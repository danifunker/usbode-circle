#ifndef FATFS_PARTITION_H
#define FATFS_PARTITION_H

#include <fatfs/ff.h>

#if FF_MULTI_PARTITION

// Make sure FF_VOLUMES is at least 2 for our use case
#if FF_VOLUMES < 2
#error "FF_VOLUMES must be at least 2 to support both boot and data partitions"
#endif

// Define the volume to partition mapping table
// Format: {physical_drive, partition} where:
// - physical_drive: 0=SD/eMMC, 1=USB1, 2=USB2, etc.
// - partition: 0=auto-detect, 1=first partition, 2=second partition, etc.

static PARTITION s_VolToPart[FF_VOLUMES] = {
    {0, 1},    // Volume 0: SD card, partition 1 (first partition)
    {0, 2}     // Volume 1: SD card, partition 2 (second partition)
};

// Global pointer that FatFs will use
PARTITION VolToPart[FF_VOLUMES];

// Initialize function to copy our static array to the global one
inline void InitFatFsPartitions() {
    for (int i = 0; i < FF_VOLUMES; i++) {
        VolToPart[i] = s_VolToPart[i];
    }
}

#endif // FF_MULTI_PARTITION

#endif // FATFS_PARTITION_H