## Using NR-Scope with the X410 + SC-2430 

The SC2430 signal conditioner requires a UHD driver extension to use. The linking between NR-Scope, the UHD driver, and the SC2430 driver extension can be tricky to set up. The `sc2430.sh` script builds a local copy of the driver, extension, and NR-Scope; it copies the nrscope binary to `./nrscope-sc2430`, which can be used as normal with additional support for the sc2340.


### Quick start

If you are using this branch for features besides SC2430 support, compile as normal.

**For using SC2430**
From the repo root, run the following commands. Note that this was tested on a fresh clone with no `build` directory.
```bash
./scripts/sc2430/sc2430-build.sh # build ./nrscope-2430
cp ./nrscope/config/config_100mhz_sc2430.yaml ./config.yaml # get example 2430 config file
./nrscope-2430 # run nrscope with 2430 support
```

### New config file options
This branch adds the following options to the config file.

`single_threaded_workers: true` makes each decode worker compute in a single thread, rather than spawning new threads for RACH and DCI decoding. This improves performance slightly by reducing synchronization overhead. Note that running workers in single threaded mode will require more workers overall. 

`exit_on_overflow: true` aborts when an overflow is detected in the frontend, rather than just stopping output and continuing to run.

`silent: true` suppresses many info messages to stdout. Improves performance a bit.

`skip_ssb_decode_num: 1` Attempt to decode every SSB (around once per 20ms), even in the track phase. Set to a multiple of 10 to decode one out of every N SSBs. Set to 0 to disable SSB decoding (default). SSB decoding in the track phase can help maintain synchronization in long runs of NR-Scope, and is not that computationally intensive.


### Files
`scripts/sc2430/sc2430-build.sh` pulls and builds the UHD and SC2430 drivers to `uhd_local/`, builds NR-Scope, and copies the nrscope binary from `./build/nrscope/src/nrscope` to `./nrscope-sc2430`.

Any rebuilds of nrscope (i.e., `cd build; make`) will also be linked to the local UHD and SC2430.

`./nrscope/config/config_100mhz_sc2430.yaml` is an example config file for listening to a 100 Mhz cell with the x410+sc2430.

#### Config file notes
A few X410 / SC2430 details in the config file:

1. The `rf_arg` `extension=sc2430` flag tells the UHD driver to use the sc2430 extension, e.g.: `rf_args: "clock=external,type=x4xx,extension=sc2430`. If using the x410 alone, simply remove the argument.

2. `max_rx_gain` can go up to 81 with the sc2430+x410, versus 60 for standalone x410.

3. We use `srate_hz: 122880000` and `srsran_srate_hz: 122880000` to monitor 122.88 Mhz. This is 1/2 the clock rate of the x410, to reduce processing cost. Since it is an integer factor, resampling is not required, which would be an added cost.


### Tuning Notes

- Use a large MTU (e.g., 9000) for the interface connected to the x410.

- Running nrscope with a higher priority may reduce receive buffer overflows, e.g.: `sudo nice -n -10 ./nrscope-sc2430`

- On a machine with multiple NUMA nodes, it is **VERY IMPORTANT** for performance to limit `nrscope` to a single NUMA node, e.g.: `sudo numactl --cpunodebind=0 --membind=0 nice -n -10 ./nrscope-sc2430`. The bottleneck is in the memcopy in FetchAndDecode, from frontend subframes to worker slots.

- For best NUMA performance, bind to the NUMA node connected to the interface attached to the SDR. `cat /sys/class/net/<MY_INTERFACE_NAME>/device/numa_node` will return the NUMA node id to put into the `--cpunodebind=0 --membind=0` of `numactl`

- NR-Scope decoding workers may be configured to use multiple threads internally. In most cases, performance will be better if each worker is limited to a single thread. using the config option `single_threaded_workers: true`. Note, this may require increasing the number of workers.



## Record and replay mode

This branch also adds preliminary record and replay modes to NR-Scope.

**Record mode** allocates a buffer at startup, copies rf samples to the buffer as they arrive, and writes them to "samples.bin" when the buffer is full. Normal operation continues after that.

**Replay mode** opens "samples.bin" instead of a socket to the SDR, and reads samples from that file instead. It terminates at EOF. 

### Usage

1. Toggle record or replay mode with the `mode` option in your config.yaml: 

`mode: "RECORD"` or `mode: "REPLAY"`

2. For record mode, set the capture buffer length: 

`record_buf_size_gb: 8`

**Important: NR-Scope currently assumes that no other configurations change between a recording and a replay.**
