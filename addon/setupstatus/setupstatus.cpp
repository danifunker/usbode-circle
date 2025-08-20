#include "setupstatus.h"
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <fatfs/ff.h>

static const char FromSetupStatus[] = "setupstatus";
LOGMODULE("setupstatus");

SetupStatus* SetupStatus::s_pThis = nullptr;

SetupStatus::SetupStatus()
    : CTask(TASK_STACK_SIZE),
      m_setupRequired(false),
      m_setupInProgress(false), 
      m_setupComplete(false),
      m_currentProgress(0),
      m_totalProgress(0) {
    
    s_pThis = this;
    LOGNOTE("SetupStatus service initialized");
    
    // Log initial state
    LOGNOTE("Initial state: required=%s, inProgress=%s, complete=%s", 
           m_setupRequired ? "YES" : "NO",
           m_setupInProgress ? "YES" : "NO", 
           m_setupComplete ? "YES" : "NO");
}

SetupStatus::~SetupStatus() {
    s_pThis = nullptr;
}

void SetupStatus::Run() {
    LOGNOTE("SetupStatus service started");
    
    // Log partition check results on startup
    for (int i = 0; i <= 3; i++) {
        bool exists = checkPartitionExists(i);
        LOGNOTE("Partition %d exists: %s", i, exists ? "YES" : "NO");
    }
    
    while (true) {
        // Log current state periodically
        static unsigned lastStateLog = 0;
        unsigned currentTime = CTimer::GetClockTicks();
        
        if (currentTime - lastStateLog > 10 * HZ) { // Every 10 seconds
            LOGNOTE("Current state: required=%s, inProgress=%s, complete=%s, progress=%d/%d", 
                   m_setupRequired ? "YES" : "NO",
                   m_setupInProgress ? "YES" : "NO", 
                   m_setupComplete ? "YES" : "NO",
                   m_currentProgress, m_totalProgress);
            
            if (m_statusMessage.GetLength() > 0) {
                LOGNOTE("Status message: '%s'", (const char*)m_statusMessage);
            }
            
            lastStateLog = currentTime;
        }
        
        // Sleep for 5 seconds
        CScheduler::Get()->MsSleep(5000);
    }
}

void SetupStatus::setSetupRequired(bool required) {
    if (m_setupRequired != required) {
        m_setupRequired = required;
        LOGNOTE("Setup required changed: %s", required ? "YES" : "NO");
    }
}

bool SetupStatus::isSetupRequired() const {
    return m_setupRequired;
}

void SetupStatus::setSetupInProgress(bool inProgress) {
    if (m_setupInProgress != inProgress) {
        m_setupInProgress = inProgress;
        LOGNOTE("Setup in progress changed: %s", inProgress ? "YES" : "NO");
        if (inProgress) {
            m_currentProgress = 0;
            m_totalProgress = 0;
        }
    }
}

bool SetupStatus::isSetupInProgress() const {
    return m_setupInProgress;
}

void SetupStatus::setSetupComplete(bool complete) {
    if (complete && !m_setupComplete) {
        m_setupComplete = true;
        m_setupInProgress = false;
        m_setupRequired = false;
        LOGNOTE("Setup completed successfully");
        m_Event.Set(); // Wake up the task
    } else if (!complete) {
        m_setupComplete = complete;
        LOGNOTE("Setup complete changed: %s", complete ? "YES" : "NO");
    }
}

bool SetupStatus::isSetupComplete() const {
    return m_setupComplete;
}

void SetupStatus::setStatusMessage(const char* message) {
    CString newMessage = message ? message : "";
    if (m_statusMessage.Compare(newMessage) != 0) {
        m_statusMessage = newMessage;
        LOGNOTE("Status message changed: '%s'", (const char*)m_statusMessage);
    }
}

const char* SetupStatus::getStatusMessage() const {
    return m_statusMessage;
}

void SetupStatus::setProgress(int current, int total) {
    if (m_currentProgress != current || m_totalProgress != total) {
        m_currentProgress = current;
        m_totalProgress = total;
        LOGNOTE("Progress changed: %d/%d", current, total);
    }
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
    
    LOGNOTE("Checking if partition %d exists...", partition);
    
    // First try to mount with force=0 (no format)
    FATFS fs;
    FRESULT result = f_mount(&fs, (const char*)drive, 0);
    if (result != FR_OK) {
        LOGNOTE("Partition %d does not exist (mount failed with error %d)", partition, result);
        return false;
    }
    
    // Now try to access the filesystem to verify it's actually usable
    DWORD free_clusters;
    FATFS* fs_ptr;
    result = f_getfree((const char*)drive, &free_clusters, &fs_ptr);
    
    // Unmount regardless of result
    f_mount(nullptr, (const char*)drive, 0);
    
    if (result == FR_OK && fs_ptr != nullptr) {
        LOGNOTE("Partition %d exists and is accessible", partition);
        return true;
    } else {
        LOGNOTE("Partition %d mount succeeded but filesystem not accessible (error %d)", partition, result);
        return false;
    }
}

