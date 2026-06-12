# Handoff: new CD-ROM parser architecture (branch `new-CDROM-parser`)

Context document for debugging/extending the disc-image layer rewrite
(commits `d56f451` + `03ce20e`, 2026-06-12). Companion to
`docs/TESTING-cdrom-parser.md` (test plan). Read this before changing
anything in `addon/discimage/`, `addon/cueparser/`, or the read path of
`addon/usbcdgadget/`.

## 1. The core contract (everything depends on this)

The gadget and every image device communicate through a **normalized cue
sheet** plus a **virtual byte space**:

1. Every device's `GetCueSheet()` returns a *normalized* cue: single
   `FILE`, `BINARY`, absolute `INDEX 00`/`INDEX 01` times (MM:SS:FF where
   frame 0 = LBA 0, **no** +150 offset), optional `REM SESSION nn`
   markers, and **never** a `PREGAP` directive.
2. The device guarantees its `Seek`/`Read`/`GetSize` byte space is *exactly*
   the concatenation implied by `CUEParser`'s accounting of that cue:
   every byte position the accounting derives is backed either by stored
   image data or by zero-fill (inter-session gaps, unstored pregaps).
3. Consumers (gadget, cdplayer) never compute `block_size * LBA`. They map
   LBA → byte via the track that contains it:
   `CUEByteOffset(track, lba) = track.file_offset + (lba - track.data_start) * track.sector_length`
   (negative branch for stored-pregap LBAs between INDEX 00 and INDEX 01).
   Helper lives in `addon/cueparser/cueparser.h`.

The accounting rule (what "the cue implies" means), as implemented by
`CUEParser::next_track`:

- `file_offset(track N)` = `file_offset(N-1)` +
  `(track_start(N) - data_start(N-1)) * sector_length(N-1)` +
  `(data_start(N) - track_start(N)) * sector_length(N)`.
- So *all* LBA ranges between INDEX 01 of one track and INDEX 01 of the
  next are byte-backed; the device decides whether those bytes come from a
  file or are zeros.

Session gaps are **baked into the LBAs** of the normalized cue (the cue
parser does not add gaps itself — `REM SESSION` only tags the session
number). Standard gap constants, which must match between devices and the
gadget's TOC code (`CDUtils::GetSessionGapFrames`): session 1→2 =
6750 + 4500 = **11250** frames; later transitions = 2250 + 4500 = **6750**.

## 2. Component map

| Area | Files | Role |
|---|---|---|
| Cue parsing | `addon/cueparser/cueparser.{h,cpp}` | ZuluSCSI-derived parser + `session` field + `CUEByteOffset`. Two fixed bugs, see §3. |
| Track table | `usbcdgadget.h/.cpp` (`BuildTrackTable`, `m_TrackTable[99]`, `m_nLeadoutLBA`, `m_nSessionCount`) | Built once in `SetDevice` from the normalized cue. All hot-path lookups go through it via `CDUtils` (`cd_utils.cpp`). |
| Data path | `tcdstate_update.cpp` (`DataInRead`) | Per-batch track resolution, geometry recompute for cooked reads (`skip_bytes_per_track`), batch capped at track end, per-sector subchannel append. |
| Command layer | `scsi_read.cpp` (READ 10/12, READ CD), `scsi_toc.cpp` (TOC formats 0/1/2, disc/track info, 0x42 MCN/ISRC) | Per-LBA geometry; session-aware TOC; subchannel selections 0x01/0x02/0x04. |
| Subchannel math | `addon/discimage/subchannel.{h,cpp}` | Pure code: CRC16 (complemented), P-W (de)interleave, Q/P-W synthesis. Host-tested. |
| CUE/BIN device | `addon/discimage/cuebinfile.{h,cpp}` | Segment table `{vstart, vlen, fileIdx(-1=zeros), fileOffset}`; opens all FILEs; emits normalized cue; `LBAToFileOffset()` for subclasses. |
| MDS device | `addon/discimage/mdsfile.{h,cpp}` | `TrackMap` (vstart/base_size/sector_size/start_offset); supports 2448 (sub-bearing), 2352, 2336, 2048-byte sectors; subs passed through raw interleaved. |
| CCD device | `addon/ccdparser/` + `addon/discimage/ccdfile.{h,cpp}` | Synthesizes a *source* cue from the .ccd and **subclasses CCueBinFileDevice** for all mapping; only adds .sub handling (stored LINEAR → interleaved on output). |
| CHD device | `addon/discimage/chdfile.{h,cpp}` | MAME-style accounting: `track_start/data_start/chdFrameStart/vstart` per track, 4-frame CHD padding, `PGTYPE:V` = stored pregap. |
| Listing | `addon/scsitbservice/scsitbservice.cpp` (`HideCueCoveredBins`) | Lists `.cue`/`.ccd`; hides bins referenced by a sibling cue's FILE lines. |
| cdplayer | `addon/cdplayer/cdplayer.{h,cpp}` (`LBAToByteOffset`) | Own cue parse + cached current-track lookup; seeks per-track. |
| Host tests | `test/` (`make run`) | cueparser math (incl. both fixed bugs + normalized-contract test), subchannel CRC/interleave, ccdparser. |

