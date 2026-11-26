#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "attention.h"
#include "wine-tuning.h"
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

typedef struct {
    GApplication *app;
    GSubprocess *child;
    char *wine;
    char *prefix;
    char *client;
    guint dpi;
    gboolean downloads;
    gboolean watch_attention;
    gboolean korean_profile;
    BridgeAttention *attention;
    gboolean stopping;
    gboolean stop_pending;
    int exit_status;
    const char *operation;
    guint tray_object;
    guint tray_watch;
} Bridge;

static const char *tray_xml =
    "<node><interface name='org.kde.StatusNotifierItem'>"
    "<property name='Category' type='s' access='read'/>"
    "<property name='Id' type='s' access='read'/>"
    "<property name='Title' type='s' access='read'/>"
    "<property name='Status' type='s' access='read'/>"
    "<property name='IconName' type='s' access='read'/>"
    "<property name='ItemIsMenu' type='b' access='read'/>"
    "<method name='Activate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "<method name='SecondaryActivate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "</interface></node>";

static gboolean open_settings(GError **error)
{
    g_autofree char *self = g_file_read_link("/proc/self/exe", error);
    if (!self)
        return FALSE;
    g_autofree char *dir = g_path_get_dirname(self);
    g_autofree char *settings = g_build_filename(dir, BRIDGE_EXE "-settings", NULL);
    const char *args[] = { settings, NULL };
    return g_spawn_async(NULL, (char **)args, NULL, 0, NULL, NULL, NULL, error);
}

static gboolean open_downloads(GError **error)
{
    g_autofree char *path = bridge_downloads();
    g_autoptr(GFile) file = g_file_new_for_path(path);
    g_autofree char *uri = g_file_get_uri(file);
    return g_app_info_launch_default_for_uri(uri, NULL, error);
}

static GVariant *tray_property(GDBusConnection *connection, const char *sender,
                               const char *path, const char *interface, const char *property,
                               GError **error, gpointer data)
{
    (void)connection; (void)sender; (void)path; (void)interface; (void)error; (void)data;
    if (g_str_equal(property, "Category")) return g_variant_new_string("Communications");
    if (g_str_equal(property, "Id")) return g_variant_new_string(BRIDGE_EXE);
    if (g_str_equal(property, "Title")) return g_variant_new_string("KakaoTalk");
    if (g_str_equal(property, "Status")) return g_variant_new_string("Active");
    if (g_str_equal(property, "IconName")) return g_variant_new_string(BRIDGE_ID);
    if (g_str_equal(property, "ItemIsMenu")) return g_variant_new_boolean(FALSE);
    return NULL;
}

static void tray_method(GDBusConnection *connection, const char *sender, const char *path,
                        const char *interface, const char *method, GVariant *args,
                        GDBusMethodInvocation *call, gpointer data)
{
    (void)connection; (void)sender; (void)path; (void)interface; (void)args; (void)data;
    g_autoptr(GError) error = NULL;
    gboolean ok = g_str_equal(method, "Activate") ? open_settings(&error) : open_downloads(&error);
    if (ok)
        g_dbus_method_invocation_return_value(call, NULL);
    else
        g_dbus_method_invocation_return_gerror(call, error);
}

static void tray_watcher(GDBusConnection *bus, const char *name, const char *owner, gpointer data)
{
    (void)name; (void)data;
    g_dbus_connection_call(bus, owner, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
                           "RegisterStatusNotifierItem", g_variant_new("(s)", "/StatusNotifierItem"),
                           NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, 3000, NULL, NULL, NULL);
}

