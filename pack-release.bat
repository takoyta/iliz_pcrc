@echo off
setlocal EnableDelayedExpansion
set ROOT=%~dp0

call "%ROOT%build.bat" || exit /b 1

if not exist "%ROOT%bin\iliz_pcrc.exe" (
  echo Missing bin\iliz_pcrc.exe
  exit /b 1
)
if not exist "%ROOT%bin\bass.dll" (
  echo Missing bin\bass.dll — put bass.dll next to the exe in bin\
  exit /b 1
)
if not exist "%ROOT%bin\bassopus.dll" (
  echo Missing bin\bassopus.dll — put bassopus.dll next to the exe in bin\
  exit /b 1
)
if not exist "%ROOT%defaults\config.json" (
  echo Missing defaults\config.json
  exit /b 1
)

set VER=
for /f "tokens=3 delims= " %%a in ('findstr /C:"#define APP_VERSION_A" "%ROOT%src\appinfo.h"') do set VER=%%~a
set VER=!VER:"=!
if "!VER!"=="" (
  echo Could not read APP_VERSION_A from src\appinfo.h
  exit /b 1
)

set OUT=%ROOT%dist
if not exist "%OUT%" mkdir "%OUT%"
set NAME=iliz_pcrc-!VER!
set STAGE=%OUT%\%NAME%
set ZIP=%OUT%\%NAME%.zip

if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%ZIP%" del /q "%ZIP%"
mkdir "%STAGE%"
copy /y "%ROOT%bin\iliz_pcrc.exe" "%STAGE%\" >nul
copy /y "%ROOT%bin\bass.dll" "%STAGE%\" >nul
copy /y "%ROOT%bin\bassopus.dll" "%STAGE%\" >nul
copy /y "%ROOT%defaults\config.json" "%STAGE%\config.json" >nul
if exist "%ROOT%bin\beep.mp3" copy /y "%ROOT%bin\beep.mp3" "%STAGE%\" >nul

powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ZIP%' -Force" || exit /b 1
rmdir /s /q "%STAGE%"

echo Packed: %ZIP%
