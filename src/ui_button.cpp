#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string.h>
#include "appinfo.h"
#include "cmd.h"
#include "config.h"
#include "text.h"
#include "ui_internal.h"

#define IDC_E_ROW     2101
#define IDC_E_LABEL   2102
#define IDC_E_ACTION  2103
#define IDC_E_VALUE   2104
#define IDC_E_OK      2105
#define IDC_E_CANCEL  2106
#define IDC_E_HOTKEY  2108
#define IDC_E_MEDIA   2109
#define IDC_E_SYSTEM  2110
#define IDC_E_LBL_ROW 2111
#define IDC_E_LBL_BTN 2112
#define IDC_E_LBL_ACT 2113
#define IDC_E_LBL_VAL 2114

#define EDIT_W 360
#define EDIT_H 250

#define CAP_CTRL  1
#define CAP_ALT   2
#define CAP_SHIFT 4
#define CAP_WIN   8

static int g_edit_index = -1;
static HWND g_cap_edit;
static HHOOK g_cap_hook;
static int g_cap_mods;
static wchar_t g_cap_saved[CFG_VALUE_MAX];

static const char *kMediaValues[] = {
    "prev", "next", "pause", "stop", "vol_up", "vol_down", "mute"
};
static const int kMediaValueCount = 7;

static int S(int v)
{
    return UiScale(v);
}

static void LayoutEditBtn(HWND wnd)
{
    int pad = S(12);
    int y = pad;
    int lw = S(78);
    int gap = S(8);
    int ew = S(EDIT_W) - pad * 2 - lw - gap;
    int drop = S(280);
    int row = S(36);
    int lh = S(22);
    int eh = S(26);
    int x = pad + lw + gap;
    MoveWindow(GetDlgItem(wnd, IDC_E_LBL_ROW), pad, y, lw, lh, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_E_ROW), x, y, S(70), drop, TRUE);
    y += row;
    MoveWindow(GetDlgItem(wnd, IDC_E_LBL_BTN), pad, y, lw, lh, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_E_LABEL), x, y, ew, eh, TRUE);
    y += row;
    MoveWindow(GetDlgItem(wnd, IDC_E_LBL_ACT), pad, y, lw, lh, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_E_ACTION), x, y, ew, drop, TRUE);
    y += row;
    MoveWindow(GetDlgItem(wnd, IDC_E_LBL_VAL), pad, y, lw, lh, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_E_VALUE), x, y, ew, eh, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_E_HOTKEY), x, y, ew, eh, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_E_MEDIA), x, y, ew, drop, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_E_SYSTEM), x, y, ew, drop, TRUE);
    y += eh + S(14);
    MoveWindow(GetDlgItem(wnd, IDC_E_OK), S(EDIT_W) - pad - S(160), y, S(70), S(28), TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_E_CANCEL), S(EDIT_W) - pad - S(80), y, S(70), S(28), TRUE);
}

static BOOL CapIsMod(DWORD vk)
{
    return vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL ||
           vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU ||
           vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT ||
           vk == VK_LWIN || vk == VK_RWIN;
}

static int CapBit(DWORD vk)
{
    if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL) return CAP_CTRL;
    if (vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU) return CAP_ALT;
    if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT) return CAP_SHIFT;
    if (vk == VK_LWIN || vk == VK_RWIN) return CAP_WIN;
    return 0;
}

