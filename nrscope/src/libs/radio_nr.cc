#include "nrscope/hdr/radio_nr.h"
#include <liquid/liquid.h>
#include <semaphore>
#include <chrono>

#define RING_BUF_SIZE 10000
#define RING_BUF_MODULUS (RING_BUF_SIZE - 1)

Radio::Radio() : 
  logger(srslog::fetch_basic_logger("PHY")), 
  srsran_searcher(logger),
  rf_buffer_t(1),
  task_scheduler_nrscope()
{
  raido_shared = std::make_shared<srsran::radio>();
  radio = nullptr;

  nof_trials = 100;
  nof_trials_scan = 500;
  sf_round = 0;
  srsran_searcher_args_t.max_srate_hz = 92.16e6;
  srsran_searcher_args_t.ssb_min_scs = srsran_subcarrier_spacing_15kHz;
  srsran_searcher.init(srsran_searcher_args_t);

  ssb_cfg = {};
  ue_sync_nr = {};
  outcome = {};
  ue_sync_nr_args = {};
  sync_cfg = {};

  sem_init(&smph_sf_data_prod_cons, 0, 0); 
}

Radio::~Radio() {
}

int Radio::RadioThread(){
  RadioInitandStart();
  return SRSRAN_SUCCESS;
}

int Radio::ScanThread(){
  ScanInitandStart();
  return SRSRAN_SUCCESS;
}

static int resample_partially(msresamp_crcf * q, 
                              std::complex<float> *in, 
                              std::complex<float> *temp_out, 
                              uint8_t worker_id, 
                              uint32_t splitted_nx, 
                              uint32_t * actual_size) {
  msresamp_crcf_execute(*q, (in + splitted_nx * worker_id), 
    splitted_nx, temp_out, actual_size);
  return 0;
}

static int copy_c_to_cpp_complex_arr_and_zero_padding(cf_t* src, 
                                                      std::complex<float>* dst, 
                                                      uint32_t sz1, 
                                                      uint32_t sz2) {
  for (uint32_t i = 0; i < sz2; i++) {
    /* indeed copy right? 
    https://en.cppreference.com/w/cpp/numeric/complex/operator%3D */
    dst[i] = i < sz1 ? src[i] : 0;
  }

  return 0;
}

static int copy_cpp_to_c_complex_arr(std::complex<float>* src, 
                                     cf_t* dst, 
                                     uint32_t sz) {
  for (uint32_t i = 0; i < sz; i++) {
    // https://en.cppreference.com/w/cpp/numeric/complex 
    dst[i] = { src[i].real(), src[i].imag() };
  }

  return 0;
}

