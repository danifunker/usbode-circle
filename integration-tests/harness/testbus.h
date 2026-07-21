//
// testbus.h
//
// Records what the gadget asks the (nonexistent) USB controller to do.
// CDWUSBGadgetEndpoint::BeginTransfer()/Stall() in the stub layer write
// here; CGadgetTestBench reads it and completes transfers the way a real
// host would.
//
#ifndef _test_host_testbus_h
#define _test_host_testbus_h

#include <stddef.h>

struct TestBus
{
    struct Pending
    {
        bool valid = false;
        void *buffer = nullptr;
        size_t length = 0;
    };

    Pending inTransfer;  // device -> host (data phase or CSW)
    Pending outTransfer; // host -> device (CBW or data phase)

    bool inStalled = false;
    bool outStalled = false;

    static TestBus &Get();

    void Reset()
    {
        inTransfer = Pending();
        outTransfer = Pending();
        inStalled = false;
        outStalled = false;
    }
};

#endif
