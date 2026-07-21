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
  - with `WITH_CHD=1`, the tracked `sdcard/usbode-audio-test.chd` and a
    **mixed-mode CHD built with `chdman`** (`testdata/mixed.chd`), whose data
    track is decoded through real libchdr and checked byte-exact.

  All committed test images are free-to-redistribute (FreeDOS GPL files or
  generated content); see `testdata/README-testdata.md`.

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
| `real_iso_*`, `real_cuebin_*`, `real_chd_*` | The reader path: cue parsing, per-track offsets across the 2048->2352 boundary, the read-ahead cache, and real CHD hunk decompression, driven from real files rather than a fake |

The bench itself found one latent firmware bug on day one: passing a
device to the `CUSBCDGadget` constructor makes `SetDevice()` delete the
device it was just handed and continue using the freed pointer
(production code always passes `nullptr` and calls `SetDevice()` later,
so the path is dormant — see `harness/bench.cpp`).

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

- **The image-loader factory** (`util.cpp` `loadCueBinIsoFileDevice`): format
  dispatch by file extension and `.cue`->`.bin` filename resolution. The
  real-image tests now read a `.cue` off the filesystem through the FatFs shim
  and parse it, but they open the matching `.bin` by known name rather than
  going through the production factory (it pulls in the MDS chain, which host
  tests don't build).
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
