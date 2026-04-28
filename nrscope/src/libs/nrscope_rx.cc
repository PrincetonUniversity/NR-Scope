// wrap rx calls for record and replay
#include "nrscope_rx.h"
#include "nrscope_print.h"

#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <memory>
#include <string>
#include <thread>
#include <time.h>


/*
  RECORD mode operation
  init: 
    allocate a large buffer (hard-coded to 30GB for now)
  operation: 
    write to buffer until full
    when buffer is full: 
      1. change mode to NORMAL
      2. spawn a background thread to write the buffer to a file


  REPLAY mode operation
  init: 
    open the file for binary reading
    read the entire file into the record buffer
  operation: 
    read from the record buffer and simulate radio reception
*/

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

static rx_mode  mode       = rx_mode::NORMAL;
static uint64_t total_bytes_fetched = 0;
static struct timespec last_rx_time  = {0, 0};
static FILE*    record_fh  = nullptr;  // used only for REPLAY

static uint64_t RECORD_BUF_CAP;
static uint8_t*  record_buf     = nullptr;
static uint64_t  record_buf_pos = 0;
static std::string record_path;

static uint64_t         last_replay_ts_full = 0;
static double           last_replay_ts_frac = 0.0;
static struct timespec  last_replay_return  = {0, 0};


// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

// Allocate the record buffer and switch to RECORD mode.
// The buffer is written to `path` by a background thread once full.
// Returns true on success.
bool init_record(const char* path, uint32_t buf_size_gb)
{
  RECORD_BUF_CAP = ((uint64_t)buf_size_gb) * 1024 * 1024 * 1024;
  record_buf = new uint8_t[RECORD_BUF_CAP];
  record_buf_pos = 0;
  record_path = path;
  mode = rx_mode::RECORD;
  printf("init_record: allocated %.0f GB buffer, will write to %s when full\n",
         RECORD_BUF_CAP / 1e9, path);
  return true;
}

bool init_normal()
{
  mode = rx_mode::NORMAL;
  return true;
}

bool init_replay(const char* path)
{
  record_fh = fopen(path, "rb");
  if (record_fh == nullptr) {
    perror("init_replay: fopen");
    return false;
  }
  mode = rx_mode::REPLAY;
  return true;
}


// ---------------------------------------------------------------------------
// Radio config wrappers
// ---------------------------------------------------------------------------
std::shared_ptr<srsran::radio_interface_phy> nrscope_radio_init(std::shared_ptr<srsran::radio> radio, srsran::rf_args_t rf_args) {
  if (mode == rx_mode::REPLAY) { // skip initialization
    // printf("nrscope_radio_init: skipping radio initialization in REPLAY mode\n");
    std::shared_ptr<srsran::radio_interface_phy> phy_radio = std::move(radio);
    return phy_radio;
  }
  srsran_assert(radio->init(rf_args, nullptr) == SRSRAN_SUCCESS, "Failed Radio initialisation");
  std::shared_ptr<srsran::radio_interface_phy> phy_radio = std::move(radio);
  // Set sampling rate
  phy_radio->set_rx_srate(rf_args.srate_hz);
  std::cout << "usrp srate_hz: " << rf_args.srate_hz << std::endl;
  // Set DL center frequency
  phy_radio->set_rx_freq(0, (double)rf_args.dl_freq);
  // Set Rx gain
  phy_radio->set_rx_gain(rf_args.rx_gain);
  return phy_radio;
}


void nrscope_radio_set_rx_freq(srsran::radio_interface_phy* radio, double freq_hz)
{
  if (mode == rx_mode::REPLAY) { // skip setting
    // printf("nrscope_radio_set_rx_freq: skipping setting rx freq in REPLAY mode\n");
    return;
  }
  radio->release_freq(0);
  radio->set_rx_freq(0, freq_hz);
}

srsran::rf_metrics_t nrscope_radio_get_metrics(srsran::radio_interface_phy* radio)
{
  if (mode == rx_mode::REPLAY) { // skip getting
    // printf("nrscope_radio_get_metrics: skipping getting metrics in REPLAY mode\n");
    srsran::rf_metrics_t dummy_metrics = {}; // empty metrics
    return dummy_metrics;
  }
  auto concrete_radio = dynamic_cast<srsran::radio*>(radio);
  srsran::rf_metrics_t metrics;
  concrete_radio->get_metrics(&metrics);
  return metrics;
}


// ---------------------------------------------------------------------------
// Core rx wrapper
// ---------------------------------------------------------------------------

// Per-call header written before each IQ block so replay can reconstruct
// buffer geometry and timestamp.
struct rx_frame_header {
  uint32_t nof_samples;
  uint64_t timestamp_full_secs;
  double   timestamp_frac_secs;
};



