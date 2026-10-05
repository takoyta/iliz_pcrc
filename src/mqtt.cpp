#define WIN32_LEAN_AND_MEAN
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "mqtt.h"
#include "text.h"
#include "cJSON.h"

static MqttOpts g_opt;
static MqttStatusFn g_on_status;
static MqttReadyFn g_on_ready;
static MqttMsgFn g_on_msg;
static HANDLE g_thread;
static HANDLE g_stop;
static volatile LONG g_run;
static volatile LONG g_gen;
static SOCKET g_sock = INVALID_SOCKET;
static BOOL g_up;
static BYTE g_rx[16384];
static int g_rx_len;
static ULONGLONG g_last_send;

static BOOL Current(LONG gen)
{
    return InterlockedCompareExchange(&g_gen, gen, gen) == gen;
}

static void Status(LONG gen, const char *text, BOOL online)
{
    if (!Current(gen) || !g_on_status)
        return;
    g_on_status(text, online);
}

static BOOL StopAsked()
{
    return !g_run || (g_stop && WaitForSingleObject(g_stop, 0) == WAIT_OBJECT_0);
}

static BOOL SendRaw(const void *data, int len)
{
    const char *p = (const char *)data;
    int left = len;
    while (left > 0) {
        if (g_sock == INVALID_SOCKET)
            return FALSE;
        int n = send(g_sock, p, left, 0);
        if (n > 0) {
            p += n;
            left -= n;
            g_last_send = GetTickCount64();
            continue;
        }
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set w;
            FD_ZERO(&w);
            FD_SET(g_sock, &w);
            timeval tv;
            tv.tv_sec = 2;
            tv.tv_usec = 0;
            if (select(0, 0, &w, 0, &tv) <= 0)
                return FALSE;
            continue;
        }
        return FALSE;
    }
    return TRUE;
}

static int EncRem(BYTE *dst, int v)
{
    int n = 0;
    do {
        BYTE b = (BYTE)(v % 128);
        v /= 128;
        if (v)
            b = (BYTE)(b | 0x80);
        dst[n++] = b;
    } while (v && n < 4);
    return n;
}

static void Topic(char *out, int cap, const char *suffix)
{
    out[0] = 0;
    CopyA(out, "pc/", cap);
    int i = LenA(out);
    const char *t = g_opt.token;
    while (*t && i < cap - 1)
        out[i++] = *t++;
    if (i < cap - 1)
        out[i++] = '/';
    while (*suffix && i < cap - 1)
        out[i++] = *suffix++;
    out[i] = 0;
}

static void CloseSock()
{
    g_up = FALSE;
    if (g_sock != INVALID_SOCKET) {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
    }
    g_rx_len = 0;
}

BOOL MqttPublish(const char *suffix, const char *payload, BOOL retain)
{
    if (!g_up || !suffix || !payload || g_sock == INVALID_SOCKET)
        return FALSE;
    char topic[96];
    Topic(topic, sizeof(topic), suffix);
    int tlen = LenA(topic);
    int plen = LenA(payload);
    int rem = 2 + tlen + plen;
    BYTE rb[4];
    int rn = EncRem(rb, rem);
    int total = 1 + rn + rem;
    BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)total);
    if (!buf)
        return FALSE;
    int n = 0;
    buf[n++] = (BYTE)(retain ? 0x31 : 0x30);
    for (int i = 0; i < rn; ++i)
        buf[n++] = rb[i];
    buf[n++] = (BYTE)(tlen >> 8);
    buf[n++] = (BYTE)tlen;
    memcpy(buf + n, topic, (size_t)tlen);
    n += tlen;
    memcpy(buf + n, payload, (size_t)plen);
    BOOL ok = SendRaw(buf, total);
    HeapFree(GetProcessHeap(), 0, buf);
    return ok;
}

