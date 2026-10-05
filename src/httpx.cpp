#define WIN32_LEAN_AND_MEAN
#include <string.h>
#include <windows.h>
#include <winhttp.h>
#include "appinfo.h"
#include "debug.h"
#include "httpx.h"
#include "text.h"

void HttpJobInit(HttpJob *job)
{
    if (job->inited)
        return;
    InitializeCriticalSection(&job->cs);
    job->req = 0;
    job->abort = 0;
    job->inited = TRUE;
}

void HttpJobFree(HttpJob *job)
{
    if (!job->inited)
        return;
    HttpJobAbort(job);
    DeleteCriticalSection(&job->cs);
    job->inited = FALSE;
}

void HttpJobReset(HttpJob *job)
{
    HttpJobInit(job);
    EnterCriticalSection(&job->cs);
    job->abort = 0;
    LeaveCriticalSection(&job->cs);
}

void HttpJobAbort(HttpJob *job)
{
    if (!job->inited)
        return;
    EnterCriticalSection(&job->cs);
    job->abort = 1;
    HINTERNET req = job->req;
    job->req = 0;
    LeaveCriticalSection(&job->cs);
    if (req)
        WinHttpCloseHandle(req);
}

struct UrlParts {
    BOOL https;
    wchar_t host[256];
    INTERNET_PORT port;
    wchar_t path[2048];
};

static BOOL SplitUrl(const wchar_t *url, UrlParts *u)
{
    memset(u, 0, sizeof(*u));
    if (!url || !url[0])
        return FALSE;
    const wchar_t *p = url;
    if (StartsW(url, L"https://")) {
        u->https = TRUE;
        p = url + 8;
        u->port = 443;
    } else if (StartsW(url, L"http://")) {
        p = url + 7;
        u->port = 80;
    } else {
        return FALSE;
    }
    int i = 0;
    while (*p && *p != L'/' && *p != L':' && i < 255)
        u->host[i++] = *p++;
    u->host[i] = 0;
    if (!u->host[0])
        return FALSE;
    if (*p == L':') {
        ++p;
        int v = 0;
        if (*p < L'0' || *p > L'9')
            return FALSE;
        while (*p >= L'0' && *p <= L'9') {
            v = v * 10 + (*p - L'0');
            ++p;
            if (v > 65535)
                return FALSE;
        }
        if (v < 1)
            return FALSE;
        u->port = (INTERNET_PORT)v;
    }
    if (*p == 0)
        CopyW(u->path, L"/", 2048);
    else
        CopyW(u->path, p, 2048);
    return TRUE;
}

static BOOL Begin(HttpJob *job, const wchar_t *url, const wchar_t *method, int timeout_ms,
                  HINTERNET *ses, HINTERNET *con, HINTERNET *req, UrlParts *parts)
{
    *ses = 0;
    *con = 0;
    *req = 0;
    if (!SplitUrl(url, parts))
        return FALSE;
    HttpJobInit(job);
    *ses = WinHttpOpen(L"iliz_pcrc/" APP_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!*ses)
        return FALSE;
    int t = timeout_ms > 0 ? timeout_ms : 15000;
    WinHttpSetTimeouts(*ses, 5000, 5000, 15000, t);
    *con = WinHttpConnect(*ses, parts->host, parts->port, 0);
    if (!*con) {
        WinHttpCloseHandle(*ses);
        *ses = 0;
        return FALSE;
    }
    DWORD flags = parts->https ? WINHTTP_FLAG_SECURE : 0;
    *req = WinHttpOpenRequest(*con, method, parts->path, 0, WINHTTP_NO_REFERER,
                              WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!*req) {
        WinHttpCloseHandle(*con);
        WinHttpCloseHandle(*ses);
        *con = 0;
        *ses = 0;
        return FALSE;
    }
    EnterCriticalSection(&job->cs);
    if (job->abort) {
        LeaveCriticalSection(&job->cs);
        WinHttpCloseHandle(*req);
        WinHttpCloseHandle(*con);
        WinHttpCloseHandle(*ses);
        *req = 0;
        *con = 0;
        *ses = 0;
        return FALSE;
    }
    job->req = *req;
    LeaveCriticalSection(&job->cs);
    return TRUE;
}

static void End(HttpJob *job, HINTERNET ses, HINTERNET con, HINTERNET req)
{
    EnterCriticalSection(&job->cs);
    BOOL own = (job->req == req);
    if (own)
        job->req = 0;
    LeaveCriticalSection(&job->cs);
    if (own && req)
        WinHttpCloseHandle(req);
    if (con)
        WinHttpCloseHandle(con);
    if (ses)
        WinHttpCloseHandle(ses);
}

static int QueryStatus(HINTERNET req)
{
    DWORD status = 0;
    DWORD sz = sizeof(status);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX))
        return 0;
    return (int)status;
}

