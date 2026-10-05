#define WIN32_LEAN_AND_MEAN
#include <string.h>
#include <windows.h>
#include "audio.h"
#include "cJSON.h"
#include "cmd.h"
#include "config.h"
#include "core.h"
#include "httpx.h"
#include "own.h"
#include "settings.h"
#include "text.h"
#include "ui.h"

#define BODY_MAX 65536

static HttpJob g_http;
static HANDLE g_thread;
static HANDLE g_stop;
static volatile LONG g_run;
static volatile LONG g_gen;
static wchar_t g_token[SET_BOT_MAX];
static wchar_t g_allowed[SET_ALLOW_MAX];
static char g_body[BODY_MAX];
static char g_meta[8192];
static char g_send[4096];

static BOOL IsAllowed(LONGLONG id)
{
    const wchar_t *p = g_allowed;
    if (!p[0])
        return FALSE;
    while (*p) {
        while (*p == L' ' || *p == L',')
            ++p;
        if (!*p)
            break;
        BOOL neg = FALSE;
        if (*p == L'-') {
            neg = TRUE;
            ++p;
        }
        if (*p < L'0' || *p > L'9') {
            while (*p && *p != L',')
                ++p;
            continue;
        }
        LONGLONG v = 0;
        while (*p >= L'0' && *p <= L'9') {
            v = v * 10 + (*p - L'0');
            ++p;
        }
        if (neg)
            v = -v;
        if (v == id)
            return TRUE;
    }
    return FALSE;
}

static void BotUrl(wchar_t *out, int cap, const char *method)
{
    char token[SET_BOT_MAX];
    WideToUtf8(g_token, token, sizeof(token));
    char path[2048];
    path[0] = 0;
    AppendA(path, sizeof(path), "https://api.telegram.org/bot");
    AppendA(path, sizeof(path), token);
    AppendA(path, sizeof(path), "/");
    AppendA(path, sizeof(path), method);
    Utf8ToWide(path, out, cap);
}

static BOOL JsonOk(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    BOOL ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok"));
    cJSON_Delete(root);
    return ok;
}

static BOOL SendText(LONGLONG chat, const char *text, BOOL keyboard)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "chat_id", (double)chat);
    cJSON_AddStringToObject(obj, "text", text ? text : "");
    if (keyboard)
        ConfigAddReplyKeyboard(obj);
    char *body = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!body)
        return FALSE;
    wchar_t url[2048];
    BotUrl(url, 2048, "sendMessage");
    int n = 0;
    BOOL ok = HttpExchange(&g_http, url, body, 15000, g_send, sizeof(g_send), &n) && JsonOk(g_send);
    cJSON_free(body);
    return ok;
}

