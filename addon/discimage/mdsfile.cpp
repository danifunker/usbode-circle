#include "mdsfile.h"
#include <assert.h>
#include <circle/stdarg.h>
#include <circle/util.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "util.h"
#include "../mdsparser/mdsparser.h"

LOGMODULE("CMDSFileDevice");

// Map an MDS track mode byte to the CUE track type describing the sectors as
// they are stored in the MDF.
//
// The encoding is the one libmirage's MDS parser implements: only the low
// nibble selects the mode, and every value has an alias 8 higher, so the 0xA9
// / 0xAA that Alcohol actually writes are the +8 forms of 0x01 / 0x02. Matching
// the nibble rather than the whole byte means an image written with the
// un-offset codes is read correctly instead of having its audio tracks served
// as data.
//
// Mode 2 in any of its flavours puts user data 24 bytes into the sector (12
// sync + 4 header + 8 subheader) where Mode 1 puts it at 16. Every one of them
// is described as MODE2/2352: the offset is what the reader needs and it is the
// same for all three, while Form 1 vs Form 2 differs only in how much of the
// sector past that offset is user data, which is a per-sector property that no
// CUE track type can express. Getting this wrong is not subtle -- describing a
// Video CD or a PlayStation disc as MODE1/2352 shifts every sector by 8 bytes.
static const char* CueTrackModeForMDSMode(u8 mode)
{
    // Masking with 0x07 rather than 0x0F folds each alias onto its base code in
    // one step: the pair differs only in bit 3, so this is the same test
    // libmirage spells out as "nibble == code || nibble == code + 8".
    switch (mode & 0x07) {
    case 0x01:  // 0x09, so 0xA9: what Alcohol writes for audio
        return "AUDIO";
    case 0x02:  // 0xAA
        return "MODE1/2352";
    case 0x00:  // 0xA8
    case 0x03:  // 0xAB, Mode 2
    case 0x04:  // 0xAC (and 0xEC), Mode 2 Form 1
    case 0x05:  // 0xAD, Mode 2 Form 2
    case 0x07:  // 0xAF
        return "MODE2/2352";
    default:
        // Not a mode libmirage recognises either. Mode 1 is the safer guess for
        // a data track: it is by far the commonest, and the alternative is
        // refusing to mount the disc at all.
        LOGWARN("Unknown MDS track mode 0x%02x, describing it as MODE1/2352", mode);
        return "MODE1/2352";
    }
}

CMDSFileDevice::CMDSFileDevice(const char* mds_filename, char *mds_str, size_t mds_size,
                               MEDIA_TYPE mediaType) :
    m_mds_str(mds_str),
    m_mds_size(mds_size),
    m_mediaType(mediaType)
{
    if (mds_filename) {
        strncpy(m_mds_filename, mds_filename, sizeof(m_mds_filename) - 1);
        m_mds_filename[sizeof(m_mds_filename) - 1] = '\0';
    }
}

