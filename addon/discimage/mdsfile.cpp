#include "mdsfile.h"
#include <assert.h>
#include <circle/stdarg.h>
#include <circle/util.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../mdsparser/mdsparser.h"

LOGMODULE("CMDSFileDevice");

CMDSFileDevice::CMDSFileDevice(const char* mds_filename, char *mds_str, MEDIA_TYPE mediaType) :
    m_mds_str(mds_str),
    m_mds_filename(mds_filename),
    m_mediaType(mediaType),
    m_logicalPosition(0)  // ADD THIS
{
}

bool CMDSFileDevice::Init() {
    m_parser = new MDSParser(m_mds_str);
    if (!m_parser->isValid()) {
        LOGERR("Invalid MDS file");
        return false;
    }

    LOGNOTE("=== MDS Parser Debug Info ===");
    LOGNOTE("Number of sessions: %d", m_parser->getNumSessions());

    const char* mdf_filename_from_mds = m_parser->getMDFilename();
    LOGNOTE("MDF filename from parser: %s", mdf_filename_from_mds);
    char mdf_path[255];
    char mdf_filename[255];

    if (strcmp(mdf_filename_from_mds, "*.mdf") == 0) {
        const char* extension = strrchr(m_mds_filename, '.');
        const char* last_slash = strrchr(m_mds_filename, '/');
        const char* basename_start = last_slash ? last_slash + 1 : m_mds_filename;
        
        if (extension && extension > basename_start) {
            snprintf(mdf_filename, sizeof(mdf_filename), "%.*s.mdf", 
                    (int)(extension - basename_start), basename_start);
            snprintf(mdf_path, sizeof(mdf_path), "%.*s.mdf", 
                    (int)(extension - m_mds_filename), m_mds_filename);
        } else {
            snprintf(mdf_filename, sizeof(mdf_filename), "%s.mdf", basename_start);
            snprintf(mdf_path, sizeof(mdf_path), "%s.mdf", m_mds_filename);
        }
    } else {
        const char* last_slash = strrchr(m_mds_filename, '/');
        if (last_slash) {
            snprintf(mdf_path, sizeof(mdf_path), "%.*s%s", 
                    (int)(last_slash - m_mds_filename + 1), m_mds_filename, mdf_filename_from_mds);
        } else {
            snprintf(mdf_path, sizeof(mdf_path), "%s", mdf_filename_from_mds);
        }
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

    char cue_buffer[4096];
    char* cue_ptr = cue_buffer;
    int remaining = sizeof(cue_buffer);

    int len = snprintf(cue_ptr, remaining, "FILE \"%s\" BINARY\n", mdf_filename);
    cue_ptr += len;
    remaining -= len;

    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        LOGNOTE("Session %d:", i);
        LOGNOTE("  session_start: %d", session->session_start);
        LOGNOTE("  session_end: %d", session->session_end);
        LOGNOTE("  num_all_blocks: %d", session->num_all_blocks);

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

            if (track->point == 0 || track->point >= 0xA0) {
                continue;
            }

            const char* mode_str;
            if (track->mode == 0xA9) {
                mode_str = "AUDIO";
            } else {
                mode_str = "MODE1/2352";
            }

            len = snprintf(cue_ptr, remaining, "  TRACK %02d %s\n", track->point, mode_str);
            cue_ptr += len;
            remaining -= len;

            if (extra && extra->pregap > 0) {
                LOGNOTE("    pregap: %u", extra->pregap);
                LOGNOTE("    length: %u", extra->length);
            }

            int index_lba = track->start_sector;
            if (extra && extra->pregap > 0) {
                index_lba += extra->pregap;
            }
            int minutes = index_lba / (75 * 60);
            int seconds = (index_lba / 75) % 60;
            int frames = index_lba % 75;
            len = snprintf(cue_ptr, remaining, "    INDEX 01 %02d:%02d:%02d\n", minutes, seconds, frames);
            cue_ptr += len;
            remaining -= len;
        }
    }

    m_cue_sheet = new char[strlen(cue_buffer) + 1];
    strcpy(m_cue_sheet, cue_buffer);
    
    LOGNOTE("Generated CUE sheet:\n%s", m_cue_sheet);
    
    m_hasSubchannels = false;
    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* track = m_parser->getTrack(i, j);
            
            if (track->point == 0 || track->point >= 0xA0) {
                continue;
            }
            
            if (track->subchannel != 0) {
                m_hasSubchannels = true;
                LOGNOTE("Track %d has subchannel data (type: 0x%02x, sector_size: %u)", 
                        track->point, track->subchannel, track->sector_size);
            }
        }
    }
    
    LOGNOTE("=== Image has subchannel data: %s ===", 
            m_hasSubchannels ? "YES (SafeDisc compatible)" : "NO");
    LOGNOTE("=== End MDS Debug ===");
    
    return true;
}

