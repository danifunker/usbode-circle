#!/bin/bash
set -e

export MAKEFLAGS="-j8"

projectRoot=$(git rev-parse --show-toplevel)
echo "This script requires a successful ./configure -r X --prefix=/path/to/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi- run in ${projectRoot}"
destDir=${projectRoot}/dist
git submodule update --init --recursive
cd ${projectRoot}/circle
./makeall clean
./makeall
cd ${projectRoot}/circle/addon/fatfs
make clean
make
cd ${projectRoot}/circle/addon/SDCard
make clean
make
cd ${projectRoot}/circle/addon/wlan
./makeall clean
./makeall
if [ ! -f "${projectRoot}/circle/addon/wlan/firmware/LICENCE.broadcom_bcm43xx" ]; then
    cd ${projectRoot}/circle/addon/wlan/firmware
    make -j2
fi
if [ ! -f "${projectRoot}/circle/boot/LICENCE.broadcom" ]; then
cd ${projectRoot}/circle/boot
make -j2
fi
cd ${projectRoot}/addon/gitinfo
make clean
make 
cd ${projectRoot}/addon/usbcdgadget
make clean
make 
cd ${projectRoot}/addon/usbmsdgadget
make clean
make 
cd ${projectRoot}/addon/discimage
make clean
make 
cd ${projectRoot}/addon/cueparser
make clean
make
cd ${projectRoot}/addon/filelogdaemon
make clean
make
cd ${projectRoot}/addon/webserver
make clean
make
cd ${projectRoot}/addon/ftpserver
make clean
make
cd ${projectRoot}/circle/addon/linux
make clean
make
cd ${projectRoot}/circle/addon/Properties
make clean
make
cd ${projectRoot}/addon/display
make clean
make
cd ${projectRoot}/addon/gpiobuttonmanager
make clean
make
cd ${projectRoot}/addon/cdplayer
make clean
make
cd ${projectRoot}/src
make clean
make

rm -rf ${destDir}
mkdir -p ${destDir}
cp ${projectRoot}/src/kernel*.img ${destDir}
cp ${projectRoot}/sdcard/wpa_supplicant.conf ${destDir}
cp ${projectRoot}/sdcard/cmdline.txt ${destDir}
mkdir -p ${destDir}/images
cp ${projectRoot}/sdcard/image.iso.gz ${destDir}/images
gunzip ${destDir}/images/image.iso.gz
cp ${projectRoot}/sdcard/test.pcm.gz ${destDir}
gunzip ${destDir}/test.pcm.gz
mkdir -p ${destDir}/firmware
cp ${projectRoot}/circle/addon/wlan/firmware/* ${destDir}/firmware
rm ${destDir}/firmware/Makefile
cp ${projectRoot}/circle/boot/bootcode.bin ${destDir}
cp ${projectRoot}/circle/boot/start.elf ${destDir}
arch=$(cat ${projectRoot}/circle/Config.mk  | grep AARCH | awk '{print $3}')
cp ${projectRoot}/circle/boot/config${arch}.txt ${destDir}/config.txt
cat ${projectRoot}/sdcard/config-usbode.txt >> ${destDir}/config.txt
cp ${projectRoot}/sdcard/config-options.txt ${destDir}
cp ${projectRoot}/sdcard/cmdline.txt ${destDir}

echo "Build Completed successfully. Copy the contents of ${destDir} to a freshly formatted SDCard (FAT32 or EXFAT) and try the build!"
