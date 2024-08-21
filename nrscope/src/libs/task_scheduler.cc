#include "nrscope/hdr/task_scheduler.h"

TaskSchedulerNRScope::TaskSchedulerNRScope(){
  sib1_inited = false;
  rach_inited = false;
  dci_inited = false;

  sib1_found = false;
  rach_found = false;
  sibs_vec_inited = false;

  nof_known_rntis = 0;
  known_rntis.resize(nof_known_rntis);
}

TaskSchedulerNRScope::~TaskSchedulerNRScope(){
  
}

int TaskSchedulerNRScope::decode_mib(cell_searcher_args_t* args_t_, 
                              srsue::nr::cell_search::ret_t* cs_ret_,
                              srsue::nr::cell_search::cfg_t* srsran_searcher_cfg_t_,
                              float resample_ratio_,
                              uint32_t raw_srate_){
  args_t_->base_carrier.pci = cs_ret_->ssb_res.N_id;

  if(srsran_pbch_msg_nr_mib_unpack(&(cs_ret_->ssb_res.pbch_msg), &cell.mib) < SRSRAN_SUCCESS){
    ERROR("Error decoding MIB");
    return SRSRAN_ERROR;
  }

  char str[1024] = {};
  srsran_pbch_msg_nr_mib_info(&cell.mib, str, 1024);
  printf("MIB: %s\n", str);
  printf("MIB payload: ");
  for (int i =0; i<SRSRAN_PBCH_MSG_NR_MAX_SZ; i++){
    printf("%hhu ", cs_ret_->ssb_res.pbch_msg.payload[i]);
  }
  printf("\n");
  // std::cout << "cell.mib.ssb_offset: " << cell.mib.ssb_offset << std::endl;
  // std::cout << "((int)cs_ret.ssb_res.pbch_msg.k_ssb_msb): " << ((int)cs_ret_->ssb_res.pbch_msg.k_ssb_msb) << std::endl;

  cell.k_ssb = cell.mib.ssb_offset; // already added the msb of k_ssb

  // srsran_coreset0_ssb_offset returns the offset_rb relative to ssb
  // nearly all bands in FR1 have min bandwidth 5 or 10 MHz, so there are only 5 entries here.  
  coreset0_args_t.offset_rb = srsran_coreset0_ssb_offset(cell.mib.coreset0_idx, 
    args_t_->ssb_scs, cell.mib.scs_common);
  // std::cout << "Coreset offset in rbs related to SSB: " << coreset0_args_t.offset_rb << std::endl;
  // std::cout << "set coreset0 config" << std::endl;
  coreset0_t = {};
  // srsran_coreset_zero returns the offset_rb relative to pointA
  if(srsran_coreset_zero(cs_ret_->ssb_res.N_id, 
                         0, //cell.k_ssb * SRSRAN_SUBC_SPACING_NR(srsran_subcarrier_spacing_15kHz), 
                         args_t_->ssb_scs, 
                         cell.mib.scs_common, 
                         cell.mib.coreset0_idx, 
                         &coreset0_t) == SRSRAN_SUCCESS){
    char freq_res_str[SRSRAN_CORESET_FREQ_DOMAIN_RES_SIZE] = {};
    char coreset_info[512] = {};
    srsran_coreset_to_str(&coreset0_t, coreset_info, sizeof(coreset_info));
    printf("Coreset parameter: %s", coreset_info);
  }
  // std::cout << "After calling srsran_coreset_zero()" << std::endl;

  // To find the position of coreset0, we need to use the offset between SSB and CORESET0,
  // because we don't know the ssb_pointA_freq_offset_Hz yet required by the srsran_coreset_zero function.
  // coreset0_t low bound freq = ssb center freq - 120 * scs (half of sc in ssb) - 
  // ssb_subcarrierOffset(from MIB) * scs - entry->offset_rb * 12(sc in one rb) * scs
  cell.abs_ssb_scs = SRSRAN_SUBC_SPACING_NR(args_t_->ssb_scs);
  cell.abs_pdcch_scs = SRSRAN_SUBC_SPACING_NR(cell.mib.scs_common);
  
  srsran::srsran_band_helper bands;
  coreset0_args_t.coreset0_lower_freq_hz = srsran_searcher_cfg_t_->ssb_freq_hz - (SRSRAN_SSB_BW_SUBC / 2) *
    cell.abs_ssb_scs - coreset0_args_t.offset_rb * NRSCOPE_NSC_PER_RB_NR * cell.abs_pdcch_scs - 
    cell.k_ssb * SRSRAN_SUBC_SPACING_NR(srsran_subcarrier_spacing_15kHz); // defined by standards
  coreset0_args_t.coreset0_center_freq_hz = coreset0_args_t.coreset0_lower_freq_hz + srsran_coreset_get_bw(&coreset0_t) / 2 * 
    cell.abs_pdcch_scs * NRSCOPE_NSC_PER_RB_NR;

  // std::cout << "k_ssb: " << cell.k_ssb << std::endl;
  // std::cout << "ssb freq hz: " << srsran_searcher_cfg_t.ssb_freq_hz << std::endl;
  // std::cout << "coreset0_lower freq hz: " << coreset0_args_t.coreset0_lower_freq_hz << std::endl;
  // std::cout << "coreset0 center freq hz: " << coreset0_args_t.coreset0_center_freq_hz << std::endl;
  // std::cout << "ssb_lower_freq hz: " << srsran_searcher_cfg_t.ssb_freq_hz - (SRSRAN_SSB_BW_SUBC / 2) *
  //   cell.abs_ssb_scs << std::endl;
  // std::cout << "coreset0_bw: " << srsran_coreset_get_bw(&coreset0_t) << std::endl;
  // std::cout << "coreset0_nof_symb: " << coreset0_t.duration << std::endl;
  // std::cout << "scs_common: " << cell.mib.scs_common << std::endl;
  // // the total used prb for coreset0 is 48 -> 17.28 MHz bw
  
  // std::cout << "mib pdcch-configSIB1.coreset0_idx: " << cell.mib.coreset0_idx << std::endl;
  // std::cout << "mib pdcch-configSIB1.searchSpaceZero: " << cell.mib.ss0_idx << std::endl;
  // printf("mib ssb-index: %u\n", cell.mib.ssb_idx);

  coreset_zero_t_f_entry_nrscope coreset_zero_cfg;
  // Get coreset_zero's position in time domain, check table 38.213, 13-11, because USRP can only support FR1.
  if(coreset_zero_t_f_nrscope(cell.mib.ss0_idx, cell.mib.ssb_idx, coreset0_t.duration, &coreset_zero_cfg) < 
    SRSRAN_SUCCESS){
    ERROR("Error checking table 13-11");
    return SRSRAN_ERROR;
  }
  // std::cout << "After calling coreset_zero_t_f_nrscope" << std::endl;

  cell.u = (int)args_t_->ssb_scs; 
  coreset0_args_t.n_0 = (coreset_zero_cfg.O * (int)pow(2, cell.u) + 
    (int)floor(cell.mib.ssb_idx * coreset_zero_cfg.M)) % SRSRAN_NSLOTS_X_FRAME_NR(cell.u);
  // sfn_c = 0, in even system frame, sfn_c = 1, in odd system frame    
  coreset0_args_t.sfn_c = (int)(floor(coreset_zero_cfg.O * pow(2, cell.u) + 
    floor(cell.mib.ssb_idx * coreset_zero_cfg.M)) / SRSRAN_NSLOTS_X_FRAME_NR(cell.u)) % 2;
  args_t_->base_carrier.nof_prb = srsran_coreset_get_bw(&coreset0_t);

  args_t = *args_t_;
  cs_ret = *cs_ret_;
  memcpy(&srsran_searcher_cfg_t, srsran_searcher_cfg_t_, sizeof(srsue::nr::cell_search::cfg_t));
  // std::cout << "After memcpy" << std::endl;

  // initiate resampler here
  resample_ratio = resample_ratio_;
  float As=60.0f;
  resampler = msresamp_crcf_create(resample_ratio,As);
  resampler_delay = msresamp_crcf_get_delay(resampler);
  pre_resampling_slot_sz = raw_srate_ / 1000 / SRSRAN_NOF_SLOTS_PER_SF_NR(args_t_->ssb_scs); // don't hardcode it; change later
  temp_x_sz = pre_resampling_slot_sz + (int)ceilf(resampler_delay) + 10;
  temp_y_sz = (uint32_t)(temp_x_sz * resample_ratio * 2);
  temp_x = SRSRAN_MEM_ALLOC(std::complex<float>, temp_x_sz);
  temp_y = SRSRAN_MEM_ALLOC(std::complex<float>, temp_y_sz);

  return SRSRAN_SUCCESS;
}