int Radio::ScanInitandStart(){

  srsran_assert(raido_shared->init(rf_args, nullptr) == 
    SRSRAN_SUCCESS, "Failed Radio initialisation");
  radio = std::move(raido_shared);

  // Static cell searcher parameters  
  args_t.srate_hz = rf_args.srsran_srate_hz;
  rf_args.dl_freq = args_t.base_carrier.dl_center_frequency_hz;
  args_t.rf_device_name = rf_args.device_name;
  args_t.rf_device_args = rf_args.device_args;
  args_t.rf_log_level = "info";
  args_t.rf_rx_gain_dB = rf_args.rx_gain;
  args_t.rf_freq_offset_Hz = rf_args.freq_offset;
  args_t.phy_log_level   = "warning";
  args_t.stack_log_level = "warning";
  args_t.duration_ms = 1000;

  double ssb_bw_hz;
  double ssb_center_freq_min_hz;
  double ssb_center_freq_max_hz;

  /* Store the double args_t.base_carrier.dl_center_frequency_hz and 
  cs_args.center_freq_hz in a mirror int version for precise diff calculation*/
  long long dl_center_frequency_hz_int_ver;
  long long cs_args_ssb_freq_hz_int_ver;

  uint32_t gscn_low;
  uint32_t gscn_high;
  uint32_t gscn_step;

  radio->set_rx_srate(rf_args.srate_hz);

  if (fabs(rf_args.srsran_srate_hz - rf_args.srate_hz) < 0.1) {
    resample_needed = false;
  } else {
    resample_needed = true;
  }
  std::cout << "resample_needed: " << resample_needed << std::endl;
  
  radio->set_rx_gain(rf_args.rx_gain);
  std::cout << "Initialized radio; start cell scanning" << std::endl;

  pre_resampling_slot_sz = (uint32_t)(rf_args.srate_hz / 1000.0f / 
    SRSRAN_NOF_SLOTS_PER_SF_NR(ssb_scs));
  std::cout << "pre_resampling_slot_sz: " << pre_resampling_slot_sz 
  << std::endl;
  slot_sz = (uint32_t)(rf_args.srsran_srate_hz / 1000.0f / 
    SRSRAN_NOF_SLOTS_PER_SF_NR(ssb_scs));
  std::cout << "slot_sz: " << slot_sz << std::endl;
  // Allocate receive buffer
  rx_buffer = srsran_vec_cf_malloc(SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs) *
    pre_resampling_slot_sz);
  srsran_vec_zero(rx_buffer, SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs) *
  pre_resampling_slot_sz * sizeof(cf_t));
  /* the actual slot size after resampling */
  uint32_t actual_slot_szs[CS_RESAMPLE_WORKER_NUM]; 

  // Allocate pre-resampling receive buffer
  pre_resampling_rx_buffer = srsran_vec_cf_malloc(
      SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs) * pre_resampling_slot_sz);
  srsran_vec_zero(pre_resampling_rx_buffer, 
    SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs) * pre_resampling_slot_sz * 
    sizeof(cf_t));

  // initialize resampling tool
  // resampling rate (output/input)
  float r = (float)rf_args.srsran_srate_hz/(float)rf_args.srate_hz;
  // resampling filter stop-band attenuation [dB]    
  float As=60.0f;
  msresamp_crcf q[CS_RESAMPLE_WORKER_NUM];
  uint32_t temp_x_sz;
  uint32_t temp_y_sz;
  std::complex<float> * temp_x;
  std::complex<float> * temp_y[CS_RESAMPLE_WORKER_NUM];
  if (resample_needed) {
    for (uint8_t i = 0; i < CS_RESAMPLE_WORKER_NUM; i++) {
      q[i] = msresamp_crcf_create(r,As);
    }

    float delay = resample_needed ? msresamp_crcf_get_delay(q[0]) : 0;
    // add a few zero padding
    temp_x_sz = SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs) * 
      pre_resampling_slot_sz + (int)ceilf(delay) + 10;
    temp_x = (std::complex<float> *)malloc(temp_x_sz * 
      sizeof(std::complex<float>));

    temp_y_sz = (uint32_t)(temp_x_sz * r * 2);
    for (uint8_t i = 0; i < CS_RESAMPLE_WORKER_NUM; i++) {
      temp_y[i] = (std::complex<float> *)malloc(temp_y_sz * 
        sizeof(std::complex<float>));
    }
  }

  // Traverse GSCN per band
  for (const srsran_band_helper::nr_band_ss_raster& ss_raster : 
    srsran_band_helper::nr_band_ss_raster_table) {
    std::cout << "Start scaning band " << ss_raster.band 
      << " with scs idx " << ss_raster.scs << std::endl;
    std::cout << "gscn " << ss_raster.gscn_first << " to gscn " 
      << ss_raster.gscn_last << std::endl;

    // adjust the RF's central meas freq to the first GSCN point of the band
    cs_args_ssb_freq_hz_int_ver = 
      (long long)srsran_band_helper::get_freq_from_gscn(ss_raster.gscn_first);
    dl_center_frequency_hz_int_ver = cs_args_ssb_freq_hz_int_ver;
    rf_args.dl_freq = cs_args_ssb_freq_hz_int_ver;
    args_t.base_carrier.dl_center_frequency_hz = rf_args.dl_freq;
    cs_args.center_freq_hz = args_t.base_carrier.dl_center_frequency_hz;
    cs_args.ssb_freq_hz = args_t.base_carrier.dl_center_frequency_hz;
    
    gscn_low = ss_raster.gscn_first;
    gscn_high = ss_raster.gscn_last;
    gscn_step = ss_raster.gscn_step;

    // Set ssb scs relevant logics
    ssb_scs = ss_raster.scs;
    args_t.set_ssb_from_band(ssb_scs);
    args_t.base_carrier.scs = args_t.ssb_scs;
    if(args_t.duplex_mode == SRSRAN_DUPLEX_MODE_TDD){
      args_t.base_carrier.ul_center_frequency_hz = 
        args_t.base_carrier.dl_center_frequency_hz;
    }

    cs_args.ssb_scs = args_t.ssb_scs;
    cs_args.ssb_pattern = args_t.ssb_pattern;
    cs_args.duplex_mode = args_t.duplex_mode;
    uint32_t ssb_scs_hz = SRSRAN_SUBC_SPACING_NR(cs_args.ssb_scs);

    // calculate the bandpass 
    std::cout << "Update RF's meas central freq to " 
      << cs_args.ssb_freq_hz << std::endl;
    ssb_bw_hz = SRSRAN_SSB_BW_SUBC * SRSRAN_SUBC_SPACING_NR(cs_args.ssb_scs);
    ssb_center_freq_min_hz = args_t.base_carrier.dl_center_frequency_hz - 
      (args_t.srate_hz * 0.7 - ssb_bw_hz) / 2.0;
    ssb_center_freq_max_hz = args_t.base_carrier.dl_center_frequency_hz + 
      (args_t.srate_hz * 0.7 - ssb_bw_hz) / 2.0;
    std::cout << "Update min ssb center detect boundary to " 
      << ssb_center_freq_min_hz << std::endl;
    std::cout << "Update max ssb center detect boundary to " 
      << ssb_center_freq_max_hz << std::endl;

    // Set RF
    radio->release_freq(0);
    radio->set_rx_freq(0, (double)rf_args.dl_freq);
    
    // reset the cell searcher params
    srsran_searcher.init(srsran_searcher_args_t);

    // Traverse possible SSB freq in the band
    for(uint32_t gscn = gscn_low; gscn <= gscn_high; gscn = gscn + gscn_step){

      std::cout << "Start scaning GSCN number " << gscn << std::endl;
      // Get SSB center frequency for this GSCN point
      cs_args_ssb_freq_hz_int_ver = 
        (long long)srsran_band_helper::get_freq_from_gscn(gscn);
      cs_args.ssb_freq_hz = cs_args_ssb_freq_hz_int_ver;
      std::cout << "Absolute freq " << cs_args.ssb_freq_hz << std::endl; 

      if (cs_args.ssb_freq_hz == 0) {
        std::cout << "Invalid GSCN to SSB freq? GSCN " << gscn << std::endl;
        continue;
      }

      bool offset_not_scs_aligned = false;
      bool not_in_bandpass_range = false;

      /* Calculate frequency offset between the base-band center frequency 
      and the SSB absolute frequency */
      long long offset_hz = std::abs(cs_args_ssb_freq_hz_int_ver - 
        dl_center_frequency_hz_int_ver);
       
      if (offset_hz % ssb_scs_hz != 0) {
        std::cout << "the offset " << offset_hz << 
          " is NOT multiple of the subcarrier spacing " << ssb_scs_hz << std::endl;
        offset_not_scs_aligned = true;
      }

      // The SSB absolute frequency is invalid if it is outside the bandpass range 
      if ((cs_args.ssb_freq_hz < ssb_center_freq_min_hz) or (cs_args.ssb_freq_hz > ssb_center_freq_max_hz)) {
        std::cout << "SSB freq not in RF bandpass range" << std::endl;
        not_in_bandpass_range = true;
      }

      if (offset_not_scs_aligned || not_in_bandpass_range) {
        // update and measure
        std::cout << "Update RF's meas central freq to " 
          << cs_args.ssb_freq_hz << std::endl;
        ssb_bw_hz = SRSRAN_SSB_BW_SUBC * SRSRAN_SUBC_SPACING_NR(cs_args.ssb_scs);
        ssb_center_freq_min_hz = args_t.base_carrier.dl_center_frequency_hz - 
          (args_t.srate_hz * 0.7 - ssb_bw_hz) / 2.0;
        ssb_center_freq_max_hz = args_t.base_carrier.dl_center_frequency_hz + 
          (args_t.srate_hz * 0.7 - ssb_bw_hz) / 2.0;
        std::cout << "Update min ssb center detect boundary to " << 
          ssb_center_freq_min_hz << std::endl;
        std::cout << "Update max ssb center detect boundary to " << 
          ssb_center_freq_max_hz << std::endl;

        rf_args.dl_freq = cs_args.ssb_freq_hz;
        args_t.base_carrier.dl_center_frequency_hz = rf_args.dl_freq;
        dl_center_frequency_hz_int_ver = cs_args_ssb_freq_hz_int_ver;
        // Set RF
        radio->release_freq(0);
        radio->set_rx_freq(0, (double)rf_args.dl_freq);
      }

      srsran_searcher_cfg_t.srate_hz = args_t.srate_hz;
      /* Currently looks like there is some coarse correlation issue
        that the next several GSCN can possibly detect the same ssb
        Just need to add some "deduplicate" logic when using the scanned cell info
        TO-DO: maybe fix this at a later point */
      srsran_searcher_cfg_t.center_freq_hz = cs_args.ssb_freq_hz;
      srsran_searcher_cfg_t.ssb_freq_hz = cs_args.ssb_freq_hz;
      srsran_searcher_cfg_t.ssb_scs = args_t.ssb_scs;
      srsran_searcher_cfg_t.ssb_pattern = args_t.ssb_pattern;
      srsran_searcher_cfg_t.duplex_mode = args_t.duplex_mode;
      if (not srsran_searcher.start(srsran_searcher_cfg_t)) {
        std::cout << "Searcher: failed to start cell search" << std::endl;
        return NR_FAILURE;
      }

      // Set the searching frequency to ssb_freq
      // Because the srsRAN implementation use the center_freq_hz for cell searching
      cs_args.center_freq_hz = cs_args.ssb_freq_hz;
      args_t.base_carrier.ssb_center_freq_hz = cs_args.ssb_freq_hz;

      srsran::rf_buffer_t rf_buffer = {};
      rf_buffer.set_nof_samples(pre_resampling_slot_sz);
      rf_buffer.set(0, pre_resampling_rx_buffer);

      for(uint32_t trial=0; trial < nof_trials_scan; trial++){
        if (trial == 0) {
          srsran_vec_cf_zero(rx_buffer, pre_resampling_slot_sz);
          srsran_vec_cf_zero(pre_resampling_rx_buffer, pre_resampling_slot_sz);
        }

        srsran::rf_timestamp_t& rf_timestamp = last_rx_time;

        if (not radio->rx_now(rf_buffer, rf_timestamp)) {
          return SRSRAN_ERROR;
        }

        if (resample_needed) {
          copy_c_to_cpp_complex_arr_and_zero_padding(pre_resampling_rx_buffer, 
            temp_x, pre_resampling_slot_sz, temp_x_sz);
          uint32_t splitted_nx = pre_resampling_slot_sz / CS_RESAMPLE_WORKER_NUM;
          std::vector <std::thread> ssb_scan_resample_threads;
          for (uint8_t k = 0; k < CS_RESAMPLE_WORKER_NUM; k++) {
            ssb_scan_resample_threads.emplace_back(&resample_partially, &q[k], 
              temp_x, temp_y[k], k, splitted_nx, &actual_slot_szs[k]);
          }
          for (uint8_t k = 0; k < CS_RESAMPLE_WORKER_NUM; k++){
            if(ssb_scan_resample_threads[k].joinable()){
              ssb_scan_resample_threads[k].join();
            }
          }
          // sequentially merge back
          cf_t * buf_split_ptr = rx_buffer;
          for (uint8_t k = 0; k < CS_RESAMPLE_WORKER_NUM; k++){
            copy_cpp_to_c_complex_arr(temp_y[k], buf_split_ptr, 
              actual_slot_szs[k]);
            buf_split_ptr += actual_slot_szs[k];
          }
        } else {
          // pre_resampling_slot_sz should be the same as slot_sz as 
          // resample ratio is 1 in this case
          srsran_vec_cf_copy(rx_buffer, pre_resampling_rx_buffer, 
            pre_resampling_slot_sz);
        }

        *(last_rx_time.get_ptr(0)) = rf_timestamp.get(0);

        cs_ret = srsran_searcher.run_slot(rx_buffer, slot_sz);
        if(cs_ret.result == srsue::nr::cell_search::ret_t::CELL_FOUND){
          break;
        }
      }
      if(cs_ret.result == srsue::nr::cell_search::ret_t::CELL_FOUND){
        std::cout << "Cell Found! (maybe reported multiple times in the"
          " next several GSCN; see README)" << std::endl;
        std::cout << "N_id: " << cs_ret.ssb_res.N_id << std::endl;

        if (local_log) {
          ScanLogNode scan_log_node;
          scan_log_node.gscn = gscn;
          scan_log_node.freq = cs_args.ssb_freq_hz;
          scan_log_node.pci = cs_ret.ssb_res.N_id;
          NRScopeLog::push_node(scan_log_node, rf_index);
          printf("scan log triggered here\n");
        }
      }
    }
  }

  free(rx_buffer);
  if (resample_needed) {
    for (uint8_t k = 0; k < CS_RESAMPLE_WORKER_NUM; k++) {
      msresamp_crcf_destroy(q[k]);
      free(temp_y[k]);
    }
    free(temp_x);
  }

  // Not exit explicitly as early termination might miss last found recording
  // NRScopeLog::exit_logger();

  return SRSRAN_SUCCESS;
}

