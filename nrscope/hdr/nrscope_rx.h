// wrap rx calls for record and replay
#pragma once

#include "nrscope_def.h"
#include "srsran/interfaces/radio_interfaces.h"
// #include "nrscope/hdr/radio_nr.h"


enum class rx_mode { NORMAL, RECORD, REPLAY };

enum class rx_result { RX_SUCCESS, RX_ERROR, RX_EOF };

bool init_record(const char* path, uint32_t buf_size_gb);
bool init_normal();
bool init_replay(const char* path);

std::shared_ptr<srsran::radio_interface_phy> nrscope_radio_init(std::shared_ptr<srsran::radio> radio, srsran::rf_args_t rf_args);
void nrscope_radio_set_rx_freq(srsran::radio_interface_phy* radio, double freq_hz);
srsran::rf_metrics_t nrscope_radio_get_metrics(srsran::radio_interface_phy* radio);

rx_result nrscope_rx(srsran::radio_interface_phy* radio,
           srsran::rf_buffer_interface& rf_buffer,
           srsran::rf_timestamp_interface& rf_timestamp);
