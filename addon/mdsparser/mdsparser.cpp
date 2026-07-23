#include "mdsparser.h"
#include <stdlib.h>
#include <cstring>

// Convert a UTF-16LE name to UTF-8. Both ends are bounded: src_bytes is what
// is left of the .mds buffer, because a name near the end of a truncated file
// has no terminator to find, and dst_size is the fixed destination. The
// source is read a byte at a time rather than through a uint16_t* because
// filename_offset is an arbitrary file offset with no alignment guarantee.
// Returns false and leaves nothing usable behind if the name does not fit.
static bool utf16_to_utf8(const char* src, size_t src_bytes, char* dst, size_t dst_size) {
    size_t i = 0;  // bytes consumed from src
    size_t j = 0;  // bytes written to dst

    auto unit_at = [src](size_t byte_index) -> uint32_t {
        return (uint32_t)(uint8_t)src[byte_index] |
               ((uint32_t)(uint8_t)src[byte_index + 1] << 8);
    };

    while (i + 2 <= src_bytes) {
        uint32_t codepoint = unit_at(i);
        i += 2;
        if (codepoint == 0) {
            dst[j] = '\0';
            return true;
        }

        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            // Surrogate pair: the low half has to be there too.
            if (i + 2 > src_bytes) {
                return false;
            }
            uint32_t low_surrogate = unit_at(i);
            i += 2;
            codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low_surrogate - 0xDC00));
        }

        // Space for this codepoint's bytes plus the terminator.
        size_t need = codepoint < 0x80 ? 1 : codepoint < 0x800 ? 2 : codepoint < 0x10000 ? 3 : 4;
        if (j + need + 1 > dst_size) {
            return false;
        }

        if (codepoint < 0x80) {
            dst[j++] = (char)codepoint;
        } else if (codepoint < 0x800) {
            dst[j++] = (char)(0xC0 | (codepoint >> 6));
            dst[j++] = (char)(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            dst[j++] = (char)(0xE0 | (codepoint >> 12));
            dst[j++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            dst[j++] = (char)(0x80 | (codepoint & 0x3F));
        } else {
            dst[j++] = (char)(0xF0 | (codepoint >> 18));
            dst[j++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
            dst[j++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            dst[j++] = (char)(0x80 | (codepoint & 0x3F));
        }
    }

    return false;  // ran out of source without finding the terminator
}

bool MDSParser::canRead(uint64_t offset, uint64_t bytes) const {
    return offset <= m_mds_size && bytes <= (uint64_t)m_mds_size - offset;
}

MDSParser::MDSParser(const char *mds_file, size_t mds_size) {
    m_mds_file = mds_file;
    m_mds_size = mds_size;

    // Nothing below may touch a byte outside the buffer. The offsets in an
    // MDS are absolute file offsets, so a truncated or hand-edited image can
    // aim them anywhere; the browser lists every .mds on the card, which
    // means any file a user renames reaches this parser.
    if (!canRead(0, sizeof(MDS_Header))) {
        return;
    }
    memcpy(&m_header, mds_file, sizeof(MDS_Header));

    if (memcmp(m_header.signature, "MEDIA DESCRIPTOR", 16) != 0) {
        return;
    }

    // A CD has one session; a badly mastered one has a handful. This is a
    // sanity bound on the allocation that follows, not the range check.
    if (m_header.num_sessions == 0 || m_header.num_sessions > 100) {
        return;
    }
    if (!canRead(m_header.sessions_blocks_offset,
                 (uint64_t)sizeof(MDS_SessionBlock) * m_header.num_sessions)) {
        return;
    }

    // Parse sessions
    m_sessions = new MDS_SessionBlock[m_header.num_sessions];
    memcpy(m_sessions, mds_file + m_header.sessions_blocks_offset, sizeof(MDS_SessionBlock) * m_header.num_sessions);

    // Parse tracks. m_num_sessions is set only once the per-session arrays
    // exist, so an early return below leaves the destructor with nothing to
    // walk rather than a null pointer to index.
    m_tracks = new MDS_TrackBlock*[m_header.num_sessions]();
    m_track_extras = new MDS_TrackExtraBlock*[m_header.num_sessions]();
    m_num_sessions = m_header.num_sessions;

    for (int i = 0; i < m_num_sessions; i++) {
        // num_all_blocks counts the lead-in descriptors (points A0/A1/A2) as
        // well as the tracks, so a full 99-track Red Book disc has 102. It is
        // a uint8_t, and the size check below is what actually bounds it.
        if (m_sessions[i].num_all_blocks == 0
            || !canRead(m_sessions[i].tracks_blocks_offset,
                        (uint64_t)sizeof(MDS_TrackBlock) * m_sessions[i].num_all_blocks)) {
            return;
        }

        m_tracks[i] = new MDS_TrackBlock[m_sessions[i].num_all_blocks];
        memcpy(m_tracks[i], mds_file + m_sessions[i].tracks_blocks_offset, sizeof(MDS_TrackBlock) * m_sessions[i].num_all_blocks);

        // Value-initialized: a block with no extra of its own (every lead-in
        // descriptor) must read back as a zero pregap and length, not as
        // whatever was left on the heap.
        m_track_extras[i] = new MDS_TrackExtraBlock[m_sessions[i].num_all_blocks]();
        for (int j = 0; j < m_sessions[i].num_all_blocks; j++) {
            uint32_t extra_offset = m_tracks[i][j].extra_offset;
            if (extra_offset == 0) {
                continue;
            }
            if (!canRead(extra_offset, sizeof(MDS_TrackExtraBlock))) {
                return;
            }
            memcpy(&m_track_extras[i][j], mds_file + extra_offset, sizeof(MDS_TrackExtraBlock));
        }
    }

    // The name of the data file lives in a footer the track blocks point at.
    // Lead-in descriptors carry no footer, so this walks session 0 until it
    // finds one that resolves to a usable name.
    for (int i = 0; i < m_sessions[0].num_all_blocks && m_mdf_filename == nullptr; i++) {
        uint32_t footer_offset = m_tracks[0][i].footer_offset;
        if (footer_offset == 0 || !canRead(footer_offset, sizeof(MDS_Footer))) {
            continue;
        }

        MDS_Footer footer;
        memcpy(&footer, mds_file + footer_offset, sizeof(MDS_Footer));
        if (footer.filename_offset == 0 || footer.filename_offset >= m_mds_size) {
            continue;
        }

        const char* name = mds_file + footer.filename_offset;
        size_t avail = m_mds_size - footer.filename_offset;

        if (footer.widechar_filename) {
            if (utf16_to_utf8(name, avail, m_mdf_filename_utf8, sizeof(m_mdf_filename_utf8))) {
                m_mdf_filename = m_mdf_filename_utf8;
            }
        } else if (memchr(name, '\0', avail) != nullptr) {
            // Already a C string, and it terminates inside the file.
            m_mdf_filename = name;
        }
    }

    // Without a data file there is nothing to read, whatever else parsed.
    m_valid = (m_mdf_filename != nullptr);
}

MDSParser::~MDSParser() {
    delete[] m_sessions;
    for (int i = 0; i < m_num_sessions; i++) {
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