int Radio::RadioInitandStart(){

  srsran_assert(raido_shared->init(rf_args, nullptr) == 
    SRSRAN_SUCCESS, "Failed Radio initialisation");
  radio = std::move(raido_shared);

  // Cell Searcher parameters  
  args_t.srate_hz = rf_args.srsran_srate_hz;
  rf_args.dl_freq = args_t.base_carrier.dl_center_frequency_hz;
  args_t.rf_device_name = rf_args.device_name;
  args_t.rf_device_args = rf_args.device_args;
  args_t.rf_log_level = "info";
  args_t.rf_rx_gain_dB = rf_args.rx_gain;
  args_t.rf_freq_offset_Hz = rf_args.freq_offset;
  args_t.phy_log_level   = "warning";
  args_t.stack_log_level = "warning";
  args_t.duration_ms = 1000;

  // Set sampling rate
  radio->set_rx_srate(rf_args.srate_hz);
  std::cout << "usrp srate_hz: " << rf_args.srate_hz << std::endl;

  if (fabs(rf_args.srsran_srate_hz - rf_args.srate_hz) < 0.1) {
    resample_needed = false;
  } else {
    resample_needed = true;
  }
  std::cout << "resample_needed: " << resample_needed << std::endl;

  // Set DL center frequency
  radio->set_rx_freq(0, (double)rf_args.dl_freq);
  // Set Rx gain
  radio->set_rx_gain(rf_args.rx_gain);
  
  args_t.set_ssb_from_band(ssb_scs);
  args_t.base_carrier.scs = args_t.ssb_scs;
  if(args_t.duplex_mode == SRSRAN_DUPLEX_MODE_TDD){
    args_t.base_carrier.ul_center_frequency_hz = 
      args_t.base_carrier.dl_center_frequency_hz;
  }

  pre_resampling_slot_sz = (uint32_t)(rf_args.srate_hz / 1000.0f / 
    SRSRAN_NOF_SLOTS_PER_SF_NR(ssb_scs));

  // Allocate receive buffer
  slot_sz = (uint32_t)(rf_args.srsran_srate_hz / 1000.0f / 
    SRSRAN_NOF_SLOTS_PER_SF_NR(ssb_scs));
  rx_buffer = srsran_vec_cf_malloc(SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs) * 
    pre_resampling_slot_sz * RING_BUF_SIZE);
  std::cout << "slot_sz: " << slot_sz << std::endl;
  srsran_vec_zero(rx_buffer, SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs) * 
  pre_resampling_slot_sz * RING_BUF_SIZE * sizeof(cf_t));
  /* the actual slot size after resampling */
  uint32_t actual_slot_szs[RESAMPLE_WORKER_NUM]; 

  // Allocate pre-resampling receive buffer
  pre_resampling_rx_buffer = srsran_vec_cf_malloc(
      SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs) * pre_resampling_slot_sz);
  std::cout << "pre_resampling_slot_sz: " << pre_resampling_slot_sz << std::endl;
  srsran_vec_zero(pre_resampling_rx_buffer, 
    SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs) * pre_resampling_slot_sz * 
    sizeof(cf_t));

  cs_args.center_freq_hz = args_t.base_carrier.dl_center_frequency_hz;
  cs_args.ssb_freq_hz = args_t.base_carrier.dl_center_frequency_hz;
  cs_args.ssb_scs = args_t.ssb_scs;
  cs_args.ssb_pattern = args_t.ssb_pattern;
  cs_args.duplex_mode = args_t.duplex_mode;

  uint32_t ssb_scs_hz = SRSRAN_SUBC_SPACING_NR(cs_args.ssb_scs);
  double ssb_bw_hz = SRSRAN_SSB_BW_SUBC * ssb_scs_hz;
  double ssb_center_freq_min_hz = args_t.base_carrier.dl_center_frequency_hz - 
    (args_t.srate_hz * 0.7 - ssb_bw_hz) / 2.0;
  double ssb_center_freq_max_hz = args_t.base_carrier.dl_center_frequency_hz + 
    (args_t.srate_hz * 0.7 - ssb_bw_hz) / 2.0;

  uint32_t band = bands.get_band_from_dl_freq_Hz_and_scs(
    args_t.base_carrier.dl_center_frequency_hz, cs_args.ssb_scs);
  srsran::srsran_band_helper::sync_raster_t ss = bands.get_sync_raster(
    band, cs_args.ssb_scs);
  srsran_assert(ss.valid(), "Invalid synchronization raster");

  // initialize resampling tool
  // resampling rate (output/input)
  float r = (float)rf_args.srsran_srate_hz/(float)rf_args.srate_hz;
  // resampling filter stop-band attenuation [dB]     
  float As=60.0f;
  msresamp_crcf q[RESAMPLE_WORKER_NUM];
  uint32_t temp_x_sz;
  uint32_t temp_y_sz;
  std::complex<float> * temp_x;
  std::complex<float> * temp_y[RESAMPLE_WORKER_NUM];
  if (resample_needed) {
    for (uint8_t i = 0; i < RESAMPLE_WORKER_NUM; i++) {
      q[i] = msresamp_crcf_create(r,As);
    }

    float delay = resample_needed ? msresamp_crcf_get_delay(q[0]) : 0;
    // add a few zero padding
    temp_x_sz = SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs) * 
      pre_resampling_slot_sz + (int)ceilf(delay) + 10;
    temp_x = (std::complex<float> *)malloc(temp_x_sz * 
      sizeof(std::complex<float>));
    // std::complex<float> temp_x[temp_x_sz];

    temp_y_sz = (uint32_t)(temp_x_sz * r * 2);
    for (uint8_t i = 0; i < RESAMPLE_WORKER_NUM; i++) {
      temp_y[i] = (std::complex<float> *)malloc(temp_y_sz * 
        sizeof(std::complex<float>));
    }
    // std::complex<float> temp_y[RESAMPLE_WORKER_NUM][temp_y_sz];
  }

  // FILE *fp_time_series_pre_resample;
  // fp_time_series_pre_resample = fopen("./time_series_pre_resample.txt", "w");
  // FILE *fp_time_series_post_resample;
  // fp_time_series_post_resample = fopen("./time_series_post_resample.txt", "w");

  // initialize loop-phase resamplers early
  if (resample_needed && !rk_initialized) {
    prepare_resampler(rk, 
      (float)rf_args.srsran_srate_hz/(float)rf_args.srate_hz, 
      SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs) * pre_resampling_slot_sz,
      RESAMPLE_WORKER_NUM);
    rk_initialized = true;
  }

  /* Initialize the task_scheduler and the workers in it.
     They will all remain inactive until the MIB is found. */
  task_scheduler_nrscope.InitandStart(local_log, to_google, rf_index, 
    nof_threads, nof_rnti_worker_groups, nof_bwps, cpu_affinity ,args_t, 
    nof_workers);

  std::cout << "Task scheduler started..." << std::endl;
  while (not ss.end()) {
    // Get SSB center frequency
    cs_args.ssb_freq_hz = ss.get_frequency();
    // Advance SSB frequency raster
    ss.next();

    /* Calculate frequency offset between the base-band center frequency and 
      the SSB absolute frequency */
    uint32_t offset_hz = (uint32_t)std::abs(std::round(cs_args.ssb_freq_hz - 
      args_t.base_carrier.dl_center_frequency_hz));

    // The SSB absolute frequency is invalid if it is outside the range and 
    // the offset is NOT multiple of the subcarrier spacing
    if ((cs_args.ssb_freq_hz < ssb_center_freq_min_hz) or 
        (cs_args.ssb_freq_hz > ssb_center_freq_max_hz) or
        (offset_hz % ssb_scs_hz != 0)) {
      // Skip this frequency
      continue;
    }

    /* xuyang debug: skip all other nearby measure and 
      just focus on the wanted SSB freq */
    if (offset_hz > 1) {
      continue;
    }

    /* which is indeed the srsran srate */
    srsran_searcher_cfg_t.srate_hz = args_t.srate_hz; 
    srsran_searcher_cfg_t.center_freq_hz = cs_args.ssb_freq_hz;
    srsran_searcher_cfg_t.ssb_freq_hz = cs_args.ssb_freq_hz;
    srsran_searcher_cfg_t.ssb_scs = args_t.ssb_scs;
    srsran_searcher_cfg_t.ssb_pattern = args_t.ssb_pattern;
    srsran_searcher_cfg_t.duplex_mode = args_t.duplex_mode;
    if (not srsran_searcher.start(srsran_searcher_cfg_t)) {
      std::cout << "Searcher: failed to start cell search" << std::endl;
      return NR_FAILURE;
    }
    /* Set the searching frequency to ssb_freq */
    /* Because the srsRAN implementation use the center_freq_hz for cell search */
    cs_args.center_freq_hz = cs_args.ssb_freq_hz;
    // std::cout << cs_args.ssb_freq_hz << std::endl;
    args_t.base_carrier.ssb_center_freq_hz = cs_args.ssb_freq_hz;

    radio->release_freq(0);
    radio->set_rx_freq(0, srsran_searcher_cfg_t.ssb_freq_hz);

    srsran::rf_buffer_t rf_buffer = {};
    rf_buffer.set_nof_samples(pre_resampling_slot_sz);
    rf_buffer.set(0, pre_resampling_rx_buffer);// + slot_sz);

    for(uint32_t trial=0; trial < nof_trials; trial++){
      if (trial == 0) {
        srsran_vec_cf_zero(rx_buffer, slot_sz);
        srsran_vec_cf_zero(pre_resampling_rx_buffer, pre_resampling_slot_sz);
      }
      // srsran_vec_cf_copy(rx_buffer, rx_buffer + slot_sz, slot_sz);

      srsran::rf_timestamp_t& rf_timestamp = last_rx_time;

      if (not radio->rx_now(rf_buffer, rf_timestamp)) {
        return SRSRAN_ERROR;
      }

      if (resample_needed) {
        // srsran_vec_fprint2_c(fp_time_series_pre_resample, 
        //    pre_resampling_rx_buffer, pre_resampling_slot_sz);
        copy_c_to_cpp_complex_arr_and_zero_padding(pre_resampling_rx_buffer, 
          temp_x, pre_resampling_slot_sz, temp_x_sz);
        uint32_t splitted_nx = pre_resampling_slot_sz / RESAMPLE_WORKER_NUM;
        std::vector <std::thread> ssb_scan_resample_threads;
        for (uint8_t k = 0; k < RESAMPLE_WORKER_NUM; k++) {
          ssb_scan_resample_threads.emplace_back(&resample_partially, &q[k], 
            temp_x, temp_y[k], k, splitted_nx, &actual_slot_szs[k]);
        }
        // msresamp_crcf_execute(q, temp_x, pre_resampling_slot_sz, 
        //    temp_y, &actual_slot_sz);

        for (uint8_t k = 0; k < RESAMPLE_WORKER_NUM; k++){
          if(ssb_scan_resample_threads[k].joinable()){
            ssb_scan_resample_threads[k].join();
          }
        }

        // sequentially merge back
        cf_t * buf_split_ptr = rx_buffer;
        for (uint8_t k = 0; k < RESAMPLE_WORKER_NUM; k++){
          copy_cpp_to_c_complex_arr(temp_y[k], buf_split_ptr, 
            actual_slot_szs[k]);
          buf_split_ptr += actual_slot_szs[k];
        }

        // srsran_vec_fprint2_c(fp_time_series_post_resample, 
        //    rx_buffer, actual_slot_sz);
      } else {
        // pre_resampling_slot_sz should be the same as slot_sz as 
        // resample ratio is 1 in this case
        srsran_vec_cf_copy(rx_buffer, pre_resampling_rx_buffer, 
          pre_resampling_slot_sz);
      }

      *(last_rx_time.get_ptr(0)) = rf_timestamp.get(0);
      cs_ret = srsran_searcher.run_slot(rx_buffer, slot_sz);
      // std::cout << "Slot_sz: " << slot_sz << std::endl;
      if(cs_ret.result == srsue::nr::cell_search::ret_t::CELL_FOUND ){
        // printf("found cell in this slot\n");
        // srsran_vec_fprint_c(stdout, rx_buffer, slot_sz);
        break;
      }
    }
    if(cs_ret.result == srsue::nr::cell_search::ret_t::CELL_FOUND){
      std::cout << "Cell Found!" << std::endl;
      std::cout << "N_id: " << cs_ret.ssb_res.N_id << std::endl;
      std::cout << "Decoding MIB..." << std::endl;

      /* And the states are updated in the task_scheduler*/
      if(task_scheduler_nrscope.DecodeMIB(&args_t, &cs_ret, 
         &srsran_searcher_cfg_t, r, rf_args.srate_hz) < SRSRAN_SUCCESS){
        ERROR("Error init task scheduler");
        return NR_FAILURE;
      }

      if(SyncandDownlinkInit() < SRSRAN_SUCCESS){
        ERROR("Error decoding MIB");
        return NR_FAILURE;
      }

      if(RadioCapture() < SRSASN_SUCCESS){
        ERROR("Error in RadioCapture");
        return NR_FAILURE;
      }
    }
  }

  if (resample_needed) {
    for (uint8_t k = 0; k < RESAMPLE_WORKER_NUM; k++) {
      msresamp_crcf_destroy(q[k]);
      free(temp_y[k]);
    }
    free(temp_x);
  }
  return SRSRAN_SUCCESS;
}

