#!/usr/bin/env python3
"""Generate mixedmode-repro.bin/.cue — a synthetic mixed-mode CD image that
reproduces the CD-audio wrong-offset bug in CCDPlayer (cdplayer.cpp seeks
audio at LBA*2352, ignoring that the MODE1/2048 data track is stored at
2048 bytes/sector in the BIN).

Layout:
  Track 1: real ISO9660 data track (data.iso, MODE1/2048) — must exist first;
           build it with:
             hdiutil makehybrid -iso -joliet -default-volume-name USBODE_TEST \
                 -o data.iso <folder with README.TXT and ~7.5MB PAD.DAT>
           The padding makes the data track large enough (>3482 sectors) that
           CCDPlayer's byte-offset error exceeds one full audio track.
  Tracks 2-6: 6 seconds each of a pure sine tone, one distinct pitch per
           track, so the *audible* tone identifies which track's data is
           actually being played:
             track 2 = 440 Hz, 3 = 660 Hz, 4 = 880 Hz, 5 = 1100 Hz, 6 = 1320 Hz

Expected symptom on affected firmware (3.0.5+): selecting track N plays the
wrong tone (track N+1's pitch), and track 6 seeks past EOF, reproducing the
reported freeze. On fixed firmware every track plays its own pitch from 0:00.
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BIN_NAME = "mixedmode-repro.bin"
CUE_NAME = "mixedmode-repro.cue"
ISO_PATH = os.path.join(HERE, "data.iso")

SECTOR_DATA = 2048
SECTOR_AUDIO = 2352
FRAMES_PER_SEC = 75
SAMPLE_RATE = 44100
SAMPLES_PER_SECTOR = SECTOR_AUDIO // 4  # 16-bit stereo = 4 bytes/sample-frame

AUDIO_TRACK_SECTORS = 450  # 6 seconds
TRACK_TONES_HZ = [440, 660, 880, 1100, 1320]  # tracks 2..6


def frames_to_msf(frames):
    mm = frames // (60 * FRAMES_PER_SEC)
    rem = frames % (60 * FRAMES_PER_SEC)
    return f"{mm:02d}:{rem // FRAMES_PER_SEC:02d}:{rem % FRAMES_PER_SEC:02d}"


def tone_track_bytes(freq_hz, num_sectors):
    total_frames = num_sectors * SAMPLES_PER_SECTOR
    out = bytearray()
    amp = int(0.3 * 32767)
    for n in range(total_frames):
        s = int(amp * math.sin(2 * math.pi * freq_hz * n / SAMPLE_RATE))
        out += struct.pack("<hh", s, s)
    return bytes(out)


def main():
    if not os.path.exists(ISO_PATH):
        sys.exit(f"missing {ISO_PATH} — build it first (see module docstring)")

    with open(ISO_PATH, "rb") as f:
        iso_bytes = f.read()
    if len(iso_bytes) % SECTOR_DATA != 0:
        sys.exit("data.iso is not 2048-byte aligned")
    data_sectors = len(iso_bytes) // SECTOR_DATA

    cue = [f'FILE "{BIN_NAME}" BINARY']
    bin_out = bytearray()
    lba = 0

    cue.append("  TRACK 01 MODE1/2048")
    cue.append(f"    INDEX 01 {frames_to_msf(lba)}")
    bin_out += iso_bytes
    lba += data_sectors

    for i, freq in enumerate(TRACK_TONES_HZ):
        t = i + 2
        cue.append(f"  TRACK {t:02d} AUDIO")
        cue.append(f"    INDEX 01 {frames_to_msf(lba)}")
        bin_out += tone_track_bytes(freq, AUDIO_TRACK_SECTORS)
        lba += AUDIO_TRACK_SECTORS

    with open(os.path.join(HERE, BIN_NAME), "wb") as f:
        f.write(bin_out)
    with open(os.path.join(HERE, CUE_NAME), "w", newline="") as f:
        f.write("\r\n".join(cue) + "\r\n")

    err = data_sectors * (SECTOR_AUDIO - SECTOR_DATA)
    print(f"BIN: {len(bin_out)} bytes ({data_sectors} data + "
          f"{len(TRACK_TONES_HZ)}x{AUDIO_TRACK_SECTORS} audio sectors)")
    print(f"CCDPlayer offset error: {err} bytes "
          f"({err / SECTOR_AUDIO / AUDIO_TRACK_SECTORS:.2f} audio tracks)")
    for i in range(len(TRACK_TONES_HZ)):
        t = i + 2
        correct = len(iso_bytes) + i * AUDIO_TRACK_SECTORS * SECTOR_AUDIO
        wrong = (data_sectors + i * AUDIO_TRACK_SECTORS) * SECTOR_AUDIO
        past_eof = wrong + AUDIO_TRACK_SECTORS * SECTOR_AUDIO > len(bin_out)
        print(f"track {t}: correct offset {correct}, buggy seek {wrong}"
              f"{'  (reads past EOF)' if past_eof else ''}")


if __name__ == "__main__":
    main()
