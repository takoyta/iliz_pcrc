#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include "appinfo.h"
#include "audio.h"
#include "cmd.h"
#include "config.h"
#include "core.h"
#include "debug.h"
#include "httpx.h"
#include "iliz.h"
#include "cJSON.h"
#include "mqtt.h"
#include "settings.h"
#include "text.h"
#include "ui.h"

static HttpJob g_http;
static char g_name[64];
static ButtonDef g_pub[CFG_MAX_BUTTONS];
static HANDLE g_thread;
static HANDLE g_stop;
static volatile LONG g_run;
static volatile LONG g_gen;

static void LoadName(void)
{
    wchar_t w[64];
    DWORD n = 64;
    w[0] = 0;
    if (!GetComputerNameExW(ComputerNameDnsHostname, w, &n))
        w[0] = 0;
    WideToUtf8(w, g_name, sizeof(g_name));
}

static void PublishJson(const char *suffix, cJSON *obj, BOOL retain)
{
    char *text = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!text)
        return;
    MqttPublish(suffix, text, retain);
    cJSON_free(text);
}

static cJSON *EvtObj(const char *id, BOOL ok)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id", id ? id : "");
    cJSON_AddBoolToObject(obj, "ok", ok);
    return obj;
}

static void Evt(const char *id, BOOL ok)
{
    PublishJson("evt", EvtObj(id, ok), FALSE);
}

static void EvtResult(const char *id, const char *result)
{
    cJSON *obj = EvtObj(id, TRUE);
    cJSON_AddStringToObject(obj, "result", result ? result : "");
    PublishJson("evt", obj, FALSE);
}

static void EvtError(const char *id, const char *err)
{
    cJSON *obj = EvtObj(id, FALSE);
    cJSON_AddStringToObject(obj, "error", err ? err : "");
    PublishJson("evt", obj, FALSE);
}

static void EvtConfirm(const char *id, const char *which)
{
    cJSON *obj = EvtObj(id, FALSE);
    cJSON_AddStringToObject(obj, "needConfirm", which);
    cJSON_AddNumberToObject(obj, "timeout", 30);
    PublishJson("evt", obj, FALSE);
}

static void EvtShot(const char *id)
{
    cJSON *obj = EvtObj(id, TRUE);
    cJSON_AddBoolToObject(obj, "shot", 1);
    PublishJson("evt", obj, FALSE);
}

static const char *JStr(const cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(v) || !v->valuestring)
        return "";
    return v->valuestring;
}

static void PublishLayout(void)
{
    int n = ConfigCopyAll(g_pub, CFG_MAX_BUTTONS);
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name", g_name);
    cJSON *buttons = cJSON_AddArrayToObject(obj, "buttons");
    for (int i = 0; i < n; ++i) {
        const ButtonDef *b = &g_pub[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "row", b->row > 0 ? b->row : 1);
        cJSON_AddStringToObject(item, "label", b->label);
        cJSON_AddStringToObject(item, "action", b->action);
        cJSON_AddStringToObject(item, "value", b->value);
        cJSON_AddItemToArray(buttons, item);
    }
    PublishJson("layout", obj, TRUE);
}

static void OnReady(void)
{
    CoreReset();
    PublishLayout();
}

static BOOL ServerJoin(const wchar_t *suffix, wchar_t *out, int cap)
{
    const wchar_t *server = Settings()->server;
    if (!server[0])
        return FALSE;
    CopyW(out, server, cap);
    AppendW(out, cap, suffix);
    return out[0] != 0;
}

static void HostOnlyFromUrlA(const char *url, char *out, int cap)
{
    out[0] = 0;
    if (!url || !url[0] || cap < 1)
        return;
    const char *p = url;
    if (StartsA(p, "https://"))
        p += 8;
    else if (StartsA(p, "http://"))
        p += 7;
    int i = 0;
    while (p[i] && p[i] != '/' && p[i] != ':' && i < cap - 1) {
        out[i] = p[i];
        ++i;
    }
    out[i] = 0;
}

