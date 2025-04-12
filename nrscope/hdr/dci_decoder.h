#ifndef DCI_DECODER_H
#define DCI_DECODER_H

#include "nrscope/hdr/nrscope_def.h"
// #include "nrscope/hdr/task_scheduler.h"

class DCIDecoder{
  public:
    srsran_carrier_nr_t base_carrier;
    srsran_ue_dl_nr_sratescs_info arg_scs;
    srsran_sch_hl_cfg_nr_t pdsch_hl_cfg;
    srsran_sch_hl_cfg_nr_t pusch_hl_cfg;
    srsran_softbuffer_rx_t softbuffer;
    srsran_dci_cfg_nr_t dci_cfg; // DCI format without carrier aggregation
    srsran_dci_cfg_nr_t dci_cfg_ca; // DCI format with carrier aggregation
    srsran_ue_dl_nr_args_t ue_dl_args;
    srsran_pdcch_cfg_nr_t  pdcch_cfg;
    
    cell_search_result_t cell;
    srsran_coreset_t coreset0_t;
    srsran_search_space_t* search_space;

    asn1::rrc_nr::sib1_s sib1;
    asn1::rrc_nr::cell_group_cfg_s master_cell_group;
    asn1::rrc_nr::rrc_setup_s rrc_setup;

    srsran_carrier_nr_t carrier_dl;
    srsran_carrier_nr_t carrier_ul;
    srsran_ue_dl_nr_t ue_dl_dci;
    srsue::nr::cell_search::cfg_t srsran_searcher_cfg_t;
    srsran_coreset_t coreset1_t; 

    uint32_t dci_decoder_id;
    uint32_t rnti_worker_group_id;
    uint8_t bwp_worker_id;

    // std::vector<float> dl_prb_rate;
    // std::vector<float> ul_prb_rate;

    // std::vector<float> dl_prb_bits_rate;
    // std::vector<float> ul_prb_bits_rate;

    srsran_dci_dl_nr_t dci_dl_tmp[4];
    srsran_dci_ul_nr_t dci_ul_tmp[4];

    srsran_ue_dl_nr_t* ue_dl_tmp;
    srsran_slot_cfg_t* slot_tmp;

    srsran_dci_dl_nr_t* dci_dl;
    srsran_dci_ul_nr_t* dci_ul;

    // uint8_t* data_pdcch;
    // srsran_pdsch_res_nr_t pdsch_res;

    DCIDecoder(uint32_t max_nof_rntis);
    ~DCIDecoder();

    int DCIDecoderandReceptionInit(WorkState* state,
                                   int bwp_id,
                                   cf_t* input[SRSRAN_MAX_PORTS]);

    int DecodeandParseDCIfromSlot(srsran_slot_cfg_t* slot,
                        WorkState* state,
                        std::vector <DCIFeedback>& sharded_results,
                        std::vector <std::vector <uint16_t> >& sharded_rntis,
                        std::vector<uint32_t>& nof_sharded_rntis,
                        std::vector <float>& dl_prb_rate,
                        std::vector <float>& dl_prb_bits_rate,
                        std::vector <float>& ul_prb_rate,
                        std::vector <float>& ul_prb_bits_rate);

    // int dci_thread(TaskSchedulerNRScope* task_scheduler_nrscope);
};

#endif