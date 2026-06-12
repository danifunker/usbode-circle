# Test plan: new CD-ROM parser (multi-bin CUE, multisession, subchannel, CCD)

Covers the changes introduced on the `new-CDROM-parser` branch: per-track seek
contract, multi-bin CUE + multisession support, full subchannel support
(stored passthrough + synthesis), CCD/IMG/SUB, and the CHD mapping rewrite.

**Setup:** Pi flashed with the build under test; a Linux host with
`libcdio-utils` (`cd-info`), `sg3-utils` (`sg_raw`), `cdparanoia`, `cdrdao`.
Replace `/dev/sr0` as appropriate. Run the phases in order — each depends on
the previous one being sane.

Host-side unit tests (no hardware needed): `cd test && make run`.

## Phase 0 — Regression on existing images (highest risk: every read path changed)

Use images that worked on the previous build. For each of: a plain **ISO**, a
**single-bin cue** (ideally a mixed-mode game with audio tracks), an existing
**MDS**, an existing **CHD**:

1. Mount via web UI → device enumerates, no errors in the kernel log at mount
   (`Built N track(s)` / `Generated CUE sheet` lines should look sane).
2. `cd-info /dev/sr0` → TOC identical to the old build (track count, LBAs,
   leadout). **Exception:** MDS images whose TOC was previously shifted +150 —
   data and TOC are now *consistent*; such discs should mount where they
   previously may not have.
3. `sudo dd if=/dev/sr0 bs=2048 | md5sum` → identical checksum to the same
   command on the old build (the data ISO is the critical one).
4. Mixed-mode cue: `mount /dev/sr0 /mnt` and spot-check files;
   `cdparanoia -d /dev/sr0 2 track2.wav` for an audio track.
5. Audio CD playback through the CD player (analog/I2S path) — plays, track
   seeking works, no noise at track transitions (the cdplayer seek path
   changed).
6. Disc swap between two images twice — confirm no stale TOC.

## Phase 1 — Multi-bin CUE + multisession (Redump CD-EXTRA test disc)

Reference disc: *Akumajou Dracula - MIDI Collection (Japan)* — 20 audio bins
(session 1) + 1 MODE2/2352 data bin (session 2). Copy the whole folder
(bins + cue) to the SD card.

1. **Listing:** web UI / SCSI toolbox shows only the `.cue`; the per-track
   bins are hidden.
2. Mount it. Log shows `Built 21 track(s), 21 file(s)` and a normalized cue
   with `REM SESSION 02` before track 21.
3. `cd-info /dev/sr0` → 21 tracks, **2 sessions**; track 21 (data, MODE2)
   starts at LBA = (sum of frames of tracks 1–20) + **11250**.
4. `mount /dev/sr0 /mnt` → the session-2 ISO9660 filesystem mounts and files
   open (validates session reporting + the gap baked into LBAs).
5. `cdparanoia -d /dev/sr0 1 t1.wav`, then compare the audio payload against
   `... (Track 01).bin` (the bin is headerless PCM; the wav has a 44-byte
   header). Repeat for a middle track (cross-file seek) and track 20.
6. Cross-file boundary read: extract a span straddling the track 1 → 2 join
   (e.g. `cdparanoia -d /dev/sr0 "1[:14744]-2[0:01]" span.wav`) — no
   glitch/click at the join.
7. On a real retro host: the disc shows as audio CD + data session (CD-EXTRA)
   per the OS's capability.

## Phase 2 — Subchannel, synthesized (any plain ISO)

These commands **failed with INVALID FIELD on the old build**; they must now
succeed:

1. Raw P-W:
   `sudo sg_raw -r 2448 /dev/sr0 be 00 00 00 00 10 00 00 01 f8 01 00 -t 20`
   (READ CD, LBA 16, 1 sector, full raw main channel + sub selection 0x01)
   → 2448 bytes returned.
