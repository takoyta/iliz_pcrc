#pragma once
#include <windows.h>

#define SET_TOKEN_MAX 40
#define SET_HOST_MAX  256
#define SET_USER_MAX  128
#define SET_URL_MAX   (SET_HOST_MAX + SET_TOKEN_MAX)
#define SET_BOT_MAX   128
#define SET_ALLOW_MAX 512
#define SET_TG_USER   64
#define SET_TG_LINK   192

struct AppSettings {
    BOOL iliz_on;
    BOOL own_on;
    wchar_t token[SET_TOKEN_MAX];
    wchar_t server[SET_HOST_MAX];
    wchar_t host[SET_HOST_MAX];
    wchar_t user[SET_USER_MAX];
    wchar_t pass[SET_USER_MAX];
    int port;
    wchar_t control_page[SET_URL_MAX];
    wchar_t tg_user[SET_TG_USER];
    wchar_t tg_link[SET_TG_LINK];
    wchar_t bot_token[SET_BOT_MAX];
    wchar_t allowed[SET_ALLOW_MAX];
};

void SettingsLoad(void);
BOOL SettingsSave(void);
AppSettings *SettingsMut(void);
const AppSettings *Settings(void);
BOOL SettingsEnsureToken(void);
BOOL SettingsNewToken(wchar_t *dst, int cap);
BOOL SettingsValidToken(const wchar_t *s);

BOOL SettingsAutostart(void);
void SettingsSetAutostart(BOOL on);

void SettingsBuildPage(const wchar_t *server, const wchar_t *token, const wchar_t *control,
                       wchar_t *out, int cap);
void SettingsNormalizeAllowed(wchar_t *s);
