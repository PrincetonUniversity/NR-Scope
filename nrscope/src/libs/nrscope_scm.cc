#include "nrscope/hdr/nrscope_scm.h"

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>

// Off unless load_config.cc sees `scm: true`.
bool scm_enabled = false;

namespace {

// Output file (hardcoded for now; make configurable later).
constexpr const char* kScmOutputPath = "raw_scm_data.json";

// The single latched record for the targeted cell. Guarded by `mtx`: the three
// handoffs are called from concurrent worker threads.
struct ScmState {
  std::mutex mtx;

  bool have_cell = false;  // MIB (+ PCI / duplex / freq)
  bool have_sib1 = false;
  bool have_mcg  = false;
  bool written   = false;

  cell_search_result_t             cell;
  asn1::rrc_nr::sib1_s             sib1;
  asn1::rrc_nr::cell_group_cfg_s   mcg;
};

ScmState& state() {
  static ScmState s;
  return s;
}

std::string jbool(bool b) { return b ? "true" : "false"; }

// srsran_mib_nr_t is a flat struct of scalars/enums (TS 38.331 MIB). Enum/bool
// renderings mirror srsRAN's own srsran_pbch_msg_nr_mib_info() printer.
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

// cell_search_result_t: SSB/PBCH-block detection results plus the decoded MIB.
// Also a flat struct — every field is a scalar, an enum (with a to_str helper),
// or the nested MIB above.
std::string cell_to_json(const cell_search_result_t& cell) {
  std::string s = "{";
  s += "\"found\": " + jbool(cell.found) + ", ";
  s += "\"pci\": " + std::to_string(cell.pci) + ", ";
  s += "\"ssb_abs_freq_hz\": " + std::to_string(cell.ssb_abs_freq_hz) + ", ";
  s += "\"ssb_scs\": \"" + std::string(srsran_subcarrier_spacing_to_str(cell.ssb_scs)) + "\", ";
  s += "\"ssb_pattern\": \"" + std::string(srsran_ssb_pattern_to_str(cell.ssb_pattern)) + "\", ";
  s += "\"duplex_mode\": \"" +
       std::string(cell.duplex_mode == SRSRAN_DUPLEX_MODE_TDD ? "TDD" : "FDD") + "\", ";
  s += "\"k_ssb\": " + std::to_string(cell.k_ssb) + ", ";
  s += "\"abs_ssb_scs\": " + std::to_string(cell.abs_ssb_scs) + ", ";
  s += "\"abs_pdcch_scs\": " + std::to_string(cell.abs_pdcch_scs) + ", ";
  s += "\"u\": " + std::to_string(cell.u) + ", ";
  s += "\"mib\": " + mib_to_json(cell.mib);
  s += "}";
  return s;
}

// Assumes state().mtx is held and all three records are present.
void write_and_exit_locked() {
  ScmState& s = state();

  asn1::json_writer js_sib1;
  s.sib1.to_json(js_sib1);
  asn1::json_writer js_mcg;
  s.mcg.to_json(js_mcg);

  std::string out = "{\n";
  out += "  \"cell\": " + cell_to_json(s.cell) + ",\n";
  out += "  \"sib1\": " + std::string(js_sib1.to_string()) + ",\n";
  out += "  \"master_cell_group\": " + std::string(js_mcg.to_string()) + "\n";
  out += "}\n";

  std::ofstream f(kScmOutputPath);
  f << out;
  f.close();

  s.written = true;
  printf("SCM: wrote %s (MIB + SIB1 + masterCellGroup), exiting\n", kScmOutputPath);
  fflush(stdout);
  exit(0);
}

// Call with state().mtx held. Writes + exits once all three are latched.
void maybe_finish_locked() {
  ScmState& s = state();
  if (s.written) return;
  if (s.have_cell && s.have_sib1 && s.have_mcg) {
    write_and_exit_locked();
  }
}

}  // namespace

void scm_on_cell(const cell_search_result_t& cell) {
  if (!scm_enabled) return;
  std::lock_guard<std::mutex> lock(state().mtx);
  printf("SCM: got cell info and MIB (PCI %u, freq %.0f Hz)\n", cell.pci, cell.ssb_abs_freq_hz);
  state().cell      = cell;
  state().have_cell = true;
  maybe_finish_locked();
}

void scm_on_sib1(const asn1::rrc_nr::sib1_s& sib1) {
  if (!scm_enabled) return;
  std::lock_guard<std::mutex> lock(state().mtx);
  printf("SCM: got SIB1\n");
  state().sib1      = sib1;
  state().have_sib1 = true;
  maybe_finish_locked();
}

void scm_on_master_cell_group(const asn1::rrc_nr::cell_group_cfg_s& mcg) {
  if (!scm_enabled) return;
  std::lock_guard<std::mutex> lock(state().mtx);
  printf("SCM: got master cell group from RACH MSG4\n");
  state().mcg      = mcg;
  state().have_mcg = true;
  maybe_finish_locked();
}
