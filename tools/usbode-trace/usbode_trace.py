#!/usr/bin/env python3
"""Decoder for USBODE Trace Lab .utrace files.

Usage:
    usbode_trace.py info   <file.utrace>   header summary
    usbode_trace.py decode <file.utrace>   full event stream
    usbode_trace.py stats  <file.utrace>   per-opcode latency; with a
                                           trace_mode=deep capture, also
                                           breaks READ latency into image
                                           read / USB transfer / firmware
                                           phases
    usbode_trace.py compare <A.utrace> <B.utrace>
                                           side-by-side diff of two captures:
                                           per-opcode latency shifts, phase
                                           breakdown, disc-swap timing, and
                                           errors. Use for before/after fix
                                           or firmware-version comparisons.

Mirrors addon/tracelab/traceformat.h. Keep struct layouts in sync with that
header if the format changes.
"""
import os
import struct
import sys

MAGIC = b"USBOTRCE"

FILE_HEADER_FMT = "<8sHHIQIIIIIII"
FILE_HEADER_SIZE = struct.calcsize(FILE_HEADER_FMT)

RECORD_HEADER_FMT = "<IHH"
RECORD_HEADER_SIZE = struct.calcsize(RECORD_HEADER_FMT)

EVENT_NAMES = {
    0x0001: "TRACE_CAPTURE_START",
    0x0002: "TRACE_CAPTURE_STOP",
    0x0005: "TRACE_TRIGGER_FIRED",
    0x0101: "USB_SUSPEND",
    0x0102: "USB_ACTIVATE",
    0x0103: "USB_SPEED_NEGOTIATED",
    0x0300: "BOT_IN_TRANSFER_START",
    0x0301: "BOT_IN_TRANSFER_COMPLETE",
    0x0400: "SCSI_CDB_RECEIVED",
    0x0401: "SCSI_COMMAND_COMPLETE",
    0x0402: "SCSI_SENSE_SET",
    0x0403: "SCSI_MEDIA_STATE",
    0x0500: "IMAGE_READ_START",
    0x0501: "IMAGE_READ_COMPLETE",
    0x0502: "IMAGE_READ_ERROR",
}

CDB_PAYLOAD_FMT = "<BB16s"
COMPLETE_PAYLOAD_FMT = "<BBI"
SENSE_PAYLOAD_FMT = "<BBB"
SPEED_PAYLOAD_FMT = "<B"
TRANSFER_PAYLOAD_FMT = "<I"
IMAGE_READ_PAYLOAD_FMT = "<II"


class TraceFileHeader:
    def __init__(self, raw):
        (self.magic, self.formatVersion, self.headerSize, self.flags,
         self.captureStartTime, self.timerFrequency, self.firmwareVersion,
         self.boardModel, self.bufferSize, self.recordCount,
         self.droppedRecordCount, self.mountedImageHash) = struct.unpack(FILE_HEADER_FMT, raw)

        if self.magic != MAGIC:
            raise ValueError(f"bad magic: {self.magic!r}")


def read_header(f):
    raw = f.read(FILE_HEADER_SIZE)
    if len(raw) != FILE_HEADER_SIZE:
        raise ValueError("file too short for a trace header")
    return TraceFileHeader(raw)


def iter_records(f):
    while True:
        raw = f.read(RECORD_HEADER_SIZE)
        if len(raw) < RECORD_HEADER_SIZE:
            return
        deltaTicks, eventType, payloadLength = struct.unpack(RECORD_HEADER_FMT, raw)
        payload = f.read(payloadLength)
        if len(payload) != payloadLength:
            return
        yield deltaTicks, eventType, payload


def cmd_info(path):
    with open(path, "rb") as f:
        h = read_header(f)
    print(f"format version:     {h.formatVersion}")
    print(f"header size:        {h.headerSize}")
    print(f"capture start:      {h.captureStartTime} ticks")
    print(f"timer frequency:    {h.timerFrequency} Hz")
    print(f"buffer size:        {h.bufferSize} bytes")
    print(f"record count:       {h.recordCount}")
    print(f"dropped records:    {h.droppedRecordCount}")


