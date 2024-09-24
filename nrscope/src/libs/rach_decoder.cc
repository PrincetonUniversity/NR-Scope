#include "nrscope/hdr/rach_decoder.h"

std::mutex lock_rach;

RachDecoder::RachDecoder(){
  // rach_dl_ul_info = {false, false, false, false, false};
  sib1 = {};
  prach_cfg_nr = {};
  prach = {};
  prach_cfg = {};

  dci_rach = (srsran_dci_dl_nr_t*) malloc(sizeof(srsran_dci_dl_nr_t) * 
  (SRSRAN_MAX_DCI_MSG_NR));

  data_pdcch = srsran_vec_u8_malloc(SRSRAN_SLOT_MAX_NOF_BITS_NR);
  if (data_pdcch == NULL) {
    ERROR("Error malloc");
  }
}

RachDecoder::~RachDecoder(){

}

int RachDecoder::RACHDecoderInit(WorkState state){
  sib1 = state.sib1;
  base_carrier = state.args_t.base_carrier;
  srsran::srsran_band_helper bands;
  uint16_t band = 
    bands.get_band_from_dl_freq_Hz(base_carrier.dl_center_frequency_hz);

  uint32_t cfg_idx = sib1.serving_cell_cfg_common.ul_cfg_common.
    init_ul_bwp.rach_cfg_common.setup().rach_cfg_generic.prach_cfg_idx;
  uint32_t sul_idx; // sul_id for ra-rnti calculation
  std::vector<uint32_t> t_idx; // t_id for ra-rnti calculation
  if(bands.get_duplex_mode(band) == SRSRAN_DUPLEX_MODE_TDD){
    prach_cfg_nr = *srsran_prach_nr_get_cfg_fr1_unpaired(cfg_idx);
    sul_idx = 0;
  }else if (bands.get_duplex_mode(band) == SRSRAN_DUPLEX_MODE_FDD){
    prach_cfg_nr = *srsran_prach_nr_get_cfg_fr1_paired(cfg_idx);
    sul_idx = 0;
  }else if (bands.get_duplex_mode(band) == SRSRAN_DUPLEX_MODE_SUL){
    sul_idx = 1;
  }
  t_idx.reserve(prach_cfg_nr.nof_subframe_number);
  ra_rnti = (uint16_t*) malloc(prach_cfg_nr.nof_subframe_number * 
    sizeof(uint16_t));
  // ra_rnti.reserve(prach_cfg_nr.nof_subframe_number);
  nof_ra_rnti = prach_cfg_nr.nof_subframe_number;
  // printf("rach_uplink.prach_cfg_nr.nof_subframe_number %u\n", 
  //  prach_cfg_nr.nof_subframe_number);

  // Set the config for prach_cfg
  prach_cfg.is_nr = true;
  prach_cfg.config_idx = cfg_idx;
  
  if(sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.rach_cfg_common.
     setup().prach_root_seq_idx.type() == 0) {
    // 0 as l839 and 1 as l139, check /lib/include/srsran/asn1/rrc_nr.h
    prach_cfg.root_seq_idx = sib1.serving_cell_cfg_common.ul_cfg_common.
      init_ul_bwp.rach_cfg_common.setup().prach_root_seq_idx.l839();
    for(uint32_t t_idx_id = 0; t_idx_id < prach_cfg_nr.nof_subframe_number; 
        t_idx_id++){
      t_idx[t_idx_id] = prach_cfg_nr.subframe_number[t_idx_id];
    }
  }else if(sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.
      rach_cfg_common.setup().prach_root_seq_idx.type() == 1) {
    // 0 as l839 and 1 as l139, check /lib/include/srsran/asn1/rrc_nr.h
    t_idx.reserve(prach_cfg_nr.nof_subframe_number * 
      SRSRAN_NSLOTS_PER_SF_NR(base_carrier.scs));
    ra_rnti = (uint16_t*) malloc(prach_cfg_nr.nof_subframe_number * 
      SRSRAN_NSLOTS_PER_SF_NR(base_carrier.scs) * sizeof(uint16_t));
    // ra_rnti.reserve(prach_cfg_nr.nof_subframe_number * SRSRAN_NSLOTS_PER_SF_NR(carrier_input.scs));
    nof_ra_rnti = prach_cfg_nr.nof_subframe_number * 
      SRSRAN_NSLOTS_PER_SF_NR(base_carrier.scs);
    prach_cfg.root_seq_idx = sib1.serving_cell_cfg_common.ul_cfg_common.
      init_ul_bwp.rach_cfg_common.setup().prach_root_seq_idx.l139();
    for(uint32_t t_idx_id = 0; t_idx_id < prach_cfg_nr.nof_subframe_number; 
        t_idx_id++){
      for(uint32_t slot_idx = 0; 
          slot_idx < SRSRAN_NSLOTS_PER_SF_NR(base_carrier.scs); slot_idx++){
        t_idx[t_idx_id*SRSRAN_NSLOTS_PER_SF_NR(base_carrier.scs) + slot_idx] = 
          prach_cfg_nr.subframe_number[t_idx_id] * 
          SRSRAN_NSLOTS_PER_SF_NR(base_carrier.scs) + slot_idx;
        // printf("prach_cfg_nr.subframe_number[t_idx_id] * SRSRAN_NSLOTS_PER_SF_NR(carrier_input.scs) + slot_idx: %u\n", prach_cfg_nr.subframe_number[t_idx_id] * SRSRAN_NSLOTS_PER_SF_NR(base_carrier.scs) + slot_idx);
      }
    }
  }else{
    ERROR("Invalid PRACH preamble type.");
  }
  prach_cfg.zero_corr_zone = sib1.serving_cell_cfg_common.ul_cfg_common.
    init_ul_bwp.rach_cfg_common.setup().rach_cfg_generic.zero_correlation_zone_cfg;
  prach_cfg.freq_offset = sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.
    rach_cfg_common.setup().rach_cfg_generic.msg1_freq_start;

  if(bands.get_duplex_mode(band) == SRSRAN_DUPLEX_MODE_TDD){
    prach_cfg.tdd_config.configured = true;
  }

  /* See 38.321, 5.1.3 - Random Access Preamble transmission.
  RA-RNTI = 1 + s_id + 14 × t_id + 14 × 80 × f_id + 14 × 80 × 8 × ul_carrier_id.
  s_id = index of the first OFDM symbol of the (first, for short formats) 
      PRACH occasion (0 <= s_id < 14).
  t_id = index of the first slot of the PRACH occasion in a system frame 
    (0 <= t_id < 80); the numerology of
  reference for t_id is 15kHz for long PRACH Formats, regardless of the SCS 
    common; whereas, for short PRACH formats,
  it coincides with SCS common (this can be inferred from Section 5.1.3, 
    TS 38.321, and from Section 5.3.2, TS 38.211). */
  // f_id = index of the PRACH occation in the freq domain (0 <= f_id < 8).
  // ul_carrier_id = 0 for NUL and 1 for SUL carrier.
  // printf("nof_ra_rnti: %u\n", nof_ra_rnti);
  for(uint32_t i = 0; i < nof_ra_rnti; i++){
    // printf("t_id[%u]: %d\n", i, t_idx[i]);
    ra_rnti[i] = 1 + prach_cfg_nr.starting_symbol + 
          14 * t_idx[i] + 
          14 * 80 * 0 + 14 * 80 * 8 * sul_idx;
    // printf("ra_rnti[%u]: %u\n", i, ra_rnti[i]);
  }  

  return SRSRAN_SUCCESS;
}