static void tray_start(Bridge *bridge)
{
    static const GDBusInterfaceVTable vtable = {
        .method_call = tray_method, .get_property = tray_property
    };
    GDBusConnection *bus = g_application_get_dbus_connection(bridge->app);
    if (!bus)
        return;
    g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(tray_xml, NULL);
    bridge->tray_object = g_dbus_connection_register_object(bus, "/StatusNotifierItem",
                                                           info->interfaces[0], &vtable,
                                                           NULL, NULL, NULL);
    if (bridge->tray_object)
        bridge->tray_watch = g_bus_watch_name_on_connection(bus, "org.kde.StatusNotifierWatcher",
                                                           G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                           tray_watcher, NULL, NULL, NULL);
}

static GVariant *notification(const char *title, const char *body)
{
    GVariant *actions = g_variant_new_strv(NULL, 0);
    GVariantBuilder hints;
    g_variant_builder_init(&hints, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string(BRIDGE_ID));
    return g_variant_new("(susss@as@a{sv}i)", "Kakao Linux Bridge", 0u, BRIDGE_ID, title, body,
                         actions, g_variant_builder_end(&hints), -1);
}

static gboolean notify(Bridge *bridge, const char *title, const char *body, GError **error)
{
    GDBusConnection *bus = g_application_get_dbus_connection(bridge->app);
    if (!bus) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "A desktop D-Bus session is required.");
        return FALSE;
    }
    g_autoptr(GVariant) response = g_dbus_connection_call_sync(
        bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "Notify", notification(title, body),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, error);
    return response != NULL;
}

static void notify_attention(gpointer data)
{
    Bridge *bridge = data;
    GDBusConnection *bus = g_application_get_dbus_connection(bridge->app);
    g_dbus_connection_call(bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                          "org.freedesktop.Notifications", "Notify",
                          notification("카카오톡", "카카오톡에서 확인할 항목이 있습니다."),
                          G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL, NULL);
}

static GSubprocess *wine_spawn(Bridge *bridge, const char *const *args, gboolean client,
                              GError **error)
{
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_setenv(launcher, "WINEPREFIX", bridge->prefix, TRUE);
    if (client) {
        g_autofree char *directory = g_path_get_dirname(bridge->client);
        g_subprocess_launcher_set_cwd(launcher, directory);
    }
    if (client || bridge->korean_profile)
        bridge_wine_tuning_apply(launcher, bridge->korean_profile);
    return g_subprocess_launcher_spawnv(launcher, args, error);
}

static void clear_session(Bridge *bridge)
{
    GDBusConnection *bus = g_application_get_dbus_connection(bridge->app);
    if (bridge->tray_watch)
        g_bus_unwatch_name(bridge->tray_watch);
    if (bridge->tray_object)
        g_dbus_connection_unregister_object(bus, bridge->tray_object);
    bridge->tray_watch = bridge->tray_object = 0;
    g_clear_pointer(&bridge->attention, bridge_attention_free);
    g_clear_object(&bridge->child);
    g_clear_pointer(&bridge->wine, g_free);
    g_clear_pointer(&bridge->prefix, g_free);
    g_clear_pointer(&bridge->client, g_free);
    bridge->operation = NULL;
    bridge->stopping = FALSE;
    g_application_release(bridge->app);
}

static gboolean advance(Bridge *bridge, GError **error);

static void child_done(GObject *object, GAsyncResult *result, gpointer data)
{
    Bridge *bridge = data;
    g_autoptr(GError) error = NULL;
    gboolean ok = g_subprocess_wait_check_finish(G_SUBPROCESS(object), result, &error);
    g_clear_object(&bridge->child);
    if (bridge->stopping) {
        clear_session(bridge);
        return;
    }
    if (ok && !g_str_equal(bridge->operation, "client") &&
        (bridge->dpi || bridge->downloads || g_str_equal(bridge->operation, "configure"))) {
        if (advance(bridge, &error))
            return;
        ok = FALSE;
    }
    if (!ok) {
        bridge->exit_status = 1;
        g_printerr("%s: %s\n", bridge->operation, error->message);
        notify(bridge, "Kakao Linux Bridge", "A Wine operation failed. Check Bridge Settings or the terminal output.", NULL);
    }
    clear_session(bridge);
}

