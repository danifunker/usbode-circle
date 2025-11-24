@ECHO OFF
REM Check for a USBODE and mount an image by name.
REM
REM Prerequisites:
REM - scsitb.exe (escsitoolbox)
REM - sed.exe (GNU sed)
REM Environment variables:
REM - CDROM is set to a drive letter (e.g. E:)
REM - TEMP is set to the path of a temporary directory (e.g. C:\TEMP)

IF "%1"=="" GOTO :usage

SET ADDR=
SET IMAGE_NAME=%1
SET IMAGE_ID=

REM Parse the device address (ADDR) from the output
REM The address is in format X:X:X at the beginning of the line
ECHO Looking for USBODE...
scsitb.exe info | sed.exe -n "/USBODE/ s/^\([0-9]\+:[0-9]\+:[0-9]\+\) .*/SET ADDR=\1/p" > %TEMP%\setaddr.bat
CALL %TEMP%\setaddr.bat
DEL %TEMP%\setaddr.bat > NUL

IF "%ADDR%"=="" GOTO :nousbode

ECHO USBODE found at %ADDR%

REM Search for the image name in the listing and parse the image ID
ECHO Searching for image named %IMAGE_NAME%...
scsitb.exe lsimg %ADDR% | sed -n "/%IMAGE_NAME%/p" | sed -n "{s/^\([0-9]\+\) .*/SET IMAGE_ID=\1/;p;q}" > %TEMP%\setimgid.bat
CALL %TEMP%\setimgid.bat
DEL %TEMP%\setimgid.bat > NUL

IF "%IMAGE_ID%"=="" GOTO :noimage
   
ECHO Mounting image ID %IMAGE_ID%...
scsitb.exe setimg %ADDR% %IMAGE_ID%

CALL cdready.bat %CDROM%
VOL %CDROM%

GOTO :end

:usage
ECHO Usage: %0 "Image name"
GOTO :end

:nousbode
ECHO Error: USBODE not found!
GOTO :end

:noimage
ECHO Error: Image %IMAGE_NAME% not found on device %ADDR%!

:end
