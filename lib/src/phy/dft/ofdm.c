/**
 * Copyright 2013-2023 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srsran/srsran.h"
#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "srsran/phy/common/phy_common.h"
#include "srsran/phy/dft/dft.h"
#include "srsran/phy/dft/ofdm.h"
#include "srsran/phy/utils/debug.h"
#include "srsran/phy/utils/vector.h"

/* Uncomment next line for avoiding Guru DFT call */
//#define AVOID_GURU

static int ofdm_init_mbsfn_(srsran_ofdm_t* q, srsran_ofdm_cfg_t* cfg, srsran_dft_dir_t dir)
{
  // If the symbol size is not given, calculate in function of the number of resource blocks
  if (cfg->symbol_sz == 0) {
    int symbol_sz_err = srsran_symbol_sz(cfg->nof_prb);
    if (symbol_sz_err <= SRSRAN_SUCCESS) {
      ERROR("Invalid number of PRB %d", cfg->nof_prb);
      return SRSRAN_ERROR;
    }
    cfg->symbol_sz = (uint32_t)symbol_sz_err;
  }

  // Check if there is nothing to configure
  if (memcmp(&q->cfg, cfg, sizeof(srsran_ofdm_cfg_t)) == 0) {
    return SRSRAN_SUCCESS;
  }

  if (q->max_prb > 0) {
    // The object was already initialised, update only resizing params
    q->cfg.cp        = cfg->cp;
    q->cfg.nof_prb   = cfg->nof_prb;
    q->cfg.symbol_sz = cfg->symbol_sz;
  } else {
    // Otherwise copy all parameters
    q->cfg = *cfg;

    // Phase compensation is set when it is calculated
    q->cfg.phase_compensation_hz = 0.0;
  }

  // printf("q->cfg.cp duration: %f us\n", (float)q->cfg.cp/23040000*1000000);
  uint32_t    symbol_sz = q->cfg.symbol_sz;
  srsran_cp_t cp        = q->cfg.cp;
  srsran_sf_t sf_type   = q->cfg.sf_type;

  // Set OFDM object attributes
  q->nof_symbols       = SRSRAN_CP_NSYMB(cp);
  q->nof_symbols_mbsfn = SRSRAN_CP_NSYMB(SRSRAN_CP_EXT);
  q->nof_re            = cfg->nof_prb * SRSRAN_NRE;
  q->nof_guards        = (q->cfg.symbol_sz - q->nof_re) / 2U;
  q->slot_sz           = (uint32_t)SRSRAN_SLOT_LEN(q->cfg.symbol_sz);
  // printf("q->slot_sz: %u\n", q->slot_sz);
  q->sf_sz             = (uint32_t)SRSRAN_SF_LEN(q->cfg.symbol_sz);

  // Set the CFR parameters related to OFDM symbol and FFT size
  q->cfg.cfr_tx_cfg.symbol_sz = symbol_sz;
  q->cfg.cfr_tx_cfg.symbol_bw = q->nof_re;

  // in the DL, the DC carrier is empty but still counts when designing the filter BW
  q->cfg.cfr_tx_cfg.dc_sc = (!q->cfg.keep_dc) && (!isnormal(q->cfg.freq_shift_f));
  if (q->cfg.cfr_tx_cfg.cfr_enable) {
    if (srsran_cfr_init(&q->tx_cfr, &q->cfg.cfr_tx_cfg) < SRSRAN_SUCCESS) {
      ERROR("Error while initialising CFR module");
      return SRSRAN_ERROR;
    }
  }
  // Plan MBSFN
  if (q->fft_plan.size) {
    // Replan if it was initialised previously
    if (srsran_dft_replan(&q->fft_plan, q->cfg.symbol_sz)) {
      ERROR("Replanning DFT plan");
      return SRSRAN_ERROR;
    }
  } else {
    // Create plan from zero otherwise
    if (srsran_dft_plan_c(&q->fft_plan, symbol_sz, dir)) {
      ERROR("Creating DFT plan");
      return SRSRAN_ERROR;
    }
  }

  // Reallocate temporal buffer only if the new number of resource blocks is bigger than initial
  if (q->cfg.nof_prb > q->max_prb) {
    // Free before reallocating if allocated
    if (q->tmp) {
      free(q->tmp);
      free(q->shift_buffer);
    }

#ifdef AVOID_GURU
    q->tmp = srsran_vec_cf_malloc(symbol_sz);
#else
    q->tmp = srsran_vec_cf_malloc(q->sf_sz);
#endif /* AVOID_GURU */
    if (!q->tmp) {
      perror("malloc");
      return SRSRAN_ERROR;
    }

    q->shift_buffer = srsran_vec_cf_malloc(q->sf_sz);
    if (!q->shift_buffer) {
      perror("malloc");
      return SRSRAN_ERROR;
    }

    q->window_offset_buffer = srsran_vec_cf_malloc(q->sf_sz);
    if (!q->window_offset_buffer) {
      perror("malloc");
      return SRSRAN_ERROR;
    }

    q->max_prb = cfg->nof_prb;
  }

#ifdef AVOID_GURU
  srsran_vec_cf_zero(q->tmp, symbol_sz);
#else
  uint32_t nof_prb = q->cfg.nof_prb;
  cf_t* in_buffer = q->cfg.in_buffer;
  cf_t* out_buffer = q->cfg.out_buffer;
  int cp1 = SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM(0, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);
  int cp2 = SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM(1, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);
  // printf("cp1: %d\n", cp1);
  // printf("cp1 duration: %f us\n", (float)cp1/23040000 * 1000000);
  // printf("cp2: %d\n", cp2);
  // printf("cp2 duration: %f us\n", (float)cp2/23040000 * 1000000);

  // Slides DFT window a fraction of cyclic prefix, it does not apply for the inverse-DFT
  if (isnormal(cfg->rx_window_offset)) {
    cfg->rx_window_offset = SRSRAN_MAX(0, cfg->rx_window_offset);   // Needs to be positive
    cfg->rx_window_offset = SRSRAN_MIN(100, cfg->rx_window_offset); // Needs to be below 100
    q->window_offset_n = (uint32_t)roundf((float)cp2 * cfg->rx_window_offset);

    for (uint32_t i = 0; i < symbol_sz; i++) {
      q->window_offset_buffer[i] = cexpf(I * M_PI * 2.0f * (float)q->window_offset_n * (float)i / (float)symbol_sz);
    }
  }

  // Zero temporal and input buffers always
  srsran_vec_cf_zero(q->tmp, q->sf_sz);

  if (dir == SRSRAN_DFT_BACKWARD) {
    srsran_vec_cf_zero(in_buffer, SRSRAN_SF_LEN_RE(nof_prb, cp));
  } else {
    // We already set the buffer, so we can skip this or we don't need to intialize it every time
    // we get a frame.
    srsran_vec_cf_zero(in_buffer, q->sf_sz);
  }

  for (int slot = 0; slot < SRSRAN_NOF_SLOTS_PER_SF; slot++) {
    // If Guru DFT was allocated, free
    if (q->fft_plan_sf[slot].size) {
      srsran_dft_plan_free(&q->fft_plan_sf[slot]);
    }
    // Create Tx/Rx plans
    if (dir == SRSRAN_DFT_FORWARD) {
      if (srsran_dft_plan_guru_c(&q->fft_plan_sf[slot],
                                 symbol_sz,
                                 dir,
                                 in_buffer + cp1 + q->slot_sz * slot - q->window_offset_n,
                                 q->tmp,
                                 1,
                                 1,
                                 SRSRAN_CP_NSYMB(cp),
                                 symbol_sz + cp2,
                                 symbol_sz)) {
        ERROR("Creating Guru DFT plan (%d)", slot);
        return SRSRAN_ERROR;
      }
    } else {
      if (srsran_dft_plan_guru_c(&q->fft_plan_sf[slot],
                                 symbol_sz,
                                 dir,
                                 q->tmp,
                                 out_buffer + cp1 + q->slot_sz * slot,
                                 1,
                                 1,
                                 SRSRAN_CP_NSYMB(cp),
                                 symbol_sz,
                                 symbol_sz + cp2)) {
        ERROR("Creating Guru inverse-DFT plan (%d)", slot);
        return SRSRAN_ERROR;
      }
    }
  }
#endif

  srsran_dft_plan_set_mirror(&q->fft_plan, true);

  DEBUG("Init %s symbol_sz=%d, nof_symbols=%d, cp=%s, nof_re=%d, nof_guards=%d",
        dir == SRSRAN_DFT_FORWARD ? "FFT" : "iFFT",
        q->cfg.symbol_sz,
        q->nof_symbols,
        q->cfg.cp == SRSRAN_CP_NORM ? "Normal" : "Extended",
        q->nof_re,
        q->nof_guards);

  // MBSFN logic
  if (sf_type == SRSRAN_SF_MBSFN) {
    q->mbsfn_subframe   = true;
    q->non_mbsfn_region = 2; // default set to 2
  } else {
    q->mbsfn_subframe = false;
  }

  // Set other parameters
  srsran_ofdm_set_freq_shift(q, q->cfg.freq_shift_f);
  srsran_dft_plan_set_norm(&q->fft_plan, q->cfg.normalize);
  srsran_dft_plan_set_dc(&q->fft_plan, (!cfg->keep_dc) && (!isnormal(q->cfg.freq_shift_f)));

  // set phase compensation
  if (srsran_ofdm_set_phase_compensation(q, cfg->phase_compensation_hz) < SRSRAN_SUCCESS) {
    ERROR("Error setting phase compensation");
    return SRSRAN_ERROR;
  }

  return SRSRAN_SUCCESS;
}

