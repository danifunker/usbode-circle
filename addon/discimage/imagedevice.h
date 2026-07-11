//
// Base interface for all disc image formats
//
// Copyright (C) 2025 Dani Sarfati
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
#ifndef _IIMAGEDEVICE_H
#define _IIMAGEDEVICE_H

#include <circle/device.h>
#include "filetype.h"

/// Base interface for all disc image types (CUE/BIN, MDS/MDF, ISO, etc.)
/// This provides the common operations needed for any disc image format.
class IImageDevice : public CDevice {
public:
    IImageDevice() = default;
    virtual ~IImageDevice() = default;
    
    // ========================================================================
    // Image File Operations
    // ========================================================================
    
    virtual u64 Seek(u64 ullOffset) = 0;
    virtual u64 GetSize(void) const = 0;
    virtual u64 Tell() const = 0;

    /// Translate a CD frame LBA to the byte offset in this device's Seek()
    /// space. CHD and MDS devices present a uniform 2352-bytes-per-frame
    /// logical space, so the default applies to them. CUE/BIN overrides
    /// this: its Seek() space is the raw BIN file, where per-track sector
    /// sizes differ (e.g. a MODE1/2048 data track before 2352-byte audio).
    virtual u64 GetByteOffsetForLBA(u32 lba) const { return (u64)lba * 2352ULL; }
    
    // ========================================================================
    // Media Information
    // ========================================================================
    
    virtual MEDIA_TYPE GetMediaType() const { return MEDIA_TYPE::CD; }
    virtual FileType GetFileType() const = 0;
    
    // ========================================================================
    // Track/TOC Information
    // ========================================================================
    
    virtual int GetNumTracks() const = 0;
    virtual u32 GetTrackStart(int track) const = 0;
    virtual u32 GetTrackLength(int track) const = 0;
    virtual bool IsAudioTrack(int track) const = 0;
    
    // ========================================================================
    // Subchannel Support (Critical for Copy Protection like SafeDisc)
    // ========================================================================
    
    virtual bool HasSubchannelData() const { return false; }
    virtual int ReadSubchannel(u32 lba, u8* subchannel) { return -1; }
    
    // ========================================================================
    // CUE Sheet Compatibility (for track navigation)
    // ========================================================================
    
    /// Get CUE sheet representation (for backward compatibility)
    /// Both CUE/BIN and MDS formats can provide a CUE representation
    /// for track navigation and TOC generation
    /// \return CUE sheet string, or nullptr if not available
    virtual const char* GetCueSheet() const { return nullptr; }
};

#endif