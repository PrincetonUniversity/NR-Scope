#include "nrscope/hdr/dci_decoder.h"

DCIDecoder::DCIDecoder(uint32_t max_nof_rntis)
{
  ue_dl_tmp = (srsran_ue_dl_nr_t*)malloc(sizeof(srsran_ue_dl_nr_t));
  slot_tmp  = (srsran_slot_cfg_t*)malloc(sizeof(srsran_slot_cfg_t));

  dci_dl = (srsran_dci_dl_nr_t*)malloc(sizeof(srsran_dci_dl_nr_t) * (max_nof_rntis));
  dci_ul = (srsran_dci_ul_nr_t*)malloc(sizeof(srsran_dci_ul_nr_t) * (max_nof_rntis));

  // Zero the grant-conversion configs. 
  // This caused undefined behavior that manifested as arbitrary 
  // invalid decodes due to srsran_ra_dl_nr_time reading uninitialized values 
  // from dedicated_time_ra/nof_dedicated_time_ra.
  pdsch_hl_cfg = {};
  pusch_hl_cfg = {};
}

DCIDecoder::~DCIDecoder() {}

int DCIDecoder::DCIDecoderandReceptionInit(WorkState* state, int bwp_id, cf_t* input[SRSRAN_MAX_PORTS])
{
  memcpy(&base_carrier, &state->args_t.base_carrier, sizeof(srsran_carrier_nr_t));

  rrc_recfg_user = state->rrc_recfg_user;

  arg_scs = state->arg_scs;
  cell    = state->cell;

  bwp_worker_id = bwp_id;

  ue_dl_args.nof_rx_antennas               = 1;
  ue_dl_args.pdsch.sch.disable_simd        = false;
  ue_dl_args.pdsch.sch.decoder_use_flooded = false;
  ue_dl_args.pdsch.measure_evm             = true;
  ue_dl_args.pdcch.disable_simd            = false;
  ue_dl_args.pdcch.measure_evm             = true;
  ue_dl_args.nof_max_prb                   = 275;

  ue_dl_args.pdcch_dmrs_corr_thr = 0.05;

  memcpy(&coreset0_t, &state->coreset0_t, sizeof(srsran_coreset_t));
  sib1              = state->sib1;
  master_cell_group = state->master_cell_group;
  rrc_setup         = state->rrc_setup;

  dci_cfg.bwp_dl_initial_bw   = 275;
  dci_cfg.bwp_ul_initial_bw   = 275;
  dci_cfg.bwp_dl_active_bw    = 275;
  dci_cfg.bwp_ul_active_bw    = 275;
  dci_cfg.monitor_common_0_0  = true;
  dci_cfg.monitor_0_0_and_1_0 = true;
  dci_cfg.monitor_0_1_and_1_1 = true;
  // set coreset0 bandwidth
  dci_cfg.coreset0_bw = srsran_coreset_get_bw(&coreset0_t);

  pdcch_cfg.coreset_present[0]      = true;
  search_space                      = &pdcch_cfg.search_space[0];
  pdcch_cfg.search_space_present[0] = true;
  search_space->id                  = 0;
  search_space->coreset_id          = 0;
  search_space->type                = srsran_search_space_type_common_0;
  search_space->formats[0]          = srsran_dci_format_nr_1_0;
  search_space->nof_formats         = 1;
  for (uint32_t L = 0; L < SRSRAN_SEARCH_SPACE_NOF_AGGREGATION_LEVELS_NR; L++) {
    search_space->nof_candidates[L] = srsran_pdcch_nr_max_candidates_coreset(&coreset0_t, L);
  }
  pdcch_cfg.coreset[0] = coreset0_t;

  // Check UL and DL bwp separately.
  if (bwp_id == 0 && master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp_present) {
    /* For initial_dl_bwp */
    bwp_dl_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp);
  } else if (bwp_id <= 3 && !master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp_present) {
    /* No initial_dl_bwp, bwp_id 0 is for the first bwp in the list */
    for (uint8_t i = 0; i < master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list.size(); i++) {
      if (bwp_id + 1 == master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list[i].bwp_id) {
        if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list[i].bwp_ded_present) {
          bwp_dl_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list[i].bwp_ded);
          break;
        } else {
          printf("bwp id %u does not have a ded dl config in RRCSetup", bwp_id);
        }
      }
    }
  } else if (bwp_id <= 3 && master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp_present) {
    /* both intial bwp and the list exists*/
    for (uint8_t i = 0; i < master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list.size(); i++) {
      if (bwp_id == master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list[i].bwp_id) {
        if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list[i].bwp_ded_present) {
          bwp_dl_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list[i].bwp_ded);
          break;
        } else {
          printf("bwp id %u does not have a ded dl config in RRCSetup", bwp_id);
        }
      }
    }
  } else {
    ERROR("bwp id cannot be greater than 3!\n");
    return SRSRAN_ERROR;
  }

  if (bwp_id == 0 && master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp_present) {
    bwp_ul_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp);
  } else if (bwp_id <= 3 && !master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp_present) {
    for (uint8_t i = 0; i < master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list.size(); i++) {
      if (bwp_id + 1 == master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list[i].bwp_id) {
        if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list[i].bwp_ded_present) {
          bwp_ul_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list[i].bwp_ded);
          break;
        } else {
          printf("bwp id %u does not have a ded ul config in RRCSetup", bwp_id);
        }
      }
    }
  } else if (bwp_id <= 3 && master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp_present) {
    for (uint8_t i = 0; i < master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list.size(); i++) {
      if (bwp_id == master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list[i].bwp_id) {
        if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list[i].bwp_ded_present) {
          bwp_ul_ded_s_ptr = &(master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list[i].bwp_ded);
          break;
        } else {
          printf("bwp id %u does not have a ded ul config in RRCSetup", bwp_id);
        }
      }
    }

  } else {
    ERROR("bwp id cannot be greater than 3!\n");
    return SRSRAN_ERROR;
  }

  if (bwp_dl_ded_s_ptr == NULL || bwp_ul_ded_s_ptr == NULL) {
    ERROR("bwp id %d ul or dl config never appears in RRCSetup (what we assume "
          "now only checking in RRCSetup). Currently please bring back nof_bwps"
          " back to 1 in config.yaml as we are working on encrypted"
          "RRCReconfiguration-based BWP config monitoring.\n",
          bwp_id);
    return SRSRAN_ERROR;
  }

  pdcch_cfg.ra_search_space_present = false;

  if (bwp_dl_ded_s_ptr->pdcch_cfg.is_setup()) {
    for (uint32_t ss_id = 0; ss_id < bwp_dl_ded_s_ptr->pdcch_cfg.setup().search_spaces_to_add_mod_list.size();
         ++ss_id) {
      pdcch_cfg.search_space_present[ss_id] = true;
      pdcch_cfg.search_space[ss_id].id =
          bwp_dl_ded_s_ptr->pdcch_cfg.setup().search_spaces_to_add_mod_list[ss_id].search_space_id;
      pdcch_cfg.search_space[ss_id].coreset_id =
          bwp_dl_ded_s_ptr->pdcch_cfg.setup().search_spaces_to_add_mod_list[ss_id].ctrl_res_set_id;

      // printf("pdcch_cfg.search_space[%d].coreset_id in bwp%u: %u\n", pdcch_cfg.search_space[ss_id].id,
      // bwp_id, pdcch_cfg.search_space[ss_id].coreset_id);

      pdcch_cfg.search_space[ss_id].type = srsran_search_space_type_ue;
      if (bwp_dl_ded_s_ptr->pdcch_cfg.setup()
              .search_spaces_to_add_mod_list[ss_id]
              .search_space_type.ue_specific()
              .dci_formats.formats0_minus1_and_minus1_minus1) {
        pdcch_cfg.search_space[ss_id].formats[0] = srsran_dci_format_nr_1_1;
        pdcch_cfg.search_space[ss_id].formats[1] = srsran_dci_format_nr_0_1;
        dci_cfg.monitor_0_0_and_1_0              = false;
        dci_cfg.monitor_common_0_0               = false;
      } else if (bwp_dl_ded_s_ptr->pdcch_cfg.setup()
                     .search_spaces_to_add_mod_list[ss_id]
                     .search_space_type.ue_specific()
                     .dci_formats.formats0_minus0_and_minus1_minus0) {
        pdcch_cfg.search_space[ss_id].formats[0] = srsran_dci_format_nr_1_0;
        pdcch_cfg.search_space[ss_id].formats[1] = srsran_dci_format_nr_0_0;
        dci_cfg.monitor_0_1_and_1_1              = false;
      }
      pdcch_cfg.search_space[ss_id].nof_formats = 2;
    }
  } else {
    // Use some default settings
    pdcch_cfg.search_space[0].id          = 2;
    pdcch_cfg.search_space[0].coreset_id  = 1;
    pdcch_cfg.search_space[0].type        = srsran_search_space_type_ue;
    pdcch_cfg.search_space[0].formats[0]  = srsran_dci_format_nr_1_1;
    pdcch_cfg.search_space[0].formats[1]  = srsran_dci_format_nr_0_1;
    dci_cfg.monitor_0_0_and_1_0           = false;
    dci_cfg.monitor_common_0_0            = false;
    pdcch_cfg.search_space[0].nof_formats = 2;
  }
  pdcch_cfg.coreset[0] = coreset0_t;

  // all the Coreset information is from RRCSetup
  for (uint32_t crst_id = 0; crst_id < bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list.size();
       crst_id++) {
    srsran_coreset_t coreset_n;
    coreset_n.id = bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list[crst_id].ctrl_res_set_id;

    printf("to addmod coreset_n.id in bwp0: %u\n", coreset_n.id);
    coreset_n.duration = bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list[crst_id].dur;
    for (int i = 0; i < 45; i++) {
      coreset_n.freq_resources[i] =
          bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list[crst_id].freq_domain_res.get(45 - i - 1);
    }
    coreset_n.offset_rb = 0;
    if (bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list[crst_id].precoder_granularity ==
        asn1::rrc_nr::ctrl_res_set_s::precoder_granularity_opts::same_as_reg_bundle) {
      coreset_n.precoder_granularity = srsran_coreset_precoder_granularity_reg_bundle;
    } else if (bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list[crst_id].precoder_granularity ==
               asn1::rrc_nr::ctrl_res_set_s::precoder_granularity_opts::all_contiguous_rbs) {
      coreset_n.precoder_granularity = srsran_coreset_precoder_granularity_contiguous;
    }

    if (bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list[crst_id].cce_reg_map_type.type() ==
            asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::types_opts::non_interleaved ||
        bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list[crst_id].cce_reg_map_type.type() ==
            asn1::rrc_nr::ctrl_res_set_s::cce_reg_map_type_c_::types_opts::nulltype) {
      coreset_n.mapping_type     = srsran_coreset_mapping_type_non_interleaved;
      coreset_n.interleaver_size = srsran_coreset_bundle_size_n2; // doesn't matter, fill a random value
      coreset_n.shift_index      = 0;                             // doesn't matter, fill a random value
      coreset_n.reg_bundle_size  = srsran_coreset_bundle_size_n6; // doesen't matter, fill a random value
    } else {
      coreset_n.mapping_type = srsran_coreset_mapping_type_interleaved;
      switch (bwp_dl_ded_s_ptr->pdcch_cfg.setup()
                  .ctrl_res_set_to_add_mod_list[crst_id]
                  .cce_reg_map_type.interleaved()
                  .interleaver_size) {
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
      coreset_n.shift_index = bwp_dl_ded_s_ptr->pdcch_cfg.setup()
                                  .ctrl_res_set_to_add_mod_list[crst_id]
                                  .cce_reg_map_type.interleaved()
                                  .shift_idx;
      switch (bwp_dl_ded_s_ptr->pdcch_cfg.setup()
                  .ctrl_res_set_to_add_mod_list[crst_id]
                  .cce_reg_map_type.interleaved()
                  .reg_bundle_size) {
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
    coreset_n.dmrs_scrambling_id_present =
        bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list[crst_id].pdcch_dmrs_scrambling_id_present;
    if (coreset_n.dmrs_scrambling_id_present) {
      coreset_n.dmrs_scrambling_id =
          bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list[crst_id].pdcch_dmrs_scrambling_id;
    }
    printf("coreset_dmrs_scrambling id: %u\n", coreset_n.dmrs_scrambling_id);

    pdcch_cfg.coreset[coreset_n.id]         = coreset_n;
    pdcch_cfg.coreset_present[coreset_n.id] = true;

    char coreset_info[512] = {};
    srsran_coreset_to_str(&coreset_n, coreset_info, sizeof(coreset_info));
    printf("Coreset %d parameter: %s", coreset_n.id, coreset_info);

    if (crst_id == 0) {
      coreset1_t = coreset_n;
    } else {
      ERROR("Unhandled situation for CORESET, please raise an issue!");
    }
  }

  // For FR1 offset_to_point_a uses prbs with 15kHz scs.
  srsran_searcher_cfg_t = state->srsran_searcher_cfg_t;
  double pointA         = srsran_searcher_cfg_t.ssb_freq_hz - (SRSRAN_SSB_BW_SUBC / 2) * cell.abs_ssb_scs -
                  cell.k_ssb * SRSRAN_SUBC_SPACING_NR(srsran_subcarrier_spacing_15kHz) -
                  sib1.serving_cell_cfg_common.dl_cfg_common.freq_info_dl.offset_to_point_a *
                      SRSRAN_SUBC_SPACING_NR(srsran_subcarrier_spacing_15kHz) * NRSCOPE_NSC_PER_RB_NR;
  // std::cout << "pointA: " << pointA << std::endl;

  double coreset1_center_freq_hz =
      pointA + srsran_coreset_get_bw(&coreset1_t) / 2 * cell.abs_pdcch_scs * NRSCOPE_NSC_PER_RB_NR;
  // std::cout << "previous offset: " << arg_scs.coreset_offset_scs << std::endl;
  arg_scs.coreset_offset_scs = (base_carrier.ssb_center_freq_hz - coreset1_center_freq_hz) / cell.abs_pdcch_scs;
  // std::cout << "current offset: " << arg_scs.coreset_offset_scs << std::endl;
  // std::cout << "bwp_id: " << bwp_id << std::endl;

  // set ra search space directly from the RRC Setup
  for (uint32_t ss_id = 0; ss_id < bwp_dl_ded_s_ptr->pdcch_cfg.setup().search_spaces_to_add_mod_list.size(); ss_id++) {
    pdcch_cfg.search_space[ss_id].nof_candidates[0] =
        bwp_dl_ded_s_ptr->pdcch_cfg.setup().search_spaces_to_add_mod_list[ss_id].nrof_candidates.aggregation_level1;
    pdcch_cfg.search_space[ss_id].nof_candidates[1] =
        bwp_dl_ded_s_ptr->pdcch_cfg.setup().search_spaces_to_add_mod_list[ss_id].nrof_candidates.aggregation_level2;
    pdcch_cfg.search_space[ss_id].nof_candidates[2] =
        bwp_dl_ded_s_ptr->pdcch_cfg.setup().search_spaces_to_add_mod_list[ss_id].nrof_candidates.aggregation_level4;
    pdcch_cfg.search_space[ss_id].nof_candidates[3] =
        bwp_dl_ded_s_ptr->pdcch_cfg.setup().search_spaces_to_add_mod_list[ss_id].nrof_candidates.aggregation_level8;
    pdcch_cfg.search_space[ss_id].nof_candidates[4] =
        bwp_dl_ded_s_ptr->pdcch_cfg.setup().search_spaces_to_add_mod_list[ss_id].nrof_candidates.aggregation_level16;
    // printf("ss_id: %d, l1: %d, l2: %d, l3: %d, l4: %d, l5: %d\n",
    //   pdcch_cfg.search_space[ss_id].id,
    //   pdcch_cfg.search_space[ss_id].nof_candidates[0],
    //   pdcch_cfg.search_space[ss_id].nof_candidates[1],
    //   pdcch_cfg.search_space[ss_id].nof_candidates[2],
    //   pdcch_cfg.search_space[ss_id].nof_candidates[3],
    //   pdcch_cfg.search_space[ss_id].nof_candidates[4]
    // );
  }

  /* if the supplementary_ul in sp_cell_cfg_ded is present. */
  dci_cfg.enable_sul = false;
  if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.supplementary_ul_present) {
    dci_cfg.enable_sul = true;
  }

  dci_cfg.enable_hopping = false; // if the setting is absent, it's false.
  if (bwp_ul_ded_s_ptr->pusch_cfg.setup().freq_hop_present) {
    dci_cfg.enable_hopping = true;
  }

  /// Format 0_1 specific configuration (for PUSCH only)
  ///< Number of UL BWPs excluding the initial UL BWP, mentioned in the TS as N_BWP_RRC
  dci_cfg.nof_ul_bwp = master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.ul_bwp_to_add_mod_list.size();
  ///< Number of dedicated PUSCH time domain resource assigment, set to 0 for default
  dci_cfg.nof_ul_time_res =
      bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list_present
          ? bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup().size()
          : (sib1.serving_cell_cfg_common.ul_cfg_common_present
                 ? (sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common_present
                        ? sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup()
                              .pusch_time_domain_alloc_list.size()
                        : 0)
                 : 0);
  ///< Number of configured SRS resources
  dci_cfg.nof_srs =
      bwp_ul_ded_s_ptr->srs_cfg_present ? bwp_ul_ded_s_ptr->srs_cfg.setup().srs_res_to_add_mod_list.size() : 0;

  ///< Set to the maximum number of layers for PUSCH
  if (bwp_ul_ded_s_ptr->pusch_cfg.setup().max_rank_present) {
    dci_cfg.nof_ul_layers = bwp_ul_ded_s_ptr->pusch_cfg.setup().max_rank;
  } else {
    dci_cfg.nof_ul_layers = 1;
  }

  if (rrc_recfg_user.recfg_dci_cfg.count("nof_ul_layers")) {
    dci_cfg.nof_ul_layers = stoi(rrc_recfg_user.recfg_dci_cfg["nof_ul_layers"]);
  }

  ///< determined by maxCodeBlockGroupsPerTransportBlock for PUSCH
  if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup().code_block_group_tx_present) {
    switch (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup()
                .code_block_group_tx.setup()
                .max_code_block_groups_per_transport_block) {
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

  if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.csi_meas_cfg_present) {
    dci_cfg.report_trigger_size =
        master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.csi_meas_cfg.setup().report_trigger_size;
  } else {
    dci_cfg.report_trigger_size = 0; ///< determined by reportTriggerSize
  }
  if (rrc_recfg_user.recfg_dci_cfg.count("report_trigger_size")) {
    dci_cfg.report_trigger_size = stoi(rrc_recfg_user.recfg_dci_cfg["report_trigger_size"]);
    std::cout << "report_trigger_size: " << dci_cfg.report_trigger_size << std::endl;
  }

  if (bwp_ul_ded_s_ptr->pusch_cfg.setup().transform_precoder ==
      asn1::rrc_nr::pusch_cfg_s::transform_precoder_opts::disabled) {
    /*< Set to true if PUSCH transform precoding is enabled */
    dci_cfg.enable_transform_precoding = false;
  } else if (bwp_ul_ded_s_ptr->pusch_cfg.setup().transform_precoder ==
             asn1::rrc_nr::pusch_cfg_s::transform_precoder_opts::enabled) {
    /*< Set to true if PUSCH transform precoding is enabled */
    dci_cfg.enable_transform_precoding = true;
  }

  if (rrc_recfg_user.recfg_dci_cfg.count("enable_transform_precoding")) {
    if (rrc_recfg_user.recfg_dci_cfg["enable_transform_precoding"] == "true") {
      dci_cfg.enable_transform_precoding = true;
    } else if (rrc_recfg_user.recfg_dci_cfg["enable_transform_precoding"] == "false") {
      dci_cfg.enable_transform_precoding = false;
    }
  }

  /* < Set to true if PUSCH txConfig is set to non-codebook */
  dci_cfg.pusch_tx_config_non_codebook = false;
  if (bwp_ul_ded_s_ptr->pusch_cfg.setup().tx_cfg_present) {
    if (bwp_ul_ded_s_ptr->pusch_cfg.setup().tx_cfg.value == bwp_ul_ded_s_ptr->pusch_cfg.setup().tx_cfg.codebook) {
      dci_cfg.pusch_tx_config_non_codebook = false;
    } else if (bwp_ul_ded_s_ptr->pusch_cfg.setup().tx_cfg.value ==
               bwp_ul_ded_s_ptr->pusch_cfg.setup().tx_cfg.non_codebook) {
      dci_cfg.pusch_tx_config_non_codebook = true;
    }
  }

  if (rrc_recfg_user.recfg_dci_cfg.count("pusch_tx_config_non_codebook")) {
    if (rrc_recfg_user.recfg_dci_cfg["pusch_tx_config_non_codebook"] == "true") {
      dci_cfg.pusch_tx_config_non_codebook = true;
    } else if (rrc_recfg_user.recfg_dci_cfg["pusch_tx_config_non_codebook"] == "false") {
      dci_cfg.pusch_tx_config_non_codebook = false;
    }
  }

  if (bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a.setup().phase_tracking_rs_present) {
    /*< Set to true if PT-RS are enabled for PUSCH transmissionß */
    dci_cfg.pusch_ptrs = true;
  } else {
    dci_cfg.pusch_ptrs = false;
  }

  if (rrc_recfg_user.recfg_dci_cfg.count("pusch_ptrs")) {
    if (rrc_recfg_user.recfg_dci_cfg["pusch_ptrs"] == "true") {
      dci_cfg.pusch_ptrs = true;
    } else if (rrc_recfg_user.recfg_dci_cfg["pusch_ptrs"] == "false") {
      dci_cfg.pusch_ptrs = false;
    }
  }

  if (bwp_ul_ded_s_ptr->pusch_cfg.setup().uci_on_pusch.setup().beta_offsets_present) {
    if (bwp_ul_ded_s_ptr->pusch_cfg.setup().uci_on_pusch.setup().beta_offsets.type() ==
        asn1::rrc_nr::uci_on_pusch_s::beta_offsets_c_::types_opts::dynamic_type) {
      /* < Set to true if beta offsets operation is not semi-static */
      dci_cfg.pusch_dynamic_betas = true;
    } else if (bwp_ul_ded_s_ptr->pusch_cfg.setup().uci_on_pusch.setup().beta_offsets.type() ==
               asn1::rrc_nr::uci_on_pusch_s::beta_offsets_c_::types_opts::semi_static) {
      /* < Set to true if beta offsets operation is not semi-static */
      dci_cfg.pusch_dynamic_betas = false;
    } else {
      dci_cfg.pusch_dynamic_betas = false;
    }
  }

  if (rrc_recfg_user.recfg_dci_cfg.count("pusch_dynamic_betas")) {
    if (rrc_recfg_user.recfg_dci_cfg["pusch_dynamic_betas"] == "true") {
      dci_cfg.pusch_dynamic_betas = true;
    } else if (rrc_recfg_user.recfg_dci_cfg["pusch_dynamic_betas"] == "false") {
      dci_cfg.pusch_dynamic_betas = false;
    }
  }

  ///< PUSCH resource allocation type
  if (bwp_ul_ded_s_ptr->pusch_cfg_present) {
    switch (bwp_ul_ded_s_ptr->pusch_cfg.setup().res_alloc) {
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
    ERROR("No PUSCH resource allocation found, use type 0\n");
    dci_cfg.pusch_alloc_type = srsran_resource_alloc_type0;
  }

  if (rrc_recfg_user.recfg_dci_cfg.count("pusch_alloc_type")) {
    if (rrc_recfg_user.recfg_dci_cfg["pusch_alloc_type"] == "0") {
      dci_cfg.pusch_alloc_type = srsran_resource_alloc_type0;
    } else if (rrc_recfg_user.recfg_dci_cfg["pusch_alloc_type"] == "1") {
      dci_cfg.pusch_alloc_type = srsran_resource_alloc_type1;
    } else if (rrc_recfg_user.recfg_dci_cfg["pusch_alloc_type"] == "dynamic") {
      dci_cfg.pusch_alloc_type = srsran_resource_alloc_dynamic;
    }
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

  if (bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a_present) {
    if (bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a.setup().dmrs_type_present) {
      dci_cfg.pusch_dmrs_type = srsran_dmrs_sch_type_2;
    } else {
      dci_cfg.pusch_dmrs_type = srsran_dmrs_sch_type_1;
    }
    if (bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a.setup().max_len_present) {
      dci_cfg.pusch_dmrs_max_len = srsran_dmrs_sch_len_2;
    } else {
      dci_cfg.pusch_dmrs_max_len = srsran_dmrs_sch_len_1;
    }
  } else if (bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_b_present) {
    if (bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_b.setup().dmrs_type_present) {
      dci_cfg.pusch_dmrs_type = srsran_dmrs_sch_type_2;
    } else {
      dci_cfg.pusch_dmrs_type = srsran_dmrs_sch_type_1;
    }
    if (bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_b.setup().max_len_present) {
      dci_cfg.pusch_dmrs_max_len = srsran_dmrs_sch_len_2;
    } else {
      dci_cfg.pusch_dmrs_max_len = srsran_dmrs_sch_len_1;
    }
  } else {
    /* < PUSCH DMRS type */
    dci_cfg.pusch_dmrs_type = srsran_dmrs_sch_type_1;
    /* < PUSCH DMRS maximum length */
    dci_cfg.pusch_dmrs_max_len = srsran_dmrs_sch_len_1;
  }
  pusch_hl_cfg.dmrs_max_length = dci_cfg.pusch_dmrs_max_len;

  /// Format 1_1 specific configuration (for PDSCH only)
  switch (master_cell_group.phys_cell_group_cfg.pdsch_harq_ack_codebook) {
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
  // std::cout << "before ack codebook" << std::endl;

  // For DCI 0_1
  ///< Set to true if HARQ-ACK codebook is set to dynamic with 2 sub-codebooks
  dci_cfg.dynamic_dual_harq_ack_codebook = false;
  if (rrc_recfg_user.recfg_dci_cfg.count("dynamic_dual_harq_ack_codebook")) {
    if (rrc_recfg_user.recfg_dci_cfg["dynamic_dual_harq_ack_codebook"] == "true") {
      dci_cfg.dynamic_dual_harq_ack_codebook = true;
    } else if (rrc_recfg_user.recfg_dci_cfg["dynamic_dual_harq_ack_codebook"] == "false") {
      dci_cfg.dynamic_dual_harq_ack_codebook = false;
    }
  }
  dci_cfg.nof_dl_bwp       = master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.dl_bwp_to_add_mod_list.size();
  dci_cfg.nof_dl_time_res  = bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list_present
                                 ? bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup().size()
                                 : (sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common_present
                                        ? sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup()
                                             .pdsch_time_domain_alloc_list.size()
                                        : 0);
  dci_cfg.nof_aperiodic_zp = bwp_dl_ded_s_ptr->pdsch_cfg.setup().aperiodic_zp_csi_rs_res_sets_to_add_mod_list.size();
  // maxCodeBlockGroupsPerTransportBlock
  if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg_present &&
      master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg_present) {
    if (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup().code_block_group_tx_present) {
      switch (master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup()
                  .code_block_group_tx.setup()
                  .max_code_block_groups_per_transport_block) {
        case asn1::rrc_nr::pdsch_code_block_group_tx_s::max_code_block_groups_per_transport_block_e_::n2:
          dci_cfg.pdsch_nof_cbg = 2;
          break;
        case asn1::rrc_nr::pdsch_code_block_group_tx_s::max_code_block_groups_per_transport_block_e_::n4:
          dci_cfg.pdsch_nof_cbg = 4;
          break;
        case asn1::rrc_nr::pdsch_code_block_group_tx_s::max_code_block_groups_per_transport_block_e_::n8:
          dci_cfg.pdsch_nof_cbg = 8;
          break;
        default:
          dci_cfg.pdsch_nof_cbg = 0;
          break;
      }
    }
  }
  dci_cfg.nof_dl_to_ul_ack       = bwp_ul_ded_s_ptr->pucch_cfg.setup().dl_data_to_ul_ack.size();
  dci_cfg.pdsch_inter_prb_to_prb = bwp_dl_ded_s_ptr->pdsch_cfg.setup().vrb_to_prb_interleaver_present;
  if (rrc_recfg_user.recfg_dci_cfg.count("pdsch_inter_prb_to_prb")) {
    if (rrc_recfg_user.recfg_dci_cfg["pdsch_inter_prb_to_prb"] == "true") {
      dci_cfg.pdsch_inter_prb_to_prb = true;
    } else if (rrc_recfg_user.recfg_dci_cfg["pdsch_inter_prb_to_prb"] == "false") {
      dci_cfg.pdsch_inter_prb_to_prb = false;
    }
  }
  dci_cfg.pdsch_rm_pattern1 = bwp_dl_ded_s_ptr->pdsch_cfg.setup().rate_match_pattern_group1.size();
  dci_cfg.pdsch_rm_pattern2 = bwp_dl_ded_s_ptr->pdsch_cfg.setup().rate_match_pattern_group2.size();
  /* set to false initially and if maxofcodewordscheduledbydci is 2, set to true. */
  dci_cfg.pdsch_2cw = false;
  if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().max_nrof_code_words_sched_by_dci_present) {
    if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().max_nrof_code_words_sched_by_dci ==
        asn1::rrc_nr::pdsch_cfg_s::max_nrof_code_words_sched_by_dci_opts::n2) {
      dci_cfg.pdsch_2cw = true;
    }
  }

  dci_cfg.pdsch_tci =
      bwp_dl_ded_s_ptr->pdcch_cfg.setup().ctrl_res_set_to_add_mod_list[0].tci_present_in_dci_present ? true : false;
  if (rrc_recfg_user.recfg_dci_cfg.count("pdsch_tci")) {
    if (rrc_recfg_user.recfg_dci_cfg["pdsch_tci"] == "true") {
      dci_cfg.pdsch_tci = true;
    } else if (rrc_recfg_user.recfg_dci_cfg["pdsch_tci"] == "false") {
      dci_cfg.pdsch_tci = false;
    }
  }
  dci_cfg.pdsch_cbg_flush =
      master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup().code_block_group_tx_present ? true
                                                                                                               : false;
  if (rrc_recfg_user.recfg_dci_cfg.count("pdsch_cbg_flush")) {
    if (rrc_recfg_user.recfg_dci_cfg["pdsch_cbg_flush"] == "true") {
      dci_cfg.pdsch_cbg_flush = true;
    } else if (rrc_recfg_user.recfg_dci_cfg["pdsch_cbg_flush"] == "false") {
      dci_cfg.pdsch_cbg_flush = false;
    }
  }

  dci_cfg.pdsch_dynamic_bundling = false;
  if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().prb_bundling_type.type() ==
      asn1::rrc_nr::pdsch_cfg_s::prb_bundling_type_c_::types_opts::dynamic_bundling) {
    dci_cfg.pdsch_dynamic_bundling = true;
    ERROR("PRB dynamic bundling not implemented, which can cause being unable"
          "to find DCIs. We are working on it.");
  }

  switch (bwp_dl_ded_s_ptr->pdsch_cfg.setup().res_alloc) {
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
  // std::cout << "pdsch resource alloc: " << dci_cfg.pdsch_alloc_type << std::endl;

  // T-Mobile RRC Recfg
  if (rrc_recfg_user.recfg_dci_cfg.count("pdsch_alloc_type")) {
    if (rrc_recfg_user.recfg_dci_cfg["pdsch_alloc_type"] == "1") {
      dci_cfg.pdsch_alloc_type = srsran_resource_alloc_type1;
    } else if (rrc_recfg_user.recfg_dci_cfg["pdsch_alloc_type"] == "0") {
      dci_cfg.pdsch_alloc_type = srsran_resource_alloc_type0;
    } else if (rrc_recfg_user.recfg_dci_cfg["pdsch_alloc_type"] == "dynamic") {
      dci_cfg.pdsch_alloc_type = srsran_resource_alloc_dynamic;
    }
    std::cout << "pdsch alloc type: " << dci_cfg.pdsch_alloc_type << std::endl;
  }

  if (rrc_recfg_user.recfg_dci_cfg.count("multiple_scell")) {
    if (rrc_recfg_user.recfg_dci_cfg["multiple_scell"] == "true") {
      dci_cfg.multiple_scell = true;
    } else if (rrc_recfg_user.recfg_dci_cfg["multiple_scell"] == "false") {
      dci_cfg.multiple_scell = false;
    }
    std::cout << "multiple scell: " << dci_cfg.multiple_scell << std::endl;
  }

  /* for non carrier aggregation*/
  // dci_cfg.multiple_scell = false;
  dci_cfg.carrier_indicator_size = 0;

  if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a_present) {
    if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a.setup().dmrs_type_present) {
      dci_cfg.pdsch_dmrs_type = srsran_dmrs_sch_type_2;
    } else {
      dci_cfg.pdsch_dmrs_type = srsran_dmrs_sch_type_1;
    }
    if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a.setup().max_len_present) {
      dci_cfg.pdsch_dmrs_max_len = srsran_dmrs_sch_len_2;
    } else {
      dci_cfg.pdsch_dmrs_max_len = srsran_dmrs_sch_len_1;
    }
  } else if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_b_present) {
    if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_b.setup().dmrs_type_present) {
      dci_cfg.pdsch_dmrs_type = srsran_dmrs_sch_type_2;
    } else {
      dci_cfg.pdsch_dmrs_type = srsran_dmrs_sch_type_1;
    }
    if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_b.setup().max_len_present) {
      dci_cfg.pdsch_dmrs_max_len = srsran_dmrs_sch_len_2;
    } else {
      dci_cfg.pdsch_dmrs_max_len = srsran_dmrs_sch_len_1;
    }
  } else {
    dci_cfg.pdsch_dmrs_type    = srsran_dmrs_sch_type_1;
    dci_cfg.pdsch_dmrs_max_len = srsran_dmrs_sch_len_1;
  }
  pdsch_hl_cfg.dmrs_max_length = dci_cfg.pdsch_dmrs_max_len;

  pdsch_hl_cfg.typeA_pos = cell.mib.dmrs_typeA_pos;
  pusch_hl_cfg.typeA_pos = cell.mib.dmrs_typeA_pos;
  if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a_present) {
    pdsch_hl_cfg.dmrs_typeA.present = bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a_present;
    switch (bwp_dl_ded_s_ptr->pdsch_cfg.setup().dmrs_dl_for_pdsch_map_type_a.setup().dmrs_add_position) {
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

  if (bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a_present) {
    pusch_hl_cfg.dmrs_typeA.present = bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a_present;
    switch (bwp_ul_ded_s_ptr->pusch_cfg.setup().dmrs_ul_for_pusch_map_type_a.setup().dmrs_add_position) {
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

  if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup().size() > 0) {
    for (uint32_t pdsch_time_id = 0;
         pdsch_time_id < bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup().size();
         pdsch_time_id++) {
      if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup()[pdsch_time_id].k0_present) {
        pdsch_hl_cfg.common_time_ra[pdsch_time_id].k =
            bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup()[pdsch_time_id].k0;
      }
      pdsch_hl_cfg.common_time_ra[pdsch_time_id].sliv =
          bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup()[pdsch_time_id].start_symbol_and_len;
      switch (bwp_dl_ded_s_ptr->pdsch_cfg.setup().pdsch_time_domain_alloc_list.setup()[pdsch_time_id].map_type) {
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
  } else {
    // use SIB 1 config
    for (uint32_t pdsch_time_id = 0;
         pdsch_time_id < sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup()
                             .pdsch_time_domain_alloc_list.size();
         pdsch_time_id++) {
      if (sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup()
              .pdsch_time_domain_alloc_list[pdsch_time_id]
              .k0_present) {
        pdsch_hl_cfg.common_time_ra[pdsch_time_id].k =
            sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup()
                .pdsch_time_domain_alloc_list[pdsch_time_id]
                .k0;
      }
      pdsch_hl_cfg.common_time_ra[pdsch_time_id].sliv =
          sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup()
              .pdsch_time_domain_alloc_list[pdsch_time_id]
              .start_symbol_and_len;
      switch (sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup()
                  .pdsch_time_domain_alloc_list[pdsch_time_id]
                  .map_type) {
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
    pdsch_hl_cfg.nof_common_time_ra = sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup()
                                          .pdsch_time_domain_alloc_list.size();
  }

  if (bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup().size() > 0) {
    for (uint32_t pusch_time_id = 0;
         pusch_time_id < bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup().size();
         pusch_time_id++) {
      if (bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup()[pusch_time_id].k2_present) {
        pusch_hl_cfg.common_time_ra[pusch_time_id].k =
            bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup()[pusch_time_id].k2;
      }
      pusch_hl_cfg.common_time_ra[pusch_time_id].sliv =
          bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup()[pusch_time_id].start_symbol_and_len;
      switch (bwp_ul_ded_s_ptr->pusch_cfg.setup().pusch_time_domain_alloc_list.setup()[pusch_time_id].map_type) {
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
  } else {
    // use SIB 1 config
    for (uint32_t pusch_time_id = 0;
         pusch_time_id < sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup()
                             .pusch_time_domain_alloc_list.size();
         pusch_time_id++) {
      if (sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup()
              .pusch_time_domain_alloc_list[pusch_time_id]
              .k2_present) {
        pusch_hl_cfg.common_time_ra[pusch_time_id].k =
            sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup()
                .pusch_time_domain_alloc_list[pusch_time_id]
                .k2;
      }
      pusch_hl_cfg.common_time_ra[pusch_time_id].sliv =
          sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup()
              .pusch_time_domain_alloc_list[pusch_time_id]
              .start_symbol_and_len;
      switch (sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup()
                  .pusch_time_domain_alloc_list[pusch_time_id]
                  .map_type) {
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
    pusch_hl_cfg.nof_common_time_ra = sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup()
                                          .pusch_time_domain_alloc_list.size();
  }

  // config according to the SIB 1's UL and DL BWP size
  dci_cfg.bwp_dl_initial_bw =
      sib1.serving_cell_cfg_common.dl_cfg_common.freq_info_dl.scs_specific_carrier_list[0].carrier_bw;
  dci_cfg.bwp_dl_active_bw =
      sib1.serving_cell_cfg_common.dl_cfg_common.freq_info_dl.scs_specific_carrier_list[0].carrier_bw;
  dci_cfg.bwp_ul_initial_bw =
      sib1.serving_cell_cfg_common.ul_cfg_common.freq_info_ul.scs_specific_carrier_list[0].carrier_bw;
  dci_cfg.bwp_ul_active_bw =
      sib1.serving_cell_cfg_common.ul_cfg_common.freq_info_ul.scs_specific_carrier_list[0].carrier_bw;

  base_carrier.nof_prb = srsran_coreset_get_bw(&coreset1_t);
  carrier_dl           = base_carrier;
  carrier_dl.nof_prb   = dci_cfg.bwp_dl_active_bw; // Use a dummy carrier for resource calculation.
  // Use a fixed value for Amarisoft evaluation
  carrier_dl.max_mimo_layers =
      master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg_present
          ? master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup().max_mimo_layers_present
                ? master_cell_group.sp_cell_cfg.sp_cell_cfg_ded.pdsch_serving_cell_cfg.setup().max_mimo_layers
                : 4
          : 4;

  carrier_ul                 = base_carrier;
  carrier_ul.nof_prb         = dci_cfg.bwp_ul_active_bw;
  carrier_ul.max_mimo_layers = dci_cfg.nof_ul_layers;

  dci_cfg.nof_rb_groups = 0;
  if (dci_cfg.pdsch_alloc_type == srsran_resource_alloc_type0) {
    if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().rbg_size == asn1::rrc_nr::pdsch_cfg_s::rbg_size_opts::cfg1) {
      // BWP start prb is set to 0 since this is the only scenario that we see
      dci_cfg.nof_rb_groups = get_nof_rbgs(dci_cfg.bwp_dl_active_bw, 0, true);
    } else if (bwp_dl_ded_s_ptr->pdsch_cfg.setup().rbg_size == asn1::rrc_nr::pdsch_cfg_s::rbg_size_opts::cfg2) {
      dci_cfg.nof_rb_groups = get_nof_rbgs(dci_cfg.bwp_dl_active_bw, 0, false);
    }
  }

  if (rrc_recfg_user.recfg_pdsch_cfg.count("mcs_table")) {
    if (rrc_recfg_user.recfg_pdsch_cfg["mcs_table"] == "256qam") {
      pdsch_hl_cfg.mcs_table = srsran_mcs_table_256qam;
    }
  }

  if (rrc_recfg_user.recfg_pusch_cfg.count("mcs_table")) {
    if (rrc_recfg_user.recfg_pusch_cfg["mcs_table"] == "256qam") {
      pusch_hl_cfg.mcs_table = srsran_mcs_table_256qam;
    }
  }

  pdsch_hl_cfg.rbg_size_cfg_1 =
      bwp_dl_ded_s_ptr->pdsch_cfg.setup().rbg_size == asn1::rrc_nr::pdsch_cfg_s::rbg_size_e_::cfg1 ? true : false;
  // printf("pdsch_hl_cfg.dmrs_typeA.additional_pos: %d\n",
  //  pdsch_hl_cfg.dmrs_typeA.additional_pos);

  memcpy(&dci_cfg_ca, &dci_cfg, sizeof(srsran_dci_cfg_nr_t));
  dci_cfg_ca.multiple_scell         = true;
  dci_cfg_ca.carrier_indicator_size = 3;

  if (srsran_ue_dl_nr_init_nrscope(&ue_dl_dci, input, &ue_dl_args, arg_scs)) {
    ERROR("Error UE DL");
    return SRSRAN_ERROR;
  }
  if (srsran_ue_dl_nr_set_carrier_nrscope(&ue_dl_dci, &base_carrier, arg_scs)) {
    ERROR("Error setting SCH NR carrier");
    return SRSRAN_ERROR;
  }
  if (srsran_ue_dl_nr_set_pdcch_config(&ue_dl_dci, &pdcch_cfg, &dci_cfg_ca)) {
    ERROR("Error setting CORESET");
    return SRSRAN_ERROR;
  }
  // Precompute the non-CA DCI size context: the merged candidate-first search
  // covers both configs in one pass (ue_dl_dci.dci holds the CA sizes).
  if (srsran_dci_nr_set_cfg(&dci_nr_nca, &dci_cfg) < SRSRAN_SUCCESS) {
    ERROR("Error setting non-CA DCI configuration");
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

int DCIDecoder::DecodeandParseDCIfromSlot(srsran_slot_cfg_t*                   slot,
                                          WorkState*                           state,
                                          std::vector<DCIFeedback>&            sharded_results,
                                          std::vector<std::vector<uint16_t> >& sharded_rntis,
                                          std::vector<uint32_t>&               nof_sharded_rntis,
                                          std::vector<float>&                  dl_prb_rate,
                                          std::vector<float>&                  dl_prb_bits_rate,
                                          std::vector<float>&                  ul_prb_rate,
                                          std::vector<float>&                  ul_prb_bits_rate)                                          
{
  if (!state->rach_found or !state->dci_inited) {
    std::cout << "RACH not found or DCI decoder not initialized, quitting..." << std::endl;
    return SRSRAN_SUCCESS;
  }

  uint32_t n_rntis = (uint32_t)ceil((float)state->nof_known_rntis / (float)state->nof_rnti_worker_groups);
  uint32_t rnti_s  = rnti_worker_group_id * n_rntis;
  uint32_t rnti_e  = rnti_worker_group_id * n_rntis + n_rntis;

  if (rnti_s >= state->nof_known_rntis) {
    // std::cout << "DCI decoder " << dci_decoder_id << "|"
    // << rnti_worker_group_id << " exits because it's excessive.." << std::endl;
    return SRSRAN_SUCCESS;
  }

  if (rnti_e > state->nof_known_rntis) {
    rnti_e  = state->nof_known_rntis;
    n_rntis = rnti_e - rnti_s;
  }

  // std::cout << "DCI decoder " << dci_decoder_id
  //   << " processing: [" << rnti_s << ", " << rnti_e << ")" << std::endl;

  DCIFeedback new_result;
  sharded_results[dci_decoder_id] = new_result;
  sharded_results[dci_decoder_id].dl_grants.resize(n_rntis);
  sharded_results[dci_decoder_id].ul_grants.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_dl_prbs.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_dl_tbs.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_dl_bits.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_ul_prbs.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_ul_tbs.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_ul_bits.resize(n_rntis);
  sharded_results[dci_decoder_id].dl_dcis.resize(n_rntis);
  sharded_results[dci_decoder_id].ul_dcis.resize(n_rntis);

  sharded_rntis[dci_decoder_id].resize(n_rntis);
  nof_sharded_rntis[dci_decoder_id] = n_rntis;
  // std::cout << "nof_sharded_rntis[dci_decoder_id]: "
  // << nof_sharded_rntis[dci_decoder_id] << std::endl;

  // std::cout << "sharded_rntis: ";
  for (uint32_t i = 0; i < n_rntis; i++) {
    sharded_rntis[dci_decoder_id][i] = state->known_rntis[rnti_s + i];
    // std::cout << sharded_rntis[dci_decoder_id][i] << ", ";
  }
  // std::cout << std::endl;

  // Set the buffer to 0s
  for (uint32_t idx = 0; idx < n_rntis; idx++) {
    memset(&dci_dl[idx], 0, sizeof(srsran_dci_dl_nr_t));
    memset(&dci_ul[idx], 0, sizeof(srsran_dci_dl_nr_t));
  }

  srsran_ue_dl_nr_estimate_fft_nrscope(&ue_dl_dci, slot, arg_scs);

  int total_dl_dci = 0;
  int total_ul_dci = 0;

  for (uint32_t rnti_idx = 0; rnti_idx < n_rntis; rnti_idx++) {
    // With carrier aggregation
    memcpy(ue_dl_tmp, &ue_dl_dci, sizeof(srsran_ue_dl_nr_t));
    memcpy(slot_tmp, slot, sizeof(srsran_slot_cfg_t));

    int nof_dl_dci = srsran_ue_dl_nr_find_dl_dci_nrscope_dciloop(
        ue_dl_tmp, slot_tmp, sharded_rntis[dci_decoder_id][rnti_idx], srsran_rnti_type_c, dci_dl_tmp, 4);

    if (nof_dl_dci < SRSRAN_SUCCESS) {
      ERROR("Error in blind search");
    }

    int nof_ul_dci = srsran_ue_dl_nr_find_ul_dci(
        ue_dl_tmp, slot_tmp, sharded_rntis[dci_decoder_id][rnti_idx], srsran_rnti_type_c, dci_ul_tmp, 4);

    if (nof_dl_dci > 0) {
      dci_dl[rnti_idx] = dci_dl_tmp[0];
      total_dl_dci += nof_dl_dci;
    }

    if (nof_ul_dci > 0) {
      dci_ul[rnti_idx] = dci_ul_tmp[0];
      total_ul_dci += nof_ul_dci;
    }

    // printf("slot: %d\n", slot->idx);
    // for (uint32_t pdcch_idx = 0; pdcch_idx < ue_dl_tmp->pdcch_info_count; pdcch_idx++) {
    //   const srsran_ue_dl_nr_pdcch_info_t* info = &(ue_dl_tmp->pdcch_info[pdcch_idx]);
    //   printf("PDCCH: %s-rnti=0x%x, crst_id=%d, ss_type=%s, ncce=%d, al=%d, EPRE=%+.2f, RSRP=%+.2f, corr=%.3f; "
    //   "nof_bits=%d; crc=%s;\n",
    //   srsran_rnti_type_str_short(info->dci_ctx.rnti_type),
    //   info->dci_ctx.rnti,
    //   info->dci_ctx.coreset_id,
    //   srsran_ss_type_str(info->dci_ctx.ss_type),
    //   info->dci_ctx.location.ncce,
    //   info->dci_ctx.location.L,
    //   info->measure.epre_dBfs,
    //   info->measure.rsrp_dBfs,
    //   info->measure.norm_corr,
    //   info->nof_bits,
    //   info->result.crc ? "OK" : "KO");
    // }

    if (nof_ul_dci > 0 || nof_dl_dci > 0) {
      // The UE is either using CA or not, so if we find the DCI with CA,
      // we don't need to try further.
      // NRScopePlot::push_node(ue_dl_tmp->pdcch.symbols, ue_dl_tmp->pdcch.M);
      // printf("M=%d\n", ue_dl_tmp->pdcch.M);
      // printf("symbols=");
      // srsran_vec_fprint_c(stdout, ue_dl_tmp->pdcch.symbols, ue_dl_tmp->pdcch.M);
      printf("DCIDecoder -- DCI found with CA\n");
      continue;
    }

    memcpy(ue_dl_tmp, &ue_dl_dci, sizeof(srsran_ue_dl_nr_t));
    memcpy(slot_tmp, slot, sizeof(srsran_slot_cfg_t));

    // Set the DCI size for the non-carrier aggregation UEs.
    if (srsran_ue_dl_nr_set_pdcch_config(ue_dl_tmp, &pdcch_cfg, &dci_cfg)) {
      ERROR("Error setting CORESET");
      return SRSRAN_ERROR;
    }

    // printf("id: %d, search space: %d, l1: %d, l2: %d, l3: %d, l4: %d, l5: %d\n",
    //   0,
    //   ue_dl_tmp->cfg.search_space[0].id,
    //   ue_dl_tmp->cfg.search_space[0].nof_candidates[0],
    //   ue_dl_tmp->cfg.search_space[0].nof_candidates[1],
    //   ue_dl_tmp->cfg.search_space[0].nof_candidates[2],
    //   ue_dl_tmp->cfg.search_space[0].nof_candidates[3],
    //   ue_dl_tmp->cfg.search_space[0].nof_candidates[4]
    // );

    // printf("id: %d, search space: %d, l1: %d, l2: %d, l3: %d, l4: %d, l5: %d\n",
    //   1,
    //   ue_dl_tmp->cfg.search_space[1].id,
    //   ue_dl_tmp->cfg.search_space[1].nof_candidates[0],
    //   ue_dl_tmp->cfg.search_space[1].nof_candidates[1],
    //   ue_dl_tmp->cfg.search_space[1].nof_candidates[2],
    //   ue_dl_tmp->cfg.search_space[1].nof_candidates[3],
    //   ue_dl_tmp->cfg.search_space[1].nof_candidates[4]
    // );

    int nof_dl_dci_nca = srsran_ue_dl_nr_find_dl_dci_nrscope_dciloop(
        ue_dl_tmp, slot_tmp, sharded_rntis[dci_decoder_id][rnti_idx], srsran_rnti_type_c, dci_dl_tmp, 4);

    if (nof_dl_dci_nca < SRSRAN_SUCCESS) {
      ERROR("Error in blind search");
    }

    int nof_ul_dci_nca = srsran_ue_dl_nr_find_ul_dci(
        ue_dl_tmp, slot_tmp, sharded_rntis[dci_decoder_id][rnti_idx], srsran_rnti_type_c, dci_ul_tmp, 4);

    if (nof_dl_dci_nca > 0) {
      dci_dl[rnti_idx] = dci_dl_tmp[0];
      total_dl_dci += nof_dl_dci_nca;
    }

    if (nof_ul_dci_nca > 0) {
      dci_ul[rnti_idx] = dci_ul_tmp[0];
      total_ul_dci += nof_ul_dci_nca;
    }

    // printf("slot: %d\n", slot->idx);
    // for (uint32_t pdcch_idx = 0; pdcch_idx < ue_dl_tmp->pdcch_info_count; pdcch_idx++) {
    //   const srsran_ue_dl_nr_pdcch_info_t* info = &(ue_dl_tmp->pdcch_info[pdcch_idx]);
    //   printf("PDCCH: %s-rnti=0x%x, crst_id=%d, ss_type=%s, ncce=%d, al=%d, EPRE=%+.2f, RSRP=%+.2f, corr=%.3f; "
    //   "nof_bits=%d; crc=%s;\n",
    //   srsran_rnti_type_str_short(info->dci_ctx.rnti_type),
    //   info->dci_ctx.rnti,
    //   info->dci_ctx.coreset_id,
    //   srsran_ss_type_str(info->dci_ctx.ss_type),
    //   info->dci_ctx.location.ncce,
    //   info->dci_ctx.location.L,
    //   info->measure.epre_dBfs,
    //   info->measure.rsrp_dBfs,
    //   info->measure.norm_corr,
    //   info->nof_bits,
    //   info->result.crc ? "OK" : "KO");
    // }
    if (nof_dl_dci_nca > 0 || nof_ul_dci_nca > 0) {
      // NRScopePlot::push_node(ue_dl_tmp->pdcch.symbols, ue_dl_tmp->pdcch.M);
      // printf("M=%d\n", ue_dl_tmp->pdcch.M);
      // printf("symbols=");
      // srsran_vec_fprint_c(stdout, ue_dl_tmp->pdcch.symbols, ue_dl_tmp->pdcch.M);
      printf("DCIDecoder -- DCI Found without CA\n");
    }
  }

  if (total_dl_dci > 0) {
    for (uint32_t dci_idx_dl = 0; dci_idx_dl < n_rntis; dci_idx_dl++) {
      // the rnti will not be copied if no dci found
      if (dci_dl[dci_idx_dl].ctx.rnti == sharded_rntis[dci_decoder_id][dci_idx_dl]) {
        sharded_results[dci_decoder_id].dl_dcis[dci_idx_dl] = dci_dl[dci_idx_dl];
        char str[1024]                                      = {};
        srsran_dci_dl_nr_to_str(&(ue_dl_dci.dci), &dci_dl[dci_idx_dl], str, (uint32_t)sizeof(str));
        printf("DCIDecoder -- Found DCI: %s\n", str);
        // The grant may not be decoded correctly, since srsRAN's code is not complete.
        // We can calculate the DL bandwidth for this subframe by ourselves.
        if (dci_dl[dci_idx_dl].ctx.format == srsran_dci_format_nr_1_1) {
          srsran_sch_cfg_nr_t pdsch_cfg = {};
          pdsch_cfg.dmrs.typeA_pos      = state->cell.mib.dmrs_typeA_pos;

          if (srsran_ra_dl_dci_to_grant_nr(
                  &carrier_dl, slot, &pdsch_hl_cfg, &dci_dl[dci_idx_dl], &pdsch_cfg, &pdsch_cfg.grant) <
              SRSRAN_SUCCESS) {
            ERROR("Error decoding PDSCH search");
            // return result;
          }
          srsran_sch_cfg_nr_info(&pdsch_cfg, str, (uint32_t)sizeof(str));
          printf("DCIDecoder -- PDSCH_cfg:\n%s", str);

          sharded_results[dci_decoder_id].dl_grants[dci_idx_dl] = pdsch_cfg;
          sharded_results[dci_decoder_id].nof_dl_used_prbs += pdsch_cfg.grant.nof_prb * pdsch_cfg.grant.L;

          dl_prb_rate[dci_idx_dl + rnti_s] = (float)(pdsch_cfg.grant.tb[0].tbs + pdsch_cfg.grant.tb[1].tbs) /
                                             (float)pdsch_cfg.grant.nof_prb / (float)pdsch_cfg.grant.L;
          dl_prb_bits_rate[dci_idx_dl + rnti_s] =
              (float)(pdsch_cfg.grant.tb[0].nof_bits + pdsch_cfg.grant.tb[1].nof_bits) /
              (float)pdsch_cfg.grant.nof_prb / (float)pdsch_cfg.grant.L;
        }
      }
    }
    // task_scheduler_nrscope->result.nof_dl_spare_prbs =
    //  carrier_dl.nof_prb * (14 - 2) -
    //  task_scheduler_nrscope->result.nof_dl_used_prbs;
    // for(uint32_t idx = 0; idx < task_scheduler_nrscope->nof_known_rntis; idx ++){
    //   task_scheduler_nrscope->result.spare_dl_prbs[idx] =
    //    task_scheduler_nrscope->result.nof_dl_spare_prbs /
    //    task_scheduler_nrscope->nof_known_rntis;
    //   if(abs(task_scheduler_nrscope->result.spare_dl_prbs[idx]) >
    //        carrier_dl.nof_prb * (14 - 2)){
    //     task_scheduler_nrscope->result.spare_dl_prbs[idx] = 0;
    //   }
    //   task_scheduler_nrscope->result.spare_dl_tbs[idx] =
    //    (int) ((float)task_scheduler_nrscope->result.spare_dl_prbs[idx]
    //    * dl_prb_rate[idx]);
    //   task_scheduler_nrscope->result.spare_dl_bits[idx] =
    //      (int) ((float)task_scheduler_nrscope->result.spare_dl_prbs[idx] *
    //      dl_prb_bits_rate[idx]);
    // }
  } else {
    // task_scheduler_nrscope->result.nof_dl_spare_prbs =
    //    carrier_dl.nof_prb * (14 - 2);
    // for(uint32_t idx = 0; idx < task_scheduler_nrscope->nof_known_rntis; idx++){
    //   task_scheduler_nrscope->result.spare_dl_prbs[idx] =
    //      (int)((float)task_scheduler_nrscope->result.nof_dl_spare_prbs /
    //      (float)task_scheduler_nrscope->nof_known_rntis);
    //   task_scheduler_nrscope->result.spare_dl_tbs[idx] =
    //      (int) ((float)task_scheduler_nrscope->result.spare_dl_prbs[idx] *
    //      dl_prb_rate[idx]);
    //   task_scheduler_nrscope->result.spare_dl_bits[idx] =
    //      (int) ((float)task_scheduler_nrscope->result.spare_dl_prbs[idx] *
    //      dl_prb_bits_rate[idx]);
    // }
  }

  if (total_ul_dci > 0) {
    for (uint32_t dci_idx_ul = 0; dci_idx_ul < n_rntis; dci_idx_ul++) {
      if (dci_ul[dci_idx_ul].ctx.rnti == sharded_rntis[dci_decoder_id][dci_idx_ul]) {
        sharded_results[dci_decoder_id].ul_dcis[dci_idx_ul] = dci_ul[dci_idx_ul];
        char str[1024]                                      = {};
        srsran_dci_ul_nr_to_str(&(ue_dl_dci.dci), &dci_ul[dci_idx_ul], str, (uint32_t)sizeof(str));
        printf("DCIDecoder -- Found DCI: %s\n", str);
        // The grant may not be decoded correctly, since srsRAN's code is not complete.
        // We can calculate the UL bandwidth for this subframe by ourselves.
        srsran_sch_cfg_nr_t pusch_cfg = {};
        pusch_cfg.dmrs.typeA_pos      = state->cell.mib.dmrs_typeA_pos;
        if (srsran_ra_ul_dci_to_grant_nr(
                &carrier_ul, slot, &pusch_hl_cfg, &dci_ul[dci_idx_ul], &pusch_cfg, &pusch_cfg.grant) < SRSRAN_SUCCESS) {
          ERROR("Error decoding PUSCH search");
          // return result;
        }
        srsran_sch_cfg_nr_info(&pusch_cfg, str, (uint32_t)sizeof(str));
        printf("DCIDecoder -- PUSCH_cfg:\n%s", str);

        sharded_results[dci_decoder_id].ul_grants[dci_idx_ul] = pusch_cfg;
        sharded_results[dci_decoder_id].nof_ul_used_prbs += pusch_cfg.grant.nof_prb * pusch_cfg.grant.L;

        ul_prb_rate[dci_idx_ul + rnti_s] = (float)(pusch_cfg.grant.tb[0].tbs + pusch_cfg.grant.tb[1].tbs) /
                                           (float)pusch_cfg.grant.nof_prb / (float)pusch_cfg.grant.L;
        ul_prb_bits_rate[dci_idx_ul + rnti_s] =
            (float)(pusch_cfg.grant.tb[0].nof_bits + pusch_cfg.grant.tb[1].nof_bits) / (float)pusch_cfg.grant.nof_prb /
            (float)pusch_cfg.grant.L;
      }
    }
    // task_scheduler_nrscope->result.nof_ul_spare_prbs =
    //    carrier_dl.nof_prb * (14 - 2) -
    //    task_scheduler_nrscope->result.nof_ul_used_prbs;
    // for(uint32_t idx = 0; idx < task_scheduler_nrscope->nof_known_rntis; idx ++){
    //   task_scheduler_nrscope->result.spare_ul_prbs[idx] =
    //      task_scheduler_nrscope->result.nof_ul_spare_prbs /
    //      task_scheduler_nrscope->nof_known_rntis;
    //   task_scheduler_nrscope->result.spare_ul_tbs[idx] =
    //      (int) ((float)task_scheduler_nrscope->result.spare_ul_prbs[idx] *
    //      ul_prb_rate[idx]);
    //   task_scheduler_nrscope->result.spare_ul_bits[idx] =
    //      (int) ((float)task_scheduler_nrscope->result.spare_ul_prbs[idx] *
    //      ul_prb_bits_rate[idx]);
    // }
  } else {
    // task_scheduler_nrscope->result.nof_ul_spare_prbs =
    //      carrier_dl.nof_prb * (14 - 2);
    // for(uint32_t idx = 0; idx < task_scheduler_nrscope->nof_known_rntis; idx ++){
    //   task_scheduler_nrscope->result.spare_ul_prbs[idx] =
    //      (int)((float)task_scheduler_nrscope->result.nof_ul_spare_prbs /
    //      (float)task_scheduler_nrscope->nof_known_rntis);
    //   if(abs(task_scheduler_nrscope->result.spare_ul_prbs[idx]) >
    //      carrier_dl.nof_prb * (14 - 2)){
    //     task_scheduler_nrscope->result.spare_ul_prbs[idx] = 0;
    //   }
    //   task_scheduler_nrscope->result.spare_ul_tbs[idx] =
    //      (int) ((float)task_scheduler_nrscope->result.spare_ul_prbs[idx] *
    //      ul_prb_rate[idx]);
    //   task_scheduler_nrscope->result.spare_ul_bits[idx] =
    //      (int) ((float)task_scheduler_nrscope->result.spare_ul_prbs[idx] *
    //      ul_prb_bits_rate[idx]);
    // }
  }

  return SRSRAN_SUCCESS;
}

extern "C" {
  #include "srsran/phy/fec/polar/polar_chanalloc.h"
  #include "srsran/phy/fec/polar/polar_interleaver.h"
}
#define NO_BIT_INTERLEAVE 0


/*************** CANDIDATE-FIRST BLIND SEARCH ***************/
// Restructured blind search: instead of one full sweep per RNTI (which
// re-measures and re-decodes the same physical candidate locations 8-12x),
// this enumerates the unique candidate locations across all RNTIs first and
// applies most of the decode pipeline once per unique location:
//
//   phase 1: enumerate unique (coreset, L, ncce) locations
//            - common search space locations are RNTI-independent: added once,
//              claimed by every RNTI
//            - UE search space locations are Y_p_n-hashed per RNTI: computed
//              per RNTI, deduped, claimed by the RNTIs that hash there
//   phase 2: per unique location:
//            measure -> gates -> CE extract/RE copy/equalize/demod  (once)
//            then per unique (DCI size, format, c_init) group:
//              descramble/dematch/polar/CRC-prep                    (once)
//              then per claiming RNTI: 1 XOR + 1 compare            (trivial)
//   phase 3: unpack per-RNTI results
//
// The c_init grouping makes DMRS-scrambling-ID cells correct automatically:
// when dmrs_scrambling_id_present is false (this cell), every RNTI shares one
// c_init and each (location,size) is decoded exactly once; when true, UE-SS
// groups split per RNTI and only the post-descramble stages replicate.
//
// Scope notes:
//  - Searches both DCI size contexts in one pass: q's own q->dci (the CA
//    sizes, cfg_idx 0) plus the optional dci_nca argument (non-CA sizes,
//    cfg_idx 1; pass NULL to search CA only). Locations, measure and prep are
//    shared; the extra context only adds decode groups. Each found message
//    remembers its context so phase 3 unpacks it correctly. On equal sizes,
//    CA wins — matching the original attempt order.
//  - rnti_type is expected to be srsran_rnti_type_c (no SI/RA special cases).
//  - q->pdcch_info gets one debug entry per unique location (not per RNTI
//    sweep iteration like the original), with result.crc = "any RNTI hit".



// per-worker polar decode memoization
// as part of the candidate-first restructuring, we cache the polar code construction
// results in flat_polar_code_get_cached() to avoid redundant recomputation across 
// to improve performance a little more

// One polar decode: a unique (DCI size, format, c_init) at some location,
// CRC-checked against every RNTI that monitors the location with that size.
struct CandDecodeGroup {
  uint32_t                   nof_bits;
  srsran_dci_format_nr_t     format;
  srsran_search_space_type_t ss_type;
  uint16_t                   scr_rnti; // nonzero only for UE SS on a dmrs-scrambling-id CORESET
  uint8_t                    cfg_idx;  // DCI size context: 0 = q->dci (CA), 1 = dci_nca
  std::vector<uint32_t>      rnti_idxs;
};

struct CandLocationEntry {
  uint32_t                     coreset_id;
  srsran_dci_location_t        loc;
  std::vector<CandDecodeGroup> groups;
};

// srsran_polar_code_get() rebuilds the frozen-set tables (setdiff + two
// qsorts over N<=512) on every call, ~3us each, but the result depends only
// on (K, E) and a config sees just a handful of pairs (DCI sizes x
// aggregation levels). Cache them per-thread: each persistent worker builds
// its own ~8-entry cache, no cross-thread state. CAUTION: this is only sound
// in single_threaded_workers mode (persistent worker threads). The per-slot
// thread-respawn mode would start cold every slot (no caching benefit) and
// leak each entry's polar_code_init mallocs on every thread exit.
#define POLAR_CODE_CACHE_SIZE 32
struct PolarCodeCacheEntry {
  uint16_t            K;
  uint16_t            E;
  srsran_polar_code_t code;
};
static thread_local PolarCodeCacheEntry polar_code_cache[POLAR_CODE_CACHE_SIZE];
static thread_local uint32_t            polar_code_cache_count = 0;

static const srsran_polar_code_t* flat_polar_code_get_cached(uint16_t K, uint16_t E)
{
  for (uint32_t i = 0; i < polar_code_cache_count; i++) {
    if (polar_code_cache[i].K == K && polar_code_cache[i].E == E) {
      return &polar_code_cache[i].code;
    }
  }
  if (polar_code_cache_count >= POLAR_CODE_CACHE_SIZE) {
    return NULL; // caller falls back to computing per call
  }
  PolarCodeCacheEntry* entry = &polar_code_cache[polar_code_cache_count];
  if (srsran_polar_code_init(&entry->code) < SRSRAN_SUCCESS) {
    return NULL;
  }
  if (srsran_polar_code_get(&entry->code, K, E, 9U) < SRSRAN_SUCCESS) {
    return NULL;
  }
  entry->K = K;
  entry->E = E;
  polar_code_cache_count++;
  return &entry->code;
}

int nrscope_candidate_first_find_dci(srsran_ue_dl_nr_t*       q,
                                     const srsran_dci_nr_t*   dci_nca, // non-CA size context; NULL = CA only
                                     const srsran_slot_cfg_t* slot_cfg,
                                     const uint16_t*          rnti_list,
                                     uint32_t                 nof_rntis,
                                     srsran_rnti_type_t       rnti_type,
                                     srsran_dci_dl_nr_t*      dl_dci_out, // [nof_rntis], first DL hit per RNTI
                                     int*                     nof_dl_dci, // [nof_rntis]
                                     srsran_dci_ul_nr_t*      ul_dci_out, // [nof_rntis], first UL hit per RNTI
                                     int*                     nof_ul_dci) // [nof_rntis]
{
  if (q == NULL || slot_cfg == NULL || rnti_list == NULL || dl_dci_out == NULL || nof_dl_dci == NULL ||
      ul_dci_out == NULL || nof_ul_dci == NULL) {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }

  for (uint32_t r = 0; r < nof_rntis; r++) {
    nof_dl_dci[r] = 0;
    nof_ul_dci[r] = 0;
  }
  if (nof_rntis == 0) {
    return 0;
  }

  q->pdcch_info_count = 0;

  // DCI size contexts to search, in priority order (CA first)
  const srsran_dci_nr_t* dci_ctxs[2] = {&q->dci, dci_nca};
  const uint32_t         nof_ctxs    = (dci_nca != NULL) ? 2U : 1U;

  // ========== phase 1: enumerate unique candidate locations ==========
  std::vector<CandLocationEntry> locations;
  locations.reserve(64);

  for (uint32_t ss_idx = 0; ss_idx < SRSRAN_UE_DL_NR_MAX_NOF_SEARCH_SPACE; ss_idx++) {
    if (!q->cfg.search_space_present[ss_idx]) {
      continue;
    }
    const srsran_search_space_t* search_space = &q->cfg.search_space[ss_idx];

    uint32_t coreset_id = search_space->coreset_id;
    if (coreset_id >= SRSRAN_UE_DL_NR_MAX_NOF_CORESET || !q->cfg.coreset_present[coreset_id]) {
      ERROR("CORESET %d is not present in search space %d", search_space->coreset_id, search_space->id);
      return SRSRAN_ERROR;
    }
    srsran_coreset_t* coreset = &q->cfg.coreset[coreset_id];

    bool is_ue_ss      = (search_space->type == srsran_search_space_type_ue);
    bool scr_id_active = is_ue_ss && coreset->dmrs_scrambling_id_present;

    // Unique DCI sizes of this search space across all size contexts (first
    // hit wins: CA before non-CA, mirroring the original attempt order;
    // within a context, first format with a size wins, same as the original)
    uint32_t               ss_sizes[2 * SRSRAN_DCI_NR_MAX_NOF_SIZES]   = {};
    srsran_dci_format_nr_t ss_formats[2 * SRSRAN_DCI_NR_MAX_NOF_SIZES] = {};
    uint8_t                ss_cfgs[2 * SRSRAN_DCI_NR_MAX_NOF_SIZES]    = {};
    uint32_t               nof_ss_sizes                                = 0;
    for (uint32_t cfg_idx = 0; cfg_idx < nof_ctxs; cfg_idx++) {
      for (uint32_t format_idx = 0; format_idx < SRSRAN_MIN(search_space->nof_formats, SRSRAN_DCI_FORMAT_NR_COUNT);
           format_idx++) {
        srsran_dci_format_nr_t dci_format   = search_space->formats[format_idx];
        uint32_t               dci_nof_bits = srsran_dci_nr_size(dci_ctxs[cfg_idx], search_space->type, dci_format);
        if (dci_nof_bits == 0) {
          ERROR("Error DCI size");
          return SRSRAN_ERROR;
        }
        bool skip = false;
        for (uint32_t i = 0; i < nof_ss_sizes && !skip; i++) {
          if (dci_nof_bits == ss_sizes[i]) {
            skip = true;
          }
        }
        if (skip) {
          continue;
        }
        if (nof_ss_sizes >= 2 * SRSRAN_DCI_NR_MAX_NOF_SIZES) {
          ERROR("Exceed maximum number of DCI sizes");
          return SRSRAN_ERROR;
        }
        ss_formats[nof_ss_sizes] = dci_format;
        ss_cfgs[nof_ss_sizes]    = (uint8_t)cfg_idx;
        ss_sizes[nof_ss_sizes++] = dci_nof_bits;
      }
    }

    for (uint32_t L = 0; L < SRSRAN_SEARCH_SPACE_NOF_AGGREGATION_LEVELS_NR; L++) {
      // Common SS: locations are RNTI-independent (Y_p_n = 0), enumerate once
      // and claim for every RNTI. UE SS: enumerate per RNTI (Y_p_n hash).
      uint32_t n_enum = is_ue_ss ? nof_rntis : 1;

      for (uint32_t e = 0; e < n_enum; e++) {
        uint16_t enum_rnti = rnti_list[is_ue_ss ? e : 0];

        uint32_t candidates[SRSRAN_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR] = {};
        int      nof_candidates                                        = srsran_pdcch_nr_locations_coreset(
            coreset, search_space, enum_rnti, L, SRSRAN_SLOT_NR_MOD(q->carrier.scs, slot_cfg->idx), candidates);
        if (nof_candidates < SRSRAN_SUCCESS) {
          ERROR("Error calculating DCI candidate location");
          return SRSRAN_ERROR;
        }

        for (int ncce_idx = 0; ncce_idx < nof_candidates; ncce_idx++) {
          // Find or append the location entry
          CandLocationEntry* entry = NULL;
          for (auto& le : locations) {
            if (le.coreset_id == coreset_id && le.loc.L == L && le.loc.ncce == candidates[ncce_idx]) {
              entry = &le;
              break;
            }
          }
          if (entry == NULL) {
            locations.emplace_back();
            entry             = &locations.back();
            entry->coreset_id = coreset_id;
            entry->loc.L      = L;
            entry->loc.ncce   = candidates[ncce_idx];
          }

          for (uint32_t s = 0; s < nof_ss_sizes; s++) {
            uint16_t scr_rnti = scr_id_active ? enum_rnti : 0;

            // Find or append the decode group. The key is (size, scr_rnti):
            // together they fully determine the decoded bits (scr_rnti != 0
            // implies the dmrs-scrambling-id c_init, otherwise c_init is the
            // PCI for common and UE SS alike). format/ss_type are kept from
            // the first claiming search space — the same entry that survives
            // the original sweep's payload dedup when several search spaces
            // hash onto one location.
            CandDecodeGroup* group = NULL;
            for (auto& g : entry->groups) {
              if (g.nof_bits == ss_sizes[s] && g.scr_rnti == scr_rnti) {
                group = &g;
                break;
              }
            }
            if (group == NULL) {
              entry->groups.emplace_back();
              group           = &entry->groups.back();
              group->nof_bits = ss_sizes[s];
              group->format   = ss_formats[s];
              group->ss_type  = search_space->type;
              group->scr_rnti = scr_rnti;
              group->cfg_idx  = ss_cfgs[s];
            }

            // Claim: which RNTIs get a CRC trial against this decode
            if (is_ue_ss) {
              bool have = false;
              for (uint32_t ri : group->rnti_idxs) {
                if (ri == e) {
                  have = true;
                  break;
                }
              }
              if (!have) {
                group->rnti_idxs.push_back(e);
              }
            } else if (group->rnti_idxs.size() != nof_rntis) {
              group->rnti_idxs.clear();
              for (uint32_t r = 0; r < nof_rntis; r++) {
                group->rnti_idxs.push_back(r);
              }
            }
          }
        }
      }
    }
  }


  // ========== phase 2: walk the pipeline once per unique location ==========
  // Found messages keep the size context they were decoded under so phase 3
  // can unpack each with the matching context.
  struct CandMsg {
    srsran_dci_msg_nr_t msg;
    uint8_t             cfg_idx;
  };
  std::vector<std::vector<CandMsg>> dl_msgs(nof_rntis);
  std::vector<std::vector<CandMsg>> ul_msgs(nof_rntis);

  uint32_t last_coreset_id = UINT32_MAX;

  for (auto& entry : locations) {
    // Switch the PDCCH decoder to this location's CORESET when it changes
    // (inlined srsran_pdcch_nr_set_carrier)
    if (entry.coreset_id != last_coreset_id) {
      q->pdcch.carrier = q->carrier;
      q->pdcch.coreset = q->cfg.coreset[entry.coreset_id];
      last_coreset_id  = entry.coreset_id;
    }
    srsran_pdcch_nr_t* pdcch = &q->pdcch;

    // Debug bookkeeping: one pdcch_info entry per unique location. The
    // pdcch_info array is sized for a single UE's sweep (44 candidates), but
    // the union of locations across many RNTIs can exceed that — past
    // capacity, keep processing and just skip the debug entry (the original
    // per-RNTI sweep aborts here, which we must not do).
    srsran_ue_dl_nr_pdcch_info_t  pdcch_info_scratch;
    srsran_ue_dl_nr_pdcch_info_t* pdcch_info = &pdcch_info_scratch;
    if (q->pdcch_info_count < SRSRAN_MAX_NOF_CANDIDATES_SLOT_NR) {
      pdcch_info = &q->pdcch_info[q->pdcch_info_count];
      q->pdcch_info_count++;
    }
    SRSRAN_MEM_ZERO(pdcch_info, srsran_ue_dl_nr_pdcch_info_t, 1);
    pdcch_info->dci_ctx.location   = entry.loc;
    pdcch_info->dci_ctx.coreset_id = entry.coreset_id;
    pdcch_info->dci_ctx.rnti_type  = rnti_type;
    if (!entry.groups.empty()) {
      pdcch_info->dci_ctx.ss_type = entry.groups[0].ss_type;
      pdcch_info->dci_ctx.format  = entry.groups[0].format;
      pdcch_info->nof_bits        = entry.groups[0].nof_bits;
    }
    srsran_dmrs_pdcch_measure_t* m = &pdcch_info->measure;

    // Measure the location's DMRS (correlation/EPRE)
    if (srsran_dmrs_pdcch_get_measure(&q->dmrs_pdcch[entry.coreset_id], &entry.loc, m) < SRSRAN_SUCCESS) {
      ERROR("Error getting measure location L=%d, ncce=%d", entry.loc.L, entry.loc.ncce);
      return SRSRAN_ERROR;
    }

    // Gates (identical to the per-RNTI sweep)
    if (!isnormal(m->norm_corr)) {
      continue;
    }
    if (m->epre_dBfs < q->pdcch_dmrs_epre_thr) {
      continue;
    }
    if (m->norm_corr < q->pdcch_dmrs_corr_thr) {
      continue;
    }


    // ---- per-location prep: CE extract + RE copy + equalize + demod ----
    if (srsran_dmrs_pdcch_get_ce(&q->dmrs_pdcch[entry.coreset_id], &entry.loc, q->pdcch_ce) < SRSRAN_SUCCESS) {
      ERROR("Error extracting PDCCH DMRS");
      return SRSRAN_ERROR;
    }

    uint32_t M = (1U << entry.loc.L) * (SRSRAN_NRE - 3U) * 6U; // Number of RE
    uint32_t E = M * 2;                                        // Number of rate-matched bits
    pdcch->M   = M;
    pdcch->E   = E;

    if (q->pdcch_ce->nof_re != M) {
      ERROR("Invalid number of channel estimates (%d != %d)", M, q->pdcch_ce->nof_re);
      return SRSRAN_ERROR;
    }

    uint32_t m_re = pdcch_nr_cp(pdcch, &entry.loc, q->sf_symbols[0], pdcch->symbols, false);
    if (M != m_re) {
      ERROR("Unmatch number of RE (%d != %d)", m_re, M);
      return SRSRAN_ERROR;
    }

    srsran_predecoding_single(pdcch->symbols, q->pdcch_ce->ce, pdcch->symbols, NULL, M, 1.0f, q->pdcch_ce->noise_var);

    int8_t* llr = (int8_t*)pdcch->f;
    srsran_demod_soft_demodulate_b(SRSRAN_MOD_QPSK, pdcch->symbols, llr, M);

    float evm = NAN;
    if (pdcch->evm_buffer != NULL) {
      evm = srsran_evm_run_b(pdcch->evm_buffer, &pdcch->modem_table, pdcch->symbols, llr, E);
    }
    pdcch_info->result.evm = evm;

    for (uint32_t i = 0; i < E; i++) {
      llr[i] *= -1;
    }

    // Keep the pre-descrambling LLRs: each decode group descrambles its own
    // copy (different c_init and/or size must not clobber the original)
    int8_t llr_raw[SRSRAN_PDCCH_MAX_RE * 2];
    srsran_vec_i8_copy(llr_raw, llr, E);


    // ---- per (size, format, c_init): dematch + polar decode + CRC prep ----
    for (auto& group : entry.groups) {
      uint32_t K = group.nof_bits + 24U;
      pdcch->K   = K;

      const srsran_polar_code_t* code = flat_polar_code_get_cached(K, E);
      if (code == NULL) {
        // Cache unavailable (full or init failure): compute per call
        if (srsran_polar_code_get(&pdcch->code, K, E, 9U) < SRSRAN_SUCCESS) {
          return SRSRAN_ERROR;
        }
        code = &pdcch->code;
      }

      // Descramble into the working buffer, leaving llr_raw intact
      uint32_t n_id = (group.ss_type == srsran_search_space_type_ue && pdcch->coreset.dmrs_scrambling_id_present)
                          ? pdcch->coreset.dmrs_scrambling_id
                          : pdcch->carrier.pci;
      uint32_t n_rnti = (group.ss_type == srsran_search_space_type_ue && pdcch->coreset.dmrs_scrambling_id_present)
                            ? group.scr_rnti
                            : 0U;
      uint32_t c_init = ((n_rnti << 16U) + n_id) & 0x7fffffffU;
      srsran_sequence_apply_c(llr_raw, llr, E, c_init);

      int8_t* d = (int8_t*)pdcch->d;
      if (srsran_polar_rm_rx_c(&pdcch->rm, llr, d, E, code->n, K, NO_BIT_INTERLEAVE) < SRSRAN_SUCCESS) {
        return SRSRAN_ERROR;
      }

      if (srsran_polar_decoder_decode_c(&pdcch->decoder, d, pdcch->allocated, code->n, code->F_set, code->F_set_size) <
          SRSRAN_SUCCESS) {
        return SRSRAN_ERROR;
      }

      uint8_t c_prime[SRSRAN_POLAR_INTERLEAVER_K_MAX_IL];
      srsran_polar_chanalloc_rx(pdcch->allocated, c_prime, code->K, code->nPC, code->K_set, code->PC_set);

      uint8_t* c = pdcch->c;
      srsran_bit_unpack(UINT32_MAX, &c, 24U);
      srsran_polar_interleaver_run_u8(c_prime, c, K, false);

      // checksum1 covers the 24 prepended ones + the payload: RNTI-independent.
      // checksum2_base is the received (still RNTI-masked) CRC; the gNB masks
      // its low 16 bits with the RNTI, so the per-RNTI trial reduces to one
      // XOR and one compare.
      uint32_t checksum1      = srsran_crc_checksum(&pdcch->crc24c, pdcch->c, K);
      uint8_t* ptr            = &c[K - 24];
      uint32_t checksum2_base = srsran_bit_pack(&ptr, 24);

      // ---- per claiming RNTI: the CRC trial ----
      for (uint32_t rnti_idx : group.rnti_idxs) {
        uint16_t rnti = rnti_list[rnti_idx];
        if (checksum1 != (checksum2_base ^ (uint32_t)rnti)) {
          continue;
        }

        pdcch_info->result.crc = true; // at least one RNTI hit at this location

        // Build the DCI message exactly as the original sweep would have
        srsran_dci_msg_nr_t dci_msg = {};
        dci_msg.ctx.location        = entry.loc;
        dci_msg.ctx.ss_type         = group.ss_type;
        dci_msg.ctx.coreset_id      = entry.coreset_id;
        dci_msg.ctx.coreset_start_rb = srsran_coreset_start_rb(&q->cfg.coreset[entry.coreset_id]);
        dci_msg.ctx.rnti_type       = rnti_type;
        dci_msg.ctx.rnti            = rnti;
        dci_msg.ctx.format          = group.format;
        dci_msg.nof_bits            = group.nof_bits;
        srsran_vec_u8_copy(dci_msg.payload, c, group.nof_bits);

        // Direction detection + format flip (payload-based, same as original)
        if (!srsran_dci_nr_valid_direction(&dci_msg)) {
          switch (dci_msg.ctx.format) {
            case srsran_dci_format_nr_0_0:
              dci_msg.ctx.format = srsran_dci_format_nr_1_0;
              break;
            case srsran_dci_format_nr_0_1:
              dci_msg.ctx.format = srsran_dci_format_nr_1_1;
              break;
            case srsran_dci_format_nr_1_0:
              dci_msg.ctx.format = srsran_dci_format_nr_0_0;
              break;
            case srsran_dci_format_nr_1_1:
              dci_msg.ctx.format = srsran_dci_format_nr_0_1;
              break;
            default:
              continue;
          }
        }

        // Route to the RNTI's UL or DL list (dedup by payload, cap like the
        // original's SRSRAN_MAX_DCI_MSG_NR)
        bool is_ul = (dci_msg.ctx.format == srsran_dci_format_nr_0_0 || dci_msg.ctx.format == srsran_dci_format_nr_0_1);
        std::vector<CandMsg>& list = is_ul ? ul_msgs[rnti_idx] : dl_msgs[rnti_idx];
        bool                  dup  = false;
        for (auto& cm : list) {
          if (cm.msg.nof_bits == dci_msg.nof_bits && memcmp(cm.msg.payload, dci_msg.payload, dci_msg.nof_bits) == 0) {
            dup = true;
            break;
          }
        }
        if (list.size() >= SRSRAN_MAX_DCI_MSG_NR || dup) {
          continue;
        }
        list.push_back({dci_msg, group.cfg_idx});
      }
    }
  }

  // ========== phase 3: unpack per-RNTI results ==========
  // A message that fails to unpack is dropped and not counted, matching the
  // original paths: this happens for direction-flipped 0_1/1_1 candidates
  // whose counterpart format has a different size (find_ul_dci skips them
  // with an "Unpacking DCI 0_0" error; the unpack itself logs the size
  // mismatch). It must not be fatal here.
  int total = 0;
  for (uint32_t r = 0; r < nof_rntis; r++) {
    nof_dl_dci[r] = 0;
    nof_ul_dci[r] = 0;
    // Old-path parity: the original ran the non-CA attempt only when the CA
    // attempt found nothing (DL or UL) for the RNTI, so a ~2^-24 coincidental
    // CRC pass under the not-searched config could never be reported. Mirror
    // that: take non-CA (cfg_idx 1) messages only if no CA (cfg_idx 0)
    // message unpacked for this RNTI.
    for (uint8_t cfg_pass = 0; cfg_pass < 2; cfg_pass++) {
      if (cfg_pass == 1 && (nof_dl_dci[r] > 0 || nof_ul_dci[r] > 0)) {
        break;
      }
      for (auto& cm : dl_msgs[r]) {
        if (cm.cfg_idx != cfg_pass) {
          continue;
        }
        srsran_dci_dl_nr_t unpacked = {};
        if (srsran_dci_nr_dl_unpack(dci_ctxs[cm.cfg_idx], &cm.msg, &unpacked) < SRSRAN_SUCCESS) {
          continue;
        }
        if (nof_dl_dci[r] == 0) {
          dl_dci_out[r] = unpacked;
        }
        nof_dl_dci[r]++;
      }
      for (auto& cm : ul_msgs[r]) {
        if (cm.cfg_idx != cfg_pass) {
          continue;
        }
        srsran_dci_ul_nr_t unpacked = {};
        if (srsran_dci_nr_ul_unpack(dci_ctxs[cm.cfg_idx], &cm.msg, &unpacked) < SRSRAN_SUCCESS) {
          continue;
        }
        if (nof_ul_dci[r] == 0) {
          ul_dci_out[r] = unpacked;
        }
        nof_ul_dci[r]++;
      }
    }
    total += nof_dl_dci[r] + nof_ul_dci[r];
  }

  return total;
}


int DCIDecoder::DecodeandParseDCIfromSlotOptimized(srsran_slot_cfg_t*                   slot,
                                          WorkState*                           state,
                                          std::vector<DCIFeedback>&            sharded_results,
                                          std::vector<std::vector<uint16_t> >& sharded_rntis,
                                          std::vector<uint32_t>&               nof_sharded_rntis,
                                          std::vector<float>&                  dl_prb_rate,
                                          std::vector<float>&                  dl_prb_bits_rate,
                                          std::vector<float>&                  ul_prb_rate,
                                          std::vector<float>&                  ul_prb_bits_rate)
{

  if (!state->rach_found or !state->dci_inited) {
    return SRSRAN_SUCCESS;
  }

  uint32_t n_rntis = (uint32_t)ceil((float)state->nof_known_rntis / (float)state->nof_rnti_worker_groups);
  uint32_t rnti_s  = rnti_worker_group_id * n_rntis;
  uint32_t rnti_e  = rnti_worker_group_id * n_rntis + n_rntis;

  if (rnti_s >= state->nof_known_rntis) {
    // std::cout << "DCI decoder " << dci_decoder_id << "|"
    // << rnti_worker_group_id << " exits because it's excessive.." << std::endl;
    return SRSRAN_SUCCESS;
  }

  if (rnti_e > state->nof_known_rntis) {
    rnti_e  = state->nof_known_rntis;
    n_rntis = rnti_e - rnti_s;
  }

  // std::cout << "DCI decoder " << dci_decoder_id
  //   << " processing: [" << rnti_s << ", " << rnti_e << ")" << std::endl;

  DCIFeedback new_result;
  sharded_results[dci_decoder_id] = new_result;
  sharded_results[dci_decoder_id].dl_grants.resize(n_rntis);
  sharded_results[dci_decoder_id].ul_grants.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_dl_prbs.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_dl_tbs.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_dl_bits.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_ul_prbs.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_ul_tbs.resize(n_rntis);
  sharded_results[dci_decoder_id].spare_ul_bits.resize(n_rntis);
  sharded_results[dci_decoder_id].dl_dcis.resize(n_rntis);
  sharded_results[dci_decoder_id].ul_dcis.resize(n_rntis);

  sharded_rntis[dci_decoder_id].resize(n_rntis);
  nof_sharded_rntis[dci_decoder_id] = n_rntis;
  // std::cout << "nof_sharded_rntis[dci_decoder_id]: "
  // << nof_sharded_rntis[dci_decoder_id] << std::endl;




  // std::cout << "sharded_rntis: ";
  for (uint32_t i = 0; i < n_rntis; i++) {
    sharded_rntis[dci_decoder_id][i] = state->known_rntis[rnti_s + i];
    // std::cout << sharded_rntis[dci_decoder_id][i] << ", ";
  }
  // std::cout << std::endl;

  // Set the buffer to 0s
  for (uint32_t idx = 0; idx < n_rntis; idx++) {
    memset(&dci_dl[idx], 0, sizeof(srsran_dci_dl_nr_t));
    memset(&dci_ul[idx], 0, sizeof(srsran_dci_dl_nr_t));
  }

  srsran_ue_dl_nr_estimate_fft_nrscope(&ue_dl_dci, slot, arg_scs);

  int total_dl_dci = 0;
  int total_ul_dci = 0;

  // Candidate-first blind search, CA + non-CA DCI sizes merged: one pass
  // replaces the old per-RNTI CA-then-non-CA sweep (see
  // DecodeandParseDCIfromSlot), decoding each unique candidate location once
  // and CRC-trialing it against every claiming RNTI.
  memcpy(ue_dl_tmp, &ue_dl_dci, sizeof(srsran_ue_dl_nr_t));
  memcpy(slot_tmp, slot, sizeof(srsran_slot_cfg_t));
  std::vector<srsran_dci_dl_nr_t> dl_cf(n_rntis);
  std::vector<srsran_dci_ul_nr_t> ul_cf(n_rntis);
  std::vector<int> ndl_cf(n_rntis), nul_cf(n_rntis);
  // nrscope_candidate_first_find_dci(&ue_dl_dci, &dci_nr_nca, slot,
  nrscope_candidate_first_find_dci(ue_dl_tmp, &dci_nr_nca, slot_tmp,
      sharded_rntis[dci_decoder_id].data(), n_rntis, srsran_rnti_type_c,
      dl_cf.data(), ndl_cf.data(), ul_cf.data(), nul_cf.data());
  
  // Publish the per-RNTI first hits into dci_dl[]/dci_ul[], which the grant
  // processing below reads exactly as in the original path.
  for (uint32_t i = 0; i < n_rntis; i++) {
    if (ndl_cf[i] > 0) { dci_dl[i] = dl_cf[i]; total_dl_dci += ndl_cf[i]; }
    if (nul_cf[i] > 0) { dci_ul[i] = ul_cf[i]; total_ul_dci += nul_cf[i]; }
  }

  if (total_dl_dci > 0) {
    for (uint32_t dci_idx_dl = 0; dci_idx_dl < n_rntis; dci_idx_dl++) {
      // the rnti will not be copied if no dci found
      if (dci_dl[dci_idx_dl].ctx.rnti == sharded_rntis[dci_decoder_id][dci_idx_dl]) {
        sharded_results[dci_decoder_id].dl_dcis[dci_idx_dl] = dci_dl[dci_idx_dl];
        char str[1024]                                      = {};
        // The grant may not be decoded correctly, since srsRAN's code is not complete.
        // We can calculate the DL bandwidth for this subframe by ourselves.
        if (dci_dl[dci_idx_dl].ctx.format == srsran_dci_format_nr_1_1) {
          srsran_sch_cfg_nr_t pdsch_cfg = {};
          pdsch_cfg.dmrs.typeA_pos      = state->cell.mib.dmrs_typeA_pos;

          if (srsran_ra_dl_dci_to_grant_nr(
                  &carrier_dl, slot, &pdsch_hl_cfg, &dci_dl[dci_idx_dl], &pdsch_cfg, &pdsch_cfg.grant) <
              SRSRAN_SUCCESS) {
            ERROR("Error decoding PDSCH search");
            // return result;
          }

          sharded_results[dci_decoder_id].dl_grants[dci_idx_dl] = pdsch_cfg;
          sharded_results[dci_decoder_id].nof_dl_used_prbs += pdsch_cfg.grant.nof_prb * pdsch_cfg.grant.L;

          dl_prb_rate[dci_idx_dl + rnti_s] = (float)(pdsch_cfg.grant.tb[0].tbs + pdsch_cfg.grant.tb[1].tbs) /
                                             (float)pdsch_cfg.grant.nof_prb / (float)pdsch_cfg.grant.L;
          dl_prb_bits_rate[dci_idx_dl + rnti_s] =
              (float)(pdsch_cfg.grant.tb[0].nof_bits + pdsch_cfg.grant.tb[1].nof_bits) /
              (float)pdsch_cfg.grant.nof_prb / (float)pdsch_cfg.grant.L;
        }
      }
    }
  } else { }

  if (total_ul_dci > 0) {
    for (uint32_t dci_idx_ul = 0; dci_idx_ul < n_rntis; dci_idx_ul++) {
      if (dci_ul[dci_idx_ul].ctx.rnti == sharded_rntis[dci_decoder_id][dci_idx_ul]) {
        sharded_results[dci_decoder_id].ul_dcis[dci_idx_ul] = dci_ul[dci_idx_ul];
        char str[1024]                                      = {};
        // The grant may not be decoded correctly, since srsRAN's code is not complete.
        // We can calculate the UL bandwidth for this subframe by ourselves.
        srsran_sch_cfg_nr_t pusch_cfg = {};
        pusch_cfg.dmrs.typeA_pos      = state->cell.mib.dmrs_typeA_pos;
        if (srsran_ra_ul_dci_to_grant_nr(
                &carrier_ul, slot, &pusch_hl_cfg, &dci_ul[dci_idx_ul], &pusch_cfg, &pusch_cfg.grant) < SRSRAN_SUCCESS) {
          ERROR("Error decoding PUSCH search");
          // return result;
        }

        sharded_results[dci_decoder_id].ul_grants[dci_idx_ul] = pusch_cfg;
        sharded_results[dci_decoder_id].nof_ul_used_prbs += pusch_cfg.grant.nof_prb * pusch_cfg.grant.L;

        ul_prb_rate[dci_idx_ul + rnti_s] = (float)(pusch_cfg.grant.tb[0].tbs + pusch_cfg.grant.tb[1].tbs) /
                                           (float)pusch_cfg.grant.nof_prb / (float)pusch_cfg.grant.L;
        ul_prb_bits_rate[dci_idx_ul + rnti_s] =
            (float)(pusch_cfg.grant.tb[0].nof_bits + pusch_cfg.grant.tb[1].nof_bits) / (float)pusch_cfg.grant.nof_prb /
            (float)pusch_cfg.grant.L;
      }
    }
  } else {  }

  return SRSRAN_SUCCESS;
}