// the fft plan for 5g NR, added by Haoran
static int ofdm_init_nr_nrscope_30khz(srsran_ofdm_t* q, srsran_ofdm_cfg_t* cfg, srsran_dft_dir_t dir, int scs_idx)
{
  // If the symbol size is not given, calculate in function of the number of resource blocks
  if (cfg->symbol_sz == 0) {
    int symbol_sz_err = srsran_symbol_sz(cfg->nof_prb);
    if (symbol_sz_err <= SRSRAN_SUCCESS) {
      ERROR("Invalid number of PRB %d", cfg->nof_prb);
      return SRSRAN_ERROR;
    }
    cfg->symbol_sz = (uint32_t)symbol_sz_err;
  }

  // Check if there is nothing to configure
  if (memcmp(&q->cfg, cfg, sizeof(srsran_ofdm_cfg_t)) == 0) {
    return SRSRAN_SUCCESS;
  }

  if (q->max_prb > 0) {
    // The object was already initialised, update only resizing params
    q->cfg.cp        = cfg->cp;
    q->cfg.nof_prb   = cfg->nof_prb;
    q->cfg.symbol_sz = cfg->symbol_sz;
  } else {
    // Otherwise copy all parameters
    q->cfg = *cfg;

    // Phase compensation is set when it is calculated
    q->cfg.phase_compensation_hz = 0.0;
  }

  uint32_t    symbol_sz = q->cfg.symbol_sz;
  srsran_cp_t cp        = q->cfg.cp;
  srsran_sf_t sf_type   = q->cfg.sf_type;

  // Set OFDM object attributes
  q->nof_symbols       = SRSRAN_CP_NSYMB_NR(cp);
  // q->nof_symbols_mbsfn = SRSRAN_CP_NSYMB(SRSRAN_CP_EXT); // not necessary here
  // q->nof_symbols       = SRSRAN_CP_NSYMB(cp);
  q->nof_re            = cfg->nof_prb * SRSRAN_NRE;
  q->nof_guards        = (q->cfg.symbol_sz - q->nof_re) / 2U;
  q->slot_sz           = (uint32_t)SRSRAN_SLOT_LEN_NR(q->cfg.symbol_sz);
  q->sf_sz             = (uint32_t)SRSRAN_SF_LEN_NR(q->cfg.symbol_sz, scs_idx);

  // printf("q->slot_sz: %d\n", q->slot_sz);
  // printf("q->sf_sz: %d\n", q->sf_sz);
  // printf("symbol_sz: %d\n", symbol_sz);
  // printf("q->nof_symbols: %d\n", q->nof_symbols);

  // Set the CFR parameters related to OFDM symbol and FFT size
  q->cfg.cfr_tx_cfg.symbol_sz = symbol_sz;
  q->cfg.cfr_tx_cfg.symbol_bw = q->nof_re;

  // in the DL, the DC carrier is empty but still counts when designing the filter BW
  q->cfg.cfr_tx_cfg.dc_sc = (!q->cfg.keep_dc) && (!isnormal(q->cfg.freq_shift_f));
  if (q->cfg.cfr_tx_cfg.cfr_enable) {
    if (srsran_cfr_init(&q->tx_cfr, &q->cfg.cfr_tx_cfg) < SRSRAN_SUCCESS) {
      ERROR("Error while initialising CFR module");
      return SRSRAN_ERROR;
    }
  }
  // Plan MBSFN
  if (q->fft_plan.size) {
    printf("srsran_dft_replan\n");
    // Replan if it was initialised previously
    if (srsran_dft_replan(&q->fft_plan, q->cfg.symbol_sz)) {
      ERROR("Replanning DFT plan");
      return SRSRAN_ERROR;
    }
  } else {
    // Create plan from zero otherwise
    if (srsran_dft_plan_c(&q->fft_plan, symbol_sz, dir)) {
      ERROR("Creating DFT plan");
      return SRSRAN_ERROR;
    }
  }

  // Reallocate temporal buffer only if the new number of resource blocks is bigger than initial
  if (q->cfg.nof_prb > q->max_prb) {
    // Free before reallocating if allocated
    if (q->tmp) {
      free(q->tmp);
      free(q->shift_buffer);
    }

#ifdef AVOID_GURU
    q->tmp = srsran_vec_cf_malloc(symbol_sz);
#else
    q->tmp = srsran_vec_cf_malloc(q->sf_sz);
#endif /* AVOID_GURU */
    if (!q->tmp) {
      perror("malloc");
      return SRSRAN_ERROR;
    }

    q->shift_buffer = srsran_vec_cf_malloc(q->sf_sz);
    if (!q->shift_buffer) {
      perror("malloc");
      return SRSRAN_ERROR;
    }

    q->window_offset_buffer = srsran_vec_cf_malloc(q->sf_sz);
    if (!q->window_offset_buffer) {
      perror("malloc");
      return SRSRAN_ERROR;
    }
    q->max_prb = cfg->nof_prb;
  }

#ifdef AVOID_GURU
  srsran_vec_cf_zero(q->tmp, symbol_sz);
#else
  uint32_t nof_prb = q->cfg.nof_prb;
  cf_t* in_buffer = q->cfg.in_buffer;
  cf_t* out_buffer = q->cfg.out_buffer;

  // change the cp setting for 5G NR
  // int cp1 = SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM_NR(symbol_sz) : SRSRAN_CP_LEN_EXT_NR(symbol_sz);
  
  int cp1 = SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM(0, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);
  int cp2 = SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM(1, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);

  // printf("cp1: %d\n", cp1);
  // printf("cp2: %d\n", cp2);

  // Slides DFT window a fraction of cyclic prefix, it does not apply for the inverse-DFT
  // we skip the window offset by setting cfg.rx_window_offset = 0;
  if (isnormal(cfg->rx_window_offset)) {
    cfg->rx_window_offset = SRSRAN_MAX(0, cfg->rx_window_offset);   // Needs to be positive
    cfg->rx_window_offset = SRSRAN_MIN(100, cfg->rx_window_offset); // Needs to be below 100
    q->window_offset_n = (uint32_t)roundf((float)cp2 * cfg->rx_window_offset);

    for (uint32_t i = 0; i < symbol_sz; i++) {
      q->window_offset_buffer[i] = cexpf(I * M_PI * 2.0f * (float)q->window_offset_n * (float)i / (float)symbol_sz);
    }

    // for (uint32_t i = symbol_sz; i < 2*symbol_sz; i++){
    //   q->window_offset_buffer[i] = cexpf(I * M_PI * 2.0f * (cp1 - cp2) * (float)(i-symbol_sz) / (float)symbol_sz);
    // }
  }

  // Zero temporal and input buffers always
  srsran_vec_cf_zero(q->tmp, q->sf_sz);

  if (dir == SRSRAN_DFT_BACKWARD) {
    srsran_vec_cf_zero(in_buffer, SRSRAN_SF_LEN_RE(nof_prb, cp));
  } else {
    // We already set the buffer, so we can skip this or we don't need to intialize it every time
    // we get a frame.
    srsran_vec_cf_zero(in_buffer, q->sf_sz);
  }

  for (int slot = 0; slot < 1; slot++) {
    // If Guru DFT was allocated, free
    if (q->fft_plan_sf[slot].size) {
      srsran_dft_plan_free(&q->fft_plan_sf[slot]);
    }
    // Create Tx/Rx plans
    if (dir == SRSRAN_DFT_FORWARD) {
      if (srsran_dft_plan_guru_c(&q->fft_plan_sf[slot],
                                 symbol_sz,
                                 dir,
                                 in_buffer + cp1 + q->slot_sz * slot - q->window_offset_n,
                                 q->tmp,
                                 1,
                                 1,
                                 SRSRAN_CP_NSYMB_NR(cp),
                                 symbol_sz + cp2,
                                 symbol_sz)) {
        ERROR("Creating Guru DFT plan (%d)", slot);
        return SRSRAN_ERROR;
      }
    } else {
      if (srsran_dft_plan_guru_c(&q->fft_plan_sf[slot],
                                 symbol_sz,
                                 dir,
                                 q->tmp,
                                 out_buffer + cp1 + q->slot_sz * slot,
                                 1,
                                 1,
                                 SRSRAN_CP_NSYMB_NR(cp),
                                 symbol_sz,
                                 symbol_sz + cp1)) {
        ERROR("Creating Guru inverse-DFT plan (%d)", slot);
        return SRSRAN_ERROR;
      }
    }
  }
#endif

  // What does the mirror do?
  srsran_dft_plan_set_mirror(&q->fft_plan, true);

  // printf("Init %s symbol_sz=%d, nof_symbols=%d, cp=%s, nof_re=%d, nof_guards=%d\n",
  //       dir == SRSRAN_DFT_FORWARD ? "FFT" : "iFFT",
  //       q->cfg.symbol_sz,
  //       q->nof_symbols,
  //       q->cfg.cp == SRSRAN_CP_NORM ? "Normal" : "Extended",
  //       q->nof_re,
  //       q->nof_guards);

  DEBUG("Init %s symbol_sz=%d, nof_symbols=%d, cp=%s, nof_re=%d, nof_guards=%d",
        dir == SRSRAN_DFT_FORWARD ? "FFT" : "iFFT",
        q->cfg.symbol_sz,
        q->nof_symbols,
        q->cfg.cp == SRSRAN_CP_NORM ? "Normal" : "Extended",
        q->nof_re,
        q->nof_guards);

  // MBSFN logic
  if (sf_type == SRSRAN_SF_MBSFN) {
    q->mbsfn_subframe   = true;
    q->non_mbsfn_region = 2; 
  } else {
    q->mbsfn_subframe = false;
  }

  // Set other parameters
  srsran_ofdm_set_freq_shift(q, q->cfg.freq_shift_f); // skipped because it's only for uplink
  srsran_dft_plan_set_norm(&q->fft_plan, q->cfg.normalize); // q->cfg.normaliza = 0
  srsran_dft_plan_set_dc(&q->fft_plan, (!cfg->keep_dc) && (!isnormal(q->cfg.freq_shift_f))); // is also 0

  // set phase compensation
  if (srsran_ofdm_set_phase_compensation_nrscope(q, cfg->phase_compensation_hz, scs_idx, nof_prb) < SRSRAN_SUCCESS) {
    ERROR("Error setting phase compensation");
    return SRSRAN_ERROR;
  }

  return SRSRAN_SUCCESS;
}


