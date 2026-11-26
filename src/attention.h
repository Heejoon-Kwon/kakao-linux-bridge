#pragma once
#include <glib.h>

typedef struct BridgeAttention BridgeAttention;
typedef void (*BridgeAttentionFunc)(gpointer data);

BridgeAttention *bridge_attention_watch(guint32 pid, const char *window_class,
                                        BridgeAttentionFunc callback, gpointer data,
                                        GError **error);
void bridge_attention_free(BridgeAttention *watch);
