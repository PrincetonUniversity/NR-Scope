#include <stdlib.h>
#include <string.h>

#include "srsran/common/band_helper.h"
#include "srsran/common/crash_handler.h"
#include "srsran/common/string_helpers.h"
#include "srsran/phy/common/phy_common_nr.h"
#include "srsran/radio/radio.h"
#include "srsran/srsran.h"
#include "srsue/hdr/phy/phy_nr_sa.h"
#include "srsue/hdr/stack/ue_stack_nr.h"
#include "test/phy/dummy_ue_stack.h"
#include <boost/program_options.hpp>
#include <boost/program_options/parsers.hpp>

#include "nrscope/hdr/load_config.h"
#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/radio_nr.h"
#include "nrscope/hdr/rrc_recfg_parse.h"

#include "srsran/phy/ue/srsgui_plot.h"

using namespace std;

int get_nof_usrp(std::string file_name)
{
  YAML::Node config_yaml = YAML::LoadFile(file_name);
  if (config_yaml["nof_usrp_dev"]) {
    return config_yaml["nof_usrp_dev"].as<int>();
  } else {
    return -1;
  }
}

int load_config(std::vector<Radio>& radios, std::string file_name)
{
  /*To get the config from file, including the number of usrp devices,
  cell central frequency and etc.*/
  YAML::Node config_yaml = YAML::LoadFile(file_name);

  std::cout << "Reading configs." << std::endl;

  int nof_usrp = config_yaml["nof_usrp_dev"].as<int>();

  for (int i = 0; i < nof_usrp; i++) {
    radios[i].rf_index       = i;
    std::string setting_name = "usrp_setting_" + to_string(i);
    std::cout << "USRP Device: " << i << std::endl;
    if (config_yaml[setting_name]) {
      srsran::rf_args_t rf_args = {};
      if (config_yaml[setting_name]["rf_args"]) {
        std::string rf_args_config    = config_yaml[setting_name]["rf_args"].as<string>();
        radios[i].rf_args.device_args = rf_args_config;
      }
      std::cout << "    rf_args: " << radios[i].rf_args.device_args << std::endl;

      if (config_yaml[setting_name]["device_name"]) {
        std::string rf_args_devicename = config_yaml[setting_name]["device_name"].as<string>();
        radios[i].rf_args.device_name  = rf_args_devicename;
      }
      std::cout << "    device_name: " << radios[i].rf_args.device_name << endl;

      if (config_yaml[setting_name]["log_level"]) {
        std::string rf_args_loglevel = config_yaml[setting_name]["log_level"].as<string>();
        radios[i].rf_args.log_level  = rf_args_loglevel;
      }

      if (config_yaml[setting_name]["srsran_srate_hz"]) {
        radios[i].rf_args.srsran_srate_hz = config_yaml[setting_name]["srsran_srate_hz"].as<double>();
      }
      std::cout << "    srsran_srate_hz: " << radios[i].rf_args.srsran_srate_hz / 1e6 << " MHz" << std::endl;

      if (config_yaml[setting_name]["srate_hz"]) {
        radios[i].rf_args.srate_hz = config_yaml[setting_name]["srate_hz"].as<double>();
      }
      std::cout << "    srate_hz: " << radios[i].rf_args.srate_hz / 1e6 << " MHz" << std::endl;

      if (config_yaml[setting_name]["rx_gain"]) {
        radios[i].rf_args.rx_gain = config_yaml[setting_name]["rx_gain"].as<float>();
      }
      std::cout << "    rx_gain: " << radios[i].rf_args.rx_gain << std::endl;

      if (config_yaml[setting_name]["min_rx_gain"]) {
        radios[i].min_rx_gain = config_yaml[setting_name]["min_rx_gain"].as<float>();
      }
      std::cout << "    AGC min_rx_gain: " << radios[i].min_rx_gain << std::endl;

      if (config_yaml[setting_name]["band_list"]) {
        radios[i].band_list = config_yaml[setting_name]["band_list"].as<std::vector<uint16_t> >();
        std::cout << "    band_list: ";
        for (const auto& b : radios[i].band_list)
          std::cout << b << " ";
        std::cout << std::endl;
      }

      if (config_yaml[setting_name]["pci"]) {
        radios[i].pci = config_yaml[setting_name]["pci"].as<uint16_t>();
        if (radios[i].pci > 1007) {
          ERROR("PCI ranges from 0 to 1007, are you doing it right?");
        }
        std::cout << "    pci: " << radios[i].pci << std::endl;
      } else {
        radios[i].pci = 9999; // PCI ranges from 0 to 1007, thus 9999 is invalid
        // std::cout << "    pci: " << radios[i].pci << std::endl;
      }

      if (config_yaml[setting_name]["max_rx_gain"]) {
        radios[i].max_rx_gain = config_yaml[setting_name]["max_rx_gain"].as<float>();
      }
      std::cout << "    AGC max_rx_gain: " << radios[i].max_rx_gain << std::endl;

      if (config_yaml[setting_name]["nof_carriers"]) {
        radios[i].rf_args.nof_carriers = config_yaml[setting_name]["nof_carriers"].as<int>();
      }
      std::cout << "    nof_carriers: " << radios[i].rf_args.nof_carriers << std::endl;

      if (config_yaml[setting_name]["nof_antennas"]) {
        radios[i].rf_args.nof_antennas = config_yaml[setting_name]["nof_antennas"].as<int>();
      }
      std::cout << "    nof_antennas: " << radios[i].rf_args.nof_antennas << std::endl;

      if (config_yaml[setting_name]["freq_offset"]) {
        radios[i].rf_args.freq_offset = config_yaml[setting_name]["freq_offset"].as<float>();
      }
      std::cout << "    freq_offset: " << radios[i].rf_args.freq_offset << std::endl;

      if (config_yaml[setting_name]["scs_index"]) {
        radios[i].ssb_scs = (srsran_subcarrier_spacing_t)config_yaml[setting_name]["scs_index"].as<int>();
      }
      std::cout << "    scs: " << radios[i].ssb_scs << std::endl;

      if (config_yaml[setting_name]["ssb_freq"]) {
        radios[i].args_t.base_carrier.dl_center_frequency_hz = config_yaml[setting_name]["ssb_freq"].as<double>();
      }

      if (config_yaml[setting_name]["rf_log_level"]) {
        radios[i].rf_args.log_level = config_yaml[setting_name]["rf_log_level"].as<string>();
      } else {
        radios[i].rf_args.log_level = "info";
      }

      if (config_yaml[setting_name]["log_name"]) {
        radios[i].log_name = config_yaml[setting_name]["log_name"].as<string>();
      }

      if (config_yaml[setting_name]["google_dataset_id"]) {
        radios[i].google_dataset_id = config_yaml[setting_name]["google_dataset_id"].as<string>();
      }

      if (config_yaml[setting_name]["nof_rnti_worker_groups"]) {
        radios[i].nof_rnti_worker_groups = config_yaml[setting_name]["nof_rnti_worker_groups"].as<int>();
      } else {
        radios[i].nof_rnti_worker_groups = 1;
      }

      radios[i].nof_threads = radios[i].nof_rnti_worker_groups;

      if (config_yaml[setting_name]["nof_bwps"]) {
        radios[i].nof_bwps = config_yaml[setting_name]["nof_bwps"].as<int>();
      } else {
        radios[i].nof_bwps = 1;
      }

      if (config_yaml[setting_name]["cpu_affinity"]) {
        radios[i].cpu_affinity = config_yaml[setting_name]["cpu_affinity"].as<bool>();
      } else {
        radios[i].cpu_affinity = false;
      }

      if (config_yaml[setting_name]["nof_workers"]) {
        radios[i].nof_workers = config_yaml[setting_name]["nof_workers"].as<int>();
        if (radios[i].nof_workers > 128) {
          ERROR("Worker number shouldn't be > 128");
          return SRSRAN_ERROR;
        }
      } else {
        radios[i].nof_workers = 1;
      }

      if (config_yaml[setting_name]["rrc_recfg_config"]) {
        parse_rrc_recfg(radios[i], config_yaml[setting_name]["rrc_recfg_config"].as<string>());
      }

      radios[i].nof_threads = radios[i].nof_threads * radios[i].nof_bwps;

      // std::cout << "    nof_thread: " << radios[i].nof_thread << std::endl;
    } else {
      std::cout << "Please set the usrp_setting_" << i << " in config.yaml properly." << std::endl;
      return NR_FAILURE;
    }
  }

  /* Check if the config viable */
  const auto   nof_cores      = std::thread::hardware_concurrency();
  unsigned int required_cores = 0;
  for (int i = 0; i < nof_usrp; i++) {
    if (radios[i].cpu_affinity) {
      /* One for SIB thread, one RACH thread,
       and nof_bwp * nof_rnti_group for DCI decoding*/
      required_cores += radios[i].nof_workers * (3 + radios[i].nof_bwps * radios[i].nof_rnti_worker_groups);
    }
  }
  if (required_cores > nof_cores) {
    ERROR("CPU affinity set, usrp_i's core requirement is: "
          "nof_workers * (3 + nof_bwps * nof_rnti_worker_groups)"
          ", please make sure the total required cores %d smaller than your total"
          "number of cores: %d.",
          required_cores,
          nof_cores);
    return NR_FAILURE;
  }

  std::string setting_name = "log_config";
  if (config_yaml[setting_name]["local_log"]) {
    for (int i = 0; i < nof_usrp; i++) {
      radios[i].local_log = config_yaml[setting_name]["local_log"].as<bool>();
    }

    if (config_yaml[setting_name]["enable_gui"]) {
      if (config_yaml[setting_name]["enable_gui"].as<bool>()) {
        init_plots();
      }
    }
  } else {
    for (int i = 0; i < nof_usrp; i++) {
      radios[i].local_log = false;
    }
  }

  if (config_yaml[setting_name]["push_to_google"]) {
    for (int i = 0; i < nof_usrp; i++) {
      radios[i].to_google = config_yaml[setting_name]["push_to_google"].as<bool>();
      if (config_yaml[setting_name]["google_service_account_credential"]) {
        radios[i].google_credential = config_yaml[setting_name]["google_service_account_credential"].as<string>();
      }
    }
    // if(config_yaml[setting_name]["google_project_id"]){
    //   radios[i].google_project_id =
    //      config_yaml[setting_name]["google_project_id"].as<string>();
    // }
  }

  return NR_SUCCESS;
}