static BOOL SendShot(LONGLONG chat, HGLOBAL jpeg, DWORD jsize)
{
    if (!jpeg || !jsize)
        return FALSE;
    void *bytes = GlobalLock(jpeg);
    if (!bytes)
        return FALSE;
    char head[384];
    char id[32];
    PutI64(id, sizeof(id), chat);
    head[0] = 0;
    AppendA(head, sizeof(head),
            "--ilizpcshot\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n");
    AppendA(head, sizeof(head), id);
    AppendA(head, sizeof(head),
            "\r\n--ilizpcshot\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"screen.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n");
    const char *tail = "\r\n--ilizpcshot--\r\n";
    int hlen = LenA(head);
    int tlen = LenA(tail);
    DWORD total = (DWORD)hlen + jsize + (DWORD)tlen;
    char *body = (char *)HeapAlloc(GetProcessHeap(), 0, total);
    if (!body) {
        GlobalUnlock(jpeg);
        return FALSE;
    }
    memcpy(body, head, (size_t)hlen);
    memcpy(body + hlen, bytes, jsize);
    memcpy(body + hlen + jsize, tail, (size_t)tlen);
    GlobalUnlock(jpeg);

    wchar_t url[2048];
    BotUrl(url, 2048, "sendPhoto");
    int status = 0;
    BOOL ok = HttpPostRaw(&g_http, url,
                          L"Content-Type: multipart/form-data; boundary=ilizpcshot\r\n",
                          body, (int)total, 20000, &status);
    HeapFree(GetProcessHeap(), 0, body);
    return ok;
}

static void HandleText(LONGLONG chat, LONGLONG from, const char *text)
{
    BOOL allowed = IsAllowed(from);
    if (EqA(text, "/start") || EqA(text, "/keyboard") || EqA(text, "/help")) {
        AppFlash();
        char msg[96];
        char id[32];
        msg[0] = 0;
        PutI64(id, sizeof(id), from);
        AppendA(msg, sizeof(msg), "Your ID: ");
        AppendA(msg, sizeof(msg), id);
        if (allowed)
            AppendA(msg, sizeof(msg), "\nReady.");
        else
            AppendA(msg, sizeof(msg), "\nNot authorized.");
        SendText(chat, msg, allowed);
        return;
    }
    if (!allowed)
        return;

    if (TextIsYes(text)) {
        CoreOut out;
        CoreAnswer(chat, TRUE, &out);
        if (out.kind == CORE_OK) {
            AppFlash();
            SendText(chat, EqA(out.detail, "restart") ? "Restarting." : "Shutting down.", FALSE);
            ConfigApplyPower(out.detail);
            return;
        }
    }

    CoreOut out;
    CoreButton(text, chat, &out);
    if (out.kind == CORE_UNKNOWN) {
        CoreFree(&out);
        CmdOut cmd;
        CmdRun(text, &cmd);
        if (cmd.status == CMD_NONE)
            return;
        AppFlash();
        if (cmd.status != CMD_OK)
            SendText(chat, cmd.error[0] ? cmd.error : "Command failed.", FALSE);
        else if (cmd.reply[0])
            SendText(chat, cmd.reply, FALSE);
        return;
    }
    if (out.kind == CORE_OK) {
        AppFlash();
        if (out.detail[0])
            SendText(chat, out.detail, FALSE);
        CoreFree(&out);
        return;
    }
    AppFlash();
    if (out.kind == CORE_CONFIRM) {
        if (EqA(out.detail, "restart"))
            SendText(chat, "Restart this PC? Reply Yes within 30s.", FALSE);
        else
            SendText(chat, "Shut down this PC? Reply Yes within 30s.", FALSE);
        return;
    }
    if (out.kind == CORE_SHOT) {
        if (!SendShot(chat, out.jpeg, out.jpeg_len))
            SendText(chat, "Screenshot failed.", FALSE);
        CoreFree(&out);
        return;
    }
    SendText(chat, out.detail[0] ? out.detail : "Command failed.", FALSE);
    CoreFree(&out);
}

static BOOL MediaFileId(const cJSON *msg, const char *key, char *out, int cap)
{
    cJSON *obj = cJSON_GetObjectItemCaseSensitive(msg, key);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "file_id");
    if (!cJSON_IsString(id) || !id->valuestring || !id->valuestring[0])
        return FALSE;
    CopyA(out, id->valuestring, cap);
    return out[0] != 0;
}

static BOOL AudioTemp(wchar_t *dst, int cap, const char *remote)
{
    wchar_t dir[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, dir);
    if (!n || n >= MAX_PATH)
        return FALSE;
    const char *dot = 0;
    for (const char *p = remote; *p; ++p) {
        if (*p == '.')
            dot = p;
    }
    wchar_t ext[8];
    ext[0] = 0;
    if (dot && dot[1]) {
        int i = 0;
        ext[i++] = L'.';
        for (const char *p = dot + 1; *p && i < 5; ++p) {
            char c = *p;
            if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                ext[i++] = (wchar_t)c;
            else
                break;
        }
        ext[i] = 0;
        if (i < 2)
            ext[0] = 0;
    }
    CopyW(dst, dir, cap);
    AppendW(dst, cap, L"iliz-pc-remote-audio");
    if (ext[0])
        AppendW(dst, cap, ext);
    return TRUE;
}

