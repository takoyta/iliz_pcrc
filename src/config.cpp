#define WIN32_LEAN_AND_MEAN
#include <string.h>
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <powrprof.h>
#include "audio.h"
#include "cmd.h"
#include "config.h"
#include "settings.h"
#include "text.h"
#include "cJSON.h"

static ButtonDef g_items[CFG_MAX_BUTTONS];
static int g_count;
static CRITICAL_SECTION g_cfg_cs;
static DWORD g_cfg_owner;
static int g_cfg_depth;

void ConfigInit()
{
    InitializeCriticalSection(&g_cfg_cs);
}

static void CfgLock()
{
    DWORD id = GetCurrentThreadId();
    if (g_cfg_depth > 0 && g_cfg_owner == id) {
        ++g_cfg_depth;
        return;
    }
    EnterCriticalSection(&g_cfg_cs);
    g_cfg_owner = id;
    g_cfg_depth = 1;
}

static void CfgUnlock()
{
    if (g_cfg_depth > 1) {
        --g_cfg_depth;
        return;
    }
    g_cfg_depth = 0;
    g_cfg_owner = 0;
    LeaveCriticalSection(&g_cfg_cs);
}

static BOOL EqI(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return FALSE;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static BOOL IsExtKey(WORD vk)
{
    return vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN ||
           vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END ||
           vk == VK_PRIOR || vk == VK_NEXT || vk == VK_LEFT || vk == VK_UP ||
           vk == VK_RIGHT || vk == VK_DOWN || vk == VK_NUMLOCK || vk == VK_DIVIDE ||
           vk == VK_SNAPSHOT || vk == VK_APPS;
}

static void MediaKey(WORD vk)
{
    INPUT in[2];
    memset(in, 0, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

static WORD MapKeyName(const char *name)
{
    if (EqI(name, "ctrl") || EqI(name, "control")) return VK_CONTROL;
    if (EqI(name, "alt")) return VK_MENU;
    if (EqI(name, "shift")) return VK_SHIFT;
    if (EqI(name, "win") || EqI(name, "super")) return VK_LWIN;
    if (EqI(name, "enter") || EqI(name, "return")) return VK_RETURN;
    if (EqI(name, "tab")) return VK_TAB;
    if (EqI(name, "esc") || EqI(name, "escape")) return VK_ESCAPE;
    if (EqI(name, "space")) return VK_SPACE;
    if (EqI(name, "backspace") || EqI(name, "bs")) return VK_BACK;
    if (EqI(name, "delete") || EqI(name, "del")) return VK_DELETE;
    if (EqI(name, "left")) return VK_LEFT;
    if (EqI(name, "right")) return VK_RIGHT;
    if (EqI(name, "up")) return VK_UP;
    if (EqI(name, "down")) return VK_DOWN;
    if (name[0] == 'f' || name[0] == 'F') {
        int n = 0;
        const char *p = name + 1;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (*p - '0');
            ++p;
        }
        if (n >= 1 && n <= 24 && *p == 0)
            return (WORD)(VK_F1 + n - 1);
    }
    if (name[0] && !name[1]) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') return (WORD)(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z') return (WORD)c;
        if (c >= '0' && c <= '9') return (WORD)c;
    }
    return 0;
}

static void ExecuteKeys(const char *spec)
{
    WORD keys[8];
    int n = 0;
    char part[32];
    int pi = 0;
    for (const char *p = spec;; ++p) {
        char c = *p;
        if (c == '+' || c == 0) {
            part[pi] = 0;
            if (pi > 0 && n < 8) {
                WORD vk = MapKeyName(part);
                if (vk)
                    keys[n++] = vk;
            }
            pi = 0;
            if (!c)
                break;
            continue;
        }
        if (c == ' ')
            continue;
        if (pi < 31)
            part[pi++] = c;
    }
    if (!n)
        return;

    INPUT in[16];
    memset(in, 0, sizeof(in));
    int m = 0;
    for (int i = 0; i < n && m < 16; ++i) {
        in[m].type = INPUT_KEYBOARD;
        in[m].ki.wVk = keys[i];
        if (IsExtKey(keys[i]))
            in[m].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        ++m;
    }
    for (int i = n - 1; i >= 0 && m < 16; --i) {
        in[m].type = INPUT_KEYBOARD;
        in[m].ki.wVk = keys[i];
        in[m].ki.dwFlags = KEYEVENTF_KEYUP;
        if (IsExtKey(keys[i]))
            in[m].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        ++m;
    }
    SendInput((UINT)m, in, sizeof(INPUT));
}

