#pragma once
#include <windows.h>

#define MQTT_TOKEN_MAX 40
#define MQTT_HOST_MAX  256
#define MQTT_USER_MAX  128

struct MqttOpts {
    wchar_t host[MQTT_HOST_MAX];
    int port;
    wchar_t user[MQTT_USER_MAX];
    wchar_t pass[MQTT_USER_MAX];
    char token[MQTT_TOKEN_MAX];
    char name[64];
    char version[16];
};

typedef void (*MqttStatusFn)(const char *text, BOOL online);
typedef void (*MqttReadyFn)(void);
typedef void (*MqttMsgFn)(const char *data, int len);

void MqttStart(const MqttOpts *opt, MqttStatusFn status, MqttReadyFn ready, MqttMsgFn msg);
void MqttStop(void);
BOOL MqttPublish(const char *suffix, const char *payload, BOOL retain);
