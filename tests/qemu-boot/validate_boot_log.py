#!/usr/bin/env python3
"""Validate a USBODE QEMU boot log - the whole log, not a handful of greps.

The log (serial capture, or the 0:/usbode-logs.txt file log pulled off the
card) is parsed line by line into (module, message) entries and split into
boot sessions. The validator then enforces, per session:

  1. an ORDERED list of required events (the boot must progress through
     them in sequence),
  2. an UNORDERED set of required events (scheduler-dependent task startup
     lines whose relative order is legitimately nondeterministic),
  3. FORBIDDEN patterns (panics, asserts, gadget-init attempts, mount
     failures, a second setup pass after the setup reboot),
  4. a full-log SEVERITY SWEEP: any line that looks like an error/failure
     and is not explicitly allowlisted fails the run, even if every
     required event matched. New failure modes cannot hide behind a green
     marker list.

Expected version/branch/commit are derived from buildinfo.json (written by
scripts/generate-buildinfo.sh during the build), so the test asserts that
the booted kernel identifies as EXACTLY the build under test, ending with
the `USBODE <version> ready` line emitted by src/kernel.cpp right before
the main loop.

Two profiles reflect the two observation channels (see README.md):
  --source serial   64-bit runs: full-fidelity, strict event set
  --source filelog  32-bit runs (-bios forbids -append, so no serial):
                    the file logger drops lines under burst load and loses
                    its tail at the setup reboot, so the known-lossy events
                    are downgraded to optional in this profile.

Exit code 0 = PASS, 1 = FAIL, 2 = usage/input error.
"""

import argparse
import json
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;]*m")

SERIAL_LINE = re.compile(r"^(?:\d\d:\d\d:\d\d\.\d\d )?(.+?): (.*)$")
FILELOG_LINE = re.compile(r"^\[(\d+)\] \[(.+?)\] (\w+): (.*)$")
SERIAL_SESSION_START = re.compile(r"Circle \d+ started on")
FILELOG_SESSION_START = re.compile(r"--- New Session Started ---")

SUSPICIOUS = re.compile(
    r"(?i)\b(error|fail|failed|failure|panic|assert|cannot|can't|unable|"
    r"corrupt|invalid|timeout|halt)\b"
)

# Lines that legitimately look alarming on this test rig. Anything
# suspicious NOT matched here fails the run.
ALLOWED_NOISE = [
    # No SDIO WLAN chip in QEMU: the wlan module's probe spew is expected,
    # and the REQUIRED "WLAN not available - continuing without network"
    # event pins the graceful-degradation outcome.
    re.compile(r"^wlan: "),
    # setupstatus probes the unformatted/too-small placeholder before setup
    re.compile(r"^setupstatus: Failed to mount partition \d+: \d+$"),
    re.compile(r"^setupstatus: Partition \d+ not accessible \(error \d+\)$"),
    re.compile(r"^setupstatus: Partition \d+ size is too small"),
    re.compile(r"^setupstatus: Attempting to (check|mount) partition"),
    # partition-table dump probes with f_getfree before anything is mounted;
    # NOT ACCESSIBLE there is normal for the placeholder (s1) and for the
    # not-yet-mounted exFAT volume (s2)
    re.compile(r"^setupstatus:\s+Status: NOT ACCESSIBLE \(FatFs error \d+\)$"),
    # file logger racing the setup reboot (serial shows it; harmless)
    re.compile(r"^filelogdaemon: Failed to write to log file!$"),
    # copy sweep over a dist without an images dir is tolerated by setup
    re.compile(r"^setupstatus: f_findfirst failed: \d+$"),
    # absence probes, not failures
    re.compile(r"^upgradestatus: Upgrade not found: "),
    re.compile(r"^setupstatus: Second partition not found or too small"),
    re.compile(r"^setupstatus: Setup starting\.\.\.$"),
]

