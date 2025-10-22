#include "nrscope/hdr/sibs_decoder.h"

SIBsDecoder::SIBsDecoder(){
  data_pdcch = srsran_vec_u8_malloc(SRSRAN_SLOT_MAX_NOF_BITS_NR);
  if (data_pdcch == NULL) {
    ERROR("Error malloc");
  }
}

SIBsDecoder::~SIBsDecoder(){
    
}

int SIBsDecoder::SIBDecoderandReceptionInit(WorkState* state,
                                            cf_t* input[SRSRAN_MAX_PORTS]){  
  std::cout << "Initializing SIB decoder" << std::endl;
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

  arg_scs = state->arg_scs;
  memcpy(&base_carrier, &state->args_t.base_carrier, sizeof(srsran_carrier_nr_t));
  cell = state->cell;
  pdsch_hl_cfg.typeA_pos = cell.mib.dmrs_typeA_pos;

  ue_dl_args.nof_rx_antennas               = 1;
  ue_dl_args.pdsch.sch.disable_simd        = false;
  ue_dl_args.pdsch.sch.decoder_use_flooded = false;
  ue_dl_args.pdsch.measure_evm             = true;
  ue_dl_args.pdcch.disable_simd            = false;
  ue_dl_args.pdcch.measure_evm             = true;
  ue_dl_args.nof_max_prb                   = 275;

  ue_dl_args.pdcch_dmrs_corr_thr           = 0.05;

  if (srsran_ue_dl_nr_init_nrscope(&ue_dl_sibs, input, &ue_dl_args, arg_scs)) {
    ERROR("Error UE DL");
    return SRSRAN_ERROR;
  }

  if (srsran_ue_dl_nr_set_carrier_nrscope(&ue_dl_sibs, &base_carrier, arg_scs)){
    ERROR("Error setting SCH NR carrier");
    return SRSRAN_ERROR;
  }

  if (srsran_ue_dl_nr_set_pdcch_config(&ue_dl_sibs, &pdcch_cfg, &dci_cfg)) {
    ERROR("Error setting CORESET");
    return SRSRAN_ERROR;
  }

  if (srsran_softbuffer_rx_init_guru(&softbuffer, 
      SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, SRSRAN_LDPC_MAX_LEN_ENCODED_CB) <
      SRSRAN_SUCCESS) {
    ERROR("Error init soft-buffer");
    return SRSRAN_ERROR;
  }

  // task_scheduler_nrscope->sib1_inited = true;
  std::cout << "SIB Decoder Initialized.." << std::endl;

  return SRSRAN_SUCCESS;
}