static BOOL Bootstrap(void)
{
    AppSettings *s = SettingsMut();
    wchar_t url[SET_HOST_MAX + 64];
    if (!ServerJoin(L"/api/device/bootstrap", url, SET_HOST_MAX + 64)) {
        DebugLog("bootstrap", "fail no url");
        return FALSE;
    }

    char url_a[512];
    DebugRedactUrlW(url, url_a, sizeof(url_a));
    DebugLog("bootstrap", "attempt %s", url_a);

    char token[SET_TOKEN_MAX];
    if (!WideToUtf8(s->token, token, sizeof(token))) {
        DebugLog("bootstrap", "fail token utf8");
        return FALSE;
    }
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "token", token);
    cJSON_AddStringToObject(body, "name", g_name);
    char *body_text = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_text) {
        DebugLog("bootstrap", "fail body json");
        return FALSE;
    }

    char resp[4096];
    int n = 0;
    BOOL http_ok = HttpExchange(&g_http, url, body_text, 8000, resp, sizeof(resp), &n);
    cJSON_free(body_text);
    if (!http_ok || n <= 0) {
        DebugLog("bootstrap", "fail http ok=%d bytes=%d", (int)http_ok, n);
        return FALSE;
    }

    cJSON *root = cJSON_ParseWithLength(resp, (size_t)n);
    if (!root) {
        DebugLog("bootstrap", "fail bad json");
        return FALSE;
    }
    cJSON *mqtt = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    cJSON *ctrl = cJSON_GetObjectItemCaseSensitive(root, "control");
    const char *host_s = JStr(mqtt, "host");
    const char *user_s = JStr(mqtt, "user");
    const char *pass_s = JStr(mqtt, "pass");
    const char *page_s = JStr(ctrl, "page");
    int port_v = 1883;
    cJSON *port = cJSON_GetObjectItemCaseSensitive(mqtt, "port");
    if (cJSON_IsNumber(port))
        port_v = (int)port->valuedouble;
    const char *tg_user = JStr(root, "telegram_bot_username");
    const char *tg_link = JStr(root, "telegram_deep_link");
    if (!tg_user[0])
        tg_user = JStr(ctrl, "telegram_bot_username");
    if (!tg_link[0])
        tg_link = JStr(ctrl, "telegram_deep_link");

    if (!host_s[0] || !user_s[0] || !pass_s[0] || port_v < 1 || port_v > 65535) {
        DebugLog("bootstrap", "fail missing mqtt fields");
        cJSON_Delete(root);
        return FALSE;
    }
    Utf8ToWide(host_s, s->host, SET_HOST_MAX);
    Utf8ToWide(user_s, s->user, SET_USER_MAX);
    Utf8ToWide(pass_s, s->pass, SET_USER_MAX);
    s->port = port_v;
    if (page_s[0])
        Utf8ToWide(page_s, s->control_page, SET_URL_MAX);
    if (tg_user[0]) {
        const char *u = tg_user[0] == '@' ? tg_user + 1 : tg_user;
        Utf8ToWide(u, s->tg_user, SET_TG_USER);
    }
    if (tg_link[0])
        Utf8ToWide(tg_link, s->tg_link, SET_TG_LINK);

    char page_host[256];
    page_host[0] = 0;
    if (s->control_page[0]) {
        char red[512];
        DebugRedactUrlW(s->control_page, red, sizeof(red));
        HostOnlyFromUrlA(red, page_host, sizeof(page_host));
    }
    DebugLog("bootstrap", "ok mqtt=%s:%d user=%s control_host=%s tg_user=%s",
             host_s, port_v, user_s,
             page_host[0] ? page_host : "-",
             tg_user[0] ? "yes" : "no");

    cJSON_Delete(root);
    SettingsSave();
    return TRUE;
}

static BOOL UploadShot(const char *id, HGLOBAL jpeg, DWORD len)
{
    if (!id || !id[0] || !jpeg || !len)
        return FALSE;
    for (const char *p = id; *p; ++p) {
        if (*p == '\r' || *p == '\n')
            return FALSE;
    }
    void *bytes = GlobalLock(jpeg);
    if (!bytes)
        return FALSE;
    wchar_t url[SET_HOST_MAX + 64];
    BOOL ok = FALSE;
    if (ServerJoin(L"/api/device/shot", url, SET_HOST_MAX + 64)) {
        wchar_t idw[80];
        Utf8ToWide(id, idw, 80);
        wchar_t headers[640];
        headers[0] = 0;
        AppendW(headers, 640, L"Content-Type: image/jpeg\r\nX-Device-Token: ");
        AppendW(headers, 640, Settings()->token);
        AppendW(headers, 640, L"\r\nX-Cmd-Id: ");
        AppendW(headers, 640, idw);
        AppendW(headers, 640, L"\r\n");
        int status = 0;
        ok = HttpPostRaw(&g_http, url, headers, bytes, (int)len, 20000, &status);
    }
    GlobalUnlock(jpeg);
    return ok;
}

static BOOL AudioDest(wchar_t *dst, int cap, const wchar_t *url)
{
    wchar_t dir[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, dir);
    if (!n || n >= MAX_PATH)
        return FALSE;
    const wchar_t *slash = url;
    const wchar_t *dot = 0;
    for (const wchar_t *p = url; *p; ++p) {
        if (*p == L'/' || *p == L'\\')
            slash = p + 1;
    }
    for (const wchar_t *p = slash; *p && *p != L'?' && *p != L'#'; ++p) {
        if (*p == L'.')
            dot = p;
    }
    wchar_t ext[8];
    ext[0] = 0;
    if (dot) {
        int i = 0;
        for (const wchar_t *p = dot; *p && *p != L'?' && *p != L'#' && i < 6; ++p) {
            wchar_t c = *p;
            if (c >= L'A' && c <= L'Z')
                c = (wchar_t)(c - L'A' + L'a');
            if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'.')
                ext[i++] = c;
            else
                break;
        }
        ext[i] = 0;
        if (i < 2)
            ext[0] = 0;
    }
    CopyW(dst, dir, cap);
    AppendW(dst, cap, L"iliz-pc-audio");
    if (ext[0])
        AppendW(dst, cap, ext);
    return TRUE;
}

