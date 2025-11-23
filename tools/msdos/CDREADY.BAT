@ECHO OFF
REM Wait for media in the specified CD drive to become ready.

SET WAIT_SECONDS=5

IF "%1"=="" GOTO :usage

:retry
ECHO Waiting for %WAIT_SECONDS% seconds for media in drive %1 to become ready...
CHOICE /C:A /N /T:A,%WAIT_SECONDS% > NUL
CDREADY.COM %1
IF NOT ERRORLEVEL 3 GOTO end
GOTO :retry

:usage
ECHO Usage: %0 Drive

:end
