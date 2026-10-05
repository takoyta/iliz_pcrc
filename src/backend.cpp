#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "audio.h"
#include "backend.h"
#include "iliz.h"
#include "own.h"
#include "settings.h"
#include "ui.h"

void BackendStart(void)
{
    BackendStop();
    if (Settings()->iliz_on)
        IlizStart();
    else
        AppSetIlizStatus("", FALSE);
    if (Settings()->own_on)
        OwnStart();
    else
        AppSetOwnStatus("", FALSE);
}

void BackendStop(void)
{
    OwnStop();
    IlizStop();
    AudioShutdown();
}
