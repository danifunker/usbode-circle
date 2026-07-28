# QEMU first-boot integration test

Boots the **real, unmodified kernel images from the build** (real Circle
headers, real drivers — no stubs) under QEMU, walks them through USBODE's
own first-boot experience on a virgin 1 GiB SD card, and validates the
**entire** boot log against the exact version being built.

In CI (`.github/workflows/main.yaml`) this is its own pipeline stage
between build and release: `build` uploads the freshly packaged
`dist`/`dist64` trees plus `buildinfo.json` as `qemu-boot-input-*`
artifacts, the two matrix jobs `qemu-boot-test (32bit)` and
`qemu-boot-test (64bit)` consume them on the self-hosted agent
(`fail-fast: false`, so one architecture failing still reports the
other), and the `release` job publishes only when both pass — a kernel
that no longer boots cannot be **released**. (Per-run build artifacts do
upload before the tests, since the tests consume them; the release is the
gated product.)

This complements `integration-tests/` (which compiles the SCSI/BOT and
disc-reader code against stub Circle headers on the host): that suite
checks protocol behavior; this one checks that the shipped binaries
actually come up on the real framework.

## What one run does

```
tests/qemu-boot/run-boot-test.sh --preset 64bit --dist dist64 --buildinfo buildinfo.json --out qemu-boot-out
tests/qemu-boot/run-boot-test.sh --preset 32bit --dist dist   --buildinfo buildinfo.json --out qemu-boot-out
```

1. `mkcard.py` builds a **virgin card** replicating what a user gets from
   flashing a release image onto a bigger SD card: partition 1 is FAT32
   with the whole `dist/` tree; partition 2 is the same 4 MiB placeholder
   `scripts/create-img.sh` leaves — except here it is deliberately
   **unformatted**, so USBODE must do all the work.
2. QEMU boots the kernel. USBODE detects the placeholder (`<= 10 MB`),
   runs first-boot setup: rewrites the MBR to grow partition 2 to the full
   card, formats it as exFAT with `f_mkfs`, sweeps `0:/images` (empty by
   design, see below), and reboots.
3. The second boot must come up **without** re-triggering setup, mount the
   freshly created exFAT partition, honor the persisted `ejected=1` state
   ("Boot: drive was ejected at power-off, coming up empty" — the eject
   persistence added in 3.2.3), start every non-network service, and print
   the terminal checkpoint `USBODE <version> ready` (src/kernel.cpp) before
   entering the main loop.
4. `validate_boot_log.py` then judges the **whole log**, not a grep list:
   - an *ordered* chain of required events per boot session (setup boot,
     then normal boot) — progress must happen in sequence;
   - *unordered* required events for scheduler-dependent task startup;
   - *forbidden* patterns (asserts, panics, mount failures, any attempt to
     initialize the USB gadget hardware, setup re-triggering after its own
     reboot = reboot loop);
   - a *severity sweep*: any error-looking line anywhere that is not on
     the explicit allowlist fails the run, so new failure modes cannot
     hide behind green markers;
   - the version handshake: the booted kernel must identify as exactly
     this build. Expectations are derived at run time from the
     `buildinfo.json` the build just generated
     (`scripts/generate-buildinfo.sh`), mirroring
     `CGitInfo::UpdateFormattedVersions()`:
     `x.y.z[-<build>][-<branch-if-not-main>]-<commit7>`.

Wall-clock per preset is normally 1–3 minutes; the pollers stop the guest
as soon as the terminal state (or a deterministic failure) is visible, and
a hard `--timeout` (default 420 s) caps hangs.

## The two presets

| preset | kernel | QEMU machine | loaded via | observed via | invocations |
|---|---|---|---|---|---|
| `64bit` | `dist64/kernel8.img` (Pi 3 / Zero 2 W) | `qemu-system-aarch64 -M raspi3b` | `-kernel` | serial (`-append "logdev=ttyS1 ..."` → PL011 → log file) | one — the in-guest watchdog reboot works on raspi3b |
| `32bit` | `dist/kernel.img` (Pi Zero / W) | `qemu-system-arm -M raspi0` | `-bios` (QEMU rule: 32-bit raspi machines do not enter raw `-kernel` images) | the **file log** `0:/usbode-logs.txt` pulled off the card with `mcopy` — `-bios` forbids `-append`, so Circle's log device stays `tty1`, a sink in this `SCREEN_HEADLESS` build | two, same card — QEMU's raspi0 never returns from the guest watchdog reset (verified empirically), so the harness re-invokes QEMU for the second boot; the file log accumulates one session per boot either way |