static void ExecuteMedia(const char *value)
{
    if (EqA(value, "prev") || EqA(value, "back"))
        MediaKey(VK_MEDIA_PREV_TRACK);
    else if (EqA(value, "next"))
        MediaKey(VK_MEDIA_NEXT_TRACK);
    else if (EqA(value, "pause") || EqA(value, "play"))
        MediaKey(VK_MEDIA_PLAY_PAUSE);
    else if (EqA(value, "stop"))
        MediaKey(VK_MEDIA_STOP);
    else if (EqA(value, "vol_down") || EqA(value, "vol-"))
        MediaKey(VK_VOLUME_DOWN);
    else if (EqA(value, "vol_up") || EqA(value, "vol+"))
        MediaKey(VK_VOLUME_UP);
    else if (EqA(value, "mute"))
        MediaKey(VK_VOLUME_MUTE);
}

static void ExecuteRun(const char *value)
{
    wchar_t path[CFG_VALUE_MAX];
    MultiByteToWideChar(CP_UTF8, 0, value, -1, path, CFG_VALUE_MAX);
    path[CFG_VALUE_MAX - 1] = 0;
    if (path[0])
        ShellExecuteW(0, L"open", path, 0, 0, SW_SHOWNORMAL);
}

static const struct {
    const char *value;
    const wchar_t *title;
} kSystem[] = {
    { "lock",     L"Lock screen" },
    { "sleep",    L"Sleep" },
    { "display",  L"Turn off display" },
    { "restart",  L"Restart" },
    { "shutdown", L"Shut down" },
    { "signout",  L"Sign out" },
    { "shot",     L"Screenshot" },
    { "mic",      L"Toggle microphone" },
    { "beep",     L"Find PC" },
};
static const int kSystemCount = 9;

int ConfigSystemCount()
{
    return kSystemCount;
}

const char *ConfigSystemValue(int index)
{
    if (index < 0 || index >= kSystemCount)
        return kSystem[0].value;
    return kSystem[index].value;
}

const wchar_t *ConfigSystemTitle(int index)
{
    if (index < 0 || index >= kSystemCount)
        return kSystem[0].title;
    return kSystem[index].title;
}

int ConfigSystemIndex(const char *value)
{
    for (int i = 0; i < kSystemCount; ++i) {
        if (EqA(value, kSystem[i].value))
            return i;
    }
    return -1;
}

static void ToggleMic()
{
    static const GUID clsidEnum = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
    static const GUID iidEnum = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
    static const GUID iidVol = {0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};

    HRESULT hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    BOOL uninit = (hr == S_OK || hr == S_FALSE);
    IMMDeviceEnumerator *en = 0;
    IMMDevice *dev = 0;
    IAudioEndpointVolume *vol = 0;
    hr = CoCreateInstance(clsidEnum, 0, CLSCTX_ALL, iidEnum, (void **)&en);
    if (SUCCEEDED(hr) && en) {
        hr = en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev);
        if (SUCCEEDED(hr) && dev) {
            hr = dev->Activate(iidVol, CLSCTX_ALL, 0, (void **)&vol);
            if (SUCCEEDED(hr) && vol) {
                BOOL muted = FALSE;
                if (SUCCEEDED(vol->GetMute(&muted)))
                    vol->SetMute(muted ? FALSE : TRUE, 0);
            }
        }
    }
    if (vol) vol->Release();
    if (dev) dev->Release();
    if (en) en->Release();
    if (uninit)
        CoUninitialize();
}

static void FindPc()
{
    AudioPlaySibling(L"beep.mp3");
}

static DWORD PowerReason()
{
    return SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED;
}

