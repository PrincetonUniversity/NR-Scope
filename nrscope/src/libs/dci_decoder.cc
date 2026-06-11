#include "nrscope/hdr/dci_decoder.h"

DCIDecoder::DCIDecoder(uint32_t max_nof_rntis)
{
  ue_dl_tmp = (srsran_ue_dl_nr_t*)malloc(sizeof(srsran_ue_dl_nr_t));
  slot_tmp  = (srsran_slot_cfg_t*)malloc(sizeof(srsran_slot_cfg_t));

  dci_dl = (srsran_dci_dl_nr_t*)malloc(sizeof(srsran_dci_dl_nr_t) * (max_nof_rntis));
  dci_ul = (srsran_dci_ul_nr_t*)malloc(sizeof(srsran_dci_ul_nr_t) * (max_nof_rntis));
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




/******* optimized decoder method ********/

#define TDCISTART(name) struct timeval name##_t0, name##_t1; gettimeofday(&name##_t0, NULL);
#define TDCIEND(name)   gettimeofday(&name##_t1, NULL); \
  if (print_enabled) { \
    printf(#name ": %ld (us)\n", (name##_t1.tv_sec - name##_t0.tv_sec) * 1000000L + (name##_t1.tv_usec - name##_t0.tv_usec)); \
  }


/*************** INLINED srsran_ue_dl_nr_find_dl_dci_nrscope_dciloop ***************/
// Flattened, single-file copy of the entire blind-search call chain so the
// control flow can be read and instrumented in one place. Logic is copied
// verbatim from the originals; commented-out code, debug prints and the
// optional meas_time bookkeeping are stripped. Leaf DSP kernels (polar,
// demodulator, equalizer, scrambling sequence, CRC, vector ops) remain
// library calls.
//
// Original layers, top to bottom:
//   srsran_ue_dl_nr_find_dl_dci_nrscope_dciloop       lib/src/phy/ue/ue_dl_nr.c:1251
//   ue_dl_nr_find_dci_ss_nrscope_dciloop              lib/src/phy/ue/ue_dl_nr.c:963
//   ue_dl_nr_find_dci_ncce_nrscope_dciloop            lib/src/phy/ue/ue_dl_nr.c:532
//   srsran_pdcch_nr_decode_with_rnti_nrscope_dciloop  lib/src/phy/phch/pdcch_nr.c:975
// plus their static helpers (copied below with a flat_ prefix):
//   find_dci_msg                                      lib/src/phy/ue/ue_dl_nr.c:632
//   srsran_pdcch_calculate_Y_p_n / srsran_pdcch_nr_get_ncce /
//     srsran_pdcch_nr_locations_coreset               lib/src/phy/phch/pdcch_nr.c:41/59/106
//   pdcch_nr_cp / pdcch_nr_c_init                     lib/src/phy/phch/pdcch_nr.c:407/461
//   srsran_dmrs_pdcch_get_measure / _get_ce           lib/src/phy/ch_estimation/dmrs_pdcch.c:491/611
//
// UL-format hits are still enqueued on q->ul_dci_msg, so a following call to
// srsran_ue_dl_nr_find_ul_dci() works unchanged.

// The polar/evm headers have no extern "C" guards of their own (they are
// normally only included from C translation units), so guard them here.
extern "C" {
#include "srsran/phy/fec/polar/polar_chanalloc.h"
#include "srsran/phy/fec/polar/polar_code.h"
#include "srsran/phy/fec/polar/polar_decoder.h"
#include "srsran/phy/fec/polar/polar_interleaver.h"
#include "srsran/phy/fec/polar/polar_rm.h"
#include "srsran/phy/modem/evm.h"
}
#include "srsran/phy/ue/srsgui_plot.h"

// File-local constants of the original .c files (not exported in any header):
// dmrs_pdcch.c:33, dmrs_pdcch.c:40, pdcch_nr.c:32
#define FLAT_NOF_PILOTS_X_RB 3
#define FLAT_DMRS_PDCCH_MAX_NOF_PILOTS_CANDIDATE                                                                       \
  ((SRSRAN_NRE / 3) * (1U << (SRSRAN_SEARCH_SPACE_NOF_AGGREGATION_LEVELS_NR - 1U)) * 6U)
#define FLAT_PDCCH_NR_POLAR_RM_IBIL 0

// cf_t is GCC's _Complex float; these two avoid pulling C99 <complex.h> into a
// C++ translation unit just for conjf()/cargf().
static inline cf_t flat_conjf(cf_t x)
{
  __imag__ x = -__imag__ x;
  return x;
}

static inline float flat_cargf(cf_t x)
{
  return atan2f(__imag__ x, __real__ x);
}

// pdcch_nr.c:41 — RNTI hash for UE-specific search space candidate placement
// (TS 38.213 10.1). This is why candidate CCE locations differ per RNTI.
static uint32_t flat_pdcch_calculate_Y_p_n(uint32_t coreset_id, uint16_t rnti, uint32_t n)
{
  static const uint32_t A_p[3] = {39827, 39829, 39839};
  const uint32_t        D      = 65537;

  uint32_t Y_p_n = (uint32_t)rnti;
  for (uint32_t i = 0; i <= n; i++) {
    Y_p_n = (A_p[coreset_id % 3] * Y_p_n) % D;
  }

  return Y_p_n;
}

// pdcch_nr.c:59 — CCE index of one candidate
static int flat_pdcch_nr_get_ncce(const srsran_coreset_t*      coreset,
                                  const srsran_search_space_t* search_space,
                                  uint16_t                     rnti,
                                  uint32_t                     aggregation_level,
                                  uint32_t                     slot_idx,
                                  uint32_t                     candidate)
{
  if (aggregation_level >= SRSRAN_SEARCH_SPACE_NOF_AGGREGATION_LEVELS_NR) {
    ERROR("Invalid aggregation level %d;", aggregation_level);
    return SRSRAN_ERROR;
  }

  uint32_t L    = 1U << aggregation_level;                         // Aggregation level
  uint32_t n_ci = 0;                                               // Carrier indicator field
  uint32_t m    = candidate;                                       // Selected PDCCH candidate
  uint32_t M    = search_space->nof_candidates[aggregation_level]; // Number of candidates

  if (M == 0) {
    ERROR("Invalid number of candidates %d for aggregation level %d", M, aggregation_level);
    return SRSRAN_ERROR;
  }

  // Every REG is 1 PRB wide and a CCE is 6 REGs
  uint32_t coreset_bw = srsran_coreset_get_bw(coreset);
  uint32_t N_cce      = coreset_bw * coreset->duration / 6;

  if (N_cce < L) {
    ERROR("Error CORESET (total bandwidth of %d RBs and %d CCEs) cannot fit the aggregation level %d (%d)",
          coreset_bw,
          N_cce,
          L,
          aggregation_level);
    return SRSRAN_ERROR;
  }

  // Y_p_n is 0 for common search spaces, RNTI hash for UE search space
  uint32_t Y_p_n = 0;
  if (search_space->type == srsran_search_space_type_ue) {
    Y_p_n = flat_pdcch_calculate_Y_p_n(coreset->id, rnti, slot_idx);
  }

  return (int)(L * ((Y_p_n + (m * N_cce) / (L * M) + n_ci) % (N_cce / L)));
}

// pdcch_nr.c:106 — all candidate CCE locations for one aggregation level
static int flat_pdcch_nr_locations_coreset(const srsran_coreset_t*      coreset,
                                           const srsran_search_space_t* search_space,
                                           uint16_t                     rnti,
                                           uint32_t                     aggregation_level,
                                           uint32_t                     slot_idx,
                                           uint32_t locations[SRSRAN_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR])
{
  if (coreset == NULL || search_space == NULL) {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }

  uint32_t nof_candidates = search_space->nof_candidates[aggregation_level];
  nof_candidates          = SRSRAN_MIN(nof_candidates, SRSRAN_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR);

  for (uint32_t candidate = 0; candidate < nof_candidates; candidate++) {
    int ret = flat_pdcch_nr_get_ncce(coreset, search_space, rnti, aggregation_level, slot_idx, candidate);
    if (ret < SRSRAN_SUCCESS) {
      return ret;
    }
    locations[candidate] = ret;
  }

  return nof_candidates;
}

// ue_dl_nr.c:632 — dedup check against the already-found DCI list
static bool flat_find_dci_msg(srsran_dci_msg_nr_t* dci_msg, uint32_t nof_dci_msg, srsran_dci_msg_nr_t* match)
{
  bool     found    = false;
  uint32_t nof_bits = match->nof_bits;

  for (uint32_t k = 0; k < nof_dci_msg && !found; k++) {
    if (dci_msg[k].nof_bits == nof_bits) {
      if (memcmp(dci_msg[k].payload, match->payload, nof_bits) == 0) {
        found = true;
      }
    }
  }

  return found;
}

// pdcch_nr.c:407 — copy the candidate's REs between resource grid and symbol
// buffer (put=false reads from the grid, skipping the DMRS REs at k%4==1)
static uint32_t flat_pdcch_nr_cp(const srsran_pdcch_nr_t*     q,
                                 const srsran_dci_location_t* dci_location,
                                 cf_t*                        slot_grid,
                                 cf_t*                        symbols,
                                 bool                         put)
{
  uint32_t offset_k = q->coreset.offset_rb * SRSRAN_NRE;

  // Compute REG list
  bool rb_mask[SRSRAN_MAX_PRB_NR] = {};
  if (srsran_pdcch_nr_cce_to_reg_mapping(&q->coreset, dci_location, rb_mask) < SRSRAN_SUCCESS) {
    return 0;
  }

  uint32_t count = 0;

  // Iterate over symbols
  for (uint32_t l = 0; l < q->coreset.duration; l++) {
    // Iterate over frequency resource groups
    uint32_t rb = 0;
    for (uint32_t r = 0; r < SRSRAN_CORESET_FREQ_DOMAIN_RES_SIZE; r++) {
      if (!q->coreset.freq_resources[r]) {
        continue;
      }

      // For each RB in the frequency resource
      for (uint32_t i = r * 6; i < (r + 1) * 6; i++, rb++) {
        if (!rb_mask[rb]) {
          continue;
        }

        // For each RE in the RB
        for (uint32_t k = i * SRSRAN_NRE; k < (i + 1) * SRSRAN_NRE; k++) {
          // Skip if it is a DMRS
          if (k % 4 == 1) {
            continue;
          }

          if (put) {
            slot_grid[q->carrier.nof_prb * SRSRAN_NRE * l + k + offset_k] = symbols[count++];
          } else {
            symbols[count++] = slot_grid[q->carrier.nof_prb * SRSRAN_NRE * l + k + offset_k];
          }
        }
      }
    }
  }

  return count;
}

// pdcch_nr.c:461 — scrambling sequence init. Note: RNTI-dependent only when
// the CORESET configures pdcch-DMRS-ScramblingID and this is a UE search
// space; otherwise identical for all RNTIs.
static uint32_t flat_pdcch_nr_c_init(const srsran_pdcch_nr_t* q, const srsran_dci_msg_nr_t* dci_msg)
{
  uint32_t n_id   = (dci_msg->ctx.ss_type == srsran_search_space_type_ue && q->coreset.dmrs_scrambling_id_present)
                        ? q->coreset.dmrs_scrambling_id
                        : q->carrier.pci;
  uint32_t n_rnti = (dci_msg->ctx.ss_type == srsran_search_space_type_ue && q->coreset.dmrs_scrambling_id_present)
                        ? dci_msg->ctx.rnti
                        : 0U;
  return ((n_rnti << 16U) + n_id) & 0x7fffffffU;
}

// dmrs_pdcch.c:491 — measure correlation/EPRE of one candidate from the
// per-slot least-squares estimates (q->lse, filled once per slot by
// srsran_dmrs_pdcch_estimate_nrscope)
static int flat_dmrs_pdcch_get_measure(const srsran_dmrs_pdcch_estimator_t* q,
                                       const srsran_dci_location_t*         dci_location,
                                       srsran_dmrs_pdcch_measure_t*         measure)
{
  if (q == NULL || dci_location == NULL || measure == NULL) {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }

  if (q->coreset.duration < SRSRAN_CORESET_DURATION_MIN) {
    ERROR("Invalid CORESET duration");
    return SRSRAN_ERROR;
  }

  // Calculate CCE-to-REG mapping mask
  bool rb_mask[SRSRAN_MAX_PRB_NR] = {};
  if (srsran_pdcch_nr_cce_to_reg_mapping(&q->coreset, dci_location, rb_mask) < SRSRAN_SUCCESS) {
    ERROR("Error in CCE-to-REG mapping");
    return SRSRAN_SUCCESS;
  }

  float rsrp                              = 0.0f; //< Averages linear RSRP
  float epre                              = 0.0f; //< Averages linear EPRE
  float cfo_avg_Hz                        = 0.0f; //< Averages CFO in Radians
  float sync_err_avg                      = 0.0f; //< Averages synchronization
  cf_t  corr[SRSRAN_CORESET_DURATION_MAX] = {};   //< Saves correlation for the different symbols

  // For each CORESET symbol
  for (uint32_t l = 0; l < q->coreset.duration; l++) {
    // Temporal least square estimates
    cf_t     tmp[FLAT_DMRS_PDCCH_MAX_NOF_PILOTS_CANDIDATE] = {};
    uint32_t nof_pilots                                    = 0;

    // For each RB in the CORESET
    for (uint32_t rb = 0; rb < q->coreset_bw; rb++) {
      if (!rb_mask[rb]) {
        continue;
      }
      srsran_vec_cf_copy(&tmp[nof_pilots], &q->lse[l][rb * FLAT_NOF_PILOTS_X_RB], FLAT_NOF_PILOTS_X_RB);
      nof_pilots += FLAT_NOF_PILOTS_X_RB;
    }

    // Measure synchronization error and accumulate for average
    float tmp_sync_err = srsran_vec_estimate_frequency(tmp, nof_pilots);
    sync_err_avg += tmp_sync_err;

    // Pre-compensate synchronization error (DMRS_PDCCH_SYNC_PRECOMPENSATE_MEAS=1)
    srsran_vec_apply_cfo(tmp, tmp_sync_err, tmp, nof_pilots);

    // Prevent undefined division
    if (!nof_pilots) {
      ERROR("Error in DMRS correlation. nof_pilots cannot be zero");
      return SRSRAN_ERROR;
    }

    // Correlate DMRS
    corr[l] = srsran_vec_acc_cc(tmp, nof_pilots) / (float)nof_pilots;

    // Measure symbol RSRP
    rsrp += __real__ corr[l] * __real__ corr[l] + __imag__ corr[l] * __imag__ corr[l];

    // Measure symbol EPRE
    epre += srsran_vec_avg_power_cf(tmp, nof_pilots);

    // Measure CFO only from the second and third symbols
    if (l != 0) {
      float Ts = srsran_symbol_distance_s(l - 1, l, q->carrier.scs);
      if (isnormal(Ts)) {
        cfo_avg_Hz += flat_cargf(corr[l] * flat_conjf(corr[l - 1])) / (2.0f * (float)M_PI * Ts);
      }
    }
  }

  // Store results
  measure->rsrp = rsrp / (float)q->coreset.duration;
  measure->epre = epre / (float)q->coreset.duration;
  if (q->coreset.duration > 1) {
    // NOTE: verbatim from the original, which divides measure->cfo_hz (zeroed
    // by the caller) instead of storing cfo_avg_Hz — so cfo_hz is always 0.
    measure->cfo_hz /= (float)(q->coreset.duration - 1);
  } else {
    measure->cfo_hz = NAN;
  }
  (void)cfo_avg_Hz;
  measure->sync_error_us =
      sync_err_avg / (4.0e-6f * (float)q->coreset.duration * SRSRAN_SUBC_SPACING_NR(q->carrier.scs));

  // Convert power measurements into logarithmic scale
  measure->rsrp_dBfs = srsran_convert_power_to_dB(measure->rsrp);
  measure->epre_dBfs = srsran_convert_power_to_dB(measure->epre);

  // Store DMRS correlation
  if (isnormal(measure->rsrp) && isnormal(measure->epre)) {
    measure->norm_corr = measure->rsrp / measure->epre;
  } else {
    measure->norm_corr = 0.0f;
  }

  return SRSRAN_SUCCESS;
}

// dmrs_pdcch.c:611 — copy the candidate's channel estimates out of the
// per-slot estimate buffer (q->ce), skipping DMRS REs
static int flat_dmrs_pdcch_get_ce(const srsran_dmrs_pdcch_estimator_t* q,
                                  const srsran_dci_location_t*         dci_location,
                                  srsran_dmrs_pdcch_ce_t*              ce)
{
  if (q == NULL || dci_location == NULL || ce == NULL) {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }

  uint32_t L = 1U << dci_location->L;

  if (q->coreset.duration < SRSRAN_CORESET_DURATION_MIN) {
    ERROR("Invalid CORESET duration");
    return SRSRAN_ERROR;
  }

  // Calculate CCE-to-REG mapping mask
  bool rb_mask[SRSRAN_MAX_PRB_NR] = {};
  if (srsran_pdcch_nr_cce_to_reg_mapping(&q->coreset, dci_location, rb_mask) < SRSRAN_SUCCESS) {
    ERROR("Error in CCE-to-REG mapping");
    return SRSRAN_SUCCESS;
  }

  uint32_t count = 0;

  // For each PDCCH symbol
  for (uint32_t l = 0; l < q->coreset.duration; l++) {
    // For each CORESET RB
    for (uint32_t rb = 0; rb < q->coreset_bw; rb++) {
      if (!rb_mask[rb]) {
        continue;
      }

      // Copy RB, skipping DMRS
      for (uint32_t k = rb * SRSRAN_NRE; k < (rb + 1) * SRSRAN_NRE; k++) {
        if (k % 4 != 1) {
          ce->ce[count++] = q->ce[q->coreset_bw * SRSRAN_NRE * l + k];
        }
      }
    }
  }

  // Double check extracted RE match ideal count
  ce->nof_re = (SRSRAN_NRE - 3) * 6 * L;
  if (count != ce->nof_re) {
    ERROR("Incorrect number of extracted resources (%d != %d)", count, ce->nof_re);
  }

  // At the moment Noise is not calculated
  ce->noise_var = 0.0f;

  return SRSRAN_SUCCESS;
}

// Per-sweep stage timing, reset at the top of every nrscope_flat_find_dl_dci
// call and accumulated across the innermost loop. thread_local so concurrent
// DCI decoder threads don't race; read it right after the call, same thread.
struct FlatSweepStats {
  uint32_t n_candidates;     // innermost-loop iterations
  uint32_t n_decoded;        // candidates that passed all gates (full pipeline)
  int64_t  t_measure_ns;     // DMRS measure (runs for every candidate)
  int64_t  t_prep_ns;        // CE extract + RE copy + equalize + demod
  int64_t  t_evm_ns;         // srsran_evm_run_b
  int64_t  t_descr_rm_ns;    // LLR negate + descramble + rate dematch
  int64_t  t_polar_ns;       // polar decode
  int64_t  t_tail_ns;        // chanalloc + deinterleave + CRC + payload copy
};
static thread_local FlatSweepStats flat_sweep_stats;

static inline int64_t flat_now_ns()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// Drop-in replacement for srsran_ue_dl_nr_find_dl_dci_nrscope_dciloop():
// identical signature, behavior and side effects. All control-flow layers are
// inlined below; loop nesting is search space > DCI format (size-deduped) >
// aggregation level > candidate location.
int nrscope_flat_find_dl_dci(srsran_ue_dl_nr_t*       q,
                             const srsran_slot_cfg_t* slot_cfg,
                             uint16_t                 rnti,
                             srsran_rnti_type_t       rnti_type,
                             srsran_dci_dl_nr_t*      dci_dl_list,
                             uint32_t                 nof_dci_msg)
{
  if (q == NULL || slot_cfg == NULL || dci_dl_list == NULL) {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }

  // Limit maximum number of DCI messages to find
  nof_dci_msg = SRSRAN_MIN(nof_dci_msg, SRSRAN_MAX_DCI_MSG_NR);

  // Reset grant and blind search information counters
  q->dl_dci_msg_count = 0;
  q->pdcch_info_count = 0;

  flat_sweep_stats = {};

  // ========== layer: srsran_ue_dl_nr_find_dl_dci_nrscope_dciloop ==========
  // Iterate all possible common and UE search spaces
  for (uint32_t ss_idx = 0; ss_idx < SRSRAN_UE_DL_NR_MAX_NOF_SEARCH_SPACE && q->dl_dci_msg_count < nof_dci_msg;
       ss_idx++) {
    if (!q->cfg.search_space_present[ss_idx]) {
      continue;
    }
    const srsran_search_space_t* search_space = &q->cfg.search_space[ss_idx];

    // ========== layer: ue_dl_nr_find_dci_ss_nrscope_dciloop ==========
    uint32_t dci_sizes[SRSRAN_DCI_NR_MAX_NOF_SIZES] = {};
    uint32_t dci_sizes_count                        = 0;

    // Select CORESET
    uint32_t coreset_id = search_space->coreset_id;
    if (coreset_id >= SRSRAN_UE_DL_NR_MAX_NOF_CORESET || !q->cfg.coreset_present[coreset_id]) {
      ERROR("CORESET %d is not present in search space %d", search_space->coreset_id, search_space->id);
      return SRSRAN_ERROR;
    }
    srsran_coreset_t* coreset = &q->cfg.coreset[coreset_id];

    // Set CORESET in PDCCH decoder (inlined srsran_pdcch_nr_set_carrier)
    q->pdcch.carrier = q->carrier;
    q->pdcch.coreset = *coreset;

    // Iterate all possible formats
    for (uint32_t format_idx = 0; format_idx < SRSRAN_MIN(search_space->nof_formats, SRSRAN_DCI_FORMAT_NR_COUNT);
         format_idx++) {
      srsran_dci_format_nr_t dci_format = search_space->formats[format_idx];

      // Calculate number of DCI bits
      uint32_t dci_nof_bits = srsran_dci_nr_size(&q->dci, search_space->type, dci_format);
      if (rnti_type == srsran_rnti_type_si) {
        dci_nof_bits = srsran_dci_nr_size(&q->dci, srsran_search_space_type_common_0, srsran_dci_format_nr_1_0);
      }
      if (dci_nof_bits == 0) {
        ERROR("Error DCI size");
        return SRSRAN_ERROR;
      }

      // Skip DCI format if the size was already searched for the search space
      bool skip = false;
      for (uint32_t i = 0; i < dci_sizes_count && !skip; i++) {
        if (dci_nof_bits == dci_sizes[i]) {
          skip = true;
        }
      }
      if (skip) {
        continue;
      }

      // Append size
      if (dci_sizes_count >= SRSRAN_DCI_NR_MAX_NOF_SIZES) {
        ERROR("Exceed maximum number of DCI sizes");
        return SRSRAN_ERROR;
      }
      dci_sizes[dci_sizes_count++] = dci_nof_bits;

      // Iterate all possible aggregation levels
      for (uint32_t L = 0;
           L < SRSRAN_SEARCH_SPACE_NOF_AGGREGATION_LEVELS_NR && q->dl_dci_msg_count < SRSRAN_MAX_DCI_MSG_NR;
           L++) {
        // Calculate possible PDCCH DCI candidates (RNTI-dependent in UE SS)
        uint32_t candidates[SRSRAN_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR] = {};
        int      nof_candidates                                        = flat_pdcch_nr_locations_coreset(
            coreset, search_space, rnti, L, SRSRAN_SLOT_NR_MOD(q->carrier.scs, slot_cfg->idx), candidates);
        if (nof_candidates < SRSRAN_SUCCESS) {
          ERROR("Error calculating DCI candidate location");
          return SRSRAN_ERROR;
        }

        // Iterate over the candidates
        for (int ncce_idx = 0; ncce_idx < nof_candidates && q->dl_dci_msg_count < SRSRAN_MAX_DCI_MSG_NR; ncce_idx++) {
          // Build DCI context
          srsran_dci_ctx_t ctx = {};
          ctx.location.L       = L;
          ctx.location.ncce    = candidates[ncce_idx];
          ctx.ss_type          = search_space->type;
          ctx.coreset_id       = search_space->coreset_id;
          ctx.coreset_start_rb = srsran_coreset_start_rb(&q->cfg.coreset[search_space->coreset_id]);
          ctx.rnti_type        = rnti_type;
          ctx.rnti             = rnti;
          ctx.format           = dci_format;

          // Build DCI message
          srsran_dci_msg_nr_t dci_msg = {};
          dci_msg.ctx                 = ctx;
          dci_msg.nof_bits            = (uint32_t)dci_nof_bits;

          // For SI-RNTI checking
          if (rnti_type == srsran_rnti_type_si) {
            dci_msg.ctx.ss_type = srsran_search_space_type_common_0A;
          }

          // ========== layer: ue_dl_nr_find_dci_ncce_nrscope_dciloop ==========
          // Per-candidate measurement/debug bookkeeping
          if (q->pdcch_info_count >= SRSRAN_MAX_NOF_CANDIDATES_SLOT_NR) {
            ERROR("The UE does not expect more than %d candidates in this serving cell",
                  SRSRAN_MAX_NOF_CANDIDATES_SLOT_NR);
            return SRSRAN_ERROR;
          }
          srsran_ue_dl_nr_pdcch_info_t* pdcch_info = &q->pdcch_info[q->pdcch_info_count];
          q->pdcch_info_count++;
          SRSRAN_MEM_ZERO(pdcch_info, srsran_ue_dl_nr_pdcch_info_t, 1);
          pdcch_info->dci_ctx            = dci_msg.ctx;
          pdcch_info->nof_bits           = dci_msg.nof_bits;
          srsran_dmrs_pdcch_measure_t* m = &pdcch_info->measure;

          
          flat_sweep_stats.n_candidates++;

          // Measure the PDCCH candidate's DMRS
          srsran_dci_location_t location = dci_msg.ctx.location;
          int64_t               ts0      = flat_now_ns();
          if (flat_dmrs_pdcch_get_measure(&q->dmrs_pdcch[coreset_id], &location, m) < SRSRAN_SUCCESS) {
            ERROR("Error getting measure location L=%d, ncce=%d", location.L, location.ncce);
            return SRSRAN_ERROR;
          }
          flat_sweep_stats.t_measure_ns += flat_now_ns() - ts0;

          // Gates: invalid measurement / EPRE / correlation (cheap early exits)
          if (!isnormal(m->norm_corr)) {
            continue;
          }
          if (m->epre_dBfs < q->pdcch_dmrs_epre_thr) {
            continue;
          }
          if (m->norm_corr < q->pdcch_dmrs_corr_thr) {
            continue;
          }

          flat_sweep_stats.n_decoded++;
          int64_t ts1 = flat_now_ns();

          // Extract PDCCH channel estimates
          if (flat_dmrs_pdcch_get_ce(&q->dmrs_pdcch[coreset_id], &location, q->pdcch_ce) < SRSRAN_SUCCESS) {
            ERROR("Error extracting PDCCH DMRS");
            return SRSRAN_ERROR;
          }

          // ========== layer: srsran_pdcch_nr_decode_with_rnti_nrscope_dciloop ==========
          srsran_pdcch_nr_t* pdcch = &q->pdcch;

          pdcch->K = dci_msg.nof_bits + 24U;                                  // Payload size including CRC
          pdcch->M = (1U << dci_msg.ctx.location.L) * (SRSRAN_NRE - 3U) * 6U; // Number of RE
          pdcch->E = pdcch->M * 2;                                            // Number of rate-matched bits

          // Check number of estimates is correct
          if (q->pdcch_ce->nof_re != pdcch->M) {
            ERROR("Invalid number of channel estimates (%d != %d)", pdcch->M, q->pdcch_ce->nof_re);
            return SRSRAN_ERROR;
          }

          // Get polar code
          if (srsran_polar_code_get(&pdcch->code, pdcch->K, pdcch->E, 9U) < SRSRAN_SUCCESS) {
            return SRSRAN_ERROR;
          }

          // Get symbols from grid
          uint32_t m_re = flat_pdcch_nr_cp(pdcch, &dci_msg.ctx.location, q->sf_symbols[0], pdcch->symbols, false);
          if (pdcch->M != m_re) {
            ERROR("Unmatch number of RE (%d != %d)", m_re, pdcch->M);
            return SRSRAN_ERROR;
          }

          // Equalise
          srsran_predecoding_single(
              pdcch->symbols, q->pdcch_ce->ce, pdcch->symbols, NULL, pdcch->M, 1.0f, q->pdcch_ce->noise_var);

          // Demodulation
          int8_t* llr = (int8_t*)pdcch->f;
          srsran_demod_soft_demodulate_b(SRSRAN_MOD_QPSK, pdcch->symbols, llr, pdcch->M);

          int64_t ts2 = flat_now_ns();
          flat_sweep_stats.t_prep_ns += ts2 - ts1;

          // Measure EVM if configured (NR-Scope sets pdcch.measure_evm=true,
          // so this runs for every decoded candidate)
          srsran_pdcch_nr_res_t res = {};
          if (pdcch->evm_buffer != NULL) {
            res.evm = srsran_evm_run_b(pdcch->evm_buffer, &pdcch->modem_table, pdcch->symbols, llr, pdcch->E);
          } else {
            res.evm = NAN;
          }

          int64_t ts3 = flat_now_ns();
          flat_sweep_stats.t_evm_ns += ts3 - ts2;

          // Negate all LLR
          for (uint32_t i = 0; i < pdcch->E; i++) {
            llr[i] *= -1;
          }

          // Descrambling
          srsran_sequence_apply_c(llr, llr, pdcch->E, flat_pdcch_nr_c_init(pdcch, &dci_msg));

          // Un-rate matching
          int8_t* d = (int8_t*)pdcch->d;
          if (srsran_polar_rm_rx_c(&pdcch->rm, llr, d, pdcch->E, pdcch->code.n, pdcch->K, FLAT_PDCCH_NR_POLAR_RM_IBIL) <
              SRSRAN_SUCCESS) {
            return SRSRAN_ERROR;
          }

          int64_t ts4 = flat_now_ns();
          flat_sweep_stats.t_descr_rm_ns += ts4 - ts3;

          // Decode (the expensive leaf)
          if (srsran_polar_decoder_decode_c(
                  &pdcch->decoder, d, pdcch->allocated, pdcch->code.n, pdcch->code.F_set, pdcch->code.F_set_size) <
              SRSRAN_SUCCESS) {
            return SRSRAN_ERROR;
          }

          int64_t ts5 = flat_now_ns();
          flat_sweep_stats.t_polar_ns += ts5 - ts4;

          // De-allocate channel
          uint8_t c_prime[SRSRAN_POLAR_INTERLEAVER_K_MAX_IL];
          srsran_polar_chanalloc_rx(
              pdcch->allocated, c_prime, pdcch->code.K, pdcch->code.nPC, pdcch->code.K_set, pdcch->code.PC_set);

          // Set first L bits to ones, c will have an offset of 24 bits
          uint8_t* c = pdcch->c;
          srsran_bit_unpack(UINT32_MAX, &c, 24U);

          // De-interleave
          srsran_polar_interleaver_run_u8(c_prime, c, pdcch->K, false);

          // Unpack RNTI
          uint8_t  unpacked_rnti[16] = {};
          uint8_t* ptr               = unpacked_rnti;
          srsran_bit_unpack(dci_msg.ctx.rnti, &ptr, 16);

          // De-Scramble CRC with RNTI (besides c_init above, the only
          // RNTI-dependent step of the whole decode)
          srsran_vec_xor_bbb(unpacked_rnti, &c[pdcch->K - 16], &c[pdcch->K - 16], 16);

          // Check CRC
          ptr                = &c[pdcch->K - 24];
          uint32_t checksum1 = srsran_crc_checksum(&pdcch->crc24c, pdcch->c, pdcch->K);
          uint32_t checksum2 = srsran_bit_pack(&ptr, 24);
          res.crc            = checksum1 == checksum2;

          // Copy DCI message
          srsran_vec_u8_copy(dci_msg.payload, c, dci_msg.nof_bits);

          // Save information
          pdcch_info->result = res;

          flat_sweep_stats.t_tail_ns += flat_now_ns() - ts5;

          // ========== back at layer: ue_dl_nr_find_dci_ss_nrscope_dciloop ==========
          // If the CRC did not match, move to next candidate
          if (!res.crc) {
            continue;
          }

          // Push equalized symbols to the constellation plot (no-op unless
          // the srsgui plot thread was initialized)
          push_node(pdcch->symbols, pdcch->M);

          // Detect if the DCI is the right direction; if not, flip the format
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

          // If UL grant, enqueue in the pending UL list, which a following
          // srsran_ue_dl_nr_find_ul_dci() call drains
          if (dci_msg.ctx.format == srsran_dci_format_nr_0_0 || dci_msg.ctx.format == srsran_dci_format_nr_0_1) {
            // If the pending UL grant list is full or has the dci message, keep moving
            if (q->ul_dci_count >= SRSRAN_MAX_DCI_MSG_NR || flat_find_dci_msg(q->ul_dci_msg, q->ul_dci_count, &dci_msg)) {
              continue;
            }
            q->ul_dci_msg[q->ul_dci_count] = dci_msg;
            q->ul_dci_count++;
            continue;
          }

          // Check if the grant exists already in the DL list
          if (flat_find_dci_msg(q->dl_dci_msg, q->dl_dci_msg_count, &dci_msg)) {
            continue;
          }

          // Append DCI message into the list
          q->dl_dci_msg[q->dl_dci_msg_count] = dci_msg;
          q->dl_dci_msg_count++;
        } // candidates
      }   // aggregation levels
    }     // formats
  }       // search spaces

  // ========== layer: back at srsran_ue_dl_nr_find_dl_dci_nrscope_dciloop ==========
  // Convert found DCI messages into DL grants
  uint32_t dci_msg_count = SRSRAN_MIN(nof_dci_msg, q->dl_dci_msg_count);
  for (uint32_t i = 0; i < dci_msg_count; i++) {
    if (srsran_dci_nr_dl_unpack(&q->dci, &q->dl_dci_msg[i], &dci_dl_list[i]) < SRSRAN_SUCCESS) {
      ERROR("Error unpacking grant %d;", i);
      return SRSRAN_ERROR;
    }
  }

  return (int)dci_msg_count;
}
/*************** END INLINED srsran_ue_dl_nr_find_dl_dci_nrscope_dciloop ***************/

/*************** CANDIDATE-FIRST BLIND SEARCH ***************/
// Restructured blind search: instead of one full sweep per RNTI (which
// re-measures and re-decodes the same physical candidate locations 8-12x),
// this enumerates the UNIQUE candidate locations across all RNTIs first and
// walks the pipeline once per location:
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
//  - Uses std::vector scratch; fine at ~60 locations/slot, can become a fixed
//    arena if allocation ever shows up in a profile.

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

// Per-call phase timing/counters for nrscope_candidate_first_find_dci,
// reset at the top of each call; read right after the call, same thread.
struct CandFirstStats {
  uint32_t n_locations;  // unique locations enumerated
  uint32_t n_passed;     // locations passing the gates (prep paid)
  uint32_t n_decodes;    // (location, size, c_init) polar decodes
  int64_t  t_enum_ns;    // phase 1: enumeration/dedup
  int64_t  t_proc_ns;    // phase 2: measure/prep/decode/CRC
  int64_t  t_unpack_ns;  // phase 3: unpack results
  // phase 2 breakdown (t_proc_ns minus these = loop/bookkeeping overhead)
  int64_t t_measure_ns;    // DMRS measure, all locations
  int64_t t_prep_ns;       // CE extract + RE copy + equalize + demod + EVM + LLR save
  int64_t t_groups_ns;     // per-(size,c_init) decode loop incl. CRC trials
  int64_t t_polar_get_ns;  // subset of t_groups_ns: srsran_polar_code_get only
};
static thread_local CandFirstStats cand_first_stats;

// One-shot diagnostic: the first call in the process dumps the full
// location/group table phase 1 built, to verify the dedup is working.
static std::atomic<bool> cand_first_dumped{false};

// srsran_polar_code_get() rebuilds the frozen-set tables (setdiff + two
// qsorts over N<=512) on every call, ~3us each, but the result depends only
// on (K, E) and a config sees just a handful of pairs (DCI sizes x
// aggregation levels). Cache them process-wide: append-only, entries are
// immutable once published, so readers scan lock-free and the mutex is only
// taken to insert. (Not thread_local: DCI threads are respawned every slot,
// which would rebuild the cache constantly and leak the entries' mallocs.)
#define POLAR_CODE_CACHE_SIZE 32
struct PolarCodeCacheEntry {
  uint16_t            K;
  uint16_t            E;
  srsran_polar_code_t code;
};
static PolarCodeCacheEntry   polar_code_cache[POLAR_CODE_CACHE_SIZE];
static std::atomic<uint32_t> polar_code_cache_count{0};
static std::mutex            polar_code_cache_mutex;

static const srsran_polar_code_t* flat_polar_code_get_cached(uint16_t K, uint16_t E)
{
  uint32_t n = polar_code_cache_count.load(std::memory_order_acquire);
  for (uint32_t i = 0; i < n; i++) {
    if (polar_code_cache[i].K == K && polar_code_cache[i].E == E) {
      return &polar_code_cache[i].code;
    }
  }

  std::lock_guard<std::mutex> lock(polar_code_cache_mutex);
  // Re-check under the lock: another thread may have inserted it meanwhile
  n = polar_code_cache_count.load(std::memory_order_acquire);
  for (uint32_t i = 0; i < n; i++) {
    if (polar_code_cache[i].K == K && polar_code_cache[i].E == E) {
      return &polar_code_cache[i].code;
    }
  }
  if (n >= POLAR_CODE_CACHE_SIZE) {
    return NULL; // caller falls back to computing per call
  }
  PolarCodeCacheEntry* entry = &polar_code_cache[n];
  if (srsran_polar_code_init(&entry->code) < SRSRAN_SUCCESS) {
    return NULL;
  }
  if (srsran_polar_code_get(&entry->code, K, E, 9U) < SRSRAN_SUCCESS) {
    return NULL;
  }
  entry->K = K;
  entry->E = E;
  polar_code_cache_count.store(n + 1, std::memory_order_release);
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

  cand_first_stats = {};
  int64_t t_phase  = flat_now_ns();

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
        int      nof_candidates                                        = flat_pdcch_nr_locations_coreset(
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

  cand_first_stats.n_locations = (uint32_t)locations.size();
  int64_t t_now                = flat_now_ns();
  cand_first_stats.t_enum_ns   = t_now - t_phase;
  t_phase                      = t_now;

  if (!cand_first_dumped.exchange(true)) {
    printf("cand_first location dump (%zu locations, %u rntis):\n", locations.size(), nof_rntis);
    for (auto& le : locations) {
      printf("  crst=%u L=%u ncce=%-3u groups=%zu:", le.coreset_id, le.loc.L, le.loc.ncce, le.groups.size());
      for (auto& g : le.groups) {
        printf(" (bits=%u fmt=%d ss=%d scr=0x%x cfg=%u nrnti=%zu)",
               g.nof_bits,
               (int)g.format,
               (int)g.ss_type,
               g.scr_rnti,
               g.cfg_idx,
               g.rnti_idxs.size());
      }
      printf("\n");
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
    int64_t tloc = flat_now_ns();
    if (flat_dmrs_pdcch_get_measure(&q->dmrs_pdcch[entry.coreset_id], &entry.loc, m) < SRSRAN_SUCCESS) {
      ERROR("Error getting measure location L=%d, ncce=%d", entry.loc.L, entry.loc.ncce);
      return SRSRAN_ERROR;
    }
    cand_first_stats.t_measure_ns += flat_now_ns() - tloc;

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

    cand_first_stats.n_passed++;
    tloc = flat_now_ns();

    // ---- per-location prep: CE extract + RE copy + equalize + demod ----
    if (flat_dmrs_pdcch_get_ce(&q->dmrs_pdcch[entry.coreset_id], &entry.loc, q->pdcch_ce) < SRSRAN_SUCCESS) {
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

    uint32_t m_re = flat_pdcch_nr_cp(pdcch, &entry.loc, q->sf_symbols[0], pdcch->symbols, false);
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

    int64_t tgroups = flat_now_ns();
    cand_first_stats.t_prep_ns += tgroups - tloc;

    // ---- per (size, format, c_init): dematch + polar decode + CRC prep ----
    for (auto& group : entry.groups) {
      cand_first_stats.n_decodes++;
      uint32_t K = group.nof_bits + 24U;
      pdcch->K   = K;

      int64_t tpg = flat_now_ns();
      const srsran_polar_code_t* code = flat_polar_code_get_cached(K, E);
      if (code == NULL) {
        // Cache unavailable (full or init failure): compute per call
        if (srsran_polar_code_get(&pdcch->code, K, E, 9U) < SRSRAN_SUCCESS) {
          return SRSRAN_ERROR;
        }
        code = &pdcch->code;
      }
      cand_first_stats.t_polar_get_ns += flat_now_ns() - tpg;

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
      if (srsran_polar_rm_rx_c(&pdcch->rm, llr, d, E, code->n, K, FLAT_PDCCH_NR_POLAR_RM_IBIL) < SRSRAN_SUCCESS) {
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
    cand_first_stats.t_groups_ns += flat_now_ns() - tgroups;
  }

  t_now                      = flat_now_ns();
  cand_first_stats.t_proc_ns = t_now - t_phase;
  t_phase                    = t_now;

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

  cand_first_stats.t_unpack_ns = flat_now_ns() - t_phase;

  return total;
}
/*************** END CANDIDATE-FIRST BLIND SEARCH ***************/


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
  // Workers process slots concurrently; emit logs from only one of them.
  const bool print_enabled = (worker_id == 0);

  if (!state->rach_found or !state->dci_inited) {
    if (print_enabled) {
      std::cout << "RACH not found or DCI decoder not initialized, quitting..." << std::endl;
    }
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

  if (print_enabled) {
    printf("Size of srsran_ue_dl_nr_t and srsran_slot_cfg_t: %lu, %lu\n", sizeof(srsran_ue_dl_nr_t), sizeof(srsran_slot_cfg_t));
  }


  // Unique physical candidate locations across all RNTIs of this CA sweep:
  // key = (coreset_id, L, ncce); the *_size sets also distinguish DCI size.
  // These tell us what a candidate-first restructure would actually pay:
  // uniq_meas ~ measure+prep work, uniq_pass_size ~ polar-decode work.
  // std::set<uint64_t> uniq_meas, uniq_pass, uniq_pass_size;

  
  // Candidate-first pass, CA + non-CA sizes merged (validation: results
  // compared against the old path, not used)
  memcpy(ue_dl_tmp, &ue_dl_dci, sizeof(srsran_ue_dl_nr_t));
  memcpy(slot_tmp, slot, sizeof(srsran_slot_cfg_t));
  std::vector<srsran_dci_dl_nr_t> dl_cf(n_rntis);
  std::vector<srsran_dci_ul_nr_t> ul_cf(n_rntis);
  std::vector<int> ndl_cf(n_rntis), nul_cf(n_rntis);
  TDCISTART(t_candidate_first)
  // nrscope_candidate_first_find_dci(&ue_dl_dci, &dci_nr_nca, slot,
  nrscope_candidate_first_find_dci(ue_dl_tmp, &dci_nr_nca, slot_tmp,
      sharded_rntis[dci_decoder_id].data(), n_rntis, srsran_rnti_type_c,
      dl_cf.data(), ndl_cf.data(), ul_cf.data(), nul_cf.data());
  TDCIEND(t_candidate_first)
  if (print_enabled) {
    printf("cand_first phases us: enum=%.1f proc=%.1f unpack=%.1f (locs=%u passed=%u decodes=%u)\n",
          cand_first_stats.t_enum_ns / 1e3,
          cand_first_stats.t_proc_ns / 1e3,
          cand_first_stats.t_unpack_ns / 1e3,
          cand_first_stats.n_locations,
          cand_first_stats.n_passed,
          cand_first_stats.n_decodes);
printf("cand_first proc us: measure=%.1f prep=%.1f groups=%.1f (polar_get=%.1f) other=%.1f\n",
       cand_first_stats.t_measure_ns / 1e3, cand_first_stats.t_prep_ns / 1e3,
       cand_first_stats.t_groups_ns / 1e3, cand_first_stats.t_polar_get_ns / 1e3,
       (cand_first_stats.t_proc_ns - cand_first_stats.t_measure_ns -
        cand_first_stats.t_prep_ns - cand_first_stats.t_groups_ns) / 1e3);
        }
  

  // TDCISTART(t_rnti_loop)
  // for (uint32_t rnti_idx = 0; rnti_idx < n_rntis; rnti_idx++) {
  //   // With carrier aggregation
  //   memcpy(ue_dl_tmp, &ue_dl_dci, sizeof(srsran_ue_dl_nr_t));
  //   memcpy(slot_tmp, slot, sizeof(srsran_slot_cfg_t));

  //   // int nof_dl_dci = srsran_ue_dl_nr_find_dl_dci_nrscope_dciloop(
  //   //     ue_dl_tmp, slot_tmp, sharded_rntis[dci_decoder_id][rnti_idx], srsran_rnti_type_c, dci_dl_tmp, 4);
  //   int nof_dl_dci = nrscope_flat_find_dl_dci(
  //       ue_dl_tmp, slot_tmp, sharded_rntis[dci_decoder_id][rnti_idx], srsran_rnti_type_c, dci_dl_tmp, 4);

  //   if (print_enabled) {
  //     printf("rnti_idx: %d, sweep candidates: %d\n", rnti_idx, ue_dl_tmp->pdcch_info_count);
  //   }
  //   int n_decoded = 0;
  //   for (uint32_t i = 0; i < ue_dl_tmp->pdcch_info_count; i++) {
  //     const srsran_ue_dl_nr_pdcch_info_t* info = &ue_dl_tmp->pdcch_info[i];
  //     const srsran_dmrs_pdcch_measure_t*  m    = &info->measure;

  //     // (coreset_id, L, ncce) packed into one key; size-aware key adds nof_bits
  //     uint64_t loc_key  = ((uint64_t)info->dci_ctx.coreset_id << 24) | ((uint64_t)info->dci_ctx.location.L << 16) |
  //                         (uint64_t)info->dci_ctx.location.ncce;
  //     uint64_t size_key = ((uint64_t)info->nof_bits << 32) | loc_key;
  //     uniq_meas.insert(loc_key);

  //     if (isnormal(m->norm_corr) && m->epre_dBfs >= ue_dl_tmp->pdcch_dmrs_epre_thr &&
  //         m->norm_corr >= ue_dl_tmp->pdcch_dmrs_corr_thr) {
  //       n_decoded++;
  //       uniq_pass.insert(loc_key);
  //       uniq_pass_size.insert(size_key);
  //     }
  //   }
  //   if (print_enabled) {
  //     printf("rnti_idx: %d, decoded candidates: %d\n", rnti_idx, n_decoded);
  //     printf("rnti_idx: %d, stage us: measure=%.1f prep=%.1f evm=%.1f descr_rm=%.1f polar=%.1f tail=%.1f "
  //            "(decoded %u of %u)\n",
  //            rnti_idx,
  //            flat_sweep_stats.t_measure_ns / 1e3,
  //            flat_sweep_stats.t_prep_ns / 1e3,
  //            flat_sweep_stats.t_evm_ns / 1e3,
  //            flat_sweep_stats.t_descr_rm_ns / 1e3,
  //            flat_sweep_stats.t_polar_ns / 1e3,
  //            flat_sweep_stats.t_tail_ns / 1e3,
  //            flat_sweep_stats.n_decoded,
  //            flat_sweep_stats.n_candidates);
  //   }

  //   if (nof_dl_dci < SRSRAN_SUCCESS) {
  //     ERROR("Error in blind search");
  //   }

  //   int nof_ul_dci = srsran_ue_dl_nr_find_ul_dci(
  //       ue_dl_tmp, slot_tmp, sharded_rntis[dci_decoder_id][rnti_idx], srsran_rnti_type_c, dci_ul_tmp, 4);


  //   if (nof_dl_dci > 0) {
  //     dci_dl[rnti_idx] = dci_dl_tmp[0];
  //     total_dl_dci += nof_dl_dci;
  //   }

  //   if (nof_ul_dci > 0) {
  //     dci_ul[rnti_idx] = dci_ul_tmp[0];
  //     total_ul_dci += nof_ul_dci;
  //   }

  //   // printf("slot: %d\n", slot->idx);
  //   // for (uint32_t pdcch_idx = 0; pdcch_idx < ue_dl_tmp->pdcch_info_count; pdcch_idx++) {
  //   //   const srsran_ue_dl_nr_pdcch_info_t* info = &(ue_dl_tmp->pdcch_info[pdcch_idx]);
  //   //   printf("PDCCH: %s-rnti=0x%x, crst_id=%d, ss_type=%s, ncce=%d, al=%d, EPRE=%+.2f, RSRP=%+.2f, corr=%.3f; "
  //   //   "nof_bits=%d; crc=%s;\n",
  //   //   srsran_rnti_type_str_short(info->dci_ctx.rnti_type),
  //   //   info->dci_ctx.rnti,
  //   //   info->dci_ctx.coreset_id,
  //   //   srsran_ss_type_str(info->dci_ctx.ss_type),
  //   //   info->dci_ctx.location.ncce,
  //   //   info->dci_ctx.location.L,
  //   //   info->measure.epre_dBfs,
  //   //   info->measure.rsrp_dBfs,
  //   //   info->measure.norm_corr,
  //   //   info->nof_bits,
  //   //   info->result.crc ? "OK" : "KO");
  //   // }

  //   if (nof_ul_dci > 0 || nof_dl_dci > 0) {
  //     // The UE is either using CA or not, so if we find the DCI with CA,
  //     // we don't need to try further.
  //     // NRScopePlot::push_node(ue_dl_tmp->pdcch.symbols, ue_dl_tmp->pdcch.M);
  //     // printf("M=%d\n", ue_dl_tmp->pdcch.M);
  //     // printf("symbols=");
  //     // srsran_vec_fprint_c(stdout, ue_dl_tmp->pdcch.symbols, ue_dl_tmp->pdcch.M);
  //     if (print_enabled) {
  //       printf("DCIDecoder -- DCI found with CA\n");
  //     }
  //     continue;
  //   }

  //   memcpy(ue_dl_tmp, &ue_dl_dci, sizeof(srsran_ue_dl_nr_t));
  //   memcpy(slot_tmp, slot, sizeof(srsran_slot_cfg_t));

  //   // Set the DCI size for the non-carrier aggregation UEs.
  //   if (srsran_ue_dl_nr_set_pdcch_config(ue_dl_tmp, &pdcch_cfg, &dci_cfg)) {
  //     ERROR("Error setting CORESET");
  //     return SRSRAN_ERROR;
  //   }

  //   // printf("id: %d, search space: %d, l1: %d, l2: %d, l3: %d, l4: %d, l5: %d\n",
  //   //   0,
  //   //   ue_dl_tmp->cfg.search_space[0].id,
  //   //   ue_dl_tmp->cfg.search_space[0].nof_candidates[0],
  //   //   ue_dl_tmp->cfg.search_space[0].nof_candidates[1],
  //   //   ue_dl_tmp->cfg.search_space[0].nof_candidates[2],
  //   //   ue_dl_tmp->cfg.search_space[0].nof_candidates[3],
  //   //   ue_dl_tmp->cfg.search_space[0].nof_candidates[4]
  //   // );

  //   // printf("id: %d, search space: %d, l1: %d, l2: %d, l3: %d, l4: %d, l5: %d\n",
  //   //   1,
  //   //   ue_dl_tmp->cfg.search_space[1].id,
  //   //   ue_dl_tmp->cfg.search_space[1].nof_candidates[0],
  //   //   ue_dl_tmp->cfg.search_space[1].nof_candidates[1],
  //   //   ue_dl_tmp->cfg.search_space[1].nof_candidates[2],
  //   //   ue_dl_tmp->cfg.search_space[1].nof_candidates[3],
  //   //   ue_dl_tmp->cfg.search_space[1].nof_candidates[4]
  //   // );

  //   // int nof_dl_dci_nca = srsran_ue_dl_nr_find_dl_dci_nrscope_dciloop(
  //   //     ue_dl_tmp, slot_tmp, sharded_rntis[dci_decoder_id][rnti_idx], srsran_rnti_type_c, dci_dl_tmp, 4);
  //   int nof_dl_dci_nca = nrscope_flat_find_dl_dci(
  //       ue_dl_tmp, slot_tmp, sharded_rntis[dci_decoder_id][rnti_idx], srsran_rnti_type_c, dci_dl_tmp, 4);


  //   if (nof_dl_dci_nca < SRSRAN_SUCCESS) {
  //     ERROR("Error in blind search");
  //   }

  //   int nof_ul_dci_nca = srsran_ue_dl_nr_find_ul_dci(
  //       ue_dl_tmp, slot_tmp, sharded_rntis[dci_decoder_id][rnti_idx], srsran_rnti_type_c, dci_ul_tmp, 4);

  //   if (nof_dl_dci_nca > 0) {
  //     dci_dl[rnti_idx] = dci_dl_tmp[0];
  //     total_dl_dci += nof_dl_dci_nca;
  //   }

  //   if (nof_ul_dci_nca > 0) {
  //     dci_ul[rnti_idx] = dci_ul_tmp[0];
  //     total_ul_dci += nof_ul_dci_nca;
  //   }

  //   // printf("slot: %d\n", slot->idx);
  //   // for (uint32_t pdcch_idx = 0; pdcch_idx < ue_dl_tmp->pdcch_info_count; pdcch_idx++) {
  //   //   const srsran_ue_dl_nr_pdcch_info_t* info = &(ue_dl_tmp->pdcch_info[pdcch_idx]);
  //   //   printf("PDCCH: %s-rnti=0x%x, crst_id=%d, ss_type=%s, ncce=%d, al=%d, EPRE=%+.2f, RSRP=%+.2f, corr=%.3f; "
  //   //   "nof_bits=%d; crc=%s;\n",
  //   //   srsran_rnti_type_str_short(info->dci_ctx.rnti_type),
  //   //   info->dci_ctx.rnti,
  //   //   info->dci_ctx.coreset_id,
  //   //   srsran_ss_type_str(info->dci_ctx.ss_type),
  //   //   info->dci_ctx.location.ncce,
  //   //   info->dci_ctx.location.L,
  //   //   info->measure.epre_dBfs,
  //   //   info->measure.rsrp_dBfs,
  //   //   info->measure.norm_corr,
  //   //   info->nof_bits,
  //   //   info->result.crc ? "OK" : "KO");
  //   // }
  //   if (nof_dl_dci_nca > 0 || nof_ul_dci_nca > 0) {
  //     // NRScopePlot::push_node(ue_dl_tmp->pdcch.symbols, ue_dl_tmp->pdcch.M);
  //     // printf("M=%d\n", ue_dl_tmp->pdcch.M);
  //     // printf("symbols=");
  //     // srsran_vec_fprint_c(stdout, ue_dl_tmp->pdcch.symbols, ue_dl_tmp->pdcch.M);
  //     if (print_enabled) {
  //       printf("DCIDecoder -- DCI Found without CA\n");
  //     }
  //   }
  // }
  // if (print_enabled) {
  //   printf("Number of DL RNTIs: %d\n", n_rntis);
  // }
  // TDCIEND(t_rnti_loop)

  // if (print_enabled) {
  //   for (uint32_t i = 0; i < n_rntis; i++) {
  //     bool old_dl = (dci_dl[i].ctx.rnti == sharded_rntis[dci_decoder_id][i]);
  //     bool old_ul = (dci_ul[i].ctx.rnti == sharded_rntis[dci_decoder_id][i]);
  //     bool new_dl = ndl_cf[i] > 0;
  //     bool new_ul = nul_cf[i] > 0;
  //     if (old_dl != new_dl || old_ul != new_ul) {
  //       printf("CF MISMATCH rnti_idx %u: old dl=%d ul=%d, new dl=%d ul=%d\n", i, old_dl, old_ul, new_dl, new_ul);
  //       continue;
  //     }
  //     if (old_dl) {
  //       char s_old[1024] = {}, s_new[1024] = {};
  //       srsran_dci_dl_nr_to_str(&(ue_dl_dci.dci), &dci_dl[i], s_old, sizeof(s_old));
  //       srsran_dci_dl_nr_to_str(&(ue_dl_dci.dci), &dl_cf[i], s_new, sizeof(s_new));
  //       if (strcmp(s_old, s_new) != 0)
  //         printf("CF DL CONTENT MISMATCH rnti_idx %u:\n  old: %s\n  new: %s\n", i, s_old, s_new);
  //     }
  //     if (old_ul) {
  //       char s_old[1024] = {}, s_new[1024] = {};
  //       srsran_dci_ul_nr_to_str(&(ue_dl_dci.dci), &dci_ul[i], s_old, sizeof(s_old));
  //       srsran_dci_ul_nr_to_str(&(ue_dl_dci.dci), &ul_cf[i], s_new, sizeof(s_new));
  //       if (strcmp(s_old, s_new) != 0)
  //         printf("CF UL CONTENT MISMATCH rnti_idx %u:\n  old: %s\n  new: %s\n", i, s_old, s_new);
  //     }
  //   }
  // }


  // if (print_enabled) {
  //   printf("unique locations measured: %zu, passing gates: %zu, (location,size) decodes: %zu\n",
  //          uniq_meas.size(),
  //          uniq_pass.size(),
  //          uniq_pass_size.size());
  // }

  // CUTOVER.
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
        if (print_enabled) {
          srsran_dci_dl_nr_to_str(&(ue_dl_dci.dci), &dci_dl[dci_idx_dl], str, (uint32_t)sizeof(str));
          printf("DCIDecoder -- Found DCI: %s\n", str);
        }
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
          if (print_enabled) {
            srsran_sch_cfg_nr_info(&pdsch_cfg, str, (uint32_t)sizeof(str));
            printf("DCIDecoder -- PDSCH_cfg:\n%s", str);
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
        if (print_enabled) {
          srsran_dci_ul_nr_to_str(&(ue_dl_dci.dci), &dci_ul[dci_idx_ul], str, (uint32_t)sizeof(str));
          printf("DCIDecoder -- Found DCI: %s\n", str);
        }
        // The grant may not be decoded correctly, since srsRAN's code is not complete.
        // We can calculate the UL bandwidth for this subframe by ourselves.
        srsran_sch_cfg_nr_t pusch_cfg = {};
        pusch_cfg.dmrs.typeA_pos      = state->cell.mib.dmrs_typeA_pos;
        if (srsran_ra_ul_dci_to_grant_nr(
                &carrier_ul, slot, &pusch_hl_cfg, &dci_ul[dci_idx_ul], &pusch_cfg, &pusch_cfg.grant) < SRSRAN_SUCCESS) {
          ERROR("Error decoding PUSCH search");
          // return result;
        }
        if (print_enabled) {
          srsran_sch_cfg_nr_info(&pusch_cfg, str, (uint32_t)sizeof(str));
          printf("DCIDecoder -- PUSCH_cfg:\n%s", str);
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

