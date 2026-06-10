#include <iostream>
#include <string>
#include <unistd.h>
#include <getopt.h>

#include "nrscope/hdr/nrscope_def.h"
#include "nrscope/hdr/load_config.h"

#include "srsran/common/band_helper.h"
#include "srsran/phy/common/phy_common_nr.h"

/**
 * Benchmarking program.
 * Available benchmarks: 
 * SSB detection time
 */



struct SSBSearchResult {
  bool success; // whether the SSB was successfully detected
  double start_time; // the time when the SSB detection started
  double detection_time; // time to detect (0 if detection failed)
  std::vector<std::tuple<double, double>> pbch_corrs; // Optional PBCH correlation values for debugging
};

// summary of one result
void print_ssb_search_result(const SSBSearchResult& result)
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

void print_ssb_search_results(const std::vector<SSBSearchResult>& results)
{
  // print a summary of trials
  std::cout << "==== SSB Search Results Summary ====" << std::endl;
  for (const auto& result : results) {
    print_ssb_search_result(result);
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


int SSBSearchGain(Radio& radio, float gain_db_min, float gain_db_max, float gain_db_step, uint32_t timeout_sec)
  // Measure the ssb search strength across a range of gains.
  // For each gain, attempt one SSB search and print max PBCH correlation,
  // success/failure, and search time in JSON format.
{
  resample_state_t rs;
  if (radio.RadioInit(&rs) != SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  std::cout << "==== Benchmarking SSB search gain ====" << std::endl;
  std::cout << "==== SSB Search Results Summary ====" << std::endl;

  for (float gain = gain_db_min; gain <= gain_db_max + 1e-6f; gain += gain_db_step) {
    radio.SetRxGain(gain);

    auto start_time = std::chrono::system_clock::now();
    auto detect_res = radio.SearchSSB(rs, timeout_sec, true);
    auto end_time   = std::chrono::system_clock::now();

    bool   success        = std::get<0>(detect_res) == SRSRAN_SUCCESS;
    double detection_time = success ? std::chrono::duration<double>(end_time - start_time).count() : 0.0;
    double start_time_d   = std::chrono::duration<double>(start_time.time_since_epoch()).count();

    double max_corr = 0.0;
    for (const auto& corr_pair : std::get<1>(detect_res)) {
      max_corr = std::max(max_corr, std::get<1>(corr_pair));
    }

    std::cout << std::fixed << std::setprecision(6)
              << "{\"gain_db\": " << gain
              << ", \"start_time\": " << start_time_d
              << ", \"ssb_found\": " << (success ? "true" : "false")
              << ", \"detection_time\": " << detection_time
              << ", \"max_pbch_correlation\": " << max_corr
              << "}" << std::endl;
  }

  if (radio.resample_needed) {
    for (uint8_t k = 0; k < RESAMPLE_WORKER_NUM; k++) {
      msresamp_crcf_destroy(rs.q[k]);
      free(rs.temp_y[k]);
    }
    free(rs.temp_x);
  }

  return SRSRAN_SUCCESS;
}

int SSBSearchTime(Radio& radio, uint32_t n_trials, uint32_t timeout_sec)
  // Measure how long it takes to search for the SSB and decode the MIB in each trial.
  // Print results in JSON format, including max pbch correlation from each trial
{
  resample_state_t rs;
  if (radio.RadioInit(&rs) != SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  // vector of ssb decode time results
  std::vector<double> ssb_decode_times;

  std::cout << "==== Benchmarking SSB search time ====" << std::endl;

  std::vector<SSBSearchResult> ssb_search_results; // store results of all trials

  for (uint32_t i = 0; i < n_trials; i++) {
    auto start_time = std::chrono::system_clock::now();
    auto detect_res = radio.SearchSSB(rs, timeout_sec, true);
    auto end_time = std::chrono::system_clock::now();
    SSBSearchResult result;
    result.start_time = std::chrono::duration<double>(start_time.time_since_epoch()).count();
    if (std::get<0>(detect_res) == SRSRAN_SUCCESS) {
      result.success = true;
      result.detection_time = std::chrono::duration<double>(end_time - start_time).count();
      result.pbch_corrs = std::get<1>(detect_res);
      ssb_search_results.push_back(result);
    } else {
      result.success = false;
      result.detection_time = 0;
      result.pbch_corrs = std::get<1>(detect_res);
      ssb_search_results.push_back(result);
    }
    print_ssb_search_result(result);    
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

  std::cout << "==== All SSB search times (ms) ====" << std::endl;
  for (size_t i = 0; i < ssb_decode_times.size(); i++) {
    std::cout << "Trial " << i << ": " << ssb_decode_times[i] << " ms" << std::endl;
  }

  std::cout << "==== SSB search benchmark results ====" << std::endl;
  print_ssb_search_results(ssb_search_results);
  return SRSRAN_SUCCESS;
}



void print_available_commands()
{
  std::cout << "Usage: nrbench -c <config.yaml> <command> [args...]" << std::endl;
  std::cout << std::endl;
  std::cout << "Available commands:" << std::endl;
  std::cout << "  ssbtime [n_trials] [timeout_sec]              Measure SSB detection latency over multiple trials" << std::endl;
  std::cout << "  ssbgain <gain_min> <gain_max> <gain_step> [timeout_sec]  Measure SSB strength across a gain sweep" << std::endl;
}

int main(int argc, char** argv){

  // Initialise logging infrastructure
  srslog::init();

  std::string file_name = "config.yaml";
  int opt;
  while ((opt = getopt(argc, argv, "c:")) != -1) {
    if (opt == 'c') {
      file_name = optarg;
    } else {
      print_available_commands();
      return NR_FAILURE;
    }
  }

  // remaining positional args: command [args...]
  int pos = optind;
  if (pos >= argc) {
    print_available_commands();
    return NR_SUCCESS;
  }

  std::string cmd = argv[pos++];

  if (cmd != "ssbtime" && cmd != "ssbgain") {
    std::cout << "Unknown command: " << cmd << std::endl;
    print_available_commands();
    return NR_FAILURE;
  }

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
  if (cmd == "ssbtime") {
    uint32_t n_trials    = (pos < argc) ? std::stoul(argv[pos++]) : 10;
    uint32_t timeout_sec = (pos < argc) ? std::stoul(argv[pos++]) : 10;
    SSBSearchTime(radio, n_trials, timeout_sec);
  } else if (cmd == "ssbgain") {
    if (pos + 2 >= argc) {
      std::cout << "ssbgain requires gain_min, gain_max, and gain_step" << std::endl;
      print_available_commands();
      return NR_FAILURE;
    }
    float    gain_min    = std::stof(argv[pos++]);
    float    gain_max    = std::stof(argv[pos++]);
    float    gain_step   = std::stof(argv[pos++]);
    uint32_t timeout_sec = (pos < argc) ? std::stoul(argv[pos++]) : 10;
    SSBSearchGain(radio, gain_min, gain_max, gain_step, timeout_sec);
  }
  return NR_SUCCESS;
}