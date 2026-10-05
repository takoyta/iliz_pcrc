#pragma once
#include <windows.h>

#define WM_APP_INFO  (WM_APP + 1)
#define WM_APP_FLASH (WM_APP + 2)
#define WM_TRAY_ICON (WM_APP + 3)

extern HWND g_ui_wnd;
extern HWND g_ui_settings;
extern HINSTANCE g_ui_inst;
extern HFONT g_ui_font;
extern int g_ui_dpi;

int UiScale(int v);
void UiRefreshButtons(HWND wnd);
void UiCenter(HWND wnd, int client_w, int client_h, DWORD style);
void UiShowEditButton(HWND parent, int index);
void UiRegisterEditClass(void);
void UiInitTheme(void);
void UiApplyFont(HWND wnd);
void UiApplyWindowTheme(HWND wnd);
void UiSyncTheme(void);
void UiOnDpiChanged(HWND wnd, WPARAM wp, LPARAM lp);
LRESULT UiCtlColor(HDC dc, HWND child);
BOOL UiEraseBg(HWND wnd, HDC dc);
HWND UiMakePush(HWND parent, const wchar_t *text, int id, BOOL primary, BOOL visible);
void UiFrame(HWND wnd);
