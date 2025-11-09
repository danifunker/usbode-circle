#ifndef _scsi_command_dispatcher_h
#define _scsi_command_dispatcher_h

#include "usbcdgadget.h"

class CUSBCDGadget;

class ScsiCommandDispatcher
{
public:
    static void Dispatch(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);

private:
    static void HandleTestUnitReady(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleRequestSense(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleRead12(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleInquiry(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleStartStopUnit(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandlePreventAllowMediumRemoval(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleReadCapacity10(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleRead10(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleReadCD(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleSetCDSpeed(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleVerify(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleReadTOC(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleReadSubChannel(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleReadTrackInformation(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleGetEventStatusNotification(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleReadDiscStructure(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleReadDiscInformation(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleReadHeader(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleGetConfiguration(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandlePauseResume(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleSeek(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandlePlayAudioMSF(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleStopScan(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandlePlayAudio10(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandlePlayAudio12(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleModeSelect10(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleModeSense6(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleModeSense10(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleGetPerformance(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleA4(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleListDevices(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleNumberOfFiles(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleListFiles(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleSetNextCD(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
    static void HandleUnknown(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW);
};

#endif