int TaskSchedulerNRScope::merge_results(){
  
  for (uint8_t b = 0; b < nof_bwps; b++) {
    DCIFeedback new_result;
    results[b] = new_result;
    results[b].dl_grants.resize(nof_known_rntis);
    results[b].ul_grants.resize(nof_known_rntis);
    results[b].spare_dl_prbs.resize(nof_known_rntis);
    results[b].spare_dl_tbs.resize(nof_known_rntis);
    results[b].spare_dl_bits.resize(nof_known_rntis);
    results[b].spare_ul_prbs.resize(nof_known_rntis);
    results[b].spare_ul_tbs.resize(nof_known_rntis);
    results[b].spare_ul_bits.resize(nof_known_rntis);
    results[b].dl_dcis.resize(nof_known_rntis);
    results[b].ul_dcis.resize(nof_known_rntis);

    uint32_t rnti_s = 0;
    uint32_t rnti_e = 0;
    for(uint32_t i = 0; i < nof_rnti_worker_groups; i++){

      if(rnti_s >= nof_known_rntis){
        continue;
      }
      uint32_t n_rntis = nof_sharded_rntis[i * nof_bwps];
      rnti_e = rnti_s + n_rntis;
      if(rnti_e > nof_known_rntis){
        rnti_e = nof_known_rntis;
      }

      uint32_t thread_id = i * nof_bwps + b;
      results[b].nof_dl_used_prbs += sharded_results[thread_id].nof_dl_used_prbs;
      results[b].nof_ul_used_prbs += sharded_results[thread_id].nof_ul_used_prbs;

      for(uint32_t k = 0; k < n_rntis; k++) {
        results[b].dl_dcis[k+rnti_s] = sharded_results[thread_id].dl_dcis[k];
        results[b].ul_dcis[k+rnti_s] = sharded_results[thread_id].ul_dcis[k];
        results[b].dl_grants[k+rnti_s] = sharded_results[thread_id].dl_grants[k];
        results[b].ul_grants[k+rnti_s] = sharded_results[thread_id].ul_grants[k];
      }
      rnti_s = rnti_e;
    }

    std::cout << "End of nof_threads..." << std::endl;

    // TO-DISCUSS: to obtain even more precise result, here maybe we should total user payload prb in that bwp - used prb
    results[b].nof_dl_spare_prbs = args_t.base_carrier.nof_prb * (14 - 2) - results[b].nof_dl_used_prbs;
    for(uint32_t idx = 0; idx < nof_known_rntis; idx ++){
      results[b].spare_dl_prbs[idx] = results[b].nof_dl_spare_prbs / nof_known_rntis;
      if(abs(results[b].spare_dl_prbs[idx]) > args_t.base_carrier.nof_prb * (14 - 2)){
        results[b].spare_dl_prbs[idx] = 0;
      }
      results[b].spare_dl_tbs[idx] = (int) ((float)results[b].spare_dl_prbs[idx] * dl_prb_rate[idx]);
      results[b].spare_dl_bits[idx] = (int) ((float)results[b].spare_dl_prbs[idx] * dl_prb_bits_rate[idx]);
    }

    results[b].nof_ul_spare_prbs = args_t.base_carrier.nof_prb * (14 - 2) - results[b].nof_ul_used_prbs;
    for(uint32_t idx = 0; idx < nof_known_rntis; idx ++){
      results[b].spare_ul_prbs[idx] = results[b].nof_ul_spare_prbs / nof_known_rntis;
      if(abs(results[b].spare_ul_prbs[idx]) > args_t.base_carrier.nof_prb * (14 - 2)){
        results[b].spare_ul_prbs[idx] = 0;
      }
      results[b].spare_ul_tbs[idx] = (int) ((float)results[b].spare_ul_prbs[idx] * ul_prb_rate[idx]);
      results[b].spare_ul_bits[idx] = (int) ((float)results[b].spare_ul_prbs[idx] * ul_prb_bits_rate[idx]);
    }
  }

  return SRSRAN_SUCCESS;
}

int TaskSchedulerNRScope::update_known_rntis(){
  if(new_rnti_number <= 0){
    return SRSRAN_SUCCESS;
  }

  nof_known_rntis += new_rnti_number;
  for(uint32_t i = 0; i < new_rnti_number; i++){
    known_rntis.emplace_back(new_rntis_found[i]);
  }

  new_rntis_found.clear();
  new_rnti_number = 0;
  return SRSRAN_SUCCESS;
}