// the fft plan for 5g NR, added by Haoran
static int ofdm_init_nr_nrscope_15khz(srsran_ofdm_t* q, srsran_ofdm_cfg_t* cfg, srsran_dft_dir_t dir, int scs_idx)
{
  // If the symbol size is not given, calculate in function of the number of resource blocks
  if (cfg->symbol_sz == 0) {
    int symbol_sz_err = srsran_symbol_sz(cfg->nof_prb);
    if (symbol_sz_err <= SRSRAN_SUCCESS) {
      ERROR("Invalid number of PRB %d", cfg->nof_prb);
      return SRSRAN_ERROR;
    }
    cfg->symbol_sz = (uint32_t)symbol_sz_err;
  }

  // Check if there is nothing to configure
  if (memcmp(&q->cfg, cfg, sizeof(srsran_ofdm_cfg_t)) == 0) {
    return SRSRAN_SUCCESS;
  }

  if (q->max_prb > 0) {
    // The object was already initialised, update only resizing params
    q->cfg.cp        = cfg->cp;
    q->cfg.nof_prb   = cfg->nof_prb;
    q->cfg.symbol_sz = cfg->symbol_sz;
  } else {
    // Otherwise copy all parameters
    q->cfg = *cfg;

    // Phase compensation is set when it is calculated
    q->cfg.phase_compensation_hz = 0.0;
  }

  uint32_t    symbol_sz = q->cfg.symbol_sz;
  srsran_cp_t cp        = q->cfg.cp;
  srsran_sf_t sf_type   = q->cfg.sf_type;

  // Set OFDM object attributes
  // Original code
  // q->nof_symbols       = SRSRAN_CP_NSYMB_NR(cp);
  // // q->nof_symbols_mbsfn = SRSRAN_CP_NSYMB(SRSRAN_CP_EXT); // not necessary here
  // // q->nof_symbols       = SRSRAN_CP_NSYMB(cp);
  // q->nof_re            = cfg->nof_prb * SRSRAN_NRE;
  // q->nof_guards        = (q->cfg.symbol_sz - q->nof_re) / 2U;
  // q->slot_sz           = (uint32_t)SRSRAN_SLOT_LEN_NR(q->cfg.symbol_sz);
  // q->sf_sz             = (uint32_t)SRSRAN_SF_LEN_NR(q->cfg.symbol_sz, scs_idx);

  // Grab from original FFR init
  q->nof_symbols       = SRSRAN_CP_NSYMB(cp);
  // q->nof_symbols_mbsfn = SRSRAN_CP_NSYMB(SRSRAN_CP_EXT);
  q->nof_re            = cfg->nof_prb * SRSRAN_NRE;
  q->nof_guards        = (q->cfg.symbol_sz - q->nof_re) / 2U;
  q->slot_sz           = (uint32_t)SRSRAN_SLOT_LEN(q->cfg.symbol_sz);
  // printf("q->slot_sz: %u\n", q->slot_sz);
  q->sf_sz             = (uint32_t)SRSRAN_SF_LEN(q->cfg.symbol_sz);


  // printf("q->slot_sz: %d\n", q->slot_sz);
  // printf("q->sf_sz: %d\n", q->sf_sz);
  // printf("symbol_sz: %d\n", symbol_sz);
  // printf("q->nof_symbols: %d\n", q->nof_symbols);

  // Set the CFR parameters related to OFDM symbol and FFT size
  q->cfg.cfr_tx_cfg.symbol_sz = symbol_sz;
  q->cfg.cfr_tx_cfg.symbol_bw = q->nof_re;

  // in the DL, the DC carrier is empty but still counts when designing the filter BW
  q->cfg.cfr_tx_cfg.dc_sc = (!q->cfg.keep_dc) && (!isnormal(q->cfg.freq_shift_f));
  if (q->cfg.cfr_tx_cfg.cfr_enable) {
    if (srsran_cfr_init(&q->tx_cfr, &q->cfg.cfr_tx_cfg) < SRSRAN_SUCCESS) {
      ERROR("Error while initialising CFR module");
      return SRSRAN_ERROR;
    }
  }
  // Plan MBSFN
  if (q->fft_plan.size) {
    printf("srsran_dft_replan\n");
    // Replan if it was initialised previously
    if (srsran_dft_replan(&q->fft_plan, q->cfg.symbol_sz)) {
      ERROR("Replanning DFT plan");
      return SRSRAN_ERROR;
    }
  } else {
    // Create plan from zero otherwise
    if (srsran_dft_plan_c(&q->fft_plan, symbol_sz, dir)) {
      ERROR("Creating DFT plan");
      return SRSRAN_ERROR;
    }
  }

  // Reallocate temporal buffer only if the new number of resource blocks is bigger than initial
  if (q->cfg.nof_prb > q->max_prb) {
    // Free before reallocating if allocated
    if (q->tmp) {
      free(q->tmp);
      free(q->shift_buffer);
    }

#ifdef AVOID_GURU
    q->tmp = srsran_vec_cf_malloc(symbol_sz);
#else
    q->tmp = srsran_vec_cf_malloc(q->sf_sz);
#endif /* AVOID_GURU */
    if (!q->tmp) {
      perror("malloc");
      return SRSRAN_ERROR;
    }

    q->shift_buffer = srsran_vec_cf_malloc(q->sf_sz);
    if (!q->shift_buffer) {
      perror("malloc");
      return SRSRAN_ERROR;
    }

    q->window_offset_buffer = srsran_vec_cf_malloc(q->sf_sz);
    if (!q->window_offset_buffer) {
      perror("malloc");
      return SRSRAN_ERROR;
    }
    q->max_prb = cfg->nof_prb;
  }

#ifdef AVOID_GURU
  srsran_vec_cf_zero(q->tmp, symbol_sz);
#else
  uint32_t nof_prb = q->cfg.nof_prb;
  cf_t* in_buffer = q->cfg.in_buffer;
  cf_t* out_buffer = q->cfg.out_buffer;

  // change the cp setting for 5G NR
  // int cp1 = SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM_NR(symbol_sz) : SRSRAN_CP_LEN_EXT_NR(symbol_sz);
  // printf("cp1: %d\n", cp1);

  int cp1 = SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM(0, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);
  int cp2 = SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM(1, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);

  // printf("cp1: %d\n", cp1);
  // printf("cp2: %d\n", cp2);

  // Slides DFT window a fraction of cyclic prefix, it does not apply for the inverse-DFT
  // we skip the window offset by setting cfg.rx_window_offset = 0;
  if (isnormal(cfg->rx_window_offset)) {
    cfg->rx_window_offset = SRSRAN_MAX(0, cfg->rx_window_offset);   // Needs to be positive
    cfg->rx_window_offset = SRSRAN_MIN(100, cfg->rx_window_offset); // Needs to be below 100
    q->window_offset_n = (uint32_t)roundf((float)cp2 * cfg->rx_window_offset);

    for (uint32_t i = 0; i < symbol_sz; i++) {
      q->window_offset_buffer[i] = cexpf(I * M_PI * 2.0f * (float)q->window_offset_n * (float)i / (float)symbol_sz);
    }
  }

  // Zero temporal and input buffers always
  srsran_vec_cf_zero(q->tmp, q->sf_sz);

  if (dir == SRSRAN_DFT_BACKWARD) {
    srsran_vec_cf_zero(in_buffer, SRSRAN_SF_LEN_RE(nof_prb, cp));
  } else {
    // We already set the buffer, so we can skip this or we don't need to intialize it every time
    // we get a frame.
    srsran_vec_cf_zero(in_buffer, q->sf_sz);
  }

  for (int slot = 0; slot < SRSRAN_NOF_SLOTS_PER_SF; slot++) {
    // If Guru DFT was allocated, free
    if (q->fft_plan_sf[slot].size) {
      srsran_dft_plan_free(&q->fft_plan_sf[slot]);
    }
    // Create Tx/Rx plans
    if (dir == SRSRAN_DFT_FORWARD) {
      if (srsran_dft_plan_guru_c(&q->fft_plan_sf[slot],
                                 symbol_sz,
                                 dir,
                                 in_buffer + cp1 + q->slot_sz * slot - q->window_offset_n,
                                 q->tmp,
                                 1,
                                 1,
                                //  SRSRAN_CP_NSYMB_NR(cp),
                                 SRSRAN_CP_NSYMB(cp),
                                 symbol_sz + cp2,
                                 symbol_sz)
                                 ) {
        ERROR("Creating Guru DFT plan (%d)", slot);
        return SRSRAN_ERROR;
      }
    } else {
      if (srsran_dft_plan_guru_c(&q->fft_plan_sf[slot],
                                 symbol_sz,
                                 dir,
                                 q->tmp,
                                 out_buffer + cp1 + q->slot_sz * slot,
                                 1,
                                 1,
                                //  SRSRAN_CP_NSYMB_NR(cp),
                                 SRSRAN_CP_NSYMB(cp),
                                 symbol_sz,
                                 symbol_sz + cp2)) {
        ERROR("Creating Guru inverse-DFT plan (%d)", slot);
        return SRSRAN_ERROR;
      }
    }
  }
#endif

  // What does the mirror do?
  srsran_dft_plan_set_mirror(&q->fft_plan, true);

  // printf("Init %s symbol_sz=%d, nof_symbols=%d, cp=%s, nof_re=%d, nof_guards=%d\n",
  //       dir == SRSRAN_DFT_FORWARD ? "FFT" : "iFFT",
  //       q->cfg.symbol_sz,
  //       q->nof_symbols,
  //       q->cfg.cp == SRSRAN_CP_NORM ? "Normal" : "Extended",
  //       q->nof_re,
  //       q->nof_guards);

  DEBUG("Init %s symbol_sz=%d, nof_symbols=%d, cp=%s, nof_re=%d, nof_guards=%d",
        dir == SRSRAN_DFT_FORWARD ? "FFT" : "iFFT",
        q->cfg.symbol_sz,
        q->nof_symbols,
        q->cfg.cp == SRSRAN_CP_NORM ? "Normal" : "Extended",
        q->nof_re,
        q->nof_guards);

  // MBSFN logic
  if (sf_type == SRSRAN_SF_MBSFN) {
    q->mbsfn_subframe   = true;
    q->non_mbsfn_region = 2; 
  } else {
    q->mbsfn_subframe = false;
  }

  // Set other parameters
  srsran_ofdm_set_freq_shift(q, q->cfg.freq_shift_f); // skipped because it's only for uplink
  srsran_dft_plan_set_norm(&q->fft_plan, q->cfg.normalize); // q->cfg.normaliza = 0
  srsran_dft_plan_set_dc(&q->fft_plan, (!cfg->keep_dc) && (!isnormal(q->cfg.freq_shift_f))); // is also 0

  // set phase compensation
  if (srsran_ofdm_set_phase_compensation_nrscope(q, cfg->phase_compensation_hz, scs_idx, nof_prb) < SRSRAN_SUCCESS) {
    ERROR("Error setting phase compensation");
    return SRSRAN_ERROR;
  }

  return SRSRAN_SUCCESS;
}


