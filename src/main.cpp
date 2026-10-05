#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "config.h"
#include "debug.h"
#include "settings.h"
#include "ui.h"
#include "ui_internal.h"

#undef WinMain
extern "C" int __stdcall WinMain(HINSTANCE, HINSTANCE, char *, int)
{
    DebugInit();
    DebugLog("app", "start");
    HWND prev = FindWindowW(L"IlizPcrc", 0);
    if (prev) {
        DebugLog("app", "already running, activate existing");
        PostMessageW(prev, WM_TRAY_ICON, 0, WM_LBUTTONDBLCLK);
        return 0;
    }
    SettingsLoad();
    ConfigInit();
    ConfigLoad();
    BOOL fresh = SettingsEnsureToken();
    if (fresh)
        DebugLog("app", "fresh token generated");
    return UiRun(fresh);
}