bool CMDSFileDevice::Init() {
    m_parser = new MDSParser(m_mds_str, m_mds_size);
    if (!m_parser->isValid()) {
        LOGERR("Invalid MDS file");
        return false;
    }

    LOGNOTE("=== MDS Parser Debug Info ===");
    LOGNOTE("Number of sessions: %d", m_parser->getNumSessions());

    // Open MDF file
    const char* mdf_filename_from_mds = m_parser->getMDFilename();
    LOGNOTE("MDF filename from parser: %s", mdf_filename_from_mds);
    // Both sized to hold anything m_mds_filename can, plus room for the
    // ".mdf" the wildcard case below swaps in for the original extension.
    // At 255 a long path was silently truncated and the open then failed
    // with a confusing "file not found".
    char mdf_path[sizeof(m_mds_filename) + sizeof(".mdf")];
    char mdf_filename[sizeof(m_mds_filename) + sizeof(".mdf")];  // For CUE sheet - just the filename without path

    if (strcmp(mdf_filename_from_mds, "*.mdf") == 0) {
        // Handle wildcard filename - derive from MDS filename
        const char* extension = strrchr(m_mds_filename, '.');
        const char* last_slash = strrchr(m_mds_filename, '/');
        const char* basename_start = last_slash ? last_slash + 1 : m_mds_filename;
        
        if (extension && extension > basename_start) {
            // Extract just the base filename for the CUE sheet
            snprintf(mdf_filename, sizeof(mdf_filename), "%.*s.mdf", 
                    (int)(extension - basename_start), basename_start);
            // Full path for opening
            snprintf(mdf_path, sizeof(mdf_path), "%.*s.mdf", 
                    (int)(extension - m_mds_filename), m_mds_filename);
        } else {
            snprintf(mdf_filename, sizeof(mdf_filename), "%s.mdf", basename_start);
            snprintf(mdf_path, sizeof(mdf_path), "%s.mdf", m_mds_filename);
        }
    } else {
        // Use the filename from MDS
        const char* last_slash = strrchr(m_mds_filename, '/');
        if (last_slash) {
            snprintf(mdf_path, sizeof(mdf_path), "%.*s%s", 
                    (int)(last_slash - m_mds_filename + 1), m_mds_filename, mdf_filename_from_mds);
        } else {
            snprintf(mdf_path, sizeof(mdf_path), "%s", mdf_filename_from_mds);
        }
        // Just copy the filename for CUE
        snprintf(mdf_filename, sizeof(mdf_filename), "%s", mdf_filename_from_mds);
    }

    LOGNOTE("Attempting to open MDF file at: %s", mdf_path);
    LOGNOTE("MDF filename for CUE sheet: %s", mdf_filename);
    m_pFile = new FIL();
    FRESULT result = f_open(m_pFile, mdf_path, FA_READ);
    if (result != FR_OK) {
        LOGERR("Cannot open MDF file for reading (FatFs error %d)", result);
        delete m_pFile;
        m_pFile = nullptr;

        LOGNOTE("Scanning for similar files...");
        DIR dir;
        FILINFO fno;
        if (f_opendir(&dir, "1:/") == FR_OK) {
            while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
                if (!(fno.fattrib & AM_DIR)) {
                    LOGNOTE("Found file: %s", fno.fname);
                }
            }
            f_closedir(&dir);
        }
        return false;
    }
        if (m_pFile) {
        FatFsOptimizer::EnableFastSeek(m_pFile, &m_pCLMT, 256, "MDS: ");
    }

    // Generate CUE sheet from MDS data.
    //
    // Sized from the disc rather than fixed. A full Red Book disc is 99
    // tracks of roughly 40 characters each, which did not fit the old 4 KB
    // buffer once the FILE line carried a long name - and the appends below
    // advanced by snprintf's return value, which is the length it WOULD have
    // written, so overflowing it walked the cursor past the end and drove
    // `remaining` negative. It is on the heap because a task stack here is
    // only 32 KB.
    size_t total_blocks = 0;
    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        total_blocks += m_parser->getSession(i)->num_all_blocks;
    }
    const size_t cue_size = 512 + total_blocks * 64;
    char* cue_buffer = new char[cue_size];
    cue_buffer[0] = '\0';
    char* cue_ptr = cue_buffer;
    int remaining = (int)cue_size;

    // Append `len` bytes already written at cue_ptr, or stop cleanly if the
    // buffer is full. snprintf truncates rather than overflowing, so the
    // guard is only ever about the cursor arithmetic.
    auto advance = [&cue_ptr, &remaining](int len) -> bool {
        if (len < 0 || len >= remaining) {
            remaining = 0;  // full: leave the buffer terminated and stop
            return false;
        }
        cue_ptr += len;
        remaining -= len;
        return true;
    };

    // Use the resolved filename (not wildcard) in the CUE sheet
    advance(snprintf(cue_ptr, remaining, "FILE \"%s\" BINARY\n", mdf_filename));

    // Process all sessions
    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        LOGNOTE("Session %d:", i);
        LOGNOTE("  session_start: %llu", (unsigned long long)session->session_start);
        LOGNOTE("  session_end: %llu", (unsigned long long)session->session_end);
        LOGNOTE("  num_all_blocks: %d", session->num_all_blocks);

        // Process all track blocks in this session
        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* track = m_parser->getTrack(i, j);
            MDS_TrackExtraBlock* extra = m_parser->getTrackExtra(i, j);

            LOGNOTE("  Track block %d:", j);
            LOGNOTE("    mode: 0x%02x", track->mode);
            LOGNOTE("    point: %d (0x%02x)", track->point, track->point);
            LOGNOTE("    start_sector: %u", track->start_sector);
            LOGNOTE("    start_offset: %llu", (unsigned long long)track->start_offset);
            LOGNOTE("    sector_size: %u", track->sector_size);
            LOGNOTE("    subchannel: 0x%02x", track->subchannel);

            // Only process actual track entries (point is the track number)
            // Skip lead-in entries (point 0xA0, 0xA1, 0xA2)
            if (track->point == 0 || track->point >= 0xA0) {
                continue;
            }

            const char* mode_str = CueTrackModeForMDSMode(track->mode);

            if (!advance(snprintf(cue_ptr, remaining, "  TRACK %02d %s\n", track->point, mode_str))) {
                LOGERR("CUE sheet buffer full at track %d - disc has more tracks than fit",
                       track->point);
                break;
            }

            // Do NOT emit a PREGAP line: start_sector below is already a
            // final disc LBA (pregaps included), so a PREGAP keyword would
            // make the CUE parser shift every data_start by the pregap
            // length again. The shifted TOC sent track 1 to LBA 150 and DOS
            // drivers, which locate the ISO9660 PVD relative to the TOC,
            // failed with "not High Sierra or ISO-9660". File positions are
            // unaffected: reads are mapped through the MDS track table, not
            // this synthesized cue.
            if (extra && extra->pregap > 0) {
                LOGNOTE("    pregap: %u (not emitted, start_sector is absolute)", extra->pregap);
                LOGNOTE("    length: %u", extra->length);
            }

            // Add INDEX 01 with the track's start position
            // start_sector is in LBA (logical block address) format
            int minutes = track->start_sector / (75 * 60);
            int seconds = (track->start_sector / 75) % 60;
            int frames = track->start_sector % 75;
            if (!advance(snprintf(cue_ptr, remaining, "    INDEX 01 %02d:%02d:%02d\n",
                                  minutes, seconds, frames))) {
                LOGERR("CUE sheet buffer full at track %d - disc has more tracks than fit",
                       track->point);
                break;
            }
        }
    }

    m_cue_sheet = new char[strlen(cue_buffer) + 1];
    strcpy(m_cue_sheet, cue_buffer);
    delete[] cue_buffer;

    LOGNOTE("Generated CUE sheet:\n%s", m_cue_sheet);
    
    // Detect if this image has subchannel data (SafeDisc support)
    m_hasSubchannels = false;
    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* track = m_parser->getTrack(i, j);
            
            // Skip non-track entries
            if (track->point == 0 || track->point >= 0xA0) {
                continue;
            }
            
            // Check if ANY track has subchannel data
            // MDS format: subchannel field indicates presence
            // 0x00 = no subchannels
            // 0x08 = PW subchannels (96 bytes)
            if (track->subchannel != 0) {
                m_hasSubchannels = true;
                LOGNOTE("Track %d has subchannel data (type: 0x%02x, sector_size: %u)", 
                        track->point, track->subchannel, track->sector_size);
            }
        }
    }
    
    // Length of the disc in frames, taken from the track table. The MDF's
    // own length cannot stand in for it: with subchannels each sector
    // occupies 2448 bytes on disc but one 2352-byte frame, so dividing the
    // file size by 2352 claims about 4% more sectors than exist and lets a
    // host read off the end.
    m_nTotalFrames = 0;
    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* track = m_parser->getTrack(i, j);
            if (track->point == 0 || track->point >= 0xA0) {
                continue;
            }
            MDS_TrackExtraBlock* extra = m_parser->getTrackExtra(i, j);
            u32 end = track->start_sector + (extra ? extra->length : 0);
            if (end > m_nTotalFrames) {
                m_nTotalFrames = end;
            }
        }
    }
    if (m_nTotalFrames == 0) {
        // No track lengths recorded. Fall back to the old behaviour rather
        // than presenting an empty disc.
        m_nTotalFrames = (u32)(f_size(m_pFile) / 2352);
        LOGWARN("No track lengths in MDS; deriving %u frames from the MDF size",
                m_nTotalFrames);
    }

    LOGNOTE("=== Image has subchannel data: %s ===",
            m_hasSubchannels ? "YES (SafeDisc compatible)" : "NO");
    LOGNOTE("=== Disc length: %u frames ===", m_nTotalFrames);
    LOGNOTE("=== End MDS Debug ===");
    
    return true;
}

