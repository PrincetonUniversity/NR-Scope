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



struct SSBDetectionResult {
  bool success; // whether the SSB was successfully detected
  double start_time; // the time when the SSB detection started
  double detection_time; // time to detect (0 if detection failed)
  std::vector<std::tuple<double, double>> pbch_corrs; // Optional PBCH correlation values for debugging
};

// summary of one result
void print_ssb_detection_result(const SSBDetectionResult& result)
{
  auto max_corr = 0.0;
  if (!result.pbch_corrs.empty()) {
    for (const auto& corr_pair : result.pbch_corrs) {
      max_corr = std::max(max_corr, std::get<1>(corr_pair));
    }
  }
  std::cout << std::fixed << std::setprecision(6)
            << "{\"start_time\": " << result.start_time
            << ", \"ssb_found\": " << (result.success ? "true" : "false")
            << ", \"detection_time\": " << result.detection_time
            << ", \"max_pbch_correlation\": " << max_corr
            << "}" << std::endl;
}

void print_ssb_detection_results(const std::vector<SSBDetectionResult>& results)
{
  // print a summary of trials
  std::cout << "==== SSB Detection Results Summary ====" << std::endl;
  for (const auto& result : results) {
    print_ssb_detection_result(result);
  }
  // print a list of (time, corr) values for all trials, one per line
  std::cout << "==== PBCH Correlation Timeseries ====" << std::endl;
  for (const auto& result : results) {
    if (!result.pbch_corrs.empty()) {
      std::cout << "{\"trial\": " << &result - &results[0] << ", \"pbch_corrs\": [";
      for (const auto& corr_pair : result.pbch_corrs) {
        std::cout << "[ " << std::get<0>(corr_pair) << ", " << std::get<1>(corr_pair) << " ]";
        if (&corr_pair != &result.pbch_corrs.back()) {
          std::cout << ", ";
        }
      }
      std::cout << "] }" << std::endl;
    }
    else {
      std::cout << "{\"trial\": " << &result - &results[0] << ", \"pbch_corrs\": []}" << std::endl;
    }
  }

}

// Measure how long it takes to detect an SSB
int BenchmarkSSBDetectionTime(Radio& radio, uint32_t n_trials, uint32_t timeout_sec)
{
  resample_state_t rs;
  if (radio.RadioInit(&rs) != SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  // vector of ssb decode time results
  std::vector<double> ssb_decode_times;

  std::cout << "==== Benchmarking SSB detection time ====" << std::endl;

  std::vector<SSBDetectionResult> ssb_detection_results; // store results of all trials

  for (uint32_t i = 0; i < n_trials; i++) {
    auto start_time = std::chrono::system_clock::now();
    auto detect_res = radio.DetectSSB(rs, timeout_sec, true);
    auto end_time = std::chrono::system_clock::now();
    SSBDetectionResult result;
    result.start_time = std::chrono::duration<double>(start_time.time_since_epoch()).count();
    if (std::get<0>(detect_res) == SRSRAN_SUCCESS) {
      result.success = true;
      result.detection_time = std::chrono::duration<double>(end_time - start_time).count();
      result.pbch_corrs = std::get<1>(detect_res);
      ssb_detection_results.push_back(result);
    } else {
      result.success = false;
      result.detection_time = 0;
      ssb_detection_results.push_back(result);
    }
    print_ssb_detection_result(result);    
    // sleep from 1 - 5 seconds, randomly selected
    auto sleep_duration = std::chrono::milliseconds(1000 + (rand() % 4000));
    std::this_thread::sleep_for(sleep_duration);
  }
  // Cleanup
  if (radio.resample_needed) {
    for (uint8_t k = 0; k < RESAMPLE_WORKER_NUM; k++) {
      msresamp_crcf_destroy(rs.q[k]);
      free(rs.temp_y[k]);
    }
    free(rs.temp_x);
  }

  std::cout << "==== All SSB detection times (ms) ====" << std::endl;
  for (size_t i = 0; i < ssb_decode_times.size(); i++) {
    std::cout << "Trial " << i << ": " << ssb_decode_times[i] << " ms" << std::endl;
  }

  std::cout << "==== SSB detection benchmark results ====" << std::endl;
  print_ssb_detection_results(ssb_detection_results);
  return SRSRAN_SUCCESS;
}



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
  // run the SSB detection benchmark (10 trials, 10 seconds timeout for each trial)
  BenchmarkSSBDetectionTime(radio, 10, 10);
  return NR_SUCCESS;
}