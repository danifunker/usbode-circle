//
// CD subchannel helpers: Q CRC, P-W (de)interleaving, Q/P-W synthesis
//
// Pure functions with no Circle/FatFs dependencies so they can be unit
// tested on the host. Format references: Red Book / ECMA-130 (Q channel,
// CRC, interleaving); MMC-3 (READ CD sub-channel selections).
//
// Copyright (C) 2026 Dani Sarfati
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
#ifndef _DISCIMAGE_SUBCHANNEL_H
#define _DISCIMAGE_SUBCHANNEL_H

#include <stdint.h>

// CRC-16/CCITT (poly x^16+x^12+x^5+1, init 0, no reflection) over len bytes,
// returned COMPLEMENTED as stored in the Q channel (ECMA-130 22.3.4).
uint16_t SubQCRC16(const uint8_t *data, int len);

// Convert between the raw interleaved P-W block (96 symbols, bit 7 = P,
// bit 6 = Q, ... bit 0 = W) and the linear layout (12 bytes P, 12 bytes Q,
// ... 12 bytes W; MSB of each byte is the earliest symbol).
void InterleavePW96(const uint8_t linear[96], uint8_t raw[96]);
void DeinterleavePW96(const uint8_t raw[96], uint8_t linear[96]);

// Inputs for synthesizing a Q position packet (mode 1 / ADR 1)
struct QSynthInfo {
    uint8_t control;  // raw control nibble: 0x00 audio, 0x04 data
    uint8_t tno;      // track number (binary, converted to BCD here)
    uint8_t index;    // 0 in pregap, 1 in the data area
    uint32_t rel_lba; // frames relative to INDEX 01 (counts down in pregap)
    uint32_t abs_lba; // absolute LBA (MSF encoded with the +150 offset)
};

// Build the 12-byte raw Q packet: CONTROL/ADR, TNO, INDEX, relative MSF,
// ZERO, absolute MSF (all BCD), complemented CRC16.
void BuildQ12(const QSynthInfo &info, uint8_t q[12]);

// Build a full 96-byte raw interleaved P-W block: P solid during pregap
// (index 0) else zero, synthesized Q, R-W zero.
void BuildRawPW96(const QSynthInfo &info, uint8_t raw[96]);

#endif
