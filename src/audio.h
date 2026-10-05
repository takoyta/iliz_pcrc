#pragma once
#include <windows.h>

void AudioShutdown();
BOOL AudioPlayFile(const wchar_t *path);
BOOL AudioPlaySibling(const wchar_t *name);
