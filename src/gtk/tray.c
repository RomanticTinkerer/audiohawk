/*
 * Minimal StatusNotifierItem for AudioHawk.
 * Works with KDE Plasma and GNOME (AppIndicator extension).
 */
#include "tray.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef AUDIOHAWK_SOURCE_DIR
#define AUDIOHAWK_SOURCE_DIR ""
#endif

struct AhTray {
    GtkApplication *app;
    AhTrayShowFn on_show;
    AhTrayQuitFn on_quit;
    gpointer user_data;
    GDBusConnection *conn;
    guint own_id;
    guint item_reg;
    guint menu_reg;
    guint watcher_watch;
    gchar *bus_name;
    gchar *icon_name;
    gchar *icon_theme_path;
    GVariant *icon_pixmap;
    gboolean registered_with_watcher;
};

static GVariant *build_menu_layout(void)
{
    GVariantBuilder show_props;
    g_variant_builder_init(&show_props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&show_props, "{sv}", "label",
                          g_variant_new_string("_Show AudioHawk"));
    g_variant_builder_add(&show_props, "{sv}", "enabled",
                          g_variant_new_boolean(TRUE));
    g_variant_builder_add(&show_props, "{sv}", "visible",
                          g_variant_new_boolean(TRUE));

    GVariantBuilder quit_props;
    g_variant_builder_init(&quit_props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&quit_props, "{sv}", "label",
                          g_variant_new_string("_Quit AudioHawk"));
    g_variant_builder_add(&quit_props, "{sv}", "enabled",
                          g_variant_new_boolean(TRUE));
    g_variant_builder_add(&quit_props, "{sv}", "visible",
                          g_variant_new_boolean(TRUE));

    GVariantBuilder empty_children;
    g_variant_builder_init(&empty_children, G_VARIANT_TYPE("av"));

    GVariant *show = g_variant_new("(ia{sv}av)", 1, &show_props, &empty_children);

    g_variant_builder_init(&empty_children, G_VARIANT_TYPE("av"));
    GVariant *quit = g_variant_new("(ia{sv}av)", 2, &quit_props, &empty_children);

    GVariantBuilder children;
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    g_variant_builder_add(&children, "v", show);
    g_variant_builder_add(&children, "v", quit);

    GVariantBuilder root_props;
    g_variant_builder_init(&root_props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&root_props, "{sv}", "children-display",
                          g_variant_new_string("submenu"));

    return g_variant_new("(ia{sv}av)", 0, &root_props, &children);
}

static void menu_method(GDBusConnection *conn, const gchar *sender,
                        const gchar *path, const gchar *iface,
                        const gchar *method, GVariant *params,
                        GDBusMethodInvocation *inv, gpointer user_data)
{
    AhTray *tray = user_data;
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;

    if (strcmp(method, "GetLayout") == 0) {
        GVariant *layout = build_menu_layout();
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(u@(ia{sv}av))", (guint32)1, layout));
        return;
    }
    if (strcmp(method, "GetGroupProperties") == 0) {
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(a(ia{sv}))", NULL));
        return;
    }
    if (strcmp(method, "GetProperty") == 0) {
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(v)", g_variant_new_string("")));
        return;
    }
    if (strcmp(method, "Event") == 0) {
        gint32 id = 0;
        const gchar *event_id = NULL;
        g_variant_get(params, "(isvu)", &id, &event_id, NULL, NULL);
        if (event_id && strcmp(event_id, "clicked") == 0) {
            if (id == 1 && tray->on_show)
                tray->on_show(tray->user_data);
            else if (id == 2 && tray->on_quit)
                tray->on_quit(tray->user_data);
        }
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    if (strcmp(method, "AboutToShow") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", FALSE));
        return;
    }
    g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "Unknown method %s", method);
}

static GVariant *menu_get_prop(GDBusConnection *c, const gchar *s,
                               const gchar *p, const gchar *i,
                               const gchar *prop, GError **e, gpointer d)
{
    (void)c;
    (void)s;
    (void)p;
    (void)i;
    (void)e;
    (void)d;
    if (strcmp(prop, "Version") == 0)
        return g_variant_new_uint32(3);
    if (strcmp(prop, "Status") == 0)
        return g_variant_new_string("normal");
    return NULL;
}

