#!/usr/bin/env python3
"""Decoder for USBODE Trace Lab .utrace files (Phase 1 format).

Usage:
    usbode_trace.py info <file.utrace>
    usbode_trace.py decode <file.utrace>

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
    0x0400: "SCSI_CDB_RECEIVED",
    0x0401: "SCSI_COMMAND_COMPLETE",
    0x0402: "SCSI_SENSE_SET",
}

CDB_PAYLOAD_FMT = "<BB16s"
COMPLETE_PAYLOAD_FMT = "<BBI"
SENSE_PAYLOAD_FMT = "<BBB"


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


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1

    command, path = argv[1], argv[2]
    if command == "info":
        cmd_info(path)
    elif command == "decode":
        cmd_decode(path)
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
