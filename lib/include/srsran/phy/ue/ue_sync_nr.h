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

#ifndef SRSRAN_UE_SYNC_NR_H
#define SRSRAN_UE_SYNC_NR_H

#include "srsran/phy/common/timestamp.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/phy/agc/agc.h"

#include <stdio.h>
#include <pthread.h>
#include <liquid/liquid.h>

#define SRSRAN_RECV_CALLBACK_TEMPLATE(NAME) int (*NAME)(void*, cf_t**, uint32_t, srsran_timestamp_t*)

/**
 * @brief Describes NR UE synchronization object internal states
 */
typedef enum SRSRAN_API {
  SRSRAN_UE_SYNC_NR_STATE_IDLE = 0, ///< Initial state, the object has no configuration
  SRSRAN_UE_SYNC_NR_STATE_FIND,     ///< State just after configuring, baseband is not aligned
  SRSRAN_UE_SYNC_NR_STATE_TRACK     ///< Baseband is aligned with subframes
} srsran_ue_sync_nr_state_t;

/**
 * @brief Describes a UE sync NR object arguments
 */
typedef struct SRSRAN_API {
  // Memory allocation constraints
  double                      max_srate_hz;    ///< Maximum sampling rate in Hz, set to zero to use default
  srsran_subcarrier_spacing_t min_scs;         ///< Minimum subcarrier spacing
  uint32_t                    nof_rx_channels; ///< Number of receive channels, set to 0 for 1

  // Enable/Disable features
  bool disable_cfo; ///< Set to true for disabling the CFO compensation close loop

  // Signal detection thresholds and averaging coefficients
  float pbch_dmrs_thr; ///< NR-PBCH DMRS threshold for blind decoding, set to 0 for default
  float cfo_alpha;     ///< Exponential Moving Average (EMA) alpha coefficient for CFO

  // Receive callback
  void* recv_obj;                               ///< Receive object
  SRSRAN_RECV_CALLBACK_TEMPLATE(recv_callback); ///< Receive callback
} srsran_ue_sync_nr_args_t;

/**
 * @brief Describes a UE sync NR object configuration
 */
typedef struct SRSRAN_API {
  srsran_ssb_cfg_t ssb;  ///< SSB configuration
  uint32_t         N_id; ///< Physical cell identifier
} srsran_ue_sync_nr_cfg_t;

/**
 * @brief  Describes a UE sync NR object
 */
typedef struct SRSRAN_API {
  // State
  srsran_ue_sync_nr_state_t     state;                 ///< Internal state
  int32_t                       next_rf_sample_offset; ///< Next RF sample offset
  uint32_t                      ssb_idx;               ///< Tracking SSB candidate index
  uint32_t                      sf_idx;                ///< Current subframe index (0-9)
  uint32_t                      sfn;                   ///< Current system frame number (0-1023)
  srsran_csi_trs_measurements_t feedback;              ///< Feedback measurements

  // Components
  srsran_ssb_t ssb;        ///< SSB internal object
  cf_t**       tmp_buffer; ///< Temporal buffer pointers

  // Initialised arguments
  uint32_t nof_rx_channels;                     ///< Number of receive channels
  bool     disable_cfo;                         ///< Set to true for disabling the CFO compensation close loop
  float    cfo_alpha;                           ///< Exponential Moving Average (EMA) alpha coefficient for CFO
  void*    recv_obj;                            ///< Receive object
  SRSRAN_RECV_CALLBACK_TEMPLATE(recv_callback); ///< Receive callback

  // Current configuration
  uint32_t N_id;     ///< Current physical cell identifier
  double   srate_hz; ///< Current sampling rate in Hz
  uint32_t sf_sz;    ///< Current subframe size

  // Metrics
  float cfo_hz;       ///< Current CFO in Hz
  float avg_delay_us; ///< Current average delay

  float resample_ratio;

  // AGC
  srsran_agc_t agc;
  bool         do_agc;
} srsran_ue_sync_nr_t;

typedef struct SRSRAN_API {
  msresamp_crcf resampler;
  cf_t * temp_y; // resampler will save result to temp_y; then copy temp_y result to place you want
} resampler_kit;

typedef struct SRSRAN_API {
  resampler_kit *rk; // resampler kit
  cf_t *in; // in buffer (no shift)
  uint32_t splitted_nx; // resample amount for each worker
  uint32_t worker_idx; // worker id (to determine which range)
  uint32_t * actual_sf_sz_splitted; // resampled point num
} resample_partially_args_nrscope;

/**
 * @brief Describes a UE sync NR zerocopy outcome
 */