static BOOL SendConnect()
{
    char client[MQTT_TOKEN_MAX + 8];
    CopyA(client, "pc-", sizeof(client));
    int i = LenA(client);
    const char *t = g_opt.token;
    while (*t && i < (int)sizeof(client) - 1)
        client[i++] = *t++;
    client[i] = 0;

    char user[512];
    char pass[512];
    if (!WideToUtf8(g_opt.user, user, sizeof(user)) || !WideToUtf8(g_opt.pass, pass, sizeof(pass)))
        return FALSE;

    char will_topic[96];
    Topic(will_topic, sizeof(will_topic), "status");
    const char *will = "{\"online\":false}";

    BYTE body[2048];
    int n = 0;
    const char *proto = "MQTT";
    body[n++] = 0;
    body[n++] = 4;
    memcpy(body + n, proto, 4);
    n += 4;
    body[n++] = 4;
    BYTE flags = 0x02 | 0x04 | 0x20;
    if (user[0])
        flags = (BYTE)(flags | 0x80);
    if (user[0] && pass[0])
        flags = (BYTE)(flags | 0x40);
    body[n++] = flags;
    body[n++] = 0;
    body[n++] = 20;

    int clen = LenA(client);
    body[n++] = (BYTE)(clen >> 8);
    body[n++] = (BYTE)clen;
    memcpy(body + n, client, (size_t)clen);
    n += clen;

    int wt = LenA(will_topic);
    body[n++] = (BYTE)(wt >> 8);
    body[n++] = (BYTE)wt;
    memcpy(body + n, will_topic, (size_t)wt);
    n += wt;
    int wl = LenA(will);
    body[n++] = (BYTE)(wl >> 8);
    body[n++] = (BYTE)wl;
    memcpy(body + n, will, (size_t)wl);
    n += wl;

    if (user[0]) {
        int ul = LenA(user);
        if (n + 2 + ul > (int)sizeof(body))
            return FALSE;
        body[n++] = (BYTE)(ul >> 8);
        body[n++] = (BYTE)ul;
        memcpy(body + n, user, (size_t)ul);
        n += ul;
    }
    if (user[0] && pass[0]) {
        int pl = LenA(pass);
        if (n + 2 + pl > (int)sizeof(body))
            return FALSE;
        body[n++] = (BYTE)(pl >> 8);
        body[n++] = (BYTE)pl;
        memcpy(body + n, pass, (size_t)pl);
        n += pl;
    }

    BYTE rb[4];
    int rn = EncRem(rb, n);
    BYTE hdr[8];
    hdr[0] = 0x10;
    for (int k = 0; k < rn; ++k)
        hdr[1 + k] = rb[k];
    if (!SendRaw(hdr, 1 + rn))
        return FALSE;
    return SendRaw(body, n);
}

static BOOL SendSubscribe()
{
    char topic[96];
    Topic(topic, sizeof(topic), "cmd");
    int tlen = LenA(topic);
    int rem = 2 + 2 + tlen + 1;
    BYTE pkt[160];
    if (1 + 1 + rem > (int)sizeof(pkt))
        return FALSE;
    int n = 0;
    pkt[n++] = 0x82;
    pkt[n++] = (BYTE)rem;
    pkt[n++] = 0;
    pkt[n++] = 1;
    pkt[n++] = (BYTE)(tlen >> 8);
    pkt[n++] = (BYTE)tlen;
    memcpy(pkt + n, topic, (size_t)tlen);
    n += tlen;
    pkt[n++] = 0;
    return SendRaw(pkt, n);
}

static BOOL SendPuback(int id)
{
    BYTE pkt[4];
    pkt[0] = 0x40;
    pkt[1] = 2;
    pkt[2] = (BYTE)(id >> 8);
    pkt[3] = (BYTE)id;
    return SendRaw(pkt, 4);
}

static BOOL SendPing()
{
    BYTE pkt[2];
    pkt[0] = 0xC0;
    pkt[1] = 0;
    return SendRaw(pkt, 2);
}

static BOOL SendDisconnect()
{
    BYTE pkt[2];
    pkt[0] = 0xE0;
    pkt[1] = 0;
    return SendRaw(pkt, 2);
}

static void PublishOnline(BOOL on)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(obj, "online", on);
    if (on) {
        cJSON_AddStringToObject(obj, "name", g_opt.name);
        cJSON_AddStringToObject(obj, "version", g_opt.version);
    }
    char *text = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!text)
        return;
    MqttPublish("status", text, TRUE);
    cJSON_free(text);
}

static int RecvWait(int timeout_ms)
{
    if (g_sock == INVALID_SOCKET)
        return -1;
    if (g_rx_len >= (int)sizeof(g_rx))
        return -1;
    fd_set r;
    FD_ZERO(&r);
    FD_SET(g_sock, &r);
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int s = select(0, &r, 0, 0, &tv);
    if (s == 0)
        return 0;
    if (s < 0)
        return -1;
    int n = recv(g_sock, (char *)g_rx + g_rx_len, (int)sizeof(g_rx) - g_rx_len, 0);
    if (n <= 0)
        return -1;
    g_rx_len += n;
    return 1;
}

static int RemLen(const BYTE *p, int avail, int *used, int *val)
{
    int mul = 1;
    int v = 0;
    int i = 0;
    for (; i < 4 && i < avail; ++i) {
        v += (p[i] & 0x7f) * mul;
        mul *= 128;
        if ((p[i] & 0x80) == 0) {
            *used = i + 1;
            *val = v;
            return 1;
        }
    }
    if (i == 4)
        return -1;
    return 0;
}

static void Consume(int n)
{
    if (n >= g_rx_len) {
        g_rx_len = 0;
        return;
    }
    int left = g_rx_len - n;
    for (int i = 0; i < left; ++i)
        g_rx[i] = g_rx[n + i];
    g_rx_len = left;
}

