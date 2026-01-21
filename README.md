# USBODE: the USB Optical Drive Emulator

Ever wanted a GoTek for CDs? If you have a Raspberry Pi Zero W or 2 W, USBODE turns it into a virtual optical drive. It allows you to store many disk images on a MicroSD card and mount them through a web interface. This project was featured on [Phil's Computer Lab](https://www.youtube.com/watch?v=Is3ULD0ZXnI), and his video provides an excellent demo.

## Features

- Acts as an optical drive on retro computers equipped with USB.
- Switch images whithout rebooting or ejecting.
- Install and run optical disc-based programs, including multi-disc titles.
- Supports ISO images, and with [some caveats](#BIN/CUE-Limitations), BIN/CUE images.
- Use the [Waveshare OLED HAT](https://www.waveshare.com/wiki/1.3inch_OLED_HAT) to swap images without the need for a web interface.
- Or, use the [Pirate Audio Line Out/LCD HAT](https://shop.pimoroni.com/products/pirate-audio-line-out) (`PIM483`), which also has a Line Out, enabling CD audio.
- Ability to use PWM audio for CD Audio (low quality, but works in a pinch), needs to be configured in the settings menu through the web UI.
- Some users have reported success with the IQaudio DAC+, however this would have to be used with a zero without headers since the headers on this device are backwards.
- Audio via the HDMI port (this might be the most expensive option though). Read details below.


Note: Some forms of CD-ROM copy protection won’t work with USBODE.

## Requirements:

1.  A supported Raspberry Pi SBC, please review the following table for details:
    
    | Model                          | Support status | Notes |
    |--------------------------------|----------------|-------|
    | Raspberry Pi A/A+ (2014)       | Unsupported    | Requires USB Host mode to operate |
    | Raspberry Pi 2 B (2015)        | Unsupported    | Requires USB Host mode to operate |
    | Raspberry Pi Zero (2015)       | Supported      | As of 2.9.2 now supported |
    | Raspberry Pi Zero W (2017)     | Supported      | If purchasing a new Pi, try to get WH model (this has headers) |
    | Raspberry Pi Zero 2 W / WH (2021)| Supported      | If purchasing a new Pi, try to get WH model (this has headers). Has multi-processor support, which may be used in the future by USBODE |
    | Raspberry Pi 3 Model A+ (2018)       | Supported      | Requires USB-A-to-USB-A cable. |
    | Raspberry Pi 3 Model B (2016) / B+    | Unsupported   | No USB host mode on this model (2018) |
    | Raspberry Pi 4 (2019)/4B (2019)   | Supported    | Use USB-C power port for gadget mode. All RAM configurations supported |
    | Raspberry Pi 400               | Unknown Support     | I'm unsure if this model works |
    | Raspberry Pi 5 (2023)                | Unsupported    | USB host-mode stack not supported by current dependencies. |

2.  A target computer (i.e. the computer you want to emulate optical drives on) that supports USB.
3.  A computer to perform the initial setup on. Needs to be running any modern OS and have a MicroSD card reader/adapter.
4.  (Optional) A 2.4 GHz Wi-Fi network. The Pi will connect to this network and will then be controllable through a web browser. Any device that is connected to that same network can then be used to control the USBODE. This includes mobile devices and even retro computers running IE 6 or above.
5.  A MicroSD card. Previous limitations of 256 GB have been resolved and has been tested with SDCards up to 1TB. Cards marked A1 or A2 will perform better.
6.  A [Micro USB](https://en.wikipedia.org/wiki/USB_hardware#/media/File:MicroB_USB_Plug.jpg) cable (not [Mini USB](https://en.wikipedia.org/wiki/USB_hardware#/media/File:Cable_Mini_USB.jpg)) that can transfer data along with power.
7.  The latest [USBODE Circle Release](https://github.com/danifunker/usbode-circle/releases).

### Optional Stuff

- USBODE supports the Waveshare OLED HAT and the PirateAudio Line Out/LCD HAT. The Pirate Audio HAT requires no configuration. The Waveshare requires that you change one line in the `config.txt` file. See Initial Setup below. There are some new considerations for the Waveshare OLED HAT, please see details below.
- The PirateAudio HAT also enables CD audio. An aux cable going into your sound card's Line In port will play that audio over your computer's speakers. Additionally, enterprising users have created 8mm-to-4-pin converter cables, allowing them to connect their Pi to their sound card's internal Line In port. You'll need to know your sound card's pinout to make one for yourself, as they vary between models.
- For Audio setup via HDMI, a number of additional components (~$30 USD) is required in order for it to work, read below for further details
- If using a Raspberry Pi 4, USB Audio might work, but this is untested (please provide comments in a new github issue!)

## Initial Setup

1.  Download the [latest release](https://github.com/danifunker/usbode-circle/releases) `.img` file. For Raspberry Pi Zero W (not 2) use the 32-bit build, all other supported Raspberry Pis can use either builds, but be aware 32-bit is tested more thoroughly.
2.  Mount the MicroSD card on the setup computer. Using the Raspberry Pi Imager, flash the latest release image to the MicroSD card. To perform this:
    - Open the Raspberry Pi Imager.
    - Under Raspberry Pi Device, choose No Filtering
    - Under Operating System, choose `Use Custom`
    - Select the `.img` file that was downloaded in step 1
    - Under Storage, select the SD card to flash
    - Click Next. If the Imager gives you an error, try using diskpart's Clean command, then try again, or check out our [Discord](https://discord.gg/na2qNrvdFY).
3.  If required, re-insert the MicroSD card if a `bootfs` volume is not listed. Open the `bootfs` volume on the MicroSD card, open the file labeled “wpa_supplicant.conf”.
4.  Under `country=GB`, replace “GB” with the [two digit code for your country](https://en.wikipedia.org/wiki/ISO_3166-1_alpha-2#Officially_assigned_code_elements) if needed. Different countries use different WiFi frequencies; if you are in the US, the device will not connect to US wifi unless you change this line to `country=US`.
5.  Under `ssid=”MySSID”`, type in the name of your WiFi network between the quotes. Keep in mind that the Pi supports only 2.4 GHz wifi signals.
6.  Under `psk=”WirelessPassword”`, type in your WiFi’s password between the quotes.
7.  The other lines in this file likely don’t need to be changed. However, if your network uses different key management or other security configurations, this file can be modified to comply with those settings.
8. Safely remove the MicroSD Card from the computer as to reduce the likelyhood of corrupt files.
8. Plug the MicroSD card into the Raspberry Pi and connect the Raspberry Pi to a computer to complete setup. If using a pirateaudiolineout (PIM483) screen, a setup screen will appear. The device will reboot itself automatically within 60 seconds once setup has completed. Be aware, no networking is available during this phase.
9. Once the initial setup has completed, remove the MicroSD card from the Raspberry Pi and copy image files to the IMGSTORE volume on the SDCard. Remember to always Safely remove the MicroSD card from the computer, however it is safe to just pull the power from the Pi in most instances.

Setup is now complete. See the instructions below to learn how to use the USBODE.

## Upgrading USBODE Versions
To upgrade USBODE without losing any of your disc images or settings, first make sure the version is at least 2.14.0 for PirateAudio, or 2.18.3 for Waveshare. ***Alternatively, you can set the screen to pirateaudio screen during the upgrade if you are on a version higher than 2.14.0 and are using the waveshare screen, but be aware the upgrade messages will not appear on the screen***. 

Next, download the following files from the release you are trying to ugprade to:
- sysupgrade.tar
- sysupgrade.crc
- sysupgrade64.tar
- sysupgrade64.crc

Place all of those files onto the root of the bootfs SDCard partition, and reboot the Pi. If you know which architecutre you are using, only copy that set of architecture files over. The update-service will detect which file should be extracted. Be aware the .crc file is not optional and is used to do a quick verification the of the tarball prior to installing the version.

The update process should start and usually takes less than 5 minutes to complete. Some screen messages should appear, however failure messages don't seem to work properly so if you see it hanging after about 10 minutes, that probably means the update failed.

It is also possible there might be additional files that have been added by Mac, if USBODE isn't starting correctly and the sdcard has recently been connected to a Mac, plug the SDCard back into the mac, then open a terminal on that mac and run the following command `find /Volumes/bootfs -iname '*._*' -exec rm -f {} \;` to cleanup any remnants of it. Be sure to eject the SDcard correctly by either using the eject button in finder, or if still in the shell `diskutil eject /Volumes/bootfs`. As long as no errors appear the disk will be ejected sucessfully so you can take it out from the Mac.

Worst case, take a backup of the SDCard and re-flash the image using the Raspbery Pi Imager.

## Using USBODE

1.  Connect the Pi to the target computer.
	-   Zero W/Zero 2W: Connect the MicroUSB end to the Pi's "USB" port, not the “PWR” or “PWR IN” port. Connect the USB-A end into your target computer. Do not connect any other power to the Raspberry Pi 3A+ as it can cause a power loop.
	-   3a +: Connect one of the USB-A ends into the Pi's USB-A port and the other into your target computer. Do not connect any other power to the Raspberry Pi 3A+ as it can cause a power loop.
	-   4b +: Connect the USB-C end to the "Power In" port, and the USB-A end into your target computer.
2.  The Pi should immediately turn on. It will then boot into the Circle environment, which takes about 7 seconds on a Pi Zero W 2 and an A1-class card. If the target computer is set up to use USB devices, it should quickly see the Pi as an optical drive.
    - Drivers may need to be installed under Windows 98, such as [nUSB](https://www.philscomputerlab.com/windows-98-usb-storage-driver.html). For DOS, we recommend the [USBASPI community driver](https://web.archive.org/web/20170907161705/https://www.mdgx.com/files/USBASPI.EXE).
3.  If the “wpa_supplicant.conf” file is configured correctly, it should automatically connect to your wifi network within 10 to 15 seconds of startup.

If you have any difficulties, help is available on [Discord](https://discord.gg/na2qNrvdFY).

### Configuring USBODE for DOS / Windows 3.X
1. Update `config.sys` file to use one of the USBASPI drivers, the one the developers recommend is the one like the one referred to in the previous section.
2. Update `config.sys` file to use a `usbcd1.sys` file. The developers recommend the panasonic one (`Panasonic USB CD-ROM Driver v1.0`).
3. Update `autoexec` to use `mscdex.exe` or `SHSUCDX.exe` with the switch `/d:usbcd001` since that is the default CDROM device name provided by usbcd1.sys.

## Using the USBODE Web Interface

The browser interface is used to load images, shutdown/reboot the device, configure settings, and view logs.To access the interface, you’ll need the IP address of the Pi. Once it connects to your WiFi, this address can be viewed from your router’s configuration page. It should appear as “usbode” in the list of connected devices. If you use a display HAT, the display will also show the IP address. Use that IP address preceded by “http://” (not “https://”). For example, if your Pi’s IP address is 192.168.0.4, you would enter `http://192.168.0.4` into your browser’s address bar. The address http://usbode or http://usbode.local should also work as well.

### Loading an Image via the Browser Interface:

The Web Interface displays a list of available images, and denotes the currently loaded image. Images can be switched at any time by clicking on them.

### Configuration
- The Configuration page can be used to change HAT settings, configure audio output, USB speed, and whether logging is enabled and how verbose it is.
- The Display Configuration section lets you change what kind of HAT you have connected, if any. It also lets you change the screen's timeout, brightness when in use, brightness when asleep, and how long the sleep timeout is.
- Audio Configuration is used to choose what mode your audio output HAT uses, as well as the volume.
- USB Configuration can be used to change the Pi's USB mode. If your retro PC has issues with High Speed (USB 2.0), it may work with Full Speed (USB 1.1).
- Logging Configuration determines if logging is on, what file the logs get written to, and how much detail is included via Log Level.

An Audio Test function is also available on this page. Click "Execute Sound Test", and the USBODE will send a sound out the Line Out port. If your speakers, Line In port, etc. are configured correctly, you should hear it over your speakers.

### Shutting Down USBODE:

On the USBODE homepage, click *Shutdown USBODE*. The LED indicator on the Pi will flash for several seconds and turn off. Or if there is a HAT connected to the Pi, navigate through the menu to choose Shutdown. Be aware that removing power from this version of USBODE has not adversly affected developers in the same way the previous linux based version has.

## Copying Images onto USBODE

USBODE stores images on the MicroSD partition labeled IMGSTORE. You'll need to put .ISO and .BIN/.CUE files directly onto this volume. This can be done by connecting the SD card to the setup computer and copying files, or by connecting to the Pi via FTP (see [Using FTP](#Using-FTP)). Mounting the card to the setup computer is the fastest method by a significant margin. As of 2.19.0 USBODE also supports folder structures, see the Folder Support section below for further details.

If adding DVD images, (and the system needs to read the disc as "DVD"), add `.dvd` to the filename, so a file named MyMovie.iso would be named `MyMovie.dvd.iso`

**DVD Support is still a work in progress, but mostly stable now**

## Using USBODE with a HAT

1.  With the Pi off, plug the HAT onto the Pi's GPIO pins.
2.  PirateAudio HAT users can move to step 3. Waveshare users first need to edit the file `config.txt` on the SD card. Under `[usbode]`, note the line reading `“displayhat=pirateaudiolineout”`. Change this to `“displayhat=waveshare”`. This can also be set in the web interface once USBODE is up and running. When using the waveshare HAT, be aware there are some GPIO PIN conflicts with the I2S audio, so in order to resolve this conflict, either completely disable audio by editing `cmdline.txt` and removing the `sounddev=sndi2s` block, OR set it to HDMI either in the web interface or set `sounddev=sndhdmi`. Another option, if you actually have an i2s HAT with the waveshare is to override the default button layout for the waveshare. Look in [addon/displayservice/README.md](addon/displayservice/README.md) for specific instructions on how-to wire up your own SH1106 or ST7889 displays. Finally be aware that there is also an issue with PWM audio and the Waveshare, since the center button is shared with one of the PWM ones (GPIO 13). Remapping this button by setting a custom display should resolve the PWM issue.
3.  Plug the SD card into the Pi, and the Pi into the target computer. The screen on the HAT should turn on.
4.  The display will turn on a second or two after plugging in the Pi. The display will say “Not connected yet” until it connects to your wifi network, then that line will display the device’s IP address. On the PirateAudio display, the bottom will indicate what the buttons do. On the Waveshare HAT, Key 1 enters Browsing Mode, where you can see what images are on your SD card. The directional stick on the left side can then be moved up and down to traverse the list of images. Moving it left or right skips 5 images, making it easier to get to the bottom of a long list. Pressing the directional stick or Key 1 here will select that image and load it. Key 2 will back out of that screen without changing the image, and Key 3 is not currently implemented.

### Supported FileTypes
* ISO files
* BIN/CUE files (the files must have the same name, aside from extension)
* MDS/MDF files (Unsure if SafeDisc rips are supported, files must have the same name aside from extension)
* CHD but be aware performance is about half of what bin/cue is. It is possible to convert with a special arm compression by adding `-f -c cdzs,cdfl -hs 9792` to the chdman conversion (this requires MAME 280+). This is commonly recommended in the misterFPGA community

Other filetypes may be added in the future.

### BIN/CUE Limitations

BIN/CUE images with multiple .BIN files do not yet work correctly. There is a workaround, however: use chdman to convert the multi-bin file to chd, then convert
it back to a bin/cue. Example command lines:

`chdman createcd -i MultiBin.cue -o MultiBin.chd` 
Note CHD files are compatible with USBODE, but performance isn't perfect yet. So to convert back to .bin/cue use:
`chdman extractct -i MultiBin.chd -o SingleBin.cue -ob SingleBin.bin`

If there are issues with CD playback, please try this first. The previous recommendation was to use an old unsupported software which does not properly support conversion.

### Using FTP:

1.  Open the FTP client of your choice.
2.  Select FTP as the protocol, not SFTP.
3.  Enter the USBODE's IP address (see the [Web Interface instructions](#Using-the-USBODE-Web-Interface)) into the Host Name field.
4.  Ensure the Port number is 21.
5.  If available, check the "Anonymous login" box. Otherwise, enter "anonymous" as the username.
6.  Navigate to `1:/` aka `USB` (`1:/` is the internal name for the second partition, USB is the incorrectly labeled drive name available through the FTP server), and drop image files into it.

## HDMI Audio Support
USBODE version 2.6.0 introduces HDMI audio support. Testing & development revealed some quirks about getting this configuration setup. In order to support HDMI audio out, the following components are required:

- HDMI Cable: Pi Zero W / Zero 2 W uses Mini HDMI, Pi 3A+ Uses Standard HDMI and Pi4B uses MicroHDMI. For reference, the Pi5 also uses MicroHDMI. It is also possible to use adapter to switch the connector types, but be aware there isn't much space on much of these Pis!
- HDMI Audio Splitter. One of the developers had purchased this specific one from [Amazon](https://www.amazon.com/dp/B017B6WFP8) however it should work with most / all HDMI audio splitters
- HDMI Dummy Dummy Plug: One of the developers purchased this specific Pack of 3 HDMI 4K Dummy plugs from [Amazon](https://www.amazon.com/dp/B0CKKLTWMN?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1)
- Sound Cable to connect 3.5" sound cable into sound card (same audio aux cable would work from the PirateAudioLineOut solution)
*Keep in mind that the HDMI audio splitter will probably require it's own power as well.

The developer believes other sound outputs would work fine, however the above configuration replicates behavior of a traditional CDROM drive best, as it runs directly through the soundcard.

## Custom USB PID / VID
To set a custom USB PID/VID add the following lines into the `[usbode]` section of config.txt
```
usbcdrom_vid=0x0e8d
usbcdrom_pid=0x1887
```
be sure to include the preceeding `0x` to instruct USBODE this is a hex number, not a decimal number.

## Folder Support
Folder support was added in version 2.19.0. The max path is 512 characters, although this has not been full tested yet. I believe the max filename size is still 256 characters as well. 

There is a toggle to enable flattened folders, which lists all of the available disc images on one screen. This might be helpful if you've forgotten where you put a disc image. Toggling this does not require a restart of USBODE. I have implemented this in lieu of search functionality.

## 2-Stage Sleep Mode
Some new functionality has been added in verion 2.20.0 where there is a 2 stage dimming mode for the Pirate Audio/ST7889 screen. The first stage is low-power mode, which will automatically set the screen to a brightness of 32 (so fairly dim), then the second phase is sleep mode, which turns off the display altogether.

## Disc Artwork
To use disc artwork, place a 240x240px JPG with the same basename of your disc image in the same folder as the disc image. e.g. if the disc image is called `mygame.iso` creating the `mygame.jpg` will be what discart expects. There is a new dedicated app which helps to identify and resize images automatically. Please have a look at the companion project [ODE-artwork-downloader](https://www.github.com/danifunker/ODE-artwork-downloader)

The disc art is shown on the web interface, as well on the ST7889 screen after about 2 seconds from being on the homepage, as well as just before entering low-power mode.

This is not configurable yet, but please open an issue if this should get adjusted.

## Classic MacOS 9.2.1 Mixed-Mode Disc Support
To use USBODE with MacOS9, USBODE must be in "Classic Mac" mode. This is set either in `config.txt` with `usbtargetos=apple` or by going into the Web Interface configuration and selecting "Classic Mac" mode, which will set the `config.txt` manually for you.

This mode enables a couple of quirks for MacOS9, and sets the emulation mode to a Sony Spressa CD-ROM drive, which uses the USBAuthoring extension, and allows for Digital Audio Extraction over USB, and the ability to mount mixed-mode discs over USB. 

To switch discs when USBODE is in Classic Mode, eject the disc from the OS first, then mount a new disc and macos9 should detect it.

Current requirements are as follows:
* OS 9.2.1
* iTunes 1.1
* USBAuthoring Extension 1.1.2 (1.1 doesn't work)

I tried initially to get this to work in OS 9.1, however CD Audio games weren't playing the music correctly.
Feel free to test different combinations, and please report back earlier MacOS version support for mixed-mode discs!

Current known-good working games:
Age of Empires (Requires Patch 1.3) -- sometimes CD Audio doesn't start, restart the game to fix this
MechWarrior 2

Current known-bad games: (CD Audio doesn't work correctly):
Quake 2

I have not been able to get Quake 2 CD Audio working on a physical USB CDROM drive either.

If experiencing issues with games not listed on this list, please try to update to the latest patch version.

I have been doing some deep research, it seems that many games use the AppleCD driver/API directly, and this might be a limiting factor for this implementation. If you have a SCSI to USB bridge adapter (particular one of interest to me is the Ratoc one) please reach out to me directly so maybe you can help me explore this option!

Currently the CDROM drive we are emulating in classic mac mode is not compatible with MacOS 10.4 (possibly other MacOS versions as well) so I am also evaluating on how that works.

The detection mechanism for many classic MacOS games assumes a single CDROM drive installed in the Mac. This causes an issue with USBODE, so I have created a patch which disables the internal CDROM drive (SCSI or IDE) and allows easy switching between USBODE as the default and re-enabling the existing physical drives, though it does require a reboot. This is the patch, named  [USBCDExtensionPatcher](https://github.com/danifunker/USBCDExtensionPatcher).


## Notes about version

- The Stable version of this project is available under the [Main branch](https://github.com/danifunker/usbode-circle/tree/main). This project also has a [pipeline](https://github.com/danifunker/usbode-circle/actions) set up to facilitate rapid deployment of new features. The pipeline builds are cutting edge and are not guaranteed to be stable.
- If you’re using USBODE 1.99 or before, you need to re-image your SDcard with the [Raspberry Pi Imager](https://www.raspberrypi.com/software/) application. That project and its instructions are located [here](https://github.com/danifunker/usbode/releases). Be aware USBODE 1.99 currently supports booting better than this project.

## Other Limitations / Known Bugs
- ITX-Llama is not able to boot from USBODE-circle at this time, it is currently being investigated but no ETA for resolution
- Some modern OSes have a hard time picking up USBODE-circle, this device is designed for retro computers and has been tested in almost all cases up to Windows 7, it is currently being investigated but no ETA for resolution
- Raspberry Pi 5 is not supported yet due to the dependency chain not supporting USB host mode on pi5s. This may change in the future
- Network Initialization can be a little bit slow, please give the device 10-15 seconds to initialize, this should be resolved in the future with a dependency chain update* This may be resolved but we are awaiting user feedback

## CD Audio Cable Creation to Soundcard
One of the developers purchased this: https://www.amazon.com/dp/B0BZWHVK4B?ref_=ppx_hzsearch_conn_dt_b_fed_asin_title_2&th=1

And used an existing 4-pin CD Audio Cable which can be found here - https://www.amazon.com/Kentek-Computer-Internal-Blaster-DVD-ROM/dp/B07KVF1DY1?crid=38R2KIO014ZFP&dib=eyJ2IjoiMSJ9.Kmw09Awtx2aEGxSh5BVucgSxoH4krHwKBnIjc4-q4MsUdg9MKl6t1YeYPVx9KoTt6xwE3d55CqdHmA8290dJJetnqCtOOUjbcZC6Hn1uywspv0K92m91mCcoGYsyoUAHWiqYhi6UV3Bq1xQWXo3lKgh05uGV4GeBRjMVXPaJcn8KWgdHw09iXLIdOM9MiDtgi-hMveUfRkoMU2mB05ftw20R25fNndK4Pp55wPuABBc.eo0wq0ptcPoQHYEtjhrO0IIuWfxy-gJWUeX-I7lkJ9M&dib_tag=se&keywords=mpc+audio+cable+4+pin&qid=1755264551&sprefix=mpc+audio+cable+4+pi%2Caps%2C210&sr=8-3

Be sure the audio connector fits your sound card correctly

For an R-G-G-L CD Audio cable: 

1) Connect the Yellow cable from the TRS3  to the white cable on the CD Audio (L signal)

2) Connect the Red Cable from the TRS3 to the Red Cable on the CD Audio  (R signal)

3) Connect the Black Cable from the TRS3 to the Black Cable(s) of the CD Audio (These are the grounds). In my case the grounds were surrounding the White and Red cables from my CD audio cable.

It's also possible to use dupont headers to chain these audio cables together, which is how one of the developers has their device setup.

## Other notes

- Do not delete “image.iso” on the IMGSTORE partition. It is required as a fallback.
- The image "usb-audio-sampler" can be used to test your Line-In settings and USBODE's audio settings. It can be safely deleted if space is at a premium. To test, use Windows 98 CD Player and play track 2. Be aware this audio test has a section near the begining of the track where only audio should come from "L", and when audio should only come from "R". This is to help troubleshoot stereo related issues with custom built cables. The track starts in stereo until around 11 seconds, then goes to "L" only from 11-18.5 seconds, then goes to "R" only from 19 seconds until about 25 seconds, at which point the track stays at stereo for the rest of the time. 

## CD-ROM / DVD-ROM Command Debugging
To enable verbose CD-ROM/DVD-ROM command debugging, in the `config.txt` under the `[usbode]` section add a new line containing `debug_cdrom=1`. Setting this to 0 or removing the line will disable the feature. Only enable debugging if required, since it does impact performance.

## Discord Server

For updates on this project please visit the discord server here: [https://discord.gg/8qfuuUPBts](https://discord.gg/8qfuuUPBts)

Feel free to contribute to the project.

## Donations

Support me on ko-fi!  
(https://ko-fi.com/danifunker)

Readme updated by danifunker, [Zarf](https://github.com/Zarf-42) and wayneknight_rider.