static BOOL PlayFileId(const char *fileId)
{
    char query[1200];
    query[0] = 0;
    AppendA(query, sizeof(query), "getFile?file_id=");
    AppendUrlEnc(query, sizeof(query), fileId, FALSE);
    wchar_t url[2048];
    BotUrl(url, 2048, query);
    int n = 0;
    if (!HttpExchange(&g_http, url, 0, 15000, g_meta, sizeof(g_meta), &n) || !JsonOk(g_meta))
        return FALSE;
    cJSON *meta = cJSON_Parse(g_meta);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(meta, "result");
    cJSON *fp = cJSON_GetObjectItemCaseSensitive(result, "file_path");
    char filePath[512];
    filePath[0] = 0;
    if (cJSON_IsString(fp) && fp->valuestring)
        CopyA(filePath, fp->valuestring, sizeof(filePath));
    cJSON_Delete(meta);
    if (!filePath[0])
        return FALSE;

    char remote[700];
    remote[0] = 0;
    AppendA(remote, sizeof(remote), "https://api.telegram.org/file/bot");
    char token[SET_BOT_MAX];
    WideToUtf8(g_token, token, sizeof(token));
    AppendA(remote, sizeof(remote), token);
    AppendA(remote, sizeof(remote), "/");
    AppendUrlEnc(remote, sizeof(remote), filePath, TRUE);
    wchar_t fileUrl[1024];
    Utf8ToWide(remote, fileUrl, 1024);

    wchar_t dest[MAX_PATH];
    if (!AudioTemp(dest, MAX_PATH, filePath))
        return FALSE;
    AudioShutdown();
    if (!HttpDownload(&g_http, fileUrl, dest, 60000))
        return FALSE;
    if (!AudioPlayFile(dest)) {
        DeleteFileW(dest);
        return FALSE;
    }
    return TRUE;
}

static BOOL JNum(const cJSON *obj, const char *key, LONGLONG *out)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(v))
        return FALSE;
    *out = (LONGLONG)v->valuedouble;
    return TRUE;
}

static void HandleMessage(const cJSON *msg)
{
    cJSON *chat = cJSON_GetObjectItemCaseSensitive(msg, "chat");
    cJSON *from = cJSON_GetObjectItemCaseSensitive(msg, "from");
    if (!cJSON_IsObject(chat) || !cJSON_IsObject(from))
        return;
    LONGLONG chat_id = 0, from_id = 0;
    if (!JNum(chat, "id", &chat_id) || !JNum(from, "id", &from_id))
        return;
    cJSON *text = cJSON_GetObjectItemCaseSensitive(msg, "text");
    if (cJSON_IsString(text) && text->valuestring) {
        HandleText(chat_id, from_id, text->valuestring);
        return;
    }
    if (!IsAllowed(from_id))
        return;
    char file_id[512];
    if (!MediaFileId(msg, "voice", file_id, sizeof(file_id)) &&
        !MediaFileId(msg, "audio", file_id, sizeof(file_id)))
        return;
    AppFlash();
    if (!PlayFileId(file_id))
        SendText(chat_id, "Can't play audio.", FALSE);
}

static void ProcessUpdates(const char *json, LONGLONG *offset)
{
    cJSON *root = cJSON_Parse(json);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    cJSON *item = 0;
    cJSON_ArrayForEach(item, result) {
        cJSON *idv = cJSON_GetObjectItemCaseSensitive(item, "update_id");
        if (!cJSON_IsNumber(idv))
            continue;
        LONGLONG id = (LONGLONG)idv->valuedouble;
        if (id >= *offset) {
            *offset = id + 1;
            cJSON *msg = cJSON_GetObjectItemCaseSensitive(item, "message");
            if (cJSON_IsObject(msg))
                HandleMessage(msg);
        }
    }
    cJSON_Delete(root);
}