static int NextPacket(int *rem, int *hdr)
{
    if (g_rx_len < 2)
        return 0;
    int used = 0;
    int val = 0;
    int st = RemLen(g_rx + 1, g_rx_len - 1, &used, &val);
    if (st <= 0)
        return st;
    if (val < 0 || val > 8192)
        return -1;
    if (g_rx_len < 1 + used + val)
        return 0;
    *hdr = used;
    *rem = val;
    return 1;
}

static BOOL HandlePublish(const BYTE *body, int rem)
{
    BYTE flags = g_rx[0];
    int qos = (flags >> 1) & 0x03;
    if (rem < 2)
        return FALSE;
    int tlen = (body[0] << 8) | body[1];
    if (tlen < 0 || 2 + tlen > rem)
        return FALSE;
    const BYTE *p = body + 2 + tlen;
    int left = rem - 2 - tlen;
    int pid = 0;
    if (qos > 0) {
        if (left < 2)
            return FALSE;
        pid = (p[0] << 8) | p[1];
        p += 2;
        left -= 2;
    }
    if (qos == 1)
        SendPuback(pid);
    BOOL cmd = tlen >= 4 && body[2 + tlen - 4] == '/' && body[2 + tlen - 3] == 'c' &&
               body[2 + tlen - 2] == 'm' && body[2 + tlen - 1] == 'd';
    if (!cmd || qos > 1 || left < 0 || !g_on_msg)
        return TRUE;
    char *tmp = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)left + 1);
    if (!tmp)
        return FALSE;
    if (left)
        memcpy(tmp, p, (size_t)left);
    tmp[left] = 0;
    g_on_msg(tmp, left);
    HeapFree(GetProcessHeap(), 0, tmp);
    return TRUE;
}

static int TakePacket(int *type, const BYTE **body, int *rem)
{
    int hdr = 0;
    int r = 0;
    int st = NextPacket(&r, &hdr);
    if (st <= 0)
        return st;
    *type = g_rx[0];
    *body = g_rx + 1 + hdr;
    *rem = r;
    return 1;
}

static void DropPacket(int rem)
{
    int hdr = 0;
    int r = 0;
    if (NextPacket(&r, &hdr) <= 0)
        return;
    Consume(1 + hdr + rem);
}

static BOOL WaitType(LONG gen, int want, int *code, int timeout_ms)
{
    ULONGLONG until = GetTickCount64() + (ULONGLONG)timeout_ms;
    for (;;) {
        if (!Current(gen) || StopAsked())
            return FALSE;
        int type = 0;
        const BYTE *body = 0;
        int rem = 0;
        int st = TakePacket(&type, &body, &rem);
        if (st < 0)
            return FALSE;
        if (st == 0) {
            ULONGLONG now = GetTickCount64();
            if (now >= until)
                return FALSE;
            int slice = (int)(until - now);
            if (slice > 200)
                slice = 200;
            int wr = RecvWait(slice);
            if (wr < 0)
                return FALSE;
            continue;
        }
        int kind = (type >> 4) & 0x0f;
        if (kind == want) {
            if (code && rem > 0)
                *code = body[rem - 1];
            DropPacket(rem);
            return TRUE;
        }
        if (kind == 3) {
            if (!HandlePublish(body, rem))
                return FALSE;
        }
        DropPacket(rem);
    }
}

static BOOL TcpConnect(LONG gen)
{
    char host[MQTT_HOST_MAX];
    if (!WideToUtf8(g_opt.host, host, sizeof(host)) || !host[0])
        return FALSE;
    char port[8];
    int v = g_opt.port;
    if (v <= 0 || v > 65535)
        v = 1883;
    char tmp[8];
    int n = 0;
    int x = v;
    if (x == 0)
        tmp[n++] = '0';
    while (x && n < 7) {
        tmp[n++] = (char)('0' + x % 10);
        x /= 10;
    }
    int pi = 0;
    while (n)
        port[pi++] = tmp[--n];
    port[pi] = 0;

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo *res = 0;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
        return FALSE;

    BOOL ok = FALSE;
    for (addrinfo *ai = res; ai && !ok; ai = ai->ai_next) {
        if (StopAsked() || !Current(gen))
            break;
        SOCKET s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET)
            continue;
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        int cr = connect(s, ai->ai_addr, (int)ai->ai_addrlen);
        if (cr != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
            closesocket(s);
            continue;
        }
        BOOL ready = FALSE;
        for (int waited = 0; waited < 5000; waited += 200) {
            if (StopAsked() || !Current(gen))
                break;
            fd_set w;
            FD_ZERO(&w);
            FD_SET(s, &w);
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            int sel = select(0, 0, &w, 0, &tv);
            if (sel > 0) {
                int err = 0;
                int len = sizeof(err);
                getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
                ready = (err == 0);
                break;
            }
            if (sel < 0)
                break;
        }
        if (!ready) {
            closesocket(s);
            continue;
        }
        g_sock = s;
        ok = TRUE;
    }
    freeaddrinfo(res);
    return ok;
}