def format_payload(eventType, payload):
    if eventType == 0x0400 and len(payload) >= struct.calcsize(CDB_PAYLOAD_FMT):
        lun, cdbLength, cdb = struct.unpack(CDB_PAYLOAD_FMT, payload[:struct.calcsize(CDB_PAYLOAD_FMT)])
        cdbHex = cdb[:cdbLength].hex()
        return f"lun={lun} cdb={cdbHex}"
    if eventType == 0x0401 and len(payload) >= struct.calcsize(COMPLETE_PAYLOAD_FMT):
        opcode, status, residue = struct.unpack(COMPLETE_PAYLOAD_FMT, payload[:struct.calcsize(COMPLETE_PAYLOAD_FMT)])
        return f"opcode=0x{opcode:02x} status={status} residue={residue}"
    if eventType == 0x0402 and len(payload) >= struct.calcsize(SENSE_PAYLOAD_FMT):
        senseKey, asc, ascq = struct.unpack(SENSE_PAYLOAD_FMT, payload[:struct.calcsize(SENSE_PAYLOAD_FMT)])
        return f"sense={senseKey:02x}/{asc:02x}/{ascq:02x}"
    if eventType == 0x0403 and len(payload) >= 2:
        states = {0: "NO_MEDIUM", 1: "UNIT_ATTENTION", 2: "READY"}
        frm = states.get(payload[0], f"?{payload[0]}")
        to = states.get(payload[1], f"?{payload[1]}")
        return f"{frm} -> {to}"
    if eventType == 0x0103 and len(payload) >= 1:
        return "full-speed (USB 1.1)" if payload[0] else "high-speed (USB 2.0)"
    if eventType in (0x0300, 0x0301) and len(payload) >= 4:
        (nbytes,) = struct.unpack(TRANSFER_PAYLOAD_FMT, payload[:4])
        return f"bytes={nbytes}"
    if eventType in (0x0500, 0x0501, 0x0502) and len(payload) >= 8:
        lba, nbytes = struct.unpack(IMAGE_READ_PAYLOAD_FMT, payload[:8])
        return f"lba={lba} bytes={nbytes}"
    return payload.hex() if payload else ""


def cmd_decode(path):
    with open(path, "rb") as f:
        h = read_header(f)
        ticks = h.captureStartTime
        for deltaTicks, eventType, payload in iter_records(f):
            ticks += deltaTicks
            seconds = ticks / h.timerFrequency if h.timerFrequency else 0
            name = EVENT_NAMES.get(eventType, f"0x{eventType:04x}")
            print(f"{seconds:12.6f}  {name:24s} {format_payload(eventType, payload)}")