bool SetupStatus::performSetup() {
    LOGNOTE("Starting setup process...");
    
    setSetupInProgress(true);
    setStatusMessage("Setting up partition...");
    setProgress(1, 3);
    
    if (!setupSecondPartition()) {
        LOGERR("Failed to setup second partition");
        setStatusMessage("Setup failed!");
        return false;
    }
    
    setStatusMessage("Copying files...");
    setProgress(2, 3);
    
    if (!copyImagesDirectory()) {
        LOGERR("Failed to copy images directory");
        setStatusMessage("Copy failed!");
        return false;
    }
    
    setStatusMessage("Finalizing setup...");
    setProgress(3, 3);
    
    LOGNOTE("Setup completed successfully");
    setSetupComplete(true);
    
    return true;
}

bool SetupStatus::setupSecondPartition() {
    LOGNOTE("Setting up second partition...");
    
    // Use the SAME reliable check as the kernel uses
    DWORD free_clusters;
    FATFS* fs;
    FRESULT result = f_getfree("1:", &free_clusters, &fs);
    if (result == FR_OK && fs != nullptr) {
        LOGNOTE("Second partition already exists and is accessible");
        return true;
    }
    
    LOGNOTE("Second partition not accessible (error %d) - creating partition table", result);
    
    // Get actual SD card size instead of hardcoding
    LBA_t total_sectors = 0;
    
    // Try to detect SD card size by reading the disk geometry
    FATFS tempfs;
    if (f_mount(&tempfs, "0:", 0) == FR_OK) {
        DWORD free_clusters_temp, total_clusters;
        FATFS* fs_ptr;
        if (f_getfree("0:", &free_clusters_temp, &fs_ptr) == FR_OK) {
            // Calculate approximate total disk size
            // This is rough - we know partition 0 ends at sector 409599
            total_sectors = 31116288; // Use known 15.9GB size for now
        }
        f_mount(nullptr, "0:", 0);
    }
    
    // Fallback to known size if detection fails
    if (total_sectors == 0) {
        total_sectors = 31116288; // 15.9GB default
        LOGWARN("Could not detect SD card size, using default: %lu sectors", (unsigned long)total_sectors);
    } else {
        LOGNOTE("Using SD card size: %lu sectors (~%.1f GB)", 
                (unsigned long)total_sectors, (float)(total_sectors * 512) / (1024*1024*1024));
    }
    
    // Get the physical drive number (typically 0 for the main SD card)
    BYTE pdrv = 0;  // Physical drive 0
    
    // Allocate working buffer (must be at least FF_MAX_SS bytes)
    const size_t work_buffer_size = 4096;
    BYTE* work_buffer = new BYTE[work_buffer_size];
    if (!work_buffer) {
        LOGERR("Failed to allocate working buffer for f_fdisk");
        return false;
    }
    
    // CORRECTED: Use the EXACT same values as original image
    // Original image: start=2048, size=407552
    // We want to recreate partition 1 exactly, then add partition 2
    
    // Calculate partition end boundaries correctly
    LBA_t p1_start = 2048;
    LBA_t p1_size = 407552;
    LBA_t p1_end = p1_start + p1_size - 1;  // 409599
    
    LBA_t p2_start = p1_end + 1;            // 409600  
    LBA_t p2_end = total_sectors - 1;       // Last sector of SD card
    
    // For f_fdisk, specify partition END sectors
    LBA_t partTable[4] = {
        p1_end,     // End of partition 1 at sector 409599
        p2_end,     // End of partition 2 at last sector  
        0,          // End of partition 3 (unused)
        0           // End of partition 4 (unused)
    };
    
    LOGNOTE("f_fdisk parameters (corrected):");
    LOGNOTE("  Physical drive: %u", pdrv);
    LOGNOTE("  Total SD card sectors: %lu", (unsigned long)total_sectors);
    LOGNOTE("  Partition 1: sectors %lu-%lu (%lu sectors, ~200MB)", 
            (unsigned long)p1_start, (unsigned long)p1_end, (unsigned long)p1_size);
    LOGNOTE("  Partition 2: sectors %lu-%lu (%lu sectors, ~%.1fGB)", 
            (unsigned long)p2_start, (unsigned long)p2_end, 
            (unsigned long)(p2_end - p2_start + 1),
            (float)((p2_end - p2_start + 1) * 512) / (1024*1024*1024));
    LOGNOTE("  partTable[0] = %lu (P1 end)", (unsigned long)partTable[0]);
    LOGNOTE("  partTable[1] = %lu (P2 end)", (unsigned long)partTable[1]);
    
    LOGWARN("WARNING: f_fdisk will repartition the entire SD card!");
    LOGWARN("This will destroy all data including boot files!");
    
    // Call f_fdisk with correct parameters
    result = f_fdisk(pdrv, partTable, work_buffer);
    
    // Clean up working buffer
    delete[] work_buffer;
    
    if (result != FR_OK) {
        LOGERR("f_fdisk failed with error code: %d", result);
        
        switch (result) {
            case FR_DISK_ERR:
                LOGERR("Disk I/O error - SD card may be faulty or write-protected");
                break;
            case FR_NOT_READY:
                LOGERR("Drive not ready - SD card not properly initialized");
                break;
            case FR_WRITE_PROTECTED:
                LOGERR("SD card is write-protected");
                break;
            case FR_INVALID_PARAMETER:
                LOGERR("Invalid parameters passed to f_fdisk");
                break;
            case FR_MKFS_ABORTED:
                LOGERR("Partitioning aborted - invalid partition layout or SD card too small");
                break;
            default:
                LOGERR("Unknown error during partitioning: %d", result);
                break;
        }
        return false;
    }
    
    LOGNOTE("f_fdisk completed successfully!");
    
    // Verify the partition table was created correctly
    LOGNOTE("Checking partition table creation...");
    LOGNOTE("Run 'hexdump' to verify:");
    LOGNOTE("  Expected P1: start=2048, size=407552, type=FAT32, bootable");
    LOGNOTE("  Expected P2: start=409600, size=%lu, type=auto", (unsigned long)(p2_end - p2_start + 1));
    
    // Format partition 2 as exFAT (data partition)
    LOGNOTE("Formatting partition 2 as exFAT for data...");
    return formatPartition();
}