int RachDecoder::RACHReceptionInit(WorkState* state,
                                   cf_t* input[SRSRAN_MAX_PORTS]){
  memcpy(&coreset0_t, &state->coreset0_t, sizeof(srsran_coreset_t));

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
    search_space->nof_candidates[L] = 
      srsran_pdcch_nr_max_candidates_coreset(&coreset0_t, L);
  }
  pdcch_cfg.coreset[0] = coreset0_t;

  pdcch_cfg.search_space_present[0]      = true;
  pdcch_cfg.search_space[0].id           = 1;
  pdcch_cfg.search_space[0].coreset_id   = 0;
  pdcch_cfg.search_space[0].type         = srsran_search_space_type_common_1;
  pdcch_cfg.search_space[0].formats[0]   = srsran_dci_format_nr_1_0;
  pdcch_cfg.search_space[0].nof_formats  = 1;

  pdcch_cfg.coreset[0] = coreset0_t; 

  pdcch_cfg.search_space[0].nof_candidates[0] = 
    sib1.serving_cell_cfg_common.dl_cfg_common.
    init_dl_bwp.pdcch_cfg_common.setup().common_search_space_list[0].
    nrof_candidates.aggregation_level1;
  pdcch_cfg.search_space[0].nof_candidates[1] = 
    sib1.serving_cell_cfg_common.dl_cfg_common.
    init_dl_bwp.pdcch_cfg_common.setup().common_search_space_list[0].
    nrof_candidates.aggregation_level2;
  pdcch_cfg.search_space[0].nof_candidates[2] = 
    sib1.serving_cell_cfg_common.dl_cfg_common.
    init_dl_bwp.pdcch_cfg_common.setup().common_search_space_list[0].
    nrof_candidates.aggregation_level4;
  pdcch_cfg.search_space[0].nof_candidates[3] = 
    sib1.serving_cell_cfg_common.dl_cfg_common.
    init_dl_bwp.pdcch_cfg_common.setup().common_search_space_list[0].
    nrof_candidates.aggregation_level8;
  pdcch_cfg.search_space[0].nof_candidates[4] = 
    sib1.serving_cell_cfg_common.dl_cfg_common.
    init_dl_bwp.pdcch_cfg_common.setup().common_search_space_list[0].
    nrof_candidates.aggregation_level16;

  arg_scs = state->arg_scs;                                   
  memcpy(&base_carrier, &state->args_t.base_carrier, sizeof(srsran_carrier_nr_t));
  cell = state->cell;
  pdsch_hl_cfg.typeA_pos = cell.mib.dmrs_typeA_pos;
  for (uint32_t pdsch_time_id = 0; pdsch_time_id < 
      sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.
      setup().pdsch_time_domain_alloc_list.size(); pdsch_time_id++){
    if(sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.
        setup().pdsch_time_domain_alloc_list[pdsch_time_id].k0_present){
      pdsch_hl_cfg.common_time_ra[pdsch_time_id].k = 
        sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.
        setup().pdsch_time_domain_alloc_list[pdsch_time_id].k0;
    }
    pdsch_hl_cfg.common_time_ra[pdsch_time_id].sliv = sib1.
      serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.
      setup().pdsch_time_domain_alloc_list[pdsch_time_id].start_symbol_and_len;
    switch(sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.
        pdsch_cfg_common.setup().pdsch_time_domain_alloc_list[pdsch_time_id].
        map_type){
      case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::type_a:
        pdsch_hl_cfg.common_time_ra[pdsch_time_id].mapping_type = 
          srsran_sch_mapping_type_A;
        break;
      case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::type_b:
        pdsch_hl_cfg.common_time_ra[pdsch_time_id].mapping_type = 
          srsran_sch_mapping_type_B;
        break;
      case asn1::rrc_nr::pdsch_time_domain_res_alloc_s::map_type_e_::nulltype:
        break;
      default:
        break;
    }
  }
  pdsch_hl_cfg.nof_common_time_ra = sib1.serving_cell_cfg_common.dl_cfg_common.
    init_dl_bwp.pdsch_cfg_common.setup().pdsch_time_domain_alloc_list.size();

  ue_dl_args.nof_rx_antennas               = 1;
  ue_dl_args.pdsch.sch.disable_simd        = false;
  ue_dl_args.pdsch.sch.decoder_use_flooded = false;
  ue_dl_args.pdsch.measure_evm             = true;
  ue_dl_args.pdcch.disable_simd            = false;
  ue_dl_args.pdcch.measure_evm             = true;
  ue_dl_args.nof_max_prb                   = 275;  

  if (srsran_ue_dl_nr_init_nrscope(&ue_dl_rach, input, &ue_dl_args, arg_scs)) {
    ERROR("RACHDecoder -- Error UE DL");
    return SRSRAN_ERROR;
  }

  if (srsran_ue_dl_nr_set_carrier_nrscope(&ue_dl_rach, &base_carrier, arg_scs)) {
    ERROR("RACHDecoder -- Error setting SCH NR carrier");
    return SRSRAN_ERROR;
  }

  if (srsran_ue_dl_nr_set_pdcch_config(&ue_dl_rach, &pdcch_cfg, &dci_cfg)) {
    ERROR("RACHDecoder -- Error setting CORESET");
    return SRSRAN_ERROR;
  }

  if (srsran_softbuffer_rx_init_guru(&softbuffer, SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, 
      SRSRAN_LDPC_MAX_LEN_ENCODED_CB) < SRSRAN_SUCCESS) {
    ERROR("RACHDecoder -- Error init soft-buffer");
    return SRSRAN_ERROR;
  }

  pdsch_carrier = base_carrier;
  arg_scs_pdsch = arg_scs;

  double pointA = state->srsran_searcher_cfg_t.ssb_freq_hz - 
    (SRSRAN_SSB_BW_SUBC / 2) * cell.abs_ssb_scs - cell.k_ssb * 
    SRSRAN_SUBC_SPACING_NR(srsran_subcarrier_spacing_15kHz) - 
    sib1.serving_cell_cfg_common.dl_cfg_common.freq_info_dl.offset_to_point_a * 
    SRSRAN_SUBC_SPACING_NR(srsran_subcarrier_spacing_15kHz) * 
    NRSCOPE_NSC_PER_RB_NR;

  pdsch_carrier.nof_prb = sib1.serving_cell_cfg_common.dl_cfg_common.
    freq_info_dl.scs_specific_carrier_list[0].carrier_bw;
  double dl_center_frequency = pointA + pdsch_carrier.nof_prb * 
    NRSCOPE_NSC_PER_RB_NR * SRSRAN_SUBC_SPACING_NR(state->args_t.ssb_scs) / 2;
  std::cout << "dl_center_frequency: " << dl_center_frequency << std::endl;
  std::cout << "pointA: " << pointA << std::endl;

  arg_scs_pdsch.coreset_offset_scs = (state->srsran_searcher_cfg_t.ssb_freq_hz -
    dl_center_frequency) / cell.abs_pdcch_scs;
  
  // The lower boundary of PDSCH can be not aligned with the lower boundary of PDCCH
  if (srsran_ue_dl_nr_init_nrscope(&ue_dl_pdsch, input, &ue_dl_args, 
      arg_scs_pdsch)) {
    ERROR("Error UE DL");
    return SRSRAN_ERROR;
  }

  if (srsran_ue_dl_nr_set_carrier_nrscope(&ue_dl_pdsch, &pdsch_carrier, 
      arg_scs_pdsch)) {
    ERROR("Error setting SCH NR carrier");
    return SRSRAN_ERROR;
  }

  start_rb = (state->coreset0_args_t.coreset0_lower_freq_hz - pointA) / 
    SRSRAN_SUBC_SPACING_NR(arg_scs_pdsch.scs) / 12;

  if (srsran_ue_dl_nr_set_pdcch_config(&ue_dl_pdsch, &pdcch_cfg, &dci_cfg)) {
    ERROR("Error setting CORESET");
    return SRSRAN_ERROR;
  }

  return SRSRAN_SUCCESS;
}


