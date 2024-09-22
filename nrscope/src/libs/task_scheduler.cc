#include "nrscope/hdr/task_scheduler.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace NRScopeTask{

TaskSchedulerNRScope::TaskSchedulerNRScope(){
  task_scheduler_state.sib1_inited = false;
  task_scheduler_state.rach_inited = false;
  task_scheduler_state.dci_inited = false;
  task_scheduler_state.sib1_found = false;
  task_scheduler_state.rach_found = false;
  task_scheduler_state.nof_known_rntis = 0;
  task_scheduler_state.known_rntis.resize(task_scheduler_state.nof_known_rntis);

  next_result.sf_round = 0;
  next_result.slot.idx = 0;
  next_result.outcome.sfn = 0;
}

TaskSchedulerNRScope::~TaskSchedulerNRScope(){
}

int TaskSchedulerNRScope::InitandStart(bool local_log_,
                                       bool to_google_,
                                       int rf_index_,
                                       int32_t nof_threads, 
                                       uint32_t nof_rnti_worker_groups,
                                       uint8_t nof_bwps,
                                       bool cpu_affinity,
                                       cell_searcher_args_t args_t,
                                       uint32_t nof_workers_){
  local_log = local_log_;
  to_google = to_google_;
  rf_index = rf_index_;
  task_scheduler_state.nof_threads = nof_threads;
  task_scheduler_state.nof_rnti_worker_groups = nof_rnti_worker_groups;
  task_scheduler_state.nof_bwps = nof_bwps;
  task_scheduler_state.args_t = args_t;
  task_scheduler_state.slot_sz = (uint32_t)(args_t.srate_hz / 1000.0f / 
    SRSRAN_NOF_SLOTS_PER_SF_NR(args_t.ssb_scs));
  task_scheduler_state.cpu_affinity = cpu_affinity;
  nof_workers = nof_workers_;

  /**
   * Load the hidden bwp db early 
   * 
   * Currently in "proof-of-concept" mode, where the db contains just 
   * the MOSOLAB 369 cell's hidden bwp info in 40 MHz mode. More specifically,
   * 
   * BWP0: start RB 0 and Num RB 78
   * BWP1: start RB 0 and Num RB 106
   * 
   * TO-DOs: 
   * (1) test on commerical/MOSOLAB cells where BWP1 may start at a higher location
   * (so we need to guess the start which is multiple of 6 RB)
   * 
   * (2) interface with the actual db (ready)
   * 
   * (3) for more than one non-initial bwp, use energy detection method to map to the
   * right hidden bwp config, as discussed in our meeting
   * 
   * Nice to have:
   * (1) CRC check for different dci size(s) for cross-validation, where use the 
   * dci size with CRC == 1 to serve as a target and maybe use dynamic programming
   * to tune the dci-size parameters towards the target size
   */
  std::ifstream f("369.txt");
  task_scheduler_state.js_hidden_bwp = json::parse(f);
  std::cout << "Starting workers..." << std::endl;
  for (uint32_t i = 0; i < nof_workers; i ++) {
    NRScopeWorker *worker = new NRScopeWorker();
    std::cout << "New worker " << i << " is going to start... "<< std::endl;
    if(worker->InitWorker(task_scheduler_state, i) < SRSRAN_SUCCESS) {
      ERROR("Error initializing worker %d", i);
      return NR_FAILURE;
    }
    workers.emplace_back(std::unique_ptr<NRScopeWorker> (worker));
  }

  std::cout << "Workers started..." << std::endl;

  scheduler_thread = std::thread{&TaskSchedulerNRScope::Run, this};
  scheduler_thread.detach();

  return SRSRAN_SUCCESS;
}

