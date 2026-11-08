#include "theme.h"
#include "settings.h"

#define PORTAL "org.freedesktop.portal.Desktop"
#define OBJECT "/org/freedesktop/portal/desktop"
#define SETTINGS "org.freedesktop.portal.Settings"
#define APPEARANCE "org.freedesktop.appearance"
#define KEY "color-scheme"

static gboolean is_dark(GVariant *value)
{
    g_autoptr(GVariant) owned = g_variant_ref(value);
    while (g_variant_is_of_type(owned, G_VARIANT_TYPE_VARIANT)) {
        GVariant *inner = g_variant_get_variant(owned);
        g_variant_unref(owned);
        owned = inner;
    }
    return g_variant_is_of_type(owned, G_VARIANT_TYPE_UINT32) &&
           g_variant_get_uint32(owned) == 1;
}

static void changed(GDBusConnection *bus, const gchar *sender, const gchar *path,
                    const gchar *interface, const gchar *signal, GVariant *args, gpointer data)
{
    Theme *theme = data;
    if (!g_variant_is_of_type(args, G_VARIANT_TYPE("(ssv)"))) return;
    const gchar *group, *key;
    g_autoptr(GVariant) value = NULL;
    g_variant_get(args, "(&s&sv)", &group, &key, &value);
    if (!g_str_equal(group, APPEARANCE) || !g_str_equal(key, KEY)) return;
    gboolean dark = is_dark(value);
    if (theme->dark == dark) return;
    theme->dark = dark;
    if (theme->repaint) theme->repaint(theme->data);
}

void theme_start(Theme *theme, void (*repaint)(gpointer), gpointer data)
{
    if (link_settings.theme != LINK_SYSTEM) {
        theme->dark = link_settings.theme == LINK_DARK;
        return;
    }
    theme->repaint = repaint;
    theme->data = data;
    theme->bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!theme->bus) return;
    theme->subscription = g_dbus_connection_signal_subscribe(theme->bus, PORTAL, SETTINGS,
        "SettingChanged", OBJECT, APPEARANCE, G_DBUS_SIGNAL_FLAGS_NONE, changed, theme, NULL);
    /* Read also works with older portals; its result can contain a nested variant. */
    g_autoptr(GVariant) reply = g_dbus_connection_call_sync(theme->bus, PORTAL, OBJECT,
        SETTINGS, "Read", g_variant_new("(ss)", APPEARANCE, KEY), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
    if (reply) {
        g_autoptr(GVariant) value = g_variant_get_child_value(reply, 0);
        theme->dark = is_dark(value);
    }
}

void theme_stop(Theme *theme)
{
    if (!theme->bus) return;
    g_dbus_connection_signal_unsubscribe(theme->bus, theme->subscription);
    g_clear_object(&theme->bus);
}
