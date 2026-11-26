#include "attention.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <string.h>
#include <xcb/xcb.h>

struct BridgeAttention {
    xcb_connection_t *connection;
    xcb_atom_t state;
    xcb_atom_t attention;
    xcb_atom_t pid_atom;
    guint32 pid;
    char *window_class;
    guint source;
    gint64 last_notification;
    BridgeAttentionFunc callback;
    gpointer data;
};

static xcb_atom_t intern(xcb_connection_t *connection, const char *name)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, FALSE, strlen(name), name);
    g_autofree xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, NULL);
    return reply ? reply->atom : XCB_ATOM_NONE;
}

static gboolean is_client(BridgeAttention *watch, xcb_window_t window)
{
    xcb_connection_t *connection = watch->connection;
    xcb_get_property_cookie_t pid_cookie = xcb_get_property(
        connection, FALSE, window, watch->pid_atom, XCB_ATOM_CARDINAL, 0, 1);
    g_autofree xcb_get_property_reply_t *pid = xcb_get_property_reply(connection, pid_cookie, NULL);
    if (!pid || pid->format != 32 || pid->value_len != 1 || pid->bytes_after ||
        *(const guint32 *)xcb_get_property_value(pid) != watch->pid)
        return FALSE;

    xcb_get_property_cookie_t class_cookie = xcb_get_property(
        connection, FALSE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 128);
    g_autofree xcb_get_property_reply_t *class = xcb_get_property_reply(connection, class_cookie, NULL);
    if (!class || class->format != 8 || class->bytes_after)
        return FALSE;

    const char *instance = xcb_get_property_value(class);
    const char *end = instance + xcb_get_property_value_length(class);
    const char *separator = memchr(instance, '\0', end - instance);
    if (!separator || separator + 1 == end || end[-1] != '\0')
        return FALSE;
    const char *name = separator + 1;
    return strlen(name) == (gsize)(end - name - 1) &&
           g_ascii_strcasecmp(name, watch->window_class) == 0;
}

static gboolean handle_events(gint fd, GIOCondition condition, gpointer data)
{
    (void)fd;
    BridgeAttention *watch = data;
    if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        watch->source = 0;
        return G_SOURCE_REMOVE;
    }

    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(watch->connection))) {
        g_autofree xcb_generic_event_t *owned = event;
        if ((event->response_type & 0x7f) != XCB_CLIENT_MESSAGE)
            continue;

        const xcb_client_message_event_t *message = (const void *)event;
        const guint32 *fields = message->data.data32;
        const gint64 now = g_get_monotonic_time();
        if (message->type != watch->state || message->format != 32 || fields[0] != 1 ||
            (fields[1] != watch->attention && fields[2] != watch->attention) ||
            (watch->last_notification && now - watch->last_notification < 3 * G_USEC_PER_SEC) ||
            !is_client(watch, message->window))
            continue;

        watch->last_notification = now;
        watch->callback(watch->data);
    }

    if (xcb_connection_has_error(watch->connection)) {
        watch->source = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

BridgeAttention *bridge_attention_watch(guint32 pid, const char *window_class,
                                        BridgeAttentionFunc callback, gpointer data,
                                        GError **error)
{
    int screen_number;
    xcb_connection_t *connection = xcb_connect(NULL, &screen_number);
    if (xcb_connection_has_error(connection)) {
        xcb_disconnect(connection);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "X11 display unavailable.");
        return NULL;
    }

    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(connection));
    for (int i = 0; i < screen_number && screens.rem; i++)
        xcb_screen_next(&screens);

    BridgeAttention *watch = g_new0(BridgeAttention, 1);
    watch->connection = connection;
    watch->state = intern(connection, "_NET_WM_STATE");
    watch->attention = intern(connection, "_NET_WM_STATE_DEMANDS_ATTENTION");
    watch->pid_atom = intern(connection, "_NET_WM_PID");
    if (!screens.rem || !watch->state || !watch->attention || !watch->pid_atom) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "X11 window state metadata unavailable.");
        bridge_attention_free(watch);
        return NULL;
    }

    guint32 mask = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    xcb_void_cookie_t select = xcb_change_window_attributes_checked(
        connection, screens.data->root, XCB_CW_EVENT_MASK, &mask);
    g_autofree xcb_generic_error_t *select_error = xcb_request_check(connection, select);
    if (select_error || xcb_connection_has_error(connection)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not subscribe to X11 window events.");
        bridge_attention_free(watch);
        return NULL;
    }

    watch->pid = pid;
    watch->window_class = g_strdup(window_class);
    watch->callback = callback;
    watch->data = data;
    watch->source = g_unix_fd_add(xcb_get_file_descriptor(connection), G_IO_IN | G_IO_ERR | G_IO_HUP,
                                   handle_events, watch);
    return watch;
}

void bridge_attention_free(BridgeAttention *watch)
{
    if (!watch)
        return;
    if (watch->source)
        g_source_remove(watch->source);
    xcb_disconnect(watch->connection);
    g_free(watch->window_class);
    g_free(watch);
}