void srsran_ofdm_set_non_mbsfn_region(srsran_ofdm_t* q, uint8_t non_mbsfn_region)
{
  q->non_mbsfn_region = non_mbsfn_region;
}

void srsran_ofdm_free_(srsran_ofdm_t* q)
{
  srsran_dft_plan_free(&q->fft_plan);

#ifndef AVOID_GURU
  for (int slot = 0; slot < 2; slot++) {
    if (q->fft_plan_sf[slot].init_size) {
      srsran_dft_plan_free(&q->fft_plan_sf[slot]);
    }
  }
#endif

  if (q->tmp) {
    free(q->tmp);
  }
  if (q->shift_buffer) {
    free(q->shift_buffer);
  }
  if (q->window_offset_buffer) {
    free(q->window_offset_buffer);
  }
  srsran_cfr_free(&q->tx_cfr);
  SRSRAN_MEM_ZERO(q, srsran_ofdm_t, 1);
}

int srsran_ofdm_rx_init(srsran_ofdm_t* q, srsran_cp_t cp, cf_t* in_buffer, cf_t* out_buffer, uint32_t max_prb)
{
  bzero(q, sizeof(srsran_ofdm_t));

  srsran_ofdm_cfg_t cfg = {};
  cfg.cp                = cp;
  cfg.in_buffer         = in_buffer;
  cfg.out_buffer        = out_buffer;
  cfg.nof_prb           = max_prb;
  cfg.sf_type           = SRSRAN_SF_NORM;

  return ofdm_init_mbsfn_(q, &cfg, SRSRAN_DFT_FORWARD);
}

int srsran_ofdm_rx_init_mbsfn(srsran_ofdm_t* q, srsran_cp_t cp, cf_t* in_buffer, cf_t* out_buffer, uint32_t max_prb)
{
  bzero(q, sizeof(srsran_ofdm_t));

  srsran_ofdm_cfg_t cfg = {};
  cfg.cp                = cp;
  cfg.in_buffer         = in_buffer;
  cfg.out_buffer        = out_buffer;
  cfg.nof_prb           = max_prb;
  cfg.sf_type           = SRSRAN_SF_MBSFN;

  return ofdm_init_mbsfn_(q, &cfg, SRSRAN_DFT_FORWARD);
}

