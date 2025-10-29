#include "nrscope/hdr/nrscope_worker.h"
#include <semaphore>
#include <chrono>

namespace NRScopeTask{

std::vector<SlotResult> global_slot_results;
std::mutex queue_lock;
std::mutex task_scheduler_lock;
std::mutex slot_data_lock;
std::mutex worker_locks[128];

sem_t smph_data;   // counts ready slots
sem_t smph_idle;   // counts idle workers

NRScopeWorker::NRScopeWorker() : 
  rf_buffer_t(1),
  rach_decoder(),
  sibs_decoder() 
{
  worker_state.sib1_inited = false;
  worker_state.rach_inited = false;
  worker_state.dci_inited = false;

  worker_state.sib1_found = false;
  worker_state.rach_found = false;

  initializing = false;

  worker_state.nof_known_rntis = 0;
  worker_state.known_rntis.resize(worker_state.nof_known_rntis);
  sem_init(&smph_has_job, 0, 0); 
  busy = false;
}

NRScopeWorker::~NRScopeWorker(){ }

int NRScopeWorker::InitWorker(WorkState task_scheduler_state, int worker_id_){
  worker_id = worker_id_;
  /* Copy initial values */
  worker_state.nof_threads = task_scheduler_state.nof_threads;
  worker_state.nof_rnti_worker_groups = 
    task_scheduler_state.nof_rnti_worker_groups;
  worker_state.nof_bwps = task_scheduler_state.nof_bwps;
  worker_state.args_t = task_scheduler_state.args_t;
  worker_state.slot_sz = task_scheduler_state.slot_sz;
  worker_state.cpu_affinity = task_scheduler_state.cpu_affinity;
  /* Size of one subframe */
  rx_buffer = srsran_vec_cf_malloc(SRSRAN_NOF_SLOTS_PER_SF_NR(
    worker_state.args_t.ssb_scs) * worker_state.slot_sz);

  /* An wrapper for the rx_buffer */
  rf_buffer_t = srsran::rf_buffer_t(rx_buffer, 
    SRSRAN_NOF_SLOTS_PER_SF_NR(worker_state.args_t.ssb_scs) * 
    worker_state.slot_sz);
  /* Start the worker thread */
  // std::cout << "Starting the worker..." << std::endl; 
  StartWorker();
  return SRSRAN_SUCCESS;
}

void NRScopeWorker::StartWorker(){
  // std::cout << "Creating the thread. " << std::endl;
  if (worker_state.cpu_affinity) {
    cpu_set_t cpu_set_worker;
    CPU_ZERO(&cpu_set_worker);
    CPU_SET(worker_id * (3 + worker_state.nof_threads), &cpu_set_worker);
    worker_thread = std::thread{&NRScopeWorker::Run, this};      
    assert(pthread_setaffinity_np(worker_thread.native_handle(), 
      sizeof(cpu_set_t), &cpu_set_worker) == 0);
  } else {
    worker_thread = std::thread{&NRScopeWorker::Run, this};
  }
    
  worker_thread.detach();
}

void NRScopeWorker::CopySlotandBuffer(uint64_t sf_round_,
                                      srsran_slot_cfg_t slot_, 
                                      srsran_ue_sync_nr_outcome_t outcome_,
                                      cf_t* rx_buffer_) {
  sf_round = sf_round_;
  slot = slot_;
  outcome = outcome_;
  srsran_vec_cf_copy(rx_buffer, rx_buffer_, worker_state.slot_sz);
}

int NRScopeWorker::InitSIBDecoder(){
  /* Will always be called before any tasks */
  initializing = true;
  if(sibs_decoder.SIBDecoderandReceptionInit(&worker_state, 
     rf_buffer_t.to_cf_t()) < SRSASN_SUCCESS){
    return SRSRAN_ERROR;
  }
  std::cout << "SIBs decoder initialized..." << std::endl;
  initializing = false;
  return SRSRAN_SUCCESS;
}

int NRScopeWorker::InitRACHDecoder() {
  // std::thread rach_init_thread {&RachDecoder::rach_decoder_init, 
  //  &rach_decoder, task_scheduler_nrscope.sib1, args_t.base_carrier};
  initializing = true;
  rach_decoder.RACHDecoderInit(worker_state);
  if(rach_decoder.RACHReceptionInit(&worker_state, rf_buffer_t.to_cf_t()) < 
      SRSASN_SUCCESS){
    ERROR("RACHDecoder Init Error");
    return SRSRAN_ERROR;
  }
  std::cout << "RACH decoder initialized.." << std::endl;
  initializing = false;
  return SRSRAN_SUCCESS;
}

int NRScopeWorker::InitDCIDecoders() {
  initializing = true;
  sharded_results.resize(worker_state.nof_threads);
  nof_sharded_rntis.resize(worker_state.nof_threads);
  sharded_rntis.resize(worker_state.nof_threads);
  results.resize(worker_state.nof_bwps);
  for(uint32_t i = 0; i < worker_state.nof_rnti_worker_groups; i++){
    // for each rnti worker group, for each bwp, spawn a decoder
    for(uint8_t j = 0; j < worker_state.nof_bwps; j++){
      DCIDecoder *decoder = new DCIDecoder(100);
      if(decoder->DCIDecoderandReceptionInit(&worker_state, j, 
          rf_buffer_t.to_cf_t()) < SRSASN_SUCCESS){
        ERROR("DCIDecoder Init Error");
        return SRSRAN_ERROR;
      }
      decoder->dci_decoder_id = i * worker_state.nof_bwps + j;
      decoder->rnti_worker_group_id = i;
      dci_decoders.push_back(std::unique_ptr<DCIDecoder> (decoder));
    }
  }
  initializing = false;
  return SRSRAN_SUCCESS;
}

int NRScopeWorker::SyncState(WorkState* task_scheduler_state) {
  worker_state.args_t = task_scheduler_state->args_t;
  worker_state.cell = task_scheduler_state->cell;
  worker_state.cs_ret = task_scheduler_state->cs_ret;
  memcpy(&worker_state.srsran_searcher_cfg_t, 
    &task_scheduler_state->srsran_searcher_cfg_t, 
    sizeof(srsue::nr::cell_search::cfg_t));
  worker_state.coreset0_args_t = task_scheduler_state->coreset0_args_t;
  memcpy(&worker_state.coreset0_t, &task_scheduler_state->coreset0_t, 
    sizeof(srsran_coreset_t));

  worker_state.arg_scs = task_scheduler_state->arg_scs;

  worker_state.sib1 = task_scheduler_state->sib1;
  worker_state.sibs.resize(task_scheduler_state->sibs.size());
  for(long unsigned int i = 0; i < task_scheduler_state->sibs.size(); i++) {
    worker_state.sibs[i] = task_scheduler_state->sibs[i];
  }
  worker_state.found_sib.resize(task_scheduler_state->found_sib.size());
  for(long unsigned int i = 0; i < task_scheduler_state->found_sib.size(); i++){
    worker_state.found_sib[i] = task_scheduler_state->found_sib[i];
  }
  worker_state.sibs_to_be_found.resize(
    task_scheduler_state->sibs_to_be_found.size());
  for(long unsigned int i = 0; i < 
      task_scheduler_state->sibs_to_be_found.size(); i++) {
    worker_state.sibs_to_be_found[i] = 
      task_scheduler_state->sibs_to_be_found[i];
  }

  worker_state.rrc_setup = task_scheduler_state->rrc_setup;
  worker_state.master_cell_group = task_scheduler_state->master_cell_group;
  worker_state.rrc_recfg = task_scheduler_state->rrc_recfg;

  /* SIB 1 decoded, we can start the RACH thread */
  worker_state.sib1_found = task_scheduler_state->sib1_found;
  worker_state.rach_found = task_scheduler_state->rach_found;

  worker_state.all_sibs_found = task_scheduler_state->all_sibs_found;

  worker_state.nof_known_rntis = task_scheduler_state->nof_known_rntis;
  worker_state.known_rntis.resize(worker_state.nof_known_rntis);
  for (long unsigned int i = 0; i < worker_state.nof_known_rntis; i ++) {
    worker_state.known_rntis[i] = task_scheduler_state->known_rntis[i];
  }

  return SRSRAN_SUCCESS;
}

int NRScopeWorker::MergeResults(){
  for (uint8_t b = 0; b < worker_state.nof_bwps; b++) {
    DCIFeedback new_result;
    results[b] = new_result;
    results[b].dl_grants.resize(worker_state.nof_known_rntis);
    results[b].ul_grants.resize(worker_state.nof_known_rntis);
    results[b].spare_dl_prbs.resize(worker_state.nof_known_rntis);
    results[b].spare_dl_tbs.resize(worker_state.nof_known_rntis);
    results[b].spare_dl_bits.resize(worker_state.nof_known_rntis);
    results[b].spare_ul_prbs.resize(worker_state.nof_known_rntis);
    results[b].spare_ul_tbs.resize(worker_state.nof_known_rntis);
    results[b].spare_ul_bits.resize(worker_state.nof_known_rntis);
    results[b].dl_dcis.resize(worker_state.nof_known_rntis);
    results[b].ul_dcis.resize(worker_state.nof_known_rntis);

    uint32_t rnti_s = 0;
    uint32_t rnti_e = 0;
    for(uint32_t i = 0; i < worker_state.nof_rnti_worker_groups; i++){
      if(rnti_s >= worker_state.nof_known_rntis){
        continue;
      }
      uint32_t n_rntis = nof_sharded_rntis[i * worker_state.nof_bwps];
      rnti_e = rnti_s + n_rntis;
      if(rnti_e > worker_state.nof_known_rntis){
        rnti_e = worker_state.nof_known_rntis;
      }

      uint32_t thread_id = i * worker_state.nof_bwps + b;
      results[b].nof_dl_used_prbs += 
        sharded_results[thread_id].nof_dl_used_prbs;
      results[b].nof_ul_used_prbs += 
        sharded_results[thread_id].nof_ul_used_prbs;

      for(uint32_t k = 0; k < n_rntis; k++) {
        results[b].dl_dcis[k+rnti_s] = sharded_results[thread_id].dl_dcis[k];
        results[b].ul_dcis[k+rnti_s] = sharded_results[thread_id].ul_dcis[k];
        results[b].dl_grants[k+rnti_s] = 
          sharded_results[thread_id].dl_grants[k];
        results[b].ul_grants[k+rnti_s] = 
          sharded_results[thread_id].ul_grants[k];
      }
      rnti_s = rnti_e;
    }

    /* TO-DISCUSS: to obtain even more precise result, 
      here maybe we should total user payload prb in that bwp - used prb */
    results[b].nof_dl_spare_prbs = 
      worker_state.args_t.base_carrier.nof_prb * 
      (14 - 2) - results[b].nof_dl_used_prbs;
    for(uint32_t idx = 0; idx < worker_state.nof_known_rntis; idx ++){
      results[b].spare_dl_prbs[idx] = results[b].nof_dl_spare_prbs / 
        worker_state.nof_known_rntis;
      if(abs(results[b].spare_dl_prbs[idx]) > 
          worker_state.args_t.base_carrier.nof_prb * (14 - 2)){
        results[b].spare_dl_prbs[idx] = 0;
      }
      results[b].spare_dl_tbs[idx] = 
        (int) ((float)results[b].spare_dl_prbs[idx] * dl_prb_rate[idx]);
      results[b].spare_dl_bits[idx] = 
        (int) ((float)results[b].spare_dl_prbs[idx] * dl_prb_bits_rate[idx]);
    }

    results[b].nof_ul_spare_prbs = 
      worker_state.args_t.base_carrier.nof_prb * (14 - 2) - 
      results[b].nof_ul_used_prbs;
    for(uint32_t idx = 0; idx < worker_state.nof_known_rntis; idx ++){
      results[b].spare_ul_prbs[idx] = results[b].nof_ul_spare_prbs / 
        worker_state.nof_known_rntis;
      if(abs(results[b].spare_ul_prbs[idx]) > 
          worker_state.args_t.base_carrier.nof_prb * (14 - 2)){
        results[b].spare_ul_prbs[idx] = 0;
      }
      results[b].spare_ul_tbs[idx] = 
        (int) ((float)results[b].spare_ul_prbs[idx] * ul_prb_rate[idx]);
      results[b].spare_ul_bits[idx] = 
        (int) ((float)results[b].spare_ul_prbs[idx] * ul_prb_bits_rate[idx]);
    }
  }

  return SRSRAN_SUCCESS;
}

void NRScopeWorker::Run() {
  while (true) {
    /* When there is a job, the semaphore is set and buffer is copied */
    sem_wait(&smph_has_job);
    // worker_locks[worker_id].lock();
    // busy = true;
    busy.store(true, std::memory_order_release);
    // worker_locks[worker_id].unlock();
    struct timeval t0, t1;

    nrscope_logger().info("Processing sf_round: %llu, sfn: %u, slot.idx: %d",
                          static_cast<unsigned long long>(sf_round),
                          outcome.sfn,
                          slot.idx);

    SlotResult slot_result = {};
    /* Set the all the results to be false, will be set inside the decoder
    threads */
    slot_result.sib_result = false;
    slot_result.rach_result = false;
    slot_result.dci_result = false;
    slot_result.slot = slot;
    slot_result.outcome = outcome;
    slot_result.sf_round = sf_round;

    /* Put the initialization delay into the worker's thread */
    if (!worker_state.sib1_inited) {
      InitSIBDecoder();
      worker_state.sib1_inited = true;
    }

    if (!worker_state.rach_inited && worker_state.sib1_found) {
      InitRACHDecoder();
      worker_state.rach_inited = true;
    }

    if (!worker_state.dci_inited && worker_state.rach_found) {
      InitDCIDecoders();
      worker_state.dci_inited = true;
    }

    std::thread sibs_thread;
    /* If sib1 is not found, we run the sibs_thread; if it's found, we skip. */
    if (worker_state.sib1_inited and !worker_state.sib1_found) {
      if (worker_state.cpu_affinity){
        cpu_set_t cpu_set_sib;
        CPU_ZERO(&cpu_set_sib);
        CPU_SET(worker_id * (3+worker_state.nof_threads) + 1, &cpu_set_sib);
        sibs_thread = std::thread {&SIBsDecoder::DecodeandParseSIB1fromSlot, 
          &sibs_decoder, &slot, &worker_state, &slot_result};
        assert(pthread_setaffinity_np(sibs_thread.native_handle(), 
          sizeof(cpu_set_t), &cpu_set_sib) == 0);
      } else {
        sibs_thread = std::thread {&SIBsDecoder::DecodeandParseSIB1fromSlot, 
          &sibs_decoder, &slot, &worker_state, &slot_result};
      }
    }    

    std::thread rach_thread;
    if (worker_state.rach_inited) {
      if (worker_state.cpu_affinity) {
        cpu_set_t cpu_set_rach;
        CPU_ZERO(&cpu_set_rach);
        CPU_SET(worker_id * (3+worker_state.nof_threads) + 2, &cpu_set_rach);
        rach_thread = std::thread {&RachDecoder::DecodeandParseMS4fromSlot,
          &rach_decoder, &slot, &worker_state, &slot_result};
        assert(pthread_setaffinity_np(rach_thread.native_handle(), 
          sizeof(cpu_set_t), &cpu_set_rach) == 0);
      } else {
        rach_thread = std::thread {&RachDecoder::DecodeandParseMS4fromSlot,
          &rach_decoder, &slot, &worker_state, &slot_result};
      }
    }

    std::vector <std::thread> dci_threads;
    if(worker_state.dci_inited){
      slot_result.dci_result = true;

      dl_prb_rate.resize(worker_state.nof_known_rntis);
      ul_prb_rate.resize(worker_state.nof_known_rntis);
      dl_prb_bits_rate.resize(worker_state.nof_known_rntis);
      ul_prb_bits_rate.resize(worker_state.nof_known_rntis);

      gettimeofday(&t0, NULL);
      if (worker_state.cpu_affinity) {
        for (uint32_t i = 0; i < worker_state.nof_threads; i ++) {
          cpu_set_t cpu_set_dci;
          CPU_ZERO(&cpu_set_dci);
          CPU_SET(worker_id * (3+worker_state.nof_threads) + i + 3, 
            &cpu_set_dci);
          dci_threads.emplace_back(&DCIDecoder::DecodeandParseDCIfromSlot, 
          dci_decoders[i].get(), &slot, &worker_state, 
          std::ref(sharded_results), std::ref(sharded_rntis), 
          std::ref(nof_sharded_rntis), std::ref(dl_prb_rate), 
          std::ref(dl_prb_bits_rate), std::ref(ul_prb_rate), 
          std::ref(ul_prb_bits_rate));
          assert(pthread_setaffinity_np(dci_threads[i].native_handle(), 
            sizeof(cpu_set_t), &cpu_set_dci) == 0);
        }
      } else {
        for (uint32_t i = 0; i < worker_state.nof_threads; i++){
          dci_threads.emplace_back(&DCIDecoder::DecodeandParseDCIfromSlot, 
          dci_decoders[i].get(), &slot, &worker_state, 
          std::ref(sharded_results), std::ref(sharded_rntis), 
          std::ref(nof_sharded_rntis), std::ref(dl_prb_rate), 
          std::ref(dl_prb_bits_rate), std::ref(ul_prb_rate), 
          std::ref(ul_prb_bits_rate));
        }
      }

      for (uint32_t i = 0; i < worker_state.nof_threads; i++){
        if(dci_threads[i].joinable()){
          dci_threads[i].join();
        }
      }
      gettimeofday(&t1, NULL);
    }

    if(sibs_thread.joinable()){
      sibs_thread.join();
    }

    if(rach_thread.joinable()){
      rach_thread.join();
    }

    if(worker_state.dci_inited){
      
      MergeResults();
      slot_result.dci_feedback_results = results;
    }

    // std::cout << "After processing sf_round: " << sf_round << ", sfn: " 
    //   << outcome.sfn << ", slot.idx: " << slot.idx << std::endl; 
    // std::cout << "slot_result sf_round: " << slot_result.sf_round << ", sfn: " 
    //   << slot_result.outcome.sfn << ", slot.idx: " << slot_result.slot.idx 
    //   << std::endl;

    /* Post the result into the result queue*/
    queue_lock.lock();
    global_slot_results.push_back(slot_result);
    queue_lock.unlock();
    // worker_locks[worker_id].lock();
    busy.store(false, std::memory_order_release);
    sem_post(&smph_idle);
    // worker_locks[worker_id].unlock();
  }
}
}