static void EnableShutdown()
{
    HANDLE tok = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
        return;
    LUID luid;
    if (LookupPrivilegeValueW(0, SE_SHUTDOWN_NAME, &luid)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, 0, 0);
    }
    CloseHandle(tok);
}

void ConfigApplyPower(const char *value)
{
    if (!EqA(value, "shutdown") && !EqA(value, "restart"))
        return;
    EnableShutdown();
    UINT flags = EWX_FORCEIFHUNG;
    if (EqA(value, "restart"))
        flags |= EWX_REBOOT;
    else
        flags |= EWX_SHUTDOWN | EWX_POWEROFF;
    ExitWindowsEx(flags, PowerReason());
}

static int ExecuteSystem(const char *value)
{
    if (EqA(value, "lock")) {
        LockWorkStation();
        return CFG_EXEC_OK;
    }
    if (EqA(value, "sleep")) {
        SetSuspendState(FALSE, FALSE, FALSE);
        return CFG_EXEC_OK;
    }
    if (EqA(value, "display")) {
        SendNotifyMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, (LPARAM)2);
        return CFG_EXEC_OK;
    }
    if (EqA(value, "signout")) {
        ExitWindowsEx(EWX_LOGOFF | EWX_FORCEIFHUNG, PowerReason());
        return CFG_EXEC_OK;
    }
    if (EqA(value, "shutdown") || EqA(value, "restart"))
        return CFG_EXEC_CONFIRM;
    if (EqA(value, "shot"))
        return CFG_EXEC_SHOT;
    if (EqA(value, "mic")) {
        ToggleMic();
        return CFG_EXEC_OK;
    }
    if (EqA(value, "beep")) {
        FindPc();
        return CFG_EXEC_OK;
    }
    return CFG_EXEC_OK;
}

int ConfigExecute(const ButtonDef *b, char *err, int errCap, char *reply, int replyCap)
{
    if (err && errCap > 0)
        err[0] = 0;
    if (reply && replyCap > 0)
        reply[0] = 0;
    if (!b || !b->action[0])
        return CFG_EXEC_OK;
    if (EqA(b->action, "media"))
        ExecuteMedia(b->value);
    else if (EqA(b->action, "keys"))
        ExecuteKeys(b->value);
    else if (EqA(b->action, "run"))
        ExecuteRun(b->value);
    else if (EqA(b->action, "system"))
        return ExecuteSystem(b->value);
    else if (EqA(b->action, "cmd")) {
        CmdOut cmd;
        CmdRun(b->value, &cmd);
        if (cmd.status != CMD_OK) {
            if (err && errCap > 0)
                CopyA(err, cmd.error[0] ? cmd.error : "bad command", errCap);
            return CFG_EXEC_FAIL;
        }
        if (reply && replyCap > 0 && cmd.reply[0])
            CopyA(reply, cmd.reply, replyCap);
    }
    return CFG_EXEC_OK;
}

const wchar_t *ConfigActionTitle(const char *action)
{
    if (EqA(action, "media")) return L"media";
    if (EqA(action, "keys")) return L"keys";
    if (EqA(action, "run")) return L"run";
    if (EqA(action, "system")) return L"system";
    if (EqA(action, "cmd")) return L"cmd";
    return L"?";
}

const wchar_t *ConfigValueTitle(const char *action, const char *value)
{
    if (EqA(action, "system")) {
        int i = ConfigSystemIndex(value);
        if (i >= 0)
            return ConfigSystemTitle(i);
    }
    static wchar_t buf[CFG_VALUE_MAX];
    MultiByteToWideChar(CP_UTF8, 0, value, -1, buf, CFG_VALUE_MAX);
    buf[CFG_VALUE_MAX - 1] = 0;
    return buf;
}

void ConfigGetPath(wchar_t *out, int cap)
{
    out[0] = 0;
    if (!GetModuleFileNameW(0, out, (DWORD)cap))
        return;
    int n = 0;
    while (out[n]) ++n;
    while (n > 0 && out[n - 1] != L'\\' && out[n - 1] != L'/')
        --n;
    const wchar_t *name = L"config.json";
    int i = 0;
    while (name[i] && n < cap - 1)
        out[n++] = name[i++];
    out[n] = 0;
}