CMDSFileDevice::~CMDSFileDevice(void) {
    FatFsOptimizer::DisableFastSeek(&m_pCLMT);
    if (m_pFile) {
        f_close(m_pFile);
        delete m_pFile;
        m_pFile = nullptr;
    }

    if (m_mds_str != nullptr) {
        delete[] m_mds_str;
        m_mds_str = nullptr;
    }

    if (m_cue_sheet != nullptr) {
        delete[] m_cue_sheet;
        m_cue_sheet = nullptr;
    }

    delete m_parser;
}

bool CMDSFileDevice::TouchesUnstoredGap(u32 firstLBA, size_t nSectors) const {
    if (!m_parser) {
        return false;
    }
    for (size_t i = 0; i < nSectors; i++) {
        u32 lba = firstLBA + (u32)i;
        if (lba >= m_nTotalFrames) {
            break;  // past the disc: DoRead rejects that before reaching here
        }
        int session, trackIdx;
        if (FindTrackForLBA(lba, &session, &trackIdx) == nullptr) {
            return true;
        }
    }
    return false;
}

int CMDSFileDevice::ReadAcrossGaps(void *pBuffer, size_t nSize) {
    u8* dest = (u8*)pBuffer;
    const size_t sectors = nSize / 2352;
    size_t total_read = 0;

    for (size_t i = 0; i < sectors; i++) {
        int session, trackIdx;
        MDS_TrackBlock* track = FindTrackForLBA(m_nCurrentLBA, &session, &trackIdx);

        if (!track) {
            // Unstored pregap. Zeros are what the pregap of a data track holds
            // anyway, and they keep the transfer whole instead of failing it.
            memset(dest, 0, 2352);
        } else {
            // Seek per frame rather than trusting the file pointer: a gap
            // consumed no file position, so it is stale after one.
            u64 offset = track->start_offset +
                         (u64)(m_nCurrentLBA - track->start_sector) * track->sector_size;
            if (f_lseek(m_pFile, offset) != FR_OK) {
                LOGERR("Gap-aware read: seek to %llu for LBA %u failed",
                       (unsigned long long)offset, m_nCurrentLBA);
                return total_read > 0 ? (int)total_read : -1;
            }
            UINT bytes_read = 0;
            FRESULT result = f_read(m_pFile, dest, 2352, &bytes_read);
            if (result != FR_OK || bytes_read != 2352) {
                LOGERR("Gap-aware read: LBA %u returned %u bytes (err %d)",
                       m_nCurrentLBA, bytes_read, result);
                return total_read > 0 ? (int)total_read : -1;
            }
        }

        dest += 2352;
        total_read += 2352;
        m_nCurrentLBA++;
    }

    return (int)total_read;
}

