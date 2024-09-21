#ifndef RACH_DECODER_H
#define RACH_DECODER_H

#include <mutex>
#include <iostream>

#include "srsran/config.h"
#include "srsran/asn1/rrc_nr.h"
#include "srsran/asn1/asn1_utils.h"
#include "srsran/common/band_helper.h"

#include "nrscope/hdr/nrscope_def.h"
// #include "nrscope/hdr/task_scheduler.h"

class RachDecoder{
  public:
    asn1::rrc_nr::sib1_s sib1; 
    srsran_carrier_nr_t base_carrier;
    srsran_carrier_nr_t pdsch_carrier;
    prach_nr_config_t prach_cfg_nr;
    srsran_prach_t prach;
    srsran_prach_cfg_t prach_cfg;

    srsran_dci_dl_nr_t* dci_rach;
    srsran_sch_cfg_nr_t pdsch_cfg;

    uint16_t *ra_rnti;
    uint32_t nof_ra_rnti;
    int start_rb;

    srsran_ue_dl_nr_sratescs_info arg_scs;
    srsran_ue_dl_nr_sratescs_info arg_scs_pdsch;
    srsran_sch_hl_cfg_nr_t pdsch_hl_cfg;
    srsran_softbuffer_rx_t softbuffer;
    srsran_dci_cfg_nr_t dci_cfg;
    srsran_ue_dl_nr_args_t ue_dl_args;
    srsran_pdcch_cfg_nr_t  pdcch_cfg;
    srsran_pdsch_res_nr_t pdsch_res;
    
    srsran_ue_dl_nr_t ue_dl_rach;
    srsran_ue_dl_nr_t ue_dl_pdsch;
    
    cell_search_result_t cell;
    srsran_coreset_t coreset0_t;
    srsran_search_space_t* search_space;
    // srsran_search_space_t* ra_search_space;

    uint8_t* data_pdcch;

    RachDecoder();
    ~RachDecoder();

    int RACHDecoderInit(WorkState state);

    int RACHReceptionInit(WorkState* state,
                          cf_t* input[SRSRAN_MAX_PORTS]);

    int DecodeandParseMS4fromSlot(srsran_slot_cfg_t* slot,
                          WorkState* state,
                          SlotResult* result);
    
    // int rach_thread(TaskSchedulerNRScope* task_scheduler_nrscope);
};

#endif