#pragma once
#include <windows.h>

void DebugInit(void);
BOOL DebugEnabled(void);
void DebugLog(const char *tag, const char *fmt, ...);
void DebugLogW(const char *tag, const wchar_t *fmt, ...);

/* Redact URL into UTF-8 out: keep scheme+host+path, strip query/fragment,
   replace any exact 32-hex path segment with "***". Returns out. */
const char *DebugRedactUrlW(const wchar_t *url, char *out, int cap);
