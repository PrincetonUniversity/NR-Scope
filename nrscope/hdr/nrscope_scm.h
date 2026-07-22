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

// Depend only on srsRAN types, not nrscope internals: srsran_mib_nr_t (MIB) and
// the ASN.1 RRC types (SIB1, masterCellGroup). Keeps SCM loosely coupled so it
// could be lifted out to depend on srsRAN alone.
#include <cstdint>

#include "srsran/asn1/rrc_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"

// Toggled by load_config.cc when `scm: true` is present in the config.
extern bool scm_enabled;

// Handoffs from the three decode sites. No-ops when scm_enabled is false.
//   cell : task_scheduler.cc  DecodeMIB   — detected PCI, the SSB tuning
//          frequency (absolute-frequency anchor; band/duplex derive from it),
//          and the decoded MIB.
//   SIB1 : sibs_decoder.cc    DecodeandParseSIB1fromSlot
//   MCG  : rach_decoder.cc    DecodeandParseMS4fromSlot (masterCellGroup)
void scm_on_cell(uint32_t pci, double ssb_freq_hz, const srsran_mib_nr_t& mib);
void scm_on_sib1(const asn1::rrc_nr::sib1_s& sib1);
void scm_on_master_cell_group(const asn1::rrc_nr::cell_group_cfg_s& mcg);