static int slot_sync_recv_callback(void* ptr, 
                                   cf_t** buffer, 
                                   uint32_t nsamples, 
                                   srsran_timestamp_t* ts)
{
  if (ptr == nullptr) {
  return SRSRAN_ERROR_INVALID_INPUTS;
  }
  srsran::radio* radio = (srsran::radio*)ptr;

  cf_t* buffer_ptr[SRSRAN_MAX_CHANNELS] = {};
  buffer_ptr[0]                         = buffer[0];
  // std::cout << "[xuyang debug 3] find fetch nsamples: " 
  //    << nsamples << std::endl;
  srsran::rf_buffer_t rf_buffer(buffer_ptr, nsamples);

  srsran::rf_timestamp_t a;
  srsran::rf_timestamp_t &rf_timestamp = a;
  *ts = a.get(0);

  return radio->rx_now(rf_buffer, rf_timestamp);
}

int Radio::SyncandDownlinkInit(){
  //***** DL args Config Start *****//
  rf_buffer_t = srsran::rf_buffer_t(rx_buffer, 
    SRSRAN_NOF_SLOTS_PER_SF_NR(task_scheduler_nrscope.task_scheduler_state.
    args_t.ssb_scs) * pre_resampling_slot_sz * 2); // only one sf here
  // it appears the srsRAN is build on 15kHz scs, we need to use the srate and 
  // scs to calculate the correct subframe size 
  arg_scs.srate = task_scheduler_nrscope.task_scheduler_state.args_t.srate_hz;
  arg_scs.scs = task_scheduler_nrscope.task_scheduler_state.cell.mib.scs_common;

  arg_scs.coreset_offset_scs = (cs_args.ssb_freq_hz - 
    task_scheduler_nrscope.task_scheduler_state.coreset0_args_t.
    coreset0_center_freq_hz) / task_scheduler_nrscope.task_scheduler_state.
    cell.abs_pdcch_scs; // + 12;
  arg_scs.coreset_slot = 
    (uint32_t)task_scheduler_nrscope.task_scheduler_state.coreset0_args_t.n_0;
  task_scheduler_nrscope.task_scheduler_state.arg_scs = arg_scs;
  // arg_scs.phase_diff_first_second_half = 0;
  //***** DL args Config End *****//

  //***** Slot Sync Start *****//
  ue_sync_nr_args.max_srate_hz    = srsran_searcher_args_t.max_srate_hz;
  ue_sync_nr_args.min_scs         = srsran_searcher_args_t.ssb_min_scs;
  ue_sync_nr_args.nof_rx_channels = 1;
  ue_sync_nr_args.disable_cfo     = false;
  ue_sync_nr_args.pbch_dmrs_thr   = 0.5;
  ue_sync_nr_args.cfo_alpha       = 0.1;
  ue_sync_nr_args.recv_obj        = radio.get();
  ue_sync_nr_args.recv_callback   = slot_sync_recv_callback;

  ue_sync_nr.resample_ratio = (float)rf_args.srsran_srate_hz / 
    (float)rf_args.srate_hz;
  if (srsran_ue_sync_nr_init(&ue_sync_nr, &ue_sync_nr_args) < SRSRAN_SUCCESS) {
    std::cout << "Error initiating UE SYNC NR object" << std::endl;
    logger.error("Error initiating UE SYNC NR object");
    return SRSRAN_ERROR;
  }
  // Be careful of all the frequency setting (SSB/center downlink and etc.)!
  ssb_cfg.srate_hz = 
    task_scheduler_nrscope.task_scheduler_state.args_t.srate_hz;
  ssb_cfg.center_freq_hz = cs_args.ssb_freq_hz;
  ssb_cfg.ssb_freq_hz = cs_args.ssb_freq_hz;
  ssb_cfg.scs = cs_args.ssb_scs;
  ssb_cfg.pattern = cs_args.ssb_pattern;
  ssb_cfg.duplex_mode = cs_args.duplex_mode;
  ssb_cfg.periodicity_ms = 20; // for all in FR1

  sync_cfg.N_id = 
    task_scheduler_nrscope.task_scheduler_state.cs_ret.ssb_res.N_id;
  sync_cfg.ssb = ssb_cfg;
  sync_cfg.ssb.srate_hz = 
    task_scheduler_nrscope.task_scheduler_state.args_t.srate_hz;
  if (srsran_ue_sync_nr_set_cfg(&ue_sync_nr, &sync_cfg) < SRSRAN_SUCCESS) {
    printf("SYNC: failed to set cell configuration for N_id %d", sync_cfg.N_id);
    logger.error("SYNC: failed to set cell configuration for N_id %d", 
      sync_cfg.N_id);
    return SRSRAN_ERROR;
  }

  return SRSRAN_SUCCESS;
}

