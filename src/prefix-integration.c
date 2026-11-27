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

static const char *find_korean_font(char **basename, GError **error)
{
    static const char *families[] = { "NanumGothic", "Noto Sans CJK KR" };
    for (guint i = 0; i < G_N_ELEMENTS(families); i++) {
        const char *args[] = { "fc-match", "-f", "%{family}\n%{file}", families[i], NULL };
        g_autoptr(GSubprocess) process = g_subprocess_newv(
            args, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, error);
        g_autofree char *match = NULL;
        if (!process || !g_subprocess_communicate_utf8(process, NULL, NULL, &match, NULL, error))
            return NULL;
        if (!g_subprocess_get_successful(process))
            break;
        g_auto(GStrv) details = g_strsplit(match, "\n", 2);
        if (!details[0] || !details[1] || !g_path_is_absolute(details[1]))
            continue;
        g_auto(GStrv) names = g_strsplit(details[0], ",", -1);
        for (guint j = 0; names[j]; j++) {
            if (g_str_equal(g_strstrip(names[j]), families[i])) {
                *basename = g_path_get_basename(details[1]);
                return families[i];
            }
        }
    }
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "Install fonts-nanum or fonts-noto-cjk before applying Korean font aliases.");
    return NULL;
}

static gboolean backup_registry(GSubprocessLauncher *launcher, const char *wine,
                                const char *key, const char *temp, const char *backup,
                                const char *filename, GError **error)
{
    g_autofree char *directory = g_path_get_basename(temp);
    g_autofree char *windows = g_strdup_printf("C:\\%s\\%s", directory, filename);
    g_autofree char *exported = g_build_filename(temp, filename, NULL);
    g_autofree char *saved = g_build_filename(backup, filename, NULL);
    g_autofree char *contents = NULL;
    gsize size = 0;
    const char *args[] = { wine, "reg", "export", key, windows, "/y", NULL };
    gboolean success = run_registry(launcher, args, error) &&
                       g_file_get_contents(exported, &contents, &size, error) &&
                       g_file_set_contents_full(saved, contents, size, G_FILE_SET_CONTENTS_CONSISTENT,
                                                 0600, error);
    g_unlink(exported);
    return success;
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

    g_autofree char *font_file = NULL;
    const char *family = fonts ? find_korean_font(&font_file, error) : NULL;
    if (fonts && !family)
        return FALSE;

    g_autofree char *font_helper = fonts ? bridge_find_helper("font-link.exe", error) : NULL;
    if (fonts && !font_helper)
        return FALSE;

    g_autoptr(GString) patch = g_string_new("\357\273\277Windows Registry Editor Version 5.00\r\n\r\n");
    static const char font_key[] = "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes";
    if (fonts) {
        g_string_append(patch, "[HKEY_CURRENT_USER\\Software\\Wine\\Fonts\\Replacements]\r\n");
        const char *names[] = { "Gulim", "GulimChe", "Dotum", "DotumChe", "Batang", "Malgun Gothic",
                                "굴림", "굴림체", "돋움", "돋움체", "바탕", "바탕체", "맑은 고딕" };
        for (guint i = 0; i < G_N_ELEMENTS(names); i++)
            g_string_append_printf(patch, "\"%s\"=\"%s\"\r\n", names[i], family);
        g_string_append(patch, "\r\n[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes]\r\n");
        const char *font_substitutes[] = { "MS Sans Serif", "MS Shell Dlg", "MS Shell Dlg 2",
                                           "굴림", "굴림체", "돋움", "돋움체", "바탕", "바탕체",
                                           "맑은 고딕", "Malgun Gothic" };
        for (guint i = 0; i < G_N_ELEMENTS(font_substitutes); i++)
            g_string_append_printf(patch, "\"%s\"=\"%s\"\r\n", font_substitutes[i], family);
    } else {
        g_string_append(patch, "[HKEY_CURRENT_USER\\Software\\Wine\\Explorer]\r\n\"Desktop\"=-\r\n\r\n");
        g_string_append_printf(patch, "[HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\%s\\Explorer]\r\n"
                                      "\"Desktop\"=-\r\n\r\n"
                                      "[HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\%s\\X11 Driver]\r\n"
                                      "\"Managed\"=\"Y\"\r\n", app, app);
    }

    gsize patch_size = 0;
    g_autofree char *patch_utf16 = g_convert(patch->str, patch->len, "UTF-16LE", "UTF-8",
                                             NULL, &patch_size, error);
    if (!patch_utf16)
        return FALSE;

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
    g_autofree char *patch_path = g_build_filename(temp, "changes.reg", NULL);
    g_autofree char *patch_windows = g_strdup_printf("C:\\%s\\changes.reg", directory);
    g_autoptr(GSubprocessLauncher) launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    g_subprocess_launcher_setenv(launcher, "WINEPREFIX", prefix, TRUE);

    gboolean saved = backup_registry(launcher, wine, "HKCU\\Software\\Wine",
                                     temp, backup_dir, "before.reg", error);
    if (saved && fonts)
        saved = backup_registry(launcher, wine, font_key, temp, backup_dir, "fonts-before.reg", error) &&
                backup_registry(launcher, wine,
                                "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontLink",
                                temp, backup_dir, "font-links-before.reg", error);

    gboolean applied = FALSE;
    if (saved) {
        *backup_path = g_build_filename(backup_dir, "before.reg", NULL);
        const char *import_args[] = { wine, "reg", "import", patch_windows, NULL };
        applied = g_file_set_contents_full(patch_path, patch_utf16, patch_size, G_FILE_SET_CONTENTS_CONSISTENT,
                                           0600, error) && run_registry(launcher, import_args, error);
        if (applied && fonts) {
            const char *link_args[] = { wine, font_helper, font_file, family, NULL };
            applied = run_registry(launcher, link_args, error);
        }
    }

    g_unlink(patch_path);
    g_rmdir(temp);
    return applied;
}
