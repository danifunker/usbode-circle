#!/bin/bash
set -e

projectRoot=$(git rev-parse --show-toplevel)
echo "This script requires a successful ./configure -r X --prefix=/path/to/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi- run in ${projectRoot}/circle"
git submodule update --init --recursive
circleDir="${projectRoot}/circle"
buildConfPath="${HOME}/build-usbode.conf"
configMkPath="${circleDir}/Config.mk"

export MAKEFLAGS="-j4"

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
find . -name Makefile -exec bash -c 'make -C "${1%/*}" clean' -- {} \;
# Build for each supported architecture
for arch in "${supported_rasppi[@]}"; do
    echo "Building for Raspberry Pi $arch architecture"
    
    # Configure for this architecture
    echo "Configuring for RASPPI=$arch"
    cd "$circleDir"
    ./configure -r $arch --prefix $PathPrefix -f
    
    # Return to project root
    cd "$projectRoot"
    
    # Lines 11-59 from your original script
    echo "Running make for RASPPI=$arch"

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
        make
    fi
    if [ ! -f "${projectRoot}/circle/boot/LICENCE.broadcom" ]; then
    cd ${projectRoot}/circle/boot
    make
    fi
    cd ${projectRoot}/addon/discimage
    make clean
    make 
    cd ${projectRoot}/addon/cueparser
    make clean
    make
    cd ${projectRoot}/addon/filelogdaemon
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
    cd ${projectRoot}/lib/usb/gadget
    make clean
    make
    cd ${projectRoot}/addon/usbode-display
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

BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")

zipFileName="usbode-${BRANCH}-${COMMIT}.zip"
cd ${destDir}
zip -r ${projectRoot}/${zipFileName} ./*
echo "Built ${zipFileName}  . Copy the contents of the zip file to a freshly formatted SDCard (FAT32 or EXFAT) and try the build!"
