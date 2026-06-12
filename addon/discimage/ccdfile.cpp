//
// A CDevice for CloneCD (CCD/IMG/SUB) images
//
// Copyright (C) 2026 Dani Sarfati
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
#include "ccdfile.h"
#include "subchannel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LOGMODULE("CCCDFileDevice");

CCCDFileDevice::CCCDFileDevice(const char* ccdPath, const char* ccd_str, MEDIA_TYPE mediaType)
    : CCueBinFileDevice(ccdPath, nullptr, mediaType)
{
    strncpy(m_CcdPath, ccdPath, sizeof(m_CcdPath) - 1);
    m_CcdPath[sizeof(m_CcdPath) - 1] = '\0';

    if (ccd_str != nullptr) {
        size_t len = strlen(ccd_str);
        m_CcdText = new char[len + 1];
        strcpy(m_CcdText, ccd_str);
    }
}

CCCDFileDevice::~CCCDFileDevice(void)
{
    if (m_pSubFile) {
        f_close(m_pSubFile);
        delete m_pSubFile;
        m_pSubFile = nullptr;
    }
    delete[] m_CcdText;
    delete m_Parser;
}

// Replace the extension of path (which must end in a 3-char extension)
static void SwapExtension(char* path, const char* ext3)
{
    size_t len = strlen(path);
    if (len >= 3)
        memcpy(path + len - 3, ext3, 3);
}

bool CCCDFileDevice::Init()
{
    if (m_CcdText == nullptr) {
        LOGERR("No CCD text");
        return false;
    }

    m_Parser = new CCDParser(m_CcdText);
    if (!m_Parser->isValid()) {
        LOGERR("Invalid CCD file");
        return false;
    }
    if (m_Parser->isDataScrambled()) {
        LOGERR("DataTracksScrambled CCD images are not supported");
        return false;
    }

    if (!BuildSourceCue())
        return false;

    if (!CCueBinFileDevice::Init())
        return false;

    // Sanity check: track LBAs reconstructed via the standard session gap
    // must match the absolute PLBA values from the [Entry] blocks
    for (int i = 0; i < m_Parser->getNumEntries(); i++) {
        const CCD_Entry* e = m_Parser->getEntry(i);
        if (e->point < 1 || e->point > 99)
            continue;
        for (int t = 0; t < m_nTracks; t++) {
            if (m_Tracks[t].track_number == e->point &&
                m_Tracks[t].data_start != (u32)e->plba) {
                LOGWARN("Track %d: computed LBA %u != CCD PLBA %d "
                        "(non-standard session gap?)",
                        e->point, m_Tracks[t].data_start, e->plba);
            }
        }
    }

    // Optional subchannel file
    char subPath[512];
    strncpy(subPath, m_CcdPath, sizeof(subPath) - 1);
    subPath[sizeof(subPath) - 1] = '\0';
    SwapExtension(subPath, "sub");

    m_pSubFile = new FIL();
    if (f_open(m_pSubFile, subPath, FA_READ) != FR_OK) {
        LOGNOTE("No .sub file (%s): subchannel data will be synthesized", subPath);
        delete m_pSubFile;
        m_pSubFile = nullptr;
    } else {
        LOGNOTE("Opened subchannel file: %s (%llu bytes)",
                subPath, (unsigned long long)f_size(m_pSubFile));
    }

    return true;
}