static void CapVkName(DWORD vk, char *out, int cap)
{
    out[0] = 0;
    if (vk >= 'A' && vk <= 'Z') {
        out[0] = (char)(vk - 'A' + 'a');
        out[1] = 0;
    } else if (vk >= '0' && vk <= '9') {
        out[0] = (char)vk;
        out[1] = 0;
    } else if (vk >= VK_F1 && vk <= VK_F24) {
        int n = (int)(vk - VK_F1 + 1);
        out[0] = 'f';
        if (n >= 10) {
            out[1] = (char)('0' + n / 10);
            out[2] = (char)('0' + n % 10);
            out[3] = 0;
        } else {
            out[1] = (char)('0' + n);
            out[2] = 0;
        }
    } else if (vk == VK_RETURN) CopyA(out, "enter", cap);
    else if (vk == VK_TAB) CopyA(out, "tab", cap);
    else if (vk == VK_ESCAPE) CopyA(out, "esc", cap);
    else if (vk == VK_SPACE) CopyA(out, "space", cap);
    else if (vk == VK_BACK) CopyA(out, "backspace", cap);
    else if (vk == VK_DELETE) CopyA(out, "delete", cap);
    else if (vk == VK_LEFT) CopyA(out, "left", cap);
    else if (vk == VK_RIGHT) CopyA(out, "right", cap);
    else if (vk == VK_UP) CopyA(out, "up", cap);
    else if (vk == VK_DOWN) CopyA(out, "down", cap);
    else if (vk == VK_OEM_PLUS) CopyA(out, "+", cap);
    else if (vk == VK_OEM_MINUS) CopyA(out, "-", cap);
    else if (vk == VK_OEM_COMMA) CopyA(out, ",", cap);
    else if (vk == VK_OEM_PERIOD) CopyA(out, ".", cap);
}

static void CapBuild(char *out, int cap, DWORD vk, BOOL preview)
{
    out[0] = 0;
    if (g_cap_mods & CAP_CTRL) AppendA(out, cap, "ctrl+");
    if (g_cap_mods & CAP_SHIFT) AppendA(out, cap, "shift+");
    if (g_cap_mods & CAP_ALT) AppendA(out, cap, "alt+");
    if (g_cap_mods & CAP_WIN) AppendA(out, cap, "win+");
    if (preview) {
        AppendA(out, cap, "...");
        return;
    }
    char key[16];
    CapVkName(vk, key, 16);
    if (!key[0]) {
        out[0] = 0;
        return;
    }
    AppendA(out, cap, key);
}

static void CapSetTextA(const char *s)
{
    if (!g_cap_edit)
        return;
    wchar_t w[64];
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 64);
    SetWindowTextW(g_cap_edit, w);
}

static void CapStop(void)
{
    if (g_cap_hook) {
        UnhookWindowsHookEx(g_cap_hook);
        g_cap_hook = 0;
    }
    g_cap_edit = 0;
    g_cap_mods = 0;
}

static void CapFinish(DWORD vk)
{
    char s[64];
    CapBuild(s, 64, vk, FALSE);
    if (!s[0]) {
        if (g_cap_edit)
            SetWindowTextW(g_cap_edit, g_cap_saved);
        CapStop();
        return;
    }
    CapSetTextA(s);
    CapStop();
}

static LRESULT CALLBACK CapHook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && g_cap_edit) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lp;
        BOOL down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
        BOOL up = (wp == WM_KEYUP || wp == WM_SYSKEYUP);
        if (down) {
            if (kb->vkCode == VK_ESCAPE) {
                if (g_cap_mods == 0) {
                    SetWindowTextW(g_cap_edit, g_cap_saved);
                    CapStop();
                } else {
                    CapFinish(VK_ESCAPE);
                }
                return 1;
            }
            if (CapIsMod(kb->vkCode)) {
                g_cap_mods |= CapBit(kb->vkCode);
                char s[64];
                CapBuild(s, 64, 0, TRUE);
                CapSetTextA(s);
                return 1;
            }
            CapFinish(kb->vkCode);
            return 1;
        }
        if (up) {
            if (CapIsMod(kb->vkCode)) {
                g_cap_mods &= ~CapBit(kb->vkCode);
                if (g_cap_hook) {
                    char s[64];
                    CapBuild(s, 64, 0, TRUE);
                    CapSetTextA(s);
                }
            }
            return 1;
        }
    }
    return CallNextHookEx(g_cap_hook, code, wp, lp);
}

static void CapStart(HWND edit)
{
    if (!edit)
        return;
    if (g_cap_hook && g_cap_edit == edit)
        return;
    CapStop();
    g_cap_edit = edit;
    GetWindowTextW(edit, g_cap_saved, CFG_VALUE_MAX);
    g_cap_mods = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) g_cap_mods |= CAP_CTRL;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) g_cap_mods |= CAP_ALT;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) g_cap_mods |= CAP_SHIFT;
    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))
        g_cap_mods |= CAP_WIN;
    SetWindowTextW(edit, L"Press keys...");
    g_cap_hook = SetWindowsHookExW(WH_KEYBOARD_LL, CapHook, GetModuleHandleW(0), 0);
    if (!g_cap_hook)
        g_cap_edit = 0;
}

