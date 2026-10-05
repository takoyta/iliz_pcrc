#define WIN32_LEAN_AND_MEAN
#include <string.h>
#include <windows.h>
#include "config.h"
#include "core.h"
#include "shot.h"
#include "text.h"

static char g_val[16];
static ULONGLONG g_until;
static LONGLONG g_chat;

static void ClearOut(CoreOut *out)
{
    memset(out, 0, sizeof(*out));
}

void CoreReset(void)
{
    g_val[0] = 0;
    g_until = 0;
    g_chat = 0;
}

void CoreFree(CoreOut *out)
{
    if (out && out->jpeg) {
        GlobalFree(out->jpeg);
        out->jpeg = 0;
        out->jpeg_len = 0;
    }
}

void CoreButton(const char *label, LONGLONG chat, CoreOut *out)
{
    ClearOut(out);
    CoreReset();
    if (!label || !label[0]) {
        out->kind = CORE_FAIL;
        CopyA(out->detail, "bad command", sizeof(out->detail));
        return;
    }
    ButtonDef btn;
    if (!ConfigFindCopy(label, &btn)) {
        out->kind = CORE_UNKNOWN;
        return;
    }
    char err[64];
    char reply[64];
    int kind = ConfigExecute(&btn, err, sizeof(err), reply, sizeof(reply));
    if (kind == CFG_EXEC_FAIL) {
        out->kind = CORE_FAIL;
        CopyA(out->detail, err[0] ? err : "command failed", sizeof(out->detail));
        return;
    }
    if (kind == CFG_EXEC_CONFIRM) {
        CopyA(g_val, btn.value, sizeof(g_val));
        g_chat = chat;
        g_until = GetTickCount64() + 30000;
        out->kind = CORE_CONFIRM;
        CopyA(out->detail, btn.value, sizeof(out->detail));
        return;
    }
    if (kind == CFG_EXEC_SHOT) {
        out->jpeg = ShotCaptureJpeg(&out->jpeg_len);
        if (!out->jpeg || !out->jpeg_len) {
            CoreFree(out);
            out->kind = CORE_FAIL;
            CopyA(out->detail, "shot failed", sizeof(out->detail));
            return;
        }
        out->kind = CORE_SHOT;
        return;
    }
    out->kind = CORE_OK;
    if (reply[0])
        CopyA(out->detail, reply, sizeof(out->detail));
}

void CoreAnswer(LONGLONG chat, BOOL yes, CoreOut *out)
{
    ClearOut(out);
    BOOL pending = g_val[0] && GetTickCount64() <= g_until && g_chat == chat;
    if (!pending || !yes) {
        CoreReset();
        out->kind = CORE_NOCONFIRM;
        return;
    }
    CopyA(out->detail, g_val, sizeof(out->detail));
    CoreReset();
    out->kind = CORE_OK;
}
