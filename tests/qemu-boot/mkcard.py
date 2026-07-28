#!/usr/bin/env python3
"""Build a virgin USBODE SD-card image for the QEMU boot test.

Replicates the topology a user gets from flashing a release .img
(scripts/create-img.sh) onto a larger card:

  partition 1: FAT32, the dist/ tree (boot files, config, firmware, ...)
  partition 2: a small unformatted placeholder

so that USBODE's own first-boot setup (addon/setupstatus) has real work to
do under emulation: it must grow partition 2 to the end of the card by
rewriting the MBR, format it as exFAT with f_mkfs, sweep 0:/images into it,
and reboot. The QEMU harness then watches the second boot come up normally.

Two deliberate deviations from a stock card, both required under QEMU and
explained in README.md:
  - displayhat is forced to "none"  (no SPI display emulation)
  - ejected=1 is forced             (the drive comes up empty, so the USB
                                     gadget hardware - which QEMU cannot
                                     emulate in device mode - is never
                                     initialized)
  - the images/ directory is shipped EMPTY (same reason: a present image
    file would trigger gadget init from ProcessPendingMount)

Needs mtools (mformat/mcopy/mmd) on PATH. No root, no loop devices.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

SECTOR = 512
MIB = 1024 * 1024
# mformat geometry: 64 heads x 32 sectors/track x 512 B = 1 MiB per cylinder,
# so the cylinder count equals the partition size in MiB.
HEADS, SPT = 64, 32

FORCED_USBODE_KEYS = {
    "displayhat": "none",  # no SPI/I2C display under QEMU
    "ejected": "1",        # come up as an empty (ejected) drive: never
                           # touches the DWC2 gadget, which QEMU lacks
}

MINIMAL_CONFIG = """# Synthesized by tests/qemu-boot/mkcard.py (dist had no config.txt).
# QEMU never reads this as firmware config; only USBODE's ConfigService does.