bool CCCDFileDevice::BuildSourceCue()
{
    // The img filename for the FILE line: basename of the ccd with .img
    const char* lastSlash = strrchr(m_CcdPath, '/');
    const char* base = lastSlash ? lastSlash + 1 : m_CcdPath;
    char imgName[sizeof(m_CcdPath)];
    snprintf(imgName, sizeof(imgName), "%s", base);
    SwapExtension(imgName, "img");

    size_t bufSize = 256 + (size_t)m_Parser->getNumEntries() * 140;
    char* buf = new char[bufSize];
    char* p = buf;
    size_t remaining = bufSize;
    int len;

    if (m_Parser->getCatalog() != nullptr) {
        len = snprintf(p, remaining, "CATALOG %s\n", m_Parser->getCatalog());
        p += len;
        remaining -= len;
    }

    len = snprintf(p, remaining, "FILE \"%s\" BINARY\n", imgName);
    p += len;
    remaining -= len;

    // Per-session lead-out start LBAs from the [Entry] point 0xA2 blocks
    // (entries appear in TOC order, lead descriptors before the tracks)
    static const int MaxSessions = 99;
    int32_t leadoutPLBA[MaxSessions + 1];
    for (int s = 0; s <= MaxSessions; s++)
        leadoutPLBA[s] = -1;
    for (int i = 0; i < m_Parser->getNumEntries(); i++) {
        const CCD_Entry* e = m_Parser->getEntry(i);
        if (e->point == 0xA2 && e->session >= 1 && e->session <= MaxSessions)
            leadoutPLBA[e->session] = e->plba;
    }

    // Fallback img positions for CCDs without [TRACK] sections: the img
    // stores each session's program area contiguously
    int session = 0;
    int32_t sessionFirstPLBA = 0;
    int32_t storedCursor = 0;     // img sector where the current session starts

    for (int i = 0; i < m_Parser->getNumEntries() && remaining > 130; i++) {
        const CCD_Entry* e = m_Parser->getEntry(i);

        if (e->point < 1 || e->point > 99)
            continue;

        if (e->session != session) {
            if (session != 0 && leadoutPLBA[session] >= 0)
                storedCursor += leadoutPLBA[session] - sessionFirstPLBA;
            session = e->session;
            sessionFirstPLBA = e->plba;

            len = snprintf(p, remaining, "REM SESSION %02d\n", session);
            p += len;
            remaining -= len;
        }

        const CCD_Track* td = m_Parser->getTrack(e->point);

        // Track type: the Control nibble decides audio vs data (the [TRACK]
        // MODE value is unreliable in the wild, use it only for Mode 1 vs 2)
        const char* type;
        if (e->control & 0x04)
            type = (td != nullptr && td->mode == 2) ? "MODE2/2352" : "MODE1/2352";
        else
            type = "AUDIO";

        // img-relative sector of INDEX 01: from the [TRACK] section when
        // present, otherwise derived from the PLBA within this session
        int32_t index1 = (td != nullptr && td->index1 >= 0)
                             ? td->index1
                             : storedCursor + (e->plba - sessionFirstPLBA);
        int32_t index0 = (td != nullptr && td->index0 >= 0) ? td->index0 : index1;

        len = snprintf(p, remaining, "  TRACK %02d %s\n", e->point, type);
        p += len;
        remaining -= len;

        if (td != nullptr && td->isrc[0] != '\0') {
            len = snprintf(p, remaining, "    ISRC %s\n", td->isrc);
            p += len;
            remaining -= len;
        }

        if (index0 < index1) {
            len = snprintf(p, remaining, "    INDEX 00 %02d:%02d:%02d\n",
                           index0 / (75 * 60), (index0 / 75) % 60, index0 % 75);
            p += len;
            remaining -= len;
        }

        len = snprintf(p, remaining, "    INDEX 01 %02d:%02d:%02d\n",
                       index1 / (75 * 60), (index1 / 75) % 60, index1 % 75);
        p += len;
        remaining -= len;
    }

    // Hand the synthesized cue to the base class parser
    delete[] m_SourceCue;
    m_SourceCue = buf;
    m_FileType = FileType::CUEBIN; // base Init takes the multi-file path

    LOGNOTE("Synthesized cue from CCD:\n%s", m_SourceCue);
    return true;
}

int CCCDFileDevice::ReadSubchannel(u32 lba, u8* subchannel)
{
    if (m_pSubFile == nullptr || subchannel == nullptr)
        return -1;

    int fileIdx;
    u64 fileOffset;
    if (!LBAToFileOffset(lba, &fileIdx, &fileOffset))
        return -1;

    // One 96-byte LINEAR P-W block per 2352-byte img sector
    u64 subOffset = (fileOffset / 2352) * 96;

    if (f_lseek(m_pSubFile, subOffset) != FR_OK) {
        LOGERR("Failed to seek .sub to %llu", (unsigned long long)subOffset);
        return -1;
    }

    u8 linear[96];
    UINT got = 0;
    if (f_read(m_pSubFile, linear, 96, &got) != FR_OK || got != 96) {
        LOGERR("Failed to read .sub at %llu", (unsigned long long)subOffset);
        return -1;
    }

    InterleavePW96(linear, subchannel);
    return 96;
}
