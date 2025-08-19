#ifndef SETUPSTATUS_H
#define SETUPSTATUS_H

#include <circle/sched/task.h>
#include <circle/sched/synchronizationevent.h>
#include <circle/string.h>
#include <circle/types.h>
#include <assert.h>

class SetupStatus : public CTask {
public:
    SetupStatus();
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

private:
    static SetupStatus* s_pThis;
    
    // Status variables (no spinlock needed for simple reads/writes)
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