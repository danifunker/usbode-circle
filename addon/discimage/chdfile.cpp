#include "chdfile.h"
#include <circle/logger.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include <libchdr/chd.h>
}

#define CD_FRAMESIZE_RAW 2352
#define CUE_METADATA_TRACK_FORMAT "CDTRACK%d"
#define CUE_METADATA_TRACK_TYPE "TYPE"
#define CUE_METADATA_TRACK_SUBTYPE "SUBTYPE"
#define CUE_METADATA_TRACK_FRAMES "FRAMES"
#define CUE_METADATA_TRACK_PREGAP "PREGAP"

CChdFileDevice::CChdFileDevice(const char* chd_filename) :
    m_pFile(nullptr),
    m_chd(nullptr),
    m_cue_sheet(nullptr),
    m_chd_filename(chd_filename),
    m_ullOffset(0)
{
}

CChdFileDevice::~CChdFileDevice(void)
{
    if (m_chd) {
        chd_close(m_chd);
    }
    if (m_pFile) {
        f_close(m_pFile);
        delete m_pFile;
    }
    if (m_cue_sheet) {
        free(m_cue_sheet);
    }
}

bool CChdFileDevice::Init()
{
    m_pFile = new FIL();
    if (f_open(m_pFile, m_chd_filename, FA_READ) != FR_OK) {
        CLogger::Get()->Write("CChdFileDevice", LogError, "Failed to open CHD file: %s", m_chd_filename);
        return false;
    }

    chd_error err = chd_open_file(m_pFile, CHD_OPEN_READ, nullptr, &m_chd);
    if (err != CHDERR_NONE) {
        CLogger::Get()->Write("CChdFileDevice", LogError, "Failed to open CHD: %d", err);
        return false;
    }

    // Generate CUE sheet
    const chd_header* header = chd_get_header(m_chd);
    u32 num_tracks = GetNumTracks();

    if (num_tracks > 0)
    {
        // Allocate a large buffer for the CUE sheet. 4k should be enough.
        m_cue_sheet = (char*)malloc(4096);
        char* cue_ptr = m_cue_sheet;
        cue_ptr += sprintf(cue_ptr, "FILE \"%s\" BINARY\n", m_chd_filename);

        for (u32 i = 0; i < num_tracks; i++) {
            char metadata_tag[256];
            snprintf(metadata_tag, sizeof(metadata_tag), CUE_METADATA_TRACK_FORMAT, i + 1);
            
            u8 buffer[256];
            u32 track_type = 0;
            u32 track_subtype = 0;

            chd_get_metadata_value(m_chd, metadata_tag, CUE_METADATA_TRACK_TYPE, buffer, sizeof(buffer));
            sscanf((char*)buffer, "%d", &track_type);

            chd_get_metadata_value(m_chd, metadata_tag, CUE_METADATA_TRACK_SUBTYPE, buffer, sizeof(buffer));
            sscanf((char*)buffer, "%d", &track_subtype);

            const char* track_mode = "MODE1/2352";
            if (track_type == 0) { // Data
                if (track_subtype == 0) { // MODE1/2352
                    track_mode = "MODE1/2352";
                } else { // MODE2/2352
                    track_mode = "MODE2/2352";
                }
            } else { // Audio
                track_mode = "AUDIO";
            }
            
            u32 pregap = 0;
            if (chd_get_metadata_value(m_chd, metadata_tag, CUE_METADATA_TRACK_PREGAP, buffer, sizeof(buffer)) == CHDERR_NONE)
            {
                sscanf((char*)buffer, "%d", &pregap);
            }

            cue_ptr += sprintf(cue_ptr, "  TRACK %02d %s\n", i + 1, track_mode);
            if (pregap > 0)
            {
                cue_ptr += sprintf(cue_ptr, "    PREGAP %02d:%02d:%02d\n", pregap / 75 / 60, (pregap / 75) % 60, pregap % 75);
            }
            cue_ptr += sprintf(cue_ptr, "    INDEX 01 %02d:%02d:%02d\n", GetTrackStart(i) / 75 / 60, (GetTrackStart(i) / 75) % 60, GetTrackStart(i) % 75);
        }
    }


    return true;
}

