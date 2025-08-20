#ifndef SETUPSTATUS_H
#define SETUPSTATUS_H

#include <circle/sched/task.h>
#include <circle/sched/synchronizationevent.h>
#include <circle/string.h>
#include <circle/types.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <assert.h>

// Partition entry structure (16 bytes)
struct MBRPartitionEntry {
    uint8_t boot;          // 0x80 = bootable
    uint8_t startCHS[3];   // obsolete
    uint8_t type;          // partition type
    uint8_t endCHS[3];     // obsolete
    uint32_t startLBA;     // little-endian
    uint32_t numSectors;   // little-endian
};

class SetupStatus : public CTask {
public:
    SetupStatus(CEMMCDevice* pEMMC, const PARTITION* partitionMap = nullptr);
    ~SetupStatus();

    static SetupStatus* Get() {
        return s_pThis;
    }

    // Task interface
    void Run() override;

    // Status management
    void setSetupRequired(bool required);
    bool isSetupRequired() const;
    
    void setSetupInProgress(bool inProgress);
    bool isSetupInProgress() const;
    
    void setSetupComplete(bool complete);
    bool isSetupComplete() const;
    
    void setStatusMessage(const char* message);
    const char* getStatusMessage() const;

    // Progress tracking
    void setProgress(int current, int total);
    int getCurrentProgress() const;
    int getTotalProgress() const;

    // Setup operations
    bool checkPartitionExists(int partition);
    bool performSetup();

private:
    static SetupStatus* s_pThis;
    
    // Hardware reference
    CEMMCDevice* m_pEMMC;
    
    // Partition mapping (optional - if provided will copy to global VolToPart)
    const PARTITION* m_pPartitionMap;
    
    // Setup helper methods - ported from kernel.cpp
    void displayPartitionTable();
    bool resizeSecondPartition();
    bool formatPartitionAsExFAT();
    bool copyImagesDirectory();
    
    // Status variables
    volatile bool m_setupRequired;
    volatile bool m_setupInProgress;
    volatile bool m_setupComplete;
    CString m_statusMessage;
    volatile int m_currentProgress;
    volatile int m_totalProgress;
    
    // Event for waking up the task
    CSynchronizationEvent m_Event;
};

#endif // SETUPSTATUS_H