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
    : m_setupRequired(false),
      m_setupInProgress(false),
      m_setupComplete(false),
      m_currentProgress(0),
      m_totalProgress(0)
{
    // I am the one and only!
    assert(s_pThis == nullptr);
    s_pThis = this;
    
    SetName(FromSetupStatus);
    LOGNOTE("SetupStatus service initialized");
}

SetupStatus::~SetupStatus() {
    s_pThis = nullptr;
}

void SetupStatus::Run() {
    LOGNOTE("SetupStatus service started");
    
    while (true) {
        m_Event.Clear();
        
        // Simple periodic logging - no complex synchronization needed
        if (m_setupInProgress) {
            if (m_totalProgress > 0) {
                LOGNOTE("Setup progress: %s (%d/%d)", 
                       (const char*)m_statusMessage, 
                       m_currentProgress, 
                       m_totalProgress);
            } else if (m_statusMessage.GetLength() > 0) {
                LOGNOTE("Setup status: %s", (const char*)m_statusMessage);
            }
        }
        
        m_Event.Wait();
    }
}

void SetupStatus::setSetupRequired(bool required) {
    if (m_setupRequired != required) {
        m_setupRequired = required;
        LOGNOTE("Setup required: %s", required ? "YES" : "NO");
        m_Event.Set(); // Wake up the task
    }
}

bool SetupStatus::isSetupRequired() const {
    return m_setupRequired;
}

void SetupStatus::setSetupInProgress(bool inProgress) {
    if (m_setupInProgress != inProgress) {
        m_setupInProgress = inProgress;
        LOGNOTE("Setup in progress: %s", inProgress ? "YES" : "NO");
        if (inProgress) {
            m_currentProgress = 0;
            m_totalProgress = 0;
        }
        m_Event.Set(); // Wake up the task
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
    }
}

bool SetupStatus::isSetupComplete() const {
    return m_setupComplete;
}

void SetupStatus::setStatusMessage(const char* message) {
    CString newMessage = message ? message : "";
    if (m_statusMessage.Compare(newMessage) != 0) {
        m_statusMessage = newMessage;
        m_Event.Set(); // Wake up the task for immediate logging
    }
}

const char* SetupStatus::getStatusMessage() const {
    return m_statusMessage;
}

void SetupStatus::setProgress(int current, int total) {
    m_currentProgress = current;
    m_totalProgress = total;
    m_Event.Set(); // Wake up the task
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
    
    FATFS fs;
    FRESULT result = f_mount(&fs, (const char*)drive, 0);
    if (result == FR_OK) {
        f_mount(nullptr, (const char*)drive, 0); // Unmount test mount
        return true;
    }
    return false;
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
    
    // Try to mount first - maybe it already exists
    FATFS fs;
    FRESULT result = f_mount(&fs, "1:", 1);
    if (result == FR_OK) {
        LOGNOTE("Second partition already exists, formatting...");
        return formatPartition();
    }
    
    LOGNOTE("Second partition doesn't exist, creating partition table...");
    
    // Create partition table using f_fdisk
    // This creates a simple 2-partition layout
    LBA_t partTable[4] = {
        50,     // Start of partition 1 (leave some space for MBR)
        0,      // Size of partition 1 (0 = use existing)
        0,      // Start of partition 2 (0 = auto-calculate)
        0       // Size of partition 2 (0 = use remaining space)
    };
    
    BYTE work[FF_MAX_SS];
    result = f_fdisk(0, partTable, work);  // Physical drive 0
    if (result != FR_OK) {
        LOGERR("Failed to create partition table (error %d)", result);
        return false;
    }
    
    LOGNOTE("Partition table created, now formatting...");
    return formatPartition();
}

bool SetupStatus::formatPartition() {
    // Now try to mount and format the second partition
    FATFS fs;
    FRESULT result = f_mount(&fs, "1:", 1);
    if (result != FR_OK) {
        LOGERR("Cannot mount second partition after creation (error %d)", result);
        return false;
    }
    
    // Format as exFAT
    BYTE work[FF_MAX_SS];
    MKFS_PARM fmt_params;
    fmt_params.fmt = FM_EXFAT;
    fmt_params.n_fat = 1;
    fmt_params.align = 0;
    fmt_params.n_root = 0;
    fmt_params.au_size = 0;
    
    result = f_mkfs("1:", &fmt_params, work, sizeof(work));
    if (result != FR_OK) {
        LOGERR("Failed to format second partition (error %d)", result);
        return false;
    }
    
    LOGNOTE("Second partition formatted successfully");
    return true;
}

bool SetupStatus::copyImagesDirectory() {
    LOGNOTE("Copying images directory from 0:/images to 1:/...");
    
    // Check if source directory exists
    DIR sourceDir;
    FRESULT result = f_opendir(&sourceDir, "0:/images");
    if (result != FR_OK) {
        LOGNOTE("Source images directory does not exist, skipping copy");
        return true; // Not an error if source doesn't exist
    }
    f_closedir(&sourceDir);
    
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
    
    LOGNOTE("Images directory copy completed - %d files copied", filesCopied);
    return true;
}