int Radio::FetchAndResample(){

  uint64_t next_produce_at = 0;

  bool in_sync = false; 
  uint32_t pre_resampling_sf_sz = 
    SRSRAN_NOF_SLOTS_PER_SF_NR(task_scheduler_nrscope.task_scheduler_state.
    args_t.ssb_scs) * pre_resampling_slot_sz;

  while(true){
    outcome.timestamp = last_rx_time.get(0);  
    struct timeval t0, t1;
    gettimeofday(&t0, NULL);   

    /* if not sync, we fetch and sync at the rx_buffer start, 
      otherwise we store from 1 to RING_BUF_MODULUS sf index 
      and back in a ring buffer manner
      i.e., 0 sf index is for sync and moving a sf data there for decoders to 
      process, note at the first round we start like 0, 2, 3... 
      (skip 1 if you do the math) */
    rf_buffer_t = !in_sync ?
      srsran::rf_buffer_t(rx_buffer, pre_resampling_sf_sz) :
      srsran::rf_buffer_t(rx_buffer + (pre_resampling_sf_sz * 
      (next_produce_at % RING_BUF_MODULUS + 1)), pre_resampling_sf_sz); 

    std::cout << "current_produce_at: " << (!in_sync ? 0 : 
        (next_produce_at % RING_BUF_MODULUS + 1)) << std::endl;
      std::cout << "current_produce_ptr: " << (rf_buffer_t.to_cf_t())[0] << 
      std::endl;

    /* note fetching the raw samples will temporarily touch area out of the 
      target sf boundary yet after resampling, all meaningful data will reside 
      the target sf arr area and the original raw extra part 
      beyond the boundary doesn't matter */
    if (srsran_ue_sync_nr_zerocopy_twinrx_nrscope(
        &ue_sync_nr, rf_buffer_t.to_cf_t(), &outcome, rk, resample_needed, 
        RESAMPLE_WORKER_NUM) < SRSRAN_SUCCESS) {
      std::cout << "SYNC: error in zerocopy" << std::endl;
      logger.error("SYNC: error in zerocopy");
      return false;
    }
    /* If in sync, update slot index. 
      The synced data is stored in rf_buffer_t.to_cf_t()[0] */
    if (outcome.in_sync){
      if (in_sync == false) {
        printf("in_sync change to true\n");
      }
      in_sync = true;
      // std::cout << "System frame idx: " << outcome.sfn << std::endl;
      // std::cout << "Subframe idx: " << outcome.sf_idx << std::endl;
      // a new sf data ready; let decoder consume
      next_produce_at++;
      sem_post(&smph_sf_data_prod_cons);
    } 

    gettimeofday(&t1, NULL);  
    std::cout << "producer time_spend: " << 
      (t1.tv_usec - t0.tv_usec) << "(us)" << std::endl;
  }

  return SRSRAN_SUCCESS;
}

