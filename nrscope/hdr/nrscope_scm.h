// SCM (Spectrum Consumption Model) mode.
//
// When enabled (config `scm: true`), NR-Scope latches the cell's decoded
// broadcast identity — MIB (+PCI/duplex/freq), SIB1, and the RACH msg4
// masterCellGroup — and, once it has all three, dumps them to scm.json and
// exits.
//
// Design (see scm-mode-analysis.md "SCM mode implementation notes"):
//   - Free functions over a file-static singleton (mirrors the g_silent idiom);
//     callers don't hold any SCM object.
//   - The three decoders run on concurrent worker threads, so the singleton is
//     mutex-guarded and latches its own copies (SlotResult/WorkState are reset
//     every slot).
#pragma once

#include "nrscope/hdr/nrscope_def.h"

// Toggled by load_config.cc when `scm: true` is present in the config.
extern bool scm_enabled;

// Handoffs from the three decode sites. No-ops when scm_enabled is false.
//   MIB  : task_scheduler.cc  DecodeMIB                 (pass the cell wrapper so
//          PCI / duplex_mode / ssb_abs_freq_hz ride along with the MIB)
//   SIB1 : sibs_decoder.cc    DecodeandParseSIB1fromSlot
//   MCG  : rach_decoder.cc    DecodeandParseMS4fromSlot (masterCellGroup)
void scm_on_cell(const cell_search_result_t& cell);
void scm_on_sib1(const asn1::rrc_nr::sib1_s& sib1);
void scm_on_master_cell_group(const asn1::rrc_nr::cell_group_cfg_s& mcg);
