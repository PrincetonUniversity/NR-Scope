## Using NR-Scope with the X410 + SC-2430 

The SC2430 signal conditioner requires a UHD driver extension to use. The linking between NR-Scope, the UHD driver, and the SC2430 driver extension can be tricky to set up. This note and associated script describes one straightforward approach. 


### Quick start

From the repo root, run the following commands. Note that this was tested on a fresh clone with no `build` directory.

```bash
./scripts/sc2430/build.sh 
cp ./nrscope/config/config_sc2430.yaml ./config.yaml
./nrscope-2430
```

### Building

The easiest way to use NR-Scope with the [X410](https://www.ettus.com/all-products/usrp-x410/) SDR and [SC2430](https://www.signalcraft.com/products/microwave-systems/sc2430/) signal conditioner is to build a local copy of the UHD driver and its SC2430 extension, then build NR-Scope linking to those libraries. 

`scripts/sc2430/build.sh` will pull and build the UHD and SC2430 drivers, build NR-Scope, and copy the nrscope binary to `./nrscope-sc2430`.

`nrscope-sc2430` can then be used as normal, with additional support for sc2430.

### Config

There is an example config for a 100mhz cell:`./nrscope/config/config_sc2430.yaml`. 

Notice two details in this config: 

1. The `rf_arg` `extension=sc2430` flag tells the UHD driver to use the sc2430 extension, e.g.: `rf_args: "clock_source=external,time_source=external,type=x4xx,extension=sc2430`. 

2. `max_rx_gain` can go up to 81 with the sc2430+x410, versus 60 without the sc2430.

No other config settings change for the sc2430 in particular, as the uhd driver automatically configures other parameters.