static gboolean watch_child(Bridge *bridge, const char *const *args, const char *operation, GError **error)
{
    bridge->operation = operation;
    bridge->child = wine_spawn(bridge, args, g_str_equal(operation, "client"), error);
    if (!bridge->child)
        return FALSE;
    g_subprocess_wait_check_async(bridge->child, NULL, child_done, bridge);
    return TRUE;
}

static gboolean advance(Bridge *bridge, GError **error)
{
    if (bridge->dpi) {
        g_autofree char *value = g_strdup_printf("%u", bridge->dpi);
        bridge->dpi = 0;
        const char *args[] = { bridge->wine, "reg", "add", "HKCU\\Control Panel\\Desktop",
                               "/v", "LogPixels", "/t", "REG_DWORD", "/d", value, "/f", NULL };
        return watch_child(bridge, args, "configure", error);
    }
    if (bridge->downloads) {
        bridge->downloads = FALSE;
        const char *args[] = { bridge->wine, "reg", "add",
                               "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
                               "/v", "{374DE290-123F-4565-9164-39C4925E467B}", "/t", "REG_SZ",
                               "/d", "C:\\Ubuntu Downloads", "/f", NULL };
        return watch_child(bridge, args, "configure", error);
    }
    const char *args[] = { bridge->wine, bridge->client, NULL };
    if (!watch_child(bridge, args, "client", error))
        return FALSE;
    if (bridge->watch_attention) {
        const char *identifier = g_subprocess_get_identifier(bridge->child);
        guint64 pid = identifier ? g_ascii_strtoull(identifier, NULL, 10) : 0;
        if (pid > 0 && pid <= G_MAXUINT32) {
            g_autoptr(GError) attention_error = NULL;
            g_autofree char *name = g_path_get_basename(bridge->client);
            bridge->attention = bridge_attention_watch((guint32)pid, name, notify_attention,
                                                       bridge, &attention_error);
            if (!bridge->attention)
                g_printerr("KakaoTalk attention notifications: %s\n", attention_error->message);
        }
    }
    tray_start(bridge);
    return TRUE;
}

static gboolean setup_downloads(const char *prefix, GError **error)
{
    g_autofree char *dir = bridge_downloads();
    g_autofree char *alias = g_build_filename(prefix, "drive_c", "Ubuntu Downloads", NULL);
    g_autofree char *target = g_file_read_link(alias, NULL);
    if (g_strcmp0(target, dir) == 0)
        return TRUE;
    if (g_mkdir_with_parents(dir, 0700) == 0 && symlink(dir, alias) == 0)
        return TRUE;
    const int code = errno;
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(code),
                "%s: %s", alias, g_strerror(code));
    return FALSE;
}

static void stop_done(GObject *object, GAsyncResult *result, gpointer data)
{
    Bridge *bridge = data;
    g_autoptr(GError) error = NULL;
    if (!g_subprocess_wait_check_finish(G_SUBPROCESS(object), result, &error) &&
        bridge->stopping && bridge->child) {
        bridge->stopping = FALSE;
        g_printerr("KakaoTalk quit request: %s\n", error->message);
    }
    bridge->stop_pending = FALSE;
    g_application_release(bridge->app);
}

static gboolean request_stop(Bridge *bridge, GError **error)
{
    if (!bridge->child || bridge->stopping)
        return TRUE;
    if (!g_str_equal(bridge->operation, "client")) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "A Wine setup operation is in progress.");
        return FALSE;
    }
    const char *args[] = { bridge->wine, "taskkill", "/IM", "KakaoTalk.exe", NULL };
    g_autoptr(GSubprocess) stop = wine_spawn(bridge, args, FALSE, error);
    if (!stop)
        return FALSE;
    bridge->stopping = TRUE;
    bridge->stop_pending = TRUE;
    g_application_hold(bridge->app);
    g_subprocess_wait_check_async(stop, NULL, stop_done, bridge);
    return TRUE;
}

