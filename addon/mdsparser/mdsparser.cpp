#include "mdsparser.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
    m_tracks = new MDS_TrackBlock*[m_header.num_sessions];
    m_track_extras = new MDS_TrackExtraBlock*[m_header.num_sessions];
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

    // Get MDF filename
    if (m_header.num_sessions > 0 && m_sessions[0].num_all_blocks > 0 && m_tracks[0][0].footer_offset > 0 && m_tracks[0][0].footer_offset < 0x100000) {
        MDS_Footer* footer = (MDS_Footer*)(mds_file + m_tracks[0][0].footer_offset);
        m_mdf_filename = (char*)(mds_file + footer->filename_offset);
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
