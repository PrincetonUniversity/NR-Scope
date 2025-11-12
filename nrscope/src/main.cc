#include <iostream>
#include <string>
#include <unistd.h>

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/load_config.h"

#include "srsran/common/band_helper.h"
#include "srsran/phy/common/phy_common_nr.h"
#include "srsran/srslog/srslog.h"

int main(int argc, char** argv){

  std::string file_name = "config.yaml";
  std::string log_file  = get_debug_log_name(file_name);

  // Initialise logging infrastructure
  srslog::sink* log_sink = srslog::create_file_sink(log_file);
  if (log_sink == nullptr) {
    std::cerr << "Failed to create logging sink for nrscope." << std::endl;
    return NR_FAILURE;
  }

  srslog::log_channel* log_channel = srslog::create_log_channel("nrscope_channel", *log_sink);
  if (log_channel == nullptr) {
    std::cerr << "Failed to create logging channel for nrscope." << std::endl;
    return NR_FAILURE;
  }
  srslog::set_default_sink(*log_sink);

  srslog::init();
  nrscope_logger().set_level(srslog::basic_levels::info);

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
    radio_threads.emplace_back(&Radio::RadioThread, &my_radio);
  }

  for (auto& t : radio_threads) {
    if(t.joinable()){
      t.join();
    }
  } 

  srslog::flush();
  log_sink->flush();

  return NR_SUCCESS;
}