static int CmpBtn(const ButtonDef *a, const ButtonDef *b)
{
    if (a->row != b->row)
        return a->row - b->row;
    return 0;
}

void ConfigSort()
{
    CfgLock();
    for (int i = 1; i < g_count; ++i) {
        ButtonDef t = g_items[i];
        int j = i;
        while (j > 0 && CmpBtn(&g_items[j - 1], &t) > 0) {
            g_items[j] = g_items[j - 1];
            --j;
        }
        g_items[j] = t;
    }
    CfgUnlock();
}

int ConfigCount()
{
    CfgLock();
    int n = g_count;
    CfgUnlock();
    return n;
}

const ButtonDef *ConfigGet(int i)
{
    static ButtonDef item;
    CfgLock();
    BOOL ok = (i >= 0 && i < g_count);
    if (ok)
        item = g_items[i];
    CfgUnlock();
    return ok ? &item : 0;
}

ButtonDef *ConfigGetMut(int i)
{
    CfgLock();
    ButtonDef *b = (i >= 0 && i < g_count) ? &g_items[i] : 0;
    CfgUnlock();
    return b;
}

BOOL ConfigCopy(int i, ButtonDef *out)
{
    if (!out)
        return FALSE;
    CfgLock();
    BOOL ok = FALSE;
    if (i >= 0 && i < g_count) {
        *out = g_items[i];
        ok = TRUE;
    }
    CfgUnlock();
    return ok;
}

int ConfigCopyAll(ButtonDef *dst, int cap)
{
    if (!dst || cap <= 0)
        return 0;
    CfgLock();
    int n = g_count;
    if (n > cap)
        n = cap;
    for (int i = 0; i < n; ++i)
        dst[i] = g_items[i];
    CfgUnlock();
    return n;
}

void ConfigClear()
{
    CfgLock();
    g_count = 0;
    CfgUnlock();
}

BOOL ConfigAdd(const ButtonDef *b)
{
    CfgLock();
    BOOL ok = FALSE;
    if (b && g_count < CFG_MAX_BUTTONS && b->label[0] && b->action[0]) {
        g_items[g_count++] = *b;
        ConfigSort();
        ok = TRUE;
    }
    CfgUnlock();
    return ok;
}

BOOL ConfigRemove(int index)
{
    CfgLock();
    BOOL ok = FALSE;
    if (index >= 0 && index < g_count) {
        for (int i = index; i < g_count - 1; ++i)
            g_items[i] = g_items[i + 1];
        --g_count;
        ok = TRUE;
    }
    CfgUnlock();
    return ok;
}

BOOL ConfigUpdate(int index, const ButtonDef *b)
{
    CfgLock();
    BOOL ok = FALSE;
    if (index >= 0 && index < g_count && b && b->label[0] && b->action[0]) {
        g_items[index] = *b;
        ConfigSort();
        ok = TRUE;
    }
    CfgUnlock();
    return ok;
}

BOOL ConfigMove(int index, int delta)
{
    CfgLock();
    BOOL ok = FALSE;
    int j = index + delta;
    if ((delta == -1 || delta == 1) && index >= 0 && index < g_count && j >= 0 && j < g_count) {
        g_items[index].row = g_items[j].row;
        ButtonDef tmp = g_items[index];
        g_items[index] = g_items[j];
        g_items[j] = tmp;
        ok = TRUE;
    }
    CfgUnlock();
    return ok;
}

BOOL ConfigFindCopy(const char *label_utf8, ButtonDef *out)
{
    if (!label_utf8 || !out)
        return FALSE;
    CfgLock();
    BOOL ok = FALSE;
    for (int i = 0; i < g_count; ++i) {
        if (EqA(g_items[i].label, label_utf8)) {
            *out = g_items[i];
            ok = TRUE;
            break;
        }
    }
    CfgUnlock();
    return ok;
}

const ButtonDef *ConfigFindLabel(const char *label_utf8)
{
    static ButtonDef found;
    if (!ConfigFindCopy(label_utf8, &found))
        return 0;
    return &found;
}

