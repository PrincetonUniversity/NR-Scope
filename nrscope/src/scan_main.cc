#include <iostream>
#include <string>
#include <unistd.h>

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/load_config.h"
// #include "nrscope/hdr/asn_decoder.h"

#include "srsran/common/band_helper.h"
#include "srsran/phy/common/phy_common_nr.h"

int main(int argc, char** argv){

  // Initialise logging infrastructure
  srslog::init();

  std::string file_name = "config.yaml";

  int nof_usrp = get_nof_usrp(file_name);
  std::vector<Radio> radios(nof_usrp);

  if(load_config(radios, file_name) == NR_FAILURE){
    std::cout << "Load config fail." << std::endl;
    return NR_FAILURE;
  }

  radios[0].log_name = "scan.csv";
  radios[0].local_log = true;

  // All the radios have the same setting for local log or push to google
  if(radios[0].local_log){
    std::vector<std::string> log_names(1);
    // Scan only needs one radio for now
    for(int i = 0; i < 1; i++){
      log_names[i] = radios[i].log_name;
    }
    NRScopeLog::init_scan_logger(log_names);
  }

  std::vector<std::thread> radio_threads;
  radio_threads.emplace_back(&Radio::ScanThread, &radios[0]);

  for (auto& t : radio_threads) {
    if(t.joinable()){
      t.join();
    }
  } 

  return NR_SUCCESS;
}