int srsran_ofdm_tx_init(srsran_ofdm_t* q, srsran_cp_t cp, cf_t* in_buffer, cf_t* out_buffer, uint32_t max_prb)
{
  bzero(q, sizeof(srsran_ofdm_t));

  srsran_ofdm_cfg_t cfg = {};
  cfg.cp                = cp;
  cfg.in_buffer         = in_buffer;
  cfg.out_buffer        = out_buffer;
  cfg.nof_prb           = max_prb;
  cfg.sf_type           = SRSRAN_SF_NORM;

  return ofdm_init_mbsfn_(q, &cfg, SRSRAN_DFT_BACKWARD);
}

int srsran_ofdm_tx_init_cfg(srsran_ofdm_t* q, srsran_ofdm_cfg_t* cfg)
{
  if (q == NULL || cfg == NULL) {
    ERROR("Error, invalid inputs");
    return SRSRAN_ERROR_INVALID_INPUTS;
  }
  return ofdm_init_mbsfn_(q, cfg, SRSRAN_DFT_BACKWARD);
}

int srsran_ofdm_rx_init_cfg(srsran_ofdm_t* q, srsran_ofdm_cfg_t* cfg)
{
  return ofdm_init_mbsfn_(q, cfg, SRSRAN_DFT_FORWARD);
}

int srsran_ofdm_rx_init_cfg_nrscope(srsran_ofdm_t* q, srsran_ofdm_cfg_t* cfg, int scs_idx)
{
  switch(scs_idx){
    case 0:
      return ofdm_init_nr_nrscope_15khz(q, cfg, SRSRAN_DFT_FORWARD, scs_idx);
      break;
    case 1:
      return ofdm_init_nr_nrscope_30khz(q, cfg, SRSRAN_DFT_FORWARD, scs_idx);
      break;
    case 2:
      ERROR("Unhandled scs_idx 60khz!");
      return -1;
      break;
    default:
      ERROR("Invalid scs_idx!");
      return -1;
      break;
  }
  
}

int srsran_ofdm_tx_init_mbsfn(srsran_ofdm_t* q, srsran_cp_t cp, cf_t* in_buffer, cf_t* out_buffer, uint32_t nof_prb)
{
  bzero(q, sizeof(srsran_ofdm_t));

  srsran_ofdm_cfg_t cfg = {};
  cfg.cp                = cp;
  cfg.in_buffer         = in_buffer;
  cfg.out_buffer        = out_buffer;
  cfg.nof_prb           = nof_prb;
  cfg.sf_type           = SRSRAN_SF_MBSFN;

  return ofdm_init_mbsfn_(q, &cfg, SRSRAN_DFT_BACKWARD);
}

int srsran_ofdm_rx_set_prb(srsran_ofdm_t* q, srsran_cp_t cp, uint32_t nof_prb)
{
  srsran_ofdm_cfg_t cfg = {};
  cfg.cp                = cp;
  cfg.nof_prb           = nof_prb;
  return ofdm_init_mbsfn_(q, &cfg, SRSRAN_DFT_FORWARD);
}

int srsran_ofdm_tx_set_prb(srsran_ofdm_t* q, srsran_cp_t cp, uint32_t nof_prb)
{
  srsran_ofdm_cfg_t cfg = {};
  cfg.cp                = cp;
  cfg.nof_prb           = nof_prb;
  return ofdm_init_mbsfn_(q, &cfg, SRSRAN_DFT_BACKWARD);
}

int srsran_ofdm_set_phase_compensation(srsran_ofdm_t* q, double center_freq_hz)
{
  // Validate pointer
  if (q == NULL) {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }

  // Check if the center frequency has changed
  if (q->cfg.phase_compensation_hz == center_freq_hz) {
    return SRSRAN_SUCCESS;
  }

  // Save the current phase compensation
  q->cfg.phase_compensation_hz = center_freq_hz;

  // If the center frequency is 0, NAN, INF, then skip
  if (!isnormal(center_freq_hz)) {
    return SRSRAN_SUCCESS;
  }

  // Extract modulation required parameters
  uint32_t symbol_sz = q->cfg.symbol_sz;
  double   scs       = 15e3; //< Assume 15kHz subcarrier spacing
  double   srate_hz  = symbol_sz * scs;
  // printf("symbol_sz: %u\n", symbol_sz);
  // printf("srate_hz: %lf\n", srate_hz);
  // printf("scs: %lf\n", scs);
  // printf("q->nof_symbols: %d\n", q->nof_symbols);

  // Assert parameters
  if (!isnormal(srate_hz)) {
    return SRSRAN_ERROR;
  }

  // Otherwise calculate the phase
  uint32_t count = 0;
  for (uint32_t l = 0; l < q->nof_symbols * SRSRAN_NOF_SLOTS_PER_SF; l++) {
    uint32_t cp_len =
        SRSRAN_CP_ISNORM(q->cfg.cp) ? SRSRAN_CP_LEN_NORM(l % q->nof_symbols, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);
    // Advance CP
    count += cp_len;

    // Calculate symbol start time
    double t_start = (double)count / srate_hz;

    // Calculate phase
    double phase_rad = -2.0 * M_PI * center_freq_hz * t_start;
    // printf("center_freq_hz: %f\n", center_freq_hz);

    // Calculate compensation phase in double precision and then convert to single
    q->phase_compensation[l] = (cf_t)cexp(I * phase_rad);
    // printf("phase_compensation: %f+%fi\n", creal(q->phase_compensation[l]), cimag(q->phase_compensation[l]));

    // Advance symbol
    count += symbol_sz;
  }

  return SRSRAN_SUCCESS;
}

int srsran_ofdm_set_phase_compensation_nrscope(srsran_ofdm_t* q, double center_freq_hz, int scs_idx, uint32_t nof_prbs)
{
  // Validate pointer
  if (q == NULL) {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }

  // Check if the center frequency has changed
  if (q->cfg.phase_compensation_hz == center_freq_hz) {
    return SRSRAN_SUCCESS;
  }

  // Save the current phase compensation
  q->cfg.phase_compensation_hz = center_freq_hz;

  // If the center frequency is 0, NAN, INF, then skip
  if (!isnormal(center_freq_hz)) {
    return SRSRAN_SUCCESS;
  }

  // Extract modulation required parameters
  uint32_t symbol_sz = q->cfg.symbol_sz;
  double   scs       = (double)SRSRAN_SUBC_SPACING_NR(scs_idx); 
  double   srate_hz  = symbol_sz * scs;
  // printf("symbol_sz: %u\n", symbol_sz);
  // printf("srate_hz: %lf\n", srate_hz);
  // printf("scs: %lf\n", scs);

  // Assert parameters
  if (!isnormal(srate_hz)) {
    return SRSRAN_ERROR;
  }

  // Otherwise calculate the phase
  uint32_t count = 0;
  for (uint32_t l = 0; l < q->nof_symbols * SRSRAN_NOF_SLOTS_PER_SF; l++) {
    uint32_t cp_len =
        // SRSRAN_CP_ISNORM(q->cfg.cp) ? SRSRAN_CP_LEN_NORM_NR(symbol_sz) : SRSRAN_CP_LEN_EXT_NR(symbol_sz);
        SRSRAN_CP_ISNORM(q->cfg.cp) ? SRSRAN_CP_LEN_NORM(l % q->nof_symbols, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);
    // printf("cp_len: %d\n", cp_len);
    // printf("center_freq: %f\n", center_freq_hz);
    // Advance CP
    count += cp_len;

    // Calculate symbol start time
    double t_start = (double)count / srate_hz;

    // Calculate phase
    double phase_rad = -2.0 * M_PI * center_freq_hz * t_start;
    // printf("center_freq_hz: %f\n", center_freq_hz);

    // Calculate compensation phase in double precision and then convert to single
    q->phase_compensation[l] = (cf_t)cexp(I * phase_rad);
    // q->phase_compensation[l+q->nof_symbols/2] = (cf_t)cexp(I * phase_rad);
    // printf("phase_compensation: %f+%fi\n", creal(q->phase_compensation[l]), cimag(q->phase_compensation[l]));
    
    // Advance symbol
    count += symbol_sz;
    // printf("count: %u\n", count);
  }

  return SRSRAN_SUCCESS;
}

void srsran_ofdm_rx_free(srsran_ofdm_t* q)
{
  srsran_ofdm_free_(q);
}

/* Shifts the signal after the iFFT or before the FFT.
 * Freq_shift is relative to inter-carrier spacing.
 * Caution: This function shall not be called during run-time
 */