static void CopyField(char *dst, int cap, const cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    dst[0] = 0;
    if (cJSON_IsString(v) && v->valuestring)
        CopyA(dst, v->valuestring, cap);
}

BOOL ConfigAddReplyKeyboard(cJSON *message)
{
    CfgLock();
    if (g_count <= 0) {
        CfgUnlock();
        return FALSE;
    }
    cJSON *rows = cJSON_CreateArray();
    cJSON *row = 0;
    int cur = -1;
    for (int i = 0; i < g_count; ++i) {
        if (g_items[i].row != cur) {
            row = cJSON_CreateArray();
            cJSON_AddItemToArray(rows, row);
            cur = g_items[i].row;
        }
        cJSON *btn = cJSON_CreateObject();
        cJSON_AddStringToObject(btn, "text", g_items[i].label);
        cJSON_AddItemToArray(row, btn);
    }
    cJSON *markup = cJSON_CreateObject();
    cJSON_AddItemToObject(markup, "keyboard", rows);
    cJSON_AddBoolToObject(markup, "resize_keyboard", 1);
    cJSON_AddItemToObject(message, "reply_markup", markup);
    CfgUnlock();
    return TRUE;
}

BOOL ConfigLoad()
{
    CfgLock();
    g_count = 0;
    wchar_t path[MAX_PATH];
    ConfigGetPath(path, MAX_PATH);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (file == INVALID_HANDLE_VALUE) {
        CfgUnlock();
        return FALSE;
    }
    DWORD size = GetFileSize(file, 0);
    if (size == INVALID_FILE_SIZE || size == 0 || size >= 65536) {
        CloseHandle(file);
        CfgUnlock();
        return FALSE;
    }
    char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size + 1);
    if (!buf) {
        CloseHandle(file);
        CfgUnlock();
        return FALSE;
    }
    DWORD got = 0;
    BOOL read = ReadFile(file, buf, size, &got, 0);
    CloseHandle(file);
    if (!read || got == 0) {
        HeapFree(GetProcessHeap(), 0, buf);
        CfgUnlock();
        return FALSE;
    }
    buf[got] = 0;
    cJSON *root = cJSON_Parse(buf);
    HeapFree(GetProcessHeap(), 0, buf);
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "buttons");
    cJSON *item = 0;
    cJSON_ArrayForEach(item, arr) {
        if (g_count >= CFG_MAX_BUTTONS || !cJSON_IsObject(item))
            continue;
        ButtonDef b;
        memset(&b, 0, sizeof(b));
        b.row = 1;
        cJSON *row = cJSON_GetObjectItemCaseSensitive(item, "row");
        if (cJSON_IsNumber(row)) {
            int r = (int)row->valuedouble;
            if (r >= 1 && r <= 20)
                b.row = r;
        }
        CopyField(b.label, CFG_LABEL_MAX, item, "label");
        CopyField(b.action, CFG_ACTION_MAX, item, "action");
        CopyField(b.value, CFG_VALUE_MAX, item, "value");
        if (b.label[0] && b.action[0])
            g_items[g_count++] = b;
    }
    cJSON_Delete(root);
    if (g_count > 0)
        ConfigSort();
    CfgUnlock();
    return g_count > 0;
}

void ConfigAppendButtons(cJSON *root)
{
    CfgLock();
    cJSON *arr = cJSON_AddArrayToObject(root, "buttons");
    for (int i = 0; i < g_count; ++i) {
        const ButtonDef *b = &g_items[i];
        cJSON *item = cJSON_CreateObject();
        int row = b->row > 0 ? b->row : 1;
        cJSON_AddNumberToObject(item, "row", row);
        cJSON_AddStringToObject(item, "label", b->label);
        cJSON_AddStringToObject(item, "action", b->action);
        cJSON_AddStringToObject(item, "value", b->value);
        cJSON_AddItemToArray(arr, item);
    }
    CfgUnlock();
}

BOOL ConfigSave()
{
    ConfigSort();
    return SettingsSave();
}
