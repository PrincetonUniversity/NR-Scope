// SCM (Spectrum Consumption Model) mode header library.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/time.h>

#include "srsran/asn1/rrc_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"

// Assumes srsRAN's asn1_utils.h emits valid JSON (nrscope fixes a SetupRelease to_json bug).
// Output writes are in append mode, not process safe (add flock for that)
namespace scm_detail {

inline uint64_t now_ms() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

// All SCM state in one place, shared across TUs via the state() singleton below.
// sensor_id is operator config; ssb_freq_hz/pci/mib_capture_ms are latched per
// capture by scm_write_mib and carried onto that cell's sib1/mcg lines.
struct State {
  std::mutex    mutex;
  bool          enabled        = false;
  std::string   sensor_id      = "unnamed_sensor";
  std::string   filename       = "raw_scm_data.jsonl";  // output path (JSON Lines)
  uint64_t      scan_start_ms  = 0;    // set at construction (~process start)
  double        ssb_freq_hz    = 0.0;
  uint32_t      pci            = 0;
  uint64_t      mib_capture_ms = 0;
  std::ofstream file;                  // opened lazily on first write (append)

  State() : scan_start_ms(now_ms()) {}
};

// One instance across all translation units (C++11 guarantees thread-safe init).
inline State& state() {
  static State s;
  return s;
}

inline std::string jbool(bool b) { return b ? "true" : "false"; }

// Hz are whole numbers; render without the ".000000" std::to_string(double) adds.
inline std::string hz_str(double v) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.0f", v);
  return buf;
}

// srsran_mib_nr_t is a flat struct of scalars/enums (TS 38.331 MIB). Enum/bool
// renderings mirror srsRAN's own srsran_pbch_msg_nr_mib_info() printer. This is
// the "mib" record's payload; pci / freq / capture time live in the envelope.
inline std::string mib_to_json(const srsran_mib_nr_t& mib) {
  std::string s = "{";
  s += "\"sfn\": " + std::to_string(mib.sfn) + ", ";
  s += "\"ssb_idx\": " + std::to_string(mib.ssb_idx) + ", ";
  s += "\"hrf\": " + jbool(mib.hrf) + ", ";
  s += "\"scs_common\": \"" + std::string(srsran_subcarrier_spacing_to_str(mib.scs_common)) + "\", ";
  s += "\"ssb_offset\": " + std::to_string(mib.ssb_offset) + ", ";
  s += "\"dmrs_typeA_pos\": \"" +
       std::string(mib.dmrs_typeA_pos == srsran_dmrs_sch_typeA_pos_2 ? "pos2" : "pos3") + "\", ";
  s += "\"coreset0_idx\": " + std::to_string(mib.coreset0_idx) + ", ";
  s += "\"ss0_idx\": " + std::to_string(mib.ss0_idx) + ", ";
  s += "\"cell_barred\": " + jbool(mib.cell_barred) + ", ";
  s += "\"intra_freq_reselection\": " + jbool(mib.intra_freq_reselection) + ", ";
  s += "\"spare\": " + std::to_string(mib.spare);
  s += "}";
  return s;
}

// Append one enveloped JSON line. Caller must hold state().mutex. The five
// identity fields are identical across a capture's three lines; only `type` and
// `record` change. Join key = (sensor_id, scan_start_ms, mib_capture_ms).
//
// srsRAN's asn1::json_writer pretty-prints with embedded newlines, which would
// break the one-object-per-line invariant; strip them so each record is a single
// line. Safe here — none of these ASN.1 string values contain a literal newline.
inline void write_record_locked(const char* type, std::string json) {
  State& st = state();
  if (!st.file.is_open()) st.file.open(st.filename, std::ios::app);
  json.erase(std::remove(json.begin(), json.end(), '\n'), json.end());
  st.file << "{\"type\": \"" << type << "\""
          << ", \"sensor_id\": \"" << st.sensor_id << "\""
          << ", \"scan_start_ms\": " << st.scan_start_ms
          << ", \"mib_capture_ms\": " << st.mib_capture_ms
          << ", \"ssb_freq_hz\": " << hz_str(st.ssb_freq_hz)
          << ", \"pci\": " << st.pci
          << ", \"record\": " << json << "}\n";
  st.file.flush();
}

}  // namespace scm_detail

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Enable SCM and set its config. Call once at startup (from load_config.cc) when
// the `scm` config block is enabled — calling this is what turns SCM on. Both
// args have sensible defaults, so a bare scm_init() enables with them.
inline void scm_init(const std::string& sensor_id = "unnamed_sensor",
                     const std::string& filename  = "raw_scm_data.jsonl") {
  scm_detail::State& st = scm_detail::state();
  std::lock_guard<std::mutex> lock(st.mutex);
  st.sensor_id = sensor_id;
  st.filename  = filename;
  st.enabled   = true;
}

