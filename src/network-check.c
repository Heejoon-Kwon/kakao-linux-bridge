#include <gio/gio.h>

typedef struct {
    GMainLoop *loop;
    guint pending;
    gboolean failed;
} NetworkCheck;

typedef struct {
    NetworkCheck *check;
    const char *host;
} Lookup;

static void report_lookup(GObject *resolver, GAsyncResult *result, gpointer data)
{
    Lookup *lookup = data;
    NetworkCheck *check = lookup->check;
    g_autoptr(GError) error = NULL;
    GList *addresses = g_resolver_lookup_by_name_finish(G_RESOLVER(resolver), result, &error);
    if (addresses) {
        g_print("%s: DNS OK\n", lookup->host);
        g_resolver_free_addresses(addresses);
    } else {
        g_print("%s: %s\n", lookup->host, error->message);
        check->failed = TRUE;
    }
    if (--check->pending == 0)
        g_main_loop_quit(check->loop);
}

static gboolean cancel_lookup(gpointer data)
{
    g_cancellable_cancel(data);
    return G_SOURCE_REMOVE;
}

int bridge_check_network(void)
{
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GResolver) resolver = g_resolver_get_default();
    g_autoptr(GCancellable) cancellable = g_cancellable_new();
    NetworkCheck check = { .loop = loop };
    Lookup lookups[] = {
        { &check, "katalk.kakao.com" },
        { &check, "booking-loco.kakao.com" },
        { &check, "ticket-loco.kakao.com" },
    };
    check.pending = G_N_ELEMENTS(lookups);
    g_print("Ubuntu DNS results\n");
    for (guint i = 0; i < G_N_ELEMENTS(lookups); i++)
        g_resolver_lookup_by_name_async(resolver, lookups[i].host, cancellable,
                                       report_lookup, &lookups[i]);
    g_autoptr(GSource) timer = g_timeout_source_new_seconds(6);
    g_source_set_callback(timer, cancel_lookup, cancellable, NULL);
    g_source_attach(timer, NULL);
    g_main_loop_run(loop);
    g_source_destroy(timer);
    return check.failed ? 1 : 0;
}
