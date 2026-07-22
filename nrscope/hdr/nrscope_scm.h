// SCM (Spectrum Consumption Model) mode.
//
// When enabled (config `scm: true`), each decoder hands its record to SCM as it
// decodes it, and SCM appends it to raw_scm_data.jsonl — one JSON object per
// line, no batching. Every line is wrapped in an envelope carrying the sensor +
// capture identity so records can be joined downstream, even when several
// sensors' outputs are merged:
//   {"type": "mib"|"sib1"|"master_cell_group",
//    "sensor_id":      <operator config>,
//    "scan_start_ms":  <this process's start; groups one scan run>,
//    "mib_capture_ms": <when the MIB was captured; labels one capture>,
//    "ssb_freq_hz":    <cell SSB frequency>,
//    "pci":            <cell PCI>,
//    "record":         { ...type-specific payload... }}
//
// A capture = one found cell. The MIB is always its first record (cell search →
// MIB → SIB1 → RACH), so scm_write_mib starts a new capture: it latches pci /
// ssb_freq_hz / mib_capture_ms, which are then stamped onto that cell's sib1/mcg
// lines too. Join key = (sensor_id, scan_start_ms, mib_capture_ms); (ssb_freq_hz,
// pci) additionally give the physical cell identity.
//
// Design (see scm-mode-analysis.md "SCM mode implementation notes"): free
// functions over file-static state (mirrors the g_silent idiom); callers hold no
// SCM object. Appends are mutex-guarded (decoders run on concurrent workers).
#pragma once

// Depend only on srsRAN types, not nrscope internals: srsran_mib_nr_t (MIB) and
// the ASN.1 RRC types (SIB1, masterCellGroup). Keeps SCM loosely coupled so it
// could be lifted out to depend on srsRAN alone.
#include <cstdint>
#include <string>

#include "srsran/asn1/rrc_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"

// Toggled by load_config.cc when `scm: true` is present in the config.
extern bool scm_enabled;

// Set once at startup from config (`scm_sensor_id`); stamped on every line.
void scm_set_sensor_id(const std::string& sensor_id);

// Handoffs from the three decode sites. No-ops when scm_enabled is false.
//   MIB  : task_scheduler.cc  DecodeMIB   — detected PCI + SSB tuning frequency
//          (absolute-frequency anchor; band/duplex derive from it) + MIB.
//          Starts a new capture (latches pci / ssb_freq_hz / mib_capture_ms).
//   SIB1 : sibs_decoder.cc    DecodeandParseSIB1fromSlot
//   MCG  : rach_decoder.cc    DecodeandParseMS4fromSlot (masterCellGroup)
void scm_write_mib(uint32_t pci, double ssb_freq_hz, const srsran_mib_nr_t& mib);
void scm_write_sib1(const asn1::rrc_nr::sib1_s& sib1);
void scm_write_mastercellgroup(const asn1::rrc_nr::cell_group_cfg_s& mcg);