static gboolean on_signal(gpointer data)
{
    g_autoptr(GError) error = NULL;
    if (!request_stop(data, &error))
        g_printerr("%s\n", error->message);
    return G_SOURCE_CONTINUE;
}

static int command_line(GApplication *app, GApplicationCommandLine *line, gpointer data)
{
    Bridge *bridge = data;
    gint argc;
    g_auto(GStrv) argv = g_application_command_line_get_arguments(line, &argc);
    const char *command = argc > 1 ? argv[1] : "start";
    g_autoptr(GError) error = NULL;
    g_autoptr(GKeyFile) config = NULL;
    g_autofree char *wine = NULL;
    g_autofree char *prefix = NULL;
    g_autofree char *managed = NULL;
    g_autofree char *client = NULL;
    g_autofree char *installer = NULL;
    if (g_str_equal(command, "--help") || g_str_equal(command, "help")) {
        g_application_command_line_print(line, "Usage: " BRIDGE_EXE
            " [start|stop|status|doctor|settings|downloads|test-notification|install-client FILE|repair|disable-autostart]\n");
        return 0;
    }
    if (g_str_equal(command, "status")) {
        g_application_command_line_print(line, "%s\n", bridge->operation ? bridge->operation : "idle");
        return 0;
    }
    if (g_str_equal(command, "test-notification")) {
        if (notify(bridge, "Kakao Linux Bridge", "Test notification", &error)) return 0;
        goto failed;
    }
    if (g_str_equal(command, "settings")) {
        if (open_settings(&error)) return 0;
        goto failed;
    }
    if (g_str_equal(command, "downloads")) {
        if (open_downloads(&error)) return 0;
        goto failed;
    }
    if (g_str_equal(command, "stop")) {
        if (request_stop(bridge, &error)) return 0;
        goto failed;
    }
    if (g_str_equal(command, "disable-autostart")) {
        if (bridge_autostart_set(FALSE, &error)) return 0;
        goto failed;
    }

    gboolean starting = g_str_equal(command, "start") && argc <= 2;
    gboolean installing = g_str_equal(command, "install-client") && argc == 3;
    gboolean repairing = g_str_equal(command, "repair") && argc == 2;
    gboolean doctor = g_str_equal(command, "doctor") && argc == 2;
    if (!starting && !installing && !repairing && !doctor) {
        g_application_command_line_printerr(line, "Invalid command. See " BRIDGE_EXE " --help.\n");
        return 2;
    }
    if (!doctor && (bridge->child || bridge->stop_pending)) {
        if (starting && bridge->operation && g_str_equal(bridge->operation, "client") && !bridge->stopping)
            return 0;
        g_application_command_line_printerr(line, "Bridge is busy with %s.\n", bridge->operation ? bridge->operation : "stop");
        return 1;
    }
    config = bridge_config_load(&error);
    if (!config)
        goto failed;
    wine = bridge_find_wine(config);
    prefix = bridge_find_prefix(config);
    managed = bridge_managed_prefix();
    client = prefix ? bridge_find_client(config, prefix) : NULL;
    gboolean korean_profile = g_key_file_get_boolean(config, "Bridge", BRIDGE_KOREAN_PROFILE, NULL);
    if (doctor) {
        g_autofree char *downloads = bridge_downloads();
        g_application_command_line_print(line, "Wine: %s\nPrefix: %s\nClient: %s\nDesktop: %s\nDownloads: %s\n",
             wine ? wine : "missing", prefix ? prefix : "invalid", client ? client : "missing",
             g_getenv("XDG_SESSION_TYPE") ? g_getenv("XDG_SESSION_TYPE") : "unknown", downloads);
        BridgeWineTuning tuning = bridge_wine_tuning_probe(korean_profile);
        g_application_command_line_print(line,
            "Korean profile: %s\nX11/XWayland: %s\nXIM: %s\nKorean UTF-8 locale: %s\nIBus Hangul schema: %s\n",
            korean_profile ? "enabled" : "off", tuning.x11_available ? "available" : "unavailable",
            tuning.xim ? tuning.xim : "desktop default",
            tuning.korean_locale_available ? "installed" : "missing",
            tuning.ibus_hangul_available ? "installed" : "not found");
        return wine && prefix && client ? 0 : 1;
    }
    if (!g_application_get_dbus_connection(app)) {
        g_application_command_line_printerr(line, "A desktop D-Bus session is required.\n");
        return 1;
    }
    if (!wine || !prefix || (starting && !client)) {
        g_application_command_line_printerr(line, "Wine or KakaoTalk is missing. Run doctor or open Bridge Settings.\n");
        if (starting && !g_application_command_line_get_is_remote(line))
            open_settings(NULL);
        return 1;
    }
    if (installing || repairing) {
        if (!g_str_equal(prefix, managed)) {
            g_application_command_line_printerr(line, "This operation requires the Bridge-managed prefix.\n");
            return 1;
        }
        if (installing) {
            installer = g_canonicalize_filename(argv[2], g_application_command_line_get_cwd(line));
            if (!g_str_has_suffix(installer, ".exe") || !g_file_test(installer, G_FILE_TEST_IS_REGULAR)) {
                g_application_command_line_printerr(line, "Select the official local KakaoTalk installer (.exe).\n");
                return 1;
            }
        }
        g_autofree char *parent = g_path_get_dirname(prefix);
        if (g_mkdir_with_parents(parent, 0700) < 0) {
            g_application_command_line_printerr(line, "%s: %s\n", parent, g_strerror(errno));
            return 1;
        }
    }
    gint dpi = g_key_file_get_integer(config, "Bridge", "DPI", NULL);
    gboolean map_downloads = starting && g_str_equal(prefix, managed) &&
        g_key_file_get_boolean(config, "Bridge", "MapDownloads", NULL);
    if (map_downloads && !setup_downloads(prefix, &error))
        goto failed;

    bridge->exit_status = 0;
    bridge->wine = g_steal_pointer(&wine);
    bridge->prefix = g_steal_pointer(&prefix);
    bridge->client = g_steal_pointer(&client);
    bridge->dpi = starting && dpi >= 96 && dpi <= 384 ? (guint)dpi : 0;
    bridge->downloads = map_downloads;
    bridge->korean_profile = korean_profile;
    bridge->watch_attention = starting && g_key_file_get_boolean(config, "Bridge", "WatchAttention", NULL);
    g_application_hold(app);
    gboolean ok;
    if (installing) {
        const char *args[] = { bridge->wine, installer, NULL };
        ok = watch_child(bridge, args, "install", &error);
    } else if (repairing) {
        const char *args[] = { bridge->wine, "wineboot", "-u", NULL };
        ok = watch_child(bridge, args, "repair", &error);
    } else {
        ok = advance(bridge, &error);
    }
    if (ok)
        return 0;
    clear_session(bridge);

failed:
    g_application_command_line_printerr(line, "%s\n", error->message);
    return 1;
}

int main(int argc, char **argv)
{
    g_autoptr(GApplication) app = g_application_new(BRIDGE_ID, G_APPLICATION_HANDLES_COMMAND_LINE);
    Bridge bridge = { .app = app };
    g_signal_connect(app, "command-line", G_CALLBACK(command_line), &bridge);
    guint term = g_unix_signal_add(SIGTERM, on_signal, &bridge);
    guint interrupt = g_unix_signal_add(SIGINT, on_signal, &bridge);
    int result = g_application_run(app, argc, argv);
    g_source_remove(interrupt);
    g_source_remove(term);
    return result ? result : bridge.exit_status;
}