inline bool scm_is_enabled() { return scm_detail::state().enabled; }

// Convenience init from a config node (e.g. a yaml-cpp YAML::Node for the `scm:`
// block). Templated on the node type so this header stays dependency-free: the
// body is compiled only in the caller's TU, which already knows the node type —
// a project without yaml-cpp simply never calls this and uses scm_init() instead.
// Returns whether SCM was enabled. Reads: node["enabled"] (bool); optional
// node["sensor_id"], node["output"].
template <typename ConfigNode>
inline bool scm_init_from_config(const ConfigNode& scm) {
  bool enabled = scm["enabled"] ? scm["enabled"].template as<bool>() : false;
  if (!enabled) return false;
  std::string sensor_id = scm["sensor_id"] ? scm["sensor_id"].template as<std::string>() : "unnamed_sensor";
  std::string output    = scm["output"] ? scm["output"].template as<std::string>() : "raw_scm_data.jsonl";
  scm_init(sensor_id, output);
  return true;
}


// Handoffs from the three decode sites. No-ops when SCM is disabled.
//   MIB  : task_scheduler.cc  DecodeMIB   — detected PCI + SSB tuning frequency
//          + MIB. Starts a new capture (latches pci / ssb_freq_hz / mib_capture_ms).
//   SIB1 : sibs_decoder.cc    DecodeandParseSIB1fromSlot
//   MCG  : rach_decoder.cc    DecodeandParseMS4fromSlot (masterCellGroup)
inline void scm_write_mib(uint32_t pci, double ssb_freq_hz, const srsran_mib_nr_t& mib) {
  scm_detail::State& st = scm_detail::state();
  if (!st.enabled) return;
  uint64_t ts = scm_detail::now_ms();
  printf("SCM: got cell info and MIB (PCI %u, freq %.0f Hz)\n", pci, ssb_freq_hz);

  std::lock_guard<std::mutex> lock(st.mutex);
  // A MIB is the first record of a newly-found cell → start a new capture: latch
  // the identity that this cell's sib1/mcg lines will also carry.
  st.pci            = pci;
  st.ssb_freq_hz    = ssb_freq_hz;
  st.mib_capture_ms = ts;
  scm_detail::write_record_locked("mib", scm_detail::mib_to_json(mib));
}

inline void scm_write_sib1(const asn1::rrc_nr::sib1_s& sib1) {
  scm_detail::State& st = scm_detail::state();
  if (!st.enabled) return;
  printf("SCM: got SIB1\n");
  asn1::json_writer js;
  sib1.to_json(js);

  std::lock_guard<std::mutex> lock(st.mutex);
  scm_detail::write_record_locked("sib1", js.to_string());
}

inline void scm_write_mastercellgroup(const asn1::rrc_nr::cell_group_cfg_s& mcg) {
  scm_detail::State& st = scm_detail::state();
  if (!st.enabled) return;
  printf("SCM: got master cell group from RACH MSG4\n");
  asn1::json_writer js;
  mcg.to_json(js);

  {
    std::lock_guard<std::mutex> lock(st.mutex);
    scm_detail::write_record_locked("master_cell_group", js.to_string());
  }

  // TEMP (testing): masterCellGroup is the last of the three records, so exit
  // once we have it. Remove when we add proper exit handling.
  printf("SCM: got all records, exiting\n");
  fflush(stdout);
  exit(0);
}
