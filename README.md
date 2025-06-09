# USBODE: the USB Optical Drive Emulator
Ever wanted a GoTek for CDs? The USBODE is an optical drive emulator that uses a Raspberry Pi Zero W to read image files from a Micro SD card, and acts as an optical drive through a USB cable. By default, it can load .ISO and .BIN/CUE images, and can be controlled over a web interface.

## What can it do?
By emulating a CD-ROM drive with USBODE, you can:
- Store a collection of image files (ISO and BIN/CUE format) on the SD card and quickly switch between them.
- Install and run CD-based games without the need for physical media. This includes multi-disc titles.
- Additionally, the USBODE can use the [Waveshare OLED HAT](https://www.waveshare.com/wiki/1.3inch_OLED_HAT), which provides a way to directly control the device without the need for a web interface.
- It can also use the [Pirate Audio Line Out/LCD HAT](https://shop.pimoroni.com/products/pirate-audio-line-out), which has all of the advantages of the Waveshare hat _and_ has a Line Out for CD Audio playback.
Note: Some forms of CD-ROM copy protection won’t work on the ODE.

## Requirements:
1. A Raspberry Pi Zero W or Zero 2 W (USBODE is optimized for the Pi Zero 2 W).
2. A MicroSD card. Cards up to 128 GB have been tested and work, larger cards might work as well. The system files take up less than 20 MB, but CD images can be 650 MB each. We suggest using a card marked A1 or A2 for best performance.
3. A setup computer with the ability to mount and format the MicroSD card. No special software is required, but Windows 10 and below have an arbitrary 32 GB limitation on FAT32 partitions. For larger cards, use the command line DISKPART utility, the [Raspberry Pi Imager](https://www.raspberrypi.com/software/), or [FAT32FormatterGUI](https://www.softpedia.com/get/System/Hard-Disk-Utils/FAT32format-GUI.shtml).
4. A USB cable with male Type A on one side, and male [Micro B](https://en.wikipedia.org/wiki/USB_hardware#/media/File:MicroB_USB_Plug.jpg) on the other. Micro B is what most people think of as an ordinary Micro USB cable (as opposed to [Mini](https://en.wikipedia.org/wiki/USB_hardware#/media/File:Cable_Mini_USB.jpg)). This cable needs to be capable of data transfer, not just power.
5. The latest [USB-ODE Circle Release]([url](https://github.com/danifunker/usbode-circle/releases)).
6. A target computer with a USB port that will be utilizing USBODE.

## Notes about versions
- The Stable version of this project is available under the [Main branch](https://github.com/danifunker/usbode-circle/tree/main). This project also has a [pipeline](https://github.com/danifunker/usbode-circle/actions) set up to facilitate rapid deployment of new features. The pipeline builds are cutting edge and are not guaranteed to be stable.
- If you’re using USB-ODE 1.99 or before, you’ll also need the [Raspberry Pi Imager](https://www.raspberrypi.com/software/) application. That project and its instructions are located [here](https://github.com/danifunker/usbode/releases).

# Recommendations
Version 2.0 and above support CD Audio. To take advantage of this, you’ll need a DAC. We suggest using the [Pirate Audio Line Out/LCD HAT](https://shop.pimoroni.com/products/pirate-audio-line-out), because it provides both a DAC and an LCD, making it easy to switch between different images.

## USBODE Initial Setup
1. Plug the Micro SD card into the setup computer, and format it using FAT32. All defaults should be fine, but keep in mind Windows 10 and below’s arbitrary limitation on FAT32 partition sizes addressed in the Requirements section above.
2. Open the USBODE ZIP file that was downloaded previously. Inside that ZIP file is a folder named after the USBODE release version; navigate into that folder, and extract the files within to the root of the Micro SD card.
3. On the Micro SD card, open the file labeled “wpa_supplicant.conf”.
4. Under `country=GB`, replace “GB” with the two digit code for your country if needed. Different countries use different WiFi frequencies; if you are in the US, the device will not connect to US wifi unless you change this line to “country=US”.
5. Under `ssid=”MySSID”`, type in the name of your WiFi network between the quotes. Keep in mind that the Pi supports only 2.4 GHz wifi signals.
6. Under `psk=”WirelessPassword”`, type in your WiFi’s password between the quotes.
7. The other lines in this file likely don’t need to be changed. However, if your network uses different key management or other security configurations, this file can be modified to comply with those settings.
8. If desired, image files can now be copied into the Images folder on the root of the SD card.
9. Eject the Micro SD card from the setup computer and put it into the Pi.

Setup is now complete. See the instructions below to learn how to use the USBODE. If you have any difficulties, help is available on [Discord](https://discord.gg/na2qNrvdFY).

## Using USBODE on the target computer
1. Connect the Pi Zero to the target computer. The USB cable needs to be plugged into the Pi’s inner USB port, labeled “USB”, not the one labeled “PWR” or “PWR IN”.
2. The Pi should immediately turn on. It will then boot into the Circle environment, which takes about 7 seconds on a Pi Zero W 2 and an A1-class card. If the target computer is set up to use USB devices, it should quickly see the Pi as an optical drive. Drivers may need to be installed under Windows 98, such as [nUSB](https://www.philscomputerlab.com/windows-98-usb-storage-driver.html). DOS requires a driver as well, such as [SHSUCDX](http://adoxa.altervista.org/shsucdx/). If the “wpa_supplicant.conf” file is configured correctly, it should connect to your wifi network automatically, within 10 to 15 seconds of startup.

## Using USBODE with a hat
1. Plug the hat onto the Pi.
2. If you’re using the Pirate Audio hat, there is no further configuration needed. If you’re using the Waveshare OLED hat, plug the Pi’s Micro SD card into the setup computer. Open config.txt. Under `[usbode]`, note the line reading `“displayhat=pirateaudiolineout”`. Change this to `“displayhat=waveshare”`.
3. Plug the SD card into the Pi, and the Pi into the target computer. The screen on the hat should turn on.
4. For the Pirate Audio HATt: The display will turn on a second or two after plugging in the Pi. The display will say “Not connected yet” until it connects to your wifi network, then that line will display the device’s IP address. The bottom of the display denotes what the buttons do.
5. For the Waveshare HAT: A description is coming soon.

## Copying Images onto USBODE
USBODE stores images on the MicroSD card in a folder labeled Images. You'll need to put .ISO and .BIN/.CUE files directly into this folder. This can be done by connecting the SD card to the setup computer and copying files, or by connecting to the Pi via FTP. Mounting the card to the setup computer is the fastest method by a significant margin.

Note: .BIN/.CUE images with multiple .BIN files do not yet work correctly. There is a workaround, however: [CDFix](https://web.archive.org/web/20240112090553/https://krikzz.com/pub/support/mega-everdrive/pro-series/cdfix/) will merge all of the .BIN files into one. Remember to make a backup of the image files before running this utility.

## Using the USBODE Browser Interface
The browser interface is used to load images and shut down the device. To access the interface, you’ll need the IP address of the Pi. Once it can connect to your WiFi network, this address can be viewed from your router’s configuration page. It should appear as “CDROM” in the list of connected devices. If you use a display HAT, the display will also show the IP address. Use that IP address preceded by “http://” (not “https://”). For example, if your Pi’s IP address is 192.168.0.4, you would enter “http://192.168.0.4” into your browser’s address bar.

### Loading an Image via the Browser Interface:
The Web Interface displays a list of available images, and displays “(Current)” after the image that is currently loaded. Images can be switched at any time by clicking on them.

### Shutting Down USBODE:
On the USBODE homepage, click _Shutdown USBODE_. The LED indicator on the Pi will flash for several seconds and eventually turn off.

## Other notes
- Do not delete “image.iso” in the Images folder. It is required.

## Discord Server
For updates on this project please visit the discord server here: (https://discord.gg/na2qNrvdFY)

Feel free to contribute to the project.

This project is also featured on video on [PhilsComputerLab](https://www.youtube.com/channel/UCj9IJ2QvygoBJKSOnUgXIRA)!
Here is his [first video](https://www.youtube.com/watch?v=Is3ULD0ZXnI).

Please like and subscribe to Phil so you can stay up to date on this project and many other cool retro computing things!

## Donations
Support me on ko-fi!
(https://ko-fi.com/danifunker)

Readme updated by [Zarf](https://github.com/Zarf-42) and wayneknight_rider.