[usbode]
current_image=image.iso
logfile=0:/usbode-logs.txt
default_volume=255
screen_timeout=25
"""

# ConfigService asserts cmdline.txt exists and is non-empty. Content mirrors
# sdcard/cmdline.txt; under QEMU Circle itself takes its options from -append,
# not from this file.
DEFAULT_CMDLINE = "sounddev=sndi2s fast=true usbspeed=high\n"


def run(cmd, **kw):
    res = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if res.returncode != 0:
        sys.exit(f"mkcard: `{' '.join(cmd)}` failed:\n{res.stdout}{res.stderr}")
    return res


def need_tools():
    missing = [t for t in ("mformat", "mcopy", "mmd") if shutil.which(t) is None]
    if missing:
        sys.exit(
            "mkcard: missing tool(s): %s\n"
            "Install mtools (Debian/Ubuntu: `apt-get install mtools`, "
            "macOS: `brew install mtools`)." % ", ".join(missing)
        )


def rewrite_config(text):
    """Force the QEMU-required [usbode] keys, preserving everything else."""
    lines = text.splitlines()
    out = []
    in_usbode = False
    seen_section = False
    pending = dict(FORCED_USBODE_KEYS)

    def flush_pending():
        for k, v in pending.items():
            out.append(f"{k}={v}")
        pending.clear()

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if in_usbode:
                flush_pending()  # leaving [usbode]: emit any key not seen inline
            in_usbode = stripped.lower() == "[usbode]"
            if in_usbode:
                seen_section = True
            out.append(line)
            continue
        if in_usbode:
            key = stripped.split("=", 1)[0].strip().lower() if "=" in stripped else None
            if key in pending:
                out.append(f"{key}={pending.pop(key)}")
                continue
            if key in FORCED_USBODE_KEYS and key not in pending:
                continue  # duplicate of a key we already forced
        out.append(line)
    if in_usbode:
        flush_pending()
    if not seen_section:
        out.append("")
        out.append("[usbode]")
        for k, v in FORCED_USBODE_KEYS.items():
            out.append(f"{k}={v}")
    return "\n".join(out) + "\n"


def build_boot_volume(dist, boot_mib, workdir):
    vol = os.path.join(workdir, "p1.fat")
    run(["mformat", "-C", "-i", vol, "-t", str(boot_mib), "-h", str(HEADS),
         "-s", str(SPT), "-F", "-v", "BOOTFS", "::"])

    # config.txt: dist copy with forced keys, or a synthesized minimal one
    cfg_src = os.path.join(dist, "config.txt")
    if os.path.isfile(cfg_src):
        with open(cfg_src, "r", errors="replace") as f:
            cfg = rewrite_config(f.read())
    else:
        print(f"mkcard: note: {cfg_src} not found, synthesizing a minimal config.txt")
        cfg = rewrite_config(MINIMAL_CONFIG)
    cfg_tmp = os.path.join(workdir, "config.txt")
    with open(cfg_tmp, "w") as f:
        f.write(cfg)
    run(["mcopy", "-i", vol, cfg_tmp, "::/config.txt"])

    # cmdline.txt: must exist and be non-empty (ConfigService asserts)
    cmd_src = os.path.join(dist, "cmdline.txt")
    cmd_tmp = os.path.join(workdir, "cmdline.txt")
    if os.path.isfile(cmd_src) and os.path.getsize(cmd_src) > 0:
        shutil.copyfile(cmd_src, cmd_tmp)
    else:
        print(f"mkcard: note: {cmd_src} missing/empty, using default cmdline.txt")
        with open(cmd_tmp, "w") as f:
            f.write(DEFAULT_CMDLINE)
    run(["mcopy", "-i", vol, cmd_tmp, "::/cmdline.txt"])

    # the rest of the dist tree, except what we already wrote and except
    # images/ content (shipped empty - see module docstring)
    for entry in sorted(os.listdir(dist)):
        if entry in ("config.txt", "cmdline.txt", "images"):
            continue
        src = os.path.join(dist, entry)
        if os.path.isdir(src):
            run(["mcopy", "-s", "-i", vol, src, f"::/{entry}"])
        else:
            run(["mcopy", "-i", vol, src, f"::/{entry}"])
    run(["mmd", "-i", vol, "::/images"])
    return vol


def write_card(out, size_mib, boot_mib, placeholder_mib, boot_vol):
    total_sectors = size_mib * MIB // SECTOR
    p1_start = 2048                      # 1 MiB alignment, same as create-img.sh
    p1_sectors = boot_mib * MIB // SECTOR
    p2_start = p1_start + p1_sectors
    p2_sectors = placeholder_mib * MIB // SECTOR
    if p2_start + p2_sectors > total_sectors:
        sys.exit("mkcard: partitions exceed card size")

    def entry(boot, ptype, start, count):
        # CHS fields set to the LBA marker bytes; everything reads the LBA fields
        return struct.pack("<B3sB3sII", boot, b"\xfe\xff\xff", ptype,
                           b"\xfe\xff\xff", start, count)

    mbr = bytearray(SECTOR)
    mbr[0x1BE:0x1CE] = entry(0x80, 0x0C, p1_start, p1_sectors)      # FAT32 LBA
    mbr[0x1CE:0x1DE] = entry(0x00, 0x07, p2_start, p2_sectors)      # exFAT-to-be
    mbr[510:512] = b"\x55\xAA"

    with open(out, "wb") as f:
        f.truncate(size_mib * MIB)
        f.write(mbr)
        f.seek(p1_start * SECTOR)
        with open(boot_vol, "rb") as v:
            shutil.copyfileobj(v, f)
        # partition 2 stays zeroed: unformatted on purpose, USBODE formats it

    print(f"mkcard: wrote {out}: {size_mib} MiB card, "
          f"p1 FAT32 {boot_mib} MiB @ LBA {p1_start}, "
          f"p2 placeholder {placeholder_mib} MiB @ LBA {p2_start} (unformatted)")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dist", required=True, help="dist/ or dist64/ tree to ship on partition 1")
    ap.add_argument("--out", required=True, help="output card image path")
    ap.add_argument("--size-mib", type=int, default=1024,
                    help="total card size in MiB; must be a power of two (QEMU sd rule)")
    ap.add_argument("--boot-mib", type=int, default=200,
                    help="partition 1 size in MiB (create-img.sh uses 200)")
    ap.add_argument("--placeholder-mib", type=int, default=4,
                    help="partition 2 placeholder size in MiB (create-img.sh leaves 4)")
    args = ap.parse_args()

    if args.size_mib & (args.size_mib - 1):
        sys.exit("mkcard: --size-mib must be a power of two (QEMU refuses other SD sizes)")
    if not os.path.isdir(args.dist):
        sys.exit(f"mkcard: dist dir not found: {args.dist}")

    need_tools()
    with tempfile.TemporaryDirectory(prefix="mkcard.") as workdir:
        vol = build_boot_volume(args.dist, args.boot_mib, workdir)
        write_card(args.out, args.size_mib, args.boot_mib, args.placeholder_mib, vol)


if __name__ == "__main__":
    main()
