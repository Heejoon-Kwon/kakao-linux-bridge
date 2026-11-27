#include "tray-menu.h"

typedef struct {
    BridgeMenuAction action;
    gpointer data;
} Menu;

static const char *items[] = { NULL, "Bridge Settings", "Open Downloads", "Quit KakaoTalk" };
static const char xml[] =
    "<node><interface name='com.canonical.dbusmenu'>"
    "<property name='Version' type='u' access='read'/>"
    "<property name='Status' type='s' access='read'/>"
    "<property name='TextDirection' type='s' access='read'/>"
    "<property name='IconThemePath' type='as' access='read'/>"
    "<method name='GetLayout'><arg type='i'/><arg type='i'/><arg type='as'/>"
    "<arg type='u' direction='out'/><arg type='(ia{sv}av)' direction='out'/></method>"
    "<method name='GetGroupProperties'><arg type='ai'/><arg type='as'/>"
    "<arg type='a(ia{sv})' direction='out'/></method>"
    "<method name='AboutToShow'><arg type='i'/><arg type='b' direction='out'/></method>"
    "<method name='Event'><arg type='i'/><arg type='s'/><arg type='v'/><arg type='u'/></method>"
    "</interface></node>";

static GVariant *properties(gint id)
{
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
    if (id == 0)
        g_variant_builder_add(&props, "{sv}", "children-display", g_variant_new_string("submenu"));
    else
        g_variant_builder_add(&props, "{sv}", "label", g_variant_new_string(items[id]));
    return g_variant_builder_end(&props);
}

static GVariant *layout(gint id, gint depth)
{
    GVariantBuilder children;
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    if (id == 0 && depth)
        for (gint i = 1; i < (gint)G_N_ELEMENTS(items); i++)
            g_variant_builder_add(&children, "v", layout(i, 0));
    return g_variant_new("(i@a{sv}@av)", id, properties(id), g_variant_builder_end(&children));
}

static gboolean valid(gint id)
{
    return id >= 0 && (guint)id < G_N_ELEMENTS(items);
}

static void menu_call(GDBusConnection *bus, const char *sender, const char *path,
                      const char *interface, const char *method, GVariant *args,
                      GDBusMethodInvocation *call, gpointer data)
{
    (void)bus; (void)sender; (void)path; (void)interface;
    Menu *menu = data;
    if (g_str_equal(method, "GetLayout")) {
        gint id, depth;
        g_variant_get_child(args, 0, "i", &id);
        g_variant_get_child(args, 1, "i", &depth);
        if (!valid(id)) {
            g_dbus_method_invocation_return_error_literal(call, G_DBUS_ERROR,
                                                          G_DBUS_ERROR_INVALID_ARGS, "Invalid item.");
            return;
        }
        g_dbus_method_invocation_return_value(call, g_variant_new("(u@(ia{sv}av))", 1u, layout(id, depth)));
    } else if (g_str_equal(method, "GetGroupProperties")) {
        g_autoptr(GVariant) ids = g_variant_get_child_value(args, 0);
        GVariantBuilder result;
        g_variant_builder_init(&result, G_VARIANT_TYPE("a(ia{sv})"));
        GVariantIter iter;
        gint id;
        g_variant_iter_init(&iter, ids);
        while (g_variant_iter_next(&iter, "i", &id))
            if (valid(id))
                g_variant_builder_add(&result, "(i@a{sv})", id, properties(id));
        g_dbus_method_invocation_return_value(call, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&result)));
    } else if (g_str_equal(method, "AboutToShow")) {
        g_dbus_method_invocation_return_value(call, g_variant_new("(b)", FALSE));
    } else {
        gint id;
        const char *event;
        g_variant_get_child(args, 0, "i", &id);
        g_variant_get_child(args, 1, "&s", &event);
        if (valid(id) && id && g_str_equal(event, "clicked"))
            menu->action(id, menu->data);
        g_dbus_method_invocation_return_value(call, NULL);
    }
}

static GVariant *menu_property(GDBusConnection *bus, const char *sender, const char *path,
                               const char *interface, const char *name, GError **error, gpointer data)
{
    (void)bus; (void)sender; (void)path; (void)interface; (void)error; (void)data;
    if (g_str_equal(name, "Version")) return g_variant_new_uint32(3);
    if (g_str_equal(name, "Status")) return g_variant_new_string("normal");
    if (g_str_equal(name, "TextDirection")) return g_variant_new_string("ltr");
    return g_variant_new_strv(NULL, 0);
}

guint bridge_menu_export(GDBusConnection *bus, BridgeMenuAction action, gpointer data)
{
    static const GDBusInterfaceVTable vtable = { .method_call = menu_call, .get_property = menu_property };
    g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(xml, NULL);
    Menu *menu = g_new(Menu, 1);
    *menu = (Menu){ .action = action, .data = data };
    guint handle = g_dbus_connection_register_object(bus, BRIDGE_MENU_PATH, info->interfaces[0], &vtable,
                                                     menu, g_free, NULL);
    if (!handle)
        g_free(menu);
    return handle;
}