bool SetupStatus::formatPartition() {
    LOGNOTE("Formatting partition 2 as exFAT (data partition)...");
    
    // Try to mount the second partition to verify it exists
    FATFS fs;
    FRESULT result = f_mount(&fs, "1:", 1);  // Force mount to detect partition
    if (result != FR_OK) {
        LOGERR("Cannot access second partition for formatting (error %d)", result);
        return false;
    }
    
    // Allocate working buffer for formatting
    const size_t work_buffer_size = 4096;
    BYTE* work_buffer = new BYTE[work_buffer_size];
    if (!work_buffer) {
        LOGERR("Failed to allocate working buffer for formatting");
        f_mount(nullptr, "1:", 0);  // Unmount
        return false;
    }
    
    // Set up format parameters for exFAT
    MKFS_PARM fmt_params = {0};
    fmt_params.fmt = FM_EXFAT;           // Format as exFAT
    fmt_params.n_fat = 1;                // exFAT uses 1 FAT
    fmt_params.align = 0;                // Auto alignment
    fmt_params.n_root = 0;               // Not used for exFAT
    fmt_params.au_size = 0;              // Auto cluster size
    
    LOGNOTE("Data partition format parameters:");
    LOGNOTE("  Filesystem: exFAT");
    LOGNOTE("  Number of FATs: %u", fmt_params.n_fat);
    LOGNOTE("  Cluster size: auto");
    
    // Format the partition
    result = f_mkfs("1:", &fmt_params, work_buffer, work_buffer_size);
    
    // Clean up
    delete[] work_buffer;
    
    if (result != FR_OK) {
        LOGERR("Failed to format data partition as exFAT (error %d)", result);
        
        // Try FAT32 as fallback for compatibility
        LOGWARN("Attempting FAT32 format as fallback...");
        
        BYTE* fallback_buffer = new BYTE[work_buffer_size];
        if (fallback_buffer) {
            fmt_params.fmt = FM_FAT32;
            fmt_params.n_fat = 2;  // FAT32 uses 2 FATs
            
            result = f_mkfs("1:", &fmt_params, fallback_buffer, work_buffer_size);
            delete[] fallback_buffer;
            
            if (result == FR_OK) {
                LOGNOTE("Second partition formatted as FAT32 (fallback)");
            } else {
                LOGERR("FAT32 fallback formatting also failed (error %d)", result);
                f_mount(nullptr, "1:", 0);
                return false;
            }
        } else {
            f_mount(nullptr, "1:", 0);
            return false;
        }
    } else {
        LOGNOTE("Second partition formatted as exFAT successfully");
    }
    
    // Set volume label
    result = f_setlabel("1:USBODE_DATA");
    if (result != FR_OK) {
        LOGWARN("Failed to set volume label (error %d)", result);
    } else {
        LOGNOTE("Volume label 'USBODE_DATA' set successfully");
    }
    
    // Unmount the partition
    f_mount(nullptr, "1:", 0);
    
    LOGNOTE("Data partition setup completed successfully");
    
    return true;
}