static void LogHttpOut(const char *method, const wchar_t *url)
{
    if (!DebugEnabled())
        return;
    char red[512];
    DebugRedactUrlW(url, red, sizeof(red));
    DebugLog("http", "-> %s %s", method, red);
}

static void LogHttpResult(BOOL ok, int status, int bytes)
{
    if (!DebugEnabled())
        return;
    if (status > 0)
        DebugLog("http", "<- ok=%d status=%d bytes=%d", (int)ok, status, bytes);
    else
        DebugLog("http", "<- ok=%d bytes=%d", (int)ok, bytes);
}

BOOL HttpExchange(HttpJob *job, const wchar_t *url, const char *post, int timeout_ms,
                  char *out, int outCap, int *outLen)
{
    *outLen = 0;
    if (outCap > 0)
        out[0] = 0;
    LogHttpOut(post ? "POST" : "GET", url);
    HINTERNET ses, con, req;
    UrlParts parts;
    if (!Begin(job, url, post ? L"POST" : L"GET", timeout_ms, &ses, &con, &req, &parts)) {
        LogHttpResult(FALSE, 0, 0);
        return FALSE;
    }
    BOOL ok = FALSE;
    if (post) {
        int len = LenA(post);
        ok = WinHttpSendRequest(req, L"Content-Type: application/json\r\n", (DWORD)-1L,
                                (LPVOID)post, (DWORD)len, (DWORD)len, 0);
    } else {
        ok = WinHttpSendRequest(req, 0, 0, 0, 0, 0, 0);
    }
    if (ok)
        ok = WinHttpReceiveResponse(req, 0);
    int status = 0;
    if (ok)
        status = QueryStatus(req);
    if (ok) {
        int used = 0;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || !avail)
                break;
            if (used >= outCap - 1)
                break;
            DWORD space = (DWORD)(outCap - 1 - used);
            if (avail > space)
                avail = space;
            DWORD got = 0;
            if (!WinHttpReadData(req, out + used, avail, &got) || !got)
                break;
            used += (int)got;
        }
        out[used] = 0;
        *outLen = used;
        ok = used > 0;
        LogHttpResult(ok, status, used);
    } else {
        LogHttpResult(FALSE, status, 0);
    }
    End(job, ses, con, req);
    return ok;
}

BOOL HttpDownload(HttpJob *job, const wchar_t *url, const wchar_t *dest, int timeout_ms)
{
    LogHttpOut("GET", url);
    HINTERNET ses, con, req;
    UrlParts parts;
    if (!Begin(job, url, L"GET", timeout_ms, &ses, &con, &req, &parts)) {
        LogHttpResult(FALSE, 0, 0);
        return FALSE;
    }
    BOOL ok = WinHttpSendRequest(req, 0, 0, 0, 0, 0, 0) && WinHttpReceiveResponse(req, 0);
    int status = 0;
    if (ok) {
        status = QueryStatus(req);
        ok = (status == 200);
    }
    HANDLE file = INVALID_HANDLE_VALUE;
    if (ok) {
        file = CreateFileW(dest, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        ok = (file != INVALID_HANDLE_VALUE);
    }
    DWORD total = 0;
    if (ok) {
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) {
                ok = FALSE;
                break;
            }
            if (!avail)
                break;
            BYTE buf[8192];
            if (avail > sizeof(buf))
                avail = sizeof(buf);
            DWORD got = 0;
            if (!WinHttpReadData(req, buf, avail, &got) || !got) {
                ok = FALSE;
                break;
            }
            total += got;
            if (total > 20 * 1024 * 1024) {
                ok = FALSE;
                break;
            }
            DWORD wrote = 0;
            if (!WriteFile(file, buf, got, &wrote, 0) || wrote != got) {
                ok = FALSE;
                break;
            }
        }
        ok = ok && total > 0;
    }
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (!ok)
        DeleteFileW(dest);
    LogHttpResult(ok, status, (int)total);
    End(job, ses, con, req);
    return ok;
}

BOOL HttpPostRaw(HttpJob *job, const wchar_t *url, const wchar_t *headers, const void *body, int bodyLen,
                 int timeout_ms, int *status)
{
    if (status)
        *status = 0;
    LogHttpOut("POST", url);
    HINTERNET ses, con, req;
    UrlParts parts;
    if (!Begin(job, url, L"POST", timeout_ms, &ses, &con, &req, &parts)) {
        LogHttpResult(FALSE, 0, 0);
        return FALSE;
    }
    BOOL ok = WinHttpSendRequest(req, headers, (DWORD)-1L, (LPVOID)body, (DWORD)bodyLen, (DWORD)bodyLen, 0);
    if (ok)
        ok = WinHttpReceiveResponse(req, 0);
    int code = 0;
    if (ok) {
        code = QueryStatus(req);
        if (status)
            *status = code;
        ok = (code >= 200 && code < 300);
    }
    LogHttpResult(ok, code, bodyLen);
    End(job, ses, con, req);
    return ok;
}
