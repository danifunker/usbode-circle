#!/bin/bash
set -e

projectRoot=$(git rev-parse --show-toplevel)
echo "This script requires a successful ./configure -r X --prefix=/path/to/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi- run in ${projectRoot}"
echo "This script also requires a success build in ${projectRoot}/boot and ${projectRoot}/addon/wlan/firmware"
destDir=${projectRoot}/dist
git submodule update --init --recursive
cd ${projectRoot}/circle
./makeall clean
./makeall
exit


cd ${projectRoot}/addon/discimage
make clean
make
cd ${projectRoot}/addon/linux
make clean
make
cd ${projectRoot}/addon/cueparser
make clean
make
cd ${projectRoot}/addon/fatfs
make clean
make
cd ${projectRoot}/addon/SDCard
make clean
make
cd ${projectRoot}/addon/filelogdaemon
make clean
make
cd ${projectRoot}/addon/Properties
make clean
make
cd ${projectRoot}/test/usb-cd-gadget
make clean
make

rm -rf ${destDir}
mkdir -p ${destDir}
cp kernel*.img ${destDir}
cp wpa_supplicant.conf ${destDir}
mkdir -p ${destDir}/images
cp cmdline.txt ${destDir}
cp image.txt ${destDir}
cp image.iso ${destDir}/images
mkdir -p ${destDir}/firmware
cp ${projectRoot}/addon/wlan/firmware/* ${destDir}/firmware
rm ${destDir}/firmware/Makefile
cp ${projectRoot}/boot/bootcode.bin ${destDir}
cp ${projectRoot}/boot/start.elf ${destDir}
arch=$(cat ${projectRoot}/Config.mk  | grep AARCH | awk '{print $3}')
cp "${projectRoot}/boot/config${arch}.txt" ${destDir}/config.txt
cp cmdline.txt ${destDir}

echo "Build Completed successfully. Copy the contents of ${destDir} to a freshly formatted SDCard (FAT32 or EXFAT) and try the build!"