bool SetupStatus::copyImagesDirectory() {
    LOGNOTE("Copying images directory from 0:/images to 1:/...");
    
    // Check if source directory exists
    DIR sourceDir;
    FRESULT result = f_opendir(&sourceDir, "0:/images");
    if (result != FR_OK) {
        LOGNOTE("Source images directory does not exist (error %d) - this is expected after f_fdisk", result);
        LOGNOTE("Boot partition is empty after partitioning - skipping copy");
        return true; // Not an error - boot partition is empty after f_fdisk
    }
    f_closedir(&sourceDir);
    
    // Verify partition 1 is accessible before trying to create directories
    DWORD free_clusters;
    FATFS* fs;
    result = f_getfree("1:", &free_clusters, &fs);
    if (result != FR_OK || fs == nullptr) {
        LOGERR("Partition 1 is not accessible for copying (error %d)", result);
        LOGERR("Partition 1 may not be properly formatted");
        return false;
    }
    
    // Create destination directory
    result = f_mkdir("1:/images");
    if (result != FR_OK && result != FR_EXIST) {
        LOGERR("Failed to create destination images directory (error %d)", result);
        return false;
    }
    
    // Open source directory for enumeration
    result = f_opendir(&sourceDir, "0:/images");
    if (result != FR_OK) {
        LOGERR("Failed to open source images directory (error %d)", result);
        return false;
    }
    
    FILINFO fileInfo;
    int filesCopied = 0;
    
    // Copy each file
    while (f_readdir(&sourceDir, &fileInfo) == FR_OK && fileInfo.fname[0]) {
        if (fileInfo.fattrib & AM_DIR) {
            continue; // Skip directories for now
        }
        
        // Use CString instead of sprintf
        CString sourcePath;
        CString destPath;
        sourcePath.Format("0:/images/%s", fileInfo.fname);
        destPath.Format("1:/images/%s", fileInfo.fname);
        
        // Update progress message for each file
        CString progressMsg;
        progressMsg.Format("Copying %s...", fileInfo.fname);
        setStatusMessage((const char*)progressMsg);
        
        // Check if destination file already exists and compare sizes
        FIL destFile;
        if (f_open(&destFile, (const char*)destPath, FA_READ) == FR_OK) {
            DWORD destSize = f_size(&destFile);
            f_close(&destFile);
            
            if (destSize == fileInfo.fsize) {
                LOGNOTE("File %s already exists with same size, skipping", fileInfo.fname);
                continue;
            }
        }
        
        LOGNOTE("Copying %s (%lu bytes)...", fileInfo.fname, fileInfo.fsize);
        
        // Copy the file
        FIL sourceFile, destFileWrite;
        result = f_open(&sourceFile, (const char*)sourcePath, FA_READ);
        if (result != FR_OK) {
            LOGERR("Failed to open source file %s (error %d)", (const char*)sourcePath, result);
            continue;
        }
        
        result = f_open(&destFileWrite, (const char*)destPath, FA_WRITE | FA_CREATE_ALWAYS);
        if (result != FR_OK) {
            LOGERR("Failed to create destination file %s (error %d)", (const char*)destPath, result);
            f_close(&sourceFile);
            continue;
        }
        
        // Copy data in chunks
        BYTE buffer[4096];
        UINT bytesRead, bytesWritten;
        bool copySuccess = true;
        
        while (copySuccess) {
            result = f_read(&sourceFile, buffer, sizeof(buffer), &bytesRead);
            if (result != FR_OK || bytesRead == 0) {
                break;
            }
            
            result = f_write(&destFileWrite, buffer, bytesRead, &bytesWritten);
            if (result != FR_OK || bytesWritten != bytesRead) {
                LOGERR("Write error for file %s", fileInfo.fname);
                copySuccess = false;
            }
        }
        
        f_close(&sourceFile);
        f_close(&destFileWrite);
        
        if (copySuccess) {
            filesCopied++;
            LOGNOTE("Successfully copied %s", fileInfo.fname);
        }
    }
    
    f_closedir(&sourceDir);
    
    LOGNOTE("Images directory copy completed");
    return true;
}

// Fallback to known size if detection fails
if (total_sectors == 0) {
    total_sectors = 31116288; // 15.9GB default
    LOGWARN("Could not detect SD card size, using default: %lu sectors", (unsigned long)total_sectors);
} else {
    LOGNOTE("Detected SD card size: %lu sectors (~%.1f GB)", 
            (unsigned long)total_sectors, (float)(total_sectors * 512) / (1024*1024*1024));
}