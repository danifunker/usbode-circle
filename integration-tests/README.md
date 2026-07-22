# USBODE integration tests

Automated tests for the SCSI/Bulk-Only-Transport layer and the disc-image
readers that run on an ordinary PC — no Raspberry Pi, no cross-compiler, no
circle-stdlib checkout. CI runs them on every push and pull request that
touches the gadget or reader code (`.github/workflows/host-tests.yml`).

```
make -C integration-tests            # build + run (command-layer + ISO/CUE/BIN images)
make -C integration-tests WITH_CHD=1 # also run the real .chd image through libchdr
USBODE_TEST_VERBOSE=1 integration-tests/out/usbode-host-tests   # with firmware logs
```

## What this is

The **real firmware sources** — all of `addon/usbcdgadget` (command
handlers, BOT state machine, `Update()` chunked-read path), the real
`cueparser`, and the real disc-image readers (`addon/discimage/cuebinfile.cpp`
and, under `WITH_CHD`, `chdfile.cpp` + libchdr) — are compiled for the build
machine against a set of thin stub headers (`harness/stubs/`) that stand in for
Circle. Nothing under test is reimplemented or mocked: the same `scsi_toc.cpp`
and `cuebinfile.cpp` that answer a Windows 98 machine answer the tests.

The harness then behaves exactly like a USB host:

1. `CGadgetTestBench` builds a CBW and copies it into the buffer the
   gadget queued on its OUT endpoint, then calls `OnTransferComplete()` —
   the same entry point the DWC interrupt handler uses on hardware.
2. The gadget runs its production dispatch (`HandleSCSICommand`), unit
   attention gating, and response builders. Its `BeginTransfer()` calls
   land in a test sink instead of DWC registers.
3. The bench completes data phases (calling `Update()` when the gadget
   enters `DataInRead`, so multi-chunk READs work) until the CSW arrives.
4. Tests assert on the exact wire bytes: data phase, CSW status, and
   **data residue** — the field Win9x storage stacks are strict about.

Two flavours of disc back the bench, both driven through the same
`IImageDevice` interface the firmware uses:

- **Command-layer tests** use a fake in-memory `IImageDevice`
  (`harness/fakedisc.h`) with deterministic sector contents, to exercise the
  SCSI/BOT layer in isolation.
- **Real-image tests** (`test-suite/test_realimages.cpp`) load actual files
  through the real readers:
  - a **real CD-DA disc built from the tracked `sdcard/test.pcm.gz`**, the
    sound-test sample `CCDPlayer::SoundTest()` plays on hardware. It is 16-bit
    stereo at 44.1 kHz, which is Red Book audio, so the Makefile decompresses
    it and cuts it into a three-track disc at build time. Nothing extra is
    committed, and the audio path runs over real signal data instead of a
    generated pattern. Checked byte-exact against the source PCM through the
    reader and on the wire via READ CD (0xBE);
  - the tracked `sdcard/image.iso.gz` and a **real FreeDOS ISO9660 + Joliet
    disc** (`testdata/freedos-test.iso.gz`, built from genuine FreeDOS 1.3 GPL
    files — see `testdata/README-testdata.md`), checked against real
    on-disc structure (primary + Joliet volume descriptors, volume id);
  - a **real game disc** (`testdata/shareware.iso.gz`: the Descent shareware
    episode and the SkyRoads freeware game, both cleared for redistribution)
    read back in full and compared byte-for-byte against the file — a real
    ~3.5 MB filesystem with multi-megabyte files spanning many sectors and
    read-ahead-cache refills;
  - synthetic CUE/BIN pairs written at run time, plus committed **real cue
    sheets loaded off the filesystem through the FatFs shim**
    (`testdata/audiocd.*`, `testdata/mixed.*`), with known bytes so reads,
    per-track offsets across the 2048->2352 boundary, and TOC/medium-type are
    checked exactly;
  - with `WITH_CHD=1`, the tracked `sdcard/usbode-audio-test.chd`, a
    **mixed-mode CHD built with `chdman`** (`testdata/mixed.chd`), whose data
    track is decoded through real libchdr and checked byte-exact, and a
    **FLAC-only CHD** (`testdata/audiocd-flac.chd`, `chdman createcd -c cdfl`)
    whose audio decodes through libchdr's FLAC path and is compared against
    the original `audiocd.bin` bytes.

  All committed test images are free-to-redistribute (FreeDOS GPL files or
  generated content); see `testdata/README-testdata.md`.