int CChdFileDevice::Read(void* pBuffer, size_t nCount)
{
    u32 hunk_size = chd_get_header(m_chd)->hunkbytes;
    u32 lba = m_ullOffset / CD_FRAMESIZE_RAW;
    u32 hunk = lba / (hunk_size / CD_FRAMESIZE_RAW);
    u32 hunk_offset = lba % (hunk_size / CD_FRAMESIZE_RAW);

    u32 bytes_to_read = nCount;
    u32 bytes_read = 0;

    while (bytes_to_read > 0) {
        u32 hunk_bytes_remaining = (hunk_size / CD_FRAMESIZE_RAW - hunk_offset) * CD_FRAMESIZE_RAW;
        u32 read_count = bytes_to_read < hunk_bytes_remaining ? bytes_to_read : hunk_bytes_remaining;

        u8 hunk_buffer[hunk_size];
        chd_error err = chd_read(m_chd, hunk, hunk_buffer);
        if (err != CHDERR_NONE) {
            CLogger::Get()->Write("CChdFileDevice", LogError, "Failed to read hunk %d: %d", hunk, err);
            return -1;
        }

        memcpy((u8*)pBuffer + bytes_read, hunk_buffer + hunk_offset * CD_FRAMESIZE_RAW, read_count);

        bytes_read += read_count;
        bytes_to_read -= read_count;
        m_ullOffset += read_count;
        lba = m_ullOffset / CD_FRAMESIZE_RAW;
        hunk = lba / (hunk_size / CD_FRAMESIZE_RAW);
        hunk_offset = lba % (hunk_size / CD_FRAMESIZE_RAW);
    }

    return bytes_read;
}

int CChdFileDevice::Write(const void* pBuffer, size_t nCount)
{
    return -1; // Not supported
}

u64 CChdFileDevice::Seek(u64 ullOffset)
{
    m_ullOffset = ullOffset;
    return m_ullOffset;
}

u64 CChdFileDevice::GetSize(void) const
{
    return chd_get_header(m_chd)->logicalbytes;
}

u64 CChdFileDevice::Tell() const
{
    return m_ullOffset;
}

int CChdFileDevice::GetNumTracks() const
{
    u32 num_tracks = 0;
    for (int i = 0; i < 100; i++) {
        char metadata_tag[256];
        snprintf(metadata_tag, sizeof(metadata_tag), CUE_METADATA_TRACK_FORMAT, i + 1);
        chd_metadata_info track_info;
        chd_error err = chd_get_metadata_info(m_chd, metadata_tag, &track_info);
        if (err == CHDERR_METADATA_NOT_FOUND) {
            break;
        }
        num_tracks++;
    }
    return num_tracks;
}

u32 CChdFileDevice::GetTrackStart(int track) const
{
    u32 frames = 0;
    for (int i = 0; i < track; i++) {
        frames += GetTrackLength(i);
    }
    return frames;
}

u32 CChdFileDevice::GetTrackLength(int track) const
{
    char metadata_tag[256];
    snprintf(metadata_tag, sizeof(metadata_tag), CUE_METADATA_TRACK_FORMAT, track + 1);
    
    u8 buffer[256];
    u32 track_frames = 0;

    chd_get_metadata_value(m_chd, metadata_tag, CUE_METADATA_TRACK_FRAMES, buffer, sizeof(buffer));
    sscanf((char*)buffer, "%d", &track_frames);

    return track_frames;
}

bool CChdFileDevice::IsAudioTrack(int track) const
{
    char metadata_tag[256];
    snprintf(metadata_tag, sizeof(metadata_tag), CUE_METADATA_TRACK_FORMAT, track + 1);
    
    u8 buffer[256];
    u32 track_type = 0;

    chd_get_metadata_value(m_chd, metadata_tag, CUE_METADATA_TRACK_TYPE, buffer, sizeof(buffer));
    sscanf((char*)buffer, "%d", &track_type);

    return track_type != 0;
}

const char* CChdFileDevice::GetCueSheet() const
{
    return m_cue_sheet;
}
