#include <iostream>
#include <string>
#include <unistd.h>

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/load_config.h"

#include "srsran/common/band_helper.h"
#include "srsran/phy/common/phy_common_nr.h"

/**
 * Benchmarking program.
 * Available benchmarks: 
 * SSB detection time
 */

int main(int argc, char** argv){

  // Initialise logging infrastructure
  srslog::init();

  std::string file_name = (argc > 1) ? argv[1] : "config.yaml";

  int nof_usrp = get_nof_usrp(file_name);
  if (nof_usrp != 1) {
    std::cout << "Benchmarking only supports single-radio operation." << std::endl;
    return NR_FAILURE;
  }
  std::vector<Radio> radios(nof_usrp);

  if(load_config(radios, file_name) == NR_FAILURE){
    std::cout << "Load config fail." << std::endl;
    return NR_FAILURE;
  }
  auto& radio = radios[0];
  // google push not supported
  if (radio.to_google) {
    std::cout << "Benchmarking does not support pushing to Google." << std::endl;
    return NR_FAILURE;
  }
  // Initialize local logging
  if (radio.local_log) {
    auto log_names = {radio.log_name};
    NRScopeLog::init_logger(log_names);
  }
  // run the SSB detection benchmark
  radio.BenchmarkSSBDetectionTime(100);
  return NR_SUCCESS;
}