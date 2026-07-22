#include "nrscope/hdr/nrscope_scm.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/time.h>

// Off unless load_config.cc sees `scm: true`.
bool scm_enabled = false;

namespace {

// Output file (hardcoded for now; make configurable later). One JSON object per
// line (JSON Lines): each decoder appends its record independently as it decodes
// it — no batching, no completion tracking.
constexpr const char* kScmOutputPath = "raw_scm_data.jsonl";

// Serializes appends + the latched identity below; decoders run on worker threads.
std::mutex& scm_mutex() {
  static std::mutex m;
  return m;
}

// Identity stamped on every line (all guarded by scm_mutex()). sensor_id is
// operator config; the other three are latched per capture by scm_write_mib and
// then carried onto that cell's sib1/mcg lines.
std::string g_sensor_id = "unnamed_sensor";
double      g_ssb_freq_hz    = 0.0;
uint32_t    g_pci            = 0;
uint64_t    g_mib_capture_ms = 0;

uint64_t now_ms() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

// Captured once, on first use: the scan-session start, i.e. this process's start.
// Groups all captures from one scan run.
uint64_t scan_start_ms() {
  static uint64_t s = now_ms();
  return s;
}

// Opened once (truncating any prior file) on first record, then kept open and
// appended. The function-local static gives us truncate-once + append-after with
// no explicit "first write" flag.
std::ofstream& scm_file() {
  static std::ofstream f(kScmOutputPath, std::ios::trunc);
  return f;
}

// Hz are whole numbers; render without the ".000000" std::to_string(double) adds.
std::string hz_str(double v) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.0f", v);
  return buf;
}

// Append one enveloped JSON line. Caller must hold scm_mutex(). The five identity
// fields are identical across a capture's three lines; only `type` and `record`
// change. Join key = (sensor_id, scan_start_ms, mib_capture_ms).
//
// srsRAN's asn1::json_writer pretty-prints with embedded newlines, which would
// break the one-object-per-line invariant; strip them so each record is a single
// line. Safe here — none of these ASN.1 string values contain a literal newline
// (JSON is otherwise whitespace-insensitive, so leftover indent spaces are fine).
void write_record_locked(const char* type, std::string json) {
  json.erase(std::remove(json.begin(), json.end(), '\n'), json.end());
  scm_file() << "{\"type\": \"" << type << "\""
             << ", \"sensor_id\": \"" << g_sensor_id << "\""
             << ", \"scan_start_ms\": " << scan_start_ms()
             << ", \"mib_capture_ms\": " << g_mib_capture_ms
             << ", \"ssb_freq_hz\": " << hz_str(g_ssb_freq_hz)
             << ", \"pci\": " << g_pci
             << ", \"record\": " << json << "}\n";
  scm_file().flush();
}

std::string jbool(bool b) { return b ? "true" : "false"; }

// srsran_mib_nr_t is a flat struct of scalars/enums (TS 38.331 MIB). Enum/bool
// renderings mirror srsRAN's own srsran_pbch_msg_nr_mib_info() printer. This is
// the "mib" record's payload; pci / freq / capture time live in the envelope.
std::string mib_to_json(const srsran_mib_nr_t& mib) {
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

}  // namespace

void scm_set_sensor_id(const std::string& sensor_id) {
  std::lock_guard<std::mutex> lock(scm_mutex());
  g_sensor_id = sensor_id;
}

void scm_write_mib(uint32_t pci, double ssb_freq_hz, const srsran_mib_nr_t& mib) {
  if (!scm_enabled) return;
  uint64_t ts = now_ms();
  printf("SCM: got cell info and MIB (PCI %u, freq %.0f Hz)\n", pci, ssb_freq_hz);

  std::lock_guard<std::mutex> lock(scm_mutex());
  // A MIB is the first record of a newly-found cell → start a new capture: latch
  // the identity that this cell's sib1/mcg lines will also carry.
  g_pci            = pci;
  g_ssb_freq_hz    = ssb_freq_hz;
  g_mib_capture_ms = ts;
  write_record_locked("mib", mib_to_json(mib));
}

void scm_write_sib1(const asn1::rrc_nr::sib1_s& sib1) {
  if (!scm_enabled) return;
  printf("SCM: got SIB1\n");
  asn1::json_writer js;
  sib1.to_json(js);

  std::lock_guard<std::mutex> lock(scm_mutex());
  write_record_locked("sib1", js.to_string());
}

void scm_write_mastercellgroup(const asn1::rrc_nr::cell_group_cfg_s& mcg) {
  if (!scm_enabled) return;
  printf("SCM: got master cell group from RACH MSG4\n");
  asn1::json_writer js;
  mcg.to_json(js);

  {
    std::lock_guard<std::mutex> lock(scm_mutex());
    write_record_locked("master_cell_group", js.to_string());
  }

  // TEMP (testing): masterCellGroup is the last of the three records, so exit
  // once we have it. Remove when we add proper exit handling.
  printf("SCM: got all records, exiting\n");
  fflush(stdout);
  exit(0);
}