int CMDSFileDevice::Read(void *pBuffer, size_t nSize) {
    if (!m_pFile) {
        LOGERR("Read !m_pFile");
        return -1;
    }

    // A transfer that crosses a pregap the MDF does not store cannot be one
    // f_read, because part of it has no bytes behind it. That is rare enough
    // to be worth detecting rather than paying for frame-by-frame reads on
    // every transfer, so the paths below are left as they were.
    if (nSize >= 2352 && TouchesUnstoredGap(m_nCurrentLBA, nSize / 2352)) {
        return ReadAcrossGaps(pBuffer, nSize);
    }

    // For images with subchannels, we need special handling
    // The file has 2448-byte sectors (2352 + 96), but callers expect 2352
    if (m_hasSubchannels) {
        // The track comes from the LBA Seek() resolved, not from the file
        // position. Tell() reports a PHYSICAL offset in the MDF, and on a
        // 2448-byte-per-sector track that runs ahead of lba * 2352 by one
        // sector every 24.5 - so dividing it by 2352 drifts, and in the last
        // few percent of the image it lands outside every track. That used to
        // fall through to the raw read below, which returned the subchannel
        // bytes as if they were user data.
        int session, trackIdx;
        MDS_TrackBlock* track = FindTrackForLBA(m_nCurrentLBA, &session, &trackIdx);

        if (track && track->sector_size == 2448) {
            // This track has subchannel data embedded
            // We need to read only the user data (2352 bytes) and skip subchannel (96 bytes)
            
            size_t sectors_to_read = nSize / 2352;
            u8* dest = (u8*)pBuffer;
            size_t total_read = 0;
            
            // LOGDBG("Reading %u sectors with subchannel skipping from LBA %u", 
            //        sectors_to_read, lba);
            
            for (size_t i = 0; i < sectors_to_read; i++) {
                UINT bytes_read = 0;
                
                // Read 2352 bytes of user data
                FRESULT result = f_read(m_pFile, dest, 2352, &bytes_read);
                if (result != FR_OK || bytes_read != 2352) {
                    LOGERR("Failed to read sector %u user data (got %u bytes)", i, bytes_read);
                    return total_read > 0 ? total_read : -1;
                }
                
                // Skip 96 bytes of subchannel data
                result = f_lseek(m_pFile, f_tell(m_pFile) + 96);
                if (result != FR_OK) {
                    LOGERR("Failed to skip subchannel data at sector %u", i);
                    return total_read > 0 ? total_read : -1;
                }
                
                dest += 2352;
                total_read += 2352;
                m_nCurrentLBA++;  // the reads below continue from here
            }

            return total_read;
        }
    }
    
    // Standard read for images without subchannels or tracks with normal sector size
    UINT nBytesRead = 0;
    FRESULT result = f_read(m_pFile, pBuffer, nSize, &nBytesRead);
    if (result != FR_OK) {
        LOGERR("Failed to read %d bytes into memory, err %d", nSize, result);
        return -1;
    }
    return nBytesRead;
}

