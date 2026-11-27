#include "config.h"
#include <errno.h>
#include <glib/gstdio.h>

static const char *group = "Bridge";

char *bridge_config_path(void)
{
    return g_build_filename(g_get_user_config_dir(), BRIDGE_EXE, "settings.ini", NULL);
}

GKeyFile *bridge_config_load(GError **error)
{
    g_autofree char *path = bridge_config_path();
    g_autoptr(GKeyFile) config = g_key_file_new();
    g_autoptr(GError) local = NULL;
    if (!g_key_file_load_from_file(config, path, G_KEY_FILE_KEEP_COMMENTS, &local) &&
        !g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
        g_propagate_error(error, g_steal_pointer(&local));
        return NULL;
    }
    return g_steal_pointer(&config);
}

static gboolean write_private(const char *path, const char *text, GError **error)
{
    g_autofree char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) < 0) {
        const int code = errno;
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(code),
                    "%s: %s", dir, g_strerror(code));
        return FALSE;
    }
    return g_file_set_contents_full(path, text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
}

gboolean bridge_config_save(GKeyFile *config, GError **error)
{
    g_autofree char *path = bridge_config_path();
    g_autofree char *text = g_key_file_to_data(config, NULL, NULL);
    return write_private(path, text, error);
}

char *bridge_autostart_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "autostart", BRIDGE_DESKTOP, NULL);
}

gboolean bridge_autostart_set(gboolean enabled, GError **error)
{
    g_autofree char *path = bridge_autostart_path();
    if (enabled)
        return write_private(path, "[Desktop Entry]\nType=Application\n"
                             "Name=KakaoTalk\nExec=" BRIDGE_EXE " start\n"
                             "TryExec=" BRIDGE_EXE "\n", error);
    if (g_unlink(path) == 0 || errno == ENOENT)
        return TRUE;
    const int code = errno;
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(code),
                "%s: %s", path, g_strerror(code));
    return FALSE;
}

char *bridge_managed_prefix(void)
{
    return g_build_filename(g_get_user_data_dir(), BRIDGE_EXE, "wine", NULL);
}

char *bridge_find_wine(GKeyFile *config)
{
    g_autofree char *value = g_key_file_get_string(config, group, "Wine", NULL);
    if (value && *value)
        return g_find_program_in_path(value);
    const char *override = g_getenv("WINE");
    return g_find_program_in_path(override && *override ? override : "wine");
}

char *bridge_find_client(GKeyFile *config, const char *prefix)
{
    g_autofree char *value = g_key_file_get_string(config, group, "Client", NULL);
    if (value && *value)
        return g_path_is_absolute(value) && g_file_test(value, G_FILE_TEST_IS_REGULAR)
            ? g_steal_pointer(&value) : NULL;
    const char *program_dirs[] = { "Program Files (x86)", "Program Files" };
    for (guint i = 0; i < G_N_ELEMENTS(program_dirs); i++) {
        g_autofree char *candidate = g_build_filename(prefix, "drive_c", program_dirs[i],
                                                    "Kakao", "KakaoTalk", "KakaoTalk.exe", NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR))
            return g_steal_pointer(&candidate);
    }
    return NULL;
}

char *bridge_find_prefix(GKeyFile *config)
{
    g_autofree char *value = g_key_file_get_string(config, group, "Prefix", NULL);
    if (value && *value)
        return g_path_is_absolute(value) ? g_steal_pointer(&value) : NULL;
    const char *override = g_getenv("WINEPREFIX");
    if (override && *override)
        return g_path_is_absolute(override) ? g_strdup(override) : NULL;

    g_autofree char *managed = bridge_managed_prefix();
    g_autofree char *existing = g_build_filename(g_get_home_dir(), ".wine", NULL);
    g_autofree char *client = bridge_find_client(config, managed);
    if (!client) {
        g_autofree char *detected = bridge_find_client(config, existing);
        if (detected)
            return g_steal_pointer(&existing);
    }
    return g_steal_pointer(&managed);
}

char *bridge_downloads(void)
{
    const char *dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    return dir ? g_strdup(dir) : g_build_filename(g_get_home_dir(), "Downloads", NULL);
}

char *bridge_find_helper(const char *name, GError **error)
{
    g_autofree char *self = g_file_read_link("/proc/self/exe", error);
    if (!self)
        return NULL;
    g_autofree char *bin = g_path_get_dirname(self);
    g_autofree char *development = g_build_filename(bin, name, NULL);
    if (g_file_test(development, G_FILE_TEST_IS_REGULAR))
        return g_steal_pointer(&development);
    g_autofree char *prefix = g_path_get_dirname(bin);
    g_autofree char *installed = g_build_filename(prefix, BRIDGE_LIBEXECDIR, BRIDGE_EXE, name, NULL);
    if (g_file_test(installed, G_FILE_TEST_IS_REGULAR))
        return g_steal_pointer(&installed);
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Windows helper is missing: %s", name);
    return NULL;
}
