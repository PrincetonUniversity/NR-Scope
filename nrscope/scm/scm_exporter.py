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
# Usage (see README.md for details):
#   scm_exporter.py [FILE] [--live] [--geolocate] [--api-key-file PATH]
#     FILE          SCM JSONL file (default: raw_scm_data.jsonl)
#     --live        follow new records as they are appended (like `tail -f`)
#     --geolocate   add lat/lng via the Google Geolocation API (networked)
#     --api-key-file  file holding the Google API key (default ~/private/gapi.txt)

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

DEFAULT_FILE = "raw_scm_data.jsonl"

# V2 geolocation (opt-in via --geolocate). The API key is read from a file kept
# OUTSIDE the repo so it is never committed or pasted anywhere; main() loads it
# only when --geolocate is passed. Override the path with --api-key-file or the
# NRSCOPE_GAPI_KEY_FILE env var.
DEFAULT_API_KEY_FILE = os.path.expanduser("~/private/gapi.txt")
GEOLOCATE_URL = "https://www.googleapis.com/geolocation/v1/geolocate"
_GEO_API_KEY = None  # set by main() when --geolocate is on

# record handler for MIB/SIB1/MCG records.
# V1 export: emit one SCM record (JSON) per cell, built from the `sib1` record —
# which, thanks to the envelope, already carries pci / freq / capture time too, so
# no cross-record join is needed. `mib` / `master_cell_group` records add nothing
# to the V1 field set, so we skip them.
def process_record(rec):
    if rec.get("type") != "sib1":
        return
    row = scm_record_from_sib1(rec)
    if _GEO_API_KEY:  # V2: only when --geolocate is on
        loc = geolocate(row["mcc"], row["mnc"], row["nci"], _GEO_API_KEY)
        if loc:
            row.update(loc)
    print(json.dumps(row), flush=True)


# --- gNB-ID / sector split (V1.5) ------------------------------------------
# NR packs a 36-bit NCI (SIB1 cellIdentity) as [ gNB-ID | sector-ID ]. The split
# point is operator-configurable (22-32 bits) and is NOT broadcast, so we assume
# a default and override per PLMN as carriers are confirmed.
#
# NOTE: 24 bits (24-bit gNB / 12-bit sector) is the common default; treat every
# result as unconfirmed until checked. Confirm a carrier on CellMapper's map or
# the CellInfo/HiCellTek NR tools, then add a (mcc, mnc) row below. Cheap sanity
# check: a correct length makes the sector-ID a small number (a gNB has only a
# few sectors) — e.g. our T-Mobile capture yields sector 3 at 24 bits.
GNB_ID_LEN_DEFAULT = 24
GNB_ID_LEN_BY_PLMN = {
    # Inferred from the finalfull2/cosmos-20260719 capture (see notes.md):
    # pick the split whose sector IDs stay small/structured across all NCIs.
    (310, 260): 24,  # T-Mobile US — 24 gives sectors {3,21,302,303,313} (decimal
                     # carrier+sector pattern) and same-tower cells share a gNB-ID;
                     # 22 gives 4117/8195. Good confidence.
    (310, 410): 26,  # AT&T — 24 gives sectors ~3097-3099; >=26 gives {25,26,27,76}.
                     # 26 vs 28 indistinguishable in this capture (sector bits 10-11
                     # all zero); 26 = minimum consistent length. To confirm.
    (311, 480): 22,  # Verizon — 22 bits per online sources (confirmed 2026-08-11);
                     # only 1 NCI in this capture (gnb 799168 / sector 1431 at 22 —
                     # large sectors are expected in VzW's 14-bit sector space).
}


def split_nci(nci, mcc, mnc):
    """Split a 36-bit NCI into (gnb_id, sector_id) using the per-PLMN gNB-ID
    length, or GNB_ID_LEN_DEFAULT for unlisted operators."""
    gnb_bits = GNB_ID_LEN_BY_PLMN.get((mcc, mnc), GNB_ID_LEN_DEFAULT)
    sector_bits = 36 - gnb_bits
    return nci >> sector_bits, nci & ((1 << sector_bits) - 1)


