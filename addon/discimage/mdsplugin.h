// discimage/mdsplugin.h
#ifndef _MDSPLUGIN_H
#define _MDSPLUGIN_H

#include "imageplugin.h"
#include "../mdsparser/mdsparser.h"
#include <fatfs/ff.h>

class CMDSPlugin : public IImagePlugin {
public:
    CMDSPlugin();
    ~CMDSPlugin() override;
    
    // Plugin interface
    const char* GetName() const override { return "MDS/MDF Plugin"; }
    bool CanHandle(const char* filename) const override;
    bool Open(const char* filename) override;
    void Close() override;
    
    // Disc structure
    int GetNumSessions() const override;
    int GetNumTracks(int session) const override;
    u32 GetTrackStart(int session, int track) const override;
    u32 GetTrackLength(int session, int track) const override;
    u8 GetTrackMode(int session, int track) const override;
    bool IsAudioTrack(int session, int track) const override;
    
    // Data reading
    int ReadSector(u32 lba, void* buffer, u32 sector_size) override;
    
    // Subchannel support - THIS IS KEY FOR SAFEDISC
    bool HasSubchannelData() const override { return true; }
    int ReadSubchannel(u32 lba, SubchannelData* subchannel) override;
    
    // Copy protection
    bool HasCopyProtection() const override;
    bool IsWeakSector(u32 lba) const override;
    bool HasSubchannelErrors(u32 lba) const override;
    
    // Media info
    MEDIA_TYPE GetMediaType() const override { return m_mediaType; }
    const char* GetCueSheet() const override { return m_cue_sheet; }
    
private:
    MDSParser* m_parser = nullptr;
    FIL* m_mdf_file = nullptr;
    char* m_cue_sheet = nullptr;
    MEDIA_TYPE m_mediaType;
    
    // Helper to extract subchannel from MDS
    bool ExtractSubchannelFromMDS(u32 lba, SubchannelData* subchannel);
};

#endif