static void item_method(GDBusConnection *conn, const gchar *sender,
                        const gchar *path, const gchar *iface,
                        const gchar *method, GVariant *params,
                        GDBusMethodInvocation *inv, gpointer user_data)
{
    AhTray *tray = user_data;
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;
    (void)params;

    if (strcmp(method, "Activate") == 0 ||
        strcmp(method, "SecondaryActivate") == 0) {
        if (tray->on_show)
            tray->on_show(tray->user_data);
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    if (strcmp(method, "ContextMenu") == 0 || strcmp(method, "Scroll") == 0) {
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "Unknown method %s", method);
}

static GVariant *item_get_prop(GDBusConnection *c, const gchar *s,
                               const gchar *p, const gchar *i,
                               const gchar *prop, GError **e, gpointer d)
{
    AhTray *tray = d;
    (void)c;
    (void)s;
    (void)p;
    (void)i;
    (void)e;

    if (strcmp(prop, "Category") == 0)
        return g_variant_new_string("ApplicationStatus");
    if (strcmp(prop, "Id") == 0)
        return g_variant_new_string("audiohawk");
    if (strcmp(prop, "Title") == 0)
        return g_variant_new_string("AudioHawk");
    if (strcmp(prop, "Status") == 0)
        return g_variant_new_string("Active");
    if (strcmp(prop, "WindowId") == 0)
        return g_variant_new_uint32(0);
    if (strcmp(prop, "IconName") == 0)
        return g_variant_new_string(tray->icon_name ? tray->icon_name : "audiohawk");
    if (strcmp(prop, "IconPixmap") == 0)
        return tray->icon_pixmap
                   ? g_variant_ref(tray->icon_pixmap)
                   : g_variant_new("a(iiay)", NULL);
    if (strcmp(prop, "IconThemePath") == 0)
        return g_variant_new_string(tray->icon_theme_path ? tray->icon_theme_path : "");
    if (strcmp(prop, "OverlayIconName") == 0 ||
        strcmp(prop, "AttentionIconName") == 0 ||
        strcmp(prop, "AttentionMovieName") == 0)
        return g_variant_new_string("");
    if (strcmp(prop, "ToolTip") == 0)
        return g_variant_new("(sa(iiay)ss)", "", NULL,
                             "AudioHawk", "A simple audio enhancer.");
    if (strcmp(prop, "ItemIsMenu") == 0)
        return g_variant_new_boolean(FALSE);
    if (strcmp(prop, "Menu") == 0)
        return g_variant_new_object_path("/MenuBar");
    return NULL;
}

static const GDBusInterfaceVTable item_vtable = {
    .method_call = item_method,
    .get_property = item_get_prop,
};

static const GDBusInterfaceVTable menu_vtable = {
    .method_call = menu_method,
    .get_property = menu_get_prop,
};

static const gchar *item_xml =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <method name='ContextMenu'>"
    "      <arg type='i' name='x' direction='in'/>"
    "      <arg type='i' name='y' direction='in'/>"
    "    </method>"
    "    <method name='Activate'>"
    "      <arg type='i' name='x' direction='in'/>"
    "      <arg type='i' name='y' direction='in'/>"
    "    </method>"
    "    <method name='SecondaryActivate'>"
    "      <arg type='i' name='x' direction='in'/>"
    "      <arg type='i' name='y' direction='in'/>"
    "    </method>"
    "    <method name='Scroll'>"
    "      <arg type='i' name='delta' direction='in'/>"
    "      <arg type='s' name='orientation' direction='in'/>"
    "    </method>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='WindowId' type='u' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='IconThemePath' type='s' access='read'/>"
    "    <property name='OverlayIconName' type='s' access='read'/>"
    "    <property name='AttentionIconName' type='s' access='read'/>"
    "    <property name='AttentionMovieName' type='s' access='read'/>"
    "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "  </interface>"
    "</node>";

static const gchar *menu_xml =
    "<node>"
    "  <interface name='com.canonical.dbusmenu'>"
    "    <method name='GetLayout'>"
    "      <arg type='i' name='parentId' direction='in'/>"
    "      <arg type='i' name='recursionDepth' direction='in'/>"
    "      <arg type='as' name='propertyNames' direction='in'/>"
    "      <arg type='u' name='revision' direction='out'/>"
    "      <arg type='(ia{sv}av)' name='layout' direction='out'/>"
    "    </method>"
    "    <method name='GetGroupProperties'>"
    "      <arg type='ai' name='ids' direction='in'/>"
    "      <arg type='as' name='propertyNames' direction='in'/>"
    "      <arg type='a(ia{sv})' name='properties' direction='out'/>"
    "    </method>"
    "    <method name='GetProperty'>"
    "      <arg type='i' name='id' direction='in'/>"
    "      <arg type='s' name='name' direction='in'/>"
    "      <arg type='v' name='value' direction='out'/>"
    "    </method>"
    "    <method name='Event'>"
    "      <arg type='i' name='id' direction='in'/>"
    "      <arg type='s' name='eventId' direction='in'/>"
    "      <arg type='v' name='data' direction='in'/>"
    "      <arg type='u' name='timestamp' direction='in'/>"
    "    </method>"
    "    <method name='AboutToShow'>"
    "      <arg type='i' name='id' direction='in'/>"
    "      <arg type='b' name='needUpdate' direction='out'/>"
    "    </method>"
    "    <signal name='LayoutUpdated'>"
    "      <arg type='u' name='revision'/>"
    "      <arg type='i' name='parent'/>"
    "    </signal>"
    "    <property name='Version' type='u' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "  </interface>"
    "</node>";

static void register_with_watcher(AhTray *tray)
{
    if (!tray->conn || tray->registered_with_watcher)
        return;

    GError *err = NULL;
    g_dbus_connection_call_sync(
        tray->conn,
        "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        "RegisterStatusNotifierItem",
        g_variant_new("(s)", tray->bus_name),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    if (err) {
        g_debug("AudioHawk tray: watcher register failed: %s", err->message);
        g_error_free(err);
        return;
    }
    tray->registered_with_watcher = TRUE;
}

static void on_watcher_appeared(GDBusConnection *c, const gchar *name,
                                const gchar *owner, gpointer user_data)
{
    (void)c;
    (void)name;
    (void)owner;
    register_with_watcher(user_data);
}

static gboolean try_svg(char *out, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    g_vsnprintf(out, n, fmt, ap);
    va_end(ap);
    return g_file_test(out, G_FILE_TEST_IS_REGULAR);
}

/* Full hawk-on-tile SVG, not the cropped PNG raster. */
static gchar *resolve_tray_svg(void)
{
    char path[1024];
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe) {
        char *dir = g_path_get_dirname(exe);
        gboolean ok =
            try_svg(path, sizeof(path),
                    "%s/../data/icons/hicolor/scalable/apps/audiohawk.svg", dir) ||
            try_svg(path, sizeof(path), "%s/../data/appicon/audiohawk-tray.svg", dir) ||
            try_svg(path, sizeof(path), "%s/../data/icons/audiohawk-tray.svg", dir) ||
            try_svg(path, sizeof(path),
                    "%s/../share/icons/hicolor/scalable/apps/audiohawk.svg", dir);
        g_free(dir);
        g_free(exe);
        if (ok)
            return g_canonicalize_filename(path, NULL);
    }
    if (AUDIOHAWK_SOURCE_DIR[0] != '\0' &&
        (try_svg(path, sizeof(path),
                 "%s/data/icons/hicolor/scalable/apps/audiohawk.svg",
                 AUDIOHAWK_SOURCE_DIR) ||
         try_svg(path, sizeof(path), "%s/data/appicon/audiohawk-tray.svg",
                 AUDIOHAWK_SOURCE_DIR)))
        return g_strdup(path);
    if (try_svg(path, sizeof(path),
                "/usr/share/icons/hicolor/scalable/apps/audiohawk.svg") ||
        try_svg(path, sizeof(path),
                "/usr/local/share/icons/hicolor/scalable/apps/audiohawk.svg") ||
        try_svg(path, sizeof(path),
                "/usr/share/audiohawk/icons/audiohawk-tray.svg"))
        return g_strdup(path);
    return NULL;
}

static GVariant *pixbuf_to_sni(GdkPixbuf *pb)
{
    const int w = gdk_pixbuf_get_width(pb);
    const int h = gdk_pixbuf_get_height(pb);
    const int stride = gdk_pixbuf_get_rowstride(pb);
    const int ch = gdk_pixbuf_get_n_channels(pb);
    const guchar *src = gdk_pixbuf_get_pixels(pb);
    const gboolean alpha = gdk_pixbuf_get_has_alpha(pb);
    GByteArray *bytes = g_byte_array_sized_new((guint)(w * h * 4));

    for (int y = 0; y < h; ++y) {
        const guchar *row = src + y * stride;
        for (int x = 0; x < w; ++x) {
            const guchar *px = row + x * ch;
            guchar r = px[0], g = px[1], b = px[2];
            guchar a = alpha ? px[3] : 255;
            guint32 argb = GUINT32_TO_BE(((guint32)a << 24) | ((guint32)r << 16) |
                                         ((guint32)g << 8) | (guint32)b);
            g_byte_array_append(bytes, (const guint8 *)&argb, 4);
        }
    }

    GVariant *data = g_variant_new_from_data(
        G_VARIANT_TYPE("ay"), bytes->data, bytes->len, TRUE,
        (GDestroyNotify)g_byte_array_unref, bytes);
    return g_variant_new("(ii@ay)", w, h, data);
}

static GVariant *render_svg_pixmaps(const char *svg)
{
    static const int sizes[] = { 22, 32, 48, 64 };
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a(iiay)"));
    int added = 0;
    for (size_t i = 0; i < G_N_ELEMENTS(sizes); ++i) {
        GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(svg, sizes[i], sizes[i], NULL);
        if (!pb)
            continue;
        g_variant_builder_add_value(&b, pixbuf_to_sni(pb));
        g_object_unref(pb);
        added++;
    }
    if (!added) {
        g_variant_builder_clear(&b);
        return NULL;
    }
    return g_variant_ref_sink(g_variant_builder_end(&b));
}

static gchar *install_tray_theme(const char *svg)
{
    char *root = g_dir_make_tmp("audiohawk-tray-XXXXXX", NULL);
    if (!root)
        return NULL;

    char *scale_dir = g_build_filename(root, "hicolor", "scalable", "apps", NULL);
    char *png_dir = g_build_filename(root, "hicolor", "48x48", "apps", NULL);
    g_mkdir_with_parents(scale_dir, 0755);
    g_mkdir_with_parents(png_dir, 0755);

    char *scale_svg = g_build_filename(scale_dir, "audiohawk.svg", NULL);
    char *flat_svg = g_build_filename(root, "audiohawk.svg", NULL);
    gchar *contents = NULL;
    gsize len = 0;
    if (g_file_get_contents(svg, &contents, &len, NULL)) {
        g_file_set_contents(scale_svg, contents, (gssize)len, NULL);
        g_file_set_contents(flat_svg, contents, (gssize)len, NULL);
        g_free(contents);
    }

    GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(svg, 48, 48, NULL);
    if (pb) {
        char *png = g_build_filename(png_dir, "audiohawk.png", NULL);
        char *flat_png = g_build_filename(root, "audiohawk.png", NULL);
        gdk_pixbuf_save(pb, png, "png", NULL, NULL);
        gdk_pixbuf_save(pb, flat_png, "png", NULL, NULL);
        g_object_unref(pb);
        g_free(png);
        g_free(flat_png);
    }

    g_free(scale_dir);
    g_free(png_dir);
    g_free(scale_svg);
    g_free(flat_svg);
    return root;
}

static void rm_rf(const char *path)
{
    if (!path || !path[0])
        return;
    GDir *dir = g_dir_open(path, 0, NULL);
    if (dir) {
        const char *name;
        while ((name = g_dir_read_name(dir))) {
            char *child = g_build_filename(path, name, NULL);
            rm_rf(child);
            g_free(child);
        }
        g_dir_close(dir);
    }
    g_remove(path);
}

AhTray *ah_tray_create(GtkApplication *app,
                       AhTrayShowFn on_show,
                       AhTrayQuitFn on_quit,
                       gpointer user_data)
{
    AhTray *tray = g_new0(AhTray, 1);
    tray->app = app;
    tray->on_show = on_show;
    tray->on_quit = on_quit;
    tray->user_data = user_data;
    tray->icon_name = g_strdup("audiohawk");
    {
        char *svg = resolve_tray_svg();
        if (svg) {
            tray->icon_pixmap = render_svg_pixmaps(svg);
            tray->icon_theme_path = install_tray_theme(svg);
            g_free(svg);
        }
    }

    GError *err = NULL;
    tray->conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!tray->conn) {
        g_warning("AudioHawk tray: no session bus: %s",
                  err ? err->message : "?");
        g_clear_error(&err);
        ah_tray_destroy(tray);
        return NULL;
    }

    tray->bus_name = g_strdup_printf("org.kde.StatusNotifierItem-%d-1", getpid());
    tray->own_id = g_bus_own_name_on_connection(
        tray->conn, tray->bus_name, G_BUS_NAME_OWNER_FLAGS_NONE,
        NULL, NULL, NULL, NULL);
    if (!tray->own_id) {
        g_warning("AudioHawk tray: could not own %s", tray->bus_name);
        ah_tray_destroy(tray);
        return NULL;
    }

    GDBusNodeInfo *item_info = g_dbus_node_info_new_for_xml(item_xml, &err);
    if (!item_info) {
        g_warning("AudioHawk tray: item xml: %s", err->message);
        g_clear_error(&err);
        ah_tray_destroy(tray);
        return NULL;
    }
    tray->item_reg = g_dbus_connection_register_object(
        tray->conn, "/StatusNotifierItem", item_info->interfaces[0],
        &item_vtable, tray, NULL, &err);
    g_dbus_node_info_unref(item_info);
    if (!tray->item_reg) {
        g_warning("AudioHawk tray: register item: %s", err->message);
        g_clear_error(&err);
        ah_tray_destroy(tray);
        return NULL;
    }

    GDBusNodeInfo *menu_info = g_dbus_node_info_new_for_xml(menu_xml, &err);
    if (!menu_info) {
        g_warning("AudioHawk tray: menu xml: %s", err->message);
        g_clear_error(&err);
        ah_tray_destroy(tray);
        return NULL;
    }
    tray->menu_reg = g_dbus_connection_register_object(
        tray->conn, "/MenuBar", menu_info->interfaces[0],
        &menu_vtable, tray, NULL, &err);
    g_dbus_node_info_unref(menu_info);
    if (!tray->menu_reg) {
        g_warning("AudioHawk tray: register menu: %s", err->message);
        g_clear_error(&err);
        ah_tray_destroy(tray);
        return NULL;
    }

    tray->watcher_watch = g_bus_watch_name_on_connection(
        tray->conn, "org.kde.StatusNotifierWatcher",
        G_BUS_NAME_WATCHER_FLAGS_NONE,
        on_watcher_appeared, NULL, tray, NULL);
    register_with_watcher(tray);
    return tray;
}

void ah_tray_destroy(AhTray *tray)
{
    if (!tray)
        return;
    if (tray->watcher_watch)
        g_bus_unwatch_name(tray->watcher_watch);
    if (tray->own_id)
        g_bus_unown_name(tray->own_id);
    if (tray->conn) {
        if (tray->item_reg)
            g_dbus_connection_unregister_object(tray->conn, tray->item_reg);
        if (tray->menu_reg)
            g_dbus_connection_unregister_object(tray->conn, tray->menu_reg);
        g_object_unref(tray->conn);
    }
    g_free(tray->bus_name);
    g_free(tray->icon_name);
    if (tray->icon_pixmap)
        g_variant_unref(tray->icon_pixmap);
    if (tray->icon_theme_path) {
        rm_rf(tray->icon_theme_path);
        g_free(tray->icon_theme_path);
    }
    g_free(tray);
}
