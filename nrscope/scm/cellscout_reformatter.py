#!/usr/bin/env python3
# Reformat cellscout output into the enveloped SIB1 JSONL that scm_exporter.py
# expects, so the exporter can run on cellscout data unchanged.
#
# cellscout writes one flat record per line (one found cell per line, no type
# envelope); lines with sib1_ok=true carry the srsRAN ASN.1 SIB1 dump as an
# embedded JSON string in `sib1_json`. This script filters to those lines and
# emits nrscope-style envelopes:
#   {"type": "sib1", "sensor_id": ..., "runid": ..., "mib_capture_ms": ...,
#    "ssb_freq_hz": ..., "pci": ..., "record": {...decoded SIB1...}}
# `runid` is a cellscout extra with no nrscope equivalent; scm_exporter ignores
# unknown envelope keys, so it rides along in the intermediate file.
#
# Usage:
#   cellscout_reformatter.py FILE > sib1_envelope.jsonl
#   scm_exporter.py sib1_envelope.jsonl > scm_output.jsonl

import argparse
import json
import re
import sys
from datetime import datetime, timezone

# cellscout (as of finalfull2) embeds srsRAN's setup/release choices as a bare
# double brace — `"pdcch-ConfigCommon": { { ... } }` — which is invalid JSON.
# Until that's fixed upstream, name the inner object "setup" so it parses.
_BARE_BRACE = re.compile(r"\{\s*\n(\s*)\{")


def parse_sib1_json(s):
    """Parse cellscout's embedded SIB1 dump, repairing the known bare-brace
    quirk if plain parsing fails. Returns the dict, or None if unparseable."""
    for text in (s, _BARE_BRACE.sub(r'{\n\1"setup": {', s)):
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            continue
    return None


def ts_utc_to_ms(ts):
    """cellscout 'ts_utc' ISO-8601 string ('2026-07-19T19:03:53Z') → epoch ms."""
    try:
        dt = datetime.fromisoformat(ts)
    except (TypeError, ValueError):
        return None
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return int(dt.timestamp() * 1000)


def envelope_from_cellscout(rec):
    """Build one enveloped sib1 record from a cellscout line, or None if the
    line has no (parseable) SIB1."""
    if not (rec.get("sib1_ok") and rec.get("sib1_json")):
        return None
    sib1 = parse_sib1_json(rec["sib1_json"])
    if sib1 is None:
        print(f"warning: unparseable sib1_json (pci={rec.get('pci')}, "
              f"ts={rec.get('ts_utc')}); skipped", file=sys.stderr)
        return None
    freq = rec.get("center_freq_hz")
    return {
        "type": "sib1",
        "sensor_id": "/".join(str(rec.get(k)) for k in ("site", "host", "sdr")),
        "runid": rec.get("runid"),
        "mib_capture_ms": ts_utc_to_ms(rec.get("ts_utc")),
        "ssb_freq_hz": int(freq) if freq is not None else None,
        "pci": rec.get("pci"),
        "record": sib1,
    }


def main():
    ap = argparse.ArgumentParser(
        description="Reformat cellscout JSONL into scm_exporter-compatible "
                    "enveloped sib1 JSONL (written to stdout).")
    ap.add_argument("file", help="cellscout JSONL file")
    args = ap.parse_args()

    try:
        f = open(args.file)
    except OSError as e:
        print(f"error: cannot open {args.file}: {e}", file=sys.stderr)
        return 1

    seen = emitted = 0
    with f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                print(f"warning: skipping malformed line {seen + 1}",
                      file=sys.stderr)
                continue
            seen += 1
            env = envelope_from_cellscout(rec)
            if env is not None:
                print(json.dumps(env), flush=True)
                emitted += 1
    print(f"{emitted} sib1 records from {seen} cellscout lines", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
