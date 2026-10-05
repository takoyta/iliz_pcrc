#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <string.h>
#include "cJSON.h"
#include "settings.h"
#include "config.h"
#include "text.h"

#define RUN_KEY   L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE L"IlizPcrc"

static AppSettings g_set;

AppSettings *SettingsMut(void)
{
    return &g_set;
}

const AppSettings *Settings(void)
{
    return &g_set;
}

static void ExeDirFile(wchar_t *out, int cap, const wchar_t *name)
{
    out[0] = 0;
    if (!GetModuleFileNameW(0, out, (DWORD)cap))
        return;
    int n = 0;
    while (out[n])
        ++n;
    while (n > 0 && out[n - 1] != L'\\' && out[n - 1] != L'/')
        --n;
    int i = 0;
    while (name[i] && n < cap - 1)
        out[n++] = name[i++];
    out[n] = 0;
}

static BOOL ReadTextFile(const wchar_t *path, char **out)
{
    *out = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD size = GetFileSize(file, 0);
    if (size == INVALID_FILE_SIZE || size == 0 || size >= 65536) {
        CloseHandle(file);
        return FALSE;
    }
    char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size + 1);
    if (!buf) {
        CloseHandle(file);
        return FALSE;
    }
    DWORD got = 0;
    BOOL ok = ReadFile(file, buf, size, &got, 0);
    CloseHandle(file);
    if (!ok || got == 0) {
        HeapFree(GetProcessHeap(), 0, buf);
        return FALSE;
    }
    buf[got] = 0;
    *out = buf;
    return TRUE;
}

static void TakeBool(const cJSON *root, const char *key, BOOL *out)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsBool(v))
        *out = cJSON_IsTrue(v);
}

static void TakeW(const cJSON *root, const char *key, wchar_t *dst, int cap)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(v) && v->valuestring)
        Utf8ToWide(v->valuestring, dst, cap);
}

static void AddW(cJSON *root, const char *key, const wchar_t *val)
{
    char utf[2048];
    utf[0] = 0;
    if (val)
        WideToUtf8(val, utf, sizeof(utf));
    cJSON_AddStringToObject(root, key, utf);
}

static void ApplyConfigText(const char *buf)
{
    cJSON *root = cJSON_Parse(buf);
    if (!root)
        return;
    TakeBool(root, "ilizEnabled", &g_set.iliz_on);
    TakeBool(root, "ownEnabled", &g_set.own_on);
    TakeW(root, "token", g_set.token, SET_TOKEN_MAX);
    TakeW(root, "server", g_set.server, SET_HOST_MAX);
    TakeW(root, "controlPage", g_set.control_page, SET_URL_MAX);
    TakeW(root, "telegramBotUsername", g_set.tg_user, SET_TG_USER);
    TakeW(root, "telegramDeepLink", g_set.tg_link, SET_TG_LINK);
    TakeW(root, "botToken", g_set.bot_token, SET_BOT_MAX);
    TakeW(root, "allowedIds", g_set.allowed, SET_ALLOW_MAX);
    cJSON_Delete(root);
}

void SettingsLoad(void)
{
    memset(&g_set, 0, sizeof(g_set));
    g_set.iliz_on = TRUE;

    wchar_t path[MAX_PATH];
    ExeDirFile(path, MAX_PATH, L"config.json");
    char *buf = 0;
    if (ReadTextFile(path, &buf)) {
        ApplyConfigText(buf);
        HeapFree(GetProcessHeap(), 0, buf);
    }
    g_set.host[0] = 0;
    g_set.user[0] = 0;
    g_set.pass[0] = 0;
    g_set.port = 0;
    TrimUrlW(g_set.server);
    TrimSpaceW(g_set.bot_token);
    if (!SettingsValidToken(g_set.token))
        g_set.token[0] = 0;
}

BOOL SettingsSave(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return FALSE;
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddBoolToObject(root, "ilizEnabled", g_set.iliz_on);
    cJSON_AddBoolToObject(root, "ownEnabled", g_set.own_on);
    AddW(root, "token", g_set.token);
    AddW(root, "server", g_set.server);
    AddW(root, "controlPage", g_set.control_page);
    AddW(root, "telegramBotUsername", g_set.tg_user);
    AddW(root, "telegramDeepLink", g_set.tg_link);
    AddW(root, "botToken", g_set.bot_token);
    AddW(root, "allowedIds", g_set.allowed);
    ConfigAppendButtons(root);
    char *body = cJSON_Print(root);
    cJSON_Delete(root);
    if (!body)
        return FALSE;

    wchar_t path[MAX_PATH];
    ExeDirFile(path, MAX_PATH, L"config.json");
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        cJSON_free(body);
        return FALSE;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(file, body, (DWORD)LenA(body), &written, 0);
    CloseHandle(file);
    cJSON_free(body);
    return ok && written > 0;
}

BOOL SettingsValidToken(const wchar_t *s)
{
    int n = LenW(s);
    if (n != 32)
        return FALSE;
    for (int i = 0; i < n; ++i) {
        wchar_t c = s[i];
        BOOL hex = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
        if (!hex)
            return FALSE;
    }
    return TRUE;
}

static BOOL GenToken(wchar_t *dst, int cap)
{
    if (cap < 33)
        return FALSE;
    BYTE raw[16];
    HCRYPTPROV prov = 0;
    if (!CryptAcquireContextW(&prov, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return FALSE;
    BOOL ok = CryptGenRandom(prov, 16, raw);
    CryptReleaseContext(prov, 0);
    if (!ok)
        return FALSE;
    static const wchar_t *hex = L"0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        dst[i * 2] = hex[raw[i] >> 4];
        dst[i * 2 + 1] = hex[raw[i] & 15];
    }
    dst[32] = 0;
    return TRUE;
}

BOOL SettingsNewToken(wchar_t *dst, int cap)
{
    return GenToken(dst, cap);
}

BOOL SettingsEnsureToken(void)
{
    if (g_set.token[0])
        return FALSE;
    if (!GenToken(g_set.token, SET_TOKEN_MAX))
        CopyW(g_set.token, L"00000000000000000000000000000000", SET_TOKEN_MAX);
    SettingsSave();
    return TRUE;
}

BOOL SettingsAutostart(void)
{
    HKEY key = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return FALSE;
    DWORD type = 0, size = 0;
    LONG err = RegQueryValueExW(key, RUN_VALUE, 0, &type, 0, &size);
    RegCloseKey(key);
    return err == ERROR_SUCCESS && type == REG_SZ && size > sizeof(wchar_t);
}

void SettingsSetAutostart(BOOL on)
{
    HKEY key = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, 0, 0, KEY_SET_VALUE, 0, &key, 0) != ERROR_SUCCESS)
        return;
    if (!on) {
        RegDeleteValueW(key, RUN_VALUE);
        RegCloseKey(key);
        return;
    }
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(0, path, MAX_PATH);
    if (!n || n >= MAX_PATH) {
        RegCloseKey(key);
        return;
    }
    wchar_t cmd[MAX_PATH + 3];
    cmd[0] = L'"';
    int i = 0;
    while (path[i]) {
        cmd[i + 1] = path[i];
        ++i;
    }
    cmd[i + 1] = L'"';
    cmd[i + 2] = 0;
    RegSetValueExW(key, RUN_VALUE, 0, REG_SZ, (const BYTE *)cmd, (DWORD)((i + 3) * sizeof(wchar_t)));
    RegCloseKey(key);
}

void SettingsBuildPage(const wchar_t *server, const wchar_t *token, const wchar_t *control,
                       wchar_t *out, int cap)
{
    out[0] = 0;
    if (control && control[0]) {
        CopyW(out, control, cap);
        return;
    }
    if (server && server[0]) {
        CopyW(out, server, cap);
        AppendW(out, cap, L"/");
        if (token)
            AppendW(out, cap, token);
        return;
    }
    AppendW(out, cap, L"/");
    if (token)
        AppendW(out, cap, token);
}

void SettingsNormalizeAllowed(wchar_t *s)
{
    wchar_t out[SET_ALLOW_MAX];
    int i = 0, j = 0;
    BOOL need_comma = FALSE;
    while (s[i] && j < SET_ALLOW_MAX - 1) {
        wchar_t c = s[i++];
        if (c == L' ' || c == L'\t')
            continue;
        if (c == L',') {
            need_comma = TRUE;
            continue;
        }
        if ((c >= L'0' && c <= L'9') || c == L'-') {
            if (need_comma && j > 0 && j < SET_ALLOW_MAX - 1)
                out[j++] = L',';
            need_comma = FALSE;
            out[j++] = c;
        }
    }
    out[j] = 0;
    CopyW(s, out, SET_ALLOW_MAX);
}
