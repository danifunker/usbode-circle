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
    // Basic Device I/O Operations (inherited from CDevice)
    // ========================================================================
    // virtual int Read(void* pBuffer, size_t nCount) = 0;
    // virtual int Write(const void* pBuffer, size_t nCount) = 0;
    
    // ========================================================================
    // Image File Operations
    // ========================================================================
    
    /// Seek to a position in the image file
    /// \param ullOffset Offset in bytes from start of file
    /// \return New position, or 0 on error
    virtual u64 Seek(u64 ullOffset) = 0;
    
    /// Get total size of the image file
    /// \return Size in bytes
    virtual u64 GetSize(void) const = 0;
    
    /// Get current position in the image file
    /// \return Current offset in bytes, or (u64)-1 on error
    virtual u64 Tell() const = 0;
    
    // ========================================================================
    // Media Information
    // ========================================================================
    
    /// Get the media type (CD or DVD)
    virtual MEDIA_TYPE GetMediaType() const { return MEDIA_TYPE::CD; }
    
    /// Get the file format type
    virtual FileType GetFileType() const = 0;
    
    // ========================================================================
    // Track/TOC Information
    // ========================================================================
    
    /// Get number of tracks on the disc
    virtual int GetNumTracks() const = 0;
    
    /// Get starting LBA of a track
    /// \param track Track number (0-based)
    /// \return Starting LBA (Logical Block Address)
    virtual u32 GetTrackStart(int track) const = 0;
    
    /// Get length of a track in sectors
    /// \param track Track number (0-based)
    /// \return Length in sectors
    virtual u32 GetTrackLength(int track) const = 0;
    
    /// Check if a track contains audio data
    /// \param track Track number (0-based)
    /// \return true if audio track, false if data track
    virtual bool IsAudioTrack(int track) const = 0;
    
    // ========================================================================
    // Subchannel Support (Critical for Copy Protection like SafeDisc)
    // ========================================================================
    
    /// Check if this image format contains subchannel data
    /// Subchannel data is required for copy-protected discs (SafeDisc, etc.)
    /// \return true if subchannel data is available
    virtual bool HasSubchannelData() const { return false; }
    // Legacy CUE sheet support (for backward compatibility)
    // Both CUE/BIN and MDS formats can provide a CUE representation
    virtual const char* GetCueSheet() const { return nullptr; }    
    /// Read subchannel data for a specific sector
    /// \param lba Logical Block Address (sector number)
    /// \param subchannel Buffer to receive 96 bytes of subchannel data (P-W channels)
    /// \return Number of bytes read (96 on success), or -1 on error
    virtual int ReadSubchannel(u32 lba, u8* subchannel) { return -1; }
};

#endif