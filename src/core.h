#pragma once
#include <windows.h>

#define CORE_OK        0
#define CORE_CONFIRM   1
#define CORE_SHOT      2
#define CORE_UNKNOWN   3
#define CORE_FAIL      4
#define CORE_NOCONFIRM 5

struct CoreOut {
    int kind;
    char detail[64];
    HGLOBAL jpeg;
    DWORD jpeg_len;
};

void CoreReset(void);
void CoreButton(const char *label, LONGLONG chat, CoreOut *out);
void CoreAnswer(LONGLONG chat, BOOL yes, CoreOut *out);
void CoreFree(CoreOut *out);