static BOOL PlayUrl(const wchar_t *url)
{
    const wchar_t *server = Settings()->server;
    if (!server[0] || !StartsW(url, server))
        return FALSE;
    wchar_t dest[MAX_PATH];
    if (!AudioDest(dest, MAX_PATH, url))
        return FALSE;
    AudioShutdown();
    if (!HttpDownload(&g_http, url, dest, 60000))
        return FALSE;
    if (!AudioPlayFile(dest)) {
        DeleteFileW(dest);
        return FALSE;
    }
    return TRUE;
}

static void Finish(const char *id, CoreOut *out)
{
    if (out->kind == CORE_OK) {
        if (out->detail[0])
            EvtResult(id, out->detail);
        else
            Evt(id, TRUE);
        return;
    }
    if (out->kind == CORE_CONFIRM) {
        EvtConfirm(id, EqA(out->detail, "restart") ? "restart" : "shutdown");
        return;
    }
    if (out->kind == CORE_SHOT) {
        BOOL up = UploadShot(id, out->jpeg, out->jpeg_len);
        CoreFree(out);
        if (up)
            EvtShot(id);
        else
            EvtError(id, "shot failed");
        return;
    }
    if (out->kind == CORE_UNKNOWN) {
        EvtError(id, "unknown button");
        return;
    }
    EvtError(id, out->detail[0] ? out->detail : "bad command");
}

static void OnCommand(const char *json, int len)
{
    if (len < 0)
        len = 0;
    cJSON *root = cJSON_ParseWithLength(json, (size_t)len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return;
    }
    const char *id_s = JStr(root, "id");
    char id[68];
    CopyA(id, id_s, sizeof(id));
    if (!id[0]) {
        cJSON_Delete(root);
        return;
    }
    AppFlash();
    const char *type = JStr(root, "type");
    if (!type[0]) {
        cJSON_Delete(root);
        EvtError(id, "bad command");
        return;
    }

    if (EqA(type, "confirm")) {
        const char *answer = JStr(root, "answer");
        CoreOut out;
        CoreAnswer(0, TextIsYes(answer), &out);
        cJSON_Delete(root);
        if (out.kind != CORE_OK) {
            EvtError(id, "nothing to confirm");
            return;
        }
        Evt(id, TRUE);
        ConfigApplyPower(out.detail);
        return;
    }

    if (EqA(type, "audio")) {
        CoreReset();
        char url_a[1024];
        CopyA(url_a, JStr(root, "url"), sizeof(url_a));
        cJSON_Delete(root);
        if (!url_a[0]) {
            EvtError(id, "bad command");
            return;
        }
        wchar_t url_w[1024];
        Utf8ToWide(url_a, url_w, 1024);
        if (!PlayUrl(url_w)) {
            EvtError(id, "audio failed");
            return;
        }
        Evt(id, TRUE);
        return;
    }

    if (EqA(type, "cmd")) {
        CoreReset();
        char text[1024];
        CopyA(text, JStr(root, "text"), sizeof(text));
        cJSON_Delete(root);
        if (!text[0]) {
            EvtError(id, "bad command");
            return;
        }
        CmdOut cmd;
        CmdRun(text, &cmd);
        if (cmd.status == CMD_OK) {
            if (cmd.reply[0])
                EvtResult(id, cmd.reply);
            else
                Evt(id, TRUE);
            return;
        }
        EvtError(id, cmd.error[0] ? cmd.error : "bad command");
        return;
    }

    if (!EqA(type, "button")) {
        cJSON_Delete(root);
        EvtError(id, "bad command");
        return;
    }
    char label[CFG_LABEL_MAX];
    CopyA(label, JStr(root, "label"), sizeof(label));
    cJSON_Delete(root);
    if (!label[0]) {
        EvtError(id, "bad command");
        return;
    }
    CoreOut out;
    CoreButton(label, 0, &out);
    Finish(id, &out);
}

static BOOL SleepStop(DWORD ms)
{
    if (g_stop && WaitForSingleObject(g_stop, ms) == WAIT_OBJECT_0)
        return FALSE;
    if (!g_stop)
        Sleep(ms);
    return InterlockedCompareExchange(&g_run, 1, 1) == 1;
}