# --- V1 field extraction from a `sib1` record ------------------------------
# Small standards tables (partial — extend as needed; unknown → None).
# NR band → duplex mode (TS 38.101-1); FR1-common subset.
BAND_DUPLEX = {
    1: "FDD", 2: "FDD", 3: "FDD", 5: "FDD", 7: "FDD", 8: "FDD", 12: "FDD",
    13: "FDD", 14: "FDD", 18: "FDD", 20: "FDD", 24: "FDD", 25: "FDD", 26: "FDD",
    28: "FDD", 30: "FDD", 65: "FDD", 66: "FDD", 70: "FDD", 71: "FDD", 74: "FDD",
    34: "TDD", 38: "TDD", 39: "TDD", 40: "TDD", 41: "TDD", 48: "TDD", 50: "TDD",
    51: "TDD", 77: "TDD", 78: "TDD", 79: "TDD", 90: "TDD",
    75: "SDL", 76: "SDL",
    80: "SUL", 81: "SUL", 82: "SUL", 83: "SUL", 84: "SUL", 86: "SUL",
}
# (scs kHz, N_RB) → channel bandwidth MHz (TS 38.101-1 Table 5.3.2-1), FR1.
NR_RB_TO_MHZ = {
    15: {25: 5, 52: 10, 79: 15, 106: 20, 133: 25, 160: 30, 216: 40, 270: 50},
    30: {11: 5, 24: 10, 38: 15, 51: 20, 65: 25, 78: 30, 106: 40, 133: 50,
         162: 60, 189: 70, 217: 80, 245: 90, 273: 100},
    60: {11: 10, 18: 15, 24: 20, 31: 25, 38: 30, 51: 40, 65: 50, 79: 60,
         93: 70, 107: 80, 121: 90, 135: 100},
}


def _dig(d, *keys):
    """Safe nested lookup through dicts (str keys) and lists (int indices)."""
    for k in keys:
        if isinstance(d, dict):
            d = d.get(k)
        elif isinstance(d, list) and isinstance(k, int) and 0 <= k < len(d):
            d = d[k]
        else:
            return None
        if d is None:
            return None
    return d


def _plmn_to_int(digits):
    """MCC/MNC digit array [3,1,0] → 310. (Loses a leading-zero MNC — fine for
    the majors; revisit if a 2-digit-with-leading-zero MNC shows up.)"""
    return int("".join(map(str, digits))) if digits else None


def _bits_to_int(s):
    """ASN.1 bit-string ('0101…') → int; None if not a bit-string."""
    return int(s, 2) if isinstance(s, str) and s and set(s) <= {"0", "1"} else None


def _scs_khz(s):
    """srsRAN SCS enum 'kHz15' → 15."""
    if isinstance(s, str) and s.startswith("kHz"):
        try:
            return int(s[3:])
        except ValueError:
            return None
    return None


def scm_record_from_sib1(rec):
    """Build a V1 SCM record from one enveloped `sib1` record. Field names track
    section 6 of the design doc (NR-clear where the LTE names were ambiguous).
    Missing/undecodable fields come out as None rather than raising."""
    r = rec.get("record", {})

    # PLMN + cell identity. Note srsRAN's double-nested `plmn-IdentityList`:
    # cellAccessRelatedInfo.plmn-IdentityList[0] is a PLMN-IdentityInfo, whose own
    # plmn-IdentityList[0] holds the actual {mcc, mnc}; tac/NCI sit on the info.
    info = _dig(r, "cellAccessRelatedInfo", "plmn-IdentityList", 0)
    plmn = _dig(info, "plmn-IdentityList", 0)
    mcc = _plmn_to_int(_dig(plmn, "mcc"))
    mnc = _plmn_to_int(_dig(plmn, "mnc"))
    tac = _bits_to_int(_dig(info, "trackingAreaCode"))
    nci = _bits_to_int(_dig(info, "cellIdentity"))

    # DL frequency info.
    fdl = _dig(r, "servingCellConfigCommon", "downlinkConfigCommon", "frequencyInfoDL")
    band = _dig(fdl, "frequencyBandList", 0, "freqBandIndicatorNR")
    nprb = _dig(fdl, "scs-SpecificCarrierList", 0, "carrierBandwidth")
    scs = _scs_khz(_dig(fdl, "scs-SpecificCarrierList", 0, "subcarrierSpacing"))
    ss_pbch_power = _dig(r, "servingCellConfigCommon", "ss-PBCH-BlockPower")

    # Derived.
    gnb_id, sector_id = (None, None)
    if nci is not None and mcc is not None and mnc is not None:
        gnb_id, sector_id = split_nci(nci, mcc, mnc)

    return {
        # provenance / envelope
        "sensor_id": rec.get("sensor_id"),
        "measurement_time_ms": rec.get("mib_capture_ms"),
        # identity
        "mcc": mcc, "mnc": mnc, "tac": tac,
        "nci": nci, "gnb_id": gnb_id, "sector_id": sector_id,  # gnb split: V1.5
        "pci": rec.get("pci"),
        # radio / spectrum
        "center_freq_hz": rec.get("ssb_freq_hz"),
        "band": band, "duplex": BAND_DUPLEX.get(band),
        "nprb": nprb, "scs_khz": scs,
        "bandwidth_mhz": NR_RB_TO_MHZ.get(scs, {}).get(nprb),
        "ss_pbch_block_power_dbm": ss_pbch_power,
    }


