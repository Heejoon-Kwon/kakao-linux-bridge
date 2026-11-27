#pragma once
#include <gio/gio.h>

gboolean bridge_integrate_prefix(const char *wine, const char *prefix, const char *client,
                                 gboolean fonts, char **backup_path, GError **error);
