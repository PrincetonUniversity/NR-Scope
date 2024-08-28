#include "nrscope/hdr/dci_decoder.h"
#include <numeric>

// Borrow from https://stackoverflow.com/questions/35833360/get-the-average-value-from-a-vector-of-integers
double avg_uint32(std::vector<uint32_t> const& v) {
    return 1.0 * std::accumulate(v.begin(), v.end(), 0LL) / v.size();
}

double sum_uint32(std::vector<uint32_t> const& v) {
    return 1.0 * std::accumulate(v.begin(), v.end(), 0LL);
}

DCIDecoder::DCIDecoder(uint32_t max_nof_rntis){

  ue_dl_tmp = (srsran_ue_dl_nr_t*) malloc(sizeof(srsran_ue_dl_nr_t));
  slot_tmp = (srsran_slot_cfg_t*) malloc(sizeof(srsran_slot_cfg_t));

  dci_dl = (srsran_dci_dl_nr_t*) malloc(sizeof(srsran_dci_dl_nr_t) * (max_nof_rntis));
  dci_ul = (srsran_dci_ul_nr_t*) malloc(sizeof(srsran_dci_ul_nr_t) * (max_nof_rntis));

}

DCIDecoder::~DCIDecoder(){

}

int DCIDecoder::dci_decoder_and_reception_init(srsran_ue_dl_nr_sratescs_info arg_scs_,
                                               TaskSchedulerNRScope* task_scheduler_nrscope,
                                               cf_t* input[SRSRAN_MAX_PORTS],
                                               u_int8_t bwp_id){ 
  
  memcpy(&base_carrier, &task_scheduler_nrscope->args_t.base_carrier, sizeof(srsran_carrier_nr_t));

  arg_scs = arg_scs_; 
  cell = task_scheduler_nrscope->cell;

  bwp_worker_id = bwp_id;

  ue_dl_args.nof_rx_antennas               = 1;
  ue_dl_args.pdsch.sch.disable_simd        = false;
  ue_dl_args.pdsch.sch.decoder_use_flooded = false;
  ue_dl_args.pdsch.measure_evm             = true;
  ue_dl_args.pdcch.disable_simd            = false;
  ue_dl_args.pdcch.measure_evm             = true;
  ue_dl_args.nof_max_prb                   = 275;

  memcpy(&coreset0_t, &task_scheduler_nrscope->coreset0_t, sizeof(srsran_coreset_t));
  sib1 = task_scheduler_nrscope->sib1;
  master_cell_group = task_scheduler_nrscope->master_cell_group;
  rrc_setup = task_scheduler_nrscope->rrc_setup;

  dci_cfg.bwp_dl_initial_bw   = 275;
  dci_cfg.bwp_ul_initial_bw   = 275;
  dci_cfg.bwp_dl_active_bw    = 275;
  dci_cfg.bwp_ul_active_bw    = 275;
  dci_cfg.monitor_common_0_0  = true;
  dci_cfg.monitor_0_0_and_1_0 = true;
  dci_cfg.monitor_0_1_and_1_1 = true;
  // set coreset0 bandwidth
  dci_cfg.coreset0_bw = srsran_coreset_get_bw(&coreset0_t);

  pdcch_cfg.coreset_present[0] = true;
  search_space = &pdcch_cfg.search_space[0];
  pdcch_cfg.search_space_present[0]   = true;
  search_space->id                    = 0;
  search_space->coreset_id            = 0;
  search_space->type                  = srsran_search_space_type_common_0;
  search_space->formats[0]            = srsran_dci_format_nr_1_0;
  search_space->nof_formats           = 1;
  for (uint32_t L = 0; L < SRSRAN_SEARCH_SPACE_NOF_AGGREGATION_LEVELS_NR; L++) {
    search_space->nof_candidates[L] = srsran_pdcch_nr_max_candidates_coreset(&coreset0_t, L);
  }
  pdcch_cfg.coreset[0] = coreset0_t; 

  // parameter settings
  pdcch_cfg.search_space_present[0]      = true;
  pdcch_cfg.search_space_present[1]      = false;
  pdcch_cfg.search_space_present[2]      = false;
  pdcch_cfg.ra_search_space_present      = false;

  asn1::rrc_nr::bwp_dl_ded_s * bwp_dl_ded_s_ptr = NULL;
  asn1::rrc_nr::bwp_ul_ded_s * bwp_ul_ded_s_ptr = NULL;

  // assume ul bwp n and dl bwp n should be activated and used at the same time (lso for sure for TDD)
  if (bwp_id == 0) {
    bwp_dl_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp);
    bwp_ul_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp);
    printf("[hidden bwp] bwp 0 set\n");
  }
  else if (bwp_id <= 3) {
    for (uint8_t i = 0; i < master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list.size(); i++) {
      if (bwp_id == master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list[i].bwp_id) {
        if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list[i].bwp_ded_present) {
          bwp_dl_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list[i].bwp_ded);
          break;
        }
        else {
          printf("bwp id %u does not have a ded dl config in RRCSetup", bwp_id);
        }
      }
    }

    if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg_present) {
      for (uint8_t i = 0; i < master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list.size(); i++) {
        if (bwp_id == master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list[i].bwp_id) {
          if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list[i].bwp_ded_present) {
            bwp_ul_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list[i].bwp_ded);
            break;
          }
          else {
            printf("bwp id %u does not have a ded ul config in RRCSetup", bwp_id);
          }
        }
      }
    }
    
  }
  else {
    ERROR("bwp id cannot be greater than 3!\n");
    return SRSRAN_ERROR;
  }

  if (bwp_dl_ded_s_ptr == NULL || bwp_ul_ded_s_ptr == NULL) {
    // ERROR("bwp id %d ul or dl config never appears in RRCSetup (what we assume now only checking in RRCSetup). Currently please bring back nof_bwps back to 1 in config.yaml as we are working on encrypted RRCReconfiguration-based BWP config monitoring.\n", bwp_id);
    printf("[hidden bwp] bwp id %d config not appear in RRCSetup. Assume it's hidden. Will monitor it heuristically\n", bwp_id);
    hidden_bwp = true;
    // copy bwp0 setting first
    bwp_dl_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp);
    bwp_ul_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp);
  } else {
    hidden_bwp = false;
  }

  // [xuyang] done pointer determination

  if(bwp_dl_ded_s_ptr->pdcch_cfg.is_setup()){
    pdcch_cfg.search_space[0].id = bwp_dl_ded_s_ptr->pdcch_cfg.setup().
                                   search_spaces_to_add_mod_list[0].search_space_id;
    pdcch_cfg.search_space[0].coreset_id = bwp_dl_ded_s_ptr->pdcch_cfg.setup().
                                           ctrl_res_set_to_add_mod_list[0].ctrl_res_set_id;
    
    printf("pdcch_cfg.search_space[0].coreset_id in bwp%u: %u\n", bwp_id, pdcch_cfg.search_space[0].coreset_id);

    pdcch_cfg.search_space[0].type = srsran_search_space_type_ue;
    if(bwp_dl_ded_s_ptr->pdcch_cfg.setup().
       search_spaces_to_add_mod_list[0].search_space_type.ue_specific().dci_formats.formats0_minus1_and_minus1_minus1){
      pdcch_cfg.search_space[0].formats[0] = srsran_dci_format_nr_1_1;
      pdcch_cfg.search_space[0].formats[1] = srsran_dci_format_nr_0_1;
      dci_cfg.monitor_0_0_and_1_0 = false;
      dci_cfg.monitor_common_0_0 = false;
    }else if(bwp_dl_ded_s_ptr->pdcch_cfg.setup().search_spaces_to_add_mod_list[0].
             search_space_type.ue_specific().dci_formats.formats0_minus0_and_minus1_minus0){
      pdcch_cfg.search_space[0].formats[0] = srsran_dci_format_nr_1_0; 
      pdcch_cfg.search_space[0].formats[1] = srsran_dci_format_nr_0_0;
      dci_cfg.monitor_0_1_and_1_1 = false;
    }
    pdcch_cfg.search_space[0].nof_formats  = 2;
    pdcch_cfg.coreset[0] = coreset0_t; 
  }else{
    // Use some default settings
    pdcch_cfg.search_space[0].id           = 2;
    pdcch_cfg.search_space[0].coreset_id   = 1;
    pdcch_cfg.search_space[0].type         = srsran_search_space_type_ue;
    pdcch_cfg.search_space[0].formats[0]   = srsran_dci_format_nr_1_1; // from RRCSetup
    pdcch_cfg.search_space[0].formats[1]   = srsran_dci_format_nr_0_1; // from RRCSetup
    dci_cfg.monitor_0_0_and_1_0 = false;
    dci_cfg.monitor_common_0_0 = false;
    pdcch_cfg.search_space[0].nof_formats  = 2;
    pdcch_cfg.coreset[0] = coreset0_t; 
  }

  // [xuyang] collect dci type to pdcch_cfg and dci_cfg
  
  // all the Coreset information is from RRCSetup
  for (uint32_t crst_id = 0; crst_id < bwp_dl_ded_s_ptr->pdcch_cfg.setup().
    ctrl_res_set_to_add_mod_list.size(); crst_id++){
    srsran_coreset_t coreset_n;
    coreset_n.id = bwp_dl_ded_s_ptr->pdcch_cfg.setup().
                  ctrl_res_set_to_add_mod_list[0].ctrl_res_set_id; 

    printf("to addmod coreset_n.id in bwp0: %u\n", coreset_n.id);
    coreset_n.duration = bwp_dl_ded_s_ptr->pdcch_cfg.setup().
                          ctrl_res_set_to_add_mod_list[0].dur;
    for(int i = 0; i < 45; i++){
      coreset_n.freq_resources[i] = bwp_dl_ded_s_ptr->pdcch_cfg.
                                    setup().ctrl_res_set_to_add_mod_list[0].freq_domain_res.get(45-i-1);
    }
    coreset_n.offset_rb = 0;
    if (bwp_dl_ded_s_ptr->pdcch_cfg.
        setup().ctrl_res_set_to_add_mod_list[0].precoder_granularity == 
        asn1::rrc_nr::ctrl_res_set_s::precoder_granularity_opts::same_as_reg_bundle){
      coreset_n.precoder_granularity = srsran_coreset_precoder_granularity_reg_bundle;
    }else if (bwp_dl_ded_s_ptr->pdcch_cfg.
              setup().ctrl_res_set_to_add_mod_list[0].precoder_granularity == 
              asn1::rrc_nr::ctrl_res_set_s::precoder_granularity_opts::all_contiguous_rbs){
      coreset_n.precoder_granularity = srsran_coreset_precoder_granularity_contiguous;
    }

    if (bwp_dl_ded_s_ptr->pdcch_cfg.
      setup().ctrl_res_set_to_add_mod_list[0].cce_reg_map_type.type() == 
      asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::types_opts::non_interleaved ||
      bwp_dl_ded_s_ptr->pdcch_cfg.
      setup().ctrl_res_set_to_add_mod_list[0].cce_reg_map_type.type() == 
      asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::types_opts::nulltype){
      coreset_n.mapping_type = srsran_coreset_mapping_type_non_interleaved; 
      coreset_n.interleaver_size = srsran_coreset_bundle_size_n2; // doesn't matter, fill a random value
      coreset_n.shift_index = 0; // doesn't matter, fill a random value
      coreset_n.reg_bundle_size = srsran_coreset_bundle_size_n6; // doesen't matter, fill a random value
    }else{
      coreset_n.mapping_type = srsran_coreset_mapping_type_interleaved; 
      switch(bwp_dl_ded_s_ptr->pdcch_cfg.
            setup().ctrl_res_set_to_add_mod_list[0].cce_reg_map_type.interleaved().interleaver_size){
        case asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::interleaved_s_::interleaver_size_e_::n2:
          coreset_n.interleaver_size = srsran_coreset_bundle_size_n2;
          break;
        case asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::interleaved_s_::interleaver_size_e_::n3:
          coreset_n.interleaver_size = srsran_coreset_bundle_size_n3;
          break;
        case asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::interleaved_s_::interleaver_size_e_::n6:
          coreset_n.interleaver_size = srsran_coreset_bundle_size_n6;
          break;
        case asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::interleaved_s_::reg_bundle_size_e_::nulltype:
          ERROR("Interleaved size not found, set as bundle_size_n6\n");
          coreset_n.reg_bundle_size = srsran_coreset_bundle_size_n6;
          break;
        default:
          ERROR("Interleaved size not found, set as bundle_size_n6\n");
          coreset_n.reg_bundle_size = srsran_coreset_bundle_size_n6;
          break;
      }
      coreset_n.shift_index = bwp_dl_ded_s_ptr->pdcch_cfg.
      setup().ctrl_res_set_to_add_mod_list[0].cce_reg_map_type.interleaved().shift_idx;
      switch(bwp_dl_ded_s_ptr->pdcch_cfg.
            setup().ctrl_res_set_to_add_mod_list[0].cce_reg_map_type.interleaved().reg_bundle_size){
        case asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::interleaved_s_::reg_bundle_size_e_::n2:
          coreset_n.reg_bundle_size = srsran_coreset_bundle_size_n2;
          break;
        case asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::interleaved_s_::reg_bundle_size_e_::n3:
          coreset_n.reg_bundle_size = srsran_coreset_bundle_size_n3;
          break;
        case asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::interleaved_s_::reg_bundle_size_e_::n6:
          coreset_n.reg_bundle_size = srsran_coreset_bundle_size_n6;
          break;
        case asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::interleaved_s_::reg_bundle_size_e_::nulltype:
          ERROR("Reg bundle size not found, set as bundle_size_n6\n");
          coreset_n.reg_bundle_size = srsran_coreset_bundle_size_n6;
          break;
        default:
          ERROR("Reg bundle size not found, set as bundle_size_n6\n");
          coreset_n.reg_bundle_size = srsran_coreset_bundle_size_n6;
          break;
      }
    }
    coreset_n.dmrs_scrambling_id_present = bwp_dl_ded_s_ptr->pdcch_cfg.
                                            setup().ctrl_res_set_to_add_mod_list[0].pdcch_dmrs_scrambling_id_present;
    if (coreset_n.dmrs_scrambling_id_present){
      coreset_n.dmrs_scrambling_id = bwp_dl_ded_s_ptr->pdcch_cfg.
                                      setup().ctrl_res_set_to_add_mod_list[0].pdcch_dmrs_scrambling_id;
    }
    printf("coreset_dmrs_scrambling id: %u\n", coreset_n.dmrs_scrambling_id);

    pdcch_cfg.coreset[coreset_n.id] = coreset_n;
    pdcch_cfg.coreset_present[coreset_n.id] = true;

    char coreset_info[512] = {};
    srsran_coreset_to_str(&coreset_n, coreset_info, sizeof(coreset_info));
    printf("Coreset %d parameter: %s", coreset_n.id, coreset_info);

    if(crst_id == 0){
      coreset1_t = coreset_n;
    }
  }

  // [xuyang] get the first ue coreset details
  
  // For FR1 offset_to_point_a uses prbs with 15kHz scs. 
  srsran_searcher_cfg_t = task_scheduler_nrscope->srsran_searcher_cfg_t;
  double pointA = srsran_searcher_cfg_t.ssb_freq_hz - (SRSRAN_SSB_BW_SUBC / 2) *
    cell.abs_ssb_scs - cell.k_ssb * SRSRAN_SUBC_SPACING_NR(srsran_subcarrier_spacing_15kHz) 
    - sib1.serving_cell_cfg_common.dl_cfg_common.freq_info_dl.offset_to_point_a * 
    SRSRAN_SUBC_SPACING_NR(srsran_subcarrier_spacing_15kHz) * NRSCOPE_NSC_PER_RB_NR;
  std::cout << "pointA: " << pointA << std::endl;

  double coreset1_center_freq_hz = pointA + srsran_coreset_get_bw(&coreset1_t) / 2 * 
    cell.abs_pdcch_scs * NRSCOPE_NSC_PER_RB_NR;
  std::cout << "previous offset: " << arg_scs.coreset_offset_scs << std::endl;
  arg_scs.coreset_offset_scs = (base_carrier.ssb_center_freq_hz - coreset1_center_freq_hz) / cell.abs_pdcch_scs;
  std::cout << "current offset: " << arg_scs.coreset_offset_scs << std::endl;

  if (hidden_bwp) {
    std::cout << "[hidden bwp] groundtruth coreset1_center_freq_hz: " << coreset1_center_freq_hz << std::endl;

    // Get the carrier bw in "6-1"-type REG unit (i.e., 6 RB)
    uint16_t cbw_total = 0;
    uint16_t cbw_total_reg = 0;
    double carr_scs_hz = 0;
    if (task_scheduler_nrscope->sib1.serving_cell_cfg_common_present) {
      asn1::rrc_nr::freq_info_dl_sib_s::scs_specific_carrier_list_l_ scs_spec_carr_list =
      task_scheduler_nrscope->sib1.serving_cell_cfg_common.dl_cfg_common.freq_info_dl.scs_specific_carrier_list;

      if (scs_spec_carr_list.size() > 0) {
        // assume only 1 carrier and it's representative
        cbw_total = scs_spec_carr_list[0].carrier_bw; // here carrier_bw should be in RB unit
        carr_scs_hz = SRSRAN_SUBC_SPACING_NR(scs_spec_carr_list[0].subcarrier_spacing);
        cbw_total_reg = cbw_total / NUM_RB_PER_REG; // in REG unit
        coreset_central_freqs.resize(cbw_total_reg);
      }
    }
    std::cout << "[hidden bwp] cbw_total: " << cbw_total << std::endl;
    std::cout << "[hidden bwp] cbw_total_reg: " << cbw_total_reg << std::endl;

    // Get all possible CORESET central freq offset location (relative to SSB central freq)
    // Currently assume the lower bound of carrier bw aligns with point A (no offset)
    // TODO: investigate whether assumption above resonable
    // Therefore, we have the following 2 restrictions
    // (1) lower bound of the CORESET >= point A
    // (2) upper bound of the CORESET <= point A + carrier bw
    
    // Get total number of possible CORESETs
    uint8_t current_hidden_coreset_idx = 0;
    long double channel_upper_bound = pointA + cbw_total * NRSCOPE_NSC_PER_RB_NR * carr_scs_hz;
    while (current_hidden_coreset_idx < coreset_central_freqs.size()) {
      // offset
      long double extra_offset_to_coreset1_center_freq_hz = 
      current_hidden_coreset_idx * (cell.abs_pdcch_scs * NRSCOPE_NSC_PER_RB_NR * NUM_RB_PER_REG);
      // coreset upper
      long double current_coreset_upper_hz = coreset1_center_freq_hz + extra_offset_to_coreset1_center_freq_hz + 
      (srsran_coreset_get_bw(&coreset1_t) / 2 * cell.abs_pdcch_scs * NRSCOPE_NSC_PER_RB_NR);
      if (current_coreset_upper_hz <= channel_upper_bound) {
        // accept this possible CORESET for later processing
        // central freq
        coreset_central_freqs[current_hidden_coreset_idx] = 
        coreset1_center_freq_hz + extra_offset_to_coreset1_center_freq_hz;
        std::cout << "[hidden bwp] : coreset_central_freqs[" << (uint32_t)current_hidden_coreset_idx << "]: " << coreset_central_freqs[current_hidden_coreset_idx] << std::endl;
        current_hidden_coreset_idx++;
      }
      else {
        break;
      }
    }
    possible_coreset_total_num = current_hidden_coreset_idx;
    std::cout << "[hidden bwp] possible_coreset_total_num: " << (uint32_t)possible_coreset_total_num << std::endl;

    // Initialize the tracker for possible CORESETs
    dl_dci_num_1000_tracker.resize(possible_coreset_total_num);
    ul_dci_num_1000_tracker.resize(possible_coreset_total_num);
    for (uint8_t i = 0; i < possible_coreset_total_num; i++) {
      dl_dci_num_1000_tracker[i].resize(1000);
      ul_dci_num_1000_tracker[i].resize(1000);
    }
  }

  // [xuyang] done deriving the abs coreset central freq 
  
   // set ra search space directly from the RRC Setup
  pdcch_cfg.search_space[0].nof_candidates[0] = bwp_dl_ded_s_ptr->
                                                pdcch_cfg.setup().search_spaces_to_add_mod_list[0].
                                                nrof_candidates.aggregation_level1;
  pdcch_cfg.search_space[0].nof_candidates[1] = bwp_dl_ded_s_ptr->
                                                pdcch_cfg.setup().search_spaces_to_add_mod_list[0].
                                                nrof_candidates.aggregation_level2;
  pdcch_cfg.search_space[0].nof_candidates[2] = bwp_dl_ded_s_ptr->
                                                pdcch_cfg.setup().search_spaces_to_add_mod_list[0].
                                                nrof_candidates.aggregation_level4;
  pdcch_cfg.search_space[0].nof_candidates[3] = bwp_dl_ded_s_ptr->
                                                pdcch_cfg.setup().search_spaces_to_add_mod_list[0].
                                                nrof_candidates.aggregation_level8;
  pdcch_cfg.search_space[0].nof_candidates[4] = bwp_dl_ded_s_ptr->
                                                pdcch_cfg.setup().search_spaces_to_add_mod_list[0].
                                                nrof_candidates.aggregation_level16;

  dci_cfg.carrier_indicator_size = 0; // for carrier aggregation, we don't consider this situation.
  
  dci_cfg.enable_sul = false; // if the supplementary_ul in sp_cell_cfg_ded is present.
  if(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.supplementary_ul_present){
    dci_cfg.enable_sul = true;
  }

  dci_cfg.enable_hopping = false; // if the setting is absent, it's false.
  if(bwp_ul_ded_s_ptr->pusch_cfg.setup().freq_hop_present){
    dci_cfg.enable_hopping = true;
  }

  // [xuyang] done collecting nof candidates for each aggregation level 

  /// Format 0_1 specific configuration (for PUSCH only)
  ///< Number of UL BWPs excluding the initial UL BWP, mentioned in the TS as N_BWP_RRC
  dci_cfg.nof_ul_bwp = master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list.size(); 
  ///< Number of dedicated PUSCH time domain resource assigment, set to 0 for default
  dci_cfg.nof_ul_time_res = bwp_ul_ded_s_ptr->pusch_cfg.setup().
                            pusch_time_domain_alloc_list_present ? 
                            bwp_ul_ded_s_ptr->pusch_cfg.setup().
                            pusch_time_domain_alloc_list.setup().size() : 
                            (sib1.serving_cell_cfg_common.ul_cfg_common_present ? 
                            (sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common_present ?
                            sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup().pusch_time_domain_alloc_list.size() : 0) : 0);     
  dci_cfg.nof_srs = bwp_ul_ded_s_ptr->srs_cfg_present ? 
                    bwp_ul_ded_s_ptr->srs_cfg.setup().
                    srs_res_to_add_mod_list.size() : 0;             ///< Number of configured SRS resources

  ///< Set to the maximum number of layers for PUSCH
  if(bwp_ul_ded_s_ptr->pusch_cfg.setup().max_rank_present){
    dci_cfg.nof_ul_layers = bwp_ul_ded_s_ptr->pusch_cfg.setup().max_rank;       
  } else {
    dci_cfg.nof_ul_layers = 1;
  }

  // [xuyang] done collecting bwp size, pusch time allocation, max rank...
  
  ///< determined by maxCodeBlockGroupsPerTransportBlock for PUSCH
  if(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup().code_block_group_tx_present){
    switch(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup().
           code_block_group_tx.setup().max_code_block_groups_per_transport_block){
      case asn1::rrc_nr::pdsch_code_block_group_tx_s::max_code_block_groups_per_transport_block_opts::n2:
        dci_cfg.pusch_nof_cbg = 2;
        break;
      case asn1::rrc_nr::pdsch_code_block_group_tx_s::max_code_block_groups_per_transport_block_opts::n4:
        dci_cfg.pusch_nof_cbg = 4;
        break;
      case asn1::rrc_nr::pdsch_code_block_group_tx_s::max_code_block_groups_per_transport_block_opts::n6:
        dci_cfg.pusch_nof_cbg = 6;
        break;
      case asn1::rrc_nr::pdsch_code_block_group_tx_s::max_code_block_groups_per_transport_block_opts::n8:
        dci_cfg.pusch_nof_cbg = 8;
        break;
      default:
        ERROR("None type or not found pusch_nof_cbg, setting to 0.\n");
        dci_cfg.pusch_nof_cbg = 0;
        break;
    }
  } else {
    dci_cfg.pusch_nof_cbg = 0;
  }

  // [xuyang] done maxCodeBlockGroupsPerTransportBlock for pusch (why from pdsch)
  
  if(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.csi_meas_cfg_present){
    dci_cfg.report_trigger_size = master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.csi_meas_cfg.setup().report_trigger_size;
  }else{
    dci_cfg.report_trigger_size = 0; ///< determined by reportTriggerSize
  }

  if(bwp_ul_ded_s_ptr->pusch_cfg.setup().transform_precoder == 
     asn1::rrc_nr::pusch_cfg_s::transform_precoder_opts::disabled){
    dci_cfg.enable_transform_precoding = false;      ///< Set to true if PUSCH transform precoding is enabled
  } else if (bwp_ul_ded_s_ptr->pusch_cfg.setup().transform_precoder == 
     asn1::rrc_nr::pusch_cfg_s::transform_precoder_opts::enabled){
    dci_cfg.enable_transform_precoding = true;      ///< Set to true if PUSCH transform precoding is enabled
  }
  
  dci_cfg.pusch_tx_config_non_codebook = false;    ///< Set to true if PUSCH txConfig is set to non-codebook
  if(bwp_ul_ded_s_ptr->pusch_cfg.setup().tx_cfg_present){
    if(bwp_ul_ded_s_ptr->pusch_cfg.setup().tx_cfg.value == 
      bwp_ul_ded_s_ptr->pusch_cfg.setup().tx_cfg.codebook){
      dci_cfg.pusch_tx_config_non_codebook = false;
    }else if (bwp_ul_ded_s_ptr->pusch_cfg.setup().tx_cfg.value == 
      bwp_ul_ded_s_ptr->pusch_cfg.setup().tx_cfg.non_codebook){
      dci_cfg.pusch_tx_config_non_codebook = true;
    }
  }
  
  if(bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a.setup().phase_tracking_rs_present){
    dci_cfg.pusch_ptrs = true;                      ///< Set to true if PT-RS are enabled for PUSCH transmissionß
  } else {
    dci_cfg.pusch_ptrs = false;
  }

  if(bwp_ul_ded_s_ptr->pusch_cfg.setup().uci_on_pusch.setup().beta_offsets_present){
    if(bwp_ul_ded_s_ptr->pusch_cfg.setup().uci_on_pusch.setup().beta_offsets.type() == 
       asn1::rrc_nr::uci_on_pusch_s::beta_offsets_c_::types_opts::dynamic_type){
      dci_cfg.pusch_dynamic_betas = true;             ///< Set to true if beta offsets operation is not semi-static
    } else if(bwp_ul_ded_s_ptr->pusch_cfg.setup().uci_on_pusch.setup().beta_offsets.type() == 
       asn1::rrc_nr::uci_on_pusch_s::beta_offsets_c_::types_opts::semi_static){
      dci_cfg.pusch_dynamic_betas = false;             ///< Set to true if beta offsets operation is not semi-static
    } else{
      dci_cfg.pusch_dynamic_betas = false;
    }
  }

  ///< PUSCH resource allocation type
  if(bwp_ul_ded_s_ptr->pusch_cfg_present){
    switch(bwp_ul_ded_s_ptr->pusch_cfg.setup().res_alloc){
      case asn1::rrc_nr::pusch_cfg_s::res_alloc_opts::res_alloc_type0:
        dci_cfg.pusch_alloc_type = srsran_resource_alloc_type0;
        break;
      case asn1::rrc_nr::pusch_cfg_s::res_alloc_opts::res_alloc_type1:
        dci_cfg.pusch_alloc_type = srsran_resource_alloc_type1;
        break;
      case asn1::rrc_nr::pusch_cfg_s::res_alloc_opts::dynamic_switch:
        dci_cfg.pusch_alloc_type = srsran_resource_alloc_dynamic;
        break;
      case asn1::rrc_nr::pusch_cfg_s::res_alloc_opts::nulltype:
        ERROR("No PUSCH resource allocation found, use type 1\n");
        dci_cfg.pusch_alloc_type = srsran_resource_alloc_type1;
        break;
    }
  } else {
    ERROR("No PUSCH resource allocation found, use type 1\n");
    dci_cfg.pusch_alloc_type = srsran_resource_alloc_type1;
  }

  // get_nof_rbgs(uint32_t bwp_nof_prb, uint32_t bwp_start, bool config1_or_2)
  dci_cfg.nof_rb_groups = 0;
  // if(dci_cfg.pusch_alloc_type == srsran_resource_alloc_type0){
  //   if(bwp_ul_ded_s_ptr->pusch_cfg.setup().rbg_size_present){
  //     // BWP start prb is set to 0 since this is the only scenario that we see
  //     dci_cfg.nof_rb_groups = get_nof_rbgs(dci_cfg.bwp_ul_active_bw, 0, true); 
  //   }else{
  //     dci_cfg.nof_rb_groups = get_nof_rbgs(dci_cfg.bwp_ul_active_bw, 0, false);
  //   }
  // }

  if(bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a_present){
    if(bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a.setup().dmrs_type_present){
      dci_cfg.pusch_dmrs_type = srsran_dmrs_sch_type_2;
    } else{
      dci_cfg.pusch_dmrs_type = srsran_dmrs_sch_type_1;
    }
    if(bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a.setup().max_len_present){
      dci_cfg.pusch_dmrs_max_len = srsran_dmrs_sch_len_2; 
    }else{
      dci_cfg.pusch_dmrs_max_len = srsran_dmrs_sch_len_1; 
    }
  }else if (bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_b_present){
    if(bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_b.setup().dmrs_type_present){
      dci_cfg.pusch_dmrs_type = srsran_dmrs_sch_type_2;
    } else{
      dci_cfg.pusch_dmrs_type = srsran_dmrs_sch_type_1;
    }
    if(bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_b.setup().max_len_present){
      dci_cfg.pusch_dmrs_max_len = srsran_dmrs_sch_len_2; 
    }else{
      dci_cfg.pusch_dmrs_max_len = srsran_dmrs_sch_len_1; 
    }
  }else{
    dci_cfg.pusch_dmrs_type = srsran_dmrs_sch_type_1;  ///< PUSCH DMRS type
    dci_cfg.pusch_dmrs_max_len = srsran_dmrs_sch_len_1; ///< PUSCH DMRS maximum length
  }

  // [xuyang] all the pusch setting 

  /// Format 1_1 specific configuration (for PDSCH only)
  switch (master_cell_group.phys_cell_group_cfg.pdsch_harq_ack_codebook){
    case asn1::rrc_nr::phys_cell_group_cfg_s::pdsch_harq_ack_codebook_opts::dynamic_value:
      dci_cfg.harq_ack_codebok = srsran_pdsch_harq_ack_codebook_dynamic;
      break;
    case asn1::rrc_nr::phys_cell_group_cfg_s::pdsch_harq_ack_codebook_opts::semi_static:
      dci_cfg.harq_ack_codebok = srsran_pdsch_harq_ack_codebook_semi_static;
      break;
    default:
      ERROR("harq_ack_code none.\n");
      dci_cfg.harq_ack_codebok = srsran_pdsch_harq_ack_codebook_none;
      break;
  }  

  // For DCI 0_1
  ///< Set to true if HARQ-ACK codebook is set to dynamic with 2 sub-codebooks
  dci_cfg.dynamic_dual_harq_ack_codebook = false;

  dci_cfg.nof_dl_bwp             = master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list.size();
  dci_cfg.nof_dl_bwp             = 1; // [hidden bwp danger] only for testing
  dci_cfg.nof_dl_time_res        = bwp_dl_ded_s_ptr->pdsch_cfg.setup().
                                   pdsch_time_domain_alloc_list_present ? 
                                   bwp_dl_ded_s_ptr->pdsch_cfg.setup().
                                   pdsch_time_domain_alloc_list.setup().size() : ( sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common_present ? 
                                   sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup().pdsch_time_domain_alloc_list.size() : 0
                                   );
  dci_cfg.nof_aperiodic_zp       = bwp_dl_ded_s_ptr->pdsch_cfg.setup().
                                   aperiodic_zp_csi_rs_res_sets_to_add_mod_list.size();
  dci_cfg.pdsch_nof_cbg          = bwp_dl_ded_s_ptr->pdsch_cfg.setup().
                                   max_nrof_code_words_sched_by_dci_present ? 
                                   bwp_dl_ded_s_ptr->pdsch_cfg.setup().
                                   max_nrof_code_words_sched_by_dci : 0;
  dci_cfg.nof_dl_to_ul_ack       = bwp_ul_ded_s_ptr->pucch_cfg.setup().
                                   dl_data_to_ul_ack.size();
  dci_cfg.pdsch_inter_prb_to_prb = bwp_dl_ded_s_ptr->pdsch_cfg.setup().
                                   vrb_to_prb_interleaver_present;
  dci_cfg.pdsch_rm_pattern1      = bwp_dl_ded_s_ptr->pdsch_cfg.setup().rate_match_pattern_group1.size();
  dci_cfg.pdsch_rm_pattern2      = bwp_dl_ded_s_ptr->pdsch_cfg.setup().rate_match_pattern_group2.size();
  dci_cfg.pdsch_2cw = false; // set to false initially and if maxofcodewordscheduledbydci is 2, set to true.
  if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().max_nrof_code_words_sched_by_dci_present){
    if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().max_nrof_code_words_sched_by_dci == 
        asn1::rrc_nr::pdsch_cfg_s::max_nrof_code_words_sched_by_dci_opts::n2){
      dci_cfg.pdsch_2cw = true;
    }
  }

  // Only consider one serving cell
  dci_cfg.multiple_scell = false; 
  dci_cfg.pdsch_tci = bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list[0].
                      tci_present_in_dci_present ? true : false; 
  dci_cfg.pdsch_cbg_flush = master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup().
                            code_block_group_tx_present ? true : false; 

  dci_cfg.pdsch_dynamic_bundling = false; 
  if(bwp_dl_ded_s_ptr->pdsch_cfg.setup().prb_bundling_type.type() == 
    asn1::rrc_nr::pdsch_cfg_s::prb_bundling_type_c_::types_opts::dynamic_bundling){
    dci_cfg.pdsch_dynamic_bundling = true;
    ERROR("PRB dynamic bundling not implemented, which can cause being unable to find DCIs. We are working on it.");
  }

  switch(bwp_dl_ded_s_ptr->pdsch_cfg.setup().res_alloc){
    case asn1::rrc_nr::pdsch_cfg_s::res_alloc_opts::res_alloc_type0:
      dci_cfg.pdsch_alloc_type = srsran_resource_alloc_type0;
      break;
    case asn1::rrc_nr::pdsch_cfg_s::res_alloc_opts::res_alloc_type1:
      dci_cfg.pdsch_alloc_type = srsran_resource_alloc_type1;
      break;
    case asn1::rrc_nr::pdsch_cfg_s::res_alloc_opts::dynamic_switch:
      dci_cfg.pdsch_alloc_type = srsran_resource_alloc_dynamic;
      break;
    default:
      ERROR("pdsch alloc type not found, using type1.\n");
      dci_cfg.pdsch_alloc_type = srsran_resource_alloc_type1;
      break;
  }
  std::cout << "pdsch resource alloc: " << dci_cfg.pdsch_alloc_type << std::endl;

  if(bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a_present){
    if(bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a.setup().dmrs_type_present){
      dci_cfg.pdsch_dmrs_type = srsran_dmrs_sch_type_2;
    } else{
      dci_cfg.pdsch_dmrs_type = srsran_dmrs_sch_type_1;
    }
    if(bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a.setup().max_len_present){
      dci_cfg.pdsch_dmrs_max_len = srsran_dmrs_sch_len_2; 
    }else{
      dci_cfg.pdsch_dmrs_max_len = srsran_dmrs_sch_len_1; 
    }
  }else if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_b_present){
    if(bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_b.setup().dmrs_type_present){
      dci_cfg.pdsch_dmrs_type = srsran_dmrs_sch_type_2;
    } else{
      dci_cfg.pdsch_dmrs_type = srsran_dmrs_sch_type_1;
    }
    if(bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_b.setup().max_len_present){
      dci_cfg.pdsch_dmrs_max_len = srsran_dmrs_sch_len_2; 
    }else{
      dci_cfg.pdsch_dmrs_max_len = srsran_dmrs_sch_len_1; 
    }
  }else{
    dci_cfg.pdsch_dmrs_type = srsran_dmrs_sch_type_1;
    dci_cfg.pdsch_dmrs_max_len = srsran_dmrs_sch_len_1; 
  }

  pdsch_hl_cfg.typeA_pos = cell.mib.dmrs_typeA_pos;
  pusch_hl_cfg.typeA_pos = cell.mib.dmrs_typeA_pos;
  if(bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a_present){
    pdsch_hl_cfg.dmrs_typeA.present = bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a_present;
    switch(bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a.setup().dmrs_add_position){
      case asn1::rrc_nr::dmrs_dl_cfg_s::dmrs_add_position_opts::pos0:
        pdsch_hl_cfg.dmrs_typeA.additional_pos = srsran_dmrs_sch_add_pos_0;
        break;
      case asn1::rrc_nr::dmrs_dl_cfg_s::dmrs_add_position_opts::pos1:
        pdsch_hl_cfg.dmrs_typeA.additional_pos = srsran_dmrs_sch_add_pos_1;
        break;
      case asn1::rrc_nr::dmrs_dl_cfg_s::dmrs_add_position_opts::pos3:
        pdsch_hl_cfg.dmrs_typeA.additional_pos = srsran_dmrs_sch_add_pos_3;
        break;
      default:
        break;
    }
  }

  if(bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a_present){
    pusch_hl_cfg.dmrs_typeA.present = bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a_present;
    switch(bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a.setup().dmrs_add_position){
      case asn1::rrc_nr::dmrs_ul_cfg_s::dmrs_add_position_opts::pos0:
        pusch_hl_cfg.dmrs_typeA.additional_pos = srsran_dmrs_sch_add_pos_0;
        break;
      case asn1::rrc_nr::dmrs_ul_cfg_s::dmrs_add_position_opts::pos1:
        pusch_hl_cfg.dmrs_typeA.additional_pos = srsran_dmrs_sch_add_pos_1;
        break;
      case asn1::rrc_nr::dmrs_ul_cfg_s::dmrs_add_position_opts::pos3:
        pusch_hl_cfg.dmrs_typeA.additional_pos = srsran_dmrs_sch_add_pos_3;
        break;
      default:
        break;
    }
  }

  pdsch_hl_cfg.alloc = dci_cfg.pdsch_alloc_type;
  pusch_hl_cfg.alloc = dci_cfg.pusch_alloc_type;

  if(bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup().size() > 0){
    for (uint32_t pdsch_time_id = 0; pdsch_time_id < bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup().size(); pdsch_time_id++){
      if(bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup()[pdsch_time_id].k0_present){
        pdsch_hl_cfg.common_time_ra[pdsch_time_id].k = bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup()[pdsch_time_id].k0;
      }
      pdsch_hl_cfg.common_time_ra[pdsch_time_id].sliv = bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup()[pdsch_time_id].start_symbol_and_len;
      switch(bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup()[pdsch_time_id].map_type){
        case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::type_a:
          pdsch_hl_cfg.common_time_ra[pdsch_time_id].mapping_type = srsran_sch_mapping_type_A;
          break;
        case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::type_b:
          pdsch_hl_cfg.common_time_ra[pdsch_time_id].mapping_type = srsran_sch_mapping_type_B;
          break;
        case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::nulltype:
          break;
        default:
          break;
      }
    }
    pdsch_hl_cfg.nof_common_time_ra = bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup().size();
  }else{
    // use SIB 1 config
    for (uint32_t pdsch_time_id = 0; pdsch_time_id < sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup().pdsch_time_domain_alloc_list.size(); pdsch_time_id++){
      if(sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup().pdsch_time_domain_alloc_list[pdsch_time_id].k0_present){
        pdsch_hl_cfg.common_time_ra[pdsch_time_id].k = sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup().pdsch_time_domain_alloc_list[pdsch_time_id].k0;
      }
      pdsch_hl_cfg.common_time_ra[pdsch_time_id].sliv = sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup().pdsch_time_domain_alloc_list[pdsch_time_id].start_symbol_and_len;
      switch(sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup().pdsch_time_domain_alloc_list[pdsch_time_id].map_type){
        case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::type_a:
          pdsch_hl_cfg.common_time_ra[pdsch_time_id].mapping_type = srsran_sch_mapping_type_A;
          break;
        case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::type_b:
          pdsch_hl_cfg.common_time_ra[pdsch_time_id].mapping_type = srsran_sch_mapping_type_B;
          break;
        case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::nulltype:
          break;
        default:
          break;
      }
    }
    pdsch_hl_cfg.nof_common_time_ra = sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup().pdsch_time_domain_alloc_list.size();
  }
  
  if(bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup().size() > 0){
    for (uint32_t pusch_time_id = 0; pusch_time_id < bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup().size(); pusch_time_id++){
      if(bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup()[pusch_time_id].k2_present){
        pusch_hl_cfg.common_time_ra[pusch_time_id].k = bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup()[pusch_time_id].k2;
      }
      pusch_hl_cfg.common_time_ra[pusch_time_id].sliv = bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup()[pusch_time_id].start_symbol_and_len;
      switch(bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup()[pusch_time_id].map_type){
        case asn1::rrc_nr::pusch_time_domain_res_alloc_s::map_type_e_::type_a:
          pusch_hl_cfg.common_time_ra[pusch_time_id].mapping_type = srsran_sch_mapping_type_A;
          break;
        case asn1::rrc_nr::pusch_time_domain_res_alloc_s::map_type_e_::type_b:
          pusch_hl_cfg.common_time_ra[pusch_time_id].mapping_type = srsran_sch_mapping_type_B;
          break;
        case asn1::rrc_nr::pusch_time_domain_res_alloc_s::map_type_e_::nulltype:
          break;
        default:
          break;
      }
    }
    pusch_hl_cfg.nof_common_time_ra = bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup().size();
  }else{
    // use SIB 1 config
    for (uint32_t pusch_time_id = 0; pusch_time_id < sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup().pusch_time_domain_alloc_list.size(); pusch_time_id++){
      if(sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup().pusch_time_domain_alloc_list[pusch_time_id].k2_present){
        pusch_hl_cfg.common_time_ra[pusch_time_id].k = sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup().pusch_time_domain_alloc_list[pusch_time_id].k2;
      }
      pusch_hl_cfg.common_time_ra[pusch_time_id].sliv = sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup().pusch_time_domain_alloc_list[pusch_time_id].start_symbol_and_len;
      switch(sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup().pusch_time_domain_alloc_list[pusch_time_id].map_type){
        case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::type_a:
          pusch_hl_cfg.common_time_ra[pusch_time_id].mapping_type = srsran_sch_mapping_type_A;
          break;
        case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::type_b:
          pusch_hl_cfg.common_time_ra[pusch_time_id].mapping_type = srsran_sch_mapping_type_B;
          break;
        case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::nulltype:
          break;
        default:
          break;
      }
    }
    pusch_hl_cfg.nof_common_time_ra = sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup().pusch_time_domain_alloc_list.size();
  }
  
  // config according to the SIB 1's UL and DL BWP size
  dci_cfg.bwp_dl_initial_bw = sib1.serving_cell_cfg_common.dl_cfg_common.freq_info_dl.scs_specific_carrier_list[0].carrier_bw;
  dci_cfg.bwp_dl_active_bw = sib1.serving_cell_cfg_common.dl_cfg_common.freq_info_dl.scs_specific_carrier_list[0].carrier_bw;
  dci_cfg.bwp_ul_initial_bw = sib1.serving_cell_cfg_common.ul_cfg_common.freq_info_ul.scs_specific_carrier_list[0].carrier_bw; 
  dci_cfg.bwp_ul_active_bw = sib1.serving_cell_cfg_common.ul_cfg_common.freq_info_ul.scs_specific_carrier_list[0].carrier_bw; 

  base_carrier.nof_prb = srsran_coreset_get_bw(&coreset1_t);
  carrier_dl = base_carrier;
  carrier_dl.nof_prb = dci_cfg.bwp_dl_active_bw; // Use a dummy carrier for resource calculation.
  // Use a fixed value for Amarisoft evaluation
  carrier_dl.max_mimo_layers = master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg_present ?
                               master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup().max_mimo_layers_present ?
                               master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup().max_mimo_layers : 2 : 2;
  
  carrier_ul = base_carrier;
  carrier_ul.nof_prb = dci_cfg.bwp_ul_active_bw;
  carrier_ul.max_mimo_layers = dci_cfg.nof_ul_layers;
  printf("carrier_ul.max_mimo_layers: %d\n", carrier_ul.max_mimo_layers);

  dci_cfg.nof_rb_groups = 0;
  if(dci_cfg.pdsch_alloc_type == srsran_resource_alloc_type0){
    if(bwp_dl_ded_s_ptr->pdsch_cfg.setup().rbg_size == asn1::rrc_nr::pdsch_cfg_s::rbg_size_opts::cfg1){
      // BWP start prb is set to 0 since this is the only scenario that we see
      dci_cfg.nof_rb_groups = get_nof_rbgs(dci_cfg.bwp_dl_active_bw, 0, true); 
    }else if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().rbg_size == asn1::rrc_nr::pdsch_cfg_s::rbg_size_opts::cfg2){
      dci_cfg.nof_rb_groups = get_nof_rbgs(dci_cfg.bwp_dl_active_bw, 0, false);
    }
  }

  if (srsran_ue_dl_nr_init_nrscope(&ue_dl_dci, input, &ue_dl_args, arg_scs)) {
    ERROR("Error UE DL");
    return SRSRAN_ERROR;
  }
  if (srsran_ue_dl_nr_set_carrier_nrscope(&ue_dl_dci, &base_carrier, arg_scs)) {
    ERROR("Error setting SCH NR carrier");
    return SRSRAN_ERROR;
  }
  if (srsran_ue_dl_nr_set_pdcch_config(&ue_dl_dci, &pdcch_cfg, &dci_cfg)) {
    ERROR("Error setting CORESET");
    return SRSRAN_ERROR;
  }
  if (srsran_softbuffer_rx_init_guru(&softbuffer, SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, SRSRAN_LDPC_MAX_LEN_ENCODED_CB) <
      SRSRAN_SUCCESS) {
    ERROR("Error init soft-buffer");
    return SRSRAN_ERROR;
  }
  std::cout << "ending.." << std::endl;
  return SRSRAN_SUCCESS;
}