int srsran_ofdm_set_freq_shift(srsran_ofdm_t* q, float freq_shift)
{
  q->cfg.freq_shift_f = freq_shift;

  // Check if fft shift is required
  if (!isnormal(q->cfg.freq_shift_f)) {
    srsran_dft_plan_set_dc(&q->fft_plan, true);
    return SRSRAN_SUCCESS;
  }

  uint32_t    symbol_sz = q->cfg.symbol_sz;
  srsran_cp_t cp        = q->cfg.cp;

  cf_t* ptr = q->shift_buffer;
  for (uint32_t n = 0; n < SRSRAN_NOF_SLOTS_PER_SF; n++) {
    for (uint32_t i = 0; i < q->nof_symbols; i++) {
      uint32_t cplen = SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM(i, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);
      for (uint32_t t = 0; t < symbol_sz + cplen; t++) {
        ptr[t] = cexpf(I * 2 * M_PI * ((float)t - (float)cplen) * freq_shift / symbol_sz);
      }
      ptr += symbol_sz + cplen;
    }
  }

  /* Disable DC carrier addition */
  srsran_dft_plan_set_dc(&q->fft_plan, false);

  return SRSRAN_SUCCESS;
}

void srsran_ofdm_tx_free(srsran_ofdm_t* q)
{
  srsran_ofdm_free_(q);
}

void srsran_ofdm_rx_slot_ng(srsran_ofdm_t* q, cf_t* input, cf_t* output)
{
  uint32_t    symbol_sz = q->cfg.symbol_sz;
  srsran_cp_t cp        = q->cfg.cp;

  for (uint32_t i = 0; i < q->nof_symbols; i++) {
    input += SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM(i, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);
    input -= q->window_offset_n;
    srsran_dft_run_c(&q->fft_plan, input, q->tmp);
    memcpy(output, &q->tmp[q->nof_guards], q->nof_re * sizeof(cf_t));
    input += symbol_sz;
    output += q->nof_re;
  }
}

/* Transforms input samples into output OFDM symbols.
 * Performs FFT on a each symbol and removes CP.
 */
static void ofdm_rx_slot(srsran_ofdm_t* q, int slot_in_sf)
{
#ifdef AVOID_GURU
  srsran_ofdm_rx_slot_ng(
      q, q->cfg.in_buffer + slot_in_sf * q->slot_sz, q->cfg.out_buffer + slot_in_sf * q->nof_re * q->nof_symbols);
#else
  uint32_t nof_symbols = q->nof_symbols;
  uint32_t nof_re = q->nof_re;
  cf_t* output = q->cfg.out_buffer + slot_in_sf * nof_re * nof_symbols;
  // printf("nof_symbols: %d\n", nof_symbols);
  // printf("nof_re: %d\n", nof_re);
  // printf("slot_in_sf * nof_re * nof_symbols: %d\n", slot_in_sf * nof_re * nof_symbols);

  uint32_t symbol_sz = q->cfg.symbol_sz;
  float norm = 1.0f / sqrtf(q->fft_plan.size);
  cf_t* tmp = q->tmp;
  uint32_t dc = (q->fft_plan.dc) ? 1 : 0;

  srsran_dft_run_guru_c(&q->fft_plan_sf[slot_in_sf]);
  // printf("q->nof_symbols: %d\n", q->nof_symbols);
  for (int i = 0; i < q->nof_symbols; i++) {
    // Apply frequency domain window offset
    if (q->window_offset_n) {
      srsran_vec_prod_ccc(tmp, q->window_offset_buffer, tmp, symbol_sz);
    }

    // Perform FFT shift
    memcpy(output, tmp + symbol_sz - nof_re / 2, sizeof(cf_t) * nof_re / 2);
    memcpy(output + nof_re / 2, &tmp[dc], sizeof(cf_t) * nof_re / 2);

    // Normalize output
    if (isnormal(q->cfg.phase_compensation_hz)) {
      // Get phase compensation
      cf_t phase_compensation = conjf(q->phase_compensation[slot_in_sf * q->nof_symbols + i]);

      // Apply normalization
      if (q->fft_plan.norm) {
        phase_compensation *= norm;
      }

      // Apply correction
      srsran_vec_sc_prod_ccc(output, phase_compensation, output, nof_re);
    } else if (q->fft_plan.norm) {
      srsran_vec_sc_prod_cfc(output, norm, output, nof_re);
    }

    tmp += symbol_sz;
    output += nof_re;
  }
#endif
}

// slot_in_sf is always 0 since we input each slot into it
static void ofdm_rx_slot_nrscope_15khz(srsran_ofdm_t* q, int slot_in_sf, int coreset_offset_scs, int scs_idx)
{
#ifdef AVOID_GURU
  srsran_ofdm_rx_slot_ng(
      q, q->cfg.in_buffer + slot_in_sf * q->slot_sz, q->cfg.out_buffer + slot_in_sf * q->nof_re * q->nof_symbols);
#else
  uint32_t nof_symbols = q->nof_symbols;
  uint32_t nof_re = q->nof_re;
  cf_t* output = q->cfg.out_buffer + slot_in_sf * nof_re * nof_symbols;  // time-freq domain: subcarrier x symbol
  // printf("nof_symbols: %d\n", nof_symbols);
  // printf("nof_re: %d\n", nof_re);
  // printf("slot_in_sf * nof_re * nof_symbols: %d\n", slot_in_sf * nof_re * nof_symbols);

  uint32_t symbol_sz = q->cfg.symbol_sz;
  float norm = 1.0f / sqrtf(q->fft_plan.size);
  cf_t* tmp = q->tmp; // where the dft results store
  uint32_t dc = (q->fft_plan.dc) ? 1 : 0;
  // printf("symbol_sz: %d\n", symbol_sz);
  // printf("nof_re: %d\n", nof_re);

  // printf("fft-input:");
  // srsran_vec_fprint_c(stdout, q->cfg.in_buffer, 11520);

  srsran_dft_run_guru_c(&q->fft_plan_sf[slot_in_sf]);

  // printf("fft-output:");
  // srsran_vec_fprint_c(stdout, tmp, (symbol_sz) * 7);

  uint32_t re_count = 0;
  for (int i = 0; i < q->nof_symbols; i++) {
    // Apply frequency domain window offset
    if (q->window_offset_n) {
      srsran_vec_prod_ccc(tmp, q->window_offset_buffer, tmp, symbol_sz);

      // printf("q->window_offset_buffer:");
      // srsran_vec_fprint_c(stdout, q->window_offset_buffer, symbol_sz);
    }

    // Perform FFT shift
    // the position of CORESET 0's center is not on current radio's center frequency
    // coreset_offset_scs = (ssb_center_freq - coreset_center_freq) / scs, all in hz
    memcpy(output, tmp + symbol_sz - (nof_re / 2 + coreset_offset_scs), sizeof(cf_t) * (nof_re / 2 + coreset_offset_scs));
    memcpy(output + (nof_re / 2 + coreset_offset_scs), &tmp[dc], sizeof(cf_t) * (nof_re / 2 - coreset_offset_scs));
    // memcpy(output, tmp + symbol_sz - nof_re / 2, sizeof(cf_t) * nof_re / 2);
    // memcpy(output + nof_re / 2, &tmp[dc], sizeof(cf_t) * nof_re / 2);

    // if(i == 2 || i == 7 || i == 11){
    //   printf("fft-output symbol %d:", i);
    //   srsran_vec_fprint_c(stdout, output, symbol_sz);
    // }

    // Normalize output
    // q->cfg.phase_compensation_hz = 0;
    if (isnormal(q->cfg.phase_compensation_hz)) {
      // Get phase compensation
      cf_t phase_compensation = conjf(q->phase_compensation[slot_in_sf * q->nof_symbols + i]);

      // printf("phase_compensation: %f+%fi\n", creal(phase_compensation), cimag(phase_compensation));

      // Apply normalization
      if (q->fft_plan.norm) {
        phase_compensation *= norm;
      }

      // Apply correction
      srsran_vec_sc_prod_ccc(output, phase_compensation, output, nof_re);
    } else if (q->fft_plan.norm) {
      srsran_vec_sc_prod_cfc(output, norm, output, nof_re);
    }
    // printf("re_idx %u, output: ", re_count);
    // srsran_vec_fprint_c(stdout, output, nof_re);

    // FILE *fp;
    // fp = fopen("SIB_debug.txt", "a");
    // fwrite(output, sizeof(cf_t), nof_re, fp);
    // fclose(fp);

    // if(i == 2 || i == 7 || i == 11){
    //   printf("fft-output symbol %d:", i);
    //   srsran_vec_fprint_c(stdout, output, symbol_sz);
    // }
    
    tmp += symbol_sz;
    output += nof_re;
    re_count += nof_re;
  }
#endif
  // printf("original symbols:");
  // srsran_vec_fprint_c(stdout, &q->cfg.out_buffer[nof_re * 2], nof_re);
  // printf("original symbols:");
  // srsran_vec_fprint_c(stdout, &q->cfg.out_buffer[nof_re * 7], nof_re);
  // printf("original symbols:");
  // srsran_vec_fprint_c(stdout, &q->cfg.out_buffer[nof_re * 11], nof_re);
}

