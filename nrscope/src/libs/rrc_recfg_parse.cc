#include "nrscope/hdr/rrc_recfg_parse.h"
#include <string>

void parse_rrc_recfg(Radio& radio, std::string file_name)
{
  YAML::Node config = YAML::LoadFile(file_name);

  if (config["pdsch_cfg"]) {
    if (config["pdsch_cfg"].IsMap()) {
      for (const auto& kv : config["pdsch_cfg"]) {
        std::string key                      = kv.first.as<std::string>();
        std::string value                    = kv.second.as<std::string>();
        radio.rrc_recfg.recfg_pdsch_cfg[key] = value;
        std::cout << "pdsch_cfg: " << key << ": " << value << std::endl;
      }
    } else {
      std::cerr << "pdsch_cfg is not a map." << std::endl;
    }
  }

  if (config["pusch_cfg"]) {
    if (config["pusch_cfg"].IsMap()) {
      for (const auto& kv : config["pusch_cfg"]) {
        std::string key                      = kv.first.as<std::string>();
        std::string value                    = kv.second.as<std::string>();
        radio.rrc_recfg.recfg_pusch_cfg[key] = value;
        std::cout << "pusch_cfg: " << key << ": " << value << std::endl;
      }
    } else {
      std::cerr << "pusch_cfg is not a map." << std::endl;
    }
  }

  if (config["dci_cfg"]) {
    if (config["dci_cfg"].IsMap()) {
      for (const auto& kv : config["dci_cfg"]) {
        std::string key                    = kv.first.as<std::string>();
        std::string value                  = kv.second.as<std::string>();
        radio.rrc_recfg.recfg_dci_cfg[key] = value;
        std::cout << "dci_cfg: " << key << ": " << value << std::endl;
      }
    } else {
      std::cerr << "dci_cfg is not a map." << std::endl;
    }
  }
}
