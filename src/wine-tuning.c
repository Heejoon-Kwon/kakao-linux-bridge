#define _POSIX_C_SOURCE 200809L
#include "wine-tuning.h"
#include <locale.h>
#include <xcb/xcb.h>

static gboolean has_gnome_hangul_source(GSettingsSchemaSource *source)
{
    if (!source)
        return FALSE;
    g_autoptr(GSettingsSchema) schema =
        g_settings_schema_source_lookup(source, "org.gnome.desktop.input-sources", TRUE);
    if (!schema || !g_settings_schema_has_key(schema, "sources"))
        return FALSE;
    g_autoptr(GSettings) settings = g_settings_new_full(schema, NULL, NULL);
    g_autoptr(GVariant) sources = g_settings_get_value(settings, "sources");
    GVariantIter entries;
    const char *kind;
    const char *name;
    g_variant_iter_init(&entries, sources);
    while (g_variant_iter_next(&entries, "(&s&s)", &kind, &name)) {
        if (g_str_equal(kind, "ibus") && g_str_equal(name, "hangul"))
            return TRUE;
    }
    return FALSE;
}

static const char *detect_xim(GSettingsSchemaSource *schemas)
{
    const char *explicit = g_getenv("XMODIFIERS");
    if (explicit && *explicit)
        return explicit;
    const char *ime = g_getenv("GTK_IM_MODULE");
    if (g_strcmp0(ime, "fcitx") == 0 || g_strcmp0(ime, "fcitx5") == 0)
        return "@im=fcitx";
    if (g_strcmp0(ime, "ibus") == 0 || has_gnome_hangul_source(schemas))
        return "@im=ibus";
    return NULL;
}

static gboolean display_is_available(void)
{
    const char *display = g_getenv("DISPLAY");
    if (!display || !*display)
        return FALSE;
    xcb_connection_t *connection = xcb_connect(display, NULL);
    gboolean available = !xcb_connection_has_error(connection);
    xcb_disconnect(connection);
    return available;
}

static gboolean has_korean_locale(void)
{
    locale_t locale = newlocale(LC_CTYPE_MASK, BRIDGE_KOREAN_LOCALE, (locale_t)0);
    if (!locale)
        return FALSE;
    freelocale(locale);
    return TRUE;
}

BridgeWineTuning bridge_wine_tuning_probe(gboolean korean_profile)
{
    GSettingsSchemaSource *schemas = g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) hangul =
        schemas ? g_settings_schema_source_lookup(schemas, "org.freedesktop.ibus.engine.hangul", TRUE) : NULL;
    gboolean display = display_is_available();
    return (BridgeWineTuning) {
        .xim = korean_profile && display ? "@im=ibus" : detect_xim(schemas),
        .x11_available = display,
        .korean_locale_available = has_korean_locale(),
        .ibus_hangul_available = hangul != NULL,
    };
}

void bridge_wine_tuning_apply(GSubprocessLauncher *launcher, gboolean korean_profile)
{
    GSettingsSchemaSource *schemas = g_settings_schema_source_get_default();
    if (korean_profile && display_is_available()) {
        const char *existing = g_getenv("WINEDLLOVERRIDES");
        g_autofree char *overrides = existing && *existing
            ? g_strconcat(existing, ";winewayland.drv=", NULL) : g_strdup("winewayland.drv=");
        g_subprocess_launcher_setenv(launcher, "WINEDLLOVERRIDES", overrides, TRUE);
        g_subprocess_launcher_setenv(launcher, "GTK_IM_MODULE", "ibus", TRUE);
        g_subprocess_launcher_setenv(launcher, "QT_IM_MODULE", "ibus", TRUE);
        g_subprocess_launcher_setenv(launcher, "XMODIFIERS", "@im=ibus", TRUE);
    } else {
        const char *xim = detect_xim(schemas);
        if (xim)
            g_subprocess_launcher_setenv(launcher, "XMODIFIERS", xim, TRUE);
    }

    if (korean_profile && has_korean_locale()) {
        g_subprocess_launcher_unsetenv(launcher, "LC_ALL");
        g_subprocess_launcher_setenv(launcher, "LANG", BRIDGE_KOREAN_LOCALE, TRUE);
        g_subprocess_launcher_setenv(launcher, "LC_CTYPE", BRIDGE_KOREAN_LOCALE, TRUE);
    }
    g_subprocess_launcher_setenv(launcher, "WINEDEBUG", "fixme-all", FALSE);
}
