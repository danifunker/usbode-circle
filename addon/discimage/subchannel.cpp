//
// CD subchannel helpers: Q CRC, P-W (de)interleaving, Q/P-W synthesis
//
// Copyright (C) 2026 Dani Sarfati
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
#include "subchannel.h"
#include <string.h>

uint16_t SubQCRC16(const uint8_t *data, int len)
{
    uint16_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return (uint16_t)~crc; // stored complemented
}

void DeinterleavePW96(const uint8_t raw[96], uint8_t linear[96])
{
    memset(linear, 0, 96);
    for (int channel = 0; channel < 8; channel++) {
        for (int i = 0; i < 12; i++) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; j++) {
                uint8_t symbol = raw[i * 8 + j];
                uint8_t bit = (symbol >> (7 - channel)) & 1;
                byte |= bit << (7 - j);
            }
            linear[channel * 12 + i] = byte;
        }
    }
}

void InterleavePW96(const uint8_t linear[96], uint8_t raw[96])
{
    memset(raw, 0, 96);
    for (int channel = 0; channel < 8; channel++) {
        for (int i = 0; i < 12; i++) {
            uint8_t byte = linear[channel * 12 + i];
            for (int j = 0; j < 8; j++) {
                uint8_t bit = (byte >> (7 - j)) & 1;
                raw[i * 8 + j] |= bit << (7 - channel);
            }
        }
    }
}

static uint8_t ToBCD(uint32_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static void LBAToMSFBCD(uint32_t frames, uint8_t out[3])
{
    out[0] = ToBCD(frames / (75 * 60));
    out[1] = ToBCD((frames / 75) % 60);
    out[2] = ToBCD(frames % 75);
}

void BuildQ12(const QSynthInfo &info, uint8_t q[12])
{
    // Raw Q layout: CONTROL in the upper nibble, ADR (1 = position) in the
    // lower one. (The MMC "ADR/Control" response byte is nibble-swapped
    // relative to this.)
    q[0] = (uint8_t)((info.control << 4) | 0x01);
    q[1] = ToBCD(info.tno);
    q[2] = ToBCD(info.index);
    LBAToMSFBCD(info.rel_lba, &q[3]);
    q[6] = 0x00;
    LBAToMSFBCD(info.abs_lba + 150, &q[7]);

    uint16_t crc = SubQCRC16(q, 10);
    q[10] = (uint8_t)(crc >> 8);
    q[11] = (uint8_t)(crc & 0xFF);
}

void BuildRawPW96(const QSynthInfo &info, uint8_t raw[96])
{
    uint8_t linear[96];
    memset(linear, 0, 96);

    // P channel: solid 1s during the pregap, 0 in the program area
    if (info.index == 0)
        memset(&linear[0], 0xFF, 12);

    BuildQ12(info, &linear[12]);

    InterleavePW96(linear, raw);
}
