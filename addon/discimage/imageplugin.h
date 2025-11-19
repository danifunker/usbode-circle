// discimage/imageplugin.h
#ifndef _IMAGEPLUGIN_H
#define _IMAGEPLUGIN_H

#include "cuedevice.h"
#include <circle/types.h>
#include "filetype.h"
// Subchannel data structure (96 bytes per sector)
struct SubchannelData {
    u8 data[96];  // P-W subchannels
    bool valid;
};

class IImagePlugin {
public:
    virtual ~IImagePlugin() = default;
    
    // Plugin identification
    virtual const char* GetName() const = 0;
    virtual bool CanHandle(const char* filename) const = 0;
    
    // Initialization
    virtual bool Open(const char* filename) = 0;
    virtual void Close() = 0;
    
    // Disc structure queries
    virtual int GetNumSessions() const = 0;
    virtual int GetNumTracks(int session) const = 0;
    virtual u32 GetTrackStart(int session, int track) const = 0;
    virtual u32 GetTrackLength(int session, int track) const = 0;
    virtual u8 GetTrackMode(int session, int track) const = 0;
    virtual bool IsAudioTrack(int session, int track) const = 0;
    
    // Data reading
    virtual int ReadSector(u32 lba, void* buffer, u32 sector_size) = 0;
    
    // Subchannel support (critical for SafeDisc)
    virtual bool HasSubchannelData() const = 0;
    virtual int ReadSubchannel(u32 lba, SubchannelData* subchannel) = 0;
    
    // Copy protection queries
    virtual bool HasCopyProtection() const = 0;
    virtual bool IsWeakSector(u32 lba) const = 0;
    virtual bool HasSubchannelErrors(u32 lba) const = 0;
    
    // Media info
    virtual MEDIA_TYPE GetMediaType() const = 0;
    virtual const char* GetCueSheet() const = 0;  // Optional, for compatibility
};

#endif