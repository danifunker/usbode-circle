#include "mdsparser.h"
#include <stdlib.h>
#include <cstring>

void utf16_to_utf8(const uint16_t* utf16_string, char* utf8_string, int utf8_string_size) {
    int i = 0;
    int j = 0;
    while (utf16_string[i] != 0) {
        uint32_t codepoint = utf16_string[i];
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            // surrogate pair
            uint32_t high_surrogate = codepoint;
            uint32_t low_surrogate = utf16_string[++i];
            codepoint = 0x10000 + ((high_surrogate - 0xD800) << 10) | (low_surrogate - 0xDC00);
        }

        if (codepoint < 0x80) {
            if (j + 1 >= utf8_string_size) break;
            utf8_string[j++] = (char)codepoint;
        } else if (codepoint < 0x800) {
            if (j + 2 >= utf8_string_size) break;
            utf8_string[j++] = (char)(0xC0 | (codepoint >> 6));
            utf8_string[j++] = (char)(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            if (j + 3 >= utf8_string_size) break;
            utf8_string[j++] = (char)(0xE0 | (codepoint >> 12));
            utf8_string[j++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            utf8_string[j++] = (char)(0x80 | (codepoint & 0x3F));
        } else {
            if (j + 4 >= utf8_string_size) break;
            utf8_string[j++] = (char)(0xF0 | (codepoint >> 18));
            utf8_string[j++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
            utf8_string[j++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            utf8_string[j++] = (char)(0x80 | (codepoint & 0x3F));
        }
        i++;
    }
    utf8_string[j] = '\0';
}

MDSParser::MDSParser(const char *mds_file) {
    m_mds_file = mds_file;
    memcpy(&m_header, mds_file, sizeof(MDS_Header));
    m_valid = (memcmp(m_header.signature, "MEDIA DESCRIPTOR", 16) == 0);

    if (!m_valid) {
        return;
    }

    if (m_header.sessions_blocks_offset > 0x100000 || m_header.num_sessions > 100) {
        m_valid = false;
        return;
    }

    // Parse sessions
    m_sessions = new MDS_SessionBlock[m_header.num_sessions];
    memcpy(m_sessions, mds_file + m_header.sessions_blocks_offset, sizeof(MDS_SessionBlock) * m_header.num_sessions);

    // Parse tracks
    m_tracks = new MDS_TrackBlock*[m_header.num_sessions]();
    m_track_extras = new MDS_TrackExtraBlock*[m_header.num_sessions]();
    for (int i = 0; i < m_header.num_sessions; i++) {
        if (m_sessions[i].tracks_blocks_offset > 0x100000 || m_sessions[i].num_all_blocks > 100) {
            m_valid = false;
            return;
        }

        m_tracks[i] = new MDS_TrackBlock[m_sessions[i].num_all_blocks];
        memcpy(m_tracks[i], mds_file + m_sessions[i].tracks_blocks_offset, sizeof(MDS_TrackBlock) * m_sessions[i].num_all_blocks);

        m_track_extras[i] = new MDS_TrackExtraBlock[m_sessions[i].num_all_blocks];
        for (int j = 0; j < m_sessions[i].num_all_blocks; j++) {
            if (m_tracks[i][j].extra_offset > 0x100000) {
                m_valid = false;
                return;
            }

            if (m_tracks[i][j].extra_offset > 0) {
                memcpy(&m_track_extras[i][j], mds_file + m_tracks[i][j].extra_offset, sizeof(MDS_TrackExtraBlock));
            }
        }
    }

    // Get MDF filename by finding the first actual track
    m_mdf_filename = nullptr;
    if (m_header.num_sessions > 0) {
        for (int i = 0; i < m_sessions[0].num_all_blocks; i++) {
            MDS_TrackBlock* track = &m_tracks[0][i];

            if (track->footer_offset > 0 && track->footer_offset < 0x100000) {
                MDS_Footer* footer = (MDS_Footer*)(mds_file + track->footer_offset);
                if (footer && footer->filename_offset > 0 && footer->filename_offset < 0x100000) {
                    m_mdf_filename = (mds_file + footer->filename_offset);
                    break;
                }
            }
        }
    }

    if (m_mdf_filename != nullptr) {
        // The footer block is found by iterating tracks, but let's re-fetch it for clarity
        MDS_Footer* footer = nullptr;
        if (m_header.num_sessions > 0 && m_sessions[0].num_all_blocks > 0) {
            for (int i = 0; i < m_sessions[0].num_all_blocks; i++) {
                MDS_TrackBlock* track = &m_tracks[0][i];
                if (track->footer_offset > 0 && track->footer_offset < 0x100000) {
                    footer = (MDS_Footer*)(mds_file + track->footer_offset);
                    break;
                }
            }
        }

        if (footer && footer->widechar_filename) {
            utf16_to_utf8((const uint16_t*)m_mdf_filename, m_mdf_filename_utf8, sizeof(m_mdf_filename_utf8));
            m_mdf_filename = m_mdf_filename_utf8;
        }
        // If not widechar, it's already a standard C-string, so no conversion is needed.
    } else {
        m_valid = false;
    }
}

MDSParser::~MDSParser() {
    delete[] m_sessions;
    for (int i = 0; i < m_header.num_sessions; i++) {
        delete[] m_tracks[i];
        delete[] m_track_extras[i];
    }
    delete[] m_tracks;
    delete[] m_track_extras;
}

bool MDSParser::isValid() {
    return m_valid;
}

const char* MDSParser::getMDFilename() {
    return m_mdf_filename;
}

int MDSParser::getNumSessions() {
    return m_header.num_sessions;
}

MDS_SessionBlock* MDSParser::getSession(int index) {
    return &m_sessions[index];
}

MDS_TrackBlock* MDSParser::getTrack(int session, int track) {
    return &m_tracks[session][track];
}

MDS_TrackExtraBlock* MDSParser::getTrackExtra(int session, int track) {
    return &m_track_extras[session][track];
}
