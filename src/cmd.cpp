#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "cmd.h"
#include "text.h"

#define CMD_MAX_STMT 32
#define CMD_MAX_ARGS 2

#define ACT_POS    0
#define ACT_CLICK  1
#define ACT_DOWN   2
#define ACT_UP     3
#define ACT_PING   4
#define ACT_SCROLL 5
#define ACT_TYPE   6

#define SCROLL_MAX 120

#define MOUSE_LEFT  0
#define MOUSE_RIGHT 1

struct CmdSpec {
    const char *name;
    int argcMask;
    int act;
    int button;
};

struct Stmt {
    const CmdSpec *spec;
    int argc;
    int args[CMD_MAX_ARGS];
    const char *text;
};

static const CmdSpec kCmds[] = {
    { "ping", 1 << 0, ACT_PING, 0 },
    { "setMousePos", 1 << 2, ACT_POS, 0 },
    { "leftClick", (1 << 0) | (1 << 2), ACT_CLICK, MOUSE_LEFT },
    { "leftDown", (1 << 0) | (1 << 2), ACT_DOWN, MOUSE_LEFT },
    { "leftUp", (1 << 0) | (1 << 2), ACT_UP, MOUSE_LEFT },
    { "rightClick", (1 << 0) | (1 << 2), ACT_CLICK, MOUSE_RIGHT },
    { "rightDown", (1 << 0) | (1 << 2), ACT_DOWN, MOUSE_RIGHT },
    { "rightUp", (1 << 0) | (1 << 2), ACT_UP, MOUSE_RIGHT },
    { "scroll", 1 << 1, ACT_SCROLL, 0 },
    { "type", 0, ACT_TYPE, 0 },
};

static void Fail(CmdOut *out, const char *msg)
{
    out->status = CMD_FAIL;
    CopyA(out->error, msg, sizeof(out->error));
}

static const char *SkipGap(const char *p)
{
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r')
            ++p;
        if (*p != ';' && *p != '\n')
            return p;
        ++p;
    }
}

static const CmdSpec *FindCmd(const char *name, int nlen)
{
    for (int i = 0; i < (int)(sizeof(kCmds) / sizeof(kCmds[0])); ++i) {
        const char *s = kCmds[i].name;
        int j = 0;
        while (j < nlen && s[j] && name[j] == s[j])
            ++j;
        if (j == nlen && !s[j])
            return &kCmds[i];
    }
    return 0;
}

static BOOL ParseInt(const char **pp, int *out)
{
    const char *p = *pp;
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        ++p;
    }
    if (*p < '0' || *p > '9')
        return FALSE;
    long long v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 2147483647LL)
            return FALSE;
        ++p;
    }
    if (sign < 0)
        v = -v;
    *out = (int)v;
    *pp = p;
    return TRUE;
}

static BOOL ParseString(const char **pp, char *scratch, int cap, int *used, const char **out)
{
    const char *p = *pp;
    if (*p != '"')
        return FALSE;
    ++p;
    int start = *used;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            if (*p == '\\' || *p == '"')
                c = *p++;
            else if (*p == 'n') {
                c = '\n';
                ++p;
            } else if (*p == 't') {
                c = '\t';
                ++p;
            } else
                return FALSE;
        }
        if (*used >= cap - 1)
            return FALSE;
        scratch[(*used)++] = c;
    }
    if (*p != '"')
        return FALSE;
    ++p;
    scratch[(*used)++] = 0;
    *out = scratch + start;
    *pp = p;
    return TRUE;
}

static BOOL Parse(const char *text, Stmt *stmts, int *count, CmdOut *out, char *scratch, int scratchCap)
{
    const char *p = text ? text : "";
    int n = 0;
    int used = 0;
    out->status = CMD_OK;
    out->error[0] = 0;
    for (;;) {
        p = SkipGap(p);
        if (!*p)
            break;
        if (n >= CMD_MAX_STMT) {
            Fail(out, "bad command");
            return FALSE;
        }
        const char *name = p;
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_')) {
            if (n == 0) {
                out->status = CMD_NONE;
                return FALSE;
            }
            Fail(out, "bad command");
            return FALSE;
        }
        ++p;
        while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
               (*p >= '0' && *p <= '9') || *p == '_')
            ++p;
        int nlen = (int)(p - name);
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p != '(') {
            if (n == 0) {
                out->status = CMD_NONE;
                return FALSE;
            }
            Fail(out, "bad command");
            return FALSE;
        }
        const CmdSpec *spec = FindCmd(name, nlen);
        if (!spec) {
            Fail(out, "unknown command");
            return FALSE;
        }
        ++p;
        Stmt *st = &stmts[n];
        st->spec = spec;
        st->argc = 0;
        st->text = "";
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            ++p;
        if (spec->act == ACT_TYPE) {
            if (!ParseString(&p, scratch, scratchCap, &used, &st->text)) {
                Fail(out, "bad args");
                return FALSE;
            }
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
                ++p;
            if (*p != ')') {
                Fail(out, "bad args");
                return FALSE;
            }
        } else if (*p != ')') {
            for (;;) {
                if (st->argc >= CMD_MAX_ARGS) {
                    Fail(out, "bad args");
                    return FALSE;
                }
                if (!ParseInt(&p, &st->args[st->argc])) {
                    Fail(out, "bad args");
                    return FALSE;
                }
                ++st->argc;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
                    ++p;
                if (*p == ')')
                    break;
                if (*p != ',') {
                    Fail(out, "bad args");
                    return FALSE;
                }
                ++p;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
                    ++p;
            }
        }
        if (spec->act != ACT_TYPE && ((1 << st->argc) & spec->argcMask) == 0) {
            Fail(out, "bad args");
            return FALSE;
        }
        if (spec->act == ACT_SCROLL && (st->args[0] < -SCROLL_MAX || st->args[0] > SCROLL_MAX)) {
            Fail(out, "bad args");
            return FALSE;
        }
        ++p;
        ++n;
        while (*p == ' ' || *p == '\t' || *p == '\r')
            ++p;
        if (*p && *p != ';' && *p != '\n') {
            Fail(out, "bad command");
            return FALSE;
        }
    }
    *count = n;
    if (n == 0) {
        out->status = CMD_NONE;
        return FALSE;
    }
    return TRUE;
}

