//
// CHD specific device interface
//
// Copyright (C) 2025 Dani Sarfati
//
#ifndef _ICHDDEVICE_H
#define _ICHDDEVICE_H

#include "imagedevice.h"

/// Interface for CHD disc images (MAME compressed hunks format)
/// CHD format is highly compressed and optimized for emulation
class ICHDDevice : public IImageDevice {
public:
    ICHDDevice() = default;
    virtual ~ICHDDevice() = default;
    
    /// CHD files always have this type
    FileType GetFileType() const override { 
        return FileType::CHD; 
    }
    
    /// CHD format can contain subchannel data
    /// \return true if this specific CHD has subchannel data
    bool HasSubchannelData() const override = 0;
    
    /// Read subchannel data from the CHD
    /// \param lba Logical Block Address
    /// \param subchannel Buffer to receive 96 bytes of subchannel data
    /// \return 96 on success, -1 on error
    int ReadSubchannel(u32 lba, u8* subchannel) override = 0;
    
    /// Optional: Get a generated CUE sheet for compatibility
    /// \return Generated CUE sheet, or nullptr
    virtual const char* GetCueSheet() const override { return nullptr; }
};

#endif