int RachDecoder::DecodeandParseMS4fromSlot(srsran_slot_cfg_t* slot,
                                           WorkState* state,
                                           SlotResult* result){
  if(!state->sib1_found or !state->rach_inited){
    // If the SIB 1 is not detected or the RACH decoder is not initialized.
    std::cout << "SIB 1 not found or decoder not initialized, quitting..." 
      << std::endl;
    result->rach_result = false;
    return SRSRAN_SUCCESS;
  }
  
  result->rach_result = true;
  result->found_rach = false;
  result->new_rnti_number = 0;
  result->new_rntis_found.clear();

  uint16_t tc_rnti;
  uint16_t c_rnti;

  for (int dci_id = 0; dci_id < SRSRAN_MAX_DCI_MSG_NR; dci_id++){
    memset(&dci_rach[dci_id], 0, sizeof(srsran_dci_dl_nr_t));
  }
  // memset(&dci_rach, 0, sizeof(srsran_dci_dl_nr_t));

  srsran_ue_dl_nr_estimate_fft_nrscope(&ue_dl_rach, slot, arg_scs);
  srsran_ue_dl_nr_estimate_fft_nrscope(&ue_dl_pdsch, slot, arg_scs_pdsch);

  int nof_found_dci = srsran_ue_dl_nr_find_dl_dci_nrscope(&ue_dl_rach, 
    slot, ra_rnti, nof_ra_rnti, srsran_rnti_type_tc, dci_rach, 4);

  if (nof_found_dci < SRSRAN_SUCCESS) {
    ERROR("RACHDecoder -- Error in blind search");
    return SRSRAN_ERROR;
  }
  // for (uint32_t pdcch_idx = 0; pdcch_idx < ue_dl_rach.pdcch_info_count; pdcch_idx++) {
  //   const srsran_ue_dl_nr_pdcch_info_t* info = &(ue_dl_rach.pdcch_info[pdcch_idx]);
  //   if(info->result.crc){ // Only print the RSRP result when the DCI's CRC is correct.
  //     printf("PDCCH: %s-rnti=0x%x, crst_id=%d, ss_type=%s, ncce=%d, al=%d, EPRE=%+.2f, RSRP=%+.2f, corr=%.3f; "
  //       "nof_bits=%d; crc=%s;\n",
  //       srsran_rnti_type_str_short(info->dci_ctx.rnti_type),
  //       info->dci_ctx.rnti,
  //       info->dci_ctx.coreset_id,
  //       srsran_ss_type_str(info->dci_ctx.ss_type),
  //       info->dci_ctx.location.ncce,
  //       info->dci_ctx.location.L,
  //       info->measure.epre_dBfs,
  //       info->measure.rsrp_dBfs,
  //       info->measure.norm_corr,
  //       info->nof_bits,
  //       info->result.crc ? "OK" : "KO");
  //   }
  // }

  if (nof_found_dci < 1) {
    printf("RACHDecoder -- No DCI found :'(\n");
    return SRSRAN_ERROR;
  }

  for (int dci_id = 0; dci_id < nof_found_dci; dci_id++){
    char str[1024] = {};
    srsran_dci_dl_nr_to_str(&(ue_dl_rach.dci), &dci_rach[dci_id], str, 
      (uint32_t)sizeof(str));
    printf("RACHDecoder -- Found DCI: %s\n", str);
    tc_rnti = dci_rach[dci_id].ctx.rnti;

    if (state->rach_found) {
      result->found_rach = true;
      result->new_rnti_number += 1;
      result->new_rntis_found.emplace_back(tc_rnti);
      std::cout << "rach skip." << std::endl;
      continue;
    }

    srsran_sch_cfg_nr_t pdsch_cfg = {};
    dci_rach[dci_id].ctx.coreset_start_rb = start_rb;

    if (srsran_ra_dl_dci_to_grant_nr(&pdsch_carrier, slot, &pdsch_hl_cfg, 
        &dci_rach[dci_id], &pdsch_cfg, &pdsch_cfg.grant) < SRSRAN_SUCCESS) {
      ERROR("RACHDecoder -- Error decoding PDSCH search");
      return SRSRAN_ERROR;
    }

    srsran_sch_cfg_nr_info(&pdsch_cfg, str, (uint32_t)sizeof(str));
    printf("PDSCH_cfg:\n%s", str);

    if (srsran_softbuffer_rx_init_guru(&softbuffer, 
        SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, SRSRAN_LDPC_MAX_LEN_ENCODED_CB) <
        SRSRAN_SUCCESS) {
      ERROR("Error init soft-buffer");
      return SRSRAN_ERROR;
    }

    // Reset the data_pdcch to zeros
    srsran_vec_u8_zero(data_pdcch, SRSRAN_SLOT_MAX_NOF_BITS_NR);

    // Set softbuffer
    pdsch_cfg.grant.tb[0].softbuffer.rx = &softbuffer;

    // Prepare PDSCH result
    pdsch_res = {};
    pdsch_res.tb[0].payload = data_pdcch;

    // Decode PDSCH
    if (srsran_ue_dl_nr_decode_pdsch(&ue_dl_pdsch, slot, &pdsch_cfg, &pdsch_res)
        < SRSRAN_SUCCESS) {
      printf("Error decoding PDSCH search\n");
      return SRSRAN_ERROR;
    }

    // printf("Decoded PDSCH (%d B)\n", pdsch_cfg.grant.tb[0].tbs / 8);
    // srsran_vec_fprint_byte(stdout, pdsch_res.tb[0].payload, 
    //    pdsch_cfg.grant.tb[0].tbs / 8);
    uint32_t bytes_offset = 0;

    for (uint32_t pdsch_res_idx = 0; pdsch_res_idx < 
         (uint32_t)pdsch_cfg.grant.tb[0].tbs / 8 - 1; pdsch_res_idx ++){
      if(pdsch_res.tb[0].payload[pdsch_res_idx] == 0x20 && pdsch_res.tb[0].
          payload[pdsch_res_idx+1] == 0x40){
        bytes_offset = pdsch_res_idx;
        break;
      }
    }

    if (!pdsch_res.tb[0].crc) {
      printf("RACHDecoder -- Error decoding PDSCH (CRC)\n");

      return SRSRAN_ERROR;
    }

    // Check payload is not all null.
    bool all_zero = true;
    for (int i = 0; i < pdsch_cfg.grant.tb[0].tbs / 8; ++i) {
      if (pdsch_res.tb[0].payload[i] != 0x0) {
        all_zero = false;
        break;
      }
    }
    if (all_zero) {
      ERROR("RACHDecoder -- PDSCH payload is all zeros");
      return SRSRAN_ERROR;
    }

    if(pdsch_cfg.grant.tb[0].tbs / 8 < 40){
      ERROR("Too short for RRC Setup");
      return SRSRAN_ERROR;
    }

    std::cout << "Decoding Msg 4..." << std::endl;
    asn1::rrc_nr::dl_ccch_msg_s dlcch_msg;
    /* What the first few bytes are? In srsgNB there are 10 extra bytes and for 
      small cell there are 3 extra bytes before the RRCSetup message. */
    asn1::cbit_ref dlcch_bref(pdsch_res.tb[0].payload + bytes_offset, 
      pdsch_cfg.grant.tb[0].tbs / 8 - bytes_offset);
    asn1::SRSASN_CODE err = dlcch_msg.unpack(dlcch_bref);
    if (err != asn1::SRSASN_SUCCESS) {
      ERROR("Failed to unpack DL-CCCH message (%d B)", 
        pdsch_cfg.grant.tb[0].tbs / 8 - bytes_offset);
    }

    result->rrc_setup = dlcch_msg.msg.c1().rrc_setup();
    std::cout << "Msg 4 Decoded." << std::endl;
    switch (dlcch_msg.msg.c1().type().value) {
      case asn1::rrc_nr::dl_ccch_msg_type_c::c1_c_::types::rrc_reject: {
        std::cout << "Unfortunately, it's a rrc_reject ;(" << std::endl;
        return SRSRAN_SUCCESS; // We need to search for RRCSetup.
      }break;
      case asn1::rrc_nr::dl_ccch_msg_type_c::c1_c_::types::rrc_setup: {
        std::cout << "It's a rrc_setup, hooray!" << std::endl;
        printf("rrc-TransactionIdentifier: %u\n", 
          (result->rrc_setup).rrc_transaction_id);
        result->found_rach = true;
      }break;
      default: {
        std::cout << "None detected, skip." << std::endl;
        return SRSRAN_ERROR;
      }
      break;
    }

    // asn1::json_writer js_msg4;
    // task_scheduler_nrscope->rrc_setup.to_json(js_msg4);
    // printf("rrcSetup content: %s\n", js_msg4.to_string().c_str());
    asn1::cbit_ref bref_cg((result->rrc_setup).crit_exts.rrc_setup().
      master_cell_group.data(), (result->rrc_setup).crit_exts.rrc_setup().
      master_cell_group.size());
    if ((result->master_cell_group).unpack(bref_cg) != asn1::SRSASN_SUCCESS) {
      ERROR("Could not unpack master cell group config.");
      return SRSRAN_ERROR;
    }        
    
    asn1::json_writer js;
    result->master_cell_group.to_json(js);
    printf("masterCellGroup: %s\n", js.to_string().c_str());

    if (!(result->master_cell_group).sp_cell_cfg.recfg_with_sync.new_ue_id){
      c_rnti = tc_rnti;
    }else{
      c_rnti = result->master_cell_group.sp_cell_cfg.recfg_with_sync.new_ue_id;
    }
    // c_rnti = tc_rnti;
    std::cout << "c-rnti: " << c_rnti << std::endl;

    /* Add the new rntis into a different list and update the 
      known_rnti vector in the end of the threads. */
    result->new_rnti_number += 1;
    result->new_rntis_found.emplace_back(c_rnti);
    // task_scheduler_nrscope->nof_known_rntis += 1;
    // task_scheduler_nrscope->known_rntis.emplace_back(c_rnti);

    srsran_softbuffer_rx_free(&softbuffer);
  }

  return SRSRAN_SUCCESS;
}