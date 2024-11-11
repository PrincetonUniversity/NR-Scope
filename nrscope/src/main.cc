#include <iostream>
#include <string>
#include <unistd.h>
#include <map>

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/load_config.h"
#include "nrscope/hdr/asn_decoder.h"

#include "srsran/common/band_helper.h"
#include "srsran/phy/common/phy_common_nr.h"

#include "nrscope/xml2json/include/xml2json.hpp"

using namespace std;

int main(int argc, char** argv){
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

  // load the encrypted-RRC db
  std::ifstream f("all.json");
  std::string line;
  map<int, string> cell_db;

  while (std::getline(f, line))
  {
    json data = json::parse(line);
    int pci = 0;
    if (data.contains("body")) {
      if (data["body"].contains("pci")) {
        pci = int(data["body"]["pci"]);
      }

      // check if there is RRCReconfiguration and in which bwp1 is added
      if (data["body"].contains("message")) {
        string xml_rrc_msg = data["body"]["message"];
        if (xml_rrc_msg.find("rrcReconfiguration") != std::string::npos && 
        xml_rrc_msg.find("downlinkBWP-ToAddModList") != std::string::npos) {
          if (pci != 0) {
            // add to the db
            string json_rrcreconfig_str = xml2json(xml_rrc_msg.c_str());
            cell_db[pci] = json_rrcreconfig_str;
          }
        }
      }
    }
  }

  // All the radios have the same setting for local log or push to google
  if(radios[0].local_log){
    std::vector<std::string> log_names(nof_usrp);
    for(int i = 0; i < nof_usrp; i++){
      log_names[i] = radios[i].log_name;
      radios[i].cell_db = cell_db;
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

  // terminate_asn_decoder();

  return NR_SUCCESS;
}