## 3. Bugs that were fixed — do not reintroduce

1. **CUEParser `file_offset` accumulator** (`cueparser.cpp`, the
   `prev_data_start` advance): the old code advanced from the previous
   track's `track_start` although `file_offset` pointed at its
   `data_start`, overshooting by the previous track's stored pregap for
   every INDEX 00 chain. Covered by `test_index00_chain_single_file`.
2. **Multi-file `file_start` cumulative double-count**: `file_start` is a
   file-frame coordinate and must exclude `cumulative_offset` (the INDEX
   handlers add it back). Covered by `test_multifile_pregap`.
3. **READ CD sub selection read from the wrong bits**: the data path used
   `mcs & 0x07` (which is EDC/header bits of CDB byte 9). Now an explicit
   `sub_channel_selection` member set from CDB byte 10.
4. **Formatted Q (sel 0x02)** previously shipped 16 bytes of adjacent
   sector data; `base_sector_size` now subtracts the per-selection size.
5. **MDS mode byte**: decode is low nibble, value or value+8
   (0xA9 audio, 0xAA mode1, 0xAB mode2, 0xAC/0xAD form1/2) — verified in
   libmirage `images/image-mds/parser.c:155-185`. The old "0xA9 else
   MODE1/2352" served Mode 2 discs 8 bytes off.
6. **MDS cue generation emitted `PREGAP` + absolute INDEX**, shifting the
   TOC +150 vs the data mapping. Normalized cue emits absolute INDEX only.
7. **Read(10/12) used track 1's geometry for the whole disc**
   (`data_block_size`/`data_skip_bytes`, now deleted). Geometry is per-LBA;
   cooked reads of audio LBAs return CHECK CONDITION 05/64/00.
8. **CHD frame mapping ignored the 4-frame track padding** (`extraframes`)
   and pregap storage; multi-track CHDs drifted up to 3 frames per track
   boundary. Accounting now mirrors MAME `cdrom_file` (see §5 sources).
9. **`GetSize()` returned physical file size**; for 2448-byte-sector MDFs
   this inflated the leadout ~4%. All devices now return the virtual size.

## 4. Symptom → likely cause (debugging playbook)

- **Everything off by exactly 150 frames / 2 seconds**: pregap or +150 MSF
  offset handling. Check normalized cue INDEX times (log prints it at
  mount) and `CDUtils::LBA2MSF(..., relative=false)` call sites.
- **Data shifted by 8 / 16 / 24 bytes**: track-type vs skip_bytes mismatch
  (16 = MODE1 sync+header, 24 = MODE2 +subheader, 8 = 2336 subheader).
  Check the cue TRACK type emitted by the device vs the real sector format
  (`GetSkipbytesForTrack` in `cd_utils.cpp`).
- **Multi-bin: first track fine, later tracks corrupt/glitchy**: segment
  construction. Compare the mount log's `vstart`/segment count against a
  hand-computed `Σ file sizes`; suspect `ConsumedBytes()` (trailing partial
  sectors) or the gap byte length (`gapFrames * prev_sector_length`).
- **Audio clicks exactly at track joins**: cross-file `Read()` continuation
  (`CCueBinFileDevice::Read` chunk loop) or cdplayer's cached-track
  invalidation (`m_CurrentTrackEnd`).
- **Session-2 filesystem won't mount but TOC looks right**: gap constant
  mismatch between device (`SessionGapFrames` in `cuebinfile.cpp`) and the
  disc's real layout — CCD logs `non-standard session gap?` when PLBAs
  disagree; for cue images the gap is assumed standard.