static int MediaValueIndex(const char *value)
{
    for (int i = 0; i < kMediaValueCount; ++i) {
        if (EqA(value, kMediaValues[i]))
            return i;
    }
    return 0;
}

static void SyncValueControls(HWND wnd)
{
    int sel = (int)SendDlgItemMessageW(wnd, IDC_E_ACTION, CB_GETCURSEL, 0, 0);
    if (sel != 1)
        CapStop();
    ShowWindow(GetDlgItem(wnd, IDC_E_MEDIA), sel == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(wnd, IDC_E_HOTKEY), sel == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(wnd, IDC_E_VALUE), (sel == 2 || sel == 4) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(wnd, IDC_E_SYSTEM), sel == 3 ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK EditBtnProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        CreateWindowExW(0, L"STATIC", L"Row", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                        0, 0, 0, 0, wnd, (HMENU)IDC_E_LBL_ROW, g_ui_inst, 0);
        HWND row = CreateWindowExW(0, L"COMBOBOX", L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                   0, 0, 0, 0, wnd, (HMENU)IDC_E_ROW, g_ui_inst, 0);
        CreateWindowExW(0, L"STATIC", L"Button", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                        0, 0, 0, 0, wnd, (HMENU)IDC_E_LBL_BTN, g_ui_inst, 0);
        HWND label = CreateWindowExW(0, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 0, 0, wnd, (HMENU)IDC_E_LABEL, g_ui_inst, 0);
        UiFrame(label);
        CreateWindowExW(0, L"STATIC", L"Action", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                        0, 0, 0, 0, wnd, (HMENU)IDC_E_LBL_ACT, g_ui_inst, 0);
        HWND act = CreateWindowExW(0, L"COMBOBOX", L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                   0, 0, 0, 0, wnd, (HMENU)IDC_E_ACTION, g_ui_inst, 0);
        CreateWindowExW(0, L"STATIC", L"Value", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                        0, 0, 0, 0, wnd, (HMENU)IDC_E_LBL_VAL, g_ui_inst, 0);
        HWND value = CreateWindowExW(0, L"EDIT", L"",
                                     WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 0, 0, wnd, (HMENU)IDC_E_VALUE, g_ui_inst, 0);
        UiFrame(value);
        HWND hotkey = CreateWindowExW(0, L"EDIT", L"",
                                      WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
                                      0, 0, 0, 0, wnd, (HMENU)IDC_E_HOTKEY, g_ui_inst, 0);
        UiFrame(hotkey);
        HWND media = CreateWindowExW(0, L"COMBOBOX", L"",
                                     WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
                                     0, 0, 0, 0, wnd, (HMENU)IDC_E_MEDIA, g_ui_inst, 0);
        HWND system = CreateWindowExW(0, L"COMBOBOX", L"",
                                      WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
                                      0, 0, 0, 0, wnd, (HMENU)IDC_E_SYSTEM, g_ui_inst, 0);
        UiMakePush(wnd, L"OK", IDC_E_OK, TRUE, TRUE);
        UiMakePush(wnd, L"Cancel", IDC_E_CANCEL, FALSE, TRUE);
        if (row) {
            for (int i = 1; i <= 10; ++i) {
                wchar_t t[4];
                if (i == 10) {
                    t[0] = L'1';
                    t[1] = L'0';
                    t[2] = 0;
                } else {
                    t[0] = (wchar_t)(L'0' + i);
                    t[1] = 0;
                }
                SendMessageW(row, CB_ADDSTRING, 0, (LPARAM)t);
            }
            SendMessageW(row, CB_SETCURSEL, 0, 0);
        }
        if (act) {
            SendMessageW(act, CB_ADDSTRING, 0, (LPARAM)L"media");
            SendMessageW(act, CB_ADDSTRING, 0, (LPARAM)L"keys");
            SendMessageW(act, CB_ADDSTRING, 0, (LPARAM)L"run");
            SendMessageW(act, CB_ADDSTRING, 0, (LPARAM)L"system");
            SendMessageW(act, CB_ADDSTRING, 0, (LPARAM)L"cmd");
            SendMessageW(act, CB_SETCURSEL, 0, 0);
        }
        if (media) {
            for (int i = 0; i < kMediaValueCount; ++i) {
                wchar_t t[32];
                MultiByteToWideChar(CP_UTF8, 0, kMediaValues[i], -1, t, 32);
                SendMessageW(media, CB_ADDSTRING, 0, (LPARAM)t);
            }
            SendMessageW(media, CB_SETCURSEL, 0, 0);
        }
        if (system) {
            int nsys = ConfigSystemCount();
            for (int i = 0; i < nsys; ++i)
                SendMessageW(system, CB_ADDSTRING, 0, (LPARAM)ConfigSystemTitle(i));
            SendMessageW(system, CB_SETCURSEL, 0, 0);
        }
        UiApplyFont(wnd);
        UiApplyWindowTheme(wnd);
        if (g_edit_index >= 0) {
            ButtonDef cur;
            if (ConfigCopy(g_edit_index, &cur)) {
                wchar_t buf[CFG_VALUE_MAX];
                int row_sel = cur.row - 1;
                if (row_sel < 0) row_sel = 0;
                if (row_sel > 9) row_sel = 9;
                if (row)
                    SendMessageW(row, CB_SETCURSEL, (WPARAM)row_sel, 0);
                MultiByteToWideChar(CP_UTF8, 0, cur.label, -1, buf, CFG_LABEL_MAX);
                SetDlgItemTextW(wnd, IDC_E_LABEL, buf);
                MultiByteToWideChar(CP_UTF8, 0, cur.value, -1, buf, CFG_VALUE_MAX);
                SetDlgItemTextW(wnd, IDC_E_VALUE, buf);
                SetDlgItemTextW(wnd, IDC_E_HOTKEY, buf);
                int sel = 0;
                if (EqA(cur.action, "keys")) sel = 1;
                else if (EqA(cur.action, "run")) sel = 2;
                else if (EqA(cur.action, "system")) sel = 3;
                else if (EqA(cur.action, "cmd")) sel = 4;
                if (act)
                    SendMessageW(act, CB_SETCURSEL, (WPARAM)sel, 0);
                if (media)
                    SendMessageW(media, CB_SETCURSEL, (WPARAM)MediaValueIndex(cur.value), 0);
                if (system) {
                    int si = ConfigSystemIndex(cur.value);
                    if (si < 0) si = 0;
                    SendMessageW(system, CB_SETCURSEL, (WPARAM)si, 0);
                }
            }
        }
        LayoutEditBtn(wnd);
        SyncValueControls(wnd);
        return 0;
    }
    case WM_DPICHANGED:
        UiOnDpiChanged(wnd, wp, lp);
        LayoutEditBtn(wnd);
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
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_E_ACTION && HIWORD(wp) == CBN_SELCHANGE) {
            SyncValueControls(wnd);
            return 0;
        }
        if (LOWORD(wp) == IDC_E_HOTKEY && HIWORD(wp) == EN_SETFOCUS) {
            CapStart((HWND)lp);
            return 0;
        }
        if (LOWORD(wp) == IDC_E_HOTKEY && HIWORD(wp) == EN_KILLFOCUS) {
            if (g_cap_hook) {
                SetWindowTextW(g_cap_edit ? g_cap_edit : (HWND)lp, g_cap_saved);
                CapStop();
            }
            return 0;
        }
        if (LOWORD(wp) == IDC_E_CANCEL) {
            CapStop();
            DestroyWindow(wnd);
            return 0;
        }
        if (LOWORD(wp) == IDC_E_OK) {
            ButtonDef b;
            memset(&b, 0, sizeof(b));
            int row_sel = (int)SendDlgItemMessageW(wnd, IDC_E_ROW, CB_GETCURSEL, 0, 0);
            if (row_sel < 0) row_sel = 0;
            if (row_sel > 9) row_sel = 9;
            b.row = row_sel + 1;
            wchar_t wbuf[CFG_VALUE_MAX];
            GetDlgItemTextW(wnd, IDC_E_LABEL, wbuf, CFG_LABEL_MAX);
            if (!wbuf[0]) {
                MessageBoxW(wnd, L"Button name is required.", APP_NAME, MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (!WideToUtf8(wbuf, b.label, CFG_LABEL_MAX)) {
                MessageBoxW(wnd, L"Button name is too long.", APP_NAME, MB_OK | MB_ICONWARNING);
                return 0;
            }
            int sel = (int)SendDlgItemMessageW(wnd, IDC_E_ACTION, CB_GETCURSEL, 0, 0);
            const char *actions[] = { "media", "keys", "run", "system", "cmd" };
            if (sel < 0 || sel > 4) sel = 0;
            CopyA(b.action, actions[sel], CFG_ACTION_MAX);
            if (sel == 0) {
                int mv = (int)SendDlgItemMessageW(wnd, IDC_E_MEDIA, CB_GETCURSEL, 0, 0);
                if (mv < 0 || mv >= kMediaValueCount) mv = 0;
                CopyA(b.value, kMediaValues[mv], CFG_VALUE_MAX);
            } else if (sel == 3) {
                int sv = (int)SendDlgItemMessageW(wnd, IDC_E_SYSTEM, CB_GETCURSEL, 0, 0);
                if (sv < 0 || sv >= ConfigSystemCount()) sv = 0;
                CopyA(b.value, ConfigSystemValue(sv), CFG_VALUE_MAX);
            } else if (sel == 1) {
                CapStop();
                GetDlgItemTextW(wnd, IDC_E_HOTKEY, wbuf, CFG_VALUE_MAX);
                if (!WideToUtf8(wbuf, b.value, CFG_VALUE_MAX)) {
                    MessageBoxW(wnd, L"Value is too long.", APP_NAME, MB_OK | MB_ICONWARNING);
                    return 0;
                }
                int n = LenA(b.value);
                BOOL bad = !b.value[0] || EqA(b.value, "Press keys...");
                if (!bad && n >= 3 && b.value[n - 1] == '.' && b.value[n - 2] == '.' && b.value[n - 3] == '.')
                    bad = TRUE;
                if (bad) {
                    MessageBoxW(wnd, L"Press a key combination.", APP_NAME, MB_OK | MB_ICONWARNING);
                    return 0;
                }
            } else {
                GetDlgItemTextW(wnd, IDC_E_VALUE, wbuf, CFG_VALUE_MAX);
                if (!WideToUtf8(wbuf, b.value, CFG_VALUE_MAX)) {
                    MessageBoxW(wnd, L"Value is too long.", APP_NAME, MB_OK | MB_ICONWARNING);
                    return 0;
                }
                if (sel == 4) {
                    char err[64];
                    if (!CmdCheck(b.value, err, sizeof(err))) {
                        wchar_t werr[80];
                        Utf8ToWide(err, werr, 80);
                        MessageBoxW(wnd, werr[0] ? werr : L"bad command", APP_NAME, MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                }
            }
            if (g_edit_index < 0)
                ConfigAdd(&b);
            else
                ConfigUpdate(g_edit_index, &b);
            if (g_ui_settings)
                UiRefreshButtons(g_ui_settings);
            DestroyWindow(wnd);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        CapStop();
        DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        CapStop();
        if (g_ui_settings) {
            EnableWindow(g_ui_settings, TRUE);
            SetForegroundWindow(g_ui_settings);
        }
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void UiRegisterEditClass(void)
{
    WNDCLASSEXW ec;
    memset(&ec, 0, sizeof(ec));
    ec.cbSize = sizeof(ec);
    ec.lpfnWndProc = EditBtnProc;
    ec.hInstance = g_ui_inst;
    ec.hCursor = LoadCursorW(0, IDC_ARROW);
    ec.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    ec.lpszClassName = L"IlizPcrcEditBtn";
    RegisterClassExW(&ec);
}

void UiShowEditButton(HWND parent, int index)
{
    g_edit_index = index;
    HWND edit = CreateWindowExW(WS_EX_DLGMODALFRAME, L"IlizPcrcEditBtn",
                                index < 0 ? L"Add button" : L"Edit button",
                                WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                CW_USEDEFAULT, CW_USEDEFAULT, S(EDIT_W), S(EDIT_H),
                                parent, 0, g_ui_inst, 0);
    if (!edit)
        return;
    UiCenter(edit, S(EDIT_W), S(EDIT_H), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU);
    EnableWindow(parent, FALSE);
    ShowWindow(edit, SW_SHOW);
    SetForegroundWindow(edit);
}
