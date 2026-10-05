#pragma once
#include <windows.h>
#include <winhttp.h>

struct HttpJob {
    CRITICAL_SECTION cs;
    HINTERNET req;
    volatile LONG abort;
    BOOL inited;
};

void HttpJobInit(HttpJob *job);
void HttpJobFree(HttpJob *job);
void HttpJobReset(HttpJob *job);
void HttpJobAbort(HttpJob *job);

BOOL HttpExchange(HttpJob *job, const wchar_t *url, const char *post, int timeout_ms,
                  char *out, int outCap, int *outLen);
BOOL HttpDownload(HttpJob *job, const wchar_t *url, const wchar_t *dest, int timeout_ms);
BOOL HttpPostRaw(HttpJob *job, const wchar_t *url, const wchar_t *headers, const void *body, int bodyLen,
                 int timeout_ms, int *status);
