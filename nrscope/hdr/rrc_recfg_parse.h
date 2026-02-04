#ifndef RRC_RECFG_PARSE_H
#define RRC_RECFG_PARSE_H

#include <iostream>
#include <yaml-cpp/yaml.h>

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/radio_nr.h"

void parse_rrc_recfg(Radio& radio, std::string file_name);

#endif