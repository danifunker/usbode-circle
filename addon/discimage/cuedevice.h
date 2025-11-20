//
// CUE/BIN specific device interface
//
// Copyright (C) 2025 Ian Cass
//
#ifndef _ICUEDEVICE_H
#define _ICUEDEVICE_H

#include "imagedevice.h"

/// Interface for CUE/BIN and ISO disc images
/// These formats use CUE sheets to describe track layout
class ICueDevice : public IImageDevice {
public:
    ICueDevice() = default;
    virtual ~ICueDevice() = default;
    
    /// Get the CUE sheet describing this disc
    /// \return CUE sheet as a string, or nullptr if not available
    virtual const char* GetCueSheet() const = 0;  // MUST override
        
    /// CUE/BIN files are always this type
    FileType GetFileType() const override { 
        return FileType::CUEBIN; 
    }
};

#endif