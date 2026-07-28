#!/usr/bin/env bash
# QEMU first-boot integration test for USBODE.
#
# Builds a virgin 1 GiB SD card from a dist tree (mkcard.py), boots the real
# kernel under QEMU, lets USBODE's own first-boot setup partition/format the
# card and reboot, waits for the second boot to reach the ejected-empty idle
# state, then validates the ENTIRE boot log (validate_boot_log.py).
#
# Presets (what CI runs):
#   run-boot-test.sh --preset 64bit --dist dist64 --buildinfo buildinfo.json --out out64
#   run-boot-test.sh --preset 32bit --dist dist   --buildinfo buildinfo.json --out out32
#
# 64bit: kernel8.img on raspi3b; observed via serial (-append logdev=ttyS1).
# 32bit: kernel.img on raspi0 via -bios (QEMU rule for 32-bit raspi machines);
#        -bios forbids -append, so serial stays silent and the test reads the
#        file log (0:/usbode-logs.txt) off the card instead.
#
# Everything scratch (card image, ~1 GiB) lives under --out and is deleted on
# exit unless --keep is given; logs are always kept in --out for CI artifacts.
#
# Requirements on the host: qemu-system-aarch64 and/or qemu-system-arm,
# mtools (mformat/mcopy/mmd), python3. See README.md.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PRESET=""
DIST=""
BUILDINFO=""
OUT="qemu-boot-out"
TIMEOUT=420          # hard cap; success normally reached well under 120 s
KEEP=0
EXTRA_VALIDATE=()

usage() { grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --preset)     PRESET="$2"; shift 2;;
        --dist)       DIST="$2"; shift 2;;
        --buildinfo)  BUILDINFO="$2"; shift 2;;
        --out)        OUT="$2"; shift 2;;
        --timeout)    TIMEOUT="$2"; shift 2;;
        --keep)       KEEP=1; shift;;
        --no-require-ready) EXTRA_VALIDATE+=(--no-require-ready); shift;;
        -h|--help)    usage;;
        *) echo "run-boot-test: unknown argument: $1" >&2; exit 2;;
    esac
done

case "$PRESET" in
    64bit)
        QEMU_BIN=qemu-system-aarch64; MACHINE=raspi3b; KERNEL=kernel8.img
        ARCH=64; SOURCE=serial; LOAD_FLAG=-kernel
        APPEND="logdev=ttyS1 loglevel=4 sounddev=sndi2s fast=true usbspeed=high"
        ;;
    32bit)
        QEMU_BIN=qemu-system-arm; MACHINE=raspi0; KERNEL=kernel.img
        ARCH=32; SOURCE=filelog; LOAD_FLAG=-bios
        APPEND=""   # -bios cannot take -append; observed via the file log
        ;;
    *) echo "run-boot-test: --preset must be 64bit or 32bit" >&2; exit 2;;
esac
[ -n "$DIST" ] || { echo "run-boot-test: --dist is required" >&2; exit 2; }
[ -n "$BUILDINFO" ] || { echo "run-boot-test: --buildinfo is required" >&2; exit 2; }
[ -f "$DIST/$KERNEL" ] || { echo "run-boot-test: $DIST/$KERNEL not found" >&2; exit 2; }
[ -f "$BUILDINFO" ] || { echo "run-boot-test: $BUILDINFO not found" >&2; exit 2; }

