NG-Scope-5G
===========

Implement on top of srsRAN_4G UE code, decode the DCI and SIB information for 5G SA base station.

## Requirements

We tested this system on Ubuntu 22.04 system and it may support other version of Ubuntu. To build this project and make it run properly, the following libraries are needed. Please refer to the [wiki page](https://github.com/PrincetonUniversity/NG-Scope-5G/wiki) for feature description and detailed build instruction.

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

## Logs

(Aug 9, 2023) Solved some problems in synchronization, see how to use external clock to perform the synchronization.

(Aug 11, 2023) DCI 1_0 and PDSCH are decoded for gNB in n41 with `ssb_freq=2523.75 MHz`. SIB 1 payload is verified with  `/srsue/src/stack/rrc_nr/test/ue_rrc_nr_test.cc` and [libasn1](https://github.com/j0lama/libasn).

(Aug 13, 2023) DCI decoding is tested with the following settings using srsgNB: `n41, TDD, 20MHz, 30kHz SCS`; `n78, TDD, 20MHz, 30kHz SCS`; `n41, TDD, 20MHz, 15kHz SCS`; `n3, FDD, 20MHz, 15kHz SCS`; `n3, FDD, 10MHz, 15kHz SCS` and `n41, TDD, 10MHz, 15kHz SCS`.

(Aug 21, 2023) Used all RA-RNTI to decode DCI 1_0 scrambled with RA-RNTI and decoded the RAR (Msg 2) bytes. Then we should use the TC-RNTI in Msg 2 to decode DCI 1_0 for Msg 4 (RRC ConnectionSetup).

(Aug 26, 2023) Verified Msg 2 decoding and get TC-RNTI. Msg 4 successfully decoded and for UE tracked in RACH process, we can easily decode its other DCI in the following data communication.

(Sep 2, 2023) Decoding DCIs for 1 UE is finished. But some settings that affects DCI's size setting are still not clear to me. They are set manually and should be set according to RRC messages in the future.

(Sep 4, 2023) Downlink DCI 1_1 and grants are decoded, and the inconsistency between srsRAN_4G and srsRAN_Project codes causes the DCI size problem. Now the downlink grants are correctly decoded. Uplink DCI 0_1 decoded, but there are some problems left -- the elements of the DCI 0_1 is not set correctly so the uplink grants may have some problems. This problem can be easily solved by more reading, but left for the testing phase.

(Oct 1, 2023) Works for 30kHz SCS with 20MHz bandwidth in TDD bands (n41, n48, n78), both srsgNB and sercomm small cell, some bugs in different SCS and bandwidth settings, such as 15kHz+20MHz, 15kHz+10MHz and 30kHz+10MHz, in TDD band (n41) and FDD bands (n3).

(Oct 7, 2023) A very rough fix for `n3, FDD, 20MHz, 15kHz SCS` and `n3, FDD, 10MHz, 15kHz SCS` settings, in `lib/src/phy/dft/ofdm.c` function `srsran_ofdm_set_phase_compensation_nrscope(..)`, I added some fixed phase compensation for such settings. Now the code can decode SIB 1 for such settings, but we cannot verify them because the real phone cannot attach. Will move on to work with small cell (`n48, TDD, 20MHz, 30kHz SCS`) and gaming platform.

(Oct 30, 2023) Code works for small cell with known UEs (listened in RACH), as in srsgNB. Now try to make it work for unknow UEs, such as UEs that are already existing in the base station.

(Feb 8, 2024) Use a new repo for code release and next phase development.

(Feb 14, 2024) Decoding other SIBs (2, 3 transmitted by the small cell, and potentially other SIBs in other cells) is finished. Now we use three concurrent threads to decode SIB, RACH and DCI for the same TTI, not in a producer-consumer style but in a non-blocking function way. Because we want the processing happens in the same time, which would be helpful for future carrier aggregation that requires synchronization between cells. However, after a while of decoding, NG-Scope can't decode any new DCIs for SIB, RACH and exisiting UEs, the behavior looks like the synchronization failure when the GPS clock is not used. The next step is to figure it out what makes the function unable to decode any new DCIs. One guess is that the processing time for SIB and RACH RRC decoding takes too long (up to 1000 us) in some TTIs, which drags the buffer and destroys the synchronization, however according to some observation, the last few DCIs' processing time before the "stop" is not so long (around 10s us).

(Feb 15, 2024) The "stop" problem on Feb 14 is solved with some optimization in SIB and RACH thread. After the decoder has all the SIBs in the cell, it skips the SIB search thread to save time. NG-Scope 5G can detect all incoming UEs (4 in the small cell) on the run and decode DCI continuously. Now wait for the Amarisoft hardware for testing.

(Feb 23, 2024) Added the local log recording function, the output log will be a .csv file and the meaning of each column is in the first row. If needed, set the `local_log` to `true` in the config.yaml and set the `log_name` with the file name. There are still some bugs in the google storage code.

(Feb 26, 2024) Google storage bug is solved, now the code can push the captured DCI in a batch of 4000 entries to the google storage during the DCI decoding.

(Mar 27, 2024) Fan added support for PDSCH and PUSCH mapping type B for DCI decoding, thank Fan. Found a bug that we didn't use SLIV in SIB 1 or RRC Setup for time domain resource mapping, which previously happens to have the same calculation results (the first row of default mapping type A and SLIV 53 for downlink). Now it's solved. Also debugged the timestamp function.

(Mar 30, 2024) Now the code's `log` and `to_google` functions work with multiple threads (USRPs), we are one step away from real-time carrier aggregation calculation. We can calculate the carrier aggregation information from the written logs now in an off-line manner.

(July 17, 2024) Implemented NR SA cell scan utility: run `./nrscan` after build. The found cell information will be logged at `scan.csv`.

(July 25, 2024) Implemented monitoring of non-initial BWPs if the additional BWPs are configured through plaintext RRCSetup, regardless of whether the BWP(s)
are switched by plaintext DCI or encrypted RRCReconfiguration. TO-DO: (blind) detection of BWPs configured by encrypted RRCReconfigurations, homomorphic to CA detection.

(August 21, 2024) Create two sampling config parameters: `srsran_srate` and `srate`. `srsran_srate` is what the srsRAN signal processing saw, which should be in the 184.32MHz family (srsRAN process assumed). `srate` is what fed to the USRP RF, which should be compatible with your USRP hardware. Therefore, the resampling ratio will be `r = srsran_srate/srate` (see `config.yaml`). Down-resample the raw signal with ratio `r`, using the [liquid-dsp](https://liquidsdr.org/) library. As resampling takes non-trivial portion of time (based on our test machine), we optimize the "producer-consumer" coordination pattern in the time-critical baseband processing scenario.
Producer is codes fetching samples from USRP. Consumer is codes processing (e.g., fft and decoding). Previously, the pattern (a loop) looks like the following:

```
    start --> producer (subframe sample fetching) --> consumer (subframe processing) --> back to start
```

The constraint is you should finish an iteration within 1ms, otherwise you don't fetch the samples timely and overflow occurs. This means sync drift and monitoring no longer works. Without resampling, the whole iteration finish within 1ms robustly in our test machine. With resampling, it exceeds 1ms (on our test machine).
Therefore, we have the variant pattern B:

```
    initialize some semaphore S (resource produced/consumed indicator)
    (thread 1) start --> producer (subframe sample fetching, resampling, and buffering) --> S signal up --> back to start
    (thread 2) start --> S signal down --> consumer (subframe processing) --> back to start
```

(September 8, 2024) Enabled 40MHz cell monitoring. PCIe connection between USRP and computer is needed.

## TODOs

There are some on-going plans for the near future:

* ~~Try to decode RRC reconfiguration message, it's hard to do so because current srsRAN 5G UE code doesn't support MIMO PDSCH decoding.~~
* Hidden BWP decoding. Forming a dataset for different carriers' perference in BWP setting.
* Add carrier aggregation decoding function (multiple USRP to decode multiple cell towers that the UE is connected to).
* Found cell searcher can mistakenly find cells in nearby frequency (e.g., GSCN -1/-2 steps). In other words, when cell searcher's to-search SSB central frequency is x, it can find SSB at x - 1/2 step. We guess it's because the coarse correlation. Investigate possibly later.
