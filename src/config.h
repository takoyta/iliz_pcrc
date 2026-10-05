#pragma once

#define CFG_LABEL_MAX   64
#define CFG_ACTION_MAX  32
#define CFG_VALUE_MAX   260
#define CFG_MAX_BUTTONS 64
#define CFG_VERSION     1

struct ButtonDef {
    int  row;
    char label[CFG_LABEL_MAX];
    char action[CFG_ACTION_MAX];
    char value[CFG_VALUE_MAX];
};

struct cJSON;

void ConfigInit();
void ConfigGetPath(wchar_t *out, int cap);
void ConfigAppendButtons(struct cJSON *root);
BOOL ConfigLoad();
BOOL ConfigSave();

int  ConfigCount();
const ButtonDef *ConfigGet(int i);
BOOL ConfigCopy(int i, ButtonDef *out);
int  ConfigCopyAll(ButtonDef *dst, int cap);
BOOL ConfigFindCopy(const char *label_utf8, ButtonDef *out);
ButtonDef *ConfigGetMut(int i);
void ConfigClear();
BOOL ConfigAdd(const ButtonDef *b);
BOOL ConfigRemove(int index);
BOOL ConfigUpdate(int index, const ButtonDef *b);
BOOL ConfigMove(int index, int delta);
void ConfigSort();

const ButtonDef *ConfigFindLabel(const char *label_utf8);
BOOL ConfigAddReplyKeyboard(struct cJSON *message);

#define CFG_EXEC_OK      0
#define CFG_EXEC_CONFIRM 1
#define CFG_EXEC_SHOT    2
#define CFG_EXEC_FAIL    3

int ConfigSystemCount();
const char *ConfigSystemValue(int index);
const wchar_t *ConfigSystemTitle(int index);
int ConfigSystemIndex(const char *value);

int ConfigExecute(const ButtonDef *b, char *err, int errCap, char *reply, int replyCap);
void ConfigApplyPower(const char *value);

const wchar_t *ConfigActionTitle(const char *action);
const wchar_t *ConfigValueTitle(const char *action, const char *value);
