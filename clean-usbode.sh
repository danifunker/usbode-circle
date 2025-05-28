#!/bin/bash
set -e
export MAKEFLAGS="-j4"

projectRoot=$(git rev-parse --show-toplevel)
echo "Cleaning..."
destDir=${projectRoot}/dist
git submodule update --init --recursive
cd ${projectRoot}/circle
./makeall clean
cd ${projectRoot}/circle/addon/wlan
./makeall clean
cd ${projectRoot}/circle/addon/wlan/firmware
make clean
cd ${projectRoot}/circle/boot
make clean
cd ${projectRoot}/addon/discimage
make clean
cd ${projectRoot}/addon/cueparser
make clean
cd ${projectRoot}/addon/filelogdaemon
make clean
cd ${projectRoot}/circle/addon/linux
make clean
cd ${projectRoot}/circle/addon/fatfs
make clean
cd ${projectRoot}/circle/addon/SDCard
make clean
cd ${projectRoot}/circle/addon/Properties
make clean
cd ${projectRoot}/lib/usb/gadget
make clean
cd ${projectRoot}/src
make clean

rm -rf ${destDir}