static void flush_record_buf(uint8_t* buf, uint64_t size, std::string path)
{
  printf("nrscope_rx: writing %.2f GB to %s\n", size / 1e9, path.c_str());
  FILE* fh = fopen(path.c_str(), "wb");
  if (!fh) { perror("flush_record_buf: fopen"); delete[] buf; return; }
  fwrite(buf, 1, size, fh);
  fclose(fh);
  delete[] buf;
  printf("nrscope_rx: DONE writing %.2f GB to %s\n", size / 1e9, path.c_str());
}

rx_result nrscope_rx(srsran::radio_interface_phy* radio,
                     srsran::rf_buffer_interface& rf_buffer,
                     srsran::rf_timestamp_interface& rf_timestamp)
{

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  uint64_t call_bytes = (uint64_t)rf_buffer.get_nof_samples() * sizeof(cf_t);
  total_bytes_fetched += call_bytes;
  double elapsed_s = (now.tv_sec - last_rx_time.tv_sec) + (now.tv_nsec - last_rx_time.tv_nsec) * 1e-9;
  double rate_mbs  = (elapsed_s > 0 && last_rx_time.tv_sec != 0) ? (call_bytes / 1e6) / elapsed_s : 0.0;
  last_rx_time = now;
  NRSCOPE_PRINT("nrscope_rx: fetching %u samples | total: %.2f MB | rate: %.2f MB/s\n",
         rf_buffer.get_nof_samples(), total_bytes_fetched / 1e6, rate_mbs);
  // Note: timestamp will be important for future applications -- double check output
  if (mode == rx_mode::NORMAL || mode == rx_mode::RECORD) {
    if (!radio->rx_now(rf_buffer, rf_timestamp)) {
      return rx_result::RX_ERROR;
    }
  }

  switch (mode) {
    case rx_mode::NORMAL:
      break; // nothing to do
    case rx_mode::RECORD: {
      rx_frame_header hdr;
      hdr.nof_samples         = rf_buffer.get_nof_samples();
      hdr.timestamp_full_secs = rf_timestamp.get(0).full_secs;
      hdr.timestamp_frac_secs = rf_timestamp.get(0).frac_secs;

      uint64_t frame_size = sizeof(hdr) + hdr.nof_samples * sizeof(cf_t);
      if (record_buf_pos + frame_size > RECORD_BUF_CAP) {
        NRSCOPE_PRINT("nrscope_rx: record buffer full (%.2f GB), stopping recording and flushing buffer\n",
               record_buf_pos / 1e9);
        mode = rx_mode::NORMAL;
        std::thread(flush_record_buf, record_buf, record_buf_pos, record_path).detach();
        record_buf = nullptr;
        record_buf_pos = 0;
      } else {
        memcpy(record_buf + record_buf_pos, &hdr, sizeof(hdr));
        record_buf_pos += sizeof(hdr);
        memcpy(record_buf + record_buf_pos, rf_buffer.get(0), hdr.nof_samples * sizeof(cf_t));
        record_buf_pos += hdr.nof_samples * sizeof(cf_t);
      }
      break;
    }
    case rx_mode::REPLAY: {
      rx_frame_header hdr;
      if (fread(&hdr, sizeof(hdr), 1, record_fh) != 1) {
        return rx_result::RX_EOF;
      }
      if (hdr.nof_samples != rf_buffer.get_nof_samples()) {
        return rx_result::RX_ERROR;
      }
      if (fread(rf_buffer.get(0), sizeof(cf_t), hdr.nof_samples, record_fh) != hdr.nof_samples) {
        return rx_result::RX_ERROR;
      }
      rf_timestamp.get_ptr(0)->full_secs = (time_t)hdr.timestamp_full_secs;
      rf_timestamp.get_ptr(0)->frac_secs = hdr.timestamp_frac_secs;

      // Pace replay to match recorded timing
      if (last_replay_return.tv_sec != 0) {
        double recorded_delta = (double)(hdr.timestamp_full_secs - last_replay_ts_full)
                                + (hdr.timestamp_frac_secs - last_replay_ts_frac);
        struct timespec spin_now;
        double system_elapsed;
        do {
          clock_gettime(CLOCK_MONOTONIC, &spin_now);
          system_elapsed = (spin_now.tv_sec  - last_replay_return.tv_sec)
                         + (spin_now.tv_nsec - last_replay_return.tv_nsec) * 1e-9;
        } while (system_elapsed < recorded_delta);
      }
      last_replay_ts_full  = hdr.timestamp_full_secs;
      last_replay_ts_frac  = hdr.timestamp_frac_secs;
      clock_gettime(CLOCK_MONOTONIC, &last_replay_return);

      break;
    }
  }
  return rx_result::RX_SUCCESS;
}
