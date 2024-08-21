#ifndef RADIO_H
#define RADIO_H

#include "cstdio"
#include "memory"

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/rach_decoder.h"
#include "nrscope/hdr/sibs_decoder.h"
#include "nrscope/hdr/dci_decoder.h"
#include "nrscope/hdr/harq_tracking.h"
#include "nrscope/hdr/task_scheduler.h"
#include "nrscope/hdr/nrscope_logger.h"
#include "nrscope/hdr/to_google.h"

#include <semaphore>
#include <liquid/liquid.h>
#include <chrono>

#define RESAMPLE_WORKER_NUM 4

class Radio{
  public:
    int                                           rf_index;
    srsran::rf_args_t                             rf_args;
    std::shared_ptr<srsran::radio>                raido_shared;
    std::shared_ptr<srsran::radio_interface_phy>  radio;

    srslog::basic_logger&                         logger;

    srsran::rf_buffer_t                           rf_buffer_t;
    cf_t*                                         rx_buffer;
    cf_t*                                         pre_resampling_rx_buffer;
    uint32_t                                      slot_sz;
    uint32_t                                      pre_resampling_slot_sz;
    srsran::rf_timestamp_t                        last_rx_time;

    cell_searcher_args_t                          args_t;
    srsue::phy_nr_sa::cell_search_args_t          cs_args;
    srsran_subcarrier_spacing_t                   ssb_scs;
    srsran::srsran_band_helper                    bands;
    srsran_ue_dl_nr_sratescs_info                 arg_scs;

    srsue::nr::cell_search                        srsran_searcher; // from cell_search.cc
    srsue::nr::cell_search::cfg_t                 srsran_searcher_cfg_t;
    srsue::nr::cell_search::args_t                srsran_searcher_args_t;
    srsue::nr::cell_search::ret_t                 cs_ret;
    uint32_t                                      nof_trials;
    uint32_t                                      nof_trials_scan;
    cell_search_result_t                          cell;

    coreset0_args                                 coreset0_args_t;
    srsran_coreset_t                              coreset0_t;
    srsran_ue_sync_nr_args_t                      ue_sync_nr_args;
    srsran_ue_sync_nr_cfg_t                       sync_cfg;

    srsue::nr::slot_sync                          slot_synchronizer;
    srsran_ue_sync_nr_t                           ue_sync_nr;
    srsran_ue_sync_nr_outcome_t                   outcome;
    
    srsran_ssb_cfg_t ssb_cfg;

    TaskSchedulerNRScope task_scheduler_nrscope;
    RachDecoder rach_decoder; // processing for uplink in rach
    SIBsDecoder sibs_decoder;
    HarqTracker harq_tracker;

    uint32_t nof_threads;
    uint32_t nof_rnti_worker_groups;
    uint8_t nof_bwps;
    std::vector<std::unique_ptr <DCIDecoder> > dci_decoders;

    // a better coordination between producer (fetch) and consumer (resample and decode)
    sem_t smph_sf_data_prod_cons;

    bool resample_needed;
    resampler_kit rk[RESAMPLE_WORKER_NUM];
    bool rk_initialized = false;

    std::string log_name;
    bool local_log;

    bool to_google;
    std::string google_credential;
    // std::string google_project_id;
    std::string google_dataset_id;

    Radio();  //constructor
    ~Radio(); //deconstructor

    /**
    * An entry to this class -- start the radio capture thread, and decode SIB, 
    * RACH and DCI inside the capture loop.
    * 
    * @return SRSRAN_SUCCESS - 0 for successfuly exit
    */
    int RadioThread();

    /**
    * Another entry to this class -- start the NR SA FR1 scan thread 
    * and output found cell(s) 
    * 
    * @return SRSRAN_SUCCESS - 0 for successfuly exit
    */
    int ScanThread();

    /**
    * This function first sets up some parameters related to the radio sample caputure according to the config file, 
    * such as sampling frequency, SSB frequency and SCS. Then it will search the MIB within the range of 
    * [SSB frequency - 0.7 * sampling frequency / 2, SSB frequency + 0.7 * sampling frequency / 2].
    *   (1) If a cell is found, this functions notifies the parameters to task_scheduler_nrscope and start decoding 
    *     SIB, RACH and DCIs.
    *   (2) If no cell is found, it will return and the thread for this USRP ends.
    * 
    * @return SRSRAN_SUCCESS (0) if no cell is found. NR_FAILURE (-1) if something is wrong in the function.
    */
    int RadioInitandStart();

    /**
    * This function goes through the per band (denoted by outer loop), and search each GSCN raster point in the band
    * (denoted by inner loop)
    * 
    * @return SRSRAN_SUCCESS (0) if no cell is found. NR_FAILURE (-1) if something is wrong in the function.
    */
    int ScanInitandStart();

    /**
    * After finding the cell and decoding the cell and synchronization signal, this function sets up the parameters
    * related to downlink synchronization, in terms of mitigating the CFO and time adjustment.
    * 
    * @return SRSRAN_SUCCESS (0) these parameters are successfuly set. 
    * SRSRAN_ERROR (-1) if something goes wrong.
    */
    int SyncandDownlinkInit();

    /**
    * (TASKS HAVE BEEN DELEGATED TO FetchAndResample AND DecodeAndProcess) 
    * After MIB decoding and synchronization, the USRP grabs 1ms data every time and dispatches the raw radio 
    * samples among SIB, RACH and DCI decoding threads. Also initialize these threads if they are not.
    * 
    * @return SRSRAN_SUCCESS (0) if the function is stopped or it will run infinitely. 
    * NR_FAILURE (-1) if something goes wrong.
    */
    int RadioCapture();
  
  private:
    /**
    * sync, track sync, and grab 1ms raw samples from USRP continuously 
    * 
    * @return SRSRAN_SUCCESS (0) if the function is stopped or it will run infinitely. 
    * NR_FAILURE (-1) if something goes wrong.
    */
    int FetchAndResample();

    /**
    * resample and decode the signals
    * 
    * @return SRSRAN_SUCCESS (0) if the function is stopped or it will run infinitely. 
    * NR_FAILURE (-1) if something goes wrong.
    */
    int DecodeAndProcess();
};

#endif