FORBIDDEN_ANYWHERE = [
    (re.compile(r"assertion failed"), "assert fired"),
    (re.compile(r": stack\["), "stack trace (panic)"),
    (re.compile(r"(?i)kernel panic|guru meditation"), "panic"),
    (re.compile(r"Cannot mount drive"), "root filesystem mount failed"),
    (re.compile(r"Failed to mount images partition"), "images partition mount failed"),
    (re.compile(r"Setup or Upgrade failed"), "setup/upgrade failed"),
    # Any of these means the USB gadget hardware path ran. QEMU cannot
    # emulate dwc2 device mode; the test design (ejected=1 + empty images/)
    # must keep boot away from it entirely.
    (re.compile(r"does not support USB gadget mode"), "gadget init attempted"),
    (re.compile(r"dwgadget: Unknown vendor"), "gadget init attempted (GSNPSID)"),
    (re.compile(r"Failed to initialize CD Gadget"), "gadget init failed"),
]


def ev(pattern, name, lossy=False):
    """lossy=True: known to be dropped by the file logger under burst load
    or at the reboot tail - required on serial, optional on filelog."""
    return {"re": re.compile(pattern), "name": name, "lossy": lossy}


def build_events(exp):
    """Ordered/unordered event lists per session for the first-boot scenario.

    exp: dict with version, branch, commit7, arch, kernel (already escaped
    where used in regexes)."""
    V = re.escape(exp["version"])
    BR = re.escape(exp["branch"])
    C7 = re.escape(exp["commit7"])
    BITS = exp["arch"]
    KRN = re.escape(exp["kernel"])

    common_early = [
        ev(r"^kernel: Initialized filesystem$", "root FAT mounted"),
        ev(r"^kernel: Initialized Config service$", "config.txt + cmdline.txt parsed"),
        ev(rf"^gitinfo: Version: {V}, Short: .*\(AARCH{BITS}\)", f"gitinfo reports {exp['version']} / AARCH{BITS}"),
        # lossy: observed dropped from the file log's burst window on raspi0
        # (present in serial capture in every run) - required on serial only
        ev(r"^kernel: WLAN not available - continuing without network$", "WLAN degraded gracefully", lossy=True),
    ]
    banner = [
        ev(r"^kernel: Welcome to USBODE$", "banner", lossy=True),
        ev(rf"^kernel: Git Info: {BR} @ {C7}(-dirty)?$", "banner git info", lossy=True),
        ev(rf"^kernel: Kernel Name: {KRN}$", f"banner kernel name = {exp['kernel']}", lossy=True),
        ev(rf"^kernel: Architecture: {BITS}$", f"banner arch = {BITS}", lossy=True),
    ]

    session1 = {
        "title": "first boot: virgin card -> setup -> reboot",
        "ordered": common_early + banner + [
            ev(r"^setupstatus: Second partition not found or too small - setup required$", "setup triggered"),
            ev(r"^setupstatus: Resizing second partition\.\.\.$", "MBR resize started"),
            ev(r"^setupstatus: Partition 2 resized successfully$", "MBR rewritten to full card"),
            ev(r"^setupstatus: Formatting partition 2 as exFAT\.\.\.$", "f_mkfs started"),
            ev(r"^setupstatus: Partition 2 formatted as ExFAT successfully$", "exFAT created"),
            ev(r"^setupstatus: File copy complete\. 0 files copied$", "images sweep (empty by design)", lossy=True),
            ev(r"^kernel: Setup or Upgrade successful, rebooting$", "setup rebooted", lossy=True),
        ],
        "unordered": [],
        "forbidden": [],
    }
    session2 = {
        "title": "second boot: initialized card -> ejected-empty idle",
        "ordered": common_early + banner + [
            ev(r"^setupstatus: Second partition exists and is adequate size - no setup required$", "setup NOT re-triggered"),
            ev(r"^kernel: Partition 1 \(data/images\) mounted successfully$", "images (exFAT) partition mounted"),
            ev(r"^kernel: USB Target OS: ", "VID/PID configured"),
            ev(r"^cdrom: Created USB CD gadget with VID: ", "gadget object constructed (no HW touched)"),
            ev(r"^scsitbservice: Boot: drive was ejected at power-off, coming up empty$", "persisted eject honored (3.2.3)"),
            ev(r"^CUSBCDGadget::ArmBootEject: Drive will come up empty", "boot eject armed"),
            ev(r"^scsitbservice: SCSITBService::RefreshCache\(\) Found 0 total entries$", "image scan: none (by design)"),
            ev(r"^kernel: Started SCSITB service$", "all services constructed"),
        ],
        "unordered": [
            ev(r"^scsitbservice: SCSITBService::Run started$", "scsitb task scheduled"),
            ev(r"^cdrom: CDROM Run Loop entered$", "cdrom task scheduled (gadget idle)"),
        ],
        # A second "setup required" here means the setup didn't stick: the
        # card would reboot-loop on real hardware.
        "forbidden": [(re.compile(r"Second partition not found or too small"),
                       "setup re-triggered after setup reboot")],
    }
    if exp["require_ready"]:
        session2["ordered"].append(
            ev(rf"^kernel: USBODE {V} ready$", f"USBODE {exp['version']} ready"))
    return [session1, session2]


