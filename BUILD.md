1. Copy the file `build-usbode.conf.example` to `${HOME}/build-usbode.conf`
2. Install the latest Arm GNU Toolchain for your OS/architecture. https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads. 
    - for MacOS using apple silicon use the package `arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi.pkg`
    - for Linux on x64 use `arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz`
3. Update `${HOME}/build-usbode.conf` to match where you installed the package installed in the previous step.
4. Navigate into the this repo, then run `./build-usbode.sh`. 
5. Output will be located in the `dist/` folder.


##Mac Build Notes
- Install complete xcode suite & cli tools
- Install the following packages through brew: `bash`, `gnu-getopt`, `texinfo`
- Add gnu-getopt to your path then restart the shell.
- build-usbode.sh should work correctly now.