#include "setupstatus.h"
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/util.h>

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