static void StartMqtt(void)
{
    AppSettings *s = SettingsMut();
    char host_a[MQTT_HOST_MAX];
    WideToUtf8(s->host, host_a, sizeof(host_a));
    DebugLog("mqtt", "connect %s:%d", host_a, s->port);
    MqttOpts opt;
    memset(&opt, 0, sizeof(opt));
    CopyW(opt.host, s->host, MQTT_HOST_MAX);
    opt.port = s->port;
    CopyW(opt.user, s->user, MQTT_USER_MAX);
    CopyW(opt.pass, s->pass, MQTT_USER_MAX);
    WideToUtf8(s->token, opt.token, MQTT_TOKEN_MAX);
    CopyA(opt.name, g_name, sizeof(opt.name));
    CopyA(opt.version, APP_VERSION_A, sizeof(opt.version));
    MqttStart(&opt, AppSetIlizStatus, OnReady, OnCommand);
}

static DWORD WINAPI BootstrapThread(void *param)
{
    LONG gen = (LONG)(INT_PTR)param;
    while (InterlockedCompareExchange(&g_run, 1, 1) == 1 &&
           InterlockedCompareExchange(&g_gen, gen, gen) == gen) {
        AppSetIlizStatus("Bootstrapping...", FALSE);
        if (Bootstrap()) {
            if (InterlockedCompareExchange(&g_gen, gen, gen) == gen)
                StartMqtt();
            return 0;
        }
        if (InterlockedCompareExchange(&g_gen, gen, gen) != gen)
            break;
        DebugLog("bootstrap", "retry in 60s");
        AppSetIlizStatus("Server unreachable, retry in 1m", FALSE);
        if (!SleepStop(60000))
            break;
    }
    return 0;
}

void IlizStart(void)
{
    IlizStop();
    LoadName();
    HttpJobReset(&g_http);
    AppSettings *s = SettingsMut();
    s->host[0] = 0;
    s->user[0] = 0;
    s->pass[0] = 0;
    s->port = 0;
    DebugLog("iliz", "start");
    if (!s->server[0] || !SettingsValidToken(s->token)) {
        DebugLog("iliz", "missing server or token");
        AppSetIlizStatus("Set server and token", FALSE);
        return;
    }
    if (!g_stop)
        g_stop = CreateEventW(0, TRUE, FALSE, 0);
    if (g_stop)
        ResetEvent(g_stop);
    InterlockedExchange(&g_run, 1);
    LONG gen = InterlockedIncrement(&g_gen);
    g_thread = CreateThread(0, 0, BootstrapThread, (void *)(INT_PTR)gen, 0, 0);
    if (!g_thread)
        AppSetIlizStatus("Thread failed", FALSE);
}

void IlizStop(void)
{
    if (g_thread || InterlockedCompareExchange(&g_run, 1, 1) == 1)
        DebugLog("iliz", "stop");
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
    MqttStop();
}

void IlizOpenPage(HWND owner)
{
    const AppSettings *s = Settings();
    wchar_t page[SET_URL_MAX];
    SettingsBuildPage(s->server, s->token, s->control_page, page, SET_URL_MAX);
    if (!page[0] || page[0] == L'/' || !SettingsValidToken(s->token)) {
        MessageBoxW(owner, L"No control page yet. Set the server in Settings and reconnect.",
                    APP_NAME, MB_OK | MB_ICONWARNING);
        return;
    }
    ShellExecuteW(0, L"open", page, 0, 0, SW_SHOWNORMAL);
}

void IlizOpenTelegram(HWND owner)
{
    const AppSettings *s = Settings();
    wchar_t link[SET_TG_LINK];
    link[0] = 0;
    if (s->tg_link[0]) {
        CopyW(link, s->tg_link, SET_TG_LINK);
    } else if (s->tg_user[0] && SettingsValidToken(s->token)) {
        char uname[SET_TG_USER];
        char token[SET_TOKEN_MAX];
        char raw[SET_TG_LINK];
        if (WideToUtf8(s->tg_user, uname, sizeof(uname)) &&
            WideToUtf8(s->token, token, sizeof(token))) {
            raw[0] = 0;
            AppendA(raw, sizeof(raw), "https://t.me/");
            AppendA(raw, sizeof(raw), uname);
            AppendA(raw, sizeof(raw), "?start=");
            AppendA(raw, sizeof(raw), token);
            Utf8ToWide(raw, link, SET_TG_LINK);
        }
    }
    if (!link[0]) {
        MessageBoxW(owner,
                    L"Telegram bot is not configured yet.\n"
                    L"Set the Telegram bot username in Settings, or wait until bootstrap returns it.",
                    APP_NAME, MB_OK | MB_ICONWARNING);
        return;
    }
    ShellExecuteW(0, L"open", link, 0, 0, SW_SHOWNORMAL);
}