// slot_in_sf is always 0 since we input each slot into it
static void ofdm_rx_slot_nrscope_30khz(srsran_ofdm_t* q, int slot_in_sf, int coreset_offset_scs, int scs_idx)
{
#ifdef AVOID_GURU
  srsran_ofdm_rx_slot_ng(
      q, q->cfg.in_buffer + slot_in_sf * q->slot_sz, q->cfg.out_buffer + slot_in_sf * q->nof_re * q->nof_symbols);
#else
  uint32_t nof_symbols = q->nof_symbols;
  uint32_t nof_re = q->nof_re;
  cf_t* output = q->cfg.out_buffer + slot_in_sf * nof_re * nof_symbols;  // time-freq domain: subcarrier x symbol
  // printf("nof_symbols: %d\n", nof_symbols);
  // printf("nof_re: %d\n", nof_re);
  // printf("slot_in_sf * nof_re * nof_symbols: %d\n", slot_in_sf * q->slot_sz);

  uint32_t symbol_sz = q->cfg.symbol_sz;
  float norm = 1.0f / sqrtf(q->fft_plan.size);
  cf_t* tmp = q->tmp; // where the dft results store
  uint32_t dc = (q->fft_plan.dc) ? 1 : 0;
  // printf("symbol_sz: %d\n", symbol_sz);
  // printf("nof_re: %d\n", nof_re);

  // printf("fft-input:");
  // srsran_vec_fprint_c(stdout, q->cfg.in_buffer, 11520);
  srsran_dft_run_guru_c(&q->fft_plan_sf[slot_in_sf]);
  // printf("fft-output:");
  // srsran_vec_fprint_c(stdout, tmp, (symbol_sz) * 7);
  uint32_t re_count = 0;
  for (int i = 0; i < q->nof_symbols; i++) {
    // Apply frequency domain window offset
    if (q->window_offset_n) {
      srsran_vec_prod_ccc(tmp, q->window_offset_buffer, tmp, symbol_sz);

      // if(scs_idx == 0 && i > 6) {
      //   srsran_vec_prod_ccc(tmp, &q->window_offset_buffer[symbol_sz], tmp, symbol_sz);
      // }
      // printf("q->window_offset_buffer:");
      // srsran_vec_fprint_c(stdout, q->window_offset_buffer, symbol_sz);
    }


    // Perform FFT shift
    // the position of CORESET 0's center is not on current radio's center frequency
    // coreset_offset_scs = (ssb_center_freq - coreset_center_freq) / scs, all in hz
    if ((nof_re / 2 + coreset_offset_scs) > nof_re) {
      memcpy(output, tmp + symbol_sz - (nof_re / 2 + coreset_offset_scs), sizeof(cf_t) * nof_re);
    } else {
      memcpy(output, tmp + symbol_sz - (nof_re / 2 + coreset_offset_scs), sizeof(cf_t) * (nof_re / 2 + coreset_offset_scs));
      memcpy(output + (nof_re / 2 + coreset_offset_scs), &tmp[dc], sizeof(cf_t) * (nof_re / 2 - coreset_offset_scs));
    }
    
    // memcpy(output, tmp + symbol_sz - nof_re / 2, sizeof(cf_t) * nof_re / 2);
    // memcpy(output + nof_re / 2, &tmp[dc], sizeof(cf_t) * nof_re / 2);

    // if(i == 2 || i == 7 || i == 11){
    //   printf("fft-output symbol %d:", i);
    //   srsran_vec_fprint_c(stdout, output, symbol_sz);
    // }

    // Normalize output
    // q->cfg.phase_compensation_hz = 0;
    if (isnormal(q->cfg.phase_compensation_hz)) {
      // Get phase compensation
      cf_t phase_compensation = conjf(q->phase_compensation[slot_in_sf * q->nof_symbols + i]);

      // printf("phase_compensation: %f+%fi\n", creal(phase_compensation), cimag(phase_compensation));

      // Apply normalization
      if (q->fft_plan.norm) {
        phase_compensation *= norm;
      }

      // Apply correction
      srsran_vec_sc_prod_ccc(output, phase_compensation, output, nof_re);
    } else if (q->fft_plan.norm) {
      srsran_vec_sc_prod_cfc(output, norm, output, nof_re);
    }
    // printf("re_idx %u, output: ", re_count);
    // srsran_vec_fprint_c(stdout, output, nof_re);

    // FILE *fp;
    // fp = fopen("SIB_debug.txt", "a");
    // fwrite(output, sizeof(cf_t), nof_re, fp);
    // fclose(fp);

    // if(i == 2 || i == 7 || i == 11){
    //   printf("fft-output symbol %d:", i);
    //   srsran_vec_fprint_c(stdout, output, symbol_sz);
    // }
    
    tmp += symbol_sz;
    output += nof_re;
    re_count += nof_re;
  }

#endif
  // printf("original symbols:");
  // srsran_vec_fprint_c(stdout, &q->cfg.out_buffer[nof_re * 2], nof_re);
  // printf("original symbols:");
  // srsran_vec_fprint_c(stdout, &q->cfg.out_buffer[nof_re * 7], nof_re);
  // printf("original symbols:");
  // srsran_vec_fprint_c(stdout, &q->cfg.out_buffer[nof_re * 11], nof_re);
}

static void ofdm_rx_slot_mbsfn(srsran_ofdm_t* q, cf_t* input, cf_t* output)
{
  uint32_t i;
  for (i = 0; i < q->nof_symbols_mbsfn; i++) {
    if (i == q->non_mbsfn_region) {
      input += SRSRAN_NON_MBSFN_REGION_GUARD_LENGTH(q->non_mbsfn_region, q->cfg.symbol_sz);
    }
    input += (i >= q->non_mbsfn_region) ? SRSRAN_CP_LEN_EXT(q->cfg.symbol_sz) : SRSRAN_CP_LEN_NORM(i, q->cfg.symbol_sz);
    srsran_dft_run_c(&q->fft_plan, input, q->tmp);
    memcpy(output, &q->tmp[q->nof_guards], q->nof_re * sizeof(cf_t));
    input += q->cfg.symbol_sz;
    output += q->nof_re;
  }
}

void srsran_ofdm_rx_slot_zerocopy(srsran_ofdm_t* q, cf_t* input, cf_t* output)
{
  uint32_t i;
  for (i = 0; i < q->nof_symbols; i++) {
    input +=
        SRSRAN_CP_ISNORM(q->cfg.cp) ? SRSRAN_CP_LEN_NORM(i, q->cfg.symbol_sz) : SRSRAN_CP_LEN_EXT(q->cfg.symbol_sz);
    srsran_dft_run_c_zerocopy(&q->fft_plan, input, q->tmp);
    memcpy(output, &q->tmp[q->cfg.symbol_sz / 2 + q->nof_guards], sizeof(cf_t) * q->nof_re / 2);
    memcpy(&output[q->nof_re / 2], &q->tmp[1], sizeof(cf_t) * q->nof_re / 2);
    input += q->cfg.symbol_sz;
    output += q->nof_re;
  }
}

void srsran_ofdm_rx_sf(srsran_ofdm_t* q)
{
  if (isnormal(q->cfg.freq_shift_f)) {
    srsran_vec_prod_ccc(q->cfg.in_buffer, q->shift_buffer, q->cfg.in_buffer, q->sf_sz);
  }
  if (!q->mbsfn_subframe) {
    for (uint32_t n = 0; n < SRSRAN_NOF_SLOTS_PER_SF; n++) {
      ofdm_rx_slot(q, n);
    }
  } else {
    ofdm_rx_slot_mbsfn(q, q->cfg.in_buffer, q->cfg.out_buffer);
    ofdm_rx_slot(q, 1);
  }
}

