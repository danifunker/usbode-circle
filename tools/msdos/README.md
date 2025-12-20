# USBODE tools for MS-DOS

This directory contains utilities to access USBODE from MS-DOS.

```
.
├── MNTUSBOD.BAT    # Batch file to check for a USBODE and to mount an image by name
├── CDREADY.BAT     # Batch file to invoke CDREADY.COM
├── CDREADY.COM     # A small utility to check whether a CD-ROM drive has readable media
└── cdready         # Source code for CDREADY.COM (see below for build instructions)
    ├── CDREADY.ASM
    ├── Makefile
    └── Makefile.unix
```

### Prerequisites

- `scsitb.exe` from [escsitoolbox v0.3-beta](https://github.com/nielsmh/escsitoolbox/releases/tag/v0.3-beta)
- `cwsdpmi.exe` from [CWSDPMI r5](https://www.delorie.com/pub/djgpp/current/v2misc/csdpmi5b.zip)
- `sed.exe` from [DJGPP sed v4.8](https://www.delorie.com/pub/djgpp/current/v2gnu/sed48b.zip)

Place the executables in your `PATH` or in the directory where you are going to run the tools from.

### Running the tools

The `MNTUSBOD.BAT` batch file expects the following environment variables to be set (e.g. in `autoexec.bat`):

- `CDROM`, the drive letter assigned to the USBODE device
- `TEMP`, the path of a temporary directory

**NOTE:** The batch files and the `cdready` utility have been tested on MS-DOS v6.22 and FreeDOS v1.4. They may not work correctly on other versions of DOS. 

## Building the cdready binary

### Prerequisites

- [NASM v3.01 for DOS](https://www.nasm.us/pub/nasm/releasebuilds/3.01/dos/nasm-3.01-dos-upx.zip)
- [CWSDPMI r5](https://www.delorie.com/pub/djgpp/current/v2misc/csdpmi5b.zip)
- [DJGPP make](https://www.delorie.com/pub/djgpp/current/v2gnu/mak44b.zip)

Install the prequisites, and run the following command in the `cdready` directory:

```shell
make clean all
```

To build the binary on UNIX-like systems, install NASM and GNU make, and issue the command:

```shell
make -f Makefile.unix clean all
```