int Radio::DecodeAndProcess(){
  uint32_t pre_resampling_sf_sz = 
    SRSRAN_NOF_SLOTS_PER_SF_NR(task_scheduler_nrscope.task_scheduler_state.
    args_t.ssb_scs) * pre_resampling_slot_sz;
  
  uint64_t next_consume_at = 0;
  bool first_time = true;
  task_scheduler_nrscope.task_scheduler_state.sib1_inited = true;
  while (true) {
    sem_wait(&smph_sf_data_prod_cons); 
    std::cout << "current_consume_at: " << (first_time ? 0 : 
      ((next_consume_at % RING_BUF_MODULUS + 1))) << std::endl;
    outcome.timestamp = last_rx_time.get(0);  
    struct timeval t0, t1;
    gettimeofday(&t0, NULL);
    // consume a sf data
    for(int slot_idx = 0; slot_idx < SRSRAN_NOF_SLOTS_PER_SF_NR(arg_scs.scs); 
        slot_idx++){
      srsran_slot_cfg_t slot = {0};
      slot.idx = (outcome.sf_idx) * SRSRAN_NSLOTS_PER_FRAME_NR(arg_scs.scs) / 
        10 + slot_idx;
      
      if (slot.idx == 0 && outcome.sfn == 0) {
        /* this is a new round of system frame indexes */
        sf_round ++;
      }
      /* Move rx_buffer
        here wanted data move to the buffer beginning for decoders to process
        fetch and resample thread will store unprocessed data at 1 to 
        RING_BUF_MODULUS sf index; we copy wanted data to 0 sf idx
        assumption: no way when we are decoding this sf the fetch thread has 
        go around the whole ring and modify this sf again */
      srsran_vec_cf_copy(rx_buffer, rx_buffer + 
        (first_time ? 0 : ((next_consume_at % RING_BUF_MODULUS + 1) * 
        pre_resampling_sf_sz)) + (slot_idx * slot_sz), slot_sz);

      std::cout << "decode slot: " << (int) slot.idx << "; current_consume_ptr: " 
        << rx_buffer + (first_time ? 0 : 
        ((next_consume_at % RING_BUF_MODULUS + 1) * pre_resampling_sf_sz)) + 
        (slot_idx * slot_sz) << std::endl; 

      if (first_time) {
        /* If the next result is not set */
        NRScopeTask::task_scheduler_lock.lock();
        task_scheduler_nrscope.next_result.sf_round = sf_round;
        task_scheduler_nrscope.next_result.slot.idx = slot.idx;
        if (outcome.sfn == 1023){
          task_scheduler_nrscope.next_result.outcome.sfn = 0;
          task_scheduler_nrscope.next_result.sf_round ++;
        } else {
          task_scheduler_nrscope.next_result.outcome.sfn = outcome.sfn + 1;
        }
        
        NRScopeTask::task_scheduler_lock.unlock();
      }

      if (task_scheduler_nrscope.AssignTask(sf_round, slot, outcome, rx_buffer) 
          < SRSRAN_SUCCESS) {
        ERROR("Assign task failed");
        /* Push empty slot result to the queue */
        SlotResult empty_result = {};
        empty_result.sf_round = sf_round;
        empty_result.slot = slot;
        empty_result.outcome = outcome;
        empty_result.sib_result = false;
        empty_result.rach_result = false;
        empty_result.dci_result = false;
        NRScopeTask::queue_lock.lock();
        NRScopeTask::global_slot_results.push_back(empty_result);
        NRScopeTask::queue_lock.unlock();
      }
    } // slot iteration

    gettimeofday(&t1, NULL);
    std::cout << "consumer time_spend: " << (int)(t1.tv_usec - t0.tv_usec) 
      << "(us)" << std::endl;
    next_consume_at++;
    first_time = false;
  } // true loop
  
  return SRSRAN_SUCCESS;
}

int Radio::RadioCapture(){

  std::thread fetch_thread {&Radio::FetchAndResample, this};
  std::thread deco_thread {&Radio::DecodeAndProcess, this};

  while (true) {}
  
  return SRSRAN_SUCCESS;
}
