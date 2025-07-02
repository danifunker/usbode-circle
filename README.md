# USBODE: the USB Optical Drive Emulator
Ever wanted a GoTek for CDs? If you have a Raspberry Pi Zero W or 2 W, USBODE turns it into a virtual optical drive. It allows you to store many disk images on a MicroSD card and mount them through a web interface. See [Phil's Computer Lab](https://www.youtube.com/watch?v=Is3ULD0ZXnI) for a demo.

## Features
- Acts as an optical drive on retro computers equipped with USB.
- Switch images whithout rebooting or ejecting.
- Install and run disk-based programs, including multi-disc titles.
- Supports ISO images, and with [some caveats](#BIN/CUE-Limitations), BIN/CUE images.
- Use the [Waveshare OLED HAT](https://www.waveshare.com/wiki/1.3inch_OLED_HAT) to swap images without the need for a web interface.
- Or, use the [Pirate Audio Line Out/LCD HAT](https://shop.pimoroni.com/products/pirate-audio-line-out), which also has a Line Out, enabling CD audio.

Note: Some forms of CD-ROM copy protection won’t work with USBODE.

## Requirements:
1. A Raspberry Pi Zero W or Zero 2 W, Raspberry Pi 3A+ (needs USB-A to USB-A cable), Raspberry Pi 4B+ (Use the USB-C connector to connect to the computer)*. Pi4 support is currently in testing
2. A Wi-Fi network. The Pi will need to connect to one so that it can be controlled over the web interface. Your retro computer does not have to be connected to it, unless you want to operate the web interface from it.
3. A MicroSD card up to 256 GB (see [Card Size Limitations](#Card-Size-Limitations) for workarounds). Cards marked A1 or A2 will perform better.
4. A computer to perform the initial setup. It needs to be running any modern OS (Windows, Mac, Linux) and have a MicroSD card reader or an adapter.
3. A [Micro USB](https://en.wikipedia.org/wiki/USB_hardware#/media/File:MicroB_USB_Plug.jpg) cable (not [Mini USB](https://en.wikipedia.org/wiki/USB_hardware#/media/File:Cable_Mini_USB.jpg)) that can transfer data along with power.
4. The latest [USB-ODE Circle Release]([url](https://github.com/danifunker/usbode-circle/releases)).
5. A target computer with a USB port that will be utilizing USBODE.

### Optional Stuff
- USBODE supports the Waveshare OLED HAT and the PirateAudio Line Out/LCD HAT. The Pirate Audio HAT requires no configuration. The Waveshare requires that you change one line in the `config.txt` file. See Initial Setup below.
- The PirateAudio HAT also enables CD audio. An aux cable going into your sound card's Line In port will play that audio over your computer's speakers. Additionally, enterprising users have created 8mm-to-4-pin converter cables, allowing them to connect their Pi to their sound card's internal Line In port. You'll need to know your sound card's pinout to make one for yourself, as they vary between models. 

## Initial Setup
1. Mount the MicroSD card on the setup computer. Format it using FAT32 (See [Card Size Limitations](#Card-Size-Limitations) if you need to do this on a card larger than 32 GB). The developers recommend using the Raspberry Pi Imager to format the SDCard as FAT32. To perform this: 
   a. Open the Raspberry Pi Imager
   b. Under Raspberry Pi Device choose No Filtering
   c. Under Operating System, choose Erase
   d. Select the SD card you would like to format.
2. Open the USBODE ZIP file that was downloaded previously. Extract the files within to the root of the MicroSD card.
3. On the MicroSD card, open the file labeled “wpa_supplicant.conf”.
4. Under `country=GB`, replace “GB” with the [two digit code for your country](https://en.wikipedia.org/wiki/ISO_3166-1_alpha-2#Officially_assigned_code_elements) if needed. Different countries use different WiFi frequencies; if you are in the US, the device will not connect to US wifi unless you change this line to `country=US`.
5. Under `ssid=”MySSID”`, type in the name of your WiFi network between the quotes. Keep in mind that the Pi supports only 2.4 GHz wifi signals.
6. Under `psk=”WirelessPassword”`, type in your WiFi’s password between the quotes.
7. The other lines in this file likely don’t need to be changed. However, if your network uses different key management or other security configurations, this file can be modified to comply with those settings.
8. If desired, image files can now be copied into the Images folder on the card.
9. Eject the MicroSD card from the setup computer and put it into the Pi.

Setup is now complete. See the instructions below to learn how to use the USBODE.

## Using USBODE
1. Connect the Pi Zero to the target computer. The USB cable needs to be plugged into the Pi’s inner USB port, labeled “USB”, not the one labeled “PWR” or “PWR IN”.
2. The Pi should immediately turn on. It will then boot into the Circle environment, which takes about 7 seconds on a Pi Zero W 2 and an A1-class card. If the target computer is set up to use USB devices, it should quickly see the Pi as an optical drive.
   - Drivers may need to be installed under Windows 98, such as [nUSB](https://www.philscomputerlab.com/windows-98-usb-storage-driver.html). For DOS, we recommend the [USBASPI community driver](https://web.archive.org/web/20170907161705/https://www.mdgx.com/files/USBASPI.EXE).
3. If the “wpa_supplicant.conf” file is configured correctly, it should automatically connect to your wifi network within 10 to 15 seconds of startup.

If you have any difficulties, help is available on [Discord](https://discord.gg/na2qNrvdFY).

## Using the USBODE Web Interface
The browser interface is used to load images and shut down the device. To access the interface, you’ll need the IP address of the Pi. Once it connects to your WiFi, this address can be viewed from your router’s configuration page. It should appear as “CDROM” in the list of connected devices. If you use a display HAT, the display will also show the IP address. Use that IP address preceded by “http://” (not “https://”). For example, if your Pi’s IP address is 192.168.0.4, you would enter `http://192.168.0.4` into your browser’s address bar.

### Loading an Image via the Browser Interface:
The Web Interface displays a list of available images, and denotes the currently loaded image. Images can be switched at any time by clicking on them.

### Shutting Down USBODE:
On the USBODE homepage, click _Shutdown USBODE_. The LED indicator on the Pi will flash for several seconds and turn off.

## Copying Images onto USBODE
USBODE stores images on the MicroSD card in a folder labeled Images. You'll need to put .ISO and .BIN/.CUE files directly into this folder. This can be done by connecting the SD card to the setup computer and copying files, or by connecting to the Pi via FTP (see [Using FTP](#Using-FTP)). Mounting the card to the setup computer is the fastest method by a significant margin.

## Using USBODE with a HAT
1. With the Pi off, plug the HAT onto the Pi's GPIO pins.
2. PirateAudio HAT users can move to step 3. Waveshare users first need to edit the file `config.txt` on the SD card. Under `[usbode]`, note the line reading `“displayhat=pirateaudiolineout”`. Change this to `“displayhat=waveshare”`.
3. Plug the SD card into the Pi, and the Pi into the target computer. The screen on the HAT should turn on.
4. The display will turn on a second or two after plugging in the Pi. The display will say “Not connected yet” until it connects to your wifi network, then that line will display the device’s IP address. On the PirateAudio display, the bottom will indicate what the buttons do. On the Waveshare HAT, Key 1 enters Browsing Mode, where you can see what images are on your SD card. The directional stick on the left side can then be moved up and down to traverse the list of images. Moving it left or right skips 5 images, making it easier to get to the bottom of a long list. Pressing the directional stick or Key 1 here will select that image and load it. Key 2 will back out of that screen without changing the image, and Key 3 is not currently implemented.

### BIN/CUE Limitations
BIN/CUE images with multiple .BIN files do not yet work correctly. There is a workaround, however: [CDFix](https://web.archive.org/web/20240112090553/https://krikzz.com/pub/support/mega-everdrive/pro-series/cdfix/) will merge all of the .BIN files into one. Remember to make a backup of the image files before running this utility.

### Card Size Limitations
 - Cards larger than 32 GB will have issues on versions of Windows prior to 11. The Windows GUI format utility and the DISKPART command line utility will refuse to make FAT32 partitions larger than 32 GB. To bypass this, we recommend using the [Raspberry Pi Imager](https://www.raspberrypi.com/software/) or [FAT32FormatterGUI](https://www.softpedia.com/get/System/Hard-Disk-Utils/FAT32format-GUI.shtml).
 - Cards larger than 256 GB will not boot on the Pi, and we do not currently have a good workaround. If a card of that capacity is your only option, you must create a primary partition smaller than 256 GB. USBODE cannot yet navigate to different partitions, so this effectively leaves half or more of your card unusable, but it will at least boot.

### Using FTP: 
1. Open the FTP client of your choice.
2. Select FTP as the protocol, not SFTP.
3. Enter the USBODE's IP address (see the [Web Interface instructions](#Using-the-USBODE-Web-Interface)) into the Host Name field.
4. Ensure the Port number is 21.
5. If available, check the "Anonymous login" box. Otherwise, enter "anonymous" as the username.
6. Navigate to the Images folder, and drop image files into it.

## Notes about versions
- The Stable version of this project is available under the [Main branch](https://github.com/danifunker/usbode-circle/tree/main). This project also has a [pipeline](https://github.com/danifunker/usbode-circle/actions) set up to facilitate rapid deployment of new features. The pipeline builds are cutting edge and are not guaranteed to be stable.
- If you’re using USB-ODE 1.99 or before, you’ll also need the [Raspberry Pi Imager](https://www.raspberrypi.com/software/) application. That project and its instructions are located [here](https://github.com/danifunker/usbode/releases).

## Other notes
- Do not delete “image.iso” in the Images folder. It is required as a fallback.

## Discord Server
For updates on this project please visit the discord server here: (https://discord.gg/na2qNrvdFY)

Feel free to contribute to the project.

## Donations
Support me on ko-fi!
(https://ko-fi.com/danifunker)

Readme updated by [Zarf](https://github.com/Zarf-42) and wayneknight_rider.
