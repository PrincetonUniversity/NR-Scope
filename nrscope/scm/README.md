# NR-Scope SCM Mode

Spectrum Consumption Model (SCM) telemetry. NR-Scope dumps a target cell's decoded
broadcast identity as JSON Lines; `scm_exporter.py` turns those into per-cell SCM
records (optionally geolocated).

- `nrscope_scm.h` — header-only C++ writer (built into nrscope)
- `scm_exporter.py` — Python reader / exporter

## 1. Run NR-Scope in SCM mode

Add an `scm:` block to your USRP setting in `config.yaml`:

```yaml
usrp_setting_0:
  # ... existing settings ...
  scm:
    enabled: true
    sensor_id: "my-sensor-1"       # stamped on every record
    output: "raw_scm_data.jsonl"   # JSON Lines, append mode
```

Run nrscope as usual. For each found cell it appends three lines — `mib`, `sib1`,
`master_cell_group` — sharing one envelope:

```
{"type":"sib1","sensor_id":"my-sensor-1","scan_start_ms":...,"mib_capture_ms":...,"ssb_freq_hz":622850000,"pci":599,"record":{...}}
```

A "capture" (one cell) = the records sharing `(sensor_id, scan_start_ms, mib_capture_ms)`.

## 2. Export SCM records

`scm_exporter.py` emits one SCM record (JSON) per cell.

```bash
# offline — scan an existing file
python3 scm_exporter.py raw_scm_data.jsonl

# live — follow the file as nrscope writes it (tail -f style)
python3 scm_exporter.py raw_scm_data.jsonl --live

# geolocated — add tower lat/lng/accuracy via the Google Geolocation API
python3 scm_exporter.py raw_scm_data.jsonl --geolocate
```

Each record carries decoded identity (`mcc`/`mnc`/`tac`/`nci`/`pci`) and derived
fields (`gnb_id`, `band`, `duplex`, `bandwidth_mhz`, …); `--geolocate` adds
`lat`/`lng`/`accuracy`.

`--geolocate` needs a Google API key in **`~/private/gapi.txt`** (one line, key
only; override with `--api-key-file PATH` or `NRSCOPE_GAPI_KEY_FILE`). It's
best-effort: an unknown cell logs `cell not found` and the record is emitted
without a location. The key is never printed.

To change what's emitted, edit `process_record()` — it's the single extension point.

## 3. Integrate the library into another project

`nrscope_scm.h` is header-only and depends only on srsRAN. In any srsRAN-based tool:

1. Drop `nrscope_scm.h` in; make sure the srsRAN include paths resolve.
2. At startup — enable + configure (calling this turns SCM on):
   `scm_init("sensor-id", "out.jsonl");`  or  `scm_init_from_config(yaml_node);`
3. At the three decode sites, hand over each record:
   - `scm_write_mib(pci, ssb_freq_hz, mib);`   // `srsran_mib_nr_t`
   - `scm_write_sib1(sib1);`                    // `asn1::rrc_nr::sib1_s`
   - `scm_write_mastercellgroup(mcg);`          // `asn1::rrc_nr::cell_group_cfg_s`

No library to link, no build wiring. Two caveats:

- Requires nrscope's `setup_release_c::to_json` fix in srsRAN's `asn1_utils.h`
  (upstream srsRAN emits invalid JSON for `SetupRelease` fields).
- `scm_write_mastercellgroup` currently calls `exit(0)` after writing (single-cell
  test convenience) — remove that `// TEMP` block for continuous operation.