int CMDSFileDevice::Write(const void *pBuffer, size_t nSize) {
    // Read-only device
    return -1;
}

u64 CMDSFileDevice::Tell() const {
    if (!m_pFile) {
        LOGERR("Tell !m_pFile");
        return static_cast<u64>(-1);
    }

    return f_tell(m_pFile);
}

u64 CMDSFileDevice::Seek(u64 nOffset) {
    if (!m_pFile) {
        LOGERR("Seek !m_pFile");
        return static_cast<u64>(-1);
    }

    // Don't seek if we're already there
    if (Tell() == nOffset)
        return nOffset;

    // Calculate which LBA is being requested
    u32 lba = nOffset / 2352;  // Assuming 2352 bytes per sector
    u32 offset_in_sector = nOffset % 2352;
    
    // Find which track contains this LBA
    int session, trackIdx;
    MDS_TrackBlock* track = FindTrackForLBA(lba, &session, &trackIdx);
    
    if (!track) {
        // An LBA inside the disc but outside every track is a pregap the
        // imaging tool chose not to store - Alcohol omits them by default, so
        // a two-track image typically has a 150-frame hole before track 2.
        // READ CAPACITY counts those frames, so a host reading slightly past a
        // track lands here legitimately, and a real drive answers rather than
        // failing. There is no file position to take up; Read() serves zeros.
        if (lba < m_nTotalFrames) {
            m_nCurrentLBA = lba;
            return nOffset;
        }
        LOGERR("Seek: LBA %u not found in any track", lba);
        return static_cast<u64>(-1);
    }

    // Calculate offset into MDF file
    u32 sectors_from_track_start = lba - track->start_sector;
    u64 actual_file_offset = track->start_offset + 
                             (sectors_from_track_start * track->sector_size) + 
                             offset_in_sector;
    
    // LOGDBG("Seek: LBA %u (offset %llu) -> track %d, file offset %llu", 
    //        lba, nOffset, track->point, actual_file_offset);
    
    FRESULT result = f_lseek(m_pFile, actual_file_offset);
    if (result != FR_OK) {
        LOGERR("Seek to file offset %llu failed, err %d", actual_file_offset, result);
        return static_cast<u64>(-1);
    }

    // Remember which frame this was: Read() cannot recover it from the file
    // position once subchannel data makes the physical stride 2448 bytes.
    m_nCurrentLBA = lba;

    // Return the logical offset that was requested (not the physical file offset)
    return nOffset;
}

u64 CMDSFileDevice::GetSize(void) const {
    if (!m_pFile) {
        LOGERR("GetSize !m_pFile");
        return 0;
    }

    // The size of the disc as callers see it, which is the same space Seek()
    // and GetByteOffsetForLBA() work in: 2352 bytes per frame. That is NOT
    // the size of the MDF whenever the image carries subchannel data, since
    // each frame then occupies 2448 bytes on disc. Returning the file size
    // there made READ CAPACITY report about 4% more sectors than the disc
    // has.
    return (u64)m_nTotalFrames * 2352ULL;
}

