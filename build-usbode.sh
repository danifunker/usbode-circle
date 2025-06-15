#!/bin/bash
set -e

# Circle Raspberry Pi model number (1, 2, 3, 4, 5, default: 1)
export RPI_MODEL=1
export MAKEFLAGS="-j8"

projectRoot=$(git rev-parse --show-toplevel)
destDir=${projectRoot}/dist
git submodule update --init --recursive
cd ${projectRoot}/circle-stdlib/
./configure -r ${RPI_MODEL}
make all
if [ ! -f "${projectRoot}/circle-stdlib/libs/circle/addon/wlan/firmware/LICENCE.broadcom_bcm43xx" ]; then
    cd ${projectRoot}/circle-stdlib/libs/circle/addon/wlan/firmware
    make -j2
fi
if [ ! -f "${projectRoot}/circle-stdlib/libs/circle/boot/LICENCE.broadcom" ]; then
cd ${projectRoot}/circle-stdlib/libs/circle/boot
make -j2
fi
cd ${projectRoot}/addon/gitinfo
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
cd ${projectRoot}/circle-stdlib/libs/circle/addon/linux
make clean
make
cd ${projectRoot}/circle-stdlib/libs/circle/addon/Properties
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
mkdir -p ${destDir}/system
cp ${projectRoot}/sdcard/image.iso.gz ${destDir}/system
gunzip ${destDir}/system/image.iso.gz
cp ${projectRoot}/sdcard/test.pcm.gz ${destDir}/system
gunzip ${destDir}/system/test.pcm.gz
mkdir -p ${destDir}/firmware
cp ${projectRoot}/circle-stdlib/libs/circle/addon/wlan/firmware/* ${destDir}/firmware
rm ${destDir}/firmware/Makefile
cp ${projectRoot}/circle-stdlib/libs/circle/boot/bootcode.bin ${destDir}
cp ${projectRoot}/circle-stdlib/libs/circle/boot/start.elf ${destDir}
#arch=$(cat ${projectRoot}/circle-stdlib/libs/circle/Config.mk  | grep AARCH | awk '{print $3}')
arch=32
cp ${projectRoot}/circle-stdlib/libs/circle/boot/config${arch}.txt ${destDir}/config.txt
cat ${projectRoot}/sdcard/config-usbode.txt >> ${destDir}/config.txt
cp ${projectRoot}/sdcard/config-options.txt ${destDir}
cp ${projectRoot}/sdcard/cmdline.txt ${destDir}

echo "Build Completed successfully. Copy the contents of ${destDir} to a freshly formatted SDCard (FAT32 or EXFAT) and try the build!"