def parse(text, source):
    """-> (prelude_lines, sessions); each session is a list of
    (lineno, module, message, level, raw)."""
    entries = []
    line_re = SERIAL_LINE if source == "serial" else FILELOG_LINE
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = ANSI.sub("", raw).rstrip("\r")
        if not line.strip():
            continue
        m = line_re.match(line)
        if source == "serial":
            if m:
                entries.append((lineno, m.group(1), m.group(2), None, line))
            else:
                entries.append((lineno, None, line, None, line))
        else:
            if m:
                entries.append((lineno, m.group(2), m.group(4), m.group(3), line))
            else:
                entries.append((lineno, None, line, None, line))

    start_re = SERIAL_SESSION_START if source == "serial" else FILELOG_SESSION_START
    prelude, sessions = [], []
    for e in entries:
        if start_re.search(e[4]):
            sessions.append([e] if source == "serial" else [])
            continue
        (sessions[-1] if sessions else prelude).append(e)
    return prelude, sessions


def canon(entry):
    _, module, message, _, raw = entry
    return f"{module}: {message}" if module is not None else raw


class Report:
    def __init__(self):
        self.lines = []
        self.failures = 0

    def ok(self, msg):
        self.lines.append(f"  PASS  {msg}")

    def skip(self, msg):
        self.lines.append(f"  skip  {msg}")

    def fail(self, msg):
        self.failures += 1
        self.lines.append(f"  FAIL  {msg}")

    def section(self, title):
        self.lines.append(f"\n== {title}")


def check_session(rep, session, spec, profile):
    rep.section(spec["title"])
    idx = 0
    for event in spec["ordered"]:
        required = not (event["lossy"] and profile == "filelog")
        hit = None
        for j in range(idx, len(session)):
            if event["re"].search(canon(session[j])):
                hit = j
                break
        if hit is not None:
            rep.ok(f"{event['name']}  (line {session[hit][0]})")
            idx = hit + 1
        elif required:
            rep.fail(f"missing/out-of-order: {event['name']}  [{event['re'].pattern}]")
        else:
            rep.skip(f"{event['name']} (optional in filelog profile: burst-lossy)")
    for event in spec["unordered"]:
        hits = [e for e in session if event["re"].search(canon(e))]
        if hits:
            rep.ok(f"{event['name']}  (line {hits[0][0]})")
        else:
            rep.fail(f"missing: {event['name']}  [{event['re'].pattern}]")
    for pattern, why in spec["forbidden"]:
        for e in session:
            if pattern.search(canon(e)):
                rep.fail(f"forbidden ({why}): line {e[0]}: {e[4]}")