- **MDS/MDF tests** (`test-suite/test_mdsimages.cpp`) drive the Alcohol 120%
  reader (`addon/discimage/mdsfile.cpp` + `addon/mdsparser/mdsparser.cpp`).
  There is no MDS fixture to commit — the format is proprietary and the
  images people mount are game discs — so the `.mds` files are authored by
  the test out of the format's own structures. Two things keep that from
  being circular: the structure sizes are asserted against the published
  on-disc layout (88/24/80/8/16 bytes), so a change that would misparse a
  real Alcohol image still fails; and the `.mdf` payload is real, raw
  2352-byte MODE1 sectors wrapping the FreeDOS ISO, so a read at the wrong
  offset is caught by ISO9660's own volume descriptors. Covers data and
  mixed data+audio discs, wildcard and UTF-16 data-file names, 2448-byte
  subchannel images (stripped from user data, and readable on request), and
  a set of malformed images that must be rejected without crashing — the
  file browser lists every `.mds` on the card, so anything a user renames
  reaches this parser.

The only host-side seams the readers get are the raw file-access boundary
(`harness/fatfs_host.cpp`, a FatFs shim over stdio) and the two
`FatFsOptimizer` fast-seek entry points (`harness/discimage_host.cpp`, a no-op
since there is no FAT under the host filesystem). All reader logic — cue
parsing, per-track sector-size math, the read-ahead cache, CHD hunk
decompression — is the real firmware code.

## Why these tests exist

Every case encodes behavior that a real host depends on, and most encode a
bug USBODE actually shipped:

