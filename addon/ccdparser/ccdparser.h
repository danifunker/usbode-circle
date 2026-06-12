//
// CloneCD .ccd control file parser
//
// Parses the INI-style CCD text into TOC entries ([Entry n]) and track
// details ([TRACK n]). Pure code with no Circle/FatFs dependencies so it
// can be unit tested on the host.
//
// Format reference: libmirage image-ccd parser (cdemu).
//
// Copyright (C) 2026 Dani Sarfati
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
#pragma once

#include <stddef.h>
#include <stdint.h>

struct CCD_Entry {        // [Entry n] - raw TOC entry
    int session = 0;
    int point = 0;        // 1..99 = track, 0xA0/0xA1/0xA2 = lead descriptors
    int adr = 1;
    int control = 0;      // bit 2 set = data track
    int32_t alba = 0;
    int32_t plba = 0;     // for tracks: absolute LBA of INDEX 01 (MSF - 150)
    int pmin = 0, psec = 0, pframe = 0;
};

struct CCD_Track {        // [TRACK n] - track details
    int number = 0;
    int mode = -1;        // 0 audio, 1 mode1, 2 mode2; -1 = absent (unreliable in the wild)
    int32_t index0 = -1;  // img-file-relative sector of INDEX 00, -1 = absent
    int32_t index1 = -1;  // img-file-relative sector of INDEX 01, -1 = absent
    char isrc[13] = {0};
};

class CCDParser {
   public:
    static const int MaxEntries = 128;
    static const int MaxTracks = 99;

    // The text must remain valid only during construction (values are copied)
    CCDParser(const char *ccd_text);

    bool isValid() const { return m_valid; }

    int getNumSessions() const { return m_numSessions; }
    bool isDataScrambled() const { return m_dataScrambled; }
    const char *getCatalog() const { return m_catalog[0] ? m_catalog : nullptr; }

    int getNumEntries() const { return m_numEntries; }
    const CCD_Entry *getEntry(int index) const;

    // Track details from [TRACK n], or nullptr if that section is absent
    const CCD_Track *getTrack(int trackNumber) const;

   private:
    void parse(const char *text);

    CCD_Entry m_entries[MaxEntries];
    int m_numEntries = 0;
    CCD_Track m_tracks[MaxTracks];
    int m_numTracks = 0;

    int m_numSessions = 1;
    bool m_dataScrambled = false;
    char m_catalog[14] = {0};
    bool m_valid = false;
};