static DWORD ButtonFlag(int button, BOOL up)
{
    if (button == MOUSE_RIGHT)
        return up ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN;
    return up ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
}

static BOOL MouseInput(int button, int act, CmdOut *out)
{
    INPUT in[2];
    memset(in, 0, sizeof(in));
    int n = 0;
    if (act == ACT_CLICK || act == ACT_DOWN) {
        in[n].type = INPUT_MOUSE;
        in[n].mi.dwFlags = ButtonFlag(button, FALSE);
        ++n;
    }
    if (act == ACT_CLICK || act == ACT_UP) {
        in[n].type = INPUT_MOUSE;
        in[n].mi.dwFlags = ButtonFlag(button, TRUE);
        ++n;
    }
    if (SendInput((UINT)n, in, sizeof(INPUT)) != (UINT)n) {
        Fail(out, "mouse failed");
        return FALSE;
    }
    return TRUE;
}

static BOOL ScrollWheel(int notches, CmdOut *out)
{
    int step = notches > 0 ? 1 : -1;
    int left = notches > 0 ? notches : -notches;
    while (left > 0) {
        INPUT in;
        memset(&in, 0, sizeof(in));
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = (DWORD)(step * WHEEL_DELTA);
        if (SendInput(1, &in, sizeof(INPUT)) != 1) {
            Fail(out, "mouse failed");
            return FALSE;
        }
        --left;
    }
    return TRUE;
}

static BOOL KeyVk(WORD vk, CmdOut *out)
{
    INPUT in[2];
    memset(in, 0, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(2, in, sizeof(INPUT)) != 2) {
        Fail(out, "type failed");
        return FALSE;
    }
    return TRUE;
}

static BOOL TypeText(const char *text, CmdOut *out)
{
    if (!text || !text[0])
        return TRUE;
    wchar_t w[1024];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, w, 1024);
    if (n <= 1) {
        Fail(out, "type failed");
        return FALSE;
    }
    --n;
    for (int i = 0; i < n; ++i) {
        if (w[i] == L'\r')
            continue;
        if (w[i] == L'\n') {
            if (!KeyVk(VK_RETURN, out))
                return FALSE;
            continue;
        }
        if (w[i] == L'\t') {
            if (!KeyVk(VK_TAB, out))
                return FALSE;
            continue;
        }
        INPUT in[2];
        memset(in, 0, sizeof(in));
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wScan = w[i];
        in[0].ki.dwFlags = KEYEVENTF_UNICODE;
        in[1].type = INPUT_KEYBOARD;
        in[1].ki.wScan = w[i];
        in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        if (SendInput(2, in, sizeof(INPUT)) != 2) {
            Fail(out, "type failed");
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL Exec(const Stmt *st, CmdOut *out)
{
    if (st->spec->act == ACT_PING) {
        CopyA(out->reply, "pong", sizeof(out->reply));
        return TRUE;
    }
    if (st->spec->act == ACT_TYPE)
        return TypeText(st->text, out);
    if (st->spec->act == ACT_SCROLL)
        return ScrollWheel(st->args[0], out);
    if (st->argc == 2 && !SetCursorPos(st->args[0], st->args[1])) {
        Fail(out, "set cursor failed");
        return FALSE;
    }
    if (st->spec->act == ACT_POS)
        return TRUE;
    return MouseInput(st->spec->button, st->spec->act, out);
}

void CmdRun(const char *text, CmdOut *out)
{
    Stmt stmts[CMD_MAX_STMT];
    char scratch[2048];
    int n = 0;
    if (!out)
        return;
    out->reply[0] = 0;
    if (!Parse(text, stmts, &n, out, scratch, (int)sizeof(scratch)))
        return;
    for (int i = 0; i < n; ++i) {
        if (!Exec(&stmts[i], out))
            return;
    }
    out->status = CMD_OK;
    out->error[0] = 0;
}

BOOL CmdCheck(const char *text, char *err, int cap)
{
    Stmt stmts[CMD_MAX_STMT];
    char scratch[2048];
    int n = 0;
    CmdOut out;
    BOOL ok = Parse(text, stmts, &n, &out, scratch, (int)sizeof(scratch));
    if (err && cap > 0)
        CopyA(err, ok ? "" : (out.error[0] ? out.error : "bad command"), cap);
    return ok;
}
