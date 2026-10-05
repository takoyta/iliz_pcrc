#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <string.h>
#include "appinfo.h"
#include "backend.h"
#include "config.h"
#include "iliz.h"
#include "settings.h"
#include "text.h"
#include "ui.h"
#include "ui_internal.h"

#define WND_CLASS      L"IlizPcrc"
#define SETTINGS_CLASS L"IlizPcrcSettings"
#define TIMER_FLASH 1
#define FLASH_MS    300

#define IDM_SETTINGS  1001
#define IDM_ABOUT     1002
#define IDM_EXIT      1003
#define IDM_OPEN_CFG  1004
#define IDM_OPEN_PAGE 1005
#define IDM_TELEGRAM  1006

#define IDC_PAGE0      2301
#define IDC_PAGE1      2302
#define IDC_PAGE2      2303
#define IDC_PAGE3      2304
#define IDC_ILIZ_ON    2320
#define IDC_OWN_ON     2321
#define IDC_LBL_TOKEN  2201
#define IDC_TOKEN      2001
#define IDC_COPY_TOK   2002
#define IDC_NEW_TOK    2003
#define IDC_LBL_SERVER 2203
#define IDC_SERVER     2006
#define IDC_LBL_BOT    2401
#define IDC_BOT        2402
#define IDC_SHOW_BOT   2403
#define IDC_LBL_ALLOW  2404
#define IDC_ALLOW      2405
#define IDC_AUTOSTART  2012
#define IDC_LIST       2013
#define IDC_ADD        2014
#define IDC_EDIT_BTN   2015
#define IDC_DEL        2016
#define IDC_UP         2019
#define IDC_DOWN       2020
#define IDC_STATUS     2017
#define IDC_SAVE       2018

#define SET_STYLE (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX)
#define SET_W 560
#define SET_H 720

#define PAGE_MAIN 0
#define PAGE_ILIZ 1
#define PAGE_OWN  2
#define PAGE_BTNS 3

static const int kMainIds[] = {
    IDC_AUTOSTART
};
static const int kIlizIds[] = {
    IDC_ILIZ_ON, IDC_LBL_TOKEN, IDC_TOKEN, IDC_COPY_TOK, IDC_NEW_TOK,
    IDC_LBL_SERVER, IDC_SERVER
};
static const int kOwnIds[] = {
    IDC_OWN_ON, IDC_LBL_BOT, IDC_BOT, IDC_SHOW_BOT, IDC_LBL_ALLOW, IDC_ALLOW
};
static const int kBtnIds[] = {
    IDC_LIST, IDC_ADD, IDC_EDIT_BTN, IDC_DEL, IDC_UP, IDC_DOWN
};

HWND g_ui_wnd;
HWND g_ui_settings;
HINSTANCE g_ui_inst;
HFONT g_ui_font;
int g_ui_dpi = 96;

static NOTIFYICONDATAW g_nid;
static char g_iliz_status[96];
static char g_own_status[96];
static BOOL g_iliz_online;
static BOOL g_own_online;
static BOOL g_flash;
static BOOL g_show_bot;
static HICON g_icon_on;
static HICON g_icon_off;
static HICON g_icon_msg;
static ButtonDef g_list_items[CFG_MAX_BUTTONS];

int UiScale(int v)
{
    return MulDiv(v, g_ui_dpi, 96);
}

static int S(int v)
{
    return UiScale(v);
}

#define DWMWA_USE_IMMERSIVE_DARK_MODE_ 20
#define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD_ 19

static BOOL g_dark;
static BOOL g_theme_ready;
static int g_page;
static int g_hot;
static HBRUSH g_bg_brush;
static HBRUSH g_edit_brush;

typedef BOOL (WINAPI *AllowDarkWindowFn)(HWND, BOOL);
typedef int (WINAPI *SetPreferredAppModeFn)(int);
typedef void (WINAPI *FlushMenuThemesFn)(void);
typedef BOOL (WINAPI *SpiForDpiFn)(UINT, UINT, PVOID, UINT, UINT);
typedef BOOL (WINAPI *AdjustRectForDpiFn)(LPRECT, DWORD, BOOL, DWORD, UINT);
typedef UINT (WINAPI *GetDpiForSystemFn)(void);

static AllowDarkWindowFn g_allow_dark_window;
static FlushMenuThemesFn g_flush_menu_themes;
static SpiForDpiFn g_spi_for_dpi;
static AdjustRectForDpiFn g_adjust_for_dpi;

static BOOL CALLBACK SetChildFont(HWND child, LPARAM font)
{
    LONG style = GetWindowLongW(child, GWL_STYLE);
    SetWindowLongW(child, GWL_STYLE, style | WS_CLIPSIBLINGS);
    SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
    return TRUE;
}

void UiApplyFont(HWND wnd)
{
    if (wnd && g_ui_font)
        EnumChildWindows(wnd, SetChildFont, (LPARAM)g_ui_font);
}

static BOOL CALLBACK FontTopLevel(HWND wnd, LPARAM)
{
    wchar_t cls[64];
    if (!GetClassNameW(wnd, cls, 64))
        return TRUE;
    if (lstrcmpW(cls, SETTINGS_CLASS) == 0 || lstrcmpW(cls, L"IlizPcrcEditBtn") == 0)
        UiApplyFont(wnd);
    return TRUE;
}

static HFONT MessageFont(int dpi)
{
    NONCLIENTMETRICSW ncm;
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    BOOL ok = FALSE;
    if (g_spi_for_dpi)
        ok = g_spi_for_dpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, (UINT)dpi);
    if (!ok)
        ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    if (ok)
        return CreateFontIndirectW(&ncm.lfMessageFont);
    return CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void RebuildFont(int dpi)
{
    HFONT next = MessageFont(dpi);
    HFONT prev = g_ui_font;
    g_ui_font = next ? next : prev;
    if (next) {
        EnumThreadWindows(GetCurrentThreadId(), FontTopLevel, 0);
        if (prev)
            DeleteObject(prev);
    }
}

static void RebuildBrushes(void)
{
    if (g_bg_brush)
        DeleteObject(g_bg_brush);
    if (g_edit_brush)
        DeleteObject(g_edit_brush);
    COLORREF bg = g_dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE);
    COLORREF edit = g_dark ? RGB(45, 45, 45) : GetSysColor(COLOR_WINDOW);
    g_bg_brush = CreateSolidBrush(bg);
    g_edit_brush = CreateSolidBrush(edit);
}