- **SafeDisc fails but raw reads look fine**: compare gadget sub output
  byte-for-byte against the MDF (`LBA*2448 + 2352`); if synthesis is being
  served instead of passthrough, `HasSubchannelData()`/`ReadSubchannel`
  returned -1 (check `FindMapForLBA` range math).
- **Q CRC rejected by host software**: the complement convention in
  `SubQCRC16` (we store `~crc`). Validate against a real MDF's stored Q
  (test plan Phase 3.2). One-line fix if inverted.
- **CCD subs scrambled**: interleave direction. CloneCD `.sub` is stored
  LINEAR (P bytes 0-11, Q 12-23, ...) — verified in libmirage
  (`PW96_LINEAR`, `image-ccd/parser.c:310`) and DuckStation (Q at
  `lba*96+12`). `ReadSubchannel` must interleave on the way OUT.
- **CHD: cooked (2048) images or Mode 2 wrong**: cue type emission in
  `GenerateCueSheet` vs `dataSize`; **track 2+ wrong**: `chdFrameStart`
  padding accumulation or `PGTYPE` parse.
- **Host sees no disc at all after a swap**: `BuildTrackTable` logged
  `No tracks parsed` → the device's normalized cue is malformed (it's
  printed in the log right above).
- **32-bit pipeline build failure with format warnings**: `u64` printf —
  new code casts to `(unsigned long long)`; look for an uncast one.

## 5. Format-knowledge sources (verified, not from memory)

- **libmirage** (local checkout `~/repos/cdemu-code/libmirage`; also at
  gitlab.com/cdemu/cdemu): MDS mode/subchannel decode
  (`images/image-mds/parser.c`, `image-mds.h`), CCD `.sub` = PW96_LINEAR,
  CCD session lead-outs 11250/6750, `[TRACK] MODE` is untrustworthy
  (`images/image-ccd/parser.c`).
- **DuckStation** `src/util/cd_image_ccd.cpp`: second source for linear
  `.sub` layout and `[Entry]` semantics.
- **MAME** `src/lib/util/cdrom.cpp`: CHD logical/chd frame accounting,
  `CD_TRACK_PADDING` = 4, `PGTYPE` 'V' = pregap frames stored within
  FRAMES.
- **ZuluSCSI/Rabbit Hole CUEParser**: basis of `addon/cueparser`
  (`next_track(prev_file_size)` is the multi-file mechanism).
- Red Book/ECMA-130: Q CRC16-CCITT stored complemented; raw Q byte 0 =
  (CONTROL<<4)|ADR — note MMC *response* ADR/Control bytes are
  nibble-swapped relative to raw Q.

## 6. Verification state (as of commit `03ce20e`)

**Proven:** host unit tests pass (`test/ && make run`); aarch64 firmware
builds and links; all dependent addons compile.

**Not yet proven (needs hardware, see test plan):** every on-device
behavior — Phase 0 regression, the Akumajou multi-bin disc, stored-sub
passthrough on a real SafeDisc MDF, the Q-CRC complement convention against
ground truth, CCD `.sub` interleave direction end-to-end, CHD padding fix
against a known-good CHD, 32-bit pipeline build.

**Known deliberate limitations:**
- Inter-session gaps for CUE/MDS assume the standard 11250/6750 frames;
  CCD logs a warning when its PLBAs disagree but still uses the constants.
- CHD multisession metadata is not parsed (treated single-session).
- CHD subchannel passthrough remains gated behind `.subchan.` in the
  filename (chdman-synthesized subs are usually garbage); stored subcode
  SUBTYPE (cooked vs raw) is not interpreted.
- `[TRACK] FLAGS` (DCP/pre-emphasis) are not reflected in TOC control bits.
- MCN/ISRC are served from cue/CCD metadata only, not extracted from
  stored Q (ADR 2/3) frames.

## 7. Related context

- Implementation plan (full rationale + per-stage breakdown):
  `~/.claude/plans/logical-greeting-turtle.md` (local, not in repo).
- Test plan: `docs/TESTING-cdrom-parser.md`.
- The test disc: `~/Downloads/Akumajou Dracula - MIDI Collection (Japan)`
  (21 bins + cue, 2 sessions; sizes are embedded in
  `test/cueparser_test.cpp` for the parser-level checks).
- Unrelated parallel work: USB gadget enumeration on strict BIOS hosts
  (#591) — descriptors untouched by this branch.
