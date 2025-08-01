#ifndef _displayservice_h
#define _displayservice_h

#include <circle/koptions.h>
#include <circle/machineinfo.h>
#include <circle/new.h>
#include <circle/sched/task.h>
#include <circle/time.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <circle/util.h>
#include <linux/kernel.h>

#include "idisplay.h"

class DisplayService : public CTask {
   public:
    DisplayService(const char* displayType);
    ~DisplayService(void);
    boolean Initialize();
    void Run(void);

   private:
    void CreateDisplay(const char* displayType);

   private:
    static DisplayService* s_pThis;
    bool isInitialized = false;
    IDisplay* m_IDisplay = nullptr;
};

#endif
