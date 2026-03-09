#!/bin/bash

# This script builds a local copy of the UHD driver and SC2430 extension, 
# then builds NR-Scope, configured to use the local driver build.
# The script assumes it is in "<repo_root>/scripts", 
# and puts all the UHD src / build / install files in "<repo_root>/uhd".
set -e  # Exit on any error

START_DIR="$(pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" 
REPO_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
LOCAL_UHD="$REPO_ROOT/uhd_local" # all UHD and SC2430 source, builds, and install go here
INSTALL_PREFIX="$LOCAL_UHD/uhd_install" # UHD installs to here
mkdir -p $LOCAL_UHD

cd $LOCAL_UHD

# Install UHD dependencies (Ubuntu/Debian)
# sudo apt-get update
# sudo apt-get install autoconf automake build-essential ccache cmake cpufrequtils doxygen ethtool \
# g++ git inetutils-tools libboost-all-dev libncurses5 libncurses5-dev libusb-1.0-0 libusb-1.0-0-dev \
# libusb-dev python3-dev python3-mako python3-numpy python3-requests python3-scipy python3-setuptools \
# python3-ruamel.yaml 

# clone uhd
[ ! -d "uhd" ] && git clone https://github.com/EttusResearch/uhd.git
# get validated version 
cd uhd
git checkout v4.8.0.0

# Build uhd drivers and install to local dir
cd host
mkdir -p build
cd build
cmake ../ -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" -DENABLE_TESTS=OFF
make -j$(nproc)
make install

# Clone SC2430 extension
cd $LOCAL_UHD
[ ! -d "SC2430-UHDExtension" ] && git clone https://github.com/SignalCraftTechnologies/SC2430-UHDExtension.git

# Fix SC2430 linkage, only if not already patched. Note UHD_LIBRARIES is defined in the SC2430 CMakeLists.txt, not here.
grep -q 'target_link_libraries(sc2430' SC2430-UHDExtension/CMakeLists.txt || sed -i.bak '/^add_library(sc2430/a\
target_link_libraries(sc2430 ${UHD_LIBRARIES})
' SC2430-UHDExtension/CMakeLists.txt

# Build SC extension and install to local dir
cd SC2430-UHDExtension
mkdir -p build
cd build
cmake ../ -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"
make -j$(nproc)
make install

# put the sc2430 extension where uhd expects to find it.
mkdir -p "$INSTALL_PREFIX/share/uhd/modules"
ln -s -f "$INSTALL_PREFIX/lib/libsc2430.so" "$INSTALL_PREFIX/share/uhd/modules/"
cd $LOCAL_UHD


# Build nrscope using local driver builds above.
cd $REPO_ROOT
export PKG_CONFIG_PATH="$INSTALL_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH"
export UHD_DIR="$INSTALL_PREFIX"
mkdir -p build
cd build
cmake ../ \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_INSTALL_RPATH="$INSTALL_PREFIX/lib" \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,--disable-new-dtags" \
    -DENABLE_UHD=ON \
    -DENABLE_SRSUE=ON \
    -DENABLE_SRSENB=OFF \
    -DENABLE_SRSEPC=OFF \
    -DENABLE_RF_PLUGINS=ON
make -j$(nproc)
cd $START_DIR
# copy built nrscope binary to start dir for easy access
cp $REPO_ROOT/build/nrscope/src/nrscope $START_DIR/nrscope-sc2430
echo "Build complete. nrscope binary copied to ""$START_DIR/nrscope-sc2430".
echo "To run, copy a config file for the sc2430 ( cp ./nrscope/config/config_sc2430.yaml ./config.yaml ) and run ./nrscope-sc2430 | tee log.txt"
echo "To remove everything built above: rm -rf uhd_local build nrscope-sc2430"