static BOOL SleepStop(DWORD ms)
{
    if (g_stop && WaitForSingleObject(g_stop, ms) == WAIT_OBJECT_0)
        return FALSE;
    if (!g_stop)
        Sleep(ms);
    return InterlockedCompareExchange(&g_run, 1, 1) == 1;
}

static DWORD WINAPI BotThread(void *param)
{
    LONG gen = (LONG)(INT_PTR)param;
    LONGLONG offset = 0;
    wchar_t url[2048];
    AppSetOwnStatus("Connecting...", FALSE);
    BotUrl(url, 2048, "getMe");
    int n = 0;
    if (!HttpExchange(&g_http, url, 0, 15000, g_body, BODY_MAX, &n) || !JsonOk(g_body)) {
        if (InterlockedCompareExchange(&g_gen, gen, gen) == gen)
            AppSetOwnStatus("Bad token", FALSE);
        return 0;
    }
    BotUrl(url, 2048, "deleteWebhook");
    HttpExchange(&g_http, url, 0, 15000, g_body, BODY_MAX, &n);
    if (InterlockedCompareExchange(&g_gen, gen, gen) != gen)
        return 0;
    AppSetOwnStatus("Online", TRUE);

    while (InterlockedCompareExchange(&g_run, 1, 1) == 1 &&
           InterlockedCompareExchange(&g_gen, gen, gen) == gen) {
        char query[96];
        char off[32];
        query[0] = 0;
        PutI64(off, sizeof(off), offset);
        AppendA(query, sizeof(query), "getUpdates?timeout=25&offset=");
        AppendA(query, sizeof(query), off);
        BotUrl(url, 2048, query);
        n = 0;
        if (!HttpExchange(&g_http, url, 0, 35000, g_body, BODY_MAX, &n)) {
            if (InterlockedCompareExchange(&g_gen, gen, gen) != gen)
                break;
            AppSetOwnStatus("Reconnect...", FALSE);
            if (!SleepStop(2000))
                break;
            if (InterlockedCompareExchange(&g_gen, gen, gen) == gen)
                AppSetOwnStatus("Online", TRUE);
            continue;
        }
        if (!JsonOk(g_body)) {
            if (InterlockedCompareExchange(&g_gen, gen, gen) != gen)
                break;
            AppSetOwnStatus("API error", FALSE);
            if (!SleepStop(2000))
                break;
            if (InterlockedCompareExchange(&g_gen, gen, gen) == gen)
                AppSetOwnStatus("Online", TRUE);
            continue;
        }
        ProcessUpdates(g_body, &offset);
    }
    return 0;
}

void OwnStop(void)
{
    InterlockedExchange(&g_run, 0);
    InterlockedIncrement(&g_gen);
    if (g_stop)
        SetEvent(g_stop);
    HttpJobAbort(&g_http);
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        g_thread = 0;
    }
    if (g_stop)
        ResetEvent(g_stop);
}

void OwnStart(void)
{
    OwnStop();
    HttpJobReset(&g_http);
    CopyW(g_token, Settings()->bot_token, SET_BOT_MAX);
    CopyW(g_allowed, Settings()->allowed, SET_ALLOW_MAX);
    TrimSpaceW(g_token);
    if (!g_stop)
        g_stop = CreateEventW(0, TRUE, FALSE, 0);
    if (g_stop)
        ResetEvent(g_stop);
    if (!g_token[0]) {
        AppSetOwnStatus("No token", FALSE);
        return;
    }
    InterlockedExchange(&g_run, 1);
    LONG gen = InterlockedIncrement(&g_gen);
    g_thread = CreateThread(0, 0, BotThread, (void *)(INT_PTR)gen, 0, 0);
    if (!g_thread)
        AppSetOwnStatus("Thread failed", FALSE);
}
