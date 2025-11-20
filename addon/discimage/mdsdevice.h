//
// MDS/MDF specific device interface
//
// Copyright (C) 2025 Dani Sarfati
//
#ifndef _IMDSDEVICE_H
#define _IMDSDEVICE_H

#include "imagedevice.h"

class MDSParser; // Forward declaration

/// Interface for MDS/MDF disc images (Alcohol 120% format)
/// MDS format includes full subchannel data, making it suitable for
/// copy-protected discs like SafeDisc
class IMDSDevice : public IImageDevice {
public:
    IMDSDevice() = default;
    virtual ~IMDSDevice() = default;
    
    /// Get the MDS parser for direct access to MDS structures
    /// \return Pointer to MDSParser, or nullptr if not available
    virtual MDSParser* GetParser() const = 0;
    
    /// MDS files always have this type
    FileType GetFileType() const override { 
        return FileType::MDS; 
    }
    
    /// MDS format typically has subchannel data
    /// \return true if this specific MDS has subchannel data
    bool HasSubchannelData() const override = 0;
    
    /// Read subchannel data from the MDF file
    /// \param lba Logical Block Address
    /// \param subchannel Buffer to receive 96 bytes of subchannel data
    /// \return 96 on success, -1 on error
    int ReadSubchannel(u32 lba, u8* subchannel) override = 0;
    
    /// Optional: Get a generated CUE sheet for compatibility
    /// Some tools may expect a CUE sheet representation
    /// \return Generated CUE sheet, or nullptr
    virtual const char* GetCueSheet() const override { return nullptr; }
};

#endif