| Test | Shipped bug it would have caught |
| --- | --- |
| `mode_sense10_medium_type_*` | #164: hardcoded medium type 0x13 broke Win98 CD audio ("data or no disc loaded") |
| `mode_sense10_page0e_win98_golden` | Byte-exact MODE SENSE page 0x0E response retail Win98 SE accepted before PLAY AUDIO (from a Trace Lab golden capture); also the pad-to-allocation-length fix |
| `read_toc_legacy_cdb9_session_info` | Win9x session-info request encoded in CDB[9] answered with a full TOC |
| `read_toc_format0_lba` residue check | CSW residue reported as 0 on short responses made Win98 usbstor.sys discard and retry forever |
| `read_blocked_by_unit_attention` | Missing STALL before CSW reset the device on Windows 11 xHCI |
| `disc_swap_media_change_full_sequence` | The runtime image-swap handshake (`SetDevice()` -> NO_MEDIUM `02/3a/00` -> settle -> UNIT ATTENTION `06/28/00` blocking reads -> REQUEST SENSE clears to READY): a regression here silently serves the previous disc's capacity/TOC/data after a swap |
| `gesn_*` | GET EVENT STATUS NOTIFICATION (0x4A) media-change polling — NewMedia / Media Removal / No Change byte-exact, and async (non-polled) requests rejected `05/24/00`; this is how Windows and macOS auto-detect a swapped disc |
| `read10_*` | Boundary clamping, multi-chunk `Update()` batching (32 blocks HS / 16 blocks FS), residue on truncated reads |
| `cbw_invalid_signature_stalls`, `cbw_short_packet_stalls` | A malformed CBW must stall rather than be executed (BOT 6.6.1). Running `CBWCB[0]` out of an unvalidated buffer issues an arbitrary opcode; skipping the length check acts on stale bytes from the previous command |
| `read12_*` | READ(12) parses LBA and block count from four bytes each — a byte-order or offset slip there reads the wrong sectors, or misreads the count as huge and runs off the disc |
| `read10_last_sector_is_addressable` | The disc-edge off-by-one: last valid LBA is leadout - 1. A `>=` where `>` belongs makes a full-disc copy fail at the very end |
| `*_zero_allocation_length` | Allocation length 0 is a legal "send no data" request, not an error; answering CHECK CONDITION or a short transfer leaves strict hosts waiting |
| `play_audio_*`, `read_subchannel_*` | The full MCICDA analog-audio sequence (the one Win98 QuickInstall's replaced USB stack never sends — oerg866/win98-quickinstall#151) |
| `cue_crlf_*`, `cue_lowercase_*`, `cue_irregular_whitespace`, `cue_metadata_lines_ignored`, `cue_filename_with_spaces_and_dot_slash` | Cosmetic cue-sheet variation (CRLF, casing, tabs, `REM`/`TITLE`/`FLAGS` lines, quoted paths) must not change the disc layout reported to the host. Asserted as equality with the canonical sheet's TOC, so a parser that mangled both alike could not satisfy it |
| `cue_stored_pregap_*`, `cue_unstored_pregap_*` | Pregap arithmetic: a stored `INDEX 00` gap must not be reported as the track start (that begins every track early, inside the silence), and an unstored `PREGAP` must shift all following tracks *and* the leadout, since it occupies disc addresses but no file bytes |
| `truncated_bin_*`, `zero_length_image`, `read_straddling_end_of_file` | Damaged and half-copied images must fail as a read error, not as a device that stops answering: every CBW still gets a CSW, and reads stay inside the file that actually exists |
| `real_flac_chd_audio_decodes_byte_exact` | libchdr's FLAC decoder was compiled but never executed: chdman picks the winning codec per hunk, and synthetic data always picks LZMA or deflate. Real music rips pick FLAC, so this is the path a user's audio CD image actually takes |
| `toolbox_*` | The vendor toolbox command set (0xD0/0xD2/0xD7/0xD8/0xD9) — how the host-side picker enumerates SD-card images and swaps discs. It is USBODE's own protocol, so there is no standard for a client to fall back on: the fixed device list, the one-byte count and its 100-entry cap, and the 40-byte directory entry with its 40-bit big-endian size are all parsed at fixed offsets by the client |
| `start_stop_unit_*`, `prevent_allow_*`, `medium_removal_lock_does_not_block_disc_swap` | The mount/eject handshake. USBODE has no tray, so these are no-ops — and the no-op is the load-bearing part: Windows sends START STOP UNIT while bringing the drive online and locks the door whenever a volume is open, so an eject that really ejected, or a lock that really locked, would kill the web UI's disc swap and leave the host holding an unusable drive |
| `mode_select10_*` | The one command with a data-out phase: the parameter list has to be consumed and the residue accounted for or the host waits forever. Also the Descent 2 volume quirk (four MODE SELECTs in a row with the channels swapped, lower of each pair wins) and a zero-length parameter list, which is legal |
| `real_audio_from_repo_pcm_sample` | Real CD-DA read back byte-exact against its source, built from the sound-test sample already in the repo rather than a new fixture. Covers READ CD (0xBE) with expected sector type CD-DA, the command a player or ripper uses to pull raw 2352-byte audio, which READ(10) does not do (it returns 2048 bytes of cooked user data) |
| `real_iso_*`, `real_cuebin_*`, `real_chd_*` | The reader path: cue parsing, per-track offsets across the 2048->2352 boundary, the read-ahead cache, and real CHD hunk decompression, driven from real files rather than a fake |

The bench found eight latent firmware bugs. All eight are fixed now, each in
its own commit with the test that pins it, so any one of them can be reviewed
or reverted on its own.

| Bug | Fixed in | Pinned by |
| --- | --- | --- |
| Mounting a cue sheet with no parseable tracks faulted at mount time: `CDUtils::GetSkipbytes()` and `GetBlocksize()` dereferenced `CUEParser::next_track()` with no null check, from `SetDevice()` | `cd_utils.cpp` | `cue_with_no_tracks`, `empty_cue_sheet`, `garbage_cue_sheet` |
| The toolbox data-in handlers never cleared the pending block count, so a toolbox transfer resumed the read path afterwards and streamed disc sectors onto the end of the reply (LIST DEVICES measured at 10248 bytes instead of 8). The three transfer-state members also had no initializers, making it reachable at boot | `scsi_toolbox.cpp`, `usbcdgadget.h` | `toolbox_command_does_not_resume_a_pending_read` |
| A CBW with a non-zero LUN or a CDB length above 16 was dropped silently: no CSW and no stall, so the host waited for a status that never came | `usbcdgadget.cpp` | `cbw_nonzero_lun_stalls`, `cbw_oversized_cb_length_stalls` |
| `SCSIToolbox::ListFiles()` shipped uninitialized heap in every entry's name padding, up to roughly 2 KB per catalog, different bytes each call | `scsi_toolbox.cpp` | `toolbox_list_files_name_padding_is_zeroed`, `toolbox_list_files_is_deterministic` |
| MODE SELECT dispatched on the page *length* where it meant the page *code*, so the audio page was ignored with any other declared length and an unrelated page declaring length 0x0E was mistaken for it. The page offset also ignored block descriptors and was never bounded against what arrived | `usbcdgadget.cpp` | `mode_select10_audio_page_with_nonstandard_length_still_applies`, `mode_select10_page_length_0e_is_not_the_audio_page`, `mode_select10_audio_page_after_block_descriptor`, `mode_select10_truncated_audio_page_ignored` |
| The MODE SELECT parameter list length was taken from the CDB unbounded, so a host could write past the 2048-byte OUT buffer | `scsi_inquiry.cpp` | none, deliberately: exercising the unclamped path corrupts the test process rather than reporting a failure |
| Multi-`FILE` cue sheets underflowed once a stored `INDEX 00` had advanced `file_offset`, reporting a track at LBA 2308179703 | `cueparser.cpp` | `multifile_cue_does_not_underflow_into_a_nonsense_lba` |
| Passing a device to the `CUSBCDGadget` constructor made `SetDevice()` free it and carry on using the pointer | `usbcdgadget.cpp` | `device_passed_to_constructor_is_not_freed` |

Two of these change what a real host sees and want a bench check before they
ship: the MODE SELECT page-code fix, which changes which MODE SELECTs take
effect (the Descent 2 volume sequence and the retail Win98 MCICDA path are
both covered here, but neither is a substitute for hardware), and the CBW
stall, which changes how the device answers a host that addresses a LUN it
does not have.

Two of the fixes are partial by choice. The multi-`FILE` cue fix bounds the
arithmetic but cannot place those tracks correctly, because the loader ignores
`FILE` names and always opens the bin named after the cue, so the real sizes
are not available; placing them properly means teaching the loader to open
each file, which is a feature rather than a fix. The MODE SELECT length is
clamped rather than answered with `05/24/00 INVALID FIELD IN CDB`, since the
memory safety is the actual bug and no observed host produces the case.

Note for anyone verifying a fix by reverting it: `git stash` can restore a
source file within the same filesystem timestamp second as the object built
from the reverted version, and `make` then keeps the stale object and reports
a false green. Run `make clean` between the revert and the rebuild.

## Layout

```
integration-tests/
  Makefile             host build; `make` = build + run, WITH_CHD=1 adds CHD
  harness/
    stubs/             minimal Circle/service headers (circle/, cdplayer/, fatfs/, ...)
    stubs.cpp          logger/scheduler/timer/endpoint implementations
    testbus.h          records BeginTransfer()/Stall() from the gadget
    fakedisc.*         in-memory disc images + cue sheets
    fatfs_host.cpp     FatFs f_open/f_read/... over host stdio (real-image reads)
    discimage_host.cpp FatFsOptimizer no-op backing (fast seek n/a on host)
    bench.*            the virtual USB host
    framework.*        tiny TEST()/CHECK() runner
  test-suite/          one file per command family, plus test_realimages.cpp
                       and test_mdsimages.cpp
```

Two production accommodations (both inert on the device):

- `tcdstate_update.cpp`: the ARM cache-maintenance asm is guarded by
  `#if AARCH == 64 / #elif AARCH == 32 / #else (host: no-op)`.
- `usbcdgadget.h`: one `friend class CGadgetTestBench;` declaration.

## Adding a test

```cpp
#include "bench.h"
#include "framework.h"

TEST(my_new_case)
{
    CFakeImageDevice *disc = MakeDataISO(1200);   // 1200-sector data CD
    CGadgetTestBench bench(disc);                  // optional: player fake
    bench.Activate();                              // enumerate
    bench.RequestSense();                          // clear unit attention

    const u8 cdb[10] = {0x43, /* ... */};
    auto r = bench.SendCommand(cdb, sizeof(cdb), /*transferLen=*/100);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.csw.dCSWDataResidue, 80u);
    CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));
}
```

Drop the file in `test-suite/` — the Makefile globs it, the `TEST` macro
registers it. To drive a real file instead of the fake, construct the reader
directly (see `test_realimages.cpp`) and hand it to the same bench.

When a Trace Lab capture from a real machine shows a request/response
pair worth preserving (like the Win98 page 0x0E case), turn it into a
byte-exact test: that is the cheapest way to convert one afternoon of
hardware debugging into permanent coverage.

## What this does not cover (Tier 2 ideas)

Host-side tests cannot see real USB timing, the DWC controller, IRQ
interleaving, or actual host OS quirks. (The real FatFs is now partially
stood in for by a stdio shim; genuine on-SD-card behavior like fragmentation
and fast-seek still only runs on hardware.) A few production paths are also
deliberately out of scope here and only run on hardware:

- **The image-loader factory** (`util.cpp` `loadImageDevice`): format dispatch
  by file extension and `.cue`->`.bin` filename resolution. The tests read a
  `.cue` (and a `.mds`) off the filesystem through the FatFs shim and parse
  it, but they construct the reader for the format under test directly rather
  than going through the production factory.
- **`FatFsOptimizer` fast-seek**: CLMT allocation, `CREATE_LINKMAP`, and
  fragmented-file seeking. The host shim makes fast-seek a no-op, so the
  reader falls back to plain seeks.
- **The property-tag serial-number success path**: the host property-tag stub
  always reports failure, so the gadget always takes its fallback-serial
  branch; the hardware success branch and serial-descriptor formatting are
  uncovered.

The natural next tier is
hardware-in-the-loop: a PC with `sg3_utils` issuing the same command set
to a real USBODE over USB, comparing Trace Lab captures against golden
traces (`usbode_trace.py compare`). QEMU is not a shortcut here — its
dwc2 model is host-mode only, so the gadget side cannot run under
emulation.