void srsran_ofdm_rx_sf_nrscope(srsran_ofdm_t* q, int scs_idx, int coreset_offset_scs)
{
  if (isnormal(q->cfg.freq_shift_f)) {
    // printf("shift buffer update\n");
    srsran_vec_prod_ccc(q->cfg.in_buffer, q->shift_buffer, q->cfg.in_buffer, q->sf_sz); // skipped for dl rx
  }

  // we don't need to do the condition
  switch(scs_idx){
    case 0:
      for (uint32_t n = 0; n < 2; n++) {
        ofdm_rx_slot_nrscope_15khz(q, n, coreset_offset_scs, scs_idx);
      }
      break;
    case 1:
      for (uint32_t n = 0; n < 1; n++) {
        ofdm_rx_slot_nrscope_30khz(q, n, coreset_offset_scs, scs_idx);
      }
      break;
    case 2:
      ERROR("Unhandled scs_idx!");
      break;
    default:
      ERROR("Invalid scs_idx!");
      break;
  }
  
}

void srsran_ofdm_rx_sf_ng(srsran_ofdm_t* q, cf_t* input, cf_t* output)
{
  uint32_t n;
  if (isnormal(q->cfg.freq_shift_f)) {
    srsran_vec_prod_ccc(input, q->shift_buffer, input, q->sf_sz);
  }
  if (!q->mbsfn_subframe) {
    for (n = 0; n < SRSRAN_NOF_SLOTS_PER_SF; n++) {
      srsran_ofdm_rx_slot_ng(q, &input[n * q->slot_sz], &output[n * q->nof_re * q->nof_symbols]);
    }
  } else {
    ofdm_rx_slot_mbsfn(q, q->cfg.in_buffer, q->cfg.out_buffer);
    ofdm_rx_slot(q, 1);
  }
}

/* Transforms input OFDM symbols into output samples.
 * Performs the FFT on each symbol and adds CP.
 */
static void ofdm_tx_slot(srsran_ofdm_t* q, int slot_in_sf)
{
  uint32_t    symbol_sz = q->cfg.symbol_sz;
  srsran_cp_t cp        = q->cfg.cp;

  cf_t* input  = q->cfg.in_buffer + slot_in_sf * q->nof_re * q->nof_symbols;
  cf_t* output = q->cfg.out_buffer + slot_in_sf * q->slot_sz;

#ifdef AVOID_GURU
  for (int i = 0; i < q->nof_symbols; i++) {
    int cp_len = SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM(i, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);
    memcpy(&q->tmp[q->nof_guards], input, q->nof_re * sizeof(cf_t));
    srsran_dft_run_c(&q->fft_plan, q->tmp, &output[cp_len]);
    input += q->nof_re;
    /* add CP */
    memcpy(output, &output[symbol_sz], cp_len * sizeof(cf_t));
    output += symbol_sz + cp_len;
  }
#else
  uint32_t nof_symbols = q->nof_symbols;
  uint32_t nof_re = q->nof_re;
  float norm = 1.0f / sqrtf(symbol_sz);
  cf_t* tmp = q->tmp;

  bzero(tmp, q->slot_sz);
  uint32_t dc = (q->fft_plan.dc) ? 1 : 0;

  for (int i = 0; i < nof_symbols; i++) {
    srsran_vec_cf_copy(&tmp[dc], &input[nof_re / 2], nof_re / 2);
    srsran_vec_cf_copy(&tmp[symbol_sz - nof_re / 2], &input[0], nof_re / 2);

    input += nof_re;
    tmp += symbol_sz;
  }

  srsran_dft_run_guru_c(&q->fft_plan_sf[slot_in_sf]);

  for (int i = 0; i < nof_symbols; i++) {
    int cp_len = SRSRAN_CP_ISNORM(cp) ? SRSRAN_CP_LEN_NORM(i, symbol_sz) : SRSRAN_CP_LEN_EXT(symbol_sz);

    if (isnormal(q->cfg.phase_compensation_hz)) {
      // Get phase compensation
      cf_t phase_compensation = q->phase_compensation[slot_in_sf * q->nof_symbols + i];

      // Apply normalization
      if (q->fft_plan.norm) {
        phase_compensation *= norm;
      }

      // Apply correction
      srsran_vec_sc_prod_ccc(&output[cp_len], phase_compensation, &output[cp_len], symbol_sz);
    } else if (q->fft_plan.norm) {
      srsran_vec_sc_prod_cfc(&output[cp_len], norm, &output[cp_len], symbol_sz);
    }

    // CFR: Process the time-domain signal without the CP
    if (q->cfg.cfr_tx_cfg.cfr_enable) {
      srsran_cfr_process(&q->tx_cfr, output + cp_len, output + cp_len);
    }

    /* add CP */
    srsran_vec_cf_copy(output, &output[symbol_sz], cp_len);
    output += symbol_sz + cp_len;
  }
#endif
}

void ofdm_tx_slot_mbsfn(srsran_ofdm_t* q, cf_t* input, cf_t* output)
{
  uint32_t symbol_sz = q->cfg.symbol_sz;

  for (uint32_t i = 0; i < q->nof_symbols_mbsfn; i++) {
    int cp_len = (i > (q->non_mbsfn_region - 1)) ? SRSRAN_CP_LEN_EXT(symbol_sz) : SRSRAN_CP_LEN_NORM(i, symbol_sz);
    memcpy(&q->tmp[q->nof_guards], input, q->nof_re * sizeof(cf_t));
    srsran_dft_run_c(&q->fft_plan, q->tmp, &output[cp_len]);
    input += q->nof_re;
    /* add CP */
    memcpy(output, &output[symbol_sz], cp_len * sizeof(cf_t));
    output += symbol_sz + cp_len;

    /*skip the small section between the non mbms region and the mbms region*/
    if (i == (q->non_mbsfn_region - 1))
      output += SRSRAN_NON_MBSFN_REGION_GUARD_LENGTH(q->non_mbsfn_region, symbol_sz);
  }
}

void srsran_ofdm_set_normalize(srsran_ofdm_t* q, bool normalize_enable)
{
  srsran_dft_plan_set_norm(&q->fft_plan, normalize_enable);
}

void srsran_ofdm_tx_sf(srsran_ofdm_t* q)
{
  uint32_t n;
  if (!q->mbsfn_subframe) {
    for (n = 0; n < SRSRAN_NOF_SLOTS_PER_SF; n++) {
      ofdm_tx_slot(q, n);
    }
  } else {
    ofdm_tx_slot_mbsfn(q, q->cfg.in_buffer, q->cfg.out_buffer);
    ofdm_tx_slot(q, 1);
  }
  if (isnormal(q->cfg.freq_shift_f)) {
    srsran_vec_prod_ccc(q->cfg.out_buffer, q->shift_buffer, q->cfg.out_buffer, q->sf_sz);
  }
}

int srsran_ofdm_set_cfr(srsran_ofdm_t* q, srsran_cfr_cfg_t* cfr)
{
  if (q == NULL || cfr == NULL) {
    ERROR("Error, invalid inputs");
    return SRSRAN_ERROR_INVALID_INPUTS;
  }
  if (!q->max_prb) {
    ERROR("Error, ofdm object not initialised");
    return SRSRAN_ERROR;
  }
  // Check if there is nothing to configure
  if (memcmp(&q->cfg.cfr_tx_cfg, cfr, sizeof(srsran_cfr_cfg_t)) == 0) {
    return SRSRAN_SUCCESS;
  }

  // Copy the CFR config into the OFDM object
  q->cfg.cfr_tx_cfg = *cfr;

  // Set the CFR parameters related to OFDM symbol and FFT size
  q->cfg.cfr_tx_cfg.symbol_sz = q->cfg.symbol_sz;
  q->cfg.cfr_tx_cfg.symbol_bw = q->nof_re;

  // in the LTE DL, the DC carrier is empty but still counts when designing the filter BW
  // in the LTE UL, the DC carrier is used
  q->cfg.cfr_tx_cfg.dc_sc = (!q->cfg.keep_dc) && (!isnormal(q->cfg.freq_shift_f));
  if (q->cfg.cfr_tx_cfg.cfr_enable) {
    if (srsran_cfr_init(&q->tx_cfr, &q->cfg.cfr_tx_cfg) < SRSRAN_SUCCESS) {
      ERROR("Error while initialising CFR module");
      return SRSRAN_ERROR;
    }
  }

  return SRSRAN_SUCCESS;
}