static BOOL QueryDark(void)
{
    HIGHCONTRASTW hc;
    memset(&hc, 0, sizeof(hc));
    hc.cbSize = sizeof(hc);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON))
        return FALSE;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) != ERROR_SUCCESS)
        return FALSE;
    DWORD light = 1;
    DWORD cb = sizeof(light);
    RegQueryValueExW(key, L"AppsUseLightTheme", 0, 0, (BYTE *)&light, &cb);
    RegCloseKey(key);
    return light == 0;
}

static BOOL CALLBACK ThemeChild(HWND child, LPARAM)
{
    wchar_t cls[32];
    if (!GetClassNameW(child, cls, 32))
        return TRUE;
    if (g_allow_dark_window)
        g_allow_dark_window(child, g_dark);
    if (lstrcmpiW(cls, L"Button") == 0) {
        SetWindowTheme(child, g_dark ? L"DarkMode_Explorer" : NULL, NULL);
    } else if (lstrcmpiW(cls, L"Edit") == 0) {
        SetWindowTheme(child, g_dark ? L"DarkMode_CFD" : NULL, NULL);
    } else if (lstrcmpiW(cls, L"ComboBox") == 0) {
        SetWindowTheme(child, g_dark ? L"DarkMode_CFD" : NULL, NULL);
    } else if (lstrcmpiW(cls, L"SysListView32") == 0) {
        SetWindowTheme(child, g_dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
        COLORREF text = g_dark ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWTEXT);
        COLORREF bg = g_dark ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW);
        ListView_SetBkColor(child, bg);
        ListView_SetTextBkColor(child, bg);
        ListView_SetTextColor(child, text);
    }
    return TRUE;
}

static BOOL CALLBACK ThemeTopLevel(HWND wnd, LPARAM)
{
    wchar_t cls[64];
    if (!GetClassNameW(wnd, cls, 64))
        return TRUE;
    if (lstrcmpW(cls, SETTINGS_CLASS) == 0 || lstrcmpW(cls, L"IlizPcrcEditBtn") == 0)
        UiApplyWindowTheme(wnd);
    return TRUE;
}

