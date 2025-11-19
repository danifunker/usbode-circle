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
    m_mds_filename(mds_filename),
    m_mds_str(mds_str),
    m_mediaType(mediaType)
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

    // Open MDF file
    const char* mdf_filename_from_mds = m_parser->getMDFilename();
    LOGNOTE("MDF filename from parser: %s", mdf_filename_from_mds);
    char mdf_path[255];
    char mdf_filename[255];  // For CUE sheet - just the filename without path

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

    // Generate CUE sheet from MDS data
    char cue_buffer[4096];
    char* cue_ptr = cue_buffer;
    int remaining = sizeof(cue_buffer);

    // Use the resolved filename (not wildcard) in the CUE sheet
    int len = snprintf(cue_ptr, remaining, "FILE \"%s\" BINARY\n", mdf_filename);
    cue_ptr += len;
    remaining -= len;

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

            // Only process actual track entries (point is the track number)
            // Skip lead-in entries (point 0xA0, 0xA1, 0xA2)
            if (track->point == 0 || track->point >= 0xA0) {
                continue;
            }

            // Determine track mode string
            const char* mode_str;
            if (track->mode == 0xA9) {
                // Audio track
                mode_str = "AUDIO";
            } else {
                // Data track - use MODE1/2352 for raw sector data
                mode_str = "MODE1/2352";
            }

            len = snprintf(cue_ptr, remaining, "  TRACK %02d %s\n", track->point, mode_str);
            cue_ptr += len;
            remaining -= len;

            // Add PREGAP if present
            if (extra && extra->pregap > 0) {
                LOGNOTE("    pregap: %u", extra->pregap);
                LOGNOTE("    length: %u", extra->length);
                int minutes = extra->pregap / (75 * 60);
                int seconds = (extra->pregap / 75) % 60;
                int frames = extra->pregap % 75;
                len = snprintf(cue_ptr, remaining, "    PREGAP %02d:%02d:%02d\n", minutes, seconds, frames);
                cue_ptr += len;
                remaining -= len;
            }

            // Add INDEX 01 with the track's start position
            // start_sector is in LBA (logical block address) format
            int minutes = track->start_sector / (75 * 60);
            int seconds = (track->start_sector / 75) % 60;
            int frames = track->start_sector % 75;
            len = snprintf(cue_ptr, remaining, "    INDEX 01 %02d:%02d:%02d\n", minutes, seconds, frames);
            cue_ptr += len;
            remaining -= len;
        }
    }

    m_cue_sheet = new char[strlen(cue_buffer) + 1];
    strcpy(m_cue_sheet, cue_buffer);
    
    LOGNOTE("Generated CUE sheet:\n%s", m_cue_sheet);
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
        LOGERR("Seek: LBA %u not found in any track", lba);
        return static_cast<u64>(-1);
    }
    
    // Calculate offset into MDF file
    u32 sectors_from_track_start = lba - track->start_sector;
    u64 actual_file_offset = track->start_offset + 
                             (sectors_from_track_start * track->sector_size) + 
                             offset_in_sector;
    
    LOGDBG("Seek: LBA %u (offset %llu) -> track %d, file offset %llu", 
           lba, nOffset, track->point, actual_file_offset);
    
    FRESULT result = f_lseek(m_pFile, actual_file_offset);
    if (result != FR_OK) {
        LOGERR("Seek to file offset %llu failed, err %d", actual_file_offset, result);
        return static_cast<u64>(-1);
    }
    
    // Return the logical offset that was requested (not the physical file offset)
    return nOffset;
}

u64 CMDSFileDevice::GetSize(void) const {
    if (!m_pFile) {
        LOGERR("GetSize !m_pFile");
        return 0;
    }

    u64 size = f_size(m_pFile);
    if (size < 0) {
        LOGERR("GetSize f_size < 0");
        return 0;
    }

    return size;
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