#!/bin/bash
set -e

projectRoot=$(git rev-parse --show-toplevel)
echo "This script requires a successful ./configure -r X --prefix=/path/to/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi- run in ${projectRoot}/circle-stdlib"
git submodule update --init --recursive
circleDir="${projectRoot}/circle-stdlib"
buildConfPath="${HOME}/build-usbode.conf"
configMkPath="${circleDir}/Config.mk"
if ls ${projectRoot}/usbode*.zip 1> /dev/null 2>&1; then
    echo "Removing existing zip files..."
    rm ${projectRoot}/usbode*.zip
fi
export MAKEFLAGS="-j8"

# Check if build.conf exists
if [ ! -f "$buildConfPath" ]; then
    echo "Error: build.conf file not found at $buildConfPath"
    echo "Please create this file with supported_rasppi=1 2 3 (or your desired architectures)"
    exit 1
fi

# Read supported architectures from build.conf
source "$buildConfPath"
if [ ${#supported_rasppi[@]} -eq 0 ]; then
    echo "Error: supported_rasppi not defined in build.conf"
    exit 1
fi
if [ -z "$PathPrefix" ]; then
    echo "Error: PathPrefix not defined in build.conf"
    exit 1
fi

# Create destination directory once
destDir="${projectRoot}/dist"
rm -rf ${destDir}
mkdir -p ${destDir}
echo "Performing a full clean in the directory tree"
cd ${projectRoot}
# commented this out for a more targetted approach
# find . -name Makefile -exec bash -c 'make -C "${1%/*}" clean' -- {} \;
# Build for each supported architecture
for arch in "${supported_rasppi[@]}"; do
    echo "Building for Raspberry Pi $arch architecture"
    
    # Configure for this architecture
    echo "Configuring for RASPPI=$arch"
    cd "$circleDir"
    #./configure -r $arch
    
    echo "Running make for RASPPI=$arch"
    make all
    if [ ! -f "${projectRoot}/circle-stdlib/libs/circle/addon/wlan/firmware/LICENCE.broadcom_bcm43xx" ]; then
        cd ${projectRoot}/circle-stdlib/libs/circle/addon/wlan/firmware
        make
    fi
    if [ ! -f "${projectRoot}/circle-stdlib/libs/circle/boot/LICENCE.broadcom" ]; then
        cd ${projectRoot}/circle-stdlib/libs/circle/boot
        make
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
    cp ${projectRoot}/src/kernel*.img ${destDir}
done

echo "Platform Specific Builds Completes Sucessfully, copying general files to ${destDir}"
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

BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")

zipFileName="usbode-${BRANCH}-${COMMIT}.zip"
cd ${destDir}
zip -r ${projectRoot}/${zipFileName} ./*
echo "Built ${zipFileName}  . Copy the contents of the zip file to a freshly formatted SDCard (FAT32 or EXFAT) and try the build!"
