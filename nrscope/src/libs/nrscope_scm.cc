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

// Minimal MIB/cell JSON. TODO(step 4): full field printer for srsran_mib_nr_t.
// TODO: lookup the available data here, probably want to print everything available
std::string cell_to_json(const cell_search_result_t& cell) {
  std::string s = "{";
  s += "\"pci\": " + std::to_string(cell.pci) + ", ";
  s += "\"duplex_mode\": " +
       std::string(cell.duplex_mode == SRSRAN_DUPLEX_MODE_TDD ? "\"TDD\"" : "\"FDD\"") + ", ";
  s += "\"ssb_abs_freq_hz\": " + std::to_string(cell.ssb_abs_freq_hz) + ", ";
  s += "\"scs_common\": " + std::to_string((int)cell.mib.scs_common) + ", ";
  s += "\"coreset0_idx\": " + std::to_string(cell.mib.coreset0_idx);
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
  out += "  \"mib\": " + cell_to_json(s.cell) + ",\n";
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
  printf("SCM: got cell info and MIB (PCI %d, freq %lld Hz)\n", cell.pci, cell.ssb_abs_freq_hz);
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
