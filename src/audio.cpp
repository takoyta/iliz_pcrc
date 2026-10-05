#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "audio.h"

typedef unsigned long long QWORD;

typedef BOOL (WINAPI *BassInitFn)(int, DWORD, DWORD, HWND, const void *);
typedef BOOL (WINAPI *BassFreeFn)(void);
typedef DWORD (WINAPI *BassStreamFn)(DWORD, const void *, QWORD, QWORD, DWORD);
typedef BOOL (WINAPI *BassPlayFn)(DWORD, BOOL);
typedef BOOL (WINAPI *BassStreamFreeFn)(DWORD);

static HMODULE g_bass;
static HMODULE g_opus;
static BassInitFn g_init;
static BassFreeFn g_free;
static BassStreamFn g_stream;
static BassStreamFn g_opus_stream;
static BassPlayFn g_play;
static BassStreamFreeFn g_stream_free;
static BOOL g_inited;
static DWORD g_handle;

static BOOL ExtIs(const wchar_t *path, const wchar_t *ext)
{
    int plen = 0;
    int elen = 0;
    while (path[plen]) ++plen;
    while (ext[elen]) ++elen;
    if (plen < elen)
        return FALSE;
    for (int i = 0; i < elen; ++i) {
        wchar_t a = path[plen - elen + i];
        wchar_t b = ext[i];
        if (a >= L'A' && a <= L'Z')
            a = (wchar_t)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z')
            b = (wchar_t)(b - L'A' + L'a');
        if (a != b)
            return FALSE;
    }
    return TRUE;
}

static BOOL ExeSibling(wchar_t *dst, int cap, const wchar_t *name)
{
    DWORD n = GetModuleFileNameW(0, dst, cap);
    if (!n || n >= (DWORD)cap)
        return FALSE;
    int i = (int)n;
    while (i > 0 && dst[i - 1] != L'\\' && dst[i - 1] != L'/')
        --i;
    int k = 0;
    while (name[k] && i + k < cap - 1) {
        dst[i + k] = name[k];
        ++k;
    }
    if (name[k])
        return FALSE;
    dst[i + k] = 0;
    return TRUE;
}

static void StopHandle()
{
    if (g_handle && g_stream_free)
        g_stream_free(g_handle);
    g_handle = 0;
}

static BOOL EnsureBass()
{
    if (g_bass)
        return g_init && g_stream && g_play && g_stream_free;
    wchar_t path[MAX_PATH];
    if (!ExeSibling(path, MAX_PATH, L"bass.dll"))
        return FALSE;
    g_bass = LoadLibraryW(path);
    if (!g_bass)
        return FALSE;
    g_init = (BassInitFn)GetProcAddress(g_bass, "BASS_Init");
    g_free = (BassFreeFn)GetProcAddress(g_bass, "BASS_Free");
    g_stream = (BassStreamFn)GetProcAddress(g_bass, "BASS_StreamCreateFile");
    g_play = (BassPlayFn)GetProcAddress(g_bass, "BASS_ChannelPlay");
    g_stream_free = (BassStreamFreeFn)GetProcAddress(g_bass, "BASS_StreamFree");
    if (!g_init || !g_stream || !g_play || !g_stream_free)
        return FALSE;
    if (ExeSibling(path, MAX_PATH, L"bassopus.dll")) {
        g_opus = LoadLibraryW(path);
        if (g_opus)
            g_opus_stream = (BassStreamFn)GetProcAddress(g_opus, "BASS_OPUS_StreamCreateFile");
    }
    return TRUE;
}

void AudioShutdown()
{
    StopHandle();
    if (g_inited && g_free)
        g_free();
    g_inited = FALSE;
    if (g_opus) {
        FreeLibrary(g_opus);
        g_opus = 0;
    }
    g_opus_stream = 0;
    if (g_bass) {
        FreeLibrary(g_bass);
        g_bass = 0;
    }
    g_init = 0;
    g_free = 0;
    g_stream = 0;
    g_play = 0;
    g_stream_free = 0;
}

BOOL AudioPlaySibling(const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    if (!name || !name[0] || !ExeSibling(path, MAX_PATH, name))
        return FALSE;
    return AudioPlayFile(path);
}

BOOL AudioPlayFile(const wchar_t *path)
{
    if (!path || !path[0] || !EnsureBass())
        return FALSE;
    StopHandle();
    if (!g_inited) {
        if (!g_init(-1, 44100, 0, 0, 0))
            return FALSE;
        g_inited = TRUE;
    }

    BOOL opus_name = ExtIs(path, L".oga") || ExtIs(path, L".ogg") || ExtIs(path, L".opus");
    DWORD handle = 0;
    if (opus_name && g_opus_stream)
        handle = g_opus_stream(0, path, 0, 0, 0x80000000);
    if (!handle)
        handle = g_stream(0, path, 0, 0, 0x80000000);
    if (!handle && !opus_name && g_opus_stream)
        handle = g_opus_stream(0, path, 0, 0, 0x80000000);
    if (!handle)
        return FALSE;
    if (!g_play(handle, FALSE)) {
        g_stream_free(handle);
        return FALSE;
    }
    g_handle = handle;
    return TRUE;
}
