#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include <liquid/liquid.h>

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/nrscope_logger.h"
#include "nrscope/hdr/nrscope_worker.h"
#include "nrscope/hdr/to_google.h"

namespace NRScopeTask {
class TaskSchedulerNRScope
{
public:
  /* Task scheduler stores all the latest global params that each worker
  should sync with it each time the task is assigned. */
  WorkState   task_scheduler_state;
  uint32_t    nof_workers;
  std::thread scheduler_thread;

  std::vector<std::unique_ptr<NRScopeWorker> > workers;
  /* Slot results reorder buffer */
  std::vector<SlotResult> slot_results;
  /* The next slot result that task_scheduler expects*/
  SlotResult next_result;

  /* Slot data queue storage buffer */
  std::vector<SlotData> slot_data;
  std::atomic<uint32_t> slot_data_len;
  std::atomic<uint32_t> next_slot_idx;
  std::atomic<uint32_t> current_slot_idx;
  std::thread           task_thread;

  bool local_log;
  bool to_google;
  /* USRP's index */
  int rf_index;

  TaskSchedulerNRScope();
  ~TaskSchedulerNRScope();

  /* Start the workers and the receiving result thread, called before the
  radio reception. */
  int InitandStart(bool                 local_log_,
                   bool                 to_google_,
                   int                  rf_index_,
                   int32_t              nof_threads,
                   uint32_t             nof_rnti_worker_groups,
                   uint8_t              nof_bwps,
                   bool                 cpu_affinity,
                   cell_searcher_args_t args_t,
                   uint32_t             nof_workers_,
                   RRCRecfg             rrc_recfg_);

  int DecodeMIB(cell_searcher_args_t*          args_t_,
                srsue::nr::cell_search::ret_t* cs_ret_,
                srsue::nr::cell_search::cfg_t* srsran_searcher_cfg_t,
                float                          resample_ratio_,
                uint32_t                       raw_srate_);

  int UpdateStateandLog();

  int UpdatewithResult(SlotResult now_result);

  void UpdateNextResult();

  int ClaimIdleWorker();

  /* Assign the current slot to one worker*/
  int AssignTask(uint64_t sf_round, srsran_slot_cfg_t slot, srsran_ue_sync_nr_outcome_t outcome, cf_t* rx_buffer_);

  int StoreSlotData(uint64_t sf_round, srsran_slot_cfg_t slot, srsran_ue_sync_nr_outcome_t outcome, cf_t* rx_buffer_);

  /* resampler tools */
  float                resample_ratio;
  msresamp_crcf        resampler;
  float                resampler_delay;
  uint32_t             temp_x_sz;
  uint32_t             temp_y_sz;
  std::complex<float>* temp_x;
  std::complex<float>* temp_y;
  uint32_t             pre_resampling_slot_sz;

private:
  void Run();
  void TasksDispatch();
};

} // namespace NRScopeTask

#endif