int DCIDecoder::decode_and_parse_dci_from_slot(srsran_slot_cfg_t* slot,
                                               TaskSchedulerNRScope* task_scheduler_nrscope,
                                               cf_t * raw_buffer){
  if(!task_scheduler_nrscope->rach_found or !task_scheduler_nrscope->dci_inited){
    std::cout << "RACH not found or DCI decoder not initialized, quitting..." << std::endl;
    return SRSRAN_SUCCESS;
  }

  // if (hidden_bwp) {
  //   std::cout << "[hidden bwp] Currently disable hidden bwp operations." << std::endl;
  //   return SRSRAN_SUCCESS;
  // }

  uint32_t n_rntis = (uint32_t) ceil((float) task_scheduler_nrscope->nof_known_rntis / (float) task_scheduler_nrscope->nof_rnti_worker_groups);
  uint32_t rnti_s = rnti_worker_group_id * n_rntis;
  uint32_t rnti_e = rnti_worker_group_id * n_rntis + n_rntis;

  if(rnti_s >= task_scheduler_nrscope->nof_known_rntis){
    std::cout << "DCI decoder " << dci_decoder_id << "|" << rnti_worker_group_id << " exits because it's excessive.." << std::endl;
    return SRSRAN_SUCCESS;
  }

  if(rnti_e > task_scheduler_nrscope->nof_known_rntis){
    rnti_e = task_scheduler_nrscope->nof_known_rntis;
    n_rntis = rnti_e - rnti_s;
  }

  std::cout << "DCI decoder " << dci_decoder_id << " processing: [" << rnti_s << ", " << rnti_e << ")" << std::endl;

  DCIFeedback new_result;
  task_scheduler_nrscope->sharded_results[dci_decoder_id] = new_result;

  task_scheduler_nrscope->sharded_results[dci_decoder_id].dl_grants.resize(n_rntis);
  task_scheduler_nrscope->sharded_results[dci_decoder_id].ul_grants.resize(n_rntis);
  task_scheduler_nrscope->sharded_results[dci_decoder_id].spare_dl_prbs.resize(n_rntis);
  task_scheduler_nrscope->sharded_results[dci_decoder_id].spare_dl_tbs.resize(n_rntis);
  task_scheduler_nrscope->sharded_results[dci_decoder_id].spare_dl_bits.resize(n_rntis);
  task_scheduler_nrscope->sharded_results[dci_decoder_id].spare_ul_prbs.resize(n_rntis);
  task_scheduler_nrscope->sharded_results[dci_decoder_id].spare_ul_tbs.resize(n_rntis);
  task_scheduler_nrscope->sharded_results[dci_decoder_id].spare_ul_bits.resize(n_rntis);
  task_scheduler_nrscope->sharded_results[dci_decoder_id].dl_dcis.resize(n_rntis);
  task_scheduler_nrscope->sharded_results[dci_decoder_id].ul_dcis.resize(n_rntis);

  task_scheduler_nrscope->sharded_rntis[dci_decoder_id].resize(n_rntis);
  task_scheduler_nrscope->nof_sharded_rntis[dci_decoder_id] = n_rntis;
  // std::cout << "task_scheduler_nrscope->nof_sharded_rntis[dci_decoder_id]: " << task_scheduler_nrscope->nof_sharded_rntis[dci_decoder_id] << std::endl;

  // std::cout << "sharded_rntis: ";
  for(uint32_t i = 0; i < n_rntis; i++){
    task_scheduler_nrscope->sharded_rntis[dci_decoder_id][i] = task_scheduler_nrscope->known_rntis[rnti_s + i];
    // std::cout << task_scheduler_nrscope->sharded_rntis[dci_decoder_id][i] << ", ";
  }
  // std::cout << std::endl;

  printf("[hidden bwp] trigger 1\n");
  for (uint8_t i = 0; !hidden_bwp || i < possible_coreset_total_num; i++) {

    if (hidden_bwp) {
      arg_scs.coreset_offset_scs = (base_carrier.ssb_center_freq_hz - coreset_central_freqs[i]) / cell.abs_pdcch_scs;
      printf("[hidden bwp] %u current offset to ssb: %d\n", i, arg_scs.coreset_offset_scs);
    }
    printf("[hidden bwp] trigger 1.1\n");

    // Set the buffer to 0s
    for(uint32_t idx = 0; idx < n_rntis; idx++){
      memset(&dci_dl[idx], 0, sizeof(srsran_dci_dl_nr_t));
      memset(&dci_ul[idx], 0, sizeof(srsran_dci_dl_nr_t));
    }
    printf("[hidden bwp] trigger 1.2\n");

    srsran_ue_dl_nr_estimate_fft_nrscope(&ue_dl_dci, slot, arg_scs);
    printf("[hidden bwp] trigger 1.3\n");

    int total_dl_dci = 0;
    int total_ul_dci = 0;

    printf("[hidden bwp] trigger 2\n");  

    for (uint32_t rnti_idx = 0; rnti_idx < n_rntis; rnti_idx++){
      
      memcpy(ue_dl_tmp, &ue_dl_dci, sizeof(srsran_ue_dl_nr_t));
      memcpy(slot_tmp, slot, sizeof(srsran_slot_cfg_t));

      int nof_dl_dci = srsran_ue_dl_nr_find_dl_dci_nrscope_dciloop(ue_dl_tmp, slot_tmp, task_scheduler_nrscope->sharded_rntis[dci_decoder_id][rnti_idx], 
                      srsran_rnti_type_c, dci_dl_tmp, 4);

      if (nof_dl_dci < SRSRAN_SUCCESS) {
        ERROR("Error in blind search");
      }

      int nof_ul_dci = srsran_ue_dl_nr_find_ul_dci(ue_dl_tmp, slot_tmp, task_scheduler_nrscope->sharded_rntis[dci_decoder_id][rnti_idx], srsran_rnti_type_c, dci_ul_tmp, 4);

      if(nof_dl_dci > 0){
        dci_dl[rnti_idx] = dci_dl_tmp[0];
        total_dl_dci += nof_dl_dci;
      }

      if(nof_ul_dci > 0){
        dci_ul[rnti_idx] = dci_ul_tmp[0];
        total_ul_dci += nof_ul_dci;
      }
    }

    if (hidden_bwp) {
      // Record found dci num for this possible CORESET
      dl_dci_num_1000_tracker[i][cur_tracker_idx % 1000] = (uint32_t)total_dl_dci;
      ul_dci_num_1000_tracker[i][cur_tracker_idx % 1000] = (uint32_t)total_ul_dci;
    }

    if(!hidden_bwp && total_dl_dci > 0){
      for (uint32_t dci_idx_dl = 0; dci_idx_dl < n_rntis; dci_idx_dl++){
        // the rnti will not be copied if no dci found
        if(dci_dl[dci_idx_dl].ctx.rnti == task_scheduler_nrscope->sharded_rntis[dci_decoder_id][dci_idx_dl]){
          task_scheduler_nrscope->sharded_results[dci_decoder_id].dl_dcis[dci_idx_dl] = dci_dl[dci_idx_dl];
          char str[1024] = {};
          srsran_dci_dl_nr_to_str(&(ue_dl_dci.dci), &dci_dl[dci_idx_dl], str, (uint32_t)sizeof(str));
          printf("DCIDecoder -- Found DCI: %s\n", str);
          // The grant may not be decoded correctly, since srsRAN's code is not complete.
          // We can calculate the DL bandwidth for this subframe by ourselves.
          if(dci_dl[dci_idx_dl].ctx.format == srsran_dci_format_nr_1_1) {
            srsran_sch_cfg_nr_t pdsch_cfg = {};
            pdsch_hl_cfg.mcs_table = srsran_mcs_table_256qam;
            // printf("pdsch_hl_cfg.dmrs_typeA.additional_pos: %d\n", pdsch_hl_cfg.dmrs_typeA.additional_pos);

            if (srsran_ra_dl_dci_to_grant_nr(&carrier_dl, slot, &pdsch_hl_cfg, &dci_dl[dci_idx_dl], 
                                            &pdsch_cfg, &pdsch_cfg.grant) < SRSRAN_SUCCESS) {
              ERROR("Error decoding PDSCH search");
              // return result;
            }
            srsran_sch_cfg_nr_info(&pdsch_cfg, str, (uint32_t)sizeof(str));
            printf("DCIDecoder -- PDSCH_cfg:\n%s", str);

            task_scheduler_nrscope->sharded_results[dci_decoder_id].dl_grants[dci_idx_dl] = pdsch_cfg;
            task_scheduler_nrscope->sharded_results[dci_decoder_id].nof_dl_used_prbs += pdsch_cfg.grant.nof_prb * pdsch_cfg.grant.L;

            task_scheduler_nrscope->dl_prb_rate[dci_idx_dl+rnti_s] = (float)(pdsch_cfg.grant.tb[0].tbs + 
              pdsch_cfg.grant.tb[1].tbs) / (float)pdsch_cfg.grant.nof_prb / (float)pdsch_cfg.grant.L;
            task_scheduler_nrscope->dl_prb_bits_rate[dci_idx_dl+rnti_s] = (float)(pdsch_cfg.grant.tb[0].nof_bits + 
              pdsch_cfg.grant.tb[1].nof_bits) / (float)pdsch_cfg.grant.nof_prb / (float)pdsch_cfg.grant.L;
          }
        }
      }
      // task_scheduler_nrscope->result.nof_dl_spare_prbs = carrier_dl.nof_prb * (14 - 2) - task_scheduler_nrscope->result.nof_dl_used_prbs;
      // for(uint32_t idx = 0; idx < task_scheduler_nrscope->nof_known_rntis; idx ++){
      //   task_scheduler_nrscope->result.spare_dl_prbs[idx] = task_scheduler_nrscope->result.nof_dl_spare_prbs / task_scheduler_nrscope->nof_known_rntis;
      //   if(abs(task_scheduler_nrscope->result.spare_dl_prbs[idx]) > carrier_dl.nof_prb * (14 - 2)){
      //     task_scheduler_nrscope->result.spare_dl_prbs[idx] = 0;
      //   }
      //   task_scheduler_nrscope->result.spare_dl_tbs[idx] = (int) ((float)task_scheduler_nrscope->result.spare_dl_prbs[idx] * dl_prb_rate[idx]);
      //   task_scheduler_nrscope->result.spare_dl_bits[idx] = (int) ((float)task_scheduler_nrscope->result.spare_dl_prbs[idx] * dl_prb_bits_rate[idx]);
      // }
    }else{
      // task_scheduler_nrscope->result.nof_dl_spare_prbs = carrier_dl.nof_prb * (14 - 2);
      // for(uint32_t idx = 0; idx < task_scheduler_nrscope->nof_known_rntis; idx++){
      //   task_scheduler_nrscope->result.spare_dl_prbs[idx] = (int)((float)task_scheduler_nrscope->result.nof_dl_spare_prbs / (float)task_scheduler_nrscope->nof_known_rntis);
      //   task_scheduler_nrscope->result.spare_dl_tbs[idx] = (int) ((float)task_scheduler_nrscope->result.spare_dl_prbs[idx] * dl_prb_rate[idx]);
      //   task_scheduler_nrscope->result.spare_dl_bits[idx] = (int) ((float)task_scheduler_nrscope->result.spare_dl_prbs[idx] * dl_prb_bits_rate[idx]);
      // }
    }
    
    if(!hidden_bwp && total_ul_dci > 0){
      for (uint32_t dci_idx_ul = 0; dci_idx_ul < n_rntis; dci_idx_ul++){
        if(dci_ul[dci_idx_ul].ctx.rnti == task_scheduler_nrscope->sharded_rntis[dci_decoder_id][dci_idx_ul]){
          task_scheduler_nrscope->sharded_results[dci_decoder_id].ul_dcis[dci_idx_ul] = dci_ul[dci_idx_ul];
          char str[1024] = {};
          srsran_dci_ul_nr_to_str(&(ue_dl_dci.dci), &dci_ul[dci_idx_ul], str, (uint32_t)sizeof(str));
          printf("DCIDecoder -- Found DCI: %s\n", str);
          // The grant may not be decoded correctly, since srsRAN's code is not complete. 
          // We can calculate the UL bandwidth for this subframe by ourselves.
          srsran_sch_cfg_nr_t pusch_cfg = {};
          pusch_hl_cfg.mcs_table = srsran_mcs_table_256qam;
          if (srsran_ra_ul_dci_to_grant_nr(&carrier_ul, slot, &pusch_hl_cfg, &dci_ul[dci_idx_ul], 
                                          &pusch_cfg, &pusch_cfg.grant) < SRSRAN_SUCCESS) {
            ERROR("Error decoding PUSCH search");
            // return result;
          }
          srsran_sch_cfg_nr_info(&pusch_cfg, str, (uint32_t)sizeof(str));
          printf("DCIDecoder -- PUSCH_cfg:\n%s", str);

          task_scheduler_nrscope->sharded_results[dci_decoder_id].ul_grants[dci_idx_ul] = pusch_cfg;
          task_scheduler_nrscope->sharded_results[dci_decoder_id].nof_ul_used_prbs += pusch_cfg.grant.nof_prb * pusch_cfg.grant.L;
          
          task_scheduler_nrscope->ul_prb_rate[dci_idx_ul+rnti_s] = (float)(pusch_cfg.grant.tb[0].tbs + 
            pusch_cfg.grant.tb[1].tbs) / (float)pusch_cfg.grant.nof_prb / (float)pusch_cfg.grant.L;
          task_scheduler_nrscope->ul_prb_bits_rate[dci_idx_ul+rnti_s] = (float)(pusch_cfg.grant.tb[0].nof_bits + 
            pusch_cfg.grant.tb[1].nof_bits) / (float)pusch_cfg.grant.nof_prb / (float)pusch_cfg.grant.L;
        }
      }
      // task_scheduler_nrscope->result.nof_ul_spare_prbs = carrier_dl.nof_prb * (14 - 2) - task_scheduler_nrscope->result.nof_ul_used_prbs;
      // for(uint32_t idx = 0; idx < task_scheduler_nrscope->nof_known_rntis; idx ++){
      //   task_scheduler_nrscope->result.spare_ul_prbs[idx] = task_scheduler_nrscope->result.nof_ul_spare_prbs / task_scheduler_nrscope->nof_known_rntis;
      //   task_scheduler_nrscope->result.spare_ul_tbs[idx] = (int) ((float)task_scheduler_nrscope->result.spare_ul_prbs[idx] * ul_prb_rate[idx]);
      //   task_scheduler_nrscope->result.spare_ul_bits[idx] = (int) ((float)task_scheduler_nrscope->result.spare_ul_prbs[idx] * ul_prb_bits_rate[idx]);
      // }
    }else{
      // task_scheduler_nrscope->result.nof_ul_spare_prbs = carrier_dl.nof_prb * (14 - 2);
      // for(uint32_t idx = 0; idx < task_scheduler_nrscope->nof_known_rntis; idx ++){
      //   task_scheduler_nrscope->result.spare_ul_prbs[idx] = (int)((float)task_scheduler_nrscope->result.nof_ul_spare_prbs / (float)task_scheduler_nrscope->nof_known_rntis);
      //   if(abs(task_scheduler_nrscope->result.spare_ul_prbs[idx]) > carrier_dl.nof_prb * (14 - 2)){
      //     task_scheduler_nrscope->result.spare_ul_prbs[idx] = 0;
      //   }
      //   task_scheduler_nrscope->result.spare_ul_tbs[idx] = (int) ((float)task_scheduler_nrscope->result.spare_ul_prbs[idx] * ul_prb_rate[idx]);
      //   task_scheduler_nrscope->result.spare_ul_bits[idx] = (int) ((float)task_scheduler_nrscope->result.spare_ul_prbs[idx] * ul_prb_bits_rate[idx]);
      // }
    }

    // A clear BWP case, so the 0th-index holds the right location and we have examined and thus terminate
    if(!hidden_bwp) {
      break;
    }
    
  } // end for loop for hidden CORESET traversal

  if (hidden_bwp) {
    // hidden BWP statistics
    for (uint8_t j = 0; j < possible_coreset_total_num; j++) {
      double avg = avg_uint32(dl_dci_num_1000_tracker[j]);
      double sum = sum_uint32(dl_dci_num_1000_tracker[j]);
      printf("[hidden bwp] %uth possible CORESET has avg %.2f and total %.2f DCI found in last 1000 slots\n", j, avg, sum);
    }
    cur_tracker_idx++;
  }

  return SRSRAN_SUCCESS;
}