#include <fatfs/ff.h>

#if FF_MULTI_PARTITION

// Define the volume to partition mapping table
// Format: {physical_drive, partition} where:
// - physical_drive: 0=SD/eMMC, 1=USB1, 2=USB2, etc.
// - partition: 0=auto-detect, 1=first partition, 2=second partition, etc.

PARTITION VolToPart[FF_VOLUMES] = {
    {0, 1},    // Volume 0: SD card, partition 1 (first partition)
    {0, 2}     // Volume 1: SD card, partition 2 (second partition)
};

#endif