int TaskSchedulerNRScope::DecodeMIB(cell_searcher_args_t* args_t_, 
                        srsue::nr::cell_search::ret_t* cs_ret_,
                        srsue::nr::cell_search::cfg_t* srsran_searcher_cfg_t_,
                        float resample_ratio_,
                        uint32_t raw_srate_){
  args_t_->base_carrier.pci = cs_ret_->ssb_res.N_id;

  if(srsran_pbch_msg_nr_mib_unpack(&(cs_ret_->ssb_res.pbch_msg), 
      &task_scheduler_state.cell.mib) < SRSRAN_SUCCESS){
    ERROR("Error decoding MIB");
    return SRSRAN_ERROR;
  }

  char str[1024] = {};
  srsran_pbch_msg_nr_mib_info(&task_scheduler_state.cell.mib, str, 1024);
  printf("MIB: %s\n", str);
  // printf("MIB payload: ");
  // for (int i =0; i<SRSRAN_PBCH_MSG_NR_MAX_SZ; i++){
  //   printf("%hhu ", cs_ret_->ssb_res.pbch_msg.payload[i]);
  // }
  // printf("\n");

  /* already added the msb of k_ssb */
  task_scheduler_state.cell.k_ssb = task_scheduler_state.cell.mib.ssb_offset; 

  // srsran_coreset0_ssb_offset returns the offset_rb relative to ssb
  // nearly all bands in FR1 have min bandwidth 5 or 10 MHz, 
  // so there are only 5 entries here.  
  task_scheduler_state.coreset0_args_t.offset_rb = 
    srsran_coreset0_ssb_offset(task_scheduler_state.cell.mib.coreset0_idx, 
    args_t_->ssb_scs, task_scheduler_state.cell.mib.scs_common);

  task_scheduler_state.coreset0_t = {};
  /* srsran_coreset_zero returns the offset_rb relative to pointA */
  if(srsran_coreset_zero(cs_ret_->ssb_res.N_id, 
                        0,  
                        args_t_->ssb_scs, 
                        task_scheduler_state.cell.mib.scs_common, 
                        task_scheduler_state.cell.mib.coreset0_idx, 
                        &task_scheduler_state.coreset0_t) == SRSRAN_SUCCESS){
    char freq_res_str[SRSRAN_CORESET_FREQ_DOMAIN_RES_SIZE] = {};
    char coreset_info[512] = {};
    srsran_coreset_to_str(&task_scheduler_state.coreset0_t, coreset_info, 
      sizeof(coreset_info));
    printf("Coreset parameter: %s", coreset_info);
  }

  /* To find the position of coreset0, we need to use the offset between SSB 
    and CORESET0, because we don't know the ssb_pointA_freq_offset_Hz yet 
    required by the srsran_coreset_zero function.
    coreset0_t low bound freq = ssb center freq - 120 * scs (half of sc in ssb) 
    - ssb_subcarrierOffset(from MIB) * scs - entry->offset_rb * 
    12(sc in one rb) * scs */
  task_scheduler_state.cell.abs_ssb_scs = 
    SRSRAN_SUBC_SPACING_NR(args_t_->ssb_scs);
  task_scheduler_state.cell.abs_pdcch_scs = 
    SRSRAN_SUBC_SPACING_NR(task_scheduler_state.cell.mib.scs_common);
  
  srsran::srsran_band_helper bands;
  /* defined by standards */
  task_scheduler_state.coreset0_args_t.coreset0_lower_freq_hz = 
    srsran_searcher_cfg_t_->ssb_freq_hz - (SRSRAN_SSB_BW_SUBC / 2) *
    task_scheduler_state.cell.abs_ssb_scs - 
    task_scheduler_state.coreset0_args_t.offset_rb * NRSCOPE_NSC_PER_RB_NR * 
    task_scheduler_state.cell.abs_pdcch_scs - 
    task_scheduler_state.cell.k_ssb * 
    SRSRAN_SUBC_SPACING_NR(srsran_subcarrier_spacing_15kHz); 
  task_scheduler_state.coreset0_args_t.coreset0_center_freq_hz = 
    task_scheduler_state.coreset0_args_t.coreset0_lower_freq_hz + 
    srsran_coreset_get_bw(&task_scheduler_state.coreset0_t) / 2 * 
    task_scheduler_state.cell.abs_pdcch_scs * NRSCOPE_NSC_PER_RB_NR;

  coreset_zero_t_f_entry_nrscope coreset_zero_cfg;
  /* Get coreset_zero's position in time domain, check table 38.213, 13-11, 
    because USRP can only support FR1. */
  if(coreset_zero_t_f_nrscope(task_scheduler_state.cell.mib.ss0_idx, 
      task_scheduler_state.cell.mib.ssb_idx, 
      task_scheduler_state.coreset0_t.duration, &coreset_zero_cfg) < 
      SRSRAN_SUCCESS){
    ERROR("Error checking table 13-11");
    return SRSRAN_ERROR;
  }
  // std::cout << "After calling coreset_zero_t_f_nrscope" << std::endl;

  task_scheduler_state.cell.u = (int)args_t_->ssb_scs; 
  task_scheduler_state.coreset0_args_t.n_0 = (coreset_zero_cfg.O * 
    (int)pow(2, task_scheduler_state.cell.u) + 
    (int)floor(task_scheduler_state.cell.mib.ssb_idx * coreset_zero_cfg.M)) % 
    SRSRAN_NSLOTS_X_FRAME_NR(task_scheduler_state.cell.u);
  // sfn_c = 0, in even system frame, sfn_c = 1, in odd system frame    
  task_scheduler_state.coreset0_args_t.sfn_c = (int)(floor(coreset_zero_cfg.O * 
    pow(2, task_scheduler_state.cell.u) + 
    floor(task_scheduler_state.cell.mib.ssb_idx * coreset_zero_cfg.M)) / 
    SRSRAN_NSLOTS_X_FRAME_NR(task_scheduler_state.cell.u)) % 2;
  args_t_->base_carrier.nof_prb = 
    srsran_coreset_get_bw(&task_scheduler_state.coreset0_t);

  task_scheduler_state.args_t = *args_t_;
  task_scheduler_state.cs_ret = *cs_ret_;
  memcpy(&task_scheduler_state.srsran_searcher_cfg_t, srsran_searcher_cfg_t_, 
    sizeof(srsue::nr::cell_search::cfg_t));

  // initiate resampler here
  resample_ratio = resample_ratio_;
  float As=60.0f;
  resampler = msresamp_crcf_create(resample_ratio,As);
  resampler_delay = msresamp_crcf_get_delay(resampler);
  /* don't hardcode it; change later */
  pre_resampling_slot_sz = raw_srate_ / 1000 / 
    SRSRAN_NOF_SLOTS_PER_SF_NR(args_t_->ssb_scs); 
  temp_x_sz = pre_resampling_slot_sz + (int)ceilf(resampler_delay) + 10;
  temp_y_sz = (uint32_t)(temp_x_sz * resample_ratio * 2);
  temp_x = SRSRAN_MEM_ALLOC(std::complex<float>, temp_x_sz);
  temp_y = SRSRAN_MEM_ALLOC(std::complex<float>, temp_y_sz);

  return SRSRAN_SUCCESS;
}

