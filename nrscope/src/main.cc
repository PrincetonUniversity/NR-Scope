#include <iostream>
#include <string>
#include <unistd.h>

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/load_config.h"

#include "srsran/common/band_helper.h"
#include "srsran/phy/common/phy_common_nr.h"

int main(int argc, char** argv){

  // Initialise logging infrastructure
  srslog::init();

  std::string file_name = "config.yaml";

  int nof_usrp = get_nof_usrp(file_name);
  std::vector<Radio> radios(nof_usrp);

  // TODO: Add a USRP as cell searcher -- always searching for the cell 
  if(load_config(radios, file_name) == NR_FAILURE){
    std::cout << "Load config fail." << std::endl;
    return NR_FAILURE;
  }

  // All the radios have the same setting for local log or push to google
  if(radios[0].local_log){
    std::vector<std::string> log_names(nof_usrp);
    for(int i = 0; i < nof_usrp; i++){
      log_names[i] = radios[i].log_name;
    }
    NRScopeLog::init_logger(log_names);
  }

  if(radios[0].to_google){
    ToGoogle::init_to_google(radios[0].google_credential, radios[0].google_dataset_id, nof_usrp);
  }

  std::vector<std::thread> radio_threads;

  for (auto& my_radio : radios) {
    // cpu_set_t cpu_set;
    // CPU_ZERO(&cpu_set);
    // CPU_SET(0, &cpu_set);
    // CPU_SET(1, &cpu_set);
    // CPU_SET(2, &cpu_set);
    // CPU_SET(3, &cpu_set);
    // CPU_SET(4, &cpu_set);
    // CPU_SET(5, &cpu_set);
    // CPU_SET(6, &cpu_set);
    // CPU_SET(7, &cpu_set);
    // std::thread radio_thread = ;      
    radio_threads.emplace_back(std::thread{&Radio::RadioThread, &my_radio});
    // assert(pthread_setaffinity_np(
    //   radio_threads[radio_threads.size()-1].native_handle(), 
    //   sizeof(cpu_set_t), &cpu_set) == 0);
  }

  for (auto& t : radio_threads) {
    if(t.joinable()){
      t.join();
    }
  } 

  return NR_SUCCESS;
}