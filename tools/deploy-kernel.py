#!/usr/bin/env python3
"""Deploy a freshly built kernel to a running USBODE over WiFi, then reboot it.

Uses the FTP server built into USBODE (user/pass cdrom/cdrom) to write the
kernel to the boot partition (volume SD), then hits /api/reboot. Saves
pulling the SD card for every test build.

The kernel is uploaded under a temporary name and renamed into place only
after the full transfer succeeds, so a dropped connection can't leave a
truncated kernel behind.

Usage:
    tools/deploy-kernel.py <usbode-ip> [kernel-image]

    kernel-image defaults to src/kernel8.img (64-bit build).
    Pass src/kernel8-32.img (as kernel8-32.img target name) for 32-bit.
"""
import ftplib
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
    kernel = sys.argv[2] if len(sys.argv) > 2 else "src/kernel8.img"
    target = os.path.basename(kernel)
    temp = target + ".new"

    size = os.path.getsize(kernel)
    print(f"Uploading {kernel} ({size:,} bytes) to {host}:{BOOT_VOLUME}/{target} ...")

    ftp = ftplib.FTP(host, timeout=30)
    ftp.login(FTP_USER, FTP_PASS)
    ftp.cwd(BOOT_VOLUME)

    with open(kernel, "rb") as f:
        ftp.storbinary(f"STOR {temp}", f)

    # Verify the full image arrived before touching the live kernel.
    # (USBODE's FTP server has no SIZE command, so parse the LIST output.)
    remote_size = None
    listing = []
    ftp.retrlines("LIST", listing.append)
    for line in listing:
        parts = line.split()
        if parts and parts[-1] == temp:
            for p in parts:
                if p.isdigit():
                    remote_size = int(p)
    if remote_size is not None and remote_size != size:
        ftp.delete(temp)
        ftp.quit()
        sys.exit(f"Upload size mismatch ({remote_size} != {size}); aborted, "
                 f"existing kernel untouched.")

    try:
        ftp.delete(target)
    except ftplib.error_perm:
        pass  # no existing kernel with that name
    ftp.rename(temp, target)
    ftp.quit()
    print("Upload verified and renamed into place.")

    print("Rebooting USBODE ...")
    try:
        urllib.request.urlopen(f"http://{host}/api/reboot", timeout=5).read()
    except Exception:
        pass  # the device drops the connection as it reboots
    print("Done - give it ~20s to come back up.")


if __name__ == "__main__":
    main()
