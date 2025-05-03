#include "nrscope/hdr/nrscope_logger.h"

namespace NRScopeLog{
  std::vector<std::string> filename;
  std::vector< std::queue<LogNode> > log_queue;
  std::vector< std::queue<ScanLogNode> > scan_log_queue;
  std::thread log_thread;
  std::mutex lock;
  char buff[2048];
  bool run_log;
  bool is_cell_scan;

  void init_logger(std::vector<std::string> filename_input){
    is_cell_scan = false;
    filename.resize(filename_input.size());
    log_queue.resize(filename_input.size());
    for (uint32_t f_id = 0; f_id < filename_input.size(); f_id++){
      filename[f_id] = filename_input[f_id];
      // std::queue<LogNode> log_queue_empty;
      // log_queue.emplace_back(log_queue_empty);
      FILE* pFile = fopen(filename[f_id].c_str(), "a");
      // Transform the input_node into one log entry row.
      fprintf(pFile, "%s\n", "timestamp,system_frame_index,slot_index,rnti,"
        "rnti_type,dci_format,k,mapping,time_start,time_length,frequency_start,"
        "frequency_length,nof_dmrs_cdm_groups,beta_dmrs,nof_layers,n_scid,"
        "tb_scaling_field,modulation,mcs_index,transport_block_size,code_rate,"
        "redundancy_version,new_data_indicator,nof_re,nof_bits,mcs_table,"
        "xoverhead,harq_id,downlink_assignment_index,tpc,pucch_resource,"
        "harq_feedback,bwp,ports,carrier_index");
      fclose(pFile);
    }
    run_log = true;
    log_thread = std::thread{logger_thread};
  }

  void init_scan_logger(std::vector<std::string> filename_input) {
    is_cell_scan = true;
    filename.resize(filename_input.size());
    scan_log_queue.resize(filename_input.size());
    for (uint32_t f_id = 0; f_id < filename_input.size(); f_id++){
      filename[f_id] = filename_input[f_id];
      FILE* pFile = fopen(filename[f_id].c_str(), "a");
      fprintf(pFile, "%s\n", "GSCN,absolute_ssb_center_frequency,PCI");
      fclose(pFile);
    }
    run_log = true;
    log_thread = std::thread{logger_thread};
  }

  // Called to add node into the queue
  void push_node(LogNode input_node, int rf_index){
    lock.lock();
    log_queue[rf_index].push(input_node);
    lock.unlock();
  }

  void push_node(ScanLogNode input_node, int rf_index){
    lock.lock();
    scan_log_queue[rf_index].push(input_node);
    lock.unlock();
  }

  void write_entry(LogNode input_node, int rf_index){
    // Write the data in a CSV format.
    memset(buff, 0, sizeof(buff));

    int first_prb = SRSRAN_MAX_PRB_NR;
    for (int i = 0; i < SRSRAN_MAX_PRB_NR && first_prb == SRSRAN_MAX_PRB_NR; i++) {
      if (input_node.grant.grant.prb_idx[i]) {
        first_prb = i;
      }
    }

    snprintf(buff, sizeof(buff), "%f,%d,%d,%d,%s,%s,%d,%s,%d,%d,%d,%d,%d,%f,%d,"
            "%d,%d,%s,%d,%d,%f,%d,%d,%d,%d,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d", 
            input_node.timestamp,
            input_node.system_frame_idx,
            input_node.slot_idx,
            input_node.grant.grant.rnti,
            srsran_rnti_type_str(input_node.grant.grant.rnti_type),
            input_node.dci_format.c_str(),
            input_node.grant.grant.k,
            sch_mapping_to_str(input_node.grant.grant.mapping),
            input_node.grant.grant.S,
            input_node.grant.grant.L,
            first_prb,
            input_node.grant.grant.nof_prb,
            input_node.grant.grant.nof_dmrs_cdm_groups_without_data,
            input_node.grant.grant.beta_dmrs,
            input_node.grant.grant.nof_layers,
            input_node.grant.grant.n_scid,
            input_node.grant.grant.tb_scaling_field,
            srsran_mod_string(input_node.grant.grant.tb[0].mod),
            input_node.grant.grant.tb[0].mcs,
            input_node.grant.grant.tb[0].tbs,
            input_node.grant.grant.tb[0].R,
            input_node.grant.grant.tb[0].rv,
            input_node.grant.grant.tb[0].ndi,
            input_node.grant.grant.tb[0].nof_re,
            input_node.grant.grant.tb[0].nof_bits,
            srsran_mcs_table_to_str(input_node.grant.sch_cfg.mcs_table),
            sch_xoverhead_to_str(input_node.grant.sch_cfg.xoverhead),
            input_node.dci_format == 
              "1_1" ? input_node.dl_dci.pid : input_node.ul_dci.pid,
            input_node.dci_format == 
              "1_1" ? input_node.dl_dci.dai : input_node.ul_dci.dai1,
            input_node.dci_format == 
              "1_1" ? input_node.dl_dci.tpc : input_node.ul_dci.tpc,
            input_node.dci_format == 
              "1_1" ? input_node.dl_dci.pucch_resource : 0,
            input_node.dci_format == 
              "1_1" ? input_node.dl_dci.harq_feedback : 0,
            input_node.bwp_id,
            input_node.dci_format == 
              "1_1" ? input_node.dl_dci.ports : input_node.ul_dci.ports,
            input_node.dci_format == 
              "1_1" ? input_node.dl_dci.cc_id : input_node.ul_dci.cc_id
    );
    FILE* pFile = fopen(filename[rf_index].c_str(), "a");
    
    // Transform the input_node into one log entry row.
    fprintf(pFile, "%s\n", buff);
    fclose(pFile);
  }

  void write_entry(ScanLogNode input_node, int rf_index){
    // Write the data in a CSV format.
    memset(buff, 0, sizeof(buff));

    snprintf(buff, sizeof(buff), "%d,%f,%d", 
            input_node.gscn,
            input_node.freq,
            input_node.pci
    );
    FILE* pFile = fopen(filename[rf_index].c_str(), "a");
    
    // Transform the input_node into one log entry row.
    fprintf(pFile, "%s\n", buff);
    fclose(pFile);
  }

  void logger_thread(){
    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = my_sig_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);

    while(run_log){
      // printf("Queue length: %ld\n", log_queue.size());
      for (int rf_index = 0; rf_index < (int)filename.size(); rf_index++){
        if (is_cell_scan) {
          if(scan_log_queue[rf_index].size() > 0){
            lock.lock();
            ScanLogNode new_node = scan_log_queue[rf_index].front();
            // Write to local disk
            write_entry(new_node, rf_index);
            printf("new found cell written to log\n");
            scan_log_queue[rf_index].pop();
            lock.unlock();
          }else{
            usleep(1000);
          }
        }
        else {
          if(log_queue[rf_index].size() > 0){
            lock.lock();
            LogNode new_node = log_queue[rf_index].front();
            // Write to local disk
            write_entry(new_node, rf_index);
            printf("new_node_timestamp: %f\n", new_node.timestamp);
            log_queue[rf_index].pop();
            lock.unlock();
          }else{
            usleep(1000);
          }
        }
      }
    }
  }

  void exit_logger(){
    run_log = false;
  }
};