#pragma once
#include <gio/gio.h>

#define BRIDGE_KOREAN_PROFILE "UbuntuKoreanProfile"
#define BRIDGE_KOREAN_LOCALE "ko_KR.UTF-8"

typedef struct {
    const char *xim;
    gboolean x11_available;
    gboolean korean_locale_available;
    gboolean ibus_hangul_available;
} BridgeWineTuning;

BridgeWineTuning bridge_wine_tuning_probe(gboolean korean_profile);
void bridge_wine_tuning_apply(GSubprocessLauncher *launcher, gboolean korean_profile);