int CMDSFileDevice::GetNumTracks() const {
    if (!m_parser) return 0;
    
    int count = 0;
    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* track = m_parser->getTrack(i, j);
            // Count actual tracks (point is track number, skip lead-in/out)
            if (track->point > 0 && track->point < 0xA0) {
                count++;
            }
        }
    }
    return count;
}

u32 CMDSFileDevice::GetTrackStart(int track) const {
    if (!m_parser || track < 0) return 0;
    
    int current = 0;
    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* trackBlock = m_parser->getTrack(i, j);
            if (trackBlock->point > 0 && trackBlock->point < 0xA0) {
                if (current == track) {
                    return trackBlock->start_sector;
                }
                current++;
            }
        }
    }
    return 0;
}

u32 CMDSFileDevice::GetTrackLength(int track) const {
    if (!m_parser || track < 0) return 0;
    
    int current = 0;
    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* trackBlock = m_parser->getTrack(i, j);
            if (trackBlock->point > 0 && trackBlock->point < 0xA0) {
                if (current == track) {
                    MDS_TrackExtraBlock* extra = m_parser->getTrackExtra(i, j);
                    return extra ? extra->length : 0;
                }
                current++;
            }
        }
    }
    return 0;
}

bool CMDSFileDevice::IsAudioTrack(int track) const {
    if (!m_parser || track < 0) return false;
    
    int current = 0;
    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* trackBlock = m_parser->getTrack(i, j);
            if (trackBlock->point > 0 && trackBlock->point < 0xA0) {
                if (current == track) {
                    // Same encoding CueTrackModeForMDSMode() folds, so an image
                    // written with the un-offset mode codes reports its audio
                    // tracks as audio here too.
                    return (trackBlock->mode & 0x07) == 0x01;
                }
                current++;
            }
        }
    }
    return false;
}

MDS_TrackBlock* CMDSFileDevice::FindTrackForLBA(u32 lba, int* sessionOut, int* trackOut) const {
    if (!m_parser) return nullptr;
    
    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* track = m_parser->getTrack(i, j);
            
            // Skip non-track entries
            if (track->point == 0 || track->point >= 0xA0) {
                continue;
            }
            
            MDS_TrackExtraBlock* extra = m_parser->getTrackExtra(i, j);
            u32 track_length = extra ? extra->length : 0;
            
            if (lba >= track->start_sector && lba < (track->start_sector + track_length)) {
                if (sessionOut) *sessionOut = i;
                if (trackOut) *trackOut = j;
                return track;
            }
        }
    }
    return nullptr;
}

int CMDSFileDevice::ReadSubchannel(u32 lba, u8* subchannel) {
    if (!subchannel || !m_parser || !m_hasSubchannels) {
        return -1;
    }
    
    int session, trackIdx;
    MDS_TrackBlock* track = FindTrackForLBA(lba, &session, &trackIdx);
    
    if (!track) {
        LOGERR("LBA %u not found in any track", lba);
        return -1;
    }
    
    // Check if this track has subchannel data
    if (track->subchannel == 0) {
        return -1;
    }
    
    // Calculate offset into the MDF file
    u32 sectors_from_track_start = lba - track->start_sector;
    u64 sector_offset = track->start_offset + (sectors_from_track_start * track->sector_size);
    
    // Subchannel data is stored in the last 96 bytes of each raw sector
    // Raw sector format: 2352 bytes user data + 96 bytes subchannel
    u64 subchannel_offset = sector_offset + 2352;
    
    // Seek to subchannel position
    FRESULT result = f_lseek(m_pFile, subchannel_offset);
    if (result != FR_OK) {
        LOGERR("Failed to seek to subchannel at LBA %u (offset %llu)", lba, subchannel_offset);
        return -1;
    }
    
    // Read 96 bytes of subchannel data (P-W subchannels)
    UINT bytes_read;
    result = f_read(m_pFile, subchannel, 96, &bytes_read);
    if (result != FR_OK || bytes_read != 96) {
        LOGERR("Failed to read subchannel at LBA %u (read %u bytes)", lba, bytes_read);
        return -1;
    }
    
    return 96;
}