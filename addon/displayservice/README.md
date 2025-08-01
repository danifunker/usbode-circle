# Supported Displays

## Pirate Audio Hat
This board is a plug and play colour ST7789 240x240 IPS LCD screen and a PCM5100A DAC for line out audio.

Your config.txt should have the following setting under `[usbode]` section

```
[usbode]
displayhat=pirateaudiolineout
```

## Waveshare 1.3" OLED Hat
This board is a plug and play monochrome SH1106 128x64 screen. To use this, your config.txt should have the following setting under the `usbode` section

```
[usbode]
displayhat=waveshare
```

## ST7789A
If you've bought a standalone ST7789 SPI board, you can manually wire it to the Raspberry Pi and configure it under the `[st7789]` section in your config.txt. Default values are shown below and are based on the Pirate Audio wiring. If you wire it using these default settings, you don't need to specify them in the config file.

Note, GPIO 10 and 11 must be used for SPI MOSI and SCLK. The other pins can be configured as below

```
[st7789]
dc_pin=22
reset_pin=27
backlight_pin=13
spi_cpol=1
spi_chpa=1
spi_clock_speed=80000000
spi_chip_select=0
```

In addition, four buttons can be mapped as follows. The default have been chosen to coincide with the Pirate Audio mappings but can be arbitrarily remapped to any GPIO according to the wiring. These should be configured under the same `[st7789]` section in config.txt

```
button_up=5
button_down=6
button_ok=24
button_cancel=16
```

## SH1106
If you've bought a standalone SH1106 SPI board, you can manually wire it to the Raspberry Pi and configure it under the `[sh1106]` section in your config.txt. Default values are shown below and are also based on the Pirate Audio settings. In fact, the pinout of the SH1106 SPI boards is mostly the same as the ST7789 boards and you can swap them over with no rewiring. If you wire it using these default settings, you don't need to specify them in the config file.

Note, GPIO 10 and 11 must be used for SPI MOSI and SCLK. The other pins can be configured as below

```
[sh1106]
dc_pin=22
reset_pin=27
backlight_pin=0
spi_cpol=0
spi_chpa=0
spi_clock_speed=24000000
spi_chip_select=1
```

In addition, four buttons can be mapped as follows. The default have been chosen to coincide with the Waveshare mappings but can be arbitrarily remapped to any GPIO according to the wiring. These should be configured under the same `[sh1106]` section in config.txt

```
button_up=6
button_down=19
button_ok=21
button_cancel=20
```
