//
// stubs.cpp
//
// Host implementations for the Circle classes the USBODE gadget links
// against: logger (env-gated stdout), scheduler task registry, virtual
// timer, and the USB gadget base classes whose BeginTransfer()/Stall()
// land in the TestBus sink instead of DWC hardware.
//
#include "testbus.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/usb/gadget/dwusbgadget.h>
#include <circle/usb/gadget/dwusbgadgetendpoint.h>
#include <configservice/configservice.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// TestBus
// ---------------------------------------------------------------------------

TestBus &TestBus::Get()
{
    static TestBus instance;
    return instance;
}

// ---------------------------------------------------------------------------
// CLogger
// ---------------------------------------------------------------------------

CLogger *CLogger::Get(void)
{
    static CLogger instance;
    return &instance;
}

void CLogger::Write(const char *pSource, TLogSeverity Severity, const char *pMessage, ...)
{
    static const bool verbose = getenv("USBODE_TEST_VERBOSE") != nullptr;
    if (!verbose)
    {
        return;
    }

    static const char *severityNames[] = {"panic", "error", "warn", "note", "debug"};
    fprintf(stdout, "[%s] %s: ", severityNames[Severity], pSource);

    va_list var;
    va_start(var, pMessage);
    vfprintf(stdout, pMessage, var);
    va_end(var);

    fprintf(stdout, "\n");
}

// ---------------------------------------------------------------------------
// CScheduler
// ---------------------------------------------------------------------------

namespace
{
    std::vector<std::pair<std::string, CTask *>> &TaskRegistry()
    {
        static std::vector<std::pair<std::string, CTask *>> registry;
        return registry;
    }
}

CScheduler *CScheduler::Get(void)
{
    static CScheduler instance;
    return &instance;
}

CTask *CScheduler::GetTask(const char *pTaskName)
{
    for (auto &entry : TaskRegistry())
    {
        if (entry.first == pTaskName)
        {
            return entry.second;
        }
    }
    return nullptr;
}

void CScheduler::TestRegisterTask(const char *pName, CTask *pTask)
{
    TaskRegistry().push_back({pName, pTask});
}

void CScheduler::TestClearTasks(void)
{
    TaskRegistry().clear();
}

// ---------------------------------------------------------------------------
// CTimer
// ---------------------------------------------------------------------------

namespace
{
    unsigned g_nTicks = 1000; // arbitrary nonzero start
}

CTimer *CTimer::Get(void)
{
    static CTimer instance;
    return &instance;
}

unsigned CTimer::GetTicks(void)
{
    return g_nTicks;
}

unsigned CTimer::GetClockTicks(void)
{
    return g_nTicks * (CLOCKHZ / 100);
}

void CTimer::TestAdvanceTicks(unsigned nTicks)
{
    g_nTicks += nTicks;
}

void CTimer::TestReset(void)
{
    g_nTicks = 1000;
}

// ---------------------------------------------------------------------------
// ConfigService static
// ---------------------------------------------------------------------------

ConfigService *ConfigService::s_pThis = nullptr;

// ---------------------------------------------------------------------------
// CDWUSBGadget
// ---------------------------------------------------------------------------

CDWUSBGadget::CDWUSBGadget(CInterruptSystem *pInterruptSystem, TDeviceSpeed DeviceSpeed)
    : m_DeviceSpeed(DeviceSpeed)
{
}

CDWUSBGadget::~CDWUSBGadget(void)
{
}

// ---------------------------------------------------------------------------
// CDWUSBGadgetEndpoint
// ---------------------------------------------------------------------------

CDWUSBGadgetEndpoint::CDWUSBGadgetEndpoint(const TUSBEndpointDescriptor *pDesc, CDWUSBGadget *pGadget)
    : m_Direction((pDesc->bEndpointAddress & 0x80) ? DirectionIn : DirectionOut),
      m_nMaxPacketSize(pDesc->wMaxPacketSize)
{
}

CDWUSBGadgetEndpoint::~CDWUSBGadgetEndpoint(void)
{
}

void CDWUSBGadgetEndpoint::BeginTransfer(TTransferMode Mode, void *pBuffer, size_t nLength)
{
    TestBus &bus = TestBus::Get();
    if (Mode == TransferDataIn)
    {
        bus.inTransfer.valid = true;
        bus.inTransfer.buffer = pBuffer;
        bus.inTransfer.length = nLength;
    }
    else
    {
        bus.outTransfer.valid = true;
        bus.outTransfer.buffer = pBuffer;
        bus.outTransfer.length = nLength;
    }
}

void CDWUSBGadgetEndpoint::Stall(boolean bIn)
{
    TestBus &bus = TestBus::Get();
    if (bIn)
    {
        bus.inStalled = true;
    }
    else
    {
        bus.outStalled = true;
    }
}

void CDWUSBGadgetEndpoint::SetMaxPacketSize(size_t nSize)
{
    m_nMaxPacketSize = nSize;
}
