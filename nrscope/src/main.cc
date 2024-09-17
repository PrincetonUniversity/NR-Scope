#include <iostream>
#include <string>
#include <unistd.h>
#include <fstream>
#include <nlohmann/json.hpp>

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/load_config.h"
#include "nrscope/hdr/asn_decoder.h"

#include "srsran/common/band_helper.h"
#include "srsran/phy/common/phy_common_nr.h"

using json = nlohmann::json;
// void my_handler(int s){
//   printf("Caught signal %d\n",s);
//   exit(1); 
// }

int main(int argc, char** argv){
  // srsran_debug_handle_crash(argc, argv);

  // struct sigaction sigIntHandler;

  // sigIntHandler.sa_handler = my_handler;
  // sigemptyset(&sigIntHandler.sa_mask);
  // sigIntHandler.sa_flags = 0;

  // sigaction(SIGINT, &sigIntHandler, NULL);

  /* Initialize ASN decoder */
  // init_asn_decoder("sample.sib");

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

  std::ifstream f("/home/xyc/hidden_bwp_40/NG-Scope-5G/nrscope/hidden_bwp_db/369.txt");
  json data = json::parse(f);
  if (data["downlinkBWP-ToAddModList"].is_object()) {
    printf("is object.\n");
  }
  if (data.contains("downlinkBWP-ToAddModList")) {
    printf("contains downlinkBWP-ToAddModList\n");
  }
  if (data.contains("haha")) {
    printf("contains haha\n");
  }
  std::cout << std::setw(2) << data["downlinkBWP-ToAddModList"] << std::endl;
  std::vector<std::thread> radio_threads;

  for (auto& my_radio : radios) {
    radio_threads.emplace_back(&Radio::RadioThread, &my_radio);
  }

  for (auto& t : radio_threads) {
    if(t.joinable()){
      t.join();
    }
  } 

  // terminate_asn_decoder();

  return NR_SUCCESS;
}