2. Formatted Q: same command with byte 10 = `02` and `-r 2368` → the last 16
   bytes are Q: byte 0 = `0x41`, TNO/INDEX in BCD, absolute MSF of LBA 16 =
   `00:02:16` → bytes `00 02 16` BCD, valid CRC.
3. `icedax -D /dev/sr0 -J -v toc` (or `cdrdao read-toc`) reports sane Q
   positions across the disc.

## Phase 3 — Subchannel, stored (MDS/MDF SafeDisc — the core feature)

Use a real SafeDisc dump (2448-byte sectors; the mount log shows
`subchannel: 0x08` / `SafeDisc compatible`).

1. **One-time math validation (do this first):** dump one sector's subs from
   the gadget (`sg_raw` sub selection 0x01 as above) and compare the 96 sub
   bytes against the MDF directly:
   `dd if=game.mdf bs=1 skip=$((LBA*2448 + 2352)) count=96 | xxd`.
   Must be **byte-identical** (passthrough, not synthesis).
2. Deinterleave those 96 bytes and check the Q CRC matches the convention in
   `addon/discimage/subchannel.cpp` (complemented CRC16-CCITT). If it
   mismatches, the synthesis path (Phase 2) is producing inverted CRCs and
   needs a one-line fix in `SubQCRC16`.
3. Formatted Q (selection 0x02) equals the deinterleaved stored Q.
4. **End-to-end:** Windows 98/XP VM (QEMU/VirtualBox with USB passthrough of
   the gadget) → install/launch a SafeDisc title, or run ProtectionID against
   the drive. The copy-protection check passing is the acceptance criterion.
5. A Mode 2 MDS (PSX-style dump): `cd-info` TOC correct and data reads clean —
   exercises the mode-byte decode fix (these were 8 bytes off before).

## Phase 4 — CCD/IMG/SUB

Ideally dump the **same disc** as both MDS and CCD (CloneCD or `cdrdao`).

1. Mount the `.ccd` (also try mounting the `.img` — must redirect to the ccd).
   Log shows the synthesized cue; no `non-standard session gap?` warnings.
2. `cd-info` TOC matches the CCD's `[Entry]` PLBA values exactly.
3. With `.sub` present: the gadget's sub selection 0x01 output, deinterleaved,
   must equal the 96 bytes at `LBA*96` in the `.sub` file (validates the
   interleave direction — CloneCD subs are stored linear).
4. Differential: MDS vs CCD of the same disc → identical READ CD (+sub)
   output for a sample of LBAs.
5. Rename the `.sub` away and remount → still mounts; subchannel requests
   succeed via synthesis.

## Phase 5 — CHD (regression-sensitive: mapping rewritten)

1. A known-good multi-track CHD from before → TOC and audio extraction
   unchanged **if** its track lengths happen to be multiples of 4; otherwise
   tracks 2+ will read *differently* — and now correctly. Verify with
   `chdman extractcd` on a PC and compare per-track `cdparanoia` output
   against the extracted cue/bin (byte-exact now; the old build drifted up to
   3 frames per track boundary).
2. A cooked CHD (`chdman createcd -i game.iso -o game.chd`) → mounts and
   `dd | md5sum` equals the source ISO (previously unreadable).
3. A PSX/Mode 2 CHD → data track reads correctly (previously 8 bytes off via
   the MODE1/2352 cue type).

## Phase 6 — Strict-host spot check

Boot a strict BIOS host (e.g. ITX-Llama Coreboot+SeaBIOS) with a multi-bin
disc mounted and confirm enumeration didn't regress. The USB descriptors are
untouched, but `SetDevice`/`BuildTrackTable` does more work per disc swap, so
a sanity boot is cheap insurance.

## Triage tips

- Enable the gadget's debug logging: the new code logs the normalized cue
  sheet, track table, leadout, and per-batch read geometry at mount/read time.
- The most diagnostic artifact for any data-corruption report: `cd-info` TOC
  plus an `md5sum` of the first 64 KB of a known track, old build vs new.
- If the 32-bit pipeline build fails where aarch64 passed, suspect a `u64`
  printf format specifier in the new code.
