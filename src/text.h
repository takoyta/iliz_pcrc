#pragma once
#include <windows.h>

inline int LenA(const char *s)
{
    int n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}

inline int LenW(const wchar_t *s)
{
    int n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}

inline void CopyA(char *dst, const char *src, int cap)
{
    int i = 0;
    if (!src)
        src = "";
    if (cap < 1)
        return;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

inline void CopyW(wchar_t *dst, const wchar_t *src, int cap)
{
    int i = 0;
    if (!src)
        src = L"";
    if (cap < 1)
        return;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

inline void AppendA(char *dst, int cap, const char *src)
{
    int i = 0;
    if (!src || cap < 1)
        return;
    while (dst[i] && i < cap - 1)
        ++i;
    while (*src && i < cap - 1)
        dst[i++] = *src++;
    dst[i] = 0;
}

inline void AppendW(wchar_t *dst, int cap, const wchar_t *src)
{
    int i = 0;
    if (!src || cap < 1)
        return;
    while (dst[i] && i < cap - 1)
        ++i;
    while (*src && i < cap - 1)
        dst[i++] = *src++;
    dst[i] = 0;
}

inline BOOL EqA(const char *a, const char *b)
{
    if (!a || !b)
        return FALSE;
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

inline BOOL StartsA(const char *s, const char *p)
{
    if (!s || !p)
        return FALSE;
    while (*p) {
        if (*s != *p)
            return FALSE;
        ++s;
        ++p;
    }
    return TRUE;
}

inline BOOL StartsW(const wchar_t *s, const wchar_t *p)
{
    if (!s || !p)
        return FALSE;
    while (*p) {
        if (*s != *p)
            return FALSE;
        ++s;
        ++p;
    }
    return TRUE;
}

inline BOOL WideToUtf8(const wchar_t *w, char *out, int cap)
{
    if (!out || cap < 1)
        return FALSE;
    out[0] = 0;
    if (!w)
        return FALSE;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, 0, 0);
    if (n <= 0) {
        out[0] = 0;
        return FALSE;
    }
    return TRUE;
}

inline BOOL Utf8ToWide(const char *s, wchar_t *out, int cap)
{
    if (!out || cap < 1)
        return FALSE;
    out[0] = 0;
    if (!s)
        return FALSE;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
    if (n <= 0) {
        out[0] = 0;
        return FALSE;
    }
    out[cap - 1] = 0;
    return TRUE;
}

inline void AppendUrlEnc(char *dst, int cap, const char *src, BOOL keepSlash)
{
    static const char hex[] = "0123456789ABCDEF";
    int i = 0;
    if (!dst || !src || cap < 1)
        return;
    while (dst[i] && i < cap - 1)
        ++i;
    while (*src && i < cap - 1) {
        unsigned char c = (unsigned char)*src++;
        BOOL raw = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' ||
                   (keepSlash && c == '/');
        if (raw) {
            dst[i++] = (char)c;
        } else if (i + 3 < cap) {
            dst[i++] = '%';
            dst[i++] = hex[c >> 4];
            dst[i++] = hex[c & 15];
        }
    }
    dst[i] = 0;
}

inline void PutIA(char *dst, int cap, int v)
{
    char tmp[16];
    int n = 0;
    if (cap < 1)
        return;
    if (v < 0)
        v = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v && n < 15) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    int i = 0;
    while (n && i < cap - 1)
        dst[i++] = tmp[--n];
    dst[i] = 0;
}

inline void PutIW(wchar_t *dst, int cap, int v)
{
    wchar_t tmp[16];
    int n = 0;
    if (cap < 1)
        return;
    if (v < 0)
        v = 0;
    if (v == 0)
        tmp[n++] = L'0';
    while (v && n < 15) {
        tmp[n++] = (wchar_t)(L'0' + (v % 10));
        v /= 10;
    }
    int i = 0;
    while (n && i < cap - 1)
        dst[i++] = tmp[--n];
    dst[i] = 0;
}

inline void PutI64(char *dst, int cap, LONGLONG v)
{
    char tmp[32];
    int n = 0;
    BOOL neg = v < 0;
    if (cap < 1)
        return;
    if (neg)
        v = -v;
    if (v == 0)
        tmp[n++] = '0';
    while (v > 0 && n < 30) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    int i = 0;
    if (neg && i < cap - 1)
        dst[i++] = '-';
    while (n && i < cap - 1)
        dst[i++] = tmp[--n];
    dst[i] = 0;
}

inline void TrimSpaceW(wchar_t *s)
{
    int i = 0;
    if (!s)
        return;
    while (s[i] == L' ' || s[i] == L'\t')
        ++i;
    int j = 0;
    while (s[i])
        s[j++] = s[i++];
    while (j > 0 && (s[j - 1] == L' ' || s[j - 1] == L'\t' || s[j - 1] == L'\r' || s[j - 1] == L'\n'))
        --j;
    s[j] = 0;
}

inline void TrimUrlW(wchar_t *s)
{
    TrimSpaceW(s);
    int j = LenW(s);
    while (j > 0 && s[j - 1] == L'/')
        s[--j] = 0;
}

inline BOOL TextIsYes(const char *s)
{
    return EqA(s, "yes") || EqA(s, "Yes") || EqA(s, "YES") || EqA(s, "/yes");
}