void UiApplyWindowTheme(HWND wnd)
{
    if (!wnd)
        return;
    if (g_allow_dark_window)
        g_allow_dark_window(wnd, g_dark);
    BOOL dark = g_dark;
    if (DwmSetWindowAttribute(wnd, DWMWA_USE_IMMERSIVE_DARK_MODE_, &dark, sizeof(dark)) != S_OK)
        DwmSetWindowAttribute(wnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD_, &dark, sizeof(dark));
    EnumChildWindows(wnd, ThemeChild, 0);
    RedrawWindow(wnd, 0, 0, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

void UiSyncTheme(void)
{
    BOOL dark = QueryDark();
    if (g_theme_ready && dark == g_dark)
        return;
    g_dark = dark;
    g_theme_ready = TRUE;
    RebuildBrushes();
    if (g_flush_menu_themes)
        g_flush_menu_themes();
    EnumThreadWindows(GetCurrentThreadId(), ThemeTopLevel, 0);
}

void UiInitTheme(void)
{
    HMODULE user = GetModuleHandleW(L"user32.dll");
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    g_spi_for_dpi = (SpiForDpiFn)GetProcAddress(user, "SystemParametersInfoForDpi");
    g_adjust_for_dpi = (AdjustRectForDpiFn)GetProcAddress(user, "AdjustWindowRectExForDpi");
    if (ux) {
        SetPreferredAppModeFn pref = (SetPreferredAppModeFn)GetProcAddress(ux, (LPCSTR)135);
        g_allow_dark_window = (AllowDarkWindowFn)GetProcAddress(ux, (LPCSTR)133);
        g_flush_menu_themes = (FlushMenuThemesFn)GetProcAddress(ux, (LPCSTR)136);
        if (pref)
            pref(1);
    }
    GetDpiForSystemFn get_dpi = (GetDpiForSystemFn)GetProcAddress(user, "GetDpiForSystem");
    if (get_dpi)
        g_ui_dpi = (int)get_dpi();
    if (g_ui_dpi < 96)
        g_ui_dpi = 96;
    RebuildFont(g_ui_dpi);
    UiSyncTheme();
}

void UiOnDpiChanged(HWND wnd, WPARAM wp, LPARAM lp)
{
    int dpi = HIWORD(wp);
    if (dpi >= 96 && dpi != g_ui_dpi) {
        g_ui_dpi = dpi;
        RebuildFont(dpi);
    }
    RECT *rc = (RECT *)lp;
    if (wnd && rc)
        SetWindowPos(wnd, 0, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
}

BOOL UiEraseBg(HWND wnd, HDC dc)
{
    if (!g_dark || !g_bg_brush || !wnd || !dc)
        return FALSE;
    RECT rc;
    GetClientRect(wnd, &rc);
    FillRect(dc, &rc, g_bg_brush);
    return TRUE;
}

LRESULT UiCtlColor(HDC dc, HWND child)
{
    if (!g_dark || !dc || !child || !g_bg_brush)
        return 0;
    wchar_t cls[32];
    if (!GetClassNameW(child, cls, 32))
        return 0;
    BOOL edit = lstrcmpiW(cls, L"Edit") == 0 || lstrcmpiW(cls, L"ComboBox") == 0 ||
                lstrcmpiW(cls, L"ComboLBox") == 0;
    COLORREF text = RGB(255, 255, 255);
    if (!IsWindowEnabled(child))
        text = RGB(160, 160, 160);
    SetTextColor(dc, text);
    SetBkColor(dc, edit ? RGB(45, 45, 45) : RGB(32, 32, 32));
    SetBkMode(dc, edit ? OPAQUE : TRANSPARENT);
    return (LRESULT)(edit ? g_edit_brush : g_bg_brush);
}

static void ComposeStatus(char *out, int cap, BOOL *online)
{
    const AppSettings *s = Settings();
    out[0] = 0;
    BOOL on = TRUE;
    int parts = 0;
    if (s->iliz_on) {
        AppendA(out, cap, "Iliz: ");
        AppendA(out, cap, g_iliz_status[0] ? g_iliz_status : "...");
        on = g_iliz_online;
        ++parts;
    }
    if (s->own_on) {
        if (out[0])
            AppendA(out, cap, "   ");
        AppendA(out, cap, "Bot: ");
        AppendA(out, cap, g_own_status[0] ? g_own_status : "...");
        on = parts ? (on && g_own_online) : g_own_online;
        ++parts;
    }
    if (!parts) {
        CopyA(out, "Off", cap);
        on = FALSE;
    }
    *online = on;
}

void AppSetIlizStatus(const char *text, BOOL online)
{
    CopyA(g_iliz_status, text ? text : "", sizeof(g_iliz_status));
    g_iliz_online = online;
    if (g_ui_wnd)
        PostMessageW(g_ui_wnd, WM_APP_INFO, 0, 0);
}

void AppSetOwnStatus(const char *text, BOOL online)
{
    CopyA(g_own_status, text ? text : "", sizeof(g_own_status));
    g_own_online = online;
    if (g_ui_wnd)
        PostMessageW(g_ui_wnd, WM_APP_INFO, 0, 0);
}

void AppFlash(void)
{
    if (g_ui_wnd)
        PostMessageW(g_ui_wnd, WM_APP_FLASH, 0, 0);
}

static void UpdateTray(void)
{
    char status[160];
    BOOL online = FALSE;
    ComposeStatus(status, sizeof(status), &online);
    HICON icon = g_icon_off;
    if (g_flash && g_icon_msg)
        icon = g_icon_msg;
    else if (online && g_icon_on)
        icon = g_icon_on;
    wchar_t tip[128];
    const wchar_t *name = APP_NAME;
    int i = 0;
    while (name[i] && i < 40) {
        tip[i] = name[i];
        ++i;
    }
    tip[i++] = L'\n';
    MultiByteToWideChar(CP_UTF8, 0, status, -1, tip + i, (int)(128 - i));
    tip[127] = 0;
    CopyW(g_nid.szTip, tip, 128);
    g_nid.hIcon = icon;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void RefreshStatus(HWND wnd)
{
    if (!wnd)
        return;
    char status[160];
    BOOL online = FALSE;
    ComposeStatus(status, sizeof(status), &online);
    wchar_t buf[160];
    MultiByteToWideChar(CP_UTF8, 0, status, -1, buf, 160);
    SetDlgItemTextW(wnd, IDC_STATUS, buf);
}

static void ShowId(HWND wnd, int id, BOOL on)
{
    HWND c = GetDlgItem(wnd, id);
    if (c)
        ShowWindow(c, on ? SW_SHOW : SW_HIDE);
}

static int PageOf(HWND)
{
    if (g_page < PAGE_MAIN || g_page > PAGE_BTNS)
        g_page = PAGE_MAIN;
    return g_page;
}

static void ShowIds(HWND wnd, const int *ids, int n, BOOL on)
{
    for (int i = 0; i < n; ++i)
        ShowId(wnd, ids[i], on);
}

static void ShowPage(HWND wnd, int page)
{
    if (page < PAGE_MAIN || page > PAGE_BTNS)
        page = PAGE_MAIN;
    ShowIds(wnd, kMainIds, (int)(sizeof(kMainIds) / sizeof(kMainIds[0])), page == PAGE_MAIN);
    ShowIds(wnd, kIlizIds, (int)(sizeof(kIlizIds) / sizeof(kIlizIds[0])), page == PAGE_ILIZ);
    ShowIds(wnd, kOwnIds, (int)(sizeof(kOwnIds) / sizeof(kOwnIds[0])), page == PAGE_OWN);
    ShowIds(wnd, kBtnIds, (int)(sizeof(kBtnIds) / sizeof(kBtnIds[0])), page == PAGE_BTNS);
}

static void Lay(HWND wnd, int id, int x, int y, int w, int h)
{
    HWND c = GetDlgItem(wnd, id);
    if (c)
        MoveWindow(c, x, y, w, h, TRUE);
}

static void LayoutSettings(HWND wnd)
{
    RECT rc;
    GetClientRect(wnd, &rc);
    int pad = S(12);
    int gap = S(10);
    int w = rc.right - pad * 2;
    int y = pad;
    int lh = S(22);
    int eh = S(26);
    int bh = S(28);
    int page = PageOf(wnd);
    int bottom = rc.bottom - pad - bh;
    int status_y = bottom - gap - lh;

    int tw = w / 4;
    int th = S(34);
    for (int i = 0; i < 4; ++i) {
        int bw = (i == 3) ? (w - tw * 3) : tw;
        Lay(wnd, IDC_PAGE0 + i, pad + tw * i, y, bw, th);
    }
    y += th + gap;

    if (page == PAGE_MAIN) {
        Lay(wnd, IDC_AUTOSTART, pad, y, S(220), lh);
    } else if (page == PAGE_ILIZ) {
        Lay(wnd, IDC_ILIZ_ON, pad, y, w, lh);
        y += lh + gap;
        Lay(wnd, IDC_LBL_TOKEN, pad, y, w, lh);
        y += lh + S(4);
        Lay(wnd, IDC_TOKEN, pad, y, w, eh);
        y += eh + gap;
        Lay(wnd, IDC_COPY_TOK, pad, y, S(110), bh);
        Lay(wnd, IDC_NEW_TOK, pad + S(118), y, S(110), bh);
        y += bh + gap;
        Lay(wnd, IDC_LBL_SERVER, pad, y, w, lh);
        y += lh + S(4);
        Lay(wnd, IDC_SERVER, pad, y, w, eh);
    } else if (page == PAGE_OWN) {
        Lay(wnd, IDC_OWN_ON, pad, y, w, lh);
        y += lh + gap;
        Lay(wnd, IDC_LBL_BOT, pad, y, w, lh);
        y += lh + S(4);
        Lay(wnd, IDC_BOT, pad, y, w, eh);
        y += eh + gap;
        Lay(wnd, IDC_SHOW_BOT, pad, y, S(160), lh);
        y += lh + gap;
        Lay(wnd, IDC_LBL_ALLOW, pad, y, w, lh);
        y += lh + S(4);
        Lay(wnd, IDC_ALLOW, pad, y, w, eh);
    } else {
        int list_h = status_y - gap - y;
        if (list_h < S(80))
            list_h = S(80);
        Lay(wnd, IDC_LIST, pad, y, w, list_h);
        Lay(wnd, IDC_ADD, pad, bottom, S(64), bh);
        Lay(wnd, IDC_EDIT_BTN, pad + S(70), bottom, S(64), bh);
        Lay(wnd, IDC_DEL, pad + S(140), bottom, S(64), bh);
        Lay(wnd, IDC_UP, pad + S(210), bottom, S(64), bh);
        Lay(wnd, IDC_DOWN, pad + S(280), bottom, S(64), bh);
    }

    Lay(wnd, IDC_STATUS, pad, status_y, w, lh);
    Lay(wnd, IDC_SAVE, rc.right - pad - S(88), bottom, S(88), bh);
}

static void ApplyPassEcho(HWND edit, BOOL show)
{
    if (!edit)
        return;
    SendMessageW(edit, EM_SETPASSWORDCHAR, show ? 0 : (WPARAM)L'\x25CF', 0);
    InvalidateRect(edit, 0, TRUE);
}

void UiRefreshButtons(HWND wnd)
{
    HWND list = GetDlgItem(wnd, IDC_LIST);
    if (!list)
        return;
    ListView_DeleteAllItems(list);
    int n = ConfigCopyAll(g_list_items, CFG_MAX_BUTTONS);
    for (int i = 0; i < n; ++i) {
        const ButtonDef *b = &g_list_items[i];
        wchar_t row[16], label[CFG_LABEL_MAX], action[64], value[CFG_VALUE_MAX];
        char num[16];
        int r = b->row > 0 ? b->row : 1;
        PutIA(num, sizeof(num), r);
        MultiByteToWideChar(CP_UTF8, 0, num, -1, row, 16);
        MultiByteToWideChar(CP_UTF8, 0, b->label, -1, label, CFG_LABEL_MAX);
        MultiByteToWideChar(CP_UTF8, 0, b->action, -1, action, 64);
        const wchar_t *vt = ConfigValueTitle(b->action, b->value);
        CopyW(value, vt ? vt : L"", CFG_VALUE_MAX);
        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = row;
        ListView_InsertItem(list, &item);
        ListView_SetItemText(list, i, 1, label);
        ListView_SetItemText(list, i, 2, action);
        ListView_SetItemText(list, i, 3, value);
    }
}

static int SelectedButton(HWND wnd)
{
    HWND list = GetDlgItem(wnd, IDC_LIST);
    if (!list)
        return -1;
    return ListView_GetNextItem(list, -1, LVNI_SELECTED);
}

static void SelectButton(HWND wnd, int index)
{
    HWND list = GetDlgItem(wnd, IDC_LIST);
    if (!list || index < 0)
        return;
    ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, index, FALSE);
}

static void SizeButtonColumns(HWND list)
{
    if (!list)
        return;
    ListView_SetColumnWidth(list, 0, S(44));
    ListView_SetColumnWidth(list, 1, S(100));
    ListView_SetColumnWidth(list, 2, S(80));
    ListView_SetColumnWidth(list, 3, S(200));
}

static COLORREF PageBg(BOOL on, BOOL hot, BOOL down)
{
    if (g_dark) {
        if (down)
            return RGB(58, 58, 58);
        if (on)
            return RGB(48, 48, 48);
        if (hot)
            return RGB(40, 40, 40);
        return RGB(32, 32, 32);
    }
    if (down || on)
        return GetSysColor(COLOR_WINDOW);
    if (hot)
        return GetSysColor(COLOR_BTNHIGHLIGHT);
    return GetSysColor(COLOR_BTNFACE);
}

static void DrawPageBtn(const DRAWITEMSTRUCT *di)
{
    int page = di->CtlID - IDC_PAGE0;
    BOOL on = page == g_page;
    BOOL hot = di->CtlID == g_hot;
    BOOL down = (di->itemState & ODS_SELECTED) != 0;
    RECT rc = di->rcItem;
    HBRUSH bg = CreateSolidBrush(PageBg(on, hot, down));
    FillRect(di->hDC, &rc, bg);
    DeleteObject(bg);

    RECT edge = rc;
    edge.top = edge.bottom - S(2);
    COLORREF mark = RGB(60, 60, 60);
    if (!g_dark)
        mark = GetSysColor(COLOR_BTNSHADOW);
    if (on)
        mark = RGB(0, 120, 215);
    HBRUSH bar = CreateSolidBrush(mark);
    FillRect(di->hDC, &edge, bar);
    DeleteObject(bar);

    wchar_t text[64];
    GetWindowTextW(di->hwndItem, text, 64);
    SetBkMode(di->hDC, TRANSPARENT);
    COLORREF fg = RGB(255, 255, 255);
    if (g_dark && !on)
        fg = RGB(176, 176, 176);
    if (!g_dark)
        fg = GetSysColor(COLOR_WINDOWTEXT);
    SetTextColor(di->hDC, fg);
    HFONT old = g_ui_font ? (HFONT)SelectObject(di->hDC, g_ui_font) : 0;
    RECT tr = rc;
    tr.bottom -= S(2);
    DrawTextW(di->hDC, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (old)
        SelectObject(di->hDC, old);
    if (di->itemState & ODS_FOCUS) {
        RECT fr = rc;
        InflateRect(&fr, -S(3), -S(3));
        fr.bottom -= S(2);
        DrawFocusRect(di->hDC, &fr);
    }
}

static void TrackHot(HWND wnd, UINT msg)
{
    if (msg == WM_MOUSEMOVE) {
        int id = GetDlgCtrlID(wnd);
        if (g_hot != id) {
            int prev = g_hot;
            g_hot = id;
            InvalidateRect(wnd, 0, FALSE);
            if (prev)
                InvalidateRect(GetDlgItem(GetParent(wnd), prev), 0, FALSE);
            TRACKMOUSEEVENT ev;
            memset(&ev, 0, sizeof(ev));
            ev.cbSize = sizeof(ev);
            ev.dwFlags = TME_LEAVE;
            ev.hwndTrack = wnd;
            TrackMouseEvent(&ev);
        }
        return;
    }
    if (msg == WM_MOUSELEAVE && g_hot == GetDlgCtrlID(wnd)) {
        g_hot = 0;
        InvalidateRect(wnd, 0, FALSE);
    }
}

static LRESULT CALLBACK PageBtnProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    TrackHot(wnd, msg);
    return DefSubclassProc(wnd, msg, wp, lp);
}

static COLORREF PushEdge(BOOL primary, BOOL focused)
{
    if (focused && !primary)
        return RGB(0, 120, 215);
    if (primary)
        return RGB(0, 120, 215);
    if (g_dark)
        return RGB(80, 80, 80);
    return GetSysColor(COLOR_BTNSHADOW);
}

static void PaintPush(HWND wnd, HDC dc)
{
    RECT rc;
    GetClientRect(wnd, &rc);
    LONG style = GetWindowLongW(wnd, GWL_STYLE);
    BOOL primary = (style & BS_TYPEMASK) == BS_DEFPUSHBUTTON;
    BOOL hot = GetDlgCtrlID(wnd) == g_hot;
    BOOL down = (SendMessageW(wnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
    BOOL focused = GetFocus() == wnd;
    BOOL enabled = IsWindowEnabled(wnd);
    COLORREF bg = RGB(45, 45, 45);
    COLORREF fg = RGB(240, 240, 240);
    if (!enabled) {
        bg = g_dark ? RGB(40, 40, 40) : GetSysColor(COLOR_BTNFACE);
        fg = RGB(140, 140, 140);
    } else if (primary) {
        if (down)
            bg = RGB(0, 90, 170);
        else if (hot)
            bg = RGB(26, 140, 230);
        else
            bg = RGB(0, 120, 215);
        fg = RGB(255, 255, 255);
    } else if (g_dark) {
        if (down)
            bg = RGB(64, 64, 64);
        else if (hot)
            bg = RGB(55, 55, 55);
        else
            bg = RGB(45, 45, 45);
    } else {
        if (down)
            bg = GetSysColor(COLOR_BTNFACE);
        else if (hot)
            bg = RGB(236, 236, 236);
        else
            bg = GetSysColor(COLOR_WINDOW);
        fg = GetSysColor(COLOR_WINDOWTEXT);
    }
    HBRUSH fill = CreateSolidBrush(bg);
    FillRect(dc, &rc, fill);
    DeleteObject(fill);
    HBRUSH edge = CreateSolidBrush(enabled ? PushEdge(primary, focused) : (g_dark ? RGB(55, 55, 55) : GetSysColor(COLOR_BTNSHADOW)));
    FrameRect(dc, &rc, edge);
    DeleteObject(edge);
    wchar_t text[64];
    GetWindowTextW(wnd, text, 64);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    HFONT old = g_ui_font ? (HFONT)SelectObject(dc, g_ui_font) : 0;
    DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (old)
        SelectObject(dc, old);
    if (focused && enabled) {
        RECT fr = rc;
        InflateRect(&fr, -S(3), -S(3));
        DrawFocusRect(dc, &fr);
    }
}

static LRESULT CALLBACK PushProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (msg == WM_PAINT || msg == WM_PRINT || msg == WM_PRINTCLIENT) {
        PAINTSTRUCT ps;
        HDC dc = (HDC)wp;
        if (msg == WM_PAINT)
            dc = BeginPaint(wnd, &ps);
        PaintPush(wnd, dc);
        if (msg == WM_PAINT)
            EndPaint(wnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    TrackHot(wnd, msg);
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED ||
        msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_ENABLE) {
        LRESULT r = DefSubclassProc(wnd, msg, wp, lp);
        InvalidateRect(wnd, 0, FALSE);
        return r;
    }
    return DefSubclassProc(wnd, msg, wp, lp);
}

static COLORREF FrameColor(HWND wnd)
{
    if (GetFocus() == wnd)
        return RGB(0, 120, 215);
    if (g_dark)
        return RGB(70, 70, 70);
    return RGB(180, 180, 180);
}

static LRESULT CALLBACK FrameProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (msg == WM_NCPAINT) {
        LRESULT r = DefSubclassProc(wnd, msg, wp, lp);
        HDC dc = GetWindowDC(wnd);
        if (dc) {
            RECT rc;
            GetWindowRect(wnd, &rc);
            OffsetRect(&rc, -rc.left, -rc.top);
            HBRUSH br = CreateSolidBrush(FrameColor(wnd));
            FrameRect(dc, &rc, br);
            DeleteObject(br);
            ReleaseDC(wnd, dc);
        }
        return r;
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        LRESULT r = DefSubclassProc(wnd, msg, wp, lp);
        RedrawWindow(wnd, 0, 0, RDW_FRAME | RDW_INVALIDATE);
        return r;
    }
    return DefSubclassProc(wnd, msg, wp, lp);
}

HWND UiMakePush(HWND parent, const wchar_t *text, int id, BOOL primary, BOOL visible)
{
    DWORD style = WS_CHILD | WS_TABSTOP;
    if (primary)
        style |= BS_DEFPUSHBUTTON;
    if (visible)
        style |= WS_VISIBLE;
    HWND btn = CreateWindowExW(0, L"BUTTON", text, style, 0, 0, 0, 0, parent, (HMENU)(UINT_PTR)id, g_ui_inst, 0);
    if (btn)
        SetWindowSubclass(btn, PushProc, 1, 0);
    return btn;
}

void UiFrame(HWND wnd)
{
    if (!wnd)
        return;
    LONG ex = GetWindowLongW(wnd, GWL_EXSTYLE);
    if (ex & WS_EX_CLIENTEDGE)
        SetWindowLongW(wnd, GWL_EXSTYLE, ex & ~WS_EX_CLIENTEDGE);
    LONG style = GetWindowLongW(wnd, GWL_STYLE);
    if ((style & WS_BORDER) == 0)
        SetWindowLongW(wnd, GWL_STYLE, style | WS_BORDER);
    SetWindowPos(wnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    SetWindowSubclass(wnd, FrameProc, 2, 0);
}

static void PaintHeader(HWND wnd, HDC dc)
{
    RECT rc;
    GetClientRect(wnd, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(45, 45, 45));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    HBRUSH line = CreateSolidBrush(RGB(68, 68, 68));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(230, 230, 230));
    HFONT font = (HFONT)SendMessageW(wnd, WM_GETFONT, 0, 0);
    if (!font)
        font = g_ui_font;
    HFONT old = font ? (HFONT)SelectObject(dc, font) : 0;
    int n = Header_GetItemCount(wnd);
    for (int i = 0; i < n; ++i) {
        RECT ir;
        if (!Header_GetItemRect(wnd, i, &ir))
            continue;
        wchar_t text[64];
        HDITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = HDI_TEXT;
        item.pszText = text;
        item.cchTextMax = 64;
        text[0] = 0;
        Header_GetItem(wnd, i, &item);
        RECT tr = ir;
        tr.left += S(8);
        tr.right -= S(6);
        DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT div;
        div.left = ir.right - 1;
        div.right = ir.right;
        div.top = ir.top + S(6);
        div.bottom = ir.bottom - S(6);
        FillRect(dc, &div, line);
    }
    RECT bottom = rc;
    bottom.top = bottom.bottom - 1;
    FillRect(dc, &bottom, line);
    DeleteObject(line);
    if (old)
        SelectObject(dc, old);
}

static LRESULT CALLBACK HeaderProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (!g_dark)
        return DefSubclassProc(wnd, msg, wp, lp);
    if (msg == WM_ERASEBKGND) {
        RECT rc;
        GetClientRect(wnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(45, 45, 45));
        FillRect((HDC)wp, &rc, bg);
        DeleteObject(bg);
        return 1;
    }
    if (msg == WM_PAINT || msg == WM_PRINT || msg == WM_PRINTCLIENT) {
        PAINTSTRUCT ps;
        HDC dc = (HDC)wp;
        if (msg == WM_PAINT)
            dc = BeginPaint(wnd, &ps);
        PaintHeader(wnd, dc);
        if (msg == WM_PAINT)
            EndPaint(wnd, &ps);
        return 0;
    }
    return DefSubclassProc(wnd, msg, wp, lp);
}

static void InitButtonList(HWND list)
{
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = (LPWSTR)L"Row";
    col.cx = S(44);
    ListView_InsertColumn(list, 0, &col);
    col.pszText = (LPWSTR)L"Button";
    col.cx = S(100);
    ListView_InsertColumn(list, 1, &col);
    col.pszText = (LPWSTR)L"Action";
    col.cx = S(80);
    ListView_InsertColumn(list, 2, &col);
    col.pszText = (LPWSTR)L"Value";
    col.cx = S(200);
    ListView_InsertColumn(list, 3, &col);
    HWND header = ListView_GetHeader(list);
    if (header)
        SetWindowSubclass(header, HeaderProc, 1, 0);
}

static void CopyText(HWND owner, const wchar_t *text)
{
    if (!text || !text[0] || !OpenClipboard(owner))
        return;
    int n = LenW(text);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(n + 1) * sizeof(wchar_t));
    if (!mem) {
        CloseClipboard();
        return;
    }
    wchar_t *dst = (wchar_t *)GlobalLock(mem);
    if (dst) {
        CopyW(dst, text, n + 1);
        GlobalUnlock(mem);
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem))
        GlobalFree(mem);
    CloseClipboard();
}

static void FillFromSettings(HWND wnd)
{
    const AppSettings *s = Settings();
    SetDlgItemTextW(wnd, IDC_TOKEN, s->token);
    SetDlgItemTextW(wnd, IDC_SERVER, s->server);
    SetDlgItemTextW(wnd, IDC_BOT, s->bot_token);
    SetDlgItemTextW(wnd, IDC_ALLOW, s->allowed);
    SendDlgItemMessageW(wnd, IDC_ILIZ_ON, BM_SETCHECK, s->iliz_on ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(wnd, IDC_OWN_ON, BM_SETCHECK, s->own_on ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(wnd, IDC_AUTOSTART, BM_SETCHECK, SettingsAutostart() ? BST_CHECKED : BST_UNCHECKED, 0);
    ApplyPassEcho(GetDlgItem(wnd, IDC_BOT), g_show_bot);
    ShowPage(wnd, PageOf(wnd));
    UiRefreshButtons(wnd);
    RefreshStatus(wnd);
    LayoutSettings(wnd);
}

static BOOL SaveFromUi(HWND wnd)
{
    AppSettings *s = SettingsMut();
    wchar_t token[SET_TOKEN_MAX];
    wchar_t server[SET_HOST_MAX];
    wchar_t bot[SET_BOT_MAX];
    wchar_t allowed[SET_ALLOW_MAX];
    BOOL iliz_on = SendDlgItemMessageW(wnd, IDC_ILIZ_ON, BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL own_on = SendDlgItemMessageW(wnd, IDC_OWN_ON, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetDlgItemTextW(wnd, IDC_TOKEN, token, SET_TOKEN_MAX);
    GetDlgItemTextW(wnd, IDC_SERVER, server, SET_HOST_MAX);
    GetDlgItemTextW(wnd, IDC_BOT, bot, SET_BOT_MAX);
    GetDlgItemTextW(wnd, IDC_ALLOW, allowed, SET_ALLOW_MAX);
    TrimSpaceW(token);
    TrimUrlW(server);
    TrimSpaceW(bot);
    SettingsNormalizeAllowed(allowed);
    if (!SettingsValidToken(token)) {
        MessageBoxW(wnd, L"Device token is invalid.", APP_NAME, MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    if (server[0] && !StartsW(server, L"http://") && !StartsW(server, L"https://")) {
        MessageBoxW(wnd, L"API server must start with http:// or https://", APP_NAME, MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    CopyW(s->token, token, SET_TOKEN_MAX);
    CopyW(s->server, server, SET_HOST_MAX);
    CopyW(s->bot_token, bot, SET_BOT_MAX);
    CopyW(s->allowed, allowed, SET_ALLOW_MAX);
    s->iliz_on = iliz_on;
    s->own_on = own_on;
    BOOL saved = ConfigSave();
    BackendStart();
    FillFromSettings(wnd);
    if (!saved)
        MessageBoxW(wnd, L"Couldn't save settings.", APP_NAME, MB_OK | MB_ICONWARNING);
    return saved;
}

static LRESULT CALLBACK SettingsProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        const AppSettings *s = Settings();
        static const wchar_t *kPages[] = { L"Main", L"Iliz server", L"Own bot", L"Buttons" };
        for (int i = 0; i < 4; ++i) {
            HWND page = CreateWindowExW(0, L"BUTTON", kPages[i],
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                        0, 0, 0, 0, wnd, (HMENU)(UINT_PTR)(IDC_PAGE0 + i), g_ui_inst, 0);
            if (page)
                SetWindowSubclass(page, PageBtnProc, 1, 0);
        }
        CreateWindowExW(0, L"BUTTON", L"Enabled",
                        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                        0, 0, 0, 0, wnd, (HMENU)IDC_ILIZ_ON, g_ui_inst, 0);
        CreateWindowExW(0, L"STATIC", L"Device token", WS_CHILD, 0, 0, 0, 0, wnd, (HMENU)IDC_LBL_TOKEN, g_ui_inst, 0);
        HWND token = CreateWindowExW(0, L"EDIT", s->token,
                                     WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
                                     0, 0, 0, 0, wnd, (HMENU)IDC_TOKEN, g_ui_inst, 0);
        UiFrame(token);
        UiMakePush(wnd, L"Copy token", IDC_COPY_TOK, FALSE, FALSE);
        UiMakePush(wnd, L"New token", IDC_NEW_TOK, FALSE, FALSE);
        CreateWindowExW(0, L"STATIC", L"API server", WS_CHILD, 0, 0, 0, 0, wnd, (HMENU)IDC_LBL_SERVER, g_ui_inst, 0);
        HWND server = CreateWindowExW(0, L"EDIT", s->server,
                                      WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                                      0, 0, 0, 0, wnd, (HMENU)IDC_SERVER, g_ui_inst, 0);
        UiFrame(server);
        CreateWindowExW(0, L"BUTTON", L"Enabled",
                        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                        0, 0, 0, 0, wnd, (HMENU)IDC_OWN_ON, g_ui_inst, 0);
        CreateWindowExW(0, L"STATIC", L"Bot token", WS_CHILD, 0, 0, 0, 0, wnd, (HMENU)IDC_LBL_BOT, g_ui_inst, 0);
        HWND bot = CreateWindowExW(0, L"EDIT", s->bot_token,
                                   WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                                   0, 0, 0, 0, wnd, (HMENU)IDC_BOT, g_ui_inst, 0);
        UiFrame(bot);
        CreateWindowExW(0, L"BUTTON", L"Show token", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                        0, 0, 0, 0, wnd, (HMENU)IDC_SHOW_BOT, g_ui_inst, 0);
        CreateWindowExW(0, L"STATIC", L"Allowed user IDs", WS_CHILD, 0, 0, 0, 0, wnd, (HMENU)IDC_LBL_ALLOW, g_ui_inst, 0);
        HWND allow = CreateWindowExW(0, L"EDIT", s->allowed,
                                     WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 0, 0, wnd, (HMENU)IDC_ALLOW, g_ui_inst, 0);
        UiFrame(allow);
        CreateWindowExW(0, L"BUTTON", L"Start with Windows",
                        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                        0, 0, 0, 0, wnd, (HMENU)IDC_AUTOSTART, g_ui_inst, 0);
        HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                    WS_CHILD | WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                    0, 0, 0, 0, wnd, (HMENU)IDC_LIST, g_ui_inst, 0);
        UiFrame(list);
        UiMakePush(wnd, L"Add", IDC_ADD, FALSE, FALSE);
        UiMakePush(wnd, L"Edit", IDC_EDIT_BTN, FALSE, FALSE);
        UiMakePush(wnd, L"Delete", IDC_DEL, FALSE, FALSE);
        UiMakePush(wnd, L"Up", IDC_UP, FALSE, FALSE);
        UiMakePush(wnd, L"Down", IDC_DOWN, FALSE, FALSE);
        CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, wnd, (HMENU)IDC_STATUS, g_ui_inst, 0);
        UiMakePush(wnd, L"Save", IDC_SAVE, TRUE, TRUE);
        UiApplyFont(wnd);
        UiApplyWindowTheme(wnd);
        if (token) SendMessageW(token, EM_SETLIMITTEXT, 32, 0);
        if (server) SendMessageW(server, EM_SETLIMITTEXT, SET_HOST_MAX - 1, 0);
        if (bot) SendMessageW(bot, EM_SETLIMITTEXT, SET_BOT_MAX - 1, 0);
        if (allow) SendMessageW(allow, EM_SETLIMITTEXT, SET_ALLOW_MAX - 1, 0);
        if (list) InitButtonList(list);
        FillFromSettings(wnd);
        return 0;
    }
    case WM_SIZE:
        LayoutSettings(wnd);
        return 0;
    case WM_DPICHANGED:
        UiOnDpiChanged(wnd, wp, lp);
        SizeButtonColumns(GetDlgItem(wnd, IDC_LIST));
        LayoutSettings(wnd);
        return 0;
    case WM_ERASEBKGND:
        if (UiEraseBg(wnd, (HDC)wp))
            return 1;
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        LRESULT brush = UiCtlColor((HDC)wp, (HWND)lp);
        if (brush)
            return brush;
        break;
    }
    case WM_SETTINGCHANGE:
        if (lp && !IS_INTRESOURCE(lp) && lstrcmpiW((LPCWSTR)lp, L"ImmersiveColorSet") == 0)
            UiSyncTheme();
        break;
    case WM_THEMECHANGED:
        UiSyncTheme();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = S(520);
        mm->ptMinTrackSize.y = S(560);
        return 0;
    }
    case WM_DRAWITEM:
        if (lp) {
            const DRAWITEMSTRUCT *di = (const DRAWITEMSTRUCT *)lp;
            if (di->CtlID >= IDC_PAGE0 && di->CtlID <= IDC_PAGE3) {
                DrawPageBtn(di);
                return TRUE;
            }
        }
        break;
    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR *)lp;
        if (hdr && hdr->idFrom == IDC_LIST && hdr->code == NM_DBLCLK) {
            int i = SelectedButton(wnd);
            if (i >= 0)
                UiShowEditButton(wnd, i);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) >= IDC_PAGE0 && LOWORD(wp) <= IDC_PAGE3 && HIWORD(wp) == BN_CLICKED) {
            g_page = LOWORD(wp) - IDC_PAGE0;
            ShowPage(wnd, g_page);
            LayoutSettings(wnd);
            for (int i = 0; i < 4; ++i)
                InvalidateRect(GetDlgItem(wnd, IDC_PAGE0 + i), 0, FALSE);
            return 0;
        }
        if (LOWORD(wp) == IDC_SHOW_BOT) {
            g_show_bot = SendDlgItemMessageW(wnd, IDC_SHOW_BOT, BM_GETCHECK, 0, 0) == BST_CHECKED;
            ApplyPassEcho(GetDlgItem(wnd, IDC_BOT), g_show_bot);
            return 0;
        }
        if (LOWORD(wp) == IDC_AUTOSTART) {
            BOOL on = SendDlgItemMessageW(wnd, IDC_AUTOSTART, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SettingsSetAutostart(on);
            SendDlgItemMessageW(wnd, IDC_AUTOSTART, BM_SETCHECK, SettingsAutostart() ? BST_CHECKED : BST_UNCHECKED, 0);
            return 0;
        }
        if (LOWORD(wp) == IDC_COPY_TOK) {
            wchar_t text[SET_TOKEN_MAX];
            GetDlgItemTextW(wnd, IDC_TOKEN, text, SET_TOKEN_MAX);
            CopyText(wnd, text);
            return 0;
        }
        if (LOWORD(wp) == IDC_NEW_TOK) {
            if (MessageBoxW(wnd, L"Replace the device token?\nThe old control page will stop working.",
                            APP_NAME, MB_YESNO | MB_ICONWARNING) != IDYES)
                return 0;
            wchar_t prev[SET_TOKEN_MAX];
            GetDlgItemTextW(wnd, IDC_TOKEN, prev, SET_TOKEN_MAX);
            wchar_t tok[SET_TOKEN_MAX];
            if (!SettingsNewToken(tok, SET_TOKEN_MAX)) {
                MessageBoxW(wnd, L"Couldn't generate a token.", APP_NAME, MB_OK | MB_ICONWARNING);
                return 0;
            }
            SetDlgItemTextW(wnd, IDC_TOKEN, tok);
            if (!SaveFromUi(wnd))
                SetDlgItemTextW(wnd, IDC_TOKEN, prev);
            return 0;
        }
        if (LOWORD(wp) == IDC_ADD) {
            UiShowEditButton(wnd, -1);
            return 0;
        }
        if (LOWORD(wp) == IDC_EDIT_BTN) {
            int i = SelectedButton(wnd);
            if (i >= 0)
                UiShowEditButton(wnd, i);
            return 0;
        }
        if (LOWORD(wp) == IDC_DEL) {
            int i = SelectedButton(wnd);
            if (i >= 0) {
                ConfigRemove(i);
                UiRefreshButtons(wnd);
            }
            return 0;
        }
        if (LOWORD(wp) == IDC_UP || LOWORD(wp) == IDC_DOWN) {
            int i = SelectedButton(wnd);
            int delta = LOWORD(wp) == IDC_UP ? -1 : 1;
            if (i >= 0 && ConfigMove(i, delta)) {
                UiRefreshButtons(wnd);
                SelectButton(wnd, i + delta);
            }
            return 0;
        }
        if (LOWORD(wp) == IDC_SAVE)
            SaveFromUi(wnd);
        return 0;
    case WM_CLOSE:
        ShowWindow(wnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        g_ui_settings = 0;
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void UiCenter(HWND wnd, int client_w, int client_h, DWORD style)
{
    RECT wr = { 0, 0, client_w, client_h };
    if (g_adjust_for_dpi)
        g_adjust_for_dpi(&wr, style, FALSE, 0, (UINT)g_ui_dpi);
    else
        AdjustWindowRectEx(&wr, style, FALSE, 0);
    int ww = wr.right - wr.left;
    int wh = wr.bottom - wr.top;
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    int x = 100, y = 100;
    if (GetMonitorInfoW(mon, &mi)) {
        x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - ww) / 2;
        y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - wh) / 3;
    }
    SetWindowPos(wnd, 0, x, y, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ShowSettings(void)
{
    if (!g_ui_settings) {
        g_ui_settings = CreateWindowExW(0, SETTINGS_CLASS, L"Settings", SET_STYLE,
                                        CW_USEDEFAULT, CW_USEDEFAULT, S(SET_W), S(SET_H),
                                        g_ui_wnd, 0, g_ui_inst, 0);
        if (g_ui_settings)
            UiCenter(g_ui_settings, S(SET_W), S(SET_H), SET_STYLE);
    }
    if (!g_ui_settings)
        return;
    FillFromSettings(g_ui_settings);
    ShowWindow(g_ui_settings, SW_SHOW);
    SetForegroundWindow(g_ui_settings);
}

static void ShowTrayMenu(void)
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    if (Settings()->iliz_on) {
        AppendMenuW(menu, MF_STRING, IDM_TELEGRAM, L"Open Telegram");
        AppendMenuW(menu, MF_STRING, IDM_OPEN_PAGE, L"Open control page");
    }
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings");
    AppendMenuW(menu, MF_STRING, IDM_OPEN_CFG, L"Open config.json");
    AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"About");
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
    SetForegroundWindow(g_ui_wnd);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                              pt.x, pt.y, 0, g_ui_wnd, 0);
    DestroyMenu(menu);
    PostMessageW(g_ui_wnd, WM_NULL, 0, 0);
    if (cmd == IDM_OPEN_PAGE)
        IlizOpenPage(g_ui_wnd);
    else if (cmd == IDM_TELEGRAM)
        IlizOpenTelegram(g_ui_wnd);
    else if (cmd == IDM_SETTINGS)
        ShowSettings();
    else if (cmd == IDM_OPEN_CFG) {
        wchar_t path[MAX_PATH];
        ConfigGetPath(path, MAX_PATH);
        ShellExecuteW(0, L"open", path, 0, 0, SW_SHOWNORMAL);
    } else if (cmd == IDM_ABOUT) {
        MessageBoxW(g_ui_wnd,
                    APP_NAME L"\nVersion " APP_VERSION L"\n\n"
                    L"Iliz server and your own bot can run together.",
                    APP_NAME, MB_OK | MB_ICONINFORMATION);
    } else if (cmd == IDM_EXIT) {
        PostMessageW(g_ui_wnd, WM_CLOSE, 0, 0);
    }
}

static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TRAY_ICON:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU)
            ShowTrayMenu();
        else if (lp == WM_LBUTTONDBLCLK)
            ShowSettings();
        return 0;
    case WM_APP_INFO:
        UpdateTray();
        RefreshStatus(g_ui_settings);
        return 0;
    case WM_APP_FLASH:
        g_flash = TRUE;
        UpdateTray();
        SetTimer(wnd, TIMER_FLASH, FLASH_MS, 0);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_FLASH) {
            KillTimer(wnd, TIMER_FLASH);
            g_flash = FALSE;
            UpdateTray();
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        KillTimer(wnd, TIMER_FLASH);
        BackendStop();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_ui_settings)
            DestroyWindow(g_ui_settings);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

