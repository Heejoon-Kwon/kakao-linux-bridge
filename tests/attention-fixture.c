#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>

static void receive_notification(GDBusConnection *bus, const char *sender, const char *path,
                                 const char *interface, const char *method, GVariant *args,
                                 GDBusMethodInvocation *call, gpointer data)
{
    (void)bus; (void)sender; (void)path; (void)interface; (void)method; (void)data;
    const char *title;
    const char *body;
    g_variant_get_child(args, 3, "&s", &title);
    g_variant_get_child(args, 4, "&s", &body);
    printf("%s|%s\n", title, body);
    fflush(stdout);
    g_dbus_method_invocation_return_value(call, g_variant_new("(u)", 1u));
}

static int serve_notifications(void)
{
    const char *xml =
        "<node><interface name='org.freedesktop.Notifications'><method name='Notify'>"
        "<arg type='s'/><arg type='u'/><arg type='s'/><arg type='s'/><arg type='s'/>"
        "<arg type='as'/><arg type='a{sv}'/><arg type='i'/><arg type='u' direction='out'/>"
        "</method></interface></node>";
    static const GDBusInterfaceVTable vtable = { .method_call = receive_notification };
    g_autoptr(GDBusConnection) bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    g_assert_nonnull(bus);
    g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(xml, NULL);
    guint object = g_dbus_connection_register_object(bus, "/org/freedesktop/Notifications",
                                                     info->interfaces[0], &vtable, NULL, NULL, NULL);
    g_assert_cmpuint(object, >, 0);
    g_autoptr(GVariant) reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
        g_variant_new("(su)", "org.freedesktop.Notifications", 0u), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
    g_assert_nonnull(reply);
    guint result;
    g_variant_get(reply, "(u)", &result);
    g_assert_cmpuint(result, ==, 1);
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    return 0;
}

static xcb_atom_t atom(xcb_connection_t *connection, const char *name)
{
    g_autofree xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(connection, xcb_intern_atom(connection, FALSE, strlen(name), name), NULL);
    g_assert_nonnull(reply);
    return reply->atom;
}

static int emit_attention(guint32 pid, const char *mode)
{
    xcb_connection_t *connection = xcb_connect(NULL, NULL);
    g_assert_cmpint(xcb_connection_has_error(connection), ==, 0);
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    xcb_window_t target = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, target, screen->root, 0, 0, 50, 50, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, NULL);
    const char *class = g_str_equal(mode, "wrong-class") ? "other.exe" : "kakaotalk.exe";
    char class_property[32] = { 0 };
    memcpy(class_property, class, strlen(class));
    memcpy(class_property + strlen(class) + 1, class, strlen(class));
    if (g_str_equal(mode, "wrong-pid"))
        pid++;
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, target, atom(connection, "_NET_WM_PID"),
                        XCB_ATOM_CARDINAL, 32, 1, &pid);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, target, XCB_ATOM_WM_CLASS,
                        XCB_ATOM_STRING, 8, 2 * (strlen(class) + 1), class_property);
    xcb_client_message_event_t event = {
        .response_type = XCB_CLIENT_MESSAGE, .format = 32, .window = target,
        .type = atom(connection, "_NET_WM_STATE"),
        .data.data32 = { !g_str_equal(mode, "remove"),
                         atom(connection, "_NET_WM_STATE_DEMANDS_ATTENTION"), 0, 1, 0 }
    };
    guint count = g_str_equal(mode, "valid") ? 2 : 1;
    for (guint i = 0; i < count; i++) {
        xcb_void_cookie_t sent = xcb_send_event_checked(connection, FALSE, screen->root,
            XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, (const char *)&event);
        g_autofree xcb_generic_error_t *error = xcb_request_check(connection, sent);
        g_assert_null(error);
    }
    g_usleep(G_USEC_PER_SEC / 3);
    xcb_disconnect(connection);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && g_str_equal(argv[1], "serve"))
        return serve_notifications();
    if (argc == 3)
        return emit_attention((guint32)g_ascii_strtoull(argv[1], NULL, 10), argv[2]);
    return 2;
}
