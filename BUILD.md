1. Copy the file `build-usbode.conf.example` to `${HOME}/build-usbode.conf`
2. Install the latest Arm GNU Toolchain for your OS/architecture. https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads. 
    - for MacOS using apple silicon use the package `arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi.pkg`
    - for Linux on x64 use `arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz`
4. Run `make multi-arch` to compile a build for all supported architectures.
5. Output will be located in the `dist/` folder.

(Optional)
Create a file `$(HOME)/build-usbode.conf`
which should contain contents that look similar to:
```
supported_rasppi=(1 2)
PathPrefix=arm-none-eabi-
```

If there are custom paths that need setup, or you can add or remove devices to support easily, then when executing

`make dist-single` 

It will automatically take the first in the supported_rasppi list

## Build Examples:
To build for a single architecture:
`make RASPPI=2 dist-single`

To build for a single architecture with debug flags on:
`make RASPPI=4 DEBUG_FLAGS="USB_GADGET_DEBUG" dist-single`

To build with a build number:
`make release BUILD_NUMBER=123`

To build for a single architecture with build number:
`make RASPPI=4 BUILD_NUMBER=123 dist-single`

The build number will be displayed as `2.2.5-123` but stored internally as just `123`.

##Mac Build Notes
- Install complete xcode suite & cli tools
- Install the following packages through brew: `bash`, `gnu-getopt`, `texinfo`
- Add gnu-getopt to your path then restart the shell.
- build-usbode.sh should work correctly now.