static void Graceful()
{
    if (g_up) {
        PublishOnline(FALSE);
        SendDisconnect();
    }
    CloseSock();
}

static DWORD WINAPI ThreadProc(void *param)
{
    LONG gen = (LONG)(INT_PTR)param;
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
        Status(gen, "Winsock failed", FALSE);
        return 0;
    }

    BOOL first = TRUE;
    while (Current(gen) && g_run && !StopAsked()) {
        Status(gen, first ? "Connecting..." : "Reconnect...", FALSE);
        first = FALSE;
        g_rx_len = 0;
        if (!TcpConnect(gen)) {
            if (!Current(gen) || StopAsked())
                break;
            Status(gen, "Broker unreachable", FALSE);
            if (g_stop && WaitForSingleObject(g_stop, 2000) == WAIT_OBJECT_0)
                break;
            continue;
        }
        if (!SendConnect()) {
            CloseSock();
            if (g_stop && WaitForSingleObject(g_stop, 2000) == WAIT_OBJECT_0)
                break;
            continue;
        }
        int code = -1;
        if (!WaitType(gen, 2, &code, 5000)) {
            CloseSock();
            if (!Current(gen) || StopAsked())
                break;
            Status(gen, "Broker unreachable", FALSE);
            if (g_stop && WaitForSingleObject(g_stop, 2000) == WAIT_OBJECT_0)
                break;
            continue;
        }
        if (code != 0) {
            CloseSock();
            if (!Current(gen) || StopAsked())
                break;
            Status(gen, (code == 4 || code == 5) ? "MQTT auth failed" : "MQTT refused", FALSE);
            if (g_stop && WaitForSingleObject(g_stop, 3000) == WAIT_OBJECT_0)
                break;
            continue;
        }
        g_up = TRUE;
        if (!SendSubscribe() || !WaitType(gen, 9, &code, 5000) || code == 0x80) {
            CloseSock();
            if (!Current(gen) || StopAsked())
                break;
            Status(gen, "Subscribe failed", FALSE);
            if (g_stop && WaitForSingleObject(g_stop, 2000) == WAIT_OBJECT_0)
                break;
            continue;
        }

        PublishOnline(TRUE);
        Status(gen, "Online", TRUE);
        if (g_on_ready && Current(gen))
            g_on_ready();

        while (Current(gen) && g_run && !StopAsked()) {
            int type = 0;
            const BYTE *body = 0;
            int rem = 0;
            int st = TakePacket(&type, &body, &rem);
            if (st < 0)
                break;
            if (st == 0) {
                if (GetTickCount64() - g_last_send > 10000) {
                    if (!SendPing())
                        break;
                }
                if (RecvWait(500) < 0)
                    break;
                continue;
            }
            int kind = (type >> 4) & 0x0f;
            if (kind == 3) {
                if (!HandlePublish(body, rem)) {
                    DropPacket(rem);
                    break;
                }
            }
            DropPacket(rem);
        }

        BOOL stop = StopAsked() || !Current(gen) || !g_run;
        if (stop)
            Graceful();
        else
            CloseSock();
        if (stop)
            break;
        Status(gen, "Reconnect...", FALSE);
        if (g_stop && WaitForSingleObject(g_stop, 2000) == WAIT_OBJECT_0)
            break;
    }

    CloseSock();
    WSACleanup();
    return 0;
}

void MqttStop(void)
{
    InterlockedExchange(&g_run, 0);
    InterlockedIncrement(&g_gen);
    if (g_stop)
        SetEvent(g_stop);
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        g_thread = 0;
    }
    if (g_stop)
        ResetEvent(g_stop);
}

void MqttStart(const MqttOpts *opt, MqttStatusFn status, MqttReadyFn ready, MqttMsgFn msg)
{
    MqttStop();
    g_on_status = status;
    g_on_ready = ready;
    g_on_msg = msg;
    g_up = FALSE;
    if (!g_stop)
        g_stop = CreateEventW(0, TRUE, FALSE, 0);
    if (g_stop)
        ResetEvent(g_stop);
    if (!opt || !opt->host[0] || !opt->token[0]) {
        Status(g_gen, "No broker", FALSE);
        return;
    }
    g_opt = *opt;
    InterlockedExchange(&g_run, 1);
    LONG gen = InterlockedIncrement(&g_gen);
    g_thread = CreateThread(0, 0, ThreadProc, (void *)(INT_PTR)gen, 0, 0);
    if (!g_thread)
        Status(gen, "Thread failed", FALSE);
}