int TaskSchedulerNRScope::UpdatewithResult(SlotResult now_result) {
  task_scheduler_lock.lock();
  /* This slot contains SIBs decoder's result */
  if (now_result.sib_result) {
    if (now_result.found_sib1 && !task_scheduler_state.sib1_found) {
      /* The sib1 is not found in the task_scheduler's state */
      /* We only process the SIB 1 once */
      task_scheduler_state.sib1 = now_result.sib1;
      task_scheduler_state.sib1_found = true;
      /* Setting the size of the vector for other SIBs decoding. */
      int nof_sibs = (task_scheduler_state.sib1).si_sched_info_present ? 
        (task_scheduler_state.sib1).si_sched_info.sched_info_list[0].
        sib_map_info.size() : 0;
      for (int i = 0; i < nof_sibs; i++) {
        task_scheduler_state.sibs_to_be_found.push_back(
          task_scheduler_state.sib1.si_sched_info.sched_info_list[0].
          sib_map_info[i].type.to_number());
      }
      /* Since we got the SIB1, we can now init the RACH decoder*/
      task_scheduler_state.rach_inited = true;
    }

    if (now_result.found_sib.size() > 0) {
      /* Found sibs other than SIB 1 */
      unsigned long int task_sibs_len = 
        task_scheduler_state.sibs_to_be_found.size();
      if (task_sibs_len > 0) {
        /* There are still some sibs to be found */
        for (unsigned long int i = 0; i < now_result.found_sib.size(); i ++) {
          int sib_id = now_result.found_sib[i];
          for (unsigned long int j = 0; j < task_sibs_len; j ++) {
            if (sib_id == task_scheduler_state.sibs_to_be_found[j]) {
              /* If the sib_id is in the to_be_found list */
              task_scheduler_state.found_sib.push_back(sib_id);
              task_scheduler_state.sibs.push_back(now_result.sibs[i]);
              task_scheduler_state.sibs_to_be_found.erase(
                task_scheduler_state.sibs_to_be_found.begin() + j);
              break;
            }
            /* Else skip the data */
          }
        }
      } 
      /* Or else, skip the sibs */
      if (task_scheduler_state.sibs_to_be_found.size() == 0) {
        task_scheduler_state.all_sibs_found = true;
      }
    }
  }

  /* This slot contains the RACH decoder's result */
  if (now_result.rach_result) {
    if (now_result.found_rach) {
      if (!task_scheduler_state.rach_found) {
        /* The first time that we found the RACH */
        task_scheduler_state.rrc_setup = now_result.rrc_setup;
        task_scheduler_state.master_cell_group = now_result.master_cell_group;
        task_scheduler_state.nof_known_rntis += now_result.new_rnti_number;
        for (uint32_t i = 0; i < now_result.new_rnti_number; i++) {
          task_scheduler_state.known_rntis.push_back(
            now_result.new_rntis_found[i]);
        }
        task_scheduler_state.rach_found = true;
      } else {
        /* We already found the RACH, we just append the new RNTIs */
        task_scheduler_state.nof_known_rntis += now_result.new_rnti_number;
        for (uint32_t i = 0; i < now_result.new_rnti_number; i++) {
          task_scheduler_state.known_rntis.push_back(
            now_result.new_rntis_found[i]);
        }
      }

      /* Since we got the RACH, we can now init the RACH decoder*/
      task_scheduler_state.dci_inited = true;
    }
  }
  task_scheduler_lock.unlock();

  /* This slot contains the DCI decoder's result, put all the results to log */
  if (now_result.dci_result) {
    std::vector <DCIFeedback> results = now_result.dci_feedback_results;
    for (uint8_t b = 0; b < task_scheduler_state.nof_bwps; b++) {
      DCIFeedback result = results[b];
      if((result.dl_grants.size()>0 or result.ul_grants.size()>0)){
        for (uint32_t i = 0; i < task_scheduler_state.nof_known_rntis; i++){
          if(result.dl_grants[i].grant.rnti == 
              task_scheduler_state.known_rntis[i]){
            LogNode log_node;
            log_node.slot_idx = now_result.slot.idx;
            log_node.system_frame_idx = now_result.outcome.sfn;
            log_node.timestamp = get_now_timestamp_in_double();
            log_node.grant = result.dl_grants[i];
            log_node.dci_format = 
              srsran_dci_format_nr_string(result.dl_dcis[i].ctx.format);
            log_node.dl_dci = result.dl_dcis[i];
            log_node.bwp_id = result.dl_dcis[i].bwp_id;
            if(local_log){
              NRScopeLog::push_node(log_node, rf_index);
            }
            if(to_google){
              ToGoogle::push_google_node(log_node, rf_index);
            }
          }

          if(result.ul_grants[i].grant.rnti == 
              task_scheduler_state.known_rntis[i]){
            LogNode log_node;
            log_node.slot_idx = now_result.slot.idx;
            log_node.system_frame_idx = now_result.outcome.sfn;
            log_node.timestamp = get_now_timestamp_in_double();
            log_node.grant = result.ul_grants[i];
            log_node.dci_format = 
              srsran_dci_format_nr_string(result.ul_dcis[i].ctx.format);
            log_node.ul_dci = result.ul_dcis[i];
            log_node.bwp_id = result.ul_dcis[i].bwp_id;
            if(local_log){
              NRScopeLog::push_node(log_node, rf_index);
            }
            if(to_google){
              ToGoogle::push_google_node(log_node, rf_index);
            }
          }
        } 
      }
    }
  }
  return SRSRAN_SUCCESS;
}

