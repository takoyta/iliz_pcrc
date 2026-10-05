@echo off
setlocal
set ROOT=%~dp0
if not defined VSINSTALL set VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist "%ROOT%bin" mkdir "%ROOT%bin"
if not exist "%ROOT%bin\config.json" copy /y "%ROOT%defaults\config.json" "%ROOT%bin\config.json" >nul
cd /d "%ROOT%bin"

rc /nologo /I"%ROOT%src" /foiliz_pcrc.res "%ROOT%src\app.rc" || exit /b 1

cl /nologo /std:c++17 /MT /O1 /Os /GS- /Gy /GR- /EHs-c- /W3 /DUNICODE /D_UNICODE /DCJSON_HIDE_SYMBOLS ^
   /I"%ROOT%src" /I"%ROOT%third_party\cjson" /Fe:iliz_pcrc.exe ^
   "%ROOT%src\main.cpp" "%ROOT%src\ui.cpp" "%ROOT%src\ui_button.cpp" ^
   "%ROOT%src\backend.cpp" "%ROOT%src\iliz.cpp" "%ROOT%src\own.cpp" ^
   "%ROOT%src\settings.cpp" "%ROOT%src\core.cpp" "%ROOT%src\shot.cpp" ^
   "%ROOT%src\httpx.cpp" "%ROOT%src\config.cpp" "%ROOT%src\cmd.cpp" "%ROOT%src\mqtt.cpp" "%ROOT%src\audio.cpp" ^
   "%ROOT%src\debug.cpp" ^
   "%ROOT%third_party\cjson\cJSON.c" ^
   iliz_pcrc.res ^
   /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MANIFEST:NO ^
   kernel32.lib user32.lib gdi32.lib advapi32.lib shell32.lib comctl32.lib ole32.lib ^
   dwmapi.lib uxtheme.lib ^
   powrprof.lib ws2_32.lib winhttp.lib crypt32.lib || exit /b 1

del /q *.obj *.res *.lib *.exp 2>nul
echo Built: %ROOT%bin\iliz_pcrc.exe