typedef struct SRSRAN_API {
  bool               in_sync;   ///< Indicates whether the received baseband is synchronized
  uint32_t           sf_idx;    ///< Subframe index
  uint32_t           sfn;       ///< System Frame Number
  srsran_timestamp_t timestamp; ///< Last received timestamp
  float              cfo_hz;    ///< Current CFO in Hz
  float              delay_us;  ///< Current average delay in microseconds
} srsran_ue_sync_nr_outcome_t;

SRSRAN_API int prepare_resampler(resampler_kit * q, float resample_ratio, uint32_t pre_resample_sf_sz, uint32_t resample_worker_num);

/**
 * @brief Initialises a UE sync NR object
 * @param q NR UE synchronization object
 * @param[in] args NR UE synchronization initialization arguments
 * @return SRSRAN_SUCCESS if no error occurs, SRSRAN_ERROR code otherwise
 */
SRSRAN_API int srsran_ue_sync_nr_init(srsran_ue_sync_nr_t* q, const srsran_ue_sync_nr_args_t* args);

/**
 * @brief Deallocate an NR UE synchronization object
 * @param q NR UE synchronization object
 */
SRSRAN_API void srsran_ue_sync_nr_free(srsran_ue_sync_nr_t* q);

/**
 * @brief Configures a UE sync NR object
 * @param q NR UE synchronization object
 * @param[in] cfg NR UE synchronization configuration
 * @return SRSRAN_SUCCESS if no error occurs, SRSRAN_ERROR code otherwise
 */
SRSRAN_API int srsran_ue_sync_nr_set_cfg(srsran_ue_sync_nr_t* q, const srsran_ue_sync_nr_cfg_t* cfg);

/**
 * @brief Runs the NR UE synchronization object, tries to find and track the configured SSB leaving in buffer the
 * received baseband subframe
 * @param q NR UE synchronization object
 * @param buffer 2D complex buffer
 * @param outcome zerocopy outcome
 * @return SRSRAN_SUCCESS if no error occurs, SRSRAN_ERROR code otherwise
 */
SRSRAN_API int srsran_ue_sync_nr_zerocopy(srsran_ue_sync_nr_t* q, cf_t** buffer, srsran_ue_sync_nr_outcome_t* outcome);

/**
 * @brief Resample only part of the raw signals (thus should be collectively used by multiple threads)
 */
SRSRAN_API void *resample_partially_nrscope(void * args);

/**
 * @brief Runs the NR UE synchronization object, tries to find and track the configured SSB leaving in buffer the
 * received baseband subframe
 * @param q NR UE synchronization object
 * @param buffer 2D complex buffer
 * @param outcome zerocopy outcome
 * @param rk resampler
 * @return SRSRAN_SUCCESS if no error occurs, SRSRAN_ERROR code otherwise
 */
SRSRAN_API int srsran_ue_sync_nr_zerocopy_twinrx_nrscope(srsran_ue_sync_nr_t* q, cf_t** buffer, srsran_ue_sync_nr_outcome_t* outcome, resampler_kit * rk, bool resample_needed, uint32_t resample_worker_num);

/**
 * @brief Runs the NR UE synchronization object, tries to find and track the configured SSB leaving in buffer the
 * received baseband subframe
 * @param q NR UE synchronization object
 * @param buffer 2D complex buffer
 * @param outcome zerocopy outcome
 * @param uplink_buffer split the radio buffer into another copy for uplink detection
 * @return SRSRAN_SUCCESS if no error occurs, SRSRAN_ERROR code otherwise
 */
SRSRAN_API int srsran_ue_sync_nr_zerocopy_nrscope(srsran_ue_sync_nr_t* q, 
                                                 cf_t** buffer, 
                                                 srsran_ue_sync_nr_outcome_t* outcome, 
                                                 cf_t* uplink_buffer);

/**
 * @brief Feedback Channel State Information from Tracking Reference Signals into a UE synchronization object
 * @param q NR UE synchronization object
 * @param measurements CSI-TRS feedback measurement
 * @return SRSRAN_SUCCESS if no error occurs, SRSRAN_ERROR code otherwise
 */
SRSRAN_API int srsran_ue_sync_nr_feedback(srsran_ue_sync_nr_t* q, const srsran_csi_trs_measurements_t* measurements);

SRSRAN_API int srsran_ue_sync_nr_start_agc(srsran_ue_sync_nr_t* q,
                                SRSRAN_AGC_CALLBACK(set_gain_callback),
                                float init_gain_value,
                                float min_rx_gain,
                                float max_rx_gain);

#endif // SRSRAN_UE_SYNC_NR_H