missing=()
for tool in "$QEMU_BIN" mformat mcopy python3; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
if [ ${#missing[@]} -gt 0 ]; then
    echo "run-boot-test: missing required tool(s): ${missing[*]}" >&2
    echo "  Debian/Ubuntu: sudo apt-get install qemu-system-arm mtools python3" >&2
    echo "  (qemu-system-arm provides both qemu-system-arm and qemu-system-aarch64)" >&2
    echo "  macOS: brew install qemu mtools" >&2
    exit 2
fi

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
CARD="$OUT/card-$PRESET.img"
SERIAL_LOG="$OUT/serial-$PRESET.log"
QEMU_ERR="$OUT/qemu-stderr-$PRESET.log"
FILE_LOG="$OUT/usbode-logs-$PRESET.txt"

QPID=""
cleanup() {
    status=$?
    if [ -n "$QPID" ] && kill -0 "$QPID" 2>/dev/null; then
        kill "$QPID" 2>/dev/null || true
        wait "$QPID" 2>/dev/null || true
    fi
    # The card image is ~1 GiB of scratch per run: never leave it behind on
    # a build agent unless explicitly asked to.
    if [ "$KEEP" -eq 0 ]; then
        rm -f "$CARD"
    fi
    exit $status
}
trap cleanup EXIT INT TERM

echo "run-boot-test[$PRESET]: building virgin card from $DIST"
python3 "$HERE/mkcard.py" --dist "$DIST" --out "$CARD"

extract_filelog() {
    rm -f "$FILE_LOG"
    mcopy -n -i "$CARD@@1048576" ::/usbode-logs.txt "$FILE_LOG" 2>/dev/null
}

DONE_RE='USBODE .* ready|SCSITBService::Run started'
FAIL_RE='assertion failed|: stack\[|Kernel panic|Failed to mount images partition|Cannot mount drive|Setup or Upgrade failed'
SESSION_RE='New Session Started'
SETUP_DONE_RE='Partition 2 formatted as ExFAT successfully'

# boot_phase <done-condition> <settle-secs>
#   done-condition: "serial-second-session" | "filelog-setup-done" | "filelog-second-session"
# Launches QEMU, polls for the condition (or guest exit, failure, timeout),
# waits <settle-secs> for late writes/flushes, then stops the guest.
boot_phase() {
    local condition=$1 settle=$2 verdict="timeout" started now
    QEMU_ARGS=(
        -M "$MACHINE" "$LOAD_FLAG" "$DIST/$KERNEL"
        -drive "file=$CARD,if=sd,format=raw"
        -serial "file:$SERIAL_LOG"
        -display none -monitor none
    )
    [ -n "$APPEND" ] && QEMU_ARGS+=(-append "$APPEND")

    "$QEMU_BIN" "${QEMU_ARGS[@]}" 2>>"$QEMU_ERR" &
    QPID=$!
    started=$(date +%s)
    while :; do
        if ! kill -0 "$QPID" 2>/dev/null; then verdict="qemu-exited"; break; fi
        now=$(date +%s)
        if [ $((now - started)) -ge "$TIMEOUT" ]; then verdict="timeout"; break; fi
        sleep 5
        case "$condition" in
            serial-second-session)
                # single invocation: the in-guest watchdog reboot works on
                # raspi3b, so setup boot and normal boot share one serial log
                if grep -qE "$FAIL_RE" "$SERIAL_LOG" 2>/dev/null; then verdict="guest-failure"; break; fi
                if [ "$(grep -cE 'Circle [0-9]+ started on' "$SERIAL_LOG" 2>/dev/null || true)" -ge 2 ] \
                   && awk '/Circle [0-9]+ started on/{n++} n>=2' "$SERIAL_LOG" | grep -qE "$DONE_RE"; then
                    verdict="done"; break
                fi
                ;;
            filelog-setup-done)
                # serial is silent (-bios): snapshot the file log off the live
                # card; mcopy racing guest FAT writes just means "not yet"
                if extract_filelog && grep -qE "$SETUP_DONE_RE" "$FILE_LOG" 2>/dev/null; then
                    verdict="done"; break
                fi
                ;;
            filelog-second-session)
                if extract_filelog \
                   && [ "$(grep -c "$SESSION_RE" "$FILE_LOG" 2>/dev/null || true)" -ge 2 ] \
                   && awk "/$SESSION_RE/{n++} n>=2" "$FILE_LOG" | grep -qE "$DONE_RE"; then
                    verdict="done"; break
                fi
                ;;
        esac
    done

    [ "$verdict" = done ] && sleep "$settle"
    kill "$QPID" 2>/dev/null || true
    wait "$QPID" 2>/dev/null || true
    QPID=""
    echo "run-boot-test[$PRESET]: guest stopped (reason: $verdict)"
}

if [ "$PRESET" = 64bit ]; then
    echo "run-boot-test[$PRESET]: booting $KERNEL on $MACHINE (single run: setup + in-guest reboot)"
    : > "$SERIAL_LOG"
    boot_phase serial-second-session 10
else
    # QEMU's raspi0 machine does not come back from the guest-initiated
    # watchdog reset (verified empirically; raspi3b does). Run the setup boot
    # and the normal boot as two QEMU invocations over the SAME card - the
    # file log accumulates one session per boot either way, so validation is
    # identical to a true reboot.
    echo "run-boot-test[$PRESET]: phase 1/2 - setup boot of $KERNEL on $MACHINE"
    : > "$SERIAL_LOG"
    # settle 30 s: covers the (empty) images sweep, the 10 s pre-reboot sleep
    # and the reboot attempt, so setup's card writes are complete
    boot_phase filelog-setup-done 30
    echo "run-boot-test[$PRESET]: phase 2/2 - normal boot on the initialized card"
    boot_phase filelog-second-session 10
fi

echo "run-boot-test[$PRESET]: validating"

# final file-log snapshot now that the guest is stopped
if ! extract_filelog; then
    if [ "$SOURCE" = filelog ]; then
        echo "run-boot-test[$PRESET]: FAIL - could not extract 0:/usbode-logs.txt from the card" >&2
        exit 1
    fi
    echo "run-boot-test[$PRESET]: note: no file log extracted (non-fatal for serial preset)"
fi

if [ "$SOURCE" = serial ]; then LOG="$SERIAL_LOG"; else LOG="$FILE_LOG"; fi
# ${arr[@]+...} form: empty-array expansion that survives `set -u` on the
# macOS-default bash 3.2, for local runs.
python3 "$HERE/validate_boot_log.py" \
    --log "$LOG" --source "$SOURCE" --scenario first-boot \
    --buildinfo "$BUILDINFO" \
    --expect-kernel "$KERNEL" --expect-arch "$ARCH" \
    ${EXTRA_VALIDATE[@]+"${EXTRA_VALIDATE[@]}"}
