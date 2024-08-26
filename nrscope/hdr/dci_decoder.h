#ifndef DCI_DECODER_H
#define DCI_DECODER_H

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/task_scheduler.h"

class DCIDecoder{
  public:
    srsran_carrier_nr_t base_carrier;
    srsran_ue_dl_nr_sratescs_info arg_scs;
    srsran_sch_hl_cfg_nr_t pdsch_hl_cfg;
    srsran_sch_hl_cfg_nr_t pusch_hl_cfg;
    srsran_softbuffer_rx_t softbuffer;
    srsran_dci_cfg_nr_t dci_cfg;
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

    // Hidden bwp search data structures (will be actively used only if hidden_bwp == true)
    bool hidden_bwp = false; 
    uint8_t possible_coreset_total_num;
    // Each possible hidden coreset central freq location
    std::vector <int> coreset_central_freqs;
    // Ring buf for each possible coreset that track decoded dci num for last 1000 slots
    std::vector <std::vector <uint32_t>> dl_dci_num_1000_tracker;
    std::vector <std::vector <uint32_t>> ul_dci_num_1000_tracker;
    // for the above ring buf
    uint32_t cur_tracker_idx = 0;

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

    int dci_decoder_and_reception_init(srsran_ue_dl_nr_sratescs_info arg_scs_,
                                       TaskSchedulerNRScope* task_scheduler_nrscope,
                                       cf_t* input[SRSRAN_MAX_PORTS],
                                       u_int8_t bwp_id);

    int decode_and_parse_dci_from_slot(srsran_slot_cfg_t* slot,
                                       TaskSchedulerNRScope* task_scheduler_nrscope,
                                       cf_t * raw_buffer);

    // int dci_thread(TaskSchedulerNRScope* task_scheduler_nrscope);
};

#endif