Together they cover both architectures (AArch64/AArch32), both timer/SD
code paths (RASPPI 3 vs RASPPI 1), and USBODE's two flagship boards.

## Why the card is configured the way it is

`mkcard.py` forces exactly three deviations from a stock user card, all in
service of one boundary: **QEMU has no DWC2 device-mode (gadget)
emulation** — its dwc2 model is host-only, device registers read as
zeros. If gadget init ever runs under QEMU it fails and the firmware
asserts (`cdromservice.cpp`: `Failed to initialize CD Gadget`).

- **`ejected=1`** (config.txt `[usbode]`): the drive comes up as an empty
  ejected drive — the 3.2.3 eject-persistence path, asserted explicitly.
- **empty `images/`**: gadget hardware init is *lazy* — it happens inside
  the first `CDROMService::SetDevice()`, which only runs when an image
  file gets mounted. Note `ejected=1` alone is NOT enough: the boot path
  still loads the remembered image and calls `SetDevice()` (so Insert is
  instant on real hardware); only the absence of image files keeps boot
  away from the DWC2 entirely. If that lazy-init property ever changes
  (gadget initialized eagerly at startup), this test will start failing
  with the `gadget init attempted` forbidden pattern — that is this
  coupling being detected, not a test bug; see "coverage boundary" below.
- **`displayhat=none`**: the shipped default (`pirateaudiolineout`) would
  drive an ST7789 over SPI, which QEMU stubs out.

Everything else on the card is the genuine dist tree, including
`cmdline.txt` (ConfigService requires it non-empty) and `config.txt`
(required, INI-parsed; under QEMU only USBODE reads it — there is no GPU
firmware).

## What this deliberately does not cover

- **USB gadget enumeration and SCSI traffic** — impossible under QEMU
  (host-mode-only dwc2; additionally its GSNPSID is `0x4F54294A`, not the
  Pi's `0x4F54280A`, and `raspi3b`'s board model is not in the gadget's
  machine whitelist). The host suite in `integration-tests/` covers the
  command layer; real-hardware testing covers the wire. The test pins this
  boundary: any gadget-init attempt is a *forbidden* event.
- **WLAN, webserver, FTP, mDNS, NTP** — no SDIO WLAN device exists in
  QEMU. The WLAN probe's graceful failure ("WLAN not available -
  continuing without network") is a *required* event; the network services
  legitimately never start.
- **Audio and displays** — PWM/I2S/DMA and SPI panels are stubbed or
  absent in QEMU. (`sounddev=sndi2s` still exercises CCDPlayer
  construction on the 64-bit preset; the sound device itself is created
  lazily on first USB audio activation, which never comes.)
- **`kernel7.img` / `kernel8-32.img`** — 32-bit RASPPI≥2 Circle builds
  panic in `CTimer::Initialize()` under QEMU: `USE_PHYSICAL_COUNTER`
  validates the `ARM_LOCAL_PRESCALER` value (0x6AAAAAB) that only the real
  Pi firmware programs. Not fixable from the harness side; would need a
  `NO_PHYSICAL_COUNTER` rebuild, which is no longer the shipped artifact.
- **Pi 4 / Pi 5 kernels** — `raspi4b` requires QEMU ≥ 9.0 (and its
  Circle compatibility is unverified); there is no Pi 5 machine in QEMU.

## Build agent requirements

The CI steps run on the self-hosted build runner. One-time install:

```
# Debian/Ubuntu
sudo apt-get install qemu-system-arm mtools python3
```

- `qemu-system-arm` provides **both** `qemu-system-arm` and
  `qemu-system-aarch64` on Debian/Ubuntu. Any QEMU ≥ 8.x should work: the
  needed device models (bcm2835_sdhost + the GPIO card mux, the property
  mailbox `GET_COMMAND_LINE` for `-append`, raspi0/raspi3b machines) are
  all present by 8.2; the suite was developed and verified against QEMU
  11.0.3.
