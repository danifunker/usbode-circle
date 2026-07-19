# USBODE host regression tests

Automated tests for the SCSI/Bulk-Only-Transport layer that run on an
ordinary PC — no Raspberry Pi, no cross-compiler, no circle-stdlib
checkout. CI runs them on every push and pull request that touches the
gadget code (`.github/workflows/host-tests.yml`).

```
make -C test/host          # build + run
USBODE_TEST_VERBOSE=1 test/host/out/usbode-host-tests   # with firmware logs
```

## What this is

The **real firmware sources** — all of `addon/usbcdgadget` (command
handlers, BOT state machine, `Update()` chunked-read path) plus the real
`cueparser` — are compiled for the build machine against a set of thin
stub headers (`stubs/`) that stand in for Circle. Nothing under test is
reimplemented or mocked: the same `scsi_toc.cpp` that answers a Windows 98
machine answers the tests.

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

A fake in-memory `IImageDevice` (`harness/fakedisc.h`) provides data-CD,
audio-CD, and mixed-mode discs with deterministic sector contents, and an
instrumented fake `CCDPlayer` records what the SCSI layer asked it to do.

## Why these tests exist

Every case in `tests/` encodes behavior that a real host depends on, and
most encode a bug USBODE actually shipped:

| Test | Shipped bug it would have caught |
| --- | --- |
| `mode_sense10_medium_type_*` | #164: hardcoded medium type 0x13 broke Win98 CD audio ("data or no disc loaded") |
| `mode_sense10_page0e_win98_golden` | Byte-exact MODE SENSE page 0x0E response retail Win98 SE accepted before PLAY AUDIO (from a Trace Lab golden capture); also the pad-to-allocation-length fix |
| `read_toc_legacy_cdb9_session_info` | Win9x session-info request encoded in CDB[9] answered with a full TOC |
| `read_toc_format0_lba` residue check | CSW residue reported as 0 on short responses made Win98 usbstor.sys discard and retry forever |
| `read_blocked_by_unit_attention` | Missing STALL before CSW reset the device on Windows 11 xHCI |
| `read10_*` | Boundary clamping, multi-chunk `Update()` batching (32 blocks HS / 16 blocks FS), residue on truncated reads |
| `play_audio_*`, `read_subchannel_*` | The full MCICDA analog-audio sequence (the one Win98 QuickInstall's replaced USB stack never sends — oerg866/win98-quickinstall#151) |

The bench itself found one latent firmware bug on day one: passing a
device to the `CUSBCDGadget` constructor makes `SetDevice()` delete the
device it was just handed and continue using the freed pointer
(production code always passes `nullptr` and calls `SetDevice()` later,
so the path is dormant — see `harness/bench.cpp`).

## Layout

```
test/host/
  Makefile           host build; `make` = build + run
  stubs/             minimal Circle/service headers (circle/, cdplayer/, ...)
  harness/
    stubs.cpp        logger/scheduler/timer/endpoint implementations
    testbus.h        records BeginTransfer()/Stall() from the gadget
    fakedisc.*       in-memory disc images + cue sheets
    bench.*          the virtual USB host
    framework.*      tiny TEST()/CHECK() runner
  tests/             one file per command family
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

Drop the file in `tests/` — the Makefile globs it, the `TEST` macro
registers it.

When a Trace Lab capture from a real machine shows a request/response
pair worth preserving (like the Win98 page 0x0E case), turn it into a
byte-exact test: that is the cheapest way to convert one afternoon of
hardware debugging into permanent coverage.

## What this does not cover (Tier 2 ideas)

Host-side tests cannot see real USB timing, the DWC controller, IRQ
interleaving, FatFs, or actual host OS quirks. The natural next tier is
hardware-in-the-loop: a PC with `sg3_utils` issuing the same command set
to a real USBODE over USB, comparing Trace Lab captures against golden
traces (`usbode_trace.py compare`). QEMU is not a shortcut here — its
dwc2 model is host-mode only, so the gadget side cannot run under
emulation.
