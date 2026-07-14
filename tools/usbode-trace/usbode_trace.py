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

Mirrors addon/tracelab/traceformat.h. Keep struct layouts in sync with that
header if the format changes.
"""
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


def cmd_stats(path):
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

    span = (last_t - first_t) / freq if first_t is not None else 0
    print(f"capture span: {span:.1f}s  records: {h.recordCount}  "
          f"dropped: {h.droppedRecordCount}  buffer: {h.bufferSize // 1024}KB  "
          f"mode: {'deep' if deep_seen else 'standard'}")
    print()
    print(f"{'opcode':>6} {'count':>6} {'min':>8} {'median':>8} {'avg':>8} "
          f"{'p95':>8} {'max':>8}  (latency, ms)")
    for op in sorted(counts, key=lambda o: -counts[o]):
        ls = sorted(latencies.get(op, []))
        if ls:
            mn, med, avg, p95, mx = (v / freq * 1000 for v in _percentiles(ls))
            print(f"  0x{op:02X} {counts[op]:>6} {mn:>8.2f} {med:>8.2f} "
                  f"{avg:>8.2f} {p95:>8.2f} {mx:>8.2f}")
        else:
            print(f"  0x{op:02X} {counts[op]:>6}        -        -        -        -        -")

    if deep_seen and phases["storage"]:
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

    if errors:
        print()
        print(f"{len(errors)} error events:")
        for ts, desc in errors[:10]:
            print(f"  t={ts:9.3f}s  {desc}")


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
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