- `mtools` (`mformat`/`mcopy`/`mmd`) builds and reads the card images with
  no root and no loop devices.
- `python3` (3.8+, stdlib only).
- Disk: ~2 GiB transient (one 1 GiB card per preset, deleted on exit —
  even on failure — by the runner's trap; CI additionally `rm -rf`s the
  output dir after uploading logs).
- macOS (local development): `brew install qemu mtools`.

## Running locally

CI runs the exact same script you can run by hand. Against a CI-style
build tree (has `config.txt`, `cmdline.txt`, `buildinfo.json`), from the
repo root after `make package`:

```
make qemu-boot-test          # both presets, 32-bit then 64-bit
```

or a single preset directly:

```
./tests/qemu-boot/run-boot-test.sh --preset 64bit --dist dist64 --buildinfo buildinfo.json --out /tmp/qemu-boot
```

Against a downloaded release zip (no `config.txt`/`cmdline.txt` inside —
`mkcard.py` synthesizes minimal ones; grab `buildinfo.json` from the same
release):

```
mkdir rel64 && unzip usbode-<ver>-64bit.zip -d rel64
./tests/qemu-boot/run-boot-test.sh --preset 64bit --dist rel64 \
    --buildinfo buildinfo.json --out /tmp/qemu-boot --no-require-ready
```

`--no-require-ready` tolerates kernels older than the `USBODE <version>
ready` marker (added on this branch); CI never passes it.

Debugging aids:

- `--keep` retains the card image; inspect it with
  `mcopy -i card-64bit.img@@1048576 ::/usbode-logs.txt -` (the `@@offset`
  is partition 1 at 1 MiB).
- The serial log, QEMU stderr, and the extracted file log always land in
  `--out`.
- To watch a boot interactively, run the QEMU command by hand without
  `-display none -monitor none` and with `-serial stdio`.

## Maintenance notes

- **Boot log lines are load-bearing.** The validator keys on exact
  messages (`validate_boot_log.py`: `build_events`, `ALLOWED_NOISE`,
  `FORBIDDEN_ANYWHERE`). If you rename or remove one of those log lines —
  including `USBODE %s ready` in `src/kernel.cpp` — update the validator
  in the same PR.
- **New error-looking log lines fail the sweep by design.** If a
  legitimate, expected-under-QEMU line trips it, add a tightly scoped
  regex to `ALLOWED_NOISE` with a comment saying why it is benign.
- **The file logger is lossy under burst load** (drops mid-boot lines,
  e.g. parts of the banner) **and at the setup reboot** (loses its tail).
  Events known to be affected are marked `lossy=True` and are
  required-on-serial / optional-on-filelog. Do not promote them for the
  filelog profile without re-verifying.
- **Known-good empirical baseline** (2026-07-28, QEMU 11.0.3 on macOS,
  v3.2.3 build-975 release artifacts): both presets PASS end-to-end,
  including the genuine in-guest reboot on raspi3b. If CI's QEMU behaves
  differently, compare against that baseline first.
- **Upstream note**: Circle's docs claim its SDHOST driver does not work
  under QEMU (`circle-stdlib/libs/circle/include/circle/sysconfig.h`,
  "The SDHOST device is ... not [supported] by QEMU", and the
  `configure --qemu` option forces `NO_SDHOST` accordingly). Modern QEMU
  models `bcm2835_sdhost` plus the GPIO48-53 card mux, and this suite
  boots the stock SDHOST-based kernels through full read/write workloads
  (FAT mounts, exFAT mkfs, file-log writes). No `--qemu`/`NO_SDHOST`
  rebuild is needed — which is the whole point: the artifacts under test
  are the artifacts being shipped.

## Files

```
mkcard.py             virgin-card builder (mtools + hand-written MBR, no root)
run-boot-test.sh      orchestrator: presets, QEMU lifecycle, pollers, cleanup
validate_boot_log.py  whole-log validator (sessions, ordered events, sweep)
```
