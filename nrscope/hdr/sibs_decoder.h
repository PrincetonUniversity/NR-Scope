#ifndef SIBS_DECODER_H
#define SIBS_DECODER_H

#include "srsran/support/srsran_assert.h"

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/task_scheduler.h"

class SIBsDecoder{
  public:
    uint8_t* data_pdcch;
    srsran_ue_dl_nr_sratescs_info arg_scs;
    srsran_carrier_nr_t base_carrier;
    srsran_sch_hl_cfg_nr_t pdsch_hl_cfg;
    srsran_softbuffer_rx_t softbuffer;
    asn1::rrc_nr::sib1_s sib1;

    srsran_ue_dl_nr_t ue_dl_sibs;
    srsran_dci_cfg_nr_t dci_cfg;
    srsran_dci_dl_nr_t dci_sibs;
    srsran_ue_dl_nr_args_t ue_dl_args;
    srsran_pdcch_cfg_nr_t  pdcch_cfg;
    srsran_sch_cfg_nr_t pdsch_cfg;
    srsran_pdsch_res_nr_t pdsch_res;
    
    srsran_coreset_t coreset0_t;
    srsran_search_space_t* search_space;

    cell_search_result_t cell;

    long int file_position = 0;


    SIBsDecoder();
    ~SIBsDecoder();

    /**
    * This function initialize the decoder with the required context for SIB decoding, we can't decode SIB
    * without these parameters.
    * 
    * @param arg_scs_: it contains basic information about the radio and cell, such as sampling rate, 
    * SCS and phase difference.
    * @param base_carrier_: it contains the information about the carrier of the cell, such as how many
    * PRBs are contained in the carrier and SSB center frequency.
    * @param cell_: cell search result, containing cell's SSB patterns, SCS, etc.
    * @param input: the buffer's address, which the USRP use to store radio samples, and it's used for  
    * setting the ue_dl object so that all thread shares the same buffer for processing.
    * @param coreset0_t_: the parameters of CORESET 0, with which this function can localize SIB 1 
    * in time and frequency.
    * 
    * @return SRSRAN_SUCCESS (0) if everything goes well. 
    * SRSRAN_ERROR (-1) if something is wrong in the function.
    */
    int sib_decoder_and_reception_init(srsran_ue_dl_nr_sratescs_info arg_scs_,
                                       TaskSchedulerNRScope* task_scheduler_nrscope,
                                       cf_t* input[SRSRAN_MAX_PORTS]);

    /**
    * This function decodes the current slot for SIB 1, and the result will reflect in the sib1 parameter.
    * 
    * @param slot: current slot index within the system frame (1-20 for 30kHz SCS).
    * @param sib1: the pointer to the address where the SIB 1 information should be stored.
    * Maybe change this to a task_scheduler_ngscope object.
    * 
    * @return SRSRAN_SUCCESS (0) if everything goes well. 
    * SRSRAN_ERROR (-1) if something is wrong in the function.
    */
    int decode_and_parse_sib1_from_slot(srsran_slot_cfg_t* slot,
                                        TaskSchedulerNRScope* task_scheduler_nrscope,
                                        cf_t * raw_buffer);

    // /**
    // * A function that represents the SIB thread for a producer-consumer threading design,
    // * but currently we don't adopt such design.
    // * 
    // * @return SRSRAN_SUCCESS (0) if everything goes well. 
    // * SRSRAN_ERROR (-1) if something is wrong in the function.
    // */
    // int sibs_thread(srsran_ue_dl_nr_sratescs_info arg_scs_, 
    //                 TaskSchedulerNRScope* task_scheduler_nrscope, 
    //                 cf_t* input[SRSRAN_MAX_PORTS]);
};

#endif