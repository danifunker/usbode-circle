#!/usr/bin/env python3
"""Deploy a freshly built kernel to a running USBODE over WiFi, then reboot it.

Uses the FTP server built into USBODE (user/pass cdrom/cdrom) to write the
kernel to the boot partition (volume SD), then hits /api/reboot. Saves
pulling the SD card for every test build.

The kernel is uploaded under a temporary name, downloaded back and
MD5-verified against the local file, and only then renamed into place, so
a corrupted transfer can't produce an unbootable card.

The kernel being replaced is kept as <name>.bak, but only the FIRST time:
once a backup exists it is never overwritten, so a run of test builds
can't erase the last known-good image. If a deploy goes bad, rename the
backup over the live name via FTP instead of pulling the SD card. Delete
the .bak by hand when you want a new baseline.

Usage:
    tools/deploy-kernel.py <usbode-ip> [kernel-image]

    kernel-image defaults to src/kernel8.img (64-bit build).
    Pass src/kernel8-32.img (as kernel8-32.img target name) for 32-bit.
"""
import ftplib
import hashlib
import os
import sys
import urllib.request

FTP_USER = "cdrom"
FTP_PASS = "cdrom"
BOOT_VOLUME = "/SD"


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)

    host = sys.argv[1]
    if host.endswith(".img") or os.path.exists(host):
        sys.exit(f"First argument must be the USBODE's IP address/hostname "
                 f"(got '{host}').\nUsage: deploy-kernel.py <usbode-ip> [kernel-image]")

    if len(sys.argv) > 2:
        kernel = sys.argv[2]
    else:
        # Default to src/kernel8.img relative to the repo root, so the
        # script works no matter which directory it is run from.
        repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        kernel = os.path.join(repo_root, "src", "kernel8.img")

    if not os.path.exists(kernel):
        sys.exit(f"Kernel image not found: {kernel}\nBuild it first, or pass "
                 f"the path explicitly as the second argument.")

    target = os.path.basename(kernel)
    temp = target + ".new"

    size = os.path.getsize(kernel)
    print(f"Uploading {kernel} ({size:,} bytes) to {host}:{BOOT_VOLUME}/{target} ...")

    ftp = ftplib.FTP(host, timeout=30)
    ftp.login(FTP_USER, FTP_PASS)
    ftp.cwd(BOOT_VOLUME)

    with open(kernel, "rb") as f:
        ftp.storbinary(f"STOR {temp}", f)

    # Verify content, not just size: download the upload back and compare
    # MD5 against the local file before touching the live kernel.
    local_md5 = hashlib.md5(open(kernel, "rb").read()).hexdigest()
    remote_md5 = hashlib.md5()
    remote_bytes = 0

    def _hash_chunk(chunk):
        nonlocal remote_bytes
        remote_md5.update(chunk)
        remote_bytes += len(chunk)

    print("Verifying upload (read-back + MD5) ...")
    ftp.retrbinary(f"RETR {temp}", _hash_chunk)
    if remote_bytes != size or remote_md5.hexdigest() != local_md5:
        ftp.delete(temp)
        ftp.quit()
        sys.exit(f"Upload verification FAILED ({remote_bytes:,} bytes, "
                 f"md5 {remote_md5.hexdigest()} != {local_md5}); aborted, "
                 f"existing kernel untouched.")

    # Keep the old kernel as .bak so a bad deploy is recoverable over FTP
    # (rename it back) instead of requiring an SD pull.
    #
    # The backup is written ONCE and never overwritten. It used to be replaced
    # on every deploy, which meant two deploys in a row silently destroyed the
    # only copy of the kernel you started with: the first deploy backed up the
    # good kernel, the second overwrote that backup with the first test build,
    # and there was no way back to a known-good image without an SD pull.
    # Preserving the first backup keeps that escape hatch no matter how many
    # test builds get deployed on top of it.
    #
    # (USBODE's FTP server reports missing files as 450, not 550, so catch
    # both the temp- and perm-error classes.)
    backup = target + ".bak"
    existing = set(ftp.nlst())
    have_backup = backup in existing

    if not have_backup:
        try:
            ftp.rename(target, backup)
            backup_note = f"previous kernel saved as {backup}"
        except (ftplib.error_perm, ftplib.error_temp):
            backup_note = "no existing kernel to back up"
    else:
        # A backup already exists and is not ours to clobber. Drop the kernel
        # being replaced rather than keeping a second copy: space on the boot
        # partition is tight and only the original is worth preserving.
        try:
            ftp.delete(target)
        except (ftplib.error_perm, ftplib.error_temp):
            pass
        backup_note = f"kept the existing {backup} (not overwritten)"

    ftp.rename(temp, target)
    ftp.quit()
    print(f"Upload verified (md5 {local_md5}); {backup_note}.")
    if have_backup:
        print(f"NOTE: {backup} still holds whatever was there before the FIRST "
              f"deploy. Delete it by hand if you want a fresh baseline.")

    print("Rebooting USBODE ...")
    try:
        urllib.request.urlopen(f"http://{host}/api/reboot", timeout=5).read()
    except Exception:
        pass  # the device drops the connection as it reboots
    print("Done - give it ~20s to come back up.")


if __name__ == "__main__":
    main()