int UiRun(BOOL show_settings)
{
    g_ui_inst = GetModuleHandleW(0);
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    UiInitTheme();

    CopyA(g_iliz_status, "Starting...", sizeof(g_iliz_status));
    g_icon_on = (HICON)LoadImageW(g_ui_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
    g_icon_off = (HICON)LoadImageW(g_ui_inst, MAKEINTRESOURCEW(IDI_APP_OFF), IMAGE_ICON, 32, 32, 0);
    g_icon_msg = (HICON)LoadImageW(g_ui_inst, MAKEINTRESOURCEW(IDI_APP_MSG), IMAGE_ICON, 32, 32, 0);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_ui_inst;
    wc.hIcon = g_icon_on;
    wc.hIconSm = g_icon_on;
    wc.hCursor = LoadCursorW(0, IDC_ARROW);
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);

    WNDCLASSEXW sc;
    memset(&sc, 0, sizeof(sc));
    sc.cbSize = sizeof(sc);
    sc.lpfnWndProc = SettingsProc;
    sc.hInstance = g_ui_inst;
    sc.hIcon = g_icon_on;
    sc.hIconSm = g_icon_on;
    sc.hCursor = LoadCursorW(0, IDC_ARROW);
    sc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    sc.lpszClassName = SETTINGS_CLASS;
    RegisterClassExW(&sc);
    UiRegisterEditClass();

    g_ui_wnd = CreateWindowExW(0, WND_CLASS, APP_NAME, WS_OVERLAPPED, 0, 0, 0, 0, 0, 0, g_ui_inst, 0);
    if (!g_ui_wnd)
        return 1;

    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_ui_wnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon = g_icon_off;
    CopyW(g_nid.szTip, APP_NAME, 128);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    UpdateTray();
    BackendStart();
    if (show_settings)
        ShowSettings();

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        if (g_ui_settings && IsWindowVisible(g_ui_settings) && IsDialogMessageW(g_ui_settings, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
