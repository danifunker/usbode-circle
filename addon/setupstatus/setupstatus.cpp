#include "setupstatus.h"
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/util.h>

PARTITION VolToPart[FF_VOLUMES] = {
    {0, 1},    // Volume 0: SD card, partition 1 (first partition)
    {0, 2}     // Volume 1: SD card, partition 2 (second partition)
};

static const char FromSetupStatus[] = "setupstatus";
LOGMODULE("setupstatus");

SetupStatus* SetupStatus::s_pThis = nullptr;

SetupStatus::SetupStatus(CEMMCDevice* pEMMC)
    : m_pEMMC(pEMMC),
      m_setupRequired(false),
      m_setupInProgress(false), 
      m_setupComplete(false),
      m_currentProgress(0),
      m_totalProgress(0),
      m_statusMessage("Setup starting...")
{
    
    assert(m_pEMMC != nullptr);
    
    s_pThis = this;
    LOGNOTE("SetupStatus service initialized");
    
    // Display partition table on startup
    displayPartitionTable();
    
    // Check if setup is needed - use the enhanced partition check
    if (checkPartitionExists(1)) {
        LOGNOTE("Second partition exists and is adequate size - no setup required");
        m_setupRequired = false;
    } else {
        LOGNOTE("Second partition not found or too small - setup required");
        m_setupRequired = true;
    }
}

SetupStatus::~SetupStatus() {
    s_pThis = nullptr;
}

void SetupStatus::Init(CEMMCDevice* pEMMC) {
    assert(!s_pThis && "SetupStatus::Init() must not be called more than once");
    s_pThis = new SetupStatus(pEMMC);
}

SetupStatus* SetupStatus::Get() {
    assert(s_pThis && "SetupStatus::Init() must be called first");
    return s_pThis;
}
    
void SetupStatus::displayPartitionTable() {
    LOGNOTE("Reading partition table...");
    
    uint8_t mbr[512];
    m_pEMMC->Seek(0);
    int ret = m_pEMMC->Read(mbr, 512);
    if (ret < 0) {
        LOGNOTE("Failed to read MBR %d", ret);
        return;
    }

    MBRPartitionEntry *partitions = reinterpret_cast<MBRPartitionEntry*>(mbr + 0x1BE);

    LOGNOTE("=== SD Card Partition Table ===");
    
    for (int i = 0; i < 4; ++i) {
        MBRPartitionEntry &p = partitions[i];

        if (p.type == 0) {
            LOGNOTE("Partition %d: <empty>", i+1);
            continue;
        }

        // Calculate partition size in MB
        uint64_t size_bytes = (uint64_t)p.numSectors * 512;
        uint32_t size_mb = (uint32_t)(size_bytes / (1024 * 1024));

        // Get partition type description
        const char* type_desc = "Unknown";
        switch (p.type) {
            case 0x0C: type_desc = "FAT32 LBA"; break;
            case 0x0B: type_desc = "FAT32"; break;
            case 0x06: type_desc = "FAT16"; break;
            case 0x01: type_desc = "FAT12"; break;
            case 0x07: type_desc = "NTFS/exFAT"; break;
            case 0x83: type_desc = "Linux"; break;
            case 0x82: type_desc = "Linux Swap"; break;
            default: 
                type_desc = "Other";
                break;
        }

        LOGNOTE("Partition %d:", i+1);
        LOGNOTE("  Boot: 0x%02X %s", p.boot, (p.boot & 0x80) ? "(Bootable)" : "");
        LOGNOTE("  Type: 0x%02X (%s)", p.type, type_desc);
        LOGNOTE("  Start LBA: %u", p.startLBA);
        LOGNOTE("  Num Sectors: %u (%u MB)", p.numSectors, size_mb);
        
        // Check FatFs accessibility
        if (i < 2) { // Only check partitions 1 and 2 (0: and 1: in FatFs)
            CString drive;
            drive.Format("%d:", i);
            
            LOGNOTE("  FatFs Access: %s", (const char*)drive);
            
            // Try to access via FatFs
            DWORD free_clusters;
            FATFS* fs_ptr;
            FRESULT result = f_getfree((const char*)drive, &free_clusters, &fs_ptr);
            
            if (result == FR_OK && fs_ptr != nullptr) {
                // Get filesystem type
                const char* fs_type = "Unknown";
                switch (fs_ptr->fs_type) {
                    case FS_FAT12: fs_type = "FAT12"; break;
                    case FS_FAT16: fs_type = "FAT16"; break;
                    case FS_FAT32: fs_type = "FAT32"; break;
                    case FS_EXFAT: fs_type = "exFAT"; break;
                }
                
                // Calculate filesystem sizes
                DWORD total_clusters = fs_ptr->n_fatent - 2;
                DWORD cluster_size = fs_ptr->csize; // sectors per cluster
                uint64_t total_bytes = (uint64_t)total_clusters * cluster_size * 512;
                uint64_t free_bytes = (uint64_t)free_clusters * cluster_size * 512;
                uint32_t total_mb = (uint32_t)(total_bytes / (1024 * 1024));
                uint32_t free_mb = (uint32_t)(free_bytes / (1024 * 1024));
                
                // Get volume label
                char label[12];
                CString labelStr = "<no label>";
                if (f_getlabel((const char*)drive, label, nullptr) == FR_OK && strlen(label) > 0) {
                    labelStr = label;
                }
                
                LOGNOTE("  Status: MOUNTED as %s", fs_type);
                LOGNOTE("  Label: '%s'", (const char*)labelStr);
                LOGNOTE("  Capacity: %u MB total, %u MB free", total_mb, free_mb);
                
                // Show programming access methods
                if (i == 0) {
                    LOGNOTE("  Programming Access: \"0:\" or \"SD:\" (boot/system partition)");
                } else if (i == 1) {
                    LOGNOTE("  Programming Access: \"1:\" (data/images partition)");
                }
                
            } else {
                LOGNOTE("  Status: NOT ACCESSIBLE (FatFs error %d)", result);
                LOGNOTE("  Programming Access: %s (unavailable)", (const char*)drive);
            }
        } else {
            // For partitions 3 and 4, show potential access but don't test
            LOGNOTE("  Potential FatFs Access: %d: (not configured in VolToPart)", i);
        }
        
        LOGNOTE(""); // Empty line for readability
    }
    
    LOGNOTE("Partition table analysis complete");
}