int TaskSchedulerNRScope::UpdateStateandLog() {
  /* Right now there is no re-order function, 
    just update the state and log*/
  std::sort(slot_results.begin(), slot_results.end());
  // std::cout << "expected sf_round: " << next_result.sf_round << std::endl;
  // std::cout << "expected sfn: " << next_result.outcome.sfn << std::endl;
  // std::cout << "expected slot: " << next_result.slot.idx << std::endl;
  // for (unsigned long int i = 0; i < slot_results.size(); i++) {
  //   std::cout << i << ", sfn: " << slot_results[i].outcome.sfn << std::endl;
  //   std::cout << i << ", sf_round: " << slot_results[i].sf_round << std::endl;
  //   std::cout << i << ", slot: " << slot_results[i].slot.idx << std::endl;
  // }
  while (//(slot_results[0] < next_result || slot_results[0] == next_result) && 
  slot_results.size() > 0) {
    // if (slot_results[0] < next_result){
    //   slot_results.erase(slot_results.begin());
    //   continue;
    // } else if (slot_results[0] == next_result){
    SlotResult now_result = slot_results[0];
    UpdatewithResult(now_result);
    slot_results.erase(slot_results.begin());
    UpdateNextResult();
    // } else {
    //   break;
    // }
  }
  return SRSRAN_SUCCESS;
}

