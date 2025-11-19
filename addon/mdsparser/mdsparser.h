#pragma once

#include <stddef.h>
#include <stdint.h>

#pragma pack(push, 1)

typedef struct
{
    char signature[16];
    uint8_t version[2];
    uint16_t medium_type;
    uint16_t num_sessions;
    uint16_t __dummy1__[2];
    uint16_t bca_len;
    uint32_t __dummy2__[2];
    uint32_t bca_data_offset;
    uint32_t __dummy3__[6];
    uint32_t disc_structures_offset;
    uint32_t __dummy4__[3];
    uint32_t sessions_blocks_offset;
    uint32_t dpm_blocks_offset;
} MDS_Header;

typedef struct
{
    int32_t session_start;
    int32_t session_end;
    uint16_t session_number;
    uint8_t num_all_blocks;
    uint8_t num_nontrack_blocks;
    uint16_t first_track;
    uint16_t last_track;
    uint32_t __dummy1__;
    uint32_t tracks_blocks_offset;
} MDS_SessionBlock;

typedef struct
{
    uint8_t mode;
    uint8_t subchannel;
    uint8_t adr_ctl;
    uint8_t tno;
    uint8_t point;
    uint8_t min;
    uint8_t sec;
    uint8_t frame;
    uint8_t zero;
    uint8_t pmin;
    uint8_t psec;
    uint8_t pframe;
    uint32_t extra_offset;
    uint16_t sector_size;
    uint8_t __dummy4__[18];
    uint32_t start_sector;
    uint64_t start_offset;
    uint32_t number_of_files;
    uint32_t footer_offset;
    uint8_t __dummy6__[24];
} MDS_TrackBlock;

typedef struct
{
    uint32_t pregap;
    uint32_t length;
} MDS_TrackExtraBlock;

typedef struct
{
    uint32_t filename_offset;
    uint32_t widechar_filename;
    uint32_t __dummy1__;
    uint32_t __dummy2__;
} MDS_Footer;

#pragma pack(pop)

void utf16_to_utf8(const uint16_t* utf16_string, char* utf8_string, int utf8_string_size);

class MDSParser {
   public:
    MDSParser(const char *mds_file);
    ~MDSParser();
    bool isValid();
    const char* getMDFilename();
    int getNumSessions();
    MDS_SessionBlock* getSession(int index);
    MDS_TrackBlock* getTrack(int session, int track);
    MDS_TrackExtraBlock* getTrackExtra(int session, int track);


   private:
    MDS_Header m_header;
    MDS_SessionBlock* m_sessions = nullptr;
    MDS_TrackBlock** m_tracks = nullptr;
    MDS_TrackExtraBlock** m_track_extras = nullptr;
    const char* m_mdf_filename = nullptr;
    char m_mdf_filename_utf8[256];
    bool m_valid;
    const char* m_mds_file;
};