def _percentiles(sorted_vals):
    n = len(sorted_vals)
    return (sorted_vals[0], sorted_vals[n // 2], sum(sorted_vals) / n,
            sorted_vals[int(n * 0.95)], sorted_vals[-1])


def analyze(path):
    """Single pass over a capture, returning everything stats/compare print."""
    from collections import defaultdict

    with open(path, "rb") as f:
        h = read_header(f)
        records = list(iter_records(f))

    freq = h.timerFrequency or 1_000_000
    t = 0
    first_t = last_t = None

    counts = defaultdict(int)
    latencies = defaultdict(list)  # opcode -> [ticks CDB->COMPLETE]
    # Deep-mode phase accumulators for READ-class commands:
    phases = {"storage": [], "usb": [], "firmware": []}
    deep_seen = False
    errors = []
    sense_counts = defaultdict(int)  # "kk/aa/qq" -> count
    # Disc-swap timing from SCSI_MEDIA_STATE transitions (ticks):
    swaps = []  # dicts: no_medium (0->1 gap), ua_to_ready (1->2 gap)
    t_eject = t_ua = None

    # Per-command state
    cur = None  # dict: opcode, t_cdb, storage, usb, t_img_start, t_usb_start

    for delta, etype, payload in records:
        t += delta
        if first_t is None:
            first_t = t
        last_t = t

        if etype == 0x0400:  # CDB received
            opcode = payload[2] if len(payload) >= 3 else 0
            counts[opcode] += 1
            cur = {"op": opcode, "t_cdb": t, "storage": 0, "usb": 0,
                   "t_img": None, "t_usb": None}
        elif etype == 0x0500 and cur:  # image read start
            deep_seen = True
            cur["t_img"] = t
        elif etype == 0x0501 and cur and cur["t_img"] is not None:
            cur["storage"] += t - cur["t_img"]
            cur["t_img"] = None
        elif etype == 0x0300 and cur:  # USB IN transfer start
            deep_seen = True
            cur["t_usb"] = t
        elif etype == 0x0301 and cur and cur["t_usb"] is not None:
            cur["usb"] += t - cur["t_usb"]
            cur["t_usb"] = None
        elif etype == 0x0502:
            errors.append((t / freq, "IMAGE_READ_ERROR"))
        elif etype == 0x0402 and len(payload) >= 3:
            sense_counts[f"{payload[0]:02x}/{payload[1]:02x}/{payload[2]:02x}"] += 1
        elif etype == 0x0403 and len(payload) >= 2:
            frm, to = payload[0], payload[1]
            if to == 0:  # -> NO_MEDIUM: swap begins
                t_eject, t_ua = t, None
            elif to == 1 and t_eject is not None:  # -> UNIT_ATTENTION
                t_ua = t
            elif to == 2 and t_ua is not None:  # -> READY: swap complete
                swaps.append({"no_medium": t_ua - t_eject,
                              "ua_to_ready": t - t_ua})
                t_eject = t_ua = None
        elif etype == 0x0401:  # command complete
            opcode = payload[0] if payload else 0
            status = payload[1] if len(payload) >= 2 else 0
            if status != 0:
                errors.append((t / freq, f"opcode 0x{opcode:02X} status={status}"))
            if cur and cur["op"] == opcode:
                total = t - cur["t_cdb"]
                latencies[opcode].append(total)
                if deep_seen and (cur["storage"] or cur["usb"]):
                    phases["storage"].append(cur["storage"])
                    phases["usb"].append(cur["usb"])
                    phases["firmware"].append(total - cur["storage"] - cur["usb"])
            cur = None

    return {
        "path": path,
        "header": h,
        "freq": freq,
        "span": (last_t - first_t) / freq if first_t is not None else 0,
        "counts": dict(counts),
        "latencies": {op: sorted(ls) for op, ls in latencies.items()},
        "phases": phases,
        "deep": deep_seen,
        "errors": errors,
        "sense_counts": dict(sense_counts),
        "swaps": swaps,
    }


def cmd_stats(path):
    a = analyze(path)
    h, freq = a["header"], a["freq"]
    counts, latencies, phases = a["counts"], a["latencies"], a["phases"]

    print(f"capture span: {a['span']:.1f}s  records: {h.recordCount}  "
          f"dropped: {h.droppedRecordCount}  buffer: {h.bufferSize // 1024}KB  "
          f"mode: {'deep' if a['deep'] else 'standard'}")
    print()
    print(f"{'opcode':>6} {'count':>6} {'min':>8} {'median':>8} {'avg':>8} "
          f"{'p95':>8} {'max':>8}  (latency, ms)")
    for op in sorted(counts, key=lambda o: -counts[o]):
        ls = latencies.get(op, [])
        if ls:
            mn, med, avg, p95, mx = (v / freq * 1000 for v in _percentiles(ls))
            print(f"  0x{op:02X} {counts[op]:>6} {mn:>8.2f} {med:>8.2f} "
                  f"{avg:>8.2f} {p95:>8.2f} {mx:>8.2f}")
        else:
            print(f"  0x{op:02X} {counts[op]:>6}        -        -        -        -        -")

    if a["deep"] and phases["storage"]:
        n = len(phases["storage"])
        print()
        print(f"READ-class phase breakdown ({n} commands with deep events, avg ms):")
        tot = 0.0
        for name in ("storage", "usb", "firmware"):
            avg_ms = sum(phases[name]) / n / freq * 1000
            tot += avg_ms
            label = {"storage": "image/SD read", "usb": "USB IN transfer",
                     "firmware": "firmware/scheduler"}[name]
            print(f"  {label:20s} {avg_ms:7.2f}")
        print(f"  {'total (CDB->CSW)':20s} {tot:7.2f}")

    if a["swaps"]:
        print()
        print(f"{len(a['swaps'])} disc swap(s):")
        for i, s in enumerate(a["swaps"], 1):
            print(f"  #{i}: NO_MEDIUM window {s['no_medium'] / freq:.2f}s, "
                  f"UA->READY {s['ua_to_ready'] / freq:.2f}s")

    if a["errors"]:
        print()
        print(f"{len(a['errors'])} error events:")
        for ts, desc in a["errors"][:10]:
            print(f"  t={ts:9.3f}s  {desc}")


def _med_ms(a, op):
    ls = a["latencies"].get(op, [])
    return ls[len(ls) // 2] / a["freq"] * 1000 if ls else None


def cmd_compare(path_a, path_b):
    A, B = analyze(path_a), analyze(path_b)
    na = os.path.basename(path_a)
    nb = os.path.basename(path_b)

    print(f"A: {na}")
    print(f"B: {nb}")
    print()
    for label, key in (("span", None), ("records", "recordCount"),
                       ("dropped", "droppedRecordCount")):
        if key is None:
            va, vb = f"{A['span']:.1f}s", f"{B['span']:.1f}s"
        else:
            va, vb = getattr(A["header"], key), getattr(B["header"], key)
        print(f"  {label:12s} A: {va:>10}   B: {vb:>10}")
    print(f"  {'mode':12s} A: {'deep' if A['deep'] else 'standard':>10}   "
          f"B: {'deep' if B['deep'] else 'standard':>10}")

    # Per-opcode median latency, flagging shifts >= 10 %.
    ops = sorted(set(A["counts"]) | set(B["counts"]),
                 key=lambda o: -(A["counts"].get(o, 0) + B["counts"].get(o, 0)))
    print()
    print(f"{'opcode':>6} {'cnt A':>6} {'cnt B':>6} {'med A':>8} {'med B':>8} "
          f"{'shift':>8}   (median latency, ms)")
    for op in ops:
        ca, cb = A["counts"].get(op, 0), B["counts"].get(op, 0)
        ma, mb = _med_ms(A, op), _med_ms(B, op)
        sa = f"{ma:8.2f}" if ma is not None else "       -"
        sb = f"{mb:8.2f}" if mb is not None else "       -"
        if ma and mb:
            pct = (mb - ma) / ma * 100
            flag = "  <<" if abs(pct) >= 10 else ""
            shift = f"{pct:+7.1f}%{flag}"
        else:
            shift = "       -"
        print(f"  0x{op:02X} {ca:>6} {cb:>6} {sa} {sb} {shift}")

    if A["deep"] and B["deep"] and A["phases"]["storage"] and B["phases"]["storage"]:
        print()
        print("READ-class phase breakdown (avg ms):")
        for name in ("storage", "usb", "firmware"):
            va = sum(A["phases"][name]) / len(A["phases"][name]) / A["freq"] * 1000
            vb = sum(B["phases"][name]) / len(B["phases"][name]) / B["freq"] * 1000
            label = {"storage": "image/SD read", "usb": "USB IN transfer",
                     "firmware": "firmware/scheduler"}[name]
            print(f"  {label:20s} A: {va:7.2f}   B: {vb:7.2f}")

    if A["swaps"] or B["swaps"]:
        print()
        print("disc swaps (avg):")
        for tag, S in (("A", A), ("B", B)):
            if S["swaps"]:
                n = len(S["swaps"])
                nm = sum(s["no_medium"] for s in S["swaps"]) / n / S["freq"]
                ur = sum(s["ua_to_ready"] for s in S["swaps"]) / n / S["freq"]
                print(f"  {tag}: {n} swap(s), NO_MEDIUM window {nm:.2f}s, "
                      f"UA->READY {ur:.2f}s")
            else:
                print(f"  {tag}: none")

    if A["sense_counts"] or B["sense_counts"]:
        print()
        print("sense codes set (count):")
        for key in sorted(set(A["sense_counts"]) | set(B["sense_counts"])):
            print(f"  {key}   A: {A['sense_counts'].get(key, 0):>5}   "
                  f"B: {B['sense_counts'].get(key, 0):>5}")

    ea, eb = len(A["errors"]), len(B["errors"])
    print()
    print(f"error events: A: {ea}   B: {eb}")
    for tag, S in (("A", A), ("B", B)):
        for ts, desc in S["errors"][:5]:
            print(f"  {tag}: t={ts:9.3f}s  {desc}")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1

    command, path = argv[1], argv[2]
    if command == "info":
        cmd_info(path)
    elif command == "decode":
        cmd_decode(path)
    elif command == "stats":
        cmd_stats(path)
    elif command == "compare":
        if len(argv) < 4:
            print(__doc__)
            return 1
        cmd_compare(path, argv[3])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