CMDSFileDevice::~CMDSFileDevice(void) {
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

int CMDSFileDevice::Read(void *pBuffer, size_t nSize) {
    if (!m_pFile) {
        LOGERR("Read !m_pFile");
        return -1;
    }

    LOGDBG("Read() called: size=%u, current logical position=%llu (LBA %u)", 
           nSize, m_logicalPosition, (u32)(m_logicalPosition / 2352));

    // Calculate current LBA from logical position
    u32 lba = m_logicalPosition / 2352;
    
    // Find which track we're in
    int session, trackIdx;
    MDS_TrackBlock* track = FindTrackForLBA(lba, &session, &trackIdx);
    
    if (!track) {
        LOGERR("Read: LBA %u not found in any track", lba);
        return -1;
    }
    
    // Get pregap for this track
    MDS_TrackExtraBlock* extra = m_parser->getTrackExtra(session, trackIdx);
    u32 pregap = extra ? extra->pregap : 0;
    u32 data_start_lba = track->start_sector + pregap;
    
    size_t sectors_to_read = nSize / 2352;
    u8* dest = (u8*)pBuffer;
    size_t total_read = 0;
    
    // For images with subchannels, we need special handling
    if (m_hasSubchannels && track->sector_size == 2448) {
        LOGDBG("Reading %u sectors with subchannel skipping from LBA %u", 
               sectors_to_read, lba);
        
        bool need_seek = true;  // Track if we need to seek for first data sector
        
        for (size_t i = 0; i < sectors_to_read; i++) {
            u32 current_lba = lba + i;
            
            // If we're in pregap, return zeros
            if (current_lba < data_start_lba) {
                LOGDBG("Sector LBA %u is in pregap, returning zeros", current_lba);
                memset(dest, 0, 2352);
                dest += 2352;
                total_read += 2352;
                m_logicalPosition += 2352;
                need_seek = true;  // Will need to seek when we hit data
                continue;
            }
            
            // We're at or past data start - seek if needed
            if (need_seek) {
                u32 sectors_from_data_start = current_lba - data_start_lba;
                u64 file_offset = track->start_offset + (sectors_from_data_start * track->sector_size);
                LOGDBG("Seeking to data at LBA %u, file offset %llu", current_lba, file_offset);
                FRESULT result = f_lseek(m_pFile, file_offset);
                if (result != FR_OK) {
                    LOGERR("Failed to seek to data sector, err %d", result);
                    return total_read > 0 ? total_read : -1;
                }
                need_seek = false;
            }
            
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
            m_logicalPosition += 2352;
        }
        
        return total_read;
    }
    
    // Handle non-subchannel images
    bool need_seek = true;
    
    for (size_t i = 0; i < sectors_to_read; i++) {
        u32 current_lba = lba + i;
        
        if (current_lba < data_start_lba) {
            // Reading from pregap - return zeros
            LOGDBG("Sector LBA %u is in pregap, returning zeros", current_lba);
            memset(dest, 0, 2352);
            dest += 2352;
            total_read += 2352;
            m_logicalPosition += 2352;
            need_seek = true;  // Will need to seek when we hit data
        } else {
            // Reading actual data - seek if transitioning from pregap
            if (need_seek) {
                u32 sectors_from_data_start = current_lba - data_start_lba;
                u64 file_offset = track->start_offset + (sectors_from_data_start * track->sector_size);
                LOGDBG("Seeking to data at LBA %u, file offset %llu", current_lba, file_offset);
                FRESULT result = f_lseek(m_pFile, file_offset);
                if (result != FR_OK) {
                    LOGERR("Failed to seek to data sector, err %d", result);
                    return total_read > 0 ? total_read : -1;
                }
                need_seek = false;
            }
            
            UINT nBytesRead = 0;
            FRESULT result = f_read(m_pFile, dest, 2352, &nBytesRead);
            if (result != FR_OK) {
                LOGERR("Failed to read sector, err %d", result);
                return total_read > 0 ? total_read : -1;
            }
            dest += nBytesRead;
            total_read += nBytesRead;
            m_logicalPosition += nBytesRead;
            
            if (nBytesRead < 2352) {
                // Partial read, we're done
                break;
            }
        }
    }
    
    return total_read;
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

    return m_logicalPosition;
}

u64 CMDSFileDevice::Seek(u64 nOffset) {
    if (!m_pFile) {
        LOGERR("Seek !m_pFile");
        return static_cast<u64>(-1);
    }
    LOGDBG("Seek() called: target offset=%llu (LBA %u), current position=%llu (LBA %u)", 
       nOffset, (u32)(nOffset / 2352), m_logicalPosition, (u32)(m_logicalPosition / 2352));
    // Don't seek if we're already there
    if (m_logicalPosition == nOffset)
        return nOffset;

    // Calculate which LBA is being requested
    u32 lba = nOffset / 2352;
    u32 offset_in_sector = nOffset % 2352;
    
    // Find which track contains this LBA
    int session, trackIdx;
    MDS_TrackBlock* track = FindTrackForLBA(lba, &session, &trackIdx);
    
    if (!track) {
        LOGERR("Seek: LBA %u not found in any track", lba);
        return static_cast<u64>(-1);
    }
    
    // Get pregap for this track
    MDS_TrackExtraBlock* extra = m_parser->getTrackExtra(session, trackIdx);
    u32 pregap = extra ? extra->pregap : 0;
    
    // Calculate the first LBA with actual file data
    u32 data_start_lba = track->start_sector + pregap;
    
    // Check if we're seeking into the pregap (before actual file data)
    if (lba < data_start_lba) {
        // LBA is in pregap - no physical data in file for these sectors
        // Position at start of actual data and let Read() handle returning zeros
        LOGDBG("Seek: LBA %u is in pregap (data starts at %u)", lba, data_start_lba);
        
        FRESULT result = f_lseek(m_pFile, track->start_offset);
        if (result != FR_OK) {
            LOGERR("Seek to track start failed, err %d", result);
            return static_cast<u64>(-1);
        }
        // Update logical position and return
        m_logicalPosition = nOffset;
        return nOffset;
    }
    
    // Calculate offset into MDF file for actual data
    // LBA is absolute, data_start_lba is where file data begins
    u32 sectors_from_data_start = lba - data_start_lba;
    u64 actual_file_offset = track->start_offset + 
                             (sectors_from_data_start * track->sector_size) + 
                             offset_in_sector;
    
    LOGDBG("Seek: LBA %u (offset %llu) -> track %d, file offset %llu", 
           lba, nOffset, track->point, actual_file_offset);
    
    FRESULT result = f_lseek(m_pFile, actual_file_offset);
    if (result != FR_OK) {
        LOGERR("Seek to file offset %llu failed, err %d", actual_file_offset, result);
        return static_cast<u64>(-1);
    }
    
    // Update logical position and return
    m_logicalPosition = nOffset;
    return nOffset;
}

u64 CMDSFileDevice::GetSize(void) const {
    if (!m_pFile || !m_parser) {
        LOGERR("GetSize !m_pFile or !m_parser");
        return 0;
    }

    // Calculate total logical disc size from all tracks
    // Note: We return LOGICAL size (what the OS sees), not physical file size
    // Physical MDF files have 2448-byte sectors (2352 + 96 subchannel)
    // but we report 2352-byte logical sectors to the OS
    u64 total_sectors = 0;
    
    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* track = m_parser->getTrack(i, j);
            
            // Skip non-track entries (lead-in, lead-out markers)
            if (track->point == 0 || track->point >= 0xA0) {
                continue;
            }
            
            MDS_TrackExtraBlock* extra = m_parser->getTrackExtra(i, j);
            if (extra) {
                // Each track contributes its pregap + data length
                total_sectors += extra->pregap + extra->length;
            }
        }
    }
    
    LOGDBG("GetSize: calculated %llu logical sectors, returning %llu bytes", 
           (unsigned long long)total_sectors, (unsigned long long)(total_sectors * 2352));
    
    // Return size in bytes (sectors × 2352 bytes per logical sector)
    return total_sectors * 2352;
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
                    // Return logical track start (includes pregap)
                    MDS_TrackExtraBlock* extra = m_parser->getTrackExtra(i, j);
                    u32 pregap = extra ? extra->pregap : 0;
                    u32 logical_start = trackBlock->start_sector + pregap;
                    LOGDBG("GetTrackStart(%d): physical start=%u, pregap=%u, logical start=%u",
                           track, trackBlock->start_sector, pregap, logical_start);
                    return logical_start;
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
                    u32 length = extra ? extra->length : 0;
                    LOGDBG("GetTrackLength(%d): length=%u (pregap not included)", track, length);
                    return length;
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
                    return trackBlock->mode == 0xA9; // 0xA9 = audio mode
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
            u32 pregap = extra ? extra->pregap : 0;
            u32 track_length = extra ? extra->length : 0;
            
            // The track's logical extent includes both pregap and data
            // For a track with start_sector=0, pregap=150, length=236198:
            //   - Logical start: 0 (pregap begins)
            //   - Data start: 150 (where file data begins) 
            //   - Logical end: 0 + 150 + 236198 = 236348
            u32 logical_start = track->start_sector;
            u32 logical_end = track->start_sector + pregap + track_length;
            
            if (lba >= logical_start && lba < logical_end) {
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
    
    // Get pregap for this track
    MDS_TrackExtraBlock* extra = m_parser->getTrackExtra(session, trackIdx);
    u32 pregap = extra ? extra->pregap : 0;
    u32 data_start_lba = track->start_sector + pregap;
    
    // If LBA is in pregap, there's no physical subchannel data in the file
    if (lba < data_start_lba) {
        LOGDBG("ReadSubchannel: LBA %u is in pregap, no subchannel data", lba);
        return -1;
    }
    
    // Calculate offset into the MDF file
    // LBA is absolute, data_start_lba is where file data begins
    u32 sectors_from_data_start = lba - data_start_lba;
    u64 sector_offset = track->start_offset + (sectors_from_data_start * track->sector_size);
    
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