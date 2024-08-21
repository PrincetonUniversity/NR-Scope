#include <string.h>
#include <stdlib.h>

#include "srsran/common/band_helper.h"
#include "srsran/common/crash_handler.h"
#include "srsran/common/string_helpers.h"
#include "srsran/phy/common/phy_common_nr.h"
#include "srsue/hdr/phy/phy_nr_sa.h"
#include "test/phy/dummy_ue_stack.h"
#include "srsue/hdr/stack/ue_stack_nr.h"
#include <boost/program_options.hpp>
#include <boost/program_options/parsers.hpp>
#include "srsran/srsran.h"
#include "srsran/radio/radio.h"

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/load_config.h"
#include "nrscope/hdr/radio_nr.h"

using namespace std;

int get_nof_usrp(std::string file_name){
  YAML::Node config_yaml = YAML::LoadFile(file_name);
  if(config_yaml["nof_usrp_dev"]){
    return config_yaml["nof_usrp_dev"].as<int>();
  }else{
    return -1;
  }
}

int load_config(std::vector<Radio>& radios, std::string file_name){
  /*To get the config from file, including the number of usrp devices, 
  cell central frequency and etc.*/
  YAML::Node config_yaml = YAML::LoadFile(file_name);

  std::cout << "Reading configs." << std::endl;

  int nof_usrp = config_yaml["nof_usrp_dev"].as<int>();

  // if(config_yaml["dci_log_config"]){
  //   dcilog->nof_cell = nof_usrp;
  //   dcilog->log_ul = config_yaml["dci_log_config"]["log_ul"].as<bool>();
  //   dcilog->log_dl = config_yaml["dci_log_config"]["log_dl"].as<bool>();
  // }else{
  //   std::cout << "Please set the dci_log_config in config.yaml properly." << std::endl;
  //   return NR_FAILURE;
  // }

  for(int i = 0; i < nof_usrp; i++){
    radios[i].rf_index = i;
    std::string setting_name = "usrp_setting_"+to_string(i);
    std::cout << "USRP Device: " << i << std::endl;
    if(config_yaml[setting_name]){
      // radios[i].rf_args. = config_yaml[setting_name]["rf_freq"].as<long long>();
      // cout << "    rf_freq: " << radios[i].rf_freq << endl;

      // radios[i].N_id_2 = config_yaml[setting_name]["N_id_2"].as<int>();
      // cout << "    N_id: " << radios[i].N_id_2 << endl;
      srsran::rf_args_t rf_args = {};
      if(config_yaml[setting_name]["rf_args"]){
        std::string rf_args_config = config_yaml[setting_name]["rf_args"].as<string>();
        radios[i].rf_args.device_args = rf_args_config;
      }
      std::cout << "    rf_args: " << radios[i].rf_args.device_args << std::endl;

      if(config_yaml[setting_name]["device_name"]){
        std::string rf_args_devicename = config_yaml[setting_name]["device_name"].as<string>();
        radios[i].rf_args.device_name = rf_args_devicename;
      }
      std::cout << "    device_name: " << radios[i].rf_args.device_name << endl;

      if(config_yaml[setting_name]["log_level"]){
        std::string rf_args_loglevel = config_yaml[setting_name]["log_level"].as<string>();
        radios[i].rf_args.log_level = rf_args_loglevel;
      }

      if(config_yaml[setting_name]["srsran_srate_hz"]){
        radios[i].rf_args.srsran_srate_hz = config_yaml[setting_name]["srsran_srate_hz"].as<double>();
      }
      std::cout << "    srsran_srate_hz: " << radios[i].rf_args.srsran_srate_hz / 1e6 << " MHz" << std::endl;

      if(config_yaml[setting_name]["srate_hz"]){
        radios[i].rf_args.srate_hz = config_yaml[setting_name]["srate_hz"].as<double>();
      }
      std::cout << "    srate_hz: " << radios[i].rf_args.srate_hz / 1e6 << " MHz" << std::endl;


      if(config_yaml[setting_name]["rx_gain"]){
        radios[i].rf_args.rx_gain = config_yaml[setting_name]["rx_gain"].as<float>();
      }
      std::cout << "    rx_gain: " << radios[i].rf_args.rx_gain << std::endl;


      if(config_yaml[setting_name]["nof_carriers"]){
        radios[i].rf_args.nof_carriers = config_yaml[setting_name]["nof_carriers"].as<int>();
      }
      std::cout << "    nof_carriers: " << radios[i].rf_args.nof_carriers << std::endl;

      if(config_yaml[setting_name]["nof_antennas"]){
        radios[i].rf_args.nof_antennas = config_yaml[setting_name]["nof_antennas"].as<int>();
      }
      std::cout << "    nof_antennas: " << radios[i].rf_args.nof_antennas << std::endl;

      if(config_yaml[setting_name]["freq_offset"]){
        radios[i].rf_args.freq_offset = config_yaml[setting_name]["freq_offset"].as<float>();
      }
      std::cout << "    freq_offset: " << radios[i].rf_args.freq_offset << std::endl;

      if(config_yaml[setting_name]["scs_index"]){
        radios[i].ssb_scs = (srsran_subcarrier_spacing_t)config_yaml[setting_name]["scs_index"].as<int>();
      }
      std::cout << "    scs: " << radios[i].ssb_scs << std::endl;

      if(config_yaml[setting_name]["ssb_freq"]){
        radios[i].args_t.base_carrier.dl_center_frequency_hz = config_yaml[setting_name]["ssb_freq"].as<double>();
      }

      if(config_yaml[setting_name]["rf_log_level"]){
        radios[i].rf_args.log_level = config_yaml[setting_name]["rf_log_level"].as<string>();
      }else{
        radios[i].rf_args.log_level = "info";
      }

      if(config_yaml[setting_name]["log_name"]){
        radios[i].log_name = config_yaml[setting_name]["log_name"].as<string>();
      }

      if(config_yaml[setting_name]["google_dataset_id"]){
        radios[i].google_dataset_id = config_yaml[setting_name]["google_dataset_id"].as<string>();
      }

      if(config_yaml[setting_name]["nof_rnti_worker_groups"]){
        radios[i].nof_rnti_worker_groups = config_yaml[setting_name]["nof_rnti_worker_groups"].as<int>();
      }else{
        radios[i].nof_rnti_worker_groups = 1;
      }

      radios[i].nof_threads = radios[i].nof_rnti_worker_groups;

      if(config_yaml[setting_name]["nof_bwps"]){
        radios[i].nof_bwps = config_yaml[setting_name]["nof_bwps"].as<int>();
      }else{
        radios[i].nof_bwps = 1;
      }

      radios[i].nof_threads = radios[i].nof_threads * radios[i].nof_bwps;

      // std::cout << "    nof_thread: " << radios[i].nof_thread << std::endl;
    }else{
      std::cout << "Please set the usrp_setting_" << i << " in config.yaml properly." << std::endl;
    return NR_FAILURE;
    }
  }

  std::string setting_name = "log_config";
  if(config_yaml[setting_name]["local_log"]){
    for (int i = 0; i < nof_usrp; i++){
      radios[i].local_log = config_yaml[setting_name]["local_log"].as<bool>();
    }
  }else{
    for (int i = 0; i < nof_usrp; i++){
      radios[i].local_log = false;
    }
  }
  
  if(config_yaml[setting_name]["push_to_google"]){
    for (int i = 0; i < nof_usrp; i++){
      radios[i].to_google = config_yaml[setting_name]["push_to_google"].as<bool>();
      if(config_yaml[setting_name]["google_service_account_credential"]){
        radios[i].google_credential = config_yaml[setting_name]["google_service_account_credential"].as<string>();
      }
    }
    // if(config_yaml[setting_name]["google_project_id"]){
    //   radios[i].google_project_id = config_yaml[setting_name]["google_project_id"].as<string>();
    // }
  }

  return NR_SUCCESS;
}