# --- V2 geolocation (opt-in) -----------------------------------------------

def load_api_key(path):
    """Read the Google API key from `path` (a one-line file kept outside the
    repo). Returns the key string; the key is never logged."""
    with open(os.path.expanduser(path)) as f:
        return f.read().strip()


def geolocate(mcc, mnc, nci, api_key, timeout=10):
    """Look up a cell's location via the Google Geolocation API (5G NR). Returns
    {'lat','lng','accuracy'} or None. Best-effort: Google's NR coverage is
    spotty and considerIp=false means an unknown cell returns an error (not a
    guess) — so None is a normal, common outcome. Neither the key nor the request
    URL is ever printed."""
    if None in (mcc, mnc, nci):
        return None
    body = json.dumps({
        "considerIp": False,
        "radioType": "nr",
        "cellTowers": [{
            "newRadioCellId": nci,          # the full 36-bit NCI, not the gNB-ID
            "mobileCountryCode": mcc,
            "mobileNetworkCode": mnc,
        }],
    }).encode()
    req = urllib.request.Request(
        f"{GEOLOCATE_URL}?key={api_key}", data=body,
        headers={"Content-Type": "application/json"}, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.load(resp)
    except urllib.error.HTTPError as e:
        note = "cell not found" if e.code == 404 else "error"
        print(f"geolocate: HTTP {e.code} ({note}) for nci={nci}", file=sys.stderr)
        return None
    except (urllib.error.URLError, TimeoutError, ValueError) as e:
        print(f"geolocate: {e}", file=sys.stderr)
        return None
    loc = data.get("location", {})
    return {"lat": loc.get("lat"), "lng": loc.get("lng"),
            "accuracy": data.get("accuracy")}


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
    ap = argparse.ArgumentParser(description="Export nrscope SCM-mode JSONL records.")
    ap.add_argument("file", nargs="?", default=DEFAULT_FILE,
                    help=f"SCM JSONL file (default: {DEFAULT_FILE})")
    ap.add_argument("--live", action="store_true",
                    help="skip to end and follow new records (like tail -f)")
    ap.add_argument("--geolocate", action="store_true",
                    help="add lat/lng via Google Geolocation API (networked; needs a key)")
    ap.add_argument("--api-key-file",
                    default=os.environ.get("NRSCOPE_GAPI_KEY_FILE", DEFAULT_API_KEY_FILE),
                    help=f"file holding the Google API key "
                         f"(default: {DEFAULT_API_KEY_FILE}, or env NRSCOPE_GAPI_KEY_FILE)")
    args = ap.parse_args()

    if args.geolocate:
        global _GEO_API_KEY
        try:
            _GEO_API_KEY = load_api_key(args.api_key_file)
        except OSError as e:
            print(f"error: --geolocate needs an API key file: {e}", file=sys.stderr)
            return 1

    return follow(args.file) if args.live else scan(args.file)


if __name__ == "__main__":
    sys.exit(main())