bool SetupStatus::resizeSecondPartition() {
    LOGNOTE("Resizing second partition...");
    
    // 1. Read the MBR
    uint8_t mbr[512];
    m_pEMMC->Seek(0);
    int ret = m_pEMMC->Read(mbr, 512);
    if (ret < 0) {
        LOGNOTE("Failed to read MBR %d", ret);
        return false;
    }

    // 2. Get partition entries
    MBRPartitionEntry *partitions = reinterpret_cast<MBRPartitionEntry*>(mbr + 0x1BE);
    MBRPartitionEntry &p2 = partitions[1]; // partition 2

    if (p2.type == 0) {
        LOGNOTE("Partition 2 is empty, cannot resize");
        return false;
    }

    if (p2.numSectors > 20480) { // 10MB - already resized
        LOGNOTE("Partition 2 has already been resized");
        return true;
    }

    // 3. Calculate total sectors
    uint64_t totalBlocks = m_pEMMC->GetSize() / 512;
    if (p2.startLBA >= totalBlocks) {
        LOGNOTE("Partition 2 start LBA beyond device size");
        return false;
    }

    // 4. Resize partition 2 to fill remaining space
    p2.numSectors = static_cast<uint32_t>(totalBlocks - p2.startLBA);

    LOGNOTE("Resizing Partition 2:");
    LOGNOTE("  Start LBA: %u", p2.startLBA);
    LOGNOTE("  New Num Sectors: %u", p2.numSectors);

    // 5. Write MBR back
    ret = m_pEMMC->Seek(0);
    if (ret < 0) {
        LOGNOTE("Failed to seek to MBR");
        return false;
    }

    ret = m_pEMMC->Write(mbr, 512);
    if (ret < 0) {
        LOGNOTE("Failed to write updated MBR");
        return false;
    }

    LOGNOTE("Partition 2 resized successfully");
    return true;
}

bool SetupStatus::formatPartitionAsExFAT() {
    LOGNOTE("Formatting partition 2 as exFAT...");
    
    // Define format options
    MKFS_PARM opt;
    opt.fmt = FM_EXFAT;   // filesystem type
    opt.n_fat = 1;        // number of FATs (1 is fine)
    opt.align = 0;        // allocation unit alignment (0 = default)
    opt.n_root = 0;       // root entries (0 = default)
    opt.au_size = 0;      // allocation unit size (0 = default)

    static UINT work[32 * 1024];
    FRESULT fr = f_mkfs("1:", &opt, work, sizeof(work));

    if (fr != FR_OK) {
        LOGNOTE("f_mkfs failed: %d", fr);
        return false;
    }

    LOGNOTE("Partition 2 formatted as ExFAT successfully");

    // Mount the new partition first
    FATFS fs1;
    fr = f_mount(&fs1, "1:", 1);
    if (fr != FR_OK) {
        LOGNOTE("Failed to mount partition 1 for labeling: %d", fr);
        return false;
    }

    // Set volume label after mounting
    fr = f_setlabel("1:IMGSTORE");
    if (fr != FR_OK) {
        LOGNOTE("f_setlabel failed: %d", fr);
    } else {
        LOGNOTE("Volume label set to 'IMGSTORE'");
    }

    char label_str[12];
    f_getlabel("1:", label_str, 0);
    LOGNOTE("Label of partition 1 after setlabel: %s", label_str);

    // Unmount for now
    f_mount(0, "1:", 0);
    
    return true;
}