int SIBsDecoder::DecodeandParseSIB1fromSlot(srsran_slot_cfg_t* slot,
                                            WorkState* state,
                                            SlotResult* result) {
  if (state->all_sibs_found) {
    result->sib_result = false;
    return SRSRAN_SUCCESS;
  }
  
  /* reset the result's elements */
  result->sib_result = true;
  result->found_sib1 = false;
  result->found_sib.clear();
  result->sibs.clear();

  // uint8_t hardcoded_sib1[111] = {0x6e, 0x88, 0x18, 0x0a, 0x09, 0x88, 0x49, 
  //   0x81, 0x5b, 0x98, 0x00, 0x51, 0x43, 0x68, 0x00, 0x68, 0x26, 0x23, 0x49, 
  //   0x05, 0x6e, 0x60, 0x01, 0x45, 0x0d, 0xa0, 0x01, 0x00, 0x0c, 0x50, 0x01, 
  //   0x08, 0x30, 0xc0, 0x4a, 0x08, 0x46, 0x01, 0x40, 0x00, 0x00, 0x4e, 0x65, 
  //   0x3c, 0xa1, 0x0f, 0x9b, 0x82, 0x01, 0x00, 0x00, 0x00, 0x84, 0x00, 0x02, 
  //   0x08, 0x34, 0x20, 0x21, 0x06, 0x10, 0x01, 0x9c, 0x21, 0x18, 0x20, 0x64, 
  //   0x60, 0x00, 0x00, 0x4e, 0xd5, 0xca, 0x79, 0x42, 0xd2, 0x11, 0x00, 0x78, 
  //   0xc0, 0xd4, 0x41, 0x99, 0x00, 0x5f, 0xcb, 0xce, 0x08, 0xdc, 0x21, 0xb8, 
  //   0x63, 0x71, 0x06, 0xfb, 0x65, 0xf1, 0x21, 0x16, 0x3c, 0x40, 0x54, 0x55, 
  //   0xee, 0x43, 0x14, 0x41, 0x0e, 0x00, 0x00, 0x00};
  
  memset(&dci_sibs, 0, sizeof(srsran_dci_dl_nr_t));

  // FILE *fp;
  // fp = fopen("/home/wanhr/Documents/codes/cpp/srsRAN_4G/build/srsue/src/SIB_debug.txt", "r");
  // fseek(fp, file_position * sizeof(cf_t), SEEK_SET);
  // uint32_t a = fread(ue_dl.fft[0].cfg.in_buffer, sizeof(cf_t), ue_dl.fft[0].sf_sz, fp);
  // uint32_t a = fread(ue_dl_sibs.fft[0].cfg.in_buffer, sizeof(cf_t), ue_dl_sibs.fft[0].sf_sz, fp);
  // file_position += ue_dl_sibs.fft[0].sf_sz;
  
  // Check the fft plan and how does it manipulate the buffer
  srsran_ue_dl_nr_estimate_fft_nrscope(&ue_dl_sibs, slot, arg_scs);
  // Blind search
  int nof_found_dci = srsran_ue_dl_nr_find_dl_dci(&ue_dl_sibs, slot, 0xFFFF, 
    srsran_rnti_type_si, &dci_sibs, 1);
  if (nof_found_dci < SRSRAN_SUCCESS){
    ERROR("SIBDecoder -- Error in blind search");
    return SRSRAN_ERROR;
  }
  /* Print PDCCH blind search candidates */
  for (uint32_t pdcch_idx = 0; pdcch_idx < ue_dl_sibs.pdcch_info_count; pdcch_idx++) {
    const srsran_ue_dl_nr_pdcch_info_t* info = &(ue_dl_sibs.pdcch_info[pdcch_idx]);
    printf("PDCCH: %s-rnti=0x%x, crst_id=%d, ss_type=%s, ncce=%d, al=%d, EPRE=%+.2f, RSRP=%+.2f, corr=%.3f; "
    "nof_bits=%d; crc=%s;\n",
    srsran_rnti_type_str_short(info->dci_ctx.rnti_type),
    info->dci_ctx.rnti,
    info->dci_ctx.coreset_id,
    srsran_ss_type_str(info->dci_ctx.ss_type),
    info->dci_ctx.location.ncce,
    info->dci_ctx.location.L,
    info->measure.epre_dBfs,
    info->measure.rsrp_dBfs,
    info->measure.norm_corr,
    info->nof_bits,
    info->result.crc ? "OK" : "KO");
  }
  if (nof_found_dci < 1) {
    printf("SIBDecoder -- No DCI found :'(\n");
    return SRSRAN_ERROR;
  }

  char str[1024] = {};
  srsran_dci_dl_nr_to_str(&(ue_dl_sibs.dci), &dci_sibs, str, 
    (uint32_t)sizeof(str));
  printf("SIBDecoder -- Found DCI: %s\n", str);

  pdsch_cfg = {};
  pdsch_cfg.dmrs.typeA_pos = state->cell.mib.dmrs_typeA_pos;

  if (srsran_ra_dl_dci_to_grant_nr(&base_carrier, slot, &pdsch_hl_cfg, 
      &dci_sibs, &pdsch_cfg, &pdsch_cfg.grant) < SRSRAN_SUCCESS) {
    ERROR("SIBDecoder -- Error decoding PDSCH search");
    return SRSRAN_ERROR;
  }

  srsran_sch_cfg_nr_info(&pdsch_cfg, str, (uint32_t)sizeof(str));
  printf("PDSCH_cfg:\n%s", str);

  if (srsran_softbuffer_rx_init_guru(&softbuffer, SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, 
      SRSRAN_LDPC_MAX_LEN_ENCODED_CB) < SRSRAN_SUCCESS) {
    ERROR("SIBDecoder -- Error init soft-buffer");
    return SRSRAN_ERROR;
  }

  // Reset the data_pdcch to zeros
  srsran_vec_u8_zero(data_pdcch, SRSRAN_SLOT_MAX_NOF_BITS_NR);
  
  pdsch_cfg.grant.tb[0].softbuffer.rx = &softbuffer; // Set softbuffer
  pdsch_res = {}; // Prepare PDSCH result
  pdsch_res.tb[0].payload = data_pdcch;

  // Decode PDSCH
  if (srsran_ue_dl_nr_decode_pdsch(&ue_dl_sibs, slot, &pdsch_cfg, &pdsch_res) < 
      SRSRAN_SUCCESS) {
    printf("SIBDecoder -- Error decoding PDSCH search\n");
    return SRSRAN_ERROR;
  }
  if (!pdsch_res.tb[0].crc) {
    printf("SIBDecoder -- Error decoding PDSCH (CRC)\n");
    return SRSRAN_ERROR;
  }
  // printf("Decoded PDSCH (%d B)\n", pdsch_cfg.grant.tb[0].tbs / 8);
  // srsran_vec_fprint_byte(stdout, pdsch_res.tb[0].payload, 
  // pdsch_cfg.grant.tb[0].tbs / 8);

   // check payload is not all null
  bool all_zero = true;
  for (int i = 0; i < pdsch_cfg.grant.tb[0].tbs / 8; ++i) {
    if (pdsch_res.tb[0].payload[i] != 0x0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) {
    ERROR("PDSCH payload is all zeros");
    return SRSRAN_ERROR;
  }
  std::cout << "Try to decode SIBs..." << std::endl;
  asn1::rrc_nr::bcch_dl_sch_msg_s dlsch_msg;
  asn1::cbit_ref dlsch_bref(pdsch_res.tb[0].payload, 
    pdsch_cfg.grant.tb[0].tbs / 8);
  asn1::SRSASN_CODE err = dlsch_msg.unpack(dlsch_bref);

  /* Try to decode the SIB, we need a better way to provide the result */
  if(srsran_unlikely(asn1::rrc_nr::bcch_dl_sch_msg_type_c::c1_c_::types_opts::
      sib_type1 != dlsch_msg.msg.c1().type())){
    // Try to decode other SIBs
    // Get the sib_id, sib_id is uint8_t and is 2 for sib2, 3 for sib 3, etc...
    auto sib_id = dlsch_msg.msg.c1().sys_info().crit_exts.sys_info().
      sib_type_and_info[0].type().to_number();
    auto decoded_sib = dlsch_msg.msg.c1().sys_info();
    result->sibs.emplace_back(decoded_sib);
    result->found_sib.emplace_back(sib_id);
    // sibs[sib_id - 2] = dlsch_msg.msg.c1().sys_info();
    // found_sib[sib_id - 2] = 1;

    // If we collect all the SIBs, we can skip the thread.
    // long unsigned int found_result = 0;
    // for(long unsigned int i=0; i<found_sib.size(); i++){
    //   found_result += found_sib[i];
    // }
    // if(found_result >= found_sib.size()){
    //   *all_sibs_found = true;
    // }

    std::cout << "SIB " << (int)sib_id << " Decoded." << std::endl;
    /* Uncomment to print the decode SIBs. */
    asn1::json_writer js_sibs;
    (decoded_sib).to_json(js_sibs);
    printf("Decoded SIBs: %s\n", js_sibs.to_string().c_str()); 
  }else if(srsran_unlikely(asn1::rrc_nr::bcch_dl_sch_msg_type_c::c1_c_::
      types_opts::sys_info != dlsch_msg.msg.c1().type())){
    result->found_sib1 = true;
    result->sib1 = dlsch_msg.msg.c1().sib_type1();
    std::cout << "SIB 1 Decoded." << std::endl;

    /* Uncomment to print the decode SIB1. */
    asn1::json_writer js_sib1;
    (result->sib1).to_json(js_sib1);
    printf("Decoded SIB1: %s\n", js_sib1.to_string().c_str());
  }

  srsran_softbuffer_rx_free(&softbuffer);

  return SRSRAN_SUCCESS;
}
