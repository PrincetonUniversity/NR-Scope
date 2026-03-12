## Using NR-Scope with the X410 + SC-2430 

The SC2430 signal conditioner requires a UHD driver extension to use. The linking between NR-Scope, the UHD driver, and the SC2430 driver extension can be tricky to set up. The `sc2430.sh` script builds a local copy of the driver, extension, and NR-Scope; it copies the nrscope binary to `./nrscope-sc2430`, which can be used as normal with additional support for the sc2340.


### Quick start

From the repo root, run the following commands. Note that this was tested on a fresh clone with no `build` directory.

```bash
./scripts/sc2430/sc2430-build.sh # build ./nrscope-2430
cp ./nrscope/config/config_100mhz_sc2430.yaml ./config.yaml # get example 2430 config file
./nrscope-2430 # run nrscope with 2430 support
```
### Files

`scripts/sc2430/sc2430-build.sh` pulls and builds the UHD and SC2430 drivers, builds NR-Scope, and copies the nrscope binary from `./build/nrscope/src/nrscope` to `./nrscope-sc2430`.

`./nrscope/config/config_100mhz_sc2430.yaml` is an example config file for listening to a a 100 Mhz cell with the x410+sc2430.

### Notes

There are 3 important details in the config file:

1. The `rf_arg` `extension=sc2430` flag tells the UHD driver to use the sc2430 extension, e.g.: `rf_args: "clock=external,type=x4xx,extension=sc2430`. If using the x410 alone, simply remove the argument.

2. `max_rx_gain` can go up to 81 with the sc2430+x410, versus 60 for standalone x410.

3. We use `srate_hz: 122880000` and `srsran_srate_hz: 122880000` to monitor 122.88 Mhz. This is 1/2 the clock rate of the x410, to reduce processing cost. Since it is an integer factor, resampling is not required, which would be an added cost.

### Tuning Notes

- Use a large MTU (e.g., 9000) for the interface connected to the x410.

- Running nrscope with a higher priority may reduce receive buffer overflows, e.g.: `sudo nice -n -10 ./nrscope-sc2430`