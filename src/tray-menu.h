#pragma once
#include <gio/gio.h>

typedef void (*BridgeMenuAction)(gint id, gpointer data);
guint bridge_menu_export(GDBusConnection *bus, BridgeMenuAction action, gpointer data);
#define BRIDGE_MENU_PATH "/BridgeMenu"
