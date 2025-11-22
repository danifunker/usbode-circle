//
// isdprotocol.h
//
#ifndef _isdprotocol_h
#define _isdprotocol_h

#include <circle/types.h>
#include <discimage/imagedevice.h>

// Forward declare to avoid circular includes
// We'll use a simple struct that matches what we need
struct ISDSetupData {
    u8 bmRequestType;
    u8 bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
};

class ISDProtocol
{
public:
    ISDProtocol(IImageDevice *pDevice);
    ~ISDProtocol();

    // Main control transfer handler - use our own struct
    bool HandleControlTransfer(u8 bmRequestType, u8 bRequest, u16 wValue, 
                              u16 wIndex, u16 wLength, 
                              u8 *pDataBuffer, size_t *pDataLength);
    
    // SCSI command interceptors for ISD mode
    bool HandleModeSense(const u8 *pCDB, u8 *pDataBuffer, size_t dataBufferSize, size_t *pDataLength);
    
    // Update the disc image device
    void SetDevice(IImageDevice *pDevice);

private:
    // Parse aa-delimited vendor command batch
    void ParseCommandBatch(const u8 *data, size_t length);
    
    // Individual command handlers
    void HandleVendorCommand(u8 command, const u8 *params, size_t paramLength);
    
    // MODE SENSE page handlers
    bool BuildModeSense2A(u8 *pDataBuffer, size_t bufferSize, size_t *pDataLength);
    bool BuildModeSense0E(u8 *pDataBuffer, size_t bufferSize, size_t *pDataLength);
    
    // CD Audio playback commands (for future implementation)
    void HandlePlayAudio(const u8 *params);
    void HandlePauseResume(const u8 *params);
    void HandleStopPlay(const u8 *params);
    void HandleReadSubchannel(const u8 *params);
    
    IImageDevice *m_pDevice;
    
    struct PlaybackState {
        bool isPlaying;
        u8 currentTrack;
        u32 currentLBA;
        bool isPaused;
    } m_playbackState;
};

#endif