#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "appinfo.h"
#include "debug.h"
#include "text.h"

static BOOL g_on;
static BOOL g_cs_inited;
static CRITICAL_SECTION g_cs;
static HANDLE g_file = INVALID_HANDLE_VALUE;

static BOOL IsHex32(const wchar_t *s, int n)
{
    if (n != 32)
        return FALSE;
    for (int i = 0; i < 32; ++i) {
        wchar_t c = s[i];
        BOOL hex = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') ||
                   (c >= L'A' && c <= L'F');
        if (!hex)
            return FALSE;
    }
    return TRUE;
}

/* Telegram bot API path segment: bot<digits>:<secret> */
static BOOL IsBotTokenSeg(const wchar_t *s, int n)
{
    if (n < 10 || s[0] != L'b' || s[1] != L'o' || s[2] != L't')
        return FALSE;
    int i = 3;
    if (i >= n || s[i] < L'0' || s[i] > L'9')
        return FALSE;
    while (i < n && s[i] >= L'0' && s[i] <= L'9')
        ++i;
    return i < n && s[i] == L':';
}

static BOOL ShouldRedactSeg(const wchar_t *s, int n)
{
    return IsHex32(s, n) || IsBotTokenSeg(s, n);
}

static BOOL CmdHasDebug(void)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return FALSE;
    BOOL found = FALSE;
    for (int i = 0; i < argc; ++i) {
        if (argv[i] && argv[i][0] == L'-' && argv[i][1] == L'-' &&
            argv[i][2] == L'd' && argv[i][3] == L'e' && argv[i][4] == L'b' &&
            argv[i][5] == L'u' && argv[i][6] == L'g' && argv[i][7] == 0) {
            found = TRUE;
            break;
        }
    }
    LocalFree(argv);
    return found;
}

static BOOL EnvHasDebug(void)
{
    wchar_t buf[8];
    DWORD n = GetEnvironmentVariableW(L"ILIZ_DEBUG", buf, 8);
    return n == 1 && buf[0] == L'1' && buf[1] == 0;
}

static void EnsureCs(void)
{
    if (g_cs_inited)
        return;
    InitializeCriticalSection(&g_cs);
    g_cs_inited = TRUE;
}

static void WriteRaw(const char *s, int len)
{
    if (!s || len <= 0)
        return;
    if (g_file != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        WriteFile(g_file, s, (DWORD)len, &wrote, 0);
        FlushFileBuffers(g_file);
    }
    OutputDebugStringA(s);
}

static void EmitLine(const char *tag, const char *msg)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[2048];
    int n = _snprintf(line, sizeof(line),
                      "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\r\n",
                      (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                      (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
                      (unsigned)st.wMilliseconds,
                      tag && tag[0] ? tag : "app",
                      msg ? msg : "");
    if (n < 0)
        n = (int)sizeof(line) - 1;
    if (n >= (int)sizeof(line))
        n = (int)sizeof(line) - 1;
    line[n] = 0;
    WriteRaw(line, n);
}

void DebugInit(void)
{
    EnsureCs();
    EnterCriticalSection(&g_cs);
    g_on = CmdHasDebug() || EnvHasDebug();
    if (!g_on) {
        LeaveCriticalSection(&g_cs);
        return;
    }
    wchar_t path[MAX_PATH];
    path[0] = 0;
    if (!GetModuleFileNameW(0, path, MAX_PATH)) {
        g_on = FALSE;
        LeaveCriticalSection(&g_cs);
        return;
    }
    int n = 0;
    while (path[n])
        ++n;
    while (n > 0 && path[n - 1] != L'\\' && path[n - 1] != L'/')
        --n;
    const wchar_t *name = L"debug.log";
    int i = 0;
    while (name[i] && n < MAX_PATH - 1)
        path[n++] = name[i++];
    path[n] = 0;

    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, 0);
    if (g_file == INVALID_HANDLE_VALUE) {
        g_on = FALSE;
        LeaveCriticalSection(&g_cs);
        return;
    }
    LeaveCriticalSection(&g_cs);
    DebugLog("app", "starting version=%s", APP_VERSION_A);
}

BOOL DebugEnabled(void)
{
    return g_on;
}

void DebugLog(const char *tag, const char *fmt, ...)
{
    if (!g_on)
        return;
    char msg[1536];
    msg[0] = 0;
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    va_end(ap);
    msg[sizeof(msg) - 1] = 0;
    EnsureCs();
    EnterCriticalSection(&g_cs);
    if (g_on)
        EmitLine(tag, msg);
    LeaveCriticalSection(&g_cs);
}

void DebugLogW(const char *tag, const wchar_t *fmt, ...)
{
    if (!g_on)
        return;
    wchar_t wmsg[1024];
    wmsg[0] = 0;
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(wmsg, 1024, fmt ? fmt : L"", ap);
    va_end(ap);
    wmsg[1023] = 0;
    char msg[1536];
    WideToUtf8(wmsg, msg, sizeof(msg));
    EnsureCs();
    EnterCriticalSection(&g_cs);
    if (g_on)
        EmitLine(tag, msg);
    LeaveCriticalSection(&g_cs);
}

const char *DebugRedactUrlW(const wchar_t *url, char *out, int cap)
{
    if (!out || cap < 1)
        return "";
    out[0] = 0;
    if (!url || !url[0])
        return out;

    wchar_t tmp[2048];
    CopyW(tmp, url, 2048);
    for (int i = 0; tmp[i]; ++i) {
        if (tmp[i] == L'?' || tmp[i] == L'#') {
            tmp[i] = 0;
            break;
        }
    }

    wchar_t red[2048];
    int ri = 0;
    const wchar_t *p = tmp;
    /* copy scheme:// if present */
    const wchar_t *scheme = 0;
    if (StartsW(p, L"https://"))
        scheme = L"https://";
    else if (StartsW(p, L"http://"))
        scheme = L"http://";
    if (scheme) {
        while (*scheme && ri < 2047)
            red[ri++] = *scheme++;
        p = tmp + (StartsW(tmp, L"https://") ? 8 : 7);
    }

    /* host (until /) */
    while (*p && *p != L'/' && ri < 2047)
        red[ri++] = *p++;

    /* path segments */
    while (*p && ri < 2047) {
        if (*p != L'/') {
            red[ri++] = *p++;
            continue;
        }
        red[ri++] = L'/';
        ++p;
        const wchar_t *seg = p;
        int sn = 0;
        while (p[sn] && p[sn] != L'/')
            ++sn;
        if (ShouldRedactSeg(seg, sn)) {
            if (IsBotTokenSeg(seg, sn)) {
                /* keep "bot" prefix, hide the rest */
                if (ri + 6 < 2047) {
                    red[ri++] = L'b';
                    red[ri++] = L'o';
                    red[ri++] = L't';
                    red[ri++] = L'*';
                    red[ri++] = L'*';
                    red[ri++] = L'*';
                }
            } else if (ri + 3 < 2047) {
                red[ri++] = L'*';
                red[ri++] = L'*';
                red[ri++] = L'*';
            }
            p += sn;
        } else {
            while (sn-- > 0 && ri < 2047)
                red[ri++] = *p++;
        }
    }
    red[ri] = 0;
    WideToUtf8(red, out, cap);
    return out;
}
