//
// isdprotocol.h
//
#ifndef _isdprotocol_h
#define _isdprotocol_h

#include <circle/types.h>
#include <discimage/imagedevice.h>

// Forward declare to avoid circular includes
// We'll use a simple struct that matches what we need
struct ISDSetupData
{
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
    bool GetPendingResponseData(u8 *pBuffer, size_t nMaxLength, size_t *pActualLength);
    bool HasPendingData() const { return m_nResponseDataLength > 0; }    

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
    bool HandleCommandBatch(u8 *pData, size_t nLength, size_t *pResponseLength);
    void DumpCommandBatch(const u8 *pData, size_t nLength);
    bool ParseCommandBatch(u8 *pData, size_t nLength, size_t *pResponseLength);
    bool ProcessCommand(u8 commandCode, const u8 *pCmdData, size_t nCmdLength,
                        u8 *pResponse, size_t *pResponseLength);

    // Individual command handlers
    bool HandleModeSense0E(const u8 *pCmdData, size_t nCmdLength,
                           u8 *pResponse, size_t *pResponseLength);
    bool HandleCommand14(const u8 *pCmdData, size_t nCmdLength,
                         u8 *pResponse, size_t *pResponseLength);
    bool HandleCommand15(const u8 *pCmdData, size_t nCmdLength,
                         u8 *pResponse, size_t *pResponseLength);
    bool HandleCommand16(const u8 *pCmdData, size_t nCmdLength,
                         u8 *pResponse, size_t *pResponseLength);
    bool HandleCommand17(const u8 *pCmdData, size_t nCmdLength,
                         u8 *pResponse, size_t *pResponseLength);
    u8 m_nLastStatus;

    bool HandleStatusRequest(u8 *pResponse, size_t *pResponseLength);
    IImageDevice *m_pDevice;
    static const size_t MaxResponseSize = 512;
    u8 m_ResponseBuffer[MaxResponseSize];
    size_t m_nResponseDataLength;
    size_t m_nResponseDataOffset;  // For tracking partial reads
    struct PlaybackState
    {
        bool isPlaying;
        u8 currentTrack;
        u32 currentLBA;
        bool isPaused;
    } m_playbackState;
};

#endif