#pragma once
#include <gio/gio.h>

#define BRIDGE_ID "io.github.KakaoLinuxBridge"
#define BRIDGE_DESKTOP BRIDGE_ID ".desktop"
#define BRIDGE_EXE "kakao-linux-bridge"

char *bridge_config_path(void);
GKeyFile *bridge_config_load(GError **error);
gboolean bridge_config_save(GKeyFile *config, GError **error);
char *bridge_autostart_path(void);
gboolean bridge_autostart_set(gboolean enabled, GError **error);
char *bridge_managed_prefix(void);
char *bridge_find_wine(GKeyFile *config);
char *bridge_find_prefix(GKeyFile *config);
char *bridge_find_client(GKeyFile *config, const char *prefix);
char *bridge_downloads(void);
char *bridge_find_helper(const char *name, GError **error);