void TaskSchedulerNRScope::UpdateNextResult() {
  if (next_result.slot.idx == 
    SRSRAN_NSLOTS_PER_FRAME_NR(task_scheduler_state.args_t.ssb_scs)-1) {
    next_result.slot.idx = 0;
    /* We will need to increase the outcome.sfn */
    if (next_result.outcome.sfn == 1023) {
      /* We will need to increase the sf_round */
      next_result.sf_round ++;
      next_result.outcome.sfn = 0;
    } else {
      next_result.outcome.sfn ++;
    }
  } else {
    next_result.slot.idx ++;
  }
}

void TaskSchedulerNRScope::Run() {
  while(true) {
    /* Try to extract results from the global result queue*/
    queue_lock.lock();
    auto queue_len = global_slot_results.size();
    if (queue_len > 0) {
      while (global_slot_results.size() > 0) {
        /* dequeue from the head of the queue */
        slot_results.push_back(global_slot_results[0]);
        global_slot_results.erase(global_slot_results.begin());
      }
    }
    queue_lock.unlock();

    /* reorder the local slot_results and 
      wait for the correct data for output */
    if (slot_results.size() > 0)
      UpdateStateandLog();
  }
}

int TaskSchedulerNRScope::AssignTask(uint64_t sf_round,
                                     srsran_slot_cfg_t slot, 
                                     srsran_ue_sync_nr_outcome_t outcome,
                                     cf_t* rx_buffer_){
  /* Find the first idle worker */
  bool found_worker = false;
  // std::cout << "Assigning sf_round: " << sf_round << ", sfn: " << outcome.sfn 
  //   << ", slot.idx: " << slot.idx << std::endl;
  for (uint32_t i = 0; i < nof_workers; i ++) {
    worker_locks[i].lock();
    bool busy = true;
    busy = workers[i].get()->busy;
    if (!busy) {
      found_worker = true;
      /* Copy the rx_buffer_ to the worker's rx_buffer. This won't be 
      interfering with other threads? */
      workers[i].get()->CopySlotandBuffer(sf_round, slot, outcome, rx_buffer_);
      /* Update the worker's state */
      workers[i].get()->SyncState(&task_scheduler_state);
      /* Set the worker's sem to let the task run */
      sem_post(&workers[i].get()->smph_has_job);
    }
    worker_locks[i].unlock();
    if (found_worker) {
      break;
    }
  }
  
  if (!found_worker) {
    ERROR("No available worker, if this constantly happens not in the intial"
          "stage (SIBs, RACH, DCI decoders initialization), please consider "
          "increasing the number of workers.");
    return SRSRAN_ERROR;
  } else {
    return SRSRAN_SUCCESS;
  }

}

}