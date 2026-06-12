//
// CloneCD .ccd control file parser
//
// Copyright (C) 2026 Dani Sarfati
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
#include "ccdparser.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

// Section being parsed
enum class Section { None, CloneCD, Disc, Session, Entry, Track, Other };

const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r')
        p++;
    return p;
}

// Case-insensitive "does line start with key, followed by '='" check;
// returns the value pointer (after '=') or nullptr.
const char *match_key(const char *line, const char *key)
{
    size_t klen = strlen(key);
    if (strncasecmp(line, key, klen) != 0)
        return nullptr;
    const char *p = skip_ws(line + klen);
    if (*p != '=')
        return nullptr;
    return skip_ws(p + 1);
}

// Values appear both decimal and hex (e.g. Point=0xa1) depending on writer
long parse_value(const char *v)
{
    return strtol(v, nullptr, 0);
}

} // namespace

CCDParser::CCDParser(const char *ccd_text)
{
    if (ccd_text != nullptr)
        parse(ccd_text);
}

void CCDParser::parse(const char *text)
{
    Section section = Section::None;
    CCD_Entry *entry = nullptr;
    CCD_Track *track = nullptr;
    bool sawCloneCD = false;

    const char *p = text;
    while (*p) {
        p = skip_ws(p);
        const char *lineEnd = strchr(p, '\n');

        if (*p == '[') {
            const char *name = p + 1;

            if (strncasecmp(name, "CloneCD", 7) == 0) {
                section = Section::CloneCD;
                sawCloneCD = true;
            } else if (strncasecmp(name, "Disc", 4) == 0) {
                section = Section::Disc;
            } else if (strncasecmp(name, "Session", 7) == 0) {
                section = Section::Session;
            } else if (strncasecmp(name, "Entry", 5) == 0) {
                section = Section::Entry;
                entry = (m_numEntries < MaxEntries) ? &m_entries[m_numEntries++] : nullptr;
            } else if (strncasecmp(name, "TRACK", 5) == 0) {
                section = Section::Track;
                track = (m_numTracks < MaxTracks) ? &m_tracks[m_numTracks++] : nullptr;
                if (track)
                    track->number = (int)strtol(name + 5, nullptr, 10);
            } else {
                section = Section::Other; // [CDText] etc.
            }
        } else {
            const char *v;
            switch (section) {
            case Section::Disc:
                if ((v = match_key(p, "Sessions")) != nullptr) {
                    int n = (int)parse_value(v);
                    if (n > 0)
                        m_numSessions = n;
                } else if ((v = match_key(p, "DataTracksScrambled")) != nullptr) {
                    m_dataScrambled = parse_value(v) != 0;
                } else if ((v = match_key(p, "CATALOG")) != nullptr) {
                    int n = 0;
                    while (n < 13 && v[n] >= '0' && v[n] <= '9')
                        n++;
                    if (n == 13) {
                        memcpy(m_catalog, v, 13);
                        m_catalog[13] = '\0';
                    }
                }
                break;

            case Section::Entry:
                if (entry == nullptr)
                    break;
                if ((v = match_key(p, "Session")) != nullptr)
                    entry->session = (int)parse_value(v);
                else if ((v = match_key(p, "Point")) != nullptr)
                    entry->point = (int)parse_value(v);
                else if ((v = match_key(p, "ADR")) != nullptr)
                    entry->adr = (int)parse_value(v);
                else if ((v = match_key(p, "Control")) != nullptr)
                    entry->control = (int)parse_value(v);
                else if ((v = match_key(p, "ALBA")) != nullptr)
                    entry->alba = (int32_t)parse_value(v);
                else if ((v = match_key(p, "PLBA")) != nullptr)
                    entry->plba = (int32_t)parse_value(v);
                else if ((v = match_key(p, "PMin")) != nullptr)
                    entry->pmin = (int)parse_value(v);
                else if ((v = match_key(p, "PSec")) != nullptr)
                    entry->psec = (int)parse_value(v);
                else if ((v = match_key(p, "PFrame")) != nullptr)
                    entry->pframe = (int)parse_value(v);
                break;

            case Section::Track:
                if (track == nullptr)
                    break;
                if ((v = match_key(p, "MODE")) != nullptr) {
                    track->mode = (int)parse_value(v);
                } else if ((v = match_key(p, "INDEX 0")) != nullptr) {
                    track->index0 = (int32_t)parse_value(v);
                } else if ((v = match_key(p, "INDEX 1")) != nullptr) {
                    track->index1 = (int32_t)parse_value(v);
                } else if ((v = match_key(p, "ISRC")) != nullptr) {
                    int n = 0;
                    while (n < 12 && v[n] != '\0' && v[n] != '\r' && v[n] != '\n')
                        n++;
                    if (n == 12) {
                        memcpy(track->isrc, v, 12);
                        track->isrc[12] = '\0';
                    }
                }
                break;

            default:
                break;
            }
        }

        if (lineEnd == nullptr)
            break;
        p = lineEnd + 1;
    }

    // Valid if it looks like a CCD and describes at least one track entry
    bool haveTrackEntry = false;
    for (int i = 0; i < m_numEntries; i++) {
        if (m_entries[i].point >= 1 && m_entries[i].point <= 99)
            haveTrackEntry = true;
    }
    m_valid = sawCloneCD && haveTrackEntry;
}

const CCD_Entry *CCDParser::getEntry(int index) const
{
    if (index < 0 || index >= m_numEntries)
        return nullptr;
    return &m_entries[index];
}

const CCD_Track *CCDParser::getTrack(int trackNumber) const
{
    for (int i = 0; i < m_numTracks; i++) {
        if (m_tracks[i].number == trackNumber)
            return &m_tracks[i];
    }
    return nullptr;
}
