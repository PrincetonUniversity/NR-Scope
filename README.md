NR-Scope
========

**NR-Scope** is a 5G wireless network telemetry tool for 5G Standalone network measurement and performance optimization.  NR-Scope decodes DCI, SIB and RACH information from a 5G Standalone (New Radio) base station.  It is distinct from [NG-Scope](https://github.com/PrincetonUniversity/NG-Scope), our prior 4G cellular network telemetry tool.

NR-Scope is [published](https://doi.org/10.1145/3680121.3697808) in ACM CoNEXT 2024: if you use this tool, we ask that you cite our paper and reach out to the developers at the email address `ng-scope-dev` *at* `lists.cs.princeton.edu`

```
@inproceedings{10.1145/3680121.3697808,
  author = {Wan, Haoran and Cao, Xuyang and Marder, Alexander and Jamieson, Kyle},
  title = {NR-Scope: A Practical 5G Standalone Telemetry Tool},
  year = {2024},
  isbn = {9798400711084},
  publisher = {Association for Computing Machinery},
  address = {New York, NY, USA},
  url = {https://doi.org/10.1145/3680121.3697808},
  doi = {10.1145/3680121.3697808},
  booktitle = {Proceedings of the 20th International Conference on Emerging Networking EXperiments and Technologies},
  pages = {73–80},
  numpages = {8},
  keywords = {5G network, network measurement, telemetry, wireless network},
  location = {Los Angeles, CA, USA},
  series = {CoNEXT '24}
}
```

We welcome contributions.  If you use NR-Scope and would like to contribute improvements, modifications, bug fixes, and the like, please initiate a pull request on this repository and one of our team will review your contribution.

NR-Scope and NG-Scope are open source products of NSF Award CNS-2223556 (IMR: MT).  Any opinions, findings, and conclusions or recommendations expressed in this material are those of the author(s) and do not necessarily reflect the views of the National Science Foundation.

# Features

The main features are as follows:

1. Supports 5G SA cells with 15kHz and 30kHz SCS, tested with 10MHz, 15 MHz, 20MHz, 40MHz and 100MHz cell bandwidth.
2. Carrier aggregation decoding, NR-Scope would use one SDR to try to decode both carrier aggregated and not carrier aggregated UEs within the RAN. Tested in commercial T-Mobile 15MHz FDD and 100MHz TDD cells.
3. 5G SA cell search and MIB decoding.
4. SIB 1 and all other SIBs decoding.
5. RRCSetup (MSG 4) decoding for continuously UE attatch detection.
6. Threaded DCI decoding for detected UEs.
7. SIB, RRCSetup and DCI decoding are threaded for performance and independent processing.
8. Can concurrently decode up to 64 UEs (max number we can create with groundtruth) in the same base station. In T-Mobile cell, we can decode up to hundreds of UEs.
9. Accurate DCIs, PRB and bit rate estimation for each UEs (please stay tuned for our paper).
10. Local logging functions in `.csv` file and remote google BigQuery uploading function in BigQuery table.
11. Support using multiple USRPs to decode multiple base stations independently, each has its own log.
12. Cell search for all possible frequencies in a separate file and the results are stored in `.csv` log file.
13. Non-initial (BWP id > 0) and plaintext (configured in SIB 1 or MSG 4) BWPs decoding in separate DCI decoder threads.
14. Threaded resampling function for better-fidelity TwinRX USRP X310 daughterboard (better signal quality but doesn't support 5G sampling rate without resampling).
15. Worker pool function is implemented. Each worker works on the slot data asynchronously. Now the processing doesn't need to keep up with the short slot time, and the throughput is increase through more workers.
16. Band list can be configured for cell scan functions.
17. A circular buffer is used for slot data dispatch among workers, now the CPU/worker number requirement is largely reduced.
18. PCI can be configured in the config.yaml. If configured, NR-Scope will proceed only if the detected cell's PCI matches what is configured.
19. Enable real-time constellation graph for PDCCH symbols, by setting `enable_gui: true` in the config file, now this function only support single USRP setting.
20. User provided RRCReconfiguration is supported, please check `rrc_recfg_config` folder for more information. We provide several examples of RRCReconfiguration for different scenarios (T-Mobile FDD and TDD SA cells).
21. Stay tuned... 😄

Please refer to the [wiki page](https://github.com/PrincetonUniversity/NG-Scope-5G/wiki) for more feature description and documentation.

## Notice about the base stations

If you are working with srsRAN base stations, please check out the `srsran` branch. The only difference is that during DCI decoding, the 64 QAM MCS table is selected, while the `main` branch uses 256 QAM MCS table, which is widely used in the commercial base stations. This MCS table information could be encrypted during the communication, thus it's set manually.

The `main` branch will decode the DCIs based on the RRCSetup decoded during the RACH, and will try to decode DCIs both with and without carrier aggregation bits. According to our experience with T-Mobile cells, it can decode the DCIs for most of the UEs in the RAN. In the `fixed_rnti` branch, we add another worker group to manually include the RRCReconfiguration information in their DCI decoders, because the RRCReconfiguration is encrypted and can't be decoded by NR-Scope. In this case, NR-Scope can decode nearly all of the UEs in the cells we tested with (~ 99%). Also, you can set a known RNTI in the config file in `fixed_rnti` branch, then this RNTI will be assigned to the worker group using the manually configuered RRCReconfiguration.

For a commercial cell, if you see a lot of RACH information but can't find further DCIs, this is because that the UEs are using updated configuration in the RRCReconfiguration. Please raise an issue and discuss with us, and if you could provide the RRCReconfiguration (from your phone), we can help you config NR-Scope properly.

## Requirements

We tested this system on Ubuntu 22.04 system and it may support other version of Ubuntu. To build this project and make it run properly, the following libraries are needed.

[UHD libraries](https://files.ettus.com/manual/page_install.html):

```
sudo add-apt-repository ppa:ettusresearch/uhd
sudo apt-get update
sudo apt-get install libuhd-dev uhd-host
```

[srsRAN_4G&#39;s requirements](https://docs.srsran.com/projects/4g/en/latest/general/source/1_installation.html):

```
sudo apt-get install build-essential cmake libfftw3-dev libmbedtls-dev libboost-program-options-dev libconfig++-dev libsctp-dev
```

[yaml-cpp](https://github.com/jbeder/yaml-cpp):

```
# In a different directory
git clone https://github.com/jbeder/yaml-cpp.git
cd yaml-cpp
mkdir build
cd build
cmake ..
make
sudo make install
```

We need [liquid-dsp](https://github.com/jgaeddert/liquid-dsp) for resampling if better-fidelity TwinRX USRP X310 daughterboard is used:

```
# In a different directory
sudo apt-get install automake autoconf
# download source codes
git clone https://github.com/jgaeddert/liquid-dsp.git
cd liquid-dsp
# Building and installing the main library
./bootstrap.sh
./configure
make
sudo make install
sudo ldconfig
# to double check, libs should appear at /usr/local/lib and header liquid.h should appear at /usr/local/include/liquid/
```

For different USRP daughterboard, different `config.yaml` should be used. Please refer to the sample `config.yaml` in `./nrscope/config/config.yaml` and the explanatory comment in it.

CBX:

```
......
rf_args: "clock=external,type=x300,sampling_rate=23040000" #"type=x300" #"clock=external"
rx_gain: 30 # for x310, max rx gain is 31.5, for b210, it's around 80
srate_hz: 23040000 #11520000 #11520000 #23040000
srsran_srate_hz: 23040000
......
```

TwinRX (note TwinRX has a significantly higher rx gain limit):

```
......
rf_args: "clock=external,type=x300,master_clock_rate=200000000,sampling_rate=25000000" #"type=x300" #"clock=external"
rx_gain: 90 # for x310, max rx gain is 31.5, for b210, it's around 80
srate_hz: 25000000 #11520000 #11520000 #23040000
srsran_srate_hz: 23040000
......
```

Also, you should turn on the performance mode:

```
# This script is copied from srsRAN_Project
cd script
sudo ./srsran_performance
```

Push DCI logs to google storage BigQuery table (optional):
Using google client c++ library requires c++ 14.0, where in ubuntu 22.04, the default c++ version is 11.0. We thought that nobody wants to mess with the system compiling environments, so we implement the function of pushing data to google cloud storage with python. Here is a step-by-step instruction of how to push the DCI log to our google cloud storage:

```
1. sudo pip install google-cloud-storage geocoder
2. sudo gcloud auth application-default login --impersonate-service-account bigquery-writer@tutorial-explore.iam.gserviceaccount.com
3. # There will be a link from goole, open that link and login with any of your google account.
4. sudo gcloud init
5. # In the config file (./nrscope/config.yaml), set push_to_google: true.
6. # In the config file (./nrscope/config.yaml), set google_service_account_credential: "/home/wanhr/Downloads/nsf-2223556-222187-b5d2ea50f5d1.json" with the google service account credential file (provided by us)'s location on your file system.
7. # In the config file (./nrscope/config.yaml), set google_dataset_id: "ngscope5g_dci_log" with the google cloud Bigquery dataset name, everyone has his/her own dataset. The code will create one if the dataset with this dataset id is not existed.
8. sudo gcloud auth application-default set-quota-project <Your Google Storage Project Name>
```

## Files and functions

```
Entry: /nrscope/src/main.cc
Load config: /nrscope/src/libs/load_config.cc
Radio thread (cell search, mib decoding, coreset decoding, etc.): /nrscope/src/libs/radio_nr.cc
Config file: /nrscope/config/config.yaml
Cell scan entry (scan all GSCN/SSB points): /nrscope/src/scan_main.cc
```

## Usage

```
mkdir build
cd build
cmake ../
make all -j ${nof_proc}
cd nrscope/src/
sudo ./nrscope | tee ./$(date +"%Y-%m-%d_%H:%M:%S").txt | grep Found # This command can save the trace and show if cell or DCIs are found.

# or to scan all 5G SA cells (in nrscope/src/)
sudo ./nrscan
```

Please refer to this [wiki page](https://github.com/PrincetonUniversity/NR-Scope/wiki/1.-Introduction#output-format) for the description of log output and [FAQ](https://github.com/PrincetonUniversity/NR-Scope/wiki/7.-Usage-Recommendations-and-FAQ).