def sweep(rep, prelude, sessions):
    rep.section("full-log severity sweep")
    bad = 0
    for scope, entries in [("prelude", prelude)] + [
            (f"session {i+1}", s) for i, s in enumerate(sessions)]:
        for e in entries:
            c = canon(e)
            for pattern, why in FORBIDDEN_ANYWHERE:
                if pattern.search(c):
                    rep.fail(f"forbidden ({why}): {scope} line {e[0]}: {e[4]}")
                    bad += 1
            level_bad = e[3] in ("ERROR", "PANIC") if e[3] else False
            if (SUSPICIOUS.search(c) or level_bad) and not any(
                    a.search(c) for a in ALLOWED_NOISE):
                rep.fail(f"unexplained error-like line: {scope} line {e[0]}: {e[4]}")
                bad += 1
    if not bad:
        rep.ok("no unexplained error/failure lines anywhere in the log")


def expectations(args):
    version = args.expect_version
    branch, commit7 = args.branch, args.commit
    if args.buildinfo:
        with open(args.buildinfo) as f:
            bi = json.load(f)
        full = bi["version"]["full"]
        branch = branch or bi["git"]["branch"]
        commit7 = commit7 or bi["git"]["commit_short"][:7]
        if version is None:
            # mirrors CGitInfo::UpdateFormattedVersions():
            # x.y.z[-build][-branch-if-not-main]-commit7
            version = full if branch == "main" else f"{full}-{branch}"
            version = f"{version}-{commit7}"
    if not all([version, branch, commit7]):
        sys.exit("validate: need --buildinfo or all of --expect-version/--branch/--commit")
    return {
        "version": version,
        "branch": branch,
        "commit7": commit7[:7],
        "arch": args.expect_arch,
        "kernel": args.expect_kernel,
        "require_ready": not args.no_require_ready,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", required=True)
    ap.add_argument("--source", choices=["serial", "filelog"], required=True)
    ap.add_argument("--scenario", choices=["first-boot"], default="first-boot")
    ap.add_argument("--buildinfo", help="buildinfo.json for expected version/branch/commit")
    ap.add_argument("--expect-version", help="override full expected version string")
    ap.add_argument("--branch", help="override expected git branch")
    ap.add_argument("--commit", help="override expected short commit")
    ap.add_argument("--expect-kernel", required=True, help="e.g. kernel8.img")
    ap.add_argument("--expect-arch", choices=["32", "64"], required=True)
    ap.add_argument("--no-require-ready", action="store_true",
                    help="tolerate kernels predating the 'USBODE <ver> ready' marker")
    args = ap.parse_args()

    try:
        with open(args.log, errors="replace") as f:
            text = f.read()
    except OSError as e:
        sys.exit(f"validate: cannot read log: {e}")

    exp = expectations(args)
    specs = build_events(exp)
    prelude, sessions = parse(text, args.source)

    rep = Report()
    rep.section("structure")
    if len(sessions) == len(specs):
        rep.ok(f"exactly {len(specs)} boot sessions (setup boot + normal boot)")
    else:
        rep.fail(f"expected {len(specs)} boot sessions, found {len(sessions)} "
                 f"(reboot loop, missed reboot, or truncated log)")
    if prelude and args.source == "serial":
        rep.fail(f"{len(prelude)} line(s) before the first Circle banner, "
                 f"first: {prelude[0][4]!r}")

    for spec, session in zip(specs, sessions):
        check_session(rep, session, spec, args.source)
    sweep(rep, prelude, sessions)

    print(f"validate: {args.log} [{args.source}/{args.scenario}] "
          f"expecting version {exp['version']} on {exp['kernel']}")
    print("\n".join(rep.lines))
    print(f"\nvalidate: {'PASS' if rep.failures == 0 else f'FAIL ({rep.failures} problem(s))'}")
    return 0 if rep.failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
