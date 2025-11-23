# Building USBODE on Linux

This document outlines the steps required to build the USBODE firmware on a Linux-based system.

## 1. Prerequisites

You will need to install the following packages:

*   **ARM Cross-Compiler:** The `gcc-arm-none-eabi` toolchain is required to compile the code for the Raspberry Pi.
*   **Texinfo:** The `texinfo` package is required for the build scripts.

You can install these on a Debian-based system (like Ubuntu) using the following command:

```bash
sudo apt-get update
sudo apt-get install -y gcc-arm-none-eabi texinfo
```

## 2. Toolchain Configuration

The build script needs to know the path to the ARM cross-compiler. You can configure this by creating a file named `build-usbode.conf` in your home directory (`~/`).

The contents of this file should be:

```
PathPrefix=arm-none-eabi
```

**Note:** Do not include a trailing hyphen (`-`) in the `PathPrefix`. The build script will add it automatically.

## 3. Building the Firmware

Once the prerequisites are installed and the configuration file is in place, you can build the firmware by running the following command from the root of the repository:

```bash
./build-usbode.sh
```

If the build is successful, the final firmware and associated files will be located in the `dist/` directory.

## 4. Linker Errors (Advanced)

In some cases, the C library used by the project (`newlib`) may require certain system-level functions that are not available in the bare-metal environment. This can result in linker errors for "undefined references" to functions like `_getentropy`.

The project includes a `src/syscalls.cpp` file to provide "stub" implementations for these functions. If you encounter a new undefined reference, you may need to add a stub for it in this file.
