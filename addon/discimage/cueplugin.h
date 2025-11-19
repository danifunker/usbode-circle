// discimage/cueplugin.h
#ifndef _CUEPLUGIN_H
#define _CUEPLUGIN_H

#include "imageplugin.h"
#include <fatfs/ff.h>

class CCuePlugin : public IImagePlugin {
public:
    CCuePlugin();
    ~CCuePlugin() override;
    
    const char* GetName() const override { return "CUE/BIN Plugin"; }
    bool CanHandle(const char* filename) const override;
    bool Open(const char* filename) override;
    void Close() override;
    
    // ... implement IImagePlugin interface
    
    // CUE files don't have subchannel data
    bool HasSubchannelData() const override { return false; }
    int ReadSubchannel(u32 lba, SubchannelData* subchannel) override { return -1; }
    bool HasCopyProtection() const override { return false; }
    
private:
    FIL* m_bin_file = nullptr;
    char* m_cue_data = nullptr;
    MEDIA_TYPE m_mediaType;
    
    // Parse CUE sheet into track structures
    bool ParseCueSheet(const char* cue_data);
};

#endif