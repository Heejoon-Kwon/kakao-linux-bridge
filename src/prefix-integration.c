#include "config.h"
#include "prefix-integration.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>

static gboolean run_registry(GSubprocessLauncher *launcher, const char *const *args,
                             GError **error)
{
    g_autoptr(GSubprocess) process = g_subprocess_launcher_spawnv(launcher, args, error);
    return process && g_subprocess_wait_check(process, NULL, error);
}

static const char *find_korean_font(GError **error)
{
    static const char *families[] = { "NanumGothic", "Noto Sans CJK KR" };
    for (guint i = 0; i < G_N_ELEMENTS(families); i++) {
        const char *args[] = { "fc-match", "-f", "%{family}", families[i], NULL };
        g_autoptr(GSubprocess) process = g_subprocess_newv(
            args, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, error);
        g_autofree char *match = NULL;
        if (!process || !g_subprocess_communicate_utf8(process, NULL, NULL, &match, NULL, error))
            return NULL;
        if (!g_subprocess_get_successful(process))
            break;
        g_auto(GStrv) names = g_strsplit(match, ",", -1);
        for (guint j = 0; names[j]; j++)
            if (g_str_equal(g_strstrip(names[j]), families[i]))
                return families[i];
    }
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "Install fonts-nanum or fonts-noto-cjk before applying Korean font aliases.");
    return NULL;
}

gboolean bridge_integrate_prefix(const char *wine, const char *prefix, const char *client,
                                 gboolean fonts, char **backup_path, GError **error)
{
    g_autofree char *app = g_path_get_basename(client);
    if (strpbrk(app, "\\[]\r\n") || !g_str_is_ascii(app)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Use the original ASCII KakaoTalk executable name.");
        return FALSE;
    }

    const char *family = fonts ? find_korean_font(error) : NULL;
    if (fonts && !family)
        return FALSE;

    g_autoptr(GString) patch = g_string_new("REGEDIT4\r\n\r\n");
    static const char font_key[] = "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes";
    if (fonts) {
        g_string_append(patch, "[HKEY_CURRENT_USER\\Software\\Wine\\Fonts\\Replacements]\r\n");
        const char *names[] = { "Gulim", "GulimChe", "Dotum", "DotumChe", "Batang", "Malgun Gothic" };
        for (guint i = 0; i < G_N_ELEMENTS(names); i++)
            g_string_append_printf(patch, "\"%s\"=\"%s\"\r\n", names[i], family);
        g_string_append(patch, "\r\n[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes]\r\n");
        const char *font_substitutes[] = { "Tahoma", "MS Sans Serif", "MS Shell Dlg", "MS Shell Dlg 2" };
        for (guint i = 0; i < G_N_ELEMENTS(font_substitutes); i++)
            g_string_append_printf(patch, "\"%s\"=\"%s\"\r\n", font_substitutes[i], family);
    } else {
        g_string_append(patch, "[HKEY_CURRENT_USER\\Software\\Wine\\Explorer]\r\n\"Desktop\"=-\r\n\r\n");
        g_string_append_printf(patch, "[HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\%s\\Explorer]\r\n"
                                      "\"Desktop\"=-\r\n\r\n"
                                      "[HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\%s\\X11 Driver]\r\n"
                                      "\"Managed\"=\"Y\"\r\n", app, app);
    }

    g_autofree char *temp = g_build_filename(prefix, "drive_c", "KakaoBridge-XXXXXX", NULL);
    g_autofree char *backup_dir = g_build_filename(g_get_user_state_dir(), BRIDGE_EXE, "wine-XXXXXX", NULL);
    g_autofree char *state_dir = g_path_get_dirname(backup_dir);
    if (g_mkdir_with_parents(state_dir, 0700) < 0 || !g_mkdtemp(backup_dir)) {
        int code = errno;
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(code),
                    "Cannot create registry staging or backup directory: %s", g_strerror(code));
        return FALSE;
    }

    if (!g_mkdtemp(temp)) {
        int code = errno;
        g_rmdir(backup_dir);
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(code),
                    "Cannot create registry staging directory: %s", g_strerror(code));
        return FALSE;
    }

    g_autofree char *directory = g_path_get_basename(temp);
    g_autofree char *export_path = g_build_filename(temp, "before.reg", NULL);
    g_autofree char *font_export_path = g_build_filename(temp, "fonts-before.reg", NULL);
    g_autofree char *font_backup_path = g_build_filename(backup_dir, "fonts-before.reg", NULL);
    g_autofree char *font_export_windows = g_strdup_printf("C:\\%s\\fonts-before.reg", directory);
    g_autofree char *patch_path = g_build_filename(temp, "changes.reg", NULL);
    g_autofree char *export_windows = g_strdup_printf("C:\\%s\\before.reg", directory);
    g_autofree char *patch_windows = g_strdup_printf("C:\\%s\\changes.reg", directory);
    g_autofree char *saved_path = g_build_filename(backup_dir, "before.reg", NULL);
    g_autofree char *snapshot = NULL;
    gsize size = 0;
    g_autoptr(GSubprocessLauncher) launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    g_subprocess_launcher_setenv(launcher, "WINEPREFIX", prefix, TRUE);
    const char *export_args[] = { wine, "reg", "export", "HKCU\\Software\\Wine", export_windows, "/y", NULL };
    const char *import_args[] = { wine, "reg", "import", patch_windows, NULL };
    gboolean saved = run_registry(launcher, export_args, error) &&
                     g_file_get_contents(export_path, &snapshot, &size, error) &&
                     g_file_set_contents_full(saved_path, snapshot, size, G_FILE_SET_CONTENTS_CONSISTENT,
                                               0600, error);
    if (saved && fonts) {
        const char *font_export_args[] = { wine, "reg", "export", font_key, font_export_windows, "/y", NULL };
        g_clear_pointer(&snapshot, g_free);
        saved = run_registry(launcher, font_export_args, error) &&
                g_file_get_contents(font_export_path, &snapshot, &size, error) &&
                g_file_set_contents_full(font_backup_path, snapshot, size,
                                         G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
    }
    gboolean applied = FALSE;
    if (saved) {
        *backup_path = g_steal_pointer(&saved_path);
        applied = g_file_set_contents_full(patch_path, patch->str, patch->len, G_FILE_SET_CONTENTS_CONSISTENT,
                                           0600, error) &&
                  run_registry(launcher, import_args, error);
    }

    g_unlink(export_path);
    g_unlink(patch_path);
    g_unlink(font_export_path);
    g_rmdir(temp);
    if (!saved) {
        g_unlink(saved_path);
        g_unlink(font_backup_path);
        g_rmdir(backup_dir);
    }
    return applied;
}