bool SetupStatus::copyImagesDirectory() {
    LOGNOTE("Copying images directory from 0:/images to 1:/...");
    
    // Mount partition 0 for file operations
    FATFS fs0;
    FRESULT fr = f_mount(&fs0, "0:", 1);
    if (fr != FR_OK) {
        LOGNOTE("Failed to mount partition 0: %d", fr);
        return false;
    }

    // Mount partition 1 for destination
    FATFS fs1;
    fr = f_mount(&fs1, "1:", 1);
    if (fr != FR_OK) {
        LOGNOTE("Failed to mount partition 1: %d", fr);
        f_mount(0, "0:", 0);
        return false;
    }

    // Copy files from 0:/images/* to 1:/
    DIR dir;
    FILINFO fno;
    LOGNOTE("Starting file copy from 0:/images to 1:/");
    fr = f_findfirst(&dir, &fno, "0:/images", "*");
    if (fr != FR_OK) {
        LOGNOTE("f_findfirst failed: %d", fr);
        // Unmount and return - not necessarily an error if no images folder
        f_mount(0, "0:", 0);
        f_mount(0, "1:", 0);
        return true;
    }
    
    int fileCount = 0;
    while (fr == FR_OK && fno.fname[0]) {
        LOGNOTE("Found: %s (attr: 0x%02X)", fno.fname, fno.fattrib);
        
        if (!(fno.fattrib & AM_DIR)) {
            FIL src, dst;
            CString srcPath, dstPath;
            srcPath.Format("0:/images/%s", fno.fname);
            dstPath.Format("1:/%s", fno.fname);
            
            LOGNOTE("Copying %s -> %s", (const char*)srcPath, (const char*)dstPath);
            
            FRESULT srcResult = f_open(&src, srcPath, FA_READ);
            if (srcResult == FR_OK) {
                FRESULT dstResult = f_open(&dst, dstPath, FA_WRITE | FA_CREATE_ALWAYS);
                if (dstResult == FR_OK) {
                    BYTE buffer[32 * 1024];
                    UINT br, bw;
                    DWORD totalBytes = 0;
                    boolean copySuccess = TRUE;
                    
                    while (f_read(&src, buffer, sizeof(buffer), &br) == FR_OK && br > 0) {
                        if (f_write(&dst, buffer, br, &bw) != FR_OK || bw != br) {
                            LOGNOTE("Write error for %s", fno.fname);
                            copySuccess = FALSE;
                            break;
                        }
                        totalBytes += bw;
                    }
                    f_close(&dst);
                    
                    if (copySuccess) {
                        LOGNOTE("Copied: %s (%lu bytes)", fno.fname, totalBytes);
                        fileCount++;
                        
                        // Delete the source file after successful copy
			/*
                        FRESULT delResult = f_unlink(srcPath);
                        if (delResult == FR_OK) {
                            LOGNOTE("Deleted source file: %s", fno.fname);
                        } else {
                            LOGNOTE("Failed to delete source file %s: %d", fno.fname, delResult);
                        }
			*/
                    }
                } else {
                    LOGNOTE("Failed to open destination %s: %d", (const char*)dstPath, dstResult);
                }
                f_close(&src);
            } else {
                LOGNOTE("Failed to open source %s: %d", (const char*)srcPath, srcResult);
            }
        }
        fr = f_findnext(&dir, &fno);
    }
    f_closedir(&dir);

    // Unmount both partitions
    f_mount(0, "0:", 0);
    f_mount(0, "1:", 0);
    
    LOGNOTE("File copy complete. %d files copied", fileCount);
    return true;
}

bool SetupStatus::isSetupInProgress() const {
    return m_setupInProgress;
}

bool SetupStatus::isSetupComplete() const {
    return m_setupComplete;
}

bool SetupStatus::isSetupRequired() const {
    return m_setupRequired;
}

const char* SetupStatus::getStatusMessage() const {
    return m_statusMessage;
}

int SetupStatus::getCurrentProgress() const {
    return m_currentProgress;
}

int SetupStatus::getTotalProgress() const {
    return m_totalProgress;
}

