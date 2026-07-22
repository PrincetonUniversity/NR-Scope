#!/usr/bin/env python3
# Preliminary reader for SCM-mode output produced by nrscope_scm.h.
#
# nrscope (with SCM enabled) writes a JSON Lines file — one enveloped record per
# line, of the form:
#   {"type": "mib"|"sib1"|"master_cell_group",
#    "sensor_id": ..., "scan_start_ms": ..., "mib_capture_ms": ...,
#    "ssb_freq_hz": ..., "pci": ..., "record": {...}}
# A "capture" (one found cell) is the group of records sharing
# (sensor_id, scan_start_ms, mib_capture_ms).
#
# Usage:
#   scm_reader.py [FILE] [--live]
#     FILE    SCM JSONL file (default: raw_scm_data.jsonl)
#     --live  skip to the end of the file and follow new records as they are
#             appended (like `tail -f`); without it, scan the existing file and
#             exit.

import argparse
import json
import sys
import time

DEFAULT_FILE = "raw_scm_data.jsonl"

# Per-record processing, shared by scan and follow. THIS is the thing to modify:
# for now it just prints a one-line summary — extend here (decode the `record`
# payloads, filter by sensor/pci, emit CSV, …).
def process_record(rec):
    print(f"[{rec.get('sensor_id', '?')}] "
          f"capture_ms={rec.get('mib_capture_ms', '?')} "
          f"pci={rec.get('pci', '?')} "
          f"freq={rec.get('ssb_freq_hz', '?')} "
          f"type={rec.get('type', '?')}",
          flush=True)


def scan(path):
    """Non-live: process every record already in the file, then exit."""
    try:
        f = open(path)
    except OSError as e:
        print(f"error: cannot open {path}: {e}", file=sys.stderr)
        return 1
    with f:
        for line in f:
            rec = parse_line(line)
            if rec is not None:
                process_record(rec)
    return 0


def follow(path):
    """Live: skip to the end, then print new records as they are appended."""
    try:
        f = open(path)
    except OSError as e:
        print(f"error: cannot open {path}: {e}", file=sys.stderr)
        return 1

    with f:
        f.seek(0, 2)  # skip to end — only report records that arrive from now on
        print(f"following {path} (Ctrl-C to stop)...", file=sys.stderr)
        buf = ""
        try:
            while True:
                where = f.tell()
                line = f.readline()
                if not line:
                    time.sleep(0.2)
                    f.seek(where)  # rewind over a not-yet-complete read; retry
                    continue
                buf += line
                if not buf.endswith("\n"):
                    continue  # partial line (writer mid-flush); wait for the rest
                rec = parse_line(buf)
                buf = ""
                if rec is not None:
                    process_record(rec)
        except KeyboardInterrupt:
            return 0



def parse_line(line):
    """Parse one JSONL line into a record dict, or None if blank/malformed."""
    line = line.strip()
    if not line:
        return None
    try:
        return json.loads(line)
    except json.JSONDecodeError:
        return None



def main():
    ap = argparse.ArgumentParser(description="Read nrscope SCM-mode JSONL output.")
    ap.add_argument("file", nargs="?", default=DEFAULT_FILE,
                    help=f"SCM JSONL file (default: {DEFAULT_FILE})")
    ap.add_argument("--live", action="store_true",
                    help="skip to end and follow new records (like tail -f)")
    args = ap.parse_args()
    return follow(args.file) if args.live else scan(args.file)


if __name__ == "__main__":
    sys.exit(main())
