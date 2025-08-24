#ifndef SETUPSTATUS_H
#define SETUPSTATUS_H

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

class SetupStatus {
public:

    void Run();

    // Singleton getter
    static SetupStatus* Get();
    
    // Disable copy and assignment
    SetupStatus(const SetupStatus&) = delete;
    SetupStatus& operator=(const SetupStatus&) = delete;

    static void Init(CEMMCDevice* pEMMC);
    static void Shutdown();

    // Status management
    bool isSetupRequired() const;
    bool isSetupInProgress() const;
    bool isSetupComplete() const;
    const char* getStatusMessage() const;
    int getCurrentProgress() const;
    int getTotalProgress() const;

    // Setup operations
    bool checkPartitionExists(int partition);
    bool performSetup();

private:
    SetupStatus(CEMMCDevice* pEMMC);
    ~SetupStatus();

    static SetupStatus* s_pThis;
    
    // Hardware reference
    CEMMCDevice* m_pEMMC;
    
    // Setup helper methods - ported from kernel.cpp
    void displayPartitionTable();
    bool resizeSecondPartition();
    bool formatPartitionAsExFAT();
    bool copyImagesDirectory();
    
    // Status variables
    volatile bool m_setupRequired = false;
    volatile bool m_setupInProgress = false;
    volatile bool m_setupComplete = false;
    volatile int m_currentProgress = 1;
    volatile int m_totalProgress = 5;
    const char*  m_statusMessage;
    
    // Event for waking up the task
    CSynchronizationEvent m_Event;
};

#endif // SETUPSTATUS_H