bool SetupStatus::checkPartitionExists(int partition) {
    CString drive;
    drive.Format("%d:", partition);
    
    LOGNOTE("Checking if partition %d exists and is adequate size...", partition);
    
    // Don't try to mount here - just check if we can access it
    // The mounting should happen elsewhere in a controlled manner
    LOGNOTE("Attempting to check partition %d accessibility without mounting...", partition);
    
    // Try to get free space to verify partition exists and is accessible
    DWORD free_clusters;
    FATFS* fs_ptr;
    FRESULT result = f_getfree((const char*)drive, &free_clusters, &fs_ptr);
    
    if (result == FR_OK && fs_ptr != nullptr) {
        // Calculate total size using 64-bit arithmetic
        DWORD total_clusters = fs_ptr->n_fatent - 2;
        DWORD cluster_size = fs_ptr->csize; // sectors per cluster
        uint64_t total_bytes = (uint64_t)total_clusters * cluster_size * 512;
        uint32_t total_mb = (uint32_t)(total_bytes / (1024 * 1024));
        
        LOGNOTE("Partition %d exists: %u MB total", partition, total_mb);
        
        // For partition 1 (data partition), check if it's adequately sized
        if (partition == 1) {
            if (total_mb <= 10) {
                LOGNOTE("Partition %d size is too small (%u MB <= 10 MB)", partition, total_mb);
                return false;
            }
            LOGNOTE("Partition %d size is adequate (%u MB > 10 MB)", partition, total_mb);
        }
        
        return true;
    } else {
        LOGNOTE("Partition %d not accessible (error %d)", partition, result);
        
        // If we can't access it, it might not be mounted yet
        // Try to mount it once and test again
        if (partition == 1) {
            LOGNOTE("Attempting to mount partition %d...", partition);
            static FATFS fs1; // Make it static to persist
            FRESULT mountResult = f_mount(&fs1, "1:", 1);
            if (mountResult == FR_OK) {
                LOGNOTE("Successfully mounted partition %d", partition);
                
                // Try f_getfree again after mounting
                result = f_getfree((const char*)drive, &free_clusters, &fs_ptr);
                if (result == FR_OK && fs_ptr != nullptr) {
                    DWORD total_clusters = fs_ptr->n_fatent - 2;
                    DWORD cluster_size = fs_ptr->csize;
                    uint64_t total_bytes = (uint64_t)total_clusters * cluster_size * 512;
                    uint32_t total_mb = (uint32_t)(total_bytes / (1024 * 1024));
                    
                    LOGNOTE("Partition %d exists after mount: %u MB total", partition, total_mb);
                    
                    if (total_mb <= 10) {
                        LOGNOTE("Partition %d size is too small (%u MB <= 10 MB)", partition, total_mb);
                        return false;
                    }
                    LOGNOTE("Partition %d size is adequate (%u MB > 10 MB)", partition, total_mb);
                    return true;
                } else {
                    LOGNOTE("Partition %d still not accessible after mount (error %d)", partition, result);
                    return false;
                }
            } else {
                LOGNOTE("Failed to mount partition %d: %d", partition, mountResult);
                return false;
            }
        }
        
        return false;
    }
}

bool SetupStatus::performSetup() {
    LOGNOTE("Starting setup process...");
    m_setupInProgress = true;
    
    m_statusMessage = "Resizing second partition...";
    m_currentProgress = 1;
    if (!resizeSecondPartition()) {
        LOGERR("Failed to resize second partition");
        m_statusMessage = "Resize failed!";
        return false;
    }
    
    m_statusMessage = "Formatting partition...";
    m_currentProgress = 2;
    if (!formatPartitionAsExFAT()) {
        LOGERR("Failed to format partition as exFAT");
        m_statusMessage = "Format failed!";
        return false;
    }
    
    m_statusMessage = "Copying files...";
    m_currentProgress = 3;
    if (!copyImagesDirectory()) {
        LOGERR("Failed to copy images directory");
        m_statusMessage = "Copy failed!";
        return false;
    }
    
    // Mount both partitions for normal operation after setup
    m_statusMessage = "Mounting partitions...";
    m_currentProgress = 4;
    FATFS fs0;
    FRESULT fr0 = f_mount(&fs0, "0:", 1);
    if (fr0 != FR_OK) {
        LOGERR("Failed to mount partition 0 after setup: %d", fr0);
        m_statusMessage = "Mount failed!";
        return false;
    }
    LOGNOTE("Partition 0 (boot) mounted successfully");
    
    // Mount partition 1 (data/images partition)
    FATFS fs1;
    FRESULT fr1 = f_mount(&fs1, "1:", 1);
    if (fr1 != FR_OK) {
        LOGERR("Failed to mount partition 1 after setup: %d", fr1);
        // Unmount partition 0 since we failed
        f_mount(0, "0:", 0);
        m_statusMessage = "Mount failed!";
        return false;
    }
    LOGNOTE("Partition 1 (data/images) mounted successfully");
    
    // Verify the setup by displaying the partition table again
    LOGNOTE("Verifying setup completion...");
    displayPartitionTable();
    
    LOGNOTE("Setup completed successfully - both partitions mounted and ready");
    m_currentProgress = 5;
    m_